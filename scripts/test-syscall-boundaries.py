#!/usr/bin/env python3
"""Check new syscall argument boundaries without repeating the full IPC suite."""
import argparse
import importlib.util
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    logs = build / 'syscall-boundary-test'
    logs.mkdir(exist_ok=True)
    original_digest = system.digest(build / 'data.img')
    vm = None
    try:
        with tempfile.TemporaryDirectory(prefix='os64-boundaries-', dir='/tmp') as directory:
            temp = Path(directory)
            data = temp / 'data.img'
            shutil.copyfile(build / 'data_volume.bin', data)
            try:
                vm = system.VM(args.qemu, build, data, logs, temp, 1)
                # Materialize the live Shell's three shared terminal descriptions
                # before measuring; their lifetime belongs to the Shell itself.
                vm.shell('echo boundary_warmup', ('boundary_warmup',), serial=True)
                before = resources(vm)
                disk_before = system.digest(data)
                markers = ('badptr replace path rejected',
                           'badptr replace buffer rejected',
                           'badptr replace size rejected',
                           'badptr IPC boundaries rejected',
                           'badptr performance boundaries rejected',
                           'badptr UDP boundaries rejected')
                for _ in range(3):
                    vm.shell('run /bin/badptr', markers,
                             ('user_fault_vector=', 'badptr: unexpected result'), serial=True)
                vm.shell('echo survived_boundary_checks', ('survived_boundary_checks',), serial=True)
                if resources(vm) != before:
                    raise AssertionError('Rejected syscall arguments leaked pages or kernel allocations')
                if system.digest(data) != disk_before:
                    raise AssertionError('Rejected syscall arguments changed the data volume')
                vm.shutdown()
                vm = None
                print('Syscall boundary regression passed: pipe/dup, performance/log, '
                      'UDP pointers and integer bounds; resources and disk unchanged. '
                      f'Logs: {logs}')
            except Exception:
                if vm:
                    print(vm.text()[-8000:])
                raise
            finally:
                if vm:
                    vm.stop()
                    vm = None
    finally:
        if system.digest(build / 'data.img') != original_digest:
            raise AssertionError('Boundary regression changed the user data disk')


if __name__ == '__main__':
    main()
