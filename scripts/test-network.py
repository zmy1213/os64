#!/usr/bin/env python3
"""Real QEMU virtio/NAT Ethernet, ICMP and UDP regression, with measured echo load."""
import argparse
import importlib.util
import json
import platform
import re
import resource
import shlex
import shutil
import socket
import statistics
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location('os64_system_test', Path(__file__).with_name('test-system.py'))
system_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(system_test)
VM, digest = system_test.VM, system_test.digest


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
        reserve.bind(('127.0.0.1', 0))
        return reserve.getsockname()[1]


class EchoPeer:
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.settimeout(.1)
        self.port = self.socket.getsockname()[1]
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop_event.is_set():
            try:
                payload, peer = self.socket.recvfrom(65535)
                self.socket.sendto(payload, peer)
            except socket.timeout:
                continue
            except OSError:
                break

    def close(self):
        self.stop_event.set()
        self.socket.close()
        self.thread.join(timeout=1)


def echo(client, port, payload):
    started = time.perf_counter()
    client.sendto(payload, ('127.0.0.1', port))
    actual, _ = client.recvfrom(65535)
    if actual != payload:
        raise AssertionError(f'UDP echo changed {len(payload)} bytes')
    return time.perf_counter() - started


def pcap_counts(path):
    raw = path.read_bytes()
    if len(raw) < 24:
        raise AssertionError('Packet capture missing')
    endian = '<' if raw[:4] in (b'\xd4\xc3\xb2\xa1', b'\x4d\x3c\xb2\xa1') else '>'
    counts = {'arp': 0, 'icmp_request': 0, 'icmp_reply': 0, 'udp': 0}
    cursor = 24
    while cursor + 16 <= len(raw):
        _, _, captured, _ = struct.unpack_from(endian + '4I', raw, cursor)
        cursor += 16
        if cursor + captured > len(raw):
            raise AssertionError('Truncated capture record')
        frame = raw[cursor:cursor + captured]
        cursor += captured
        if len(frame) < 14:
            continue
        kind = struct.unpack_from('!H', frame, 12)[0]
        if kind == 0x806:
            counts['arp'] += 1
        elif kind == 0x800 and len(frame) >= 34:
            ihl = (frame[14] & 15) * 4
            if frame[23] == 17:
                counts['udp'] += 1
            elif frame[23] == 1 and len(frame) > 14 + ihl:
                if frame[14 + ihl] == 8:
                    counts['icmp_request'] += 1
                elif frame[14 + ihl] == 0:
                    counts['icmp_reply'] += 1
    if any(counts[name] == 0 for name in counts):
        raise AssertionError(f'Missing real protocol packets: {counts}')
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--label', default='', help='Separate log directory for reproducible before/after rounds')
    parser.add_argument('--require-irq', action='store_true', help='Require the real guest to report IRQ enabled')
    parser.add_argument('--cpus', type=int, choices=(1, 2, 4), default=1)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not re.fullmatch(r'[A-Za-z0-9_-]*', args.label):
        parser.error('--label permits letters, digits, underscores and hyphens')
    logs = build / ('network-test' + ('-' + args.label if args.label else ''))
    logs.mkdir(exist_ok=True)
    original_digest = digest(build / 'data.img')
    vm = None
    peer = EchoPeer()
    metrics = {'host': platform.platform(), 'backend': 'virtio-net legacy / QEMU user NAT',
               'label': args.label, 'vm_memory_mib': 128, 'vm_cpus': args.cpus, 'receive_budget': 32,
               'disk_image_sha256': digest(build / 'disk.img'),
               'payload_limit': 1200,
               'qemu': subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0]}
    manifest = build / 'build-manifest.json'
    if manifest.exists():
        configuration = json.loads(manifest.read_text())
        if configuration.get('boot_disk_sha256') == metrics['disk_image_sha256']:
            metrics['build_configuration'] = configuration
    try:
        with tempfile.TemporaryDirectory(prefix='os64-net-', dir='/tmp') as directory:
            temp = Path(directory)
            data = temp / 'data.img'
            shutil.copyfile(build / 'data_volume.bin', data)
            port = free_udp_port()
            capture = logs / 'packets.pcap'
            wrapper = temp / 'qemu-network'
            additional = ['-accel', 'tcg,thread=multi' if args.cpus > 1 else 'tcg', '-cpu', 'qemu64',
                          '-netdev', f'user,id=n0,hostfwd=udp:127.0.0.1:{port}-10.0.2.15:9000',
                          '-device', 'virtio-net-pci,netdev=n0,disable-modern=on,mac=52:54:00:12:34:56',
                          '-object', f'filter-dump,id=netcap,netdev=n0,file={capture}']
            wrapper.write_text('#!/bin/sh\nexec ' + shlex.join([args.qemu, *additional]) + ' "$@"\n')
            wrapper.chmod(0o700)
            cpu_before = resource.getrusage(resource.RUSAGE_CHILDREN)
            experiment_started = time.perf_counter()
            vm = VM(str(wrapper), build, data, logs, temp, 1, cpus=args.cpus)
            metrics['driver_setup'] = vm.shell('net', ('network_ready=1', 'network_driver=virtio-net',
                            'network_ip=10.0.2.15', 'network_echo_port=9000'), serial=True)
            if args.require_irq and 'network_irq_enabled=1' not in metrics['driver_setup']:
                raise AssertionError('IRQ regression requires actual enabled guest IRQ')
            for _ in range(3):
                vm.shell('ping 10.0.2.2', ('ping_sent=1', 'ping_received=1'), serial=True)
            vm.shell('ping 999.1.2.3', ('usage:',), serial=True)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                client.settimeout(2)
                for size in (0, 1, 257, 1200):
                    echo(client, port, bytes((index * 37) & 255 for index in range(size)))
                rtts = []
                started = time.perf_counter()
                for sequence in range(128):
                    payload = struct.pack('!I', sequence) + bytes(index % 251 for index in range(1196))
                    rtts.append(echo(client, port, payload))
                elapsed = time.perf_counter() - started
                metrics['sequential'] = {'sent': 128, 'received': 128, 'payload_bytes': 1200,
                                         'elapsed_seconds': elapsed, 'roundtrips_per_second': 128 / elapsed,
                                         'echo_payload_bytes_per_second': 128 * 1200 / elapsed,
                                         'rtt_median_ms': statistics.median(rtts) * 1000,
                                         'rtt_p95_ms': sorted(rtts)[int(len(rtts) * .95)] * 1000,
                                         'raw_rtt_seconds': rtts}
                # 有界队列允许突发丢包。测量损失，而不是虚构无限队列或无损带宽。
                received = set()
                raw_batches = []
                started = time.perf_counter()
                for batch in range(16):
                    batch_started = time.perf_counter()
                    for index in range(16):
                        sequence = batch * 16 + index
                        payload = struct.pack('!I', sequence) + bytes([sequence & 255]) * 1020
                        client.sendto(payload, ('127.0.0.1', port))
                    deadline = time.monotonic() + .15
                    while time.monotonic() < deadline:
                        client.settimeout(max(.001, deadline - time.monotonic()))
                        try:
                            payload, _ = client.recvfrom(65535)
                        except socket.timeout:
                            break
                        if len(payload) != 1024:
                            raise AssertionError('Burst payload truncated')
                        sequence = struct.unpack_from('!I', payload)[0]
                        if sequence >= 256 or payload[4:] != bytes([sequence & 255]) * 1020:
                            raise AssertionError('Burst payload corrupted')
                        received.add(sequence)
                        if all(batch * 16 + index in received for index in range(16)):
                            break
                    raw_batches.append({'batch': batch, 'sent': 16,
                                        'received': sum(batch * 16 + index in received for index in range(16)),
                                        'elapsed_seconds': time.perf_counter() - batch_started})
                elapsed = time.perf_counter() - started
                if len(received) < 64:
                    raise AssertionError(f'Burst traffic made insufficient progress: {len(received)}/256')
                metrics['burst'] = {'sent': 256, 'received': len(received), 'payload_bytes': 1024,
                                    'elapsed_seconds': elapsed, 'lost': 256 - len(received),
                                    'received_per_second': len(received) / elapsed,
                                    'received_sequences': sorted(received), 'raw_batches': raw_batches}
                client.settimeout(2)
                echo(client, port, b'after_burst_network_alive')

            free_before = int(re.search(r'mem_free_pages=(\d+)', vm.shell('mem', serial=True)).group(1))
            vm.shell(f'run /bin/udp_test 10.0.2.2 {peer.port} 32',
                     ('udp_test invalid_pointer_and_range_rejected',
                      'udp_test foreign_handle_rejected', 'udp_test sent=32 received=32',
                      'udp_test binary_zero_max_payload_ok'), serial=True, timeout=30)
            bounded_wait = vm.shell('run /bin/udp_test timeout',
                                    ('udp_test blocking_timeout_ok', 'run_exit_code=0'), serial=True)
            timeout_ticks = int(re.search(r'udp_test timeout_ticks=(\d+)', bounded_wait).group(1))
            timeout_cpu = int(re.search(r'udp_test timeout_wait_cpu=(\d+)', bounded_wait).group(1))
            if not 3 <= timeout_ticks <= 6 or (args.cpus > 1 and timeout_cpu == 0):
                raise AssertionError(f'Finite UDP timeout exceeded its idle bound or missed AP: {bounded_wait}')
            metrics['bounded_udp_timeout'] = {'requested_ms': 25, 'elapsed_ticks': timeout_ticks,
                                              'minimum_ticks': 3, 'maximum_ticks': 6,
                                              'cpu': timeout_cpu, 'raw_output': bounded_wait}
            arp_wait = vm.shell('run /bin/udp_test arp_timeout',
                                ('udp_test uncached_arp_timeout_ok', 'run_exit_code=0'), serial=True, timeout=15)
            arp_cpu = int(re.search(r'udp_test arp_wait_cpu=(\d+)', arp_wait).group(1))
            if args.cpus > 1 and arp_cpu == 0:
                raise AssertionError(f'Uncached ARP did not wait on an AP: {arp_wait}')
            metrics['uncached_arp_timeout'] = {'cpu': arp_cpu, 'raw_output': arp_wait}
            mixed = []
            for workers in (0, 3):
                started = time.perf_counter()
                output = vm.shell(f'run /bin/udp_mixed 10.0.2.2 {peer.port} {workers} 64 100000000',
                                  ('udp_mixed receiver=ok', 'udp_mixed correctness=ok', 'run_exit_code=0'),
                                  serial=True, timeout=90)
                fields = {name: int(value) for name, value in re.findall(
                    r'(?m)^udp_mixed ([a-z0-9_]+)=(\d+)\r?$', output)}
                if fields.get('sent') != 64 or fields.get('received') != 64 or fields.get('online_cpus') != args.cpus:
                    raise AssertionError(f'Blocking mixed load lost packets or CPUs: {output}')
                cpu_progress = [{'cpu': int(cpu), 'user_dispatches': int(dispatches), 'user_ticks': int(ticks)}
                                for cpu, dispatches, ticks in re.findall(
                                    r'udp_mixed cpu=(\d+) user_dispatches=(\d+) user_ticks=(\d+)', output)]
                if args.cpus > 1 and fields.get('receiving_cpu', 0) == 0:
                    raise AssertionError(f'UDP blocking receive was not exercised on an AP: {output}')
                if workers and args.cpus > 1 and sum(item['user_ticks'] > 0 for item in cpu_progress) < 2:
                    raise AssertionError(f'Compute did not run on multiple CPUs: {output}')
                mixed.append({'compute_workers': workers, 'host_wall_seconds': time.perf_counter() - started,
                              'guest_fields': fields, 'cpu_progress': cpu_progress, 'raw_output': output})
            metrics['mixed_compute_network'] = mixed
            for _ in range(8):
                vm.shell('run /bin/udp_test leak', ('udp_test exit_cleanup_requested',), serial=True)
            free_after = int(re.search(r'mem_free_pages=(\d+)', vm.shell('mem', serial=True)).group(1))
            if free_after != free_before:
                raise AssertionError('Repeated user UDP processes leaked physical pages')
            vm.shell('run /bin/echo network_survived', ('network_survived',), serial=True)
            metrics['guest_counters'] = vm.shell('net', ('network_device_errors=0',), serial=True)
            vm.shutdown()
            vm = None
            cpu_after = resource.getrusage(resource.RUSAGE_CHILDREN)
            wall_seconds = time.perf_counter() - experiment_started
            user_seconds = cpu_after.ru_utime - cpu_before.ru_utime
            system_seconds = cpu_after.ru_stime - cpu_before.ru_stime
            # 此区间只启动一个子进程 QEMU。统计其从启动到 shutdown 的全部 CPU 时间，
            # 不把宿主 Python echo 服务和测试程序的 CPU 时间算入 QEMU。
            metrics['qemu_process_cpu'] = {'user_seconds': user_seconds, 'system_seconds': system_seconds,
                                         'total_seconds': user_seconds + system_seconds,
                                         'experiment_wall_seconds': wall_seconds,
                                         'one_core_percent': 100 * (user_seconds + system_seconds) / wall_seconds,
                                         'scope': 'whole regression, QEMU process only; not isolated burst CPU'}
            metrics['captured_packets'] = pcap_counts(capture)
            (logs / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
            print('Network regression passed: real ARP/ICMP/UDP, blocking UDP, compute coexistence, ownership and exit cleanup.')
            print(f"Sequential echo: {metrics['sequential']['roundtrips_per_second']:.1f} roundtrips/s; "
                  f"median {metrics['sequential']['rtt_median_ms']:.2f} ms; burst {len(received)}/256 received.")
            print(f'Logs, capture and measured metrics: {logs}')
    except Exception:
        if vm is not None:
            print(vm.text()[-8000:])
        raise
    finally:
        if vm is not None:
            vm.stop()
        peer.close()
        if digest(build / 'data.img') != original_digest:
            raise AssertionError('Network regression modified the user data disk')


if __name__ == '__main__':
    main()
