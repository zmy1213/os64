#!/usr/bin/env python3
"""Isolated real-guest CPU/FPU checks and optional same-TCG Linux comparison.

No user data image is mounted. Each guest runs on one qemu64 vCPU with 128 MiB.
Linux receives the exact same bench.cpp and compute loop with an ABI adapter.
"""
import argparse
import hashlib
import json
import math
import platform
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path

SCENARIOS = [('single', 1, 480000000, 0), ('four', 4, 120000000, 0),
             ('twelve', 12, 40000000, 0), ('yield', 4, 120000000, 65536),
             ('pipe', 0, 33554432, 0)]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def symbol_bytes(path, wanted):
    """Read ELF64 symbol bytes without depending on a particular objdump."""
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise AssertionError(f'Expected little-endian ELF64: {path}')
    section_offset = struct.unpack_from('<Q', data, 40)[0]
    section_size, section_count = struct.unpack_from('<HH', data, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, section_offset + i * section_size)
                for i in range(section_count)]
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, _, _, index, value, size = struct.unpack_from('<IBBHQQ', data, offset)
            actual_name = names[name:names.find(b'\0', name)].decode()
            if actual_name == wanted:
                target = sections[index]
                start = target[4] + value - target[3]
                return data[start:start + size]
    raise AssertionError(f'Symbol {wanted} missing: {path}')

class SerialVM:
    def __init__(self, qemu, build, assets, temporary, logs, guest, boot, data_image=None):
        self.guest = guest
        self.prompt = 'os64 % ' if guest == 'os64' else 'linux-bench % '
        self.log = logs / f'{guest}-{boot}.serial.log'
        self.error = logs / f'{guest}-{boot}.qemu.log'
        self.log.write_bytes(b'')
        serial = temporary / 'serial.sock'
        serial.unlink(missing_ok=True)
        self.arguments = [str(qemu), '-accel', 'tcg', '-cpu', 'qemu64', '-smp', '1',
                          '-m', '128M', '-display', 'none', '-no-reboot', '-monitor', 'none', '-nic', 'none',
                          '-chardev', f'socket,id=com1,path={serial},server=on,wait=off,logfile={self.log}',
                          '-serial', 'chardev:com1']
        if guest == 'os64':
            data = data_image or temporary / 'data.img'
            if data_image is None:
                shutil.copyfile(build / 'data_volume.bin', data)
            self.arguments += ['-boot', 'a', '-drive', f'format=raw,file={build / "disk.img"},if=floppy,index=0',
                               '-drive', f'format=raw,file={data},if=ide,index=0',
                               '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
        else:
            self.arguments += ['-kernel', str(assets / 'vmlinuz-virt'),
                               '-initrd', str(build / 'linux-benchmark/initramfs-benchmark.gz'),
                               '-append', 'console=ttyS0 rdinit=/init panic=-1 quiet']
        self.error_handle = self.error.open('wb')
        self.process = subprocess.Popen(self.arguments, stdout=self.error_handle, stderr=self.error_handle)
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(1)
        deadline = time.monotonic() + 15
        while True:
            try:
                self.socket.connect(str(serial))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() > deadline or self.process.poll() is not None:
                    self.stop()
                    raise RuntimeError(f'QEMU startup failed; see {self.error}')
                time.sleep(.03)
        self.stopped = threading.Event()
        self.reader = threading.Thread(target=self.drain, daemon=True)
        self.reader.start()
        try:
            if guest == 'os64':
                self.wait('shell_thread_started_tid=', timeout=90)
                self.wait(self.prompt, self.text().rfind('shell_thread_started_tid='), timeout=15)
            else:
                self.wait(self.prompt, timeout=90)
            if guest == 'os64' and 'storage_backend=ata-pio' not in self.text():
                raise AssertionError('OS64 did not mount the isolated persistent ATA image')
            if guest == 'linux' and 'linux_bench_ready' not in self.text():
                raise AssertionError('Linux did not run the isolated benchmark init')
        except Exception:
            self.stop()
            raise

    def drain(self):
        while not self.stopped.is_set():
            try:
                if not self.socket.recv(65536):
                    return
            except socket.timeout:
                continue
            except OSError:
                return

    def text(self):
        return self.log.read_text(errors='replace')

    def wait(self, marker, start=0, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            output = self.text()[start:]
            if marker in output:
                return output
            if self.process.poll() is not None:
                raise RuntimeError(f'{self.guest} exited while waiting for {marker!r}; see {self.log}')
            time.sleep(.01)
        raise RuntimeError(f'{self.guest} timeout for {marker!r}; see {self.log}')

    def shell(self, command, timeout=60):
        start = len(self.text())
        self.send_line(command)
        sent = time.monotonic()
        output = self.wait(self.prompt, start, timeout)
        wall_seconds = time.monotonic() - sent
        return output, wall_seconds

    def send_line(self, command):
        for char in command + '\n':
            self.socket.sendall(char.encode())
            time.sleep(.005)

    def shutdown(self):
        self.send_line('shutdown')
        self.process.wait(timeout=15)
        self.stop()

    def bench(self, workers, iterations, yielding):
        prefix = 'run /bin/' if self.guest == 'os64' else '/bin/'
        command = f'{prefix}bench {workers} {iterations} {yielding}' if workers else f'{prefix}bench_ipc {iterations}'
        output, wall = self.shell(command)
        expected_exit = 'run_exit_code=0' if self.guest == 'os64' else 'linux_exit_code=0'
        label = 'bench' if workers else 'ipc'
        if expected_exit not in output or f'{label} correctness=ok' not in output:
            raise AssertionError(f'Benchmark failed: {output}')
        fields = {key: int(value) for key, value in re.findall(rf'{label} ([a-z0-9_]+)=(\d+)', output)}
        if (workers and (fields.get('workers') != workers or fields.get('iterations_per_worker') != iterations)) or (not workers and fields.get('bytes') != iterations):
            raise AssertionError(f'Benchmark output mismatch: {output}')
        if fields['elapsed_ticks'] <= 0:
            raise AssertionError('Workload too short for the 100 Hz measurement clock')
        if self.guest == 'os64' and fields['free_pages_before'] != fields['free_pages_after']:
            raise AssertionError(f'Physical page leak: {output}')
        fields['seconds'] = fields['elapsed_ticks'] / fields['timer_hz']
        fields['million_updates_per_second'] = workers * iterations / fields['seconds'] / 1000000 if workers else 0
        fields['MiB_per_second'] = iterations / fields['seconds'] / 1048576 if not workers else 0
        fields['host_command_seconds'] = wall
        return fields

    def stop(self):
        if hasattr(self, 'process') and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        if hasattr(self, 'stopped'):
            self.stopped.set()
        if hasattr(self, 'socket'):
            self.socket.close()
        if hasattr(self, 'reader'):
            self.reader.join(timeout=2)
        self.error_handle.close()

def percentile(values, p):
    values = sorted(values)
    return values[max(0, math.ceil(p * len(values)) - 1)]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--samples', type=int, default=31)
    parser.add_argument('--compare-linux', action='store_true')
    parser.add_argument('--quick', action='store_true', help='Only correctness/FPU/resource stress; no timing report')
    args = parser.parse_args()
    if args.samples < 3:
        parser.error('Use at least three samples')
    b = args.build_dir.resolve()
    assets = (args.assets or b / 'benchmark-deps').resolve()
    logs = b / 'performance-results'
    logs.mkdir(parents=True, exist_ok=True)
    original = digest(b / 'data.img') if (b / 'data.img').exists() else None
    manifest = {'host': platform.platform(), 'qemu': subprocess.check_output([str(args.qemu), '--version'], text=True).splitlines()[0],
                'benchmark_harness_sha256': digest(Path(__file__)),
                'configuration': {'accelerator': 'tcg', 'cpu': 'qemu64', 'vcpus': 1, 'memory_MiB': 128},
                'kernel_optimization': 'O2 (build must use default KERNEL_OPT_LEVEL=2)',
                'user_optimization': 'Os, freestanding, no automatic SSE',
                'samples_per_scenario': args.samples, 'workload_total_updates': 480000000,
                'kernel_elf_sha256': digest(b / 'kernel.elf'), 'bench_elf_sha256': digest(b / 'user/bench.elf'),
                'raw_samples': [], 'summary': {}}
    build_manifest = b / 'build-manifest.json'
    if build_manifest.exists():
        built = json.loads(build_manifest.read_text())
        if built['boot_disk_sha256'] != digest(b / 'disk.img') or built['user_binaries']['bench.elf'] != digest(b / 'user/bench.elf'):
            raise AssertionError('Build manifest no longer matches the benchmark binaries')
        manifest['build_manifest'] = built
        manifest['kernel_optimization'] = built['kernel_optimization']
        manifest['user_optimization'] = built['user_optimization']
    elif not args.quick:
        raise AssertionError('Build manifest missing: run make build before measuring')
    if args.compare_linux:
        if manifest['build_manifest']['kernel_optimization'] != '-O2' or manifest['build_manifest']['user_optimization'] != '-Os':
            raise AssertionError('This comparison requires OS64 kernel -O2 and both user programs -Os')
        manifest['linux_assets'] = json.loads((b / 'linux-benchmark/assets.json').read_text())
        manifest['linux_build'] = json.loads((b / 'linux-benchmark/build-info.json').read_text())
        if manifest['linux_build']['compiler'] != manifest['build_manifest']['compiler'] or \
           not all(flag in manifest['linux_build']['flags'] for flag in ['-Os', '-fno-omit-frame-pointer', '-mgeneral-regs-only']):
            raise AssertionError('Guest user programs must use the same compiler and documented optimization/register flags')
        os64_loop = symbol_bytes(b / 'user/bench.o', '_Z13bench_computemm')
        linux_loop = symbol_bytes(b / 'linux-benchmark/bench.o', '_Z13bench_computemm')
        if os64_loop != linux_loop:
            raise AssertionError('Compute loop instruction bytes differ between guest builds')
        manifest['identical_compute_loop'] = {'bytes': len(os64_loop), 'sha256': hashlib.sha256(os64_loop).hexdigest()}
    with tempfile.TemporaryDirectory(prefix='os64-perf-') as temporary_name:
        temporary = Path(temporary_name)
        vm = None
        try:
            vm = SerialVM(args.qemu, b, assets, temporary, logs, 'os64', 'checks')
            output, _ = vm.shell('run /bin/fp_test', timeout=90)
            if 'fp_test SSE_x87_isolation_ok' not in output or 'run_exit_code=0' not in output:
                raise AssertionError(f'Floating-point state isolation failed: {output}')
            for command, marker in [('run /bin/perf_test', 'perf_test snapshot_logs_bad_pointers_ok'),
                                    ('run /bin/sched_test', 'sched_test eight_workers_progress_before_first_exit_ok')]:
                output, _ = vm.shell(command, timeout=90)
                if marker not in output or 'run_exit_code=0' not in output:
                    raise AssertionError(f'{command}: {output}')
            for repeat in range(4):
                for workers in [8, 12]:
                    vm.bench(workers, 2000000, 65536 if repeat & 1 else 0)
            vm.bench(0, 33554432, 0)
            output, _ = vm.shell('perf')
            if 'perf_abi=1' not in output:
                raise AssertionError(f'Performance snapshot unavailable: {output}')
            output, _ = vm.shell('dmesg')
            spawned = re.findall(r'debug process spawn pid=\d+', output)
            exited = re.findall(r'debug process exit pid=\d+', output)
            if not spawned or not exited or 'log_overwritten=' not in output:
                raise AssertionError('Structured boot logs unavailable')
            saved_markers = [spawned[-1], exited[-1]]
            output, _ = vm.shell('logsave /performance.log')
            if 'logsave ok' not in output:
                raise AssertionError(f'Log persistence failed: {output}')
            vm.shutdown(); vm = None
            vm = SerialVM(args.qemu, b, assets, temporary, logs, 'os64', 'log-cold', data_image=temporary / 'data.img')
            output, _ = vm.shell('cat /performance.log')
            if not all(marker in output for marker in saved_markers):
                raise AssertionError('Cold boot lost the saved spawn/exit log records')
            vm.shutdown(); vm = None
            manifest['correctness_checks'] = {'floating_state_isolation': True, 'eight_worker_timer_progress': True,
                                              'eight_twelve_worker_page_recovery': True, 'pipe_32MiB': True,
                                              'performance_log_bad_pointers': True, 'logsave_cold_boot': True}
            print('CPU/FPU isolation, 8/12-process stress, timer progress, 32 MiB pipe, ABI boundaries and cold-boot logsave passed.', flush=True)
            if not args.quick:
                # ABBA blocks balance slow host-temperature/load drift; only one
                # guest runs at a time. Each block warms every scenario once.
                first = (args.samples + 1) // 2
                second = args.samples - first
                guests = [('os64', first), ('linux', first), ('linux', second), ('os64', second)] if args.compare_linux else [('os64', args.samples)]
                for block, (guest, count) in enumerate(guests):
                    vm = SerialVM(args.qemu, b, assets, temporary, logs, guest, block)
                    manifest.setdefault('qemu_arguments', {})[f'{guest}-{block}'] = vm.arguments
                    if guest == 'linux':
                        manifest['linux_kernel'] = re.search(r'linux_kernel=([^\r\n]+)', vm.text()).group(1)
                    for name, workers, iterations, yielding in SCENARIOS:
                        vm.bench(workers, iterations, yielding) # warm-up is omitted
                        for sample in range(count):
                            measured = vm.bench(workers, iterations, yielding)
                            measured.update(guest=guest, scenario=name, block=block, sample=sample)
                            manifest['raw_samples'].append(measured)
                        print(f'{guest} block {block}: {name}, {count} measured samples complete', flush=True)
                    vm.stop(); vm = None
                for guest in ['os64', 'linux'] if args.compare_linux else ['os64']:
                    summary = {}
                    for name, _, _, _ in SCENARIOS:
                        rows = [row for row in manifest['raw_samples'] if row['guest'] == guest and row['scenario'] == name]
                        seconds = [row['seconds'] for row in rows]
                        summary[name] = {'samples': len(rows), 'p50_seconds': percentile(seconds, .5),
                                         'p99_seconds': percentile(seconds, .99), 'min_seconds': min(seconds),
                                         'max_seconds': max(seconds),
                                         'p50_million_updates_per_second': percentile([row['million_updates_per_second'] for row in rows], .5),
                                         'p50_MiB_per_second': percentile([row['MiB_per_second'] for row in rows], .5)}
                    manifest['summary'][guest] = summary
        finally:
            if vm is not None:
                print(vm.text()[-6000:])
                vm.stop()
    if original is not None and digest(b / 'data.img') != original:
        raise AssertionError('Benchmark changed the user persistent data disk')
    manifest['user_data_unchanged'] = True
    (logs / 'results.json').write_text(json.dumps(manifest, indent=2) + '\n')
    lines = ['# Single-vCPU TCG benchmark', '', f'QEMU: {manifest["qemu"]}', '',
             '| Guest | Scenario | Samples | p50 seconds | p99 seconds | p50 million updates/s | p50 MiB/s |',
             '|---|---|---:|---:|---:|---:|---:|']
    for guest, scenarios in manifest['summary'].items():
        for name, values in scenarios.items():
            lines.append(f'| {guest} | {name} | {values["samples"]} | {values["p50_seconds"]:.2f} | {values["p99_seconds"]:.2f} | {values["p50_million_updates_per_second"]:.2f} | {values["p50_MiB_per_second"]:.2f} |')
    lines += ['', 'CPU scenarios complete 480 million integer updates; pipe transfers and verifies 32 MiB through a 4096-byte pipe. Same source files, both -Os; OS64 kernel -O2.',
              '100 Hz measurement quantization is 10 ms. At 31 samples p99 is the maximum; this is an exploratory tail, not a production p99 estimate.',
              'Single qemu64 vCPU, 128 MiB, TCG, one guest running at a time, ABBA guest order, one warm-up per block/scenario.',
              'OS64 all-thread context switch counts and Linux SELF+reaped CHILDREN rusage counters have different scopes; do not compare them directly.',
              'These are emulator-specific CPU/scheduling measurements, not a claim that OS64 is generally faster than Linux.',
              'Raw samples, binary/asset hashes and exact QEMU arguments: results.json. All temporary guest disks were isolated; original data.img unchanged.', '']
    (logs / 'REPORT.md').write_text('\n'.join(lines))
    print(f'Performance evidence: {logs / "results.json"}', flush=True)

if __name__ == '__main__':
    main()
