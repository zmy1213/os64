#!/usr/bin/env python3
"""Exercise pipe blocking/EOF, inherited descriptions, and live Shell pipelines."""
import argparse
import importlib.util
import re
import shutil
import tempfile
from pathlib import Path

spec=importlib.util.spec_from_file_location('os64_system',Path(__file__).with_name('test-system.py'))
system=importlib.util.module_from_spec(spec)
spec.loader.exec_module(system)
spec=importlib.util.spec_from_file_location('os64_memory',Path(__file__).with_name('test-memory-user.py'))
memory=importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)

def counts(vm):
    pages=int(re.search(r'mem_free_pages=(\d+)',vm.shell('mem',serial=True)).group(1))
    heap=int(re.search(r'heap_active_allocations=(\d+)',vm.shell('heap',serial=True)).group(1))
    return pages,heap

def assert_file(image,path,expected):
    actual=memory.file_bytes(image,path)
    if actual!=expected:
        raise AssertionError(f'{path}: expected {expected!r}, got {actual!r}')

def console_input_edges(vm,image):
    vm.shell('echo sentinel > /input-protected.txt',serial=True)
    before=counts(vm)
    # The live console has a lower bound than the standalone parser: 255B.
    # A full valid prefix followed by ignored text used to run the first write.
    prefix='echo overwritten > /input-protected.txt;'
    for suffix in ('echo "unterminated', 'echo tail > /overflow-tail.txt'):
        line=prefix+' '*(255-len(prefix))+suffix
        vm.shell(line,('input line too long; entire line discarded',),serial=True)
        assert_file(image,'/input-protected.txt',b'sentinel\n')
        vm.shell('echo status=$?',('status=2',),serial=True)
    try:
        memory.file_bytes(image,'/overflow-tail.txt')
    except AssertionError as error:
        if 'file not present in disk image' not in str(error):
            raise
    else:
        raise AssertionError('An overlong line executed its discarded suffix')
    vm.shell('echo after_line_overflow',('after_line_overflow',),serial=True)
    # Exactly 255B must remain valid; reject only the first character beyond it.
    boundary='echo exact_line_limit > /line-limit.txt'
    vm.shell(boundary+' '*(255-len(boundary)),serial=True)
    assert_file(image,'/line-limit.txt',b'exact_line_limit\n')
    vm.shell(boundary+' '*(256-len(boundary)),
             ('input line too long; entire line discarded',),serial=True)
    assert_file(image,'/line-limit.txt',b'exact_line_limit\n')
    vm.shell('echo status=$?',('status=2',),serial=True)
    if counts(vm)!=before:
        raise AssertionError('Rejected console lines leaked pages or allocations')

def modern_shell_edges(vm,image):
    # Inspect disk bytes rather than matching the terminal's echo of input text.
    vm.shell('/bin/echo "single quoted" \'double quoted\' escaped\\ space > /quoting.txt',serial=True)
    assert_file(image,'/quoting.txt',b'single quoted double quoted escaped space\n')
    vm.shell('/bin/hello "two words" \'three words\' escaped\\ space',
             ('argc=4','argv[1]=two words','argv[2]=three words','argv[3]=escaped space'),serial=True)
    vm.shell('echo \'$PWD\' "\\$PWD" "$PATH" > /variables.txt',serial=True)
    assert_file(image,'/variables.txt',b'$PWD $PWD /bin\n')
    vm.shell('echo "prefix${UNSET}suffix" > /unset.txt',serial=True)
    assert_file(image,'/unset.txt',b'prefixsuffix\n')
    vm.shell('mkdir "/space dir"',serial=True)
    vm.shell('cd "/space dir"; echo "${PWD}" > /pwd.txt',serial=True)
    assert_file(image,'/pwd.txt',b'/space dir\n')
    vm.shell('cd /',serial=True)
    vm.shell('false; echo $? > /false-status.txt',serial=True)
    assert_file(image,'/false-status.txt',b'1\n')
    vm.shell('true; echo $? > /true-status.txt',serial=True)
    assert_file(image,'/true-status.txt',b'0\n')
    # && and || are evaluated left to right, not with && binding more tightly.
    vm.shell('true || false && echo left > /left.txt',serial=True)
    assert_file(image,'/left.txt',b'left\n')
    vm.shell('false && true || echo recovered > /recovery.txt',serial=True)
    assert_file(image,'/recovery.txt',b'recovered\n')
    vm.shell('cat /missing-for-stderr.txt 2> /stderr.txt',serial=True)
    vm.shell('echo status=$?',('status=1',),serial=True)
    vm.shell('cat /missing-second.txt 2>> /stderr.txt',serial=True)
    assert_file(image,'/stderr.txt',b'cat: not found or cannot open /missing-for-stderr.txt\n'
                b'cat: not found or cannot open /missing-second.txt\n')
    vm.shell('echo four stages | cat | cat | wc',('1 2 12',),serial=True)
    # Redirecting away from a pipe must release unused ends so wc can see EOF.
    vm.shell('echo pipe_source > /override.txt | wc',('0 0 0',),serial=True)
    assert_file(image,'/override.txt',b'pipe_source\n')
    vm.shell('echo unused | cat < /ipc.txt | wc',('2 3 21',),serial=True)

    vm.shell('echo sentinel > /protected-output.txt',serial=True)
    protected=b'sentinel\n'
    vm.shell('echo danger | no_such_middle | cat > /protected-output.txt',('unknown command:',),serial=True)
    assert_file(image,'/protected-output.txt',protected)
    vm.shell('echo status=$?',('status=127',),serial=True)
    invalid=[
        ('echo changed > /protected-output.txt; echo "open','unterminated quote'),
        ('echo changed > /protected-output.txt; true &&','missing command after condition'),
        ('echo bad > /protected-output.txt > /other.txt','duplicate redirect'),
        ('echo changed > /protected-output.txt; cat|cat|cat|cat|cat','4 per pipeline'),
        ('echo a b c d e f g h > /protected-output.txt','8 arguments per command'),
        ('echo '+ 'x'*64 + ' > /protected-output.txt','word exceeds 63 bytes'),
        ('echo changed > /protected-output.txt;'+'true;'*8,'too many pipelines'),
    ]
    for line,marker in invalid:
        vm.shell(line,(marker,),serial=True)
        assert_file(image,'/protected-output.txt',protected)
        vm.shell('echo status=$?',('status=2',),serial=True)
    console_input_edges(vm,image)
    vm.shell('true;'*7+'true',serial=True)
    vm.shell('echo status=$?',('status=0',),serial=True)
    vm.shell('echo a b c d e f g > /args.txt',serial=True)
    assert_file(image,'/args.txt',b'a b c d e f g\n')
    vm.shell('echo '+'x'*63+' > /word.txt',serial=True)
    assert_file(image,'/word.txt',b'x'*63+b'\n')

    before=counts(vm)
    vm.shell('sleep 200 &',serial=True)
    vm.shell('spin 1000000 &',serial=True)
    output=vm.shell('jobs',serial=True)
    if not re.search(r'\[\d+\] (running|done) ',output):
        raise AssertionError(f'Background jobs were not tracked: {output!r}')
    vm.shell('wait',serial=True)
    if re.search(r'\[\d+\] (running|done) ',vm.shell('jobs',serial=True)):
        raise AssertionError('wait did not release background jobs')
    if counts(vm)!=before:
        raise AssertionError('Background sleep/compute jobs leaked descriptors or pages')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--qemu',required=True)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--kernel-only',action='store_true',help='run syscall tests before modern Shell is integrated')
    parser.add_argument('--console-input-only',action='store_true',help='run only the live input-length boundary regression')
    args=parser.parse_args()
    build=args.build_dir.resolve()
    logs=build/'ipc-test'; logs.mkdir(exist_ok=True)
    original=system.digest(build/'data.img')
    vm=None
    try:
        with tempfile.TemporaryDirectory(prefix='os64-ipc-',dir='/tmp') as directory:
            temp=Path(directory); data=temp/'data.img'
            shutil.copyfile(build/'data_volume.bin',data)
            try:
                vm=system.VM(args.qemu,build,data,logs,temp,1)
                if args.console_input_only:
                    console_input_edges(vm,data)
                    vm.shutdown(); vm=None
                    print(f'Console input regression passed: 255B accepted, longer lines fully discarded, status2, next line and resource recovery. Logs: {logs}')
                    return
                before=counts(vm)
                markers=('pipe_test eof_epipe_ok','pipe_test blocking_32k_ok',
                         'pipe_test exit_wakes_eof_ok','pipe_test inherited_offset_ok',
                         'pipe_test multiwriter_atomic_ok',
                         'pipe_test dup_validation_ok')
                for _ in range(8):
                    vm.shell('run /bin/pipe_test',markers,serial=True)
                after=counts(vm)
                if after!=before:
                    raise AssertionError(f'IPC descriptors or pages leaked: {before} -> {after}')
                if not args.kernel_only:
                    vm.shell('stat /does-not-exist',serial=True)
                    vm.shell('echo status=$?',('status=1',),serial=True)
                    vm.shell('touch',('usage: touch',),serial=True)
                    vm.shell('echo status=$?',('status=2',),serial=True)
                    vm.shell('write /missing-parent/file.txt x',('write failed:',),serial=True)
                    vm.shell('echo status=$?',('status=1',),serial=True)
                    vm.shell('append /missing-parent/file.txt x',('append failed:',),serial=True)
                    vm.shell('echo status=$?',('status=1',),serial=True)
                    vm.shell('touch /docs',('touch not a file:',),serial=True)
                    vm.shell('echo status=$?',('status=1',),serial=True)
                    vm.shell('stat /readme.txt',serial=True)
                    vm.shell('echo status=$?',('status=0',),serial=True)
                    vm.shell('sync',('sync ok',),serial=True)
                    vm.shell('echo status=$?',('status=0',),serial=True)
                    vm.shell('echo one two | cat | wc',('1 2 8',),serial=True)
                    vm.shell('echo redirected text > /ipc.txt',serial=True)
                    vm.shell('cat < /ipc.txt | wc',('1 2 16',),serial=True)
                    vm.shell('echo tail >> /ipc.txt',serial=True)
                    vm.shell('cat /ipc.txt | wc',('2 3 21',),serial=True)
                    if memory.file_bytes(data,'/ipc.txt')!=b'redirected text\ntail\n':
                        raise AssertionError('Output redirection did not preserve expected bytes')
                    modern_shell_edges(vm,data)
                    # More bytes than the pipe buffer, with three concurrently running stages.
                    vm.shell('run /bin/mem_test seed /stream.txt 16000',serial=True)
                    vm.shell('cat /stream.txt | cat | wc',('0 1 16000',),serial=True)
                    before=counts(vm)
                    for _ in range(12):
                        vm.shell('echo repeated pipeline | cat | wc',('1 2 18',),serial=True)
                    if counts(vm)!=before:
                        raise AssertionError('Repeated Shell pipelines leaked descriptors or pages')
                vm.shutdown(); vm=None
                print(f'IPC regression passed: bounded pipe, EOF/EPIPE, exit wakeup, shared offsets, repeated release and Shell pipelines. Logs: {logs}')
            except Exception:
                if vm: print(vm.text()[-8000:])
                raise
            finally:
                if vm: vm.stop()
    finally:
        if system.digest(build/'data.img')!=original:
            raise AssertionError('IPC regression changed the user data disk')
if __name__=='__main__': main()
