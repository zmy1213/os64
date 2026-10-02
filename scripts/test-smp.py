#!/usr/bin/env python3
"""True user SMP, lifetime and FP regressions on isolated 1/2/4-vCPU disks."""
import argparse
import importlib.util
import json
import re
import tempfile
from pathlib import Path

spec=importlib.util.spec_from_file_location('performance',Path(__file__).with_name('test-performance.py'))
performance=importlib.util.module_from_spec(spec)
spec.loader.exec_module(performance)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--qemu',type=Path,required=True)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--cpus',type=int,nargs='+',choices=[1,2,4],default=[1,2,4])
    parser.add_argument('--repeats',type=int,default=4)
    args=parser.parse_args()
    if not 1<=args.repeats<=16: parser.error('repeats must be 1..16')
    build=args.build_dir.resolve()
    logs=build/'smp-test-results';logs.mkdir(exist_ok=True)
    original=performance.digest(build/'data.img') if (build/'data.img').exists() else None
    results=[]
    for cpus in args.cpus:
        with tempfile.TemporaryDirectory(prefix='os64-smp-') as directory:
            vm=None
            try:
                vm=performance.SerialVM(args.qemu,build,build/'benchmark-deps',Path(directory),logs,'os64',f'{cpus}cpu',cpus=cpus)
                for repeat in range(args.repeats):
                    output,_=vm.shell('run /bin/smp_test',timeout=120)
                    if 'smp_test twelve_workers_pin_compute_wake_resources_ok' not in output or 'run_exit_code=0' not in output:
                        raise AssertionError(f'{cpus} CPUs, repeat {repeat}: {output}')
                    fields={key:int(value) for key,value in re.findall(r'smp_test (\w+)=(\d+)',output)}
                    if fields['online_cpus']!=cpus or fields['worker_cpu_mask']!=(1<<cpus)-1:
                        raise AssertionError(f'Requested -smp {cpus} did not execute users on every CPU: {fields}')
                    if fields['free_pages_before']!=fields['free_pages_after']:
                        raise AssertionError(f'SMP page leak: {fields}')
                    results.append(dict(cpus=cpus,repeat=repeat,**fields))
                for program,marker in [('fp_test','fp_test SSE_x87_isolation_ok'),
                                       ('sched_test','sched_test eight_workers_progress_before_first_exit_ok'),
                                       ('pipe_test','pipe_test'),('badptr','badptr')]:
                    output,_=vm.shell(f'run /bin/{program}',timeout=120)
                    if marker not in output or 'run_exit_code=0' not in output:
                        raise AssertionError(f'{cpus} CPUs, {program}: {output}')
                for workers in (4,12): vm.bench(workers,40000000,0)
                vm.bench(0,33554432,0)
                vm.shutdown();vm=None
            finally:
                if vm is not None:
                    print(vm.text()[-6000:]);vm.stop()
        print(f'{cpus} CPUs: 12-worker pin/compute/brk/timed-wake/fault/reap, FP/IPC/badptr and checksum benchmarks passed.',flush=True)
    if original is not None and performance.digest(build/'data.img')!=original:
        raise AssertionError('Test changed existing data.img')
    (logs/'results.json').write_text(json.dumps({'requested_cpus':args.cpus,'repeats':args.repeats,
        'kernel_sha256':performance.digest(build/'kernel.elf'),'harness_sha256':performance.digest(Path(__file__)),
        'checks':results,'existing_data_unchanged':True},indent=2)+'\n')

if __name__=='__main__': main()
