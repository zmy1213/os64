#!/usr/bin/env python3
"""Check mixed compute/IPC on 1/2/4 vCPUs using disposable data images.

This is a correctness and lifetime regression, not a throughput benchmark.
CPU observations and the kernel's dispatch counters distinguish real SMP
execution from a single CPU switching between many processes.
"""
import argparse
import importlib.util
import json
import re
import shutil
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'os64_system', Path(__file__).with_name('test-system.py'))
system = importlib.util.module_from_spec(spec)
spec.loader.exec_module(system)


def resources(vm):
    pages = int(re.search(r'mem_free_pages=(\d+)', vm.shell('mem', serial=True)).group(1))
    heap = int(re.search(r'heap_active_allocations=(\d+)', vm.shell('heap', serial=True)).group(1))
    return pages, heap


def cooperation(vm, cpus, workers, rounds):
    output = vm.shell(f'run /bin/coop_test {workers} {rounds}',
                      ('coop_test blocked_writer_close_epipe_ok', 'coop_test records_atomic_compute_ok'),
                      ('user_fault_vector=', 'coop_test: data'),
                      timeout=120, serial=True)
    fields = dict((key, int(value)) for key, value in
                  re.findall(r'coop_test (\w+)=(\d+)', output))
    required = ('online_cpus', 'online_mask', 'cpu_mask', 'dispatch_cpu_mask',
                'bytes', 'records', 'elapsed_ticks')
    if any(key not in fields for key in required):
        raise AssertionError(f'Missing cooperation diagnostics: {output!r}')
    online = fields['online_mask']
    if fields['online_cpus'] != cpus or bin(online).count('1') != cpus or online & ~15:
        raise AssertionError(f'CPU startup disagrees with requested -smp {cpus}: {fields}')
    if fields['cpu_mask'] & ~online or fields['dispatch_cpu_mask'] & ~online:
        raise AssertionError(f'Program reported execution on an offline CPU: {fields}')
    if not fields['cpu_mask'] or not fields['dispatch_cpu_mask']:
        raise AssertionError(f'No CPU execution evidence: {fields}')
    # The parent plus >=4 producers should cover all online CPUs. On one CPU,
    # mask=1 is expected and means concurrency rather than parallel execution.
    if (fields['cpu_mask'] | fields['dispatch_cpu_mask']) != online:
        raise AssertionError(f'Not all online CPUs dispatched user work: {fields}')
    expected_bytes = workers * ((rounds // 2) * (257 + 4096) + (257 if rounds % 2 else 0))
    if fields['bytes'] != expected_bytes or fields['records'] != workers * rounds:
        raise AssertionError(f'Missing or duplicate pipe records: {fields}')
    return dict(workers=workers, rounds=rounds, **fields)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--cpus', type=int, nargs='+', default=[1, 2, 4], choices=[1, 2, 4])
    parser.add_argument('--repeats', type=int, default=4)
    parser.add_argument('--rounds', type=int, default=2048)
    args = parser.parse_args()
    if not 1 <= args.repeats <= 32 or not 1 <= args.rounds <= 4096:
        parser.error('repeats must be 1..32; rounds must be 1..4096')
    build = args.build_dir.resolve()
    logs = build / 'cooperation-test'
    logs.mkdir(exist_ok=True)
    original = system.digest(build / 'data.img')
    results = []
    vm = None
    try:
        for cpus in args.cpus:
            with tempfile.TemporaryDirectory(prefix='os64-coop-', dir='/tmp') as directory:
                temp = Path(directory)
                image = temp / 'data.img'
                shutil.copyfile(build / 'data_volume.bin', image)
                try:
                    vm = system.VM(args.qemu, build, image, logs, temp, f'{cpus}cpu', cpus=cpus)
                    vm.shell('echo cooperation_warmup', ('cooperation_warmup',), serial=True)
                    before = resources(vm)
                    disk_before = system.digest(image)
                    results.append(dict(cpus=cpus, **cooperation(vm, cpus, 4, args.rounds)))
                    if resources(vm) != before:
                        raise AssertionError('Large mixed compute/pipe test leaked pages or heap objects')
                    for _ in range(args.repeats):
                        results.append(dict(cpus=cpus, **cooperation(vm, cpus, 8, 128)))
                        if resources(vm) != before:
                            raise AssertionError('Repeated eight-process cooperation leaked resources')
                    # The existing suite covers EOF/EPIPE, automatic close on
                    # exit, bounded blocking transfers and shared file offsets.
                    vm.shell('run /bin/pipe_test',
                             ('pipe_test eof_epipe_ok', 'pipe_test blocking_32k_ok',
                              'pipe_test exit_wakes_eof_ok', 'pipe_test inherited_offset_ok',
                              'pipe_test multiwriter_atomic_ok', 'pipe_test dup_validation_ok'),
                             timeout=45, serial=True)
                    vm.shell('run /bin/badptr',
                             ('badptr IPC boundaries rejected', 'badptr UDP boundaries rejected',
                              'badptr SMP boundaries rejected', 'badptr sleep boundaries rejected'),
                             ('user_fault_vector=', 'badptr: unexpected result'), serial=True)
                    vm.shell('echo cooperation_survived', ('cooperation_survived',), serial=True)
                    if resources(vm) != before or system.digest(image) != disk_before:
                        raise AssertionError('IPC/boundary cleanup leaked resources or modified the disk')
                    vm.shutdown()
                    vm = None
                except Exception:
                    if vm:
                        print(vm.text()[-9000:])
                    raise
                finally:
                    if vm:
                        vm.stop()
                        vm = None
        (logs / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(f'Cooperation regression passed for vCPUs={args.cpus}: exact records and computation, '
              f'CPU dispatch evidence, EOF/EPIPE, boundaries and resource recovery. Logs: {logs}')
    finally:
        if system.digest(build / 'data.img') != original:
            raise AssertionError('Cooperation regression changed the user data disk')


if __name__ == '__main__':
    main()
