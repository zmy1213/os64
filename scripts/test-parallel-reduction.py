#!/usr/bin/env python3
"""Run the illustrated bounded work-pool example in real 1/2/4-vCPU guests."""
import argparse
import importlib.util
import json
import re
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location('performance', Path(__file__).with_name('test-performance.py'))
performance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(performance)


def expected(items):
    """Independent host calculation of the complete 64-bit affine reduction."""
    modulus = 1 << 64
    multiply, add = 1, 0
    for _ in range(64):
        multiply = multiply * 6364136223846793005 % modulus
        add = (add * 6364136223846793005 + 1442695040888963407) % modulus
    seed_sum = items * 123 + items * (items - 1) // 2
    return (multiply * seed_sum + add * items) % modulus


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--cpus', type=int, nargs='+', choices=[1, 2, 4], default=[1, 2, 4])
    args = parser.parse_args()
    build = args.build_dir.resolve()
    logs = build / 'parallel-reduction-results'
    logs.mkdir(exist_ok=True)
    original = performance.digest(build / 'data.img') if (build / 'data.img').exists() else None
    results = []
    # Uneven chunks, empty chunks, more consumers than jobs, and repeat cleanup.
    cases = [(1, 64, 1048576), (4, 64, 1048576), (12, 64, 1048576),
             (4, 7, 10003), (12, 1, 17), (4, 64, 1), (12, 64, 1048576)]
    for cpus in args.cpus:
        with tempfile.TemporaryDirectory(prefix='os64-reduce-') as directory:
            vm = None
            try:
                vm = performance.SerialVM(args.qemu, build, build / 'benchmark-deps',
                                          Path(directory), logs, 'os64', f'{cpus}cpu', cpus=cpus)
                for workers, jobs, items in cases:
                    output, elapsed = vm.shell(f'run /bin/parallel_reduce {workers} {jobs} {items}', timeout=90)
                    if 'parallel_reduce tasks_once_full64_resources_ok' not in output or 'run_exit_code=0' not in output:
                        raise AssertionError(f'{cpus} CPUs: {output}')
                    fields = {key: int(value) for key, value in re.findall(r'parallel_reduce (\w+)=(\d+)', output)}
                    if (fields['workers'], fields['jobs'], fields['items'], fields['online_cpus']) != (workers, jobs, items, cpus):
                        raise AssertionError(f'Configuration mismatch: {fields}')
                    if fields['checksum'] != expected(items) or fields['expected'] != expected(items) or fields['completed_jobs'] != jobs:
                        raise AssertionError(f'Host full-width checksum or task count mismatch: {fields}')
                    if fields['free_pages_before'] != fields['free_pages_after'] or fields['heap_bytes_before'] != fields['heap_bytes_after']:
                        raise AssertionError(f'Resource leak: {fields}')
                    workers_seen = [(int(worker), int(cpu), int(count)) for worker, cpu, count in
                                    re.findall(r'parallel_reduce worker=(\d+) cpu=(\d+) jobs=(\d+)', output)]
                    if (len(workers_seen) != workers or {worker for worker, _, _ in workers_seen} != set(range(workers)) or
                            any(cpu >= cpus for _, cpu, _ in workers_seen) or sum(count for _, _, count in workers_seen) != jobs):
                        raise AssertionError(f'Worker accounting mismatch: {workers_seen}')
                    mask = 0
                    # Empty tasks deliberately do not contribute to work_cpu_mask.
                    if items >= jobs:
                        for _, cpu, count in workers_seen:
                            if count:
                                mask |= 1 << cpu
                        if mask != fields['work_cpu_mask']:
                            raise AssertionError(f'CPU accounting mismatch: {fields}')
                    if fields['work_cpu_mask'] == 0 or fields['work_cpu_mask'] >> cpus:
                        raise AssertionError(f'Invalid computation CPU mask: {fields}')
                    results.append(dict(cpus=cpus, workers=workers, jobs=jobs, items=items,
                                        fields=fields, worker_distribution=workers_seen, host_shell_seconds=elapsed))
                for arguments in ['0 64 1', '13 64 1', '4 0 1', '4 65 1', '4 64 0',
                                  '4 64 16777217', '18446744073709551616 64 1', '-1 64 1', '4 7', 'worker 0 3 3']:
                    output, _ = vm.shell(f'run /bin/parallel_reduce {arguments}', timeout=30)
                    if 'run_exit_code=1' not in output:
                        raise AssertionError(f'Invalid arguments accepted: {arguments}: {output}')
                # Existing cooperating processes still work after repeated cleanup.
                output, _ = vm.shell('run /bin/pipe_test', timeout=60)
                if 'run_exit_code=0' not in output:
                    raise AssertionError(f'IPC regression after reduction: {output}')
                vm.shutdown()
                vm = None
            finally:
                if vm is not None:
                    print(vm.text()[-6000:])
                    vm.stop()
        print(f'{cpus} CPUs: 7 reductions, 10 invalid-argument cases and existing pipe test passed.', flush=True)
    if original is not None and performance.digest(build / 'data.img') != original:
        raise AssertionError('Test changed existing data.img')
    (logs / 'results.json').write_text(json.dumps({
        'kernel_sha256': performance.digest(build / 'kernel.elf'),
        'program_sha256': performance.digest(build / 'user/parallel_reduce.elf'),
        'source_sha256': performance.digest(Path(__file__).parents[1] / 'user/programs/parallel_reduce.cpp'),
        'harness_sha256': performance.digest(Path(__file__)),
        'checks': results, 'invalid_cases_per_cpu': 10, 'existing_data_unchanged': True,
        'timing_note': 'Single-run correctness timings include shell, spawn, IPC and wait. Not a Linux comparison or performance ranking.'
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
