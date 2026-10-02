#!/usr/bin/env python3
"""Test real user heaps, large ELF files, guards, and editor persistence.

The VM receives a temporary copy of the fresh template. The user's data.img
must keep exactly the same digest, including when a test fails.
"""
import argparse
import importlib.util
import re
import shutil
import struct
import tempfile
import time
from pathlib import Path

# The existing file keeps its historical hyphenated name, which is not a normal
# Python import identifier. Load it explicitly rather than copying the VM code.
spec = importlib.util.spec_from_file_location('os64_system_test', Path(__file__).with_name('test-system.py'))
system_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(system_test)
VM, PROMPT, digest = system_test.VM, system_test.PROMPT, system_test.digest


def file_bytes(image, path):
    """Read fixture bytes directly, so a successful editor message is not enough."""
    content = image.read_bytes()
    fields = struct.unpack_from('<17I', content, 8)
    inode_table, data_start, root_inode = fields[6], fields[10], fields[13]

    def inode_payload(number):
        values = struct.unpack_from('<IHHII8I4I', content, inode_table * 512 + number * 64)
        size = values[3]
        direct, indirect = values[5:13], values[13]
        blocks = list(direct[:min((size + 511) // 512, 8)])
        if size > 8 * 512:
            extra = (size + 511) // 512 - 8
            blocks += struct.unpack_from('<128I', content, (data_start + indirect) * 512)[:extra]
        return b''.join(content[(data_start + block) * 512:(data_start + block + 1) * 512]
                        for block in blocks)[:size]

    number = root_inode
    for name in path.strip('/').split('/'):
        entries = inode_payload(number)
        for offset in range(0, len(entries), 64):
            child, _, size, _, raw_name = struct.unpack_from('<IHBB56s', entries, offset)
            if raw_name[:size].decode() == name:
                number = child
                break
        else:
            raise AssertionError(f'{path}: file not present in disk image')
    return inode_payload(number)


def send_line(vm, line):
    # Feed characters gradually: the guest deliberately has a bounded input FIFO.
    for char in line + '\n':
        vm.serial_socket.sendall(char.encode())
        time.sleep(.002)


class Editor:
    def __init__(self, vm, path, expected):
        self.vm = vm
        start = len(vm.text())
        send_line(vm, 'run /bin/edit ' + path)
        output = vm.wait('edit> ', start)
        if expected not in output:
            raise AssertionError(f'Editor did not load: {output!r}')

    def command(self, line, expected=()):
        start = len(self.vm.text())
        send_line(self.vm, line)
        output = self.vm.wait('edit> ', start)
        for marker in expected:
            if marker not in output:
                raise AssertionError(f'{line!r}: missing {marker!r}: {output!r}')
        return output

    def quit(self, discard=False):
        start = len(self.vm.text())
        send_line(self.vm, 'quit!' if discard else 'quit')
        output = self.vm.wait(PROMPT, start)
        if 'run_exit_code=0' not in output:
            raise AssertionError(f'Editor exit failed: {output!r}')


def count_pages(vm):
    return int(re.search(r'mem_free_pages=(\d+)', vm.shell('mem', serial=True)).group(1))


def verify_nx_fixture(path):
    """The ret bytes must actually live in a writable, non-executable PT_LOAD.

    With an unused ordinary array, compiler constant promotion can put the test
    bytes in .rodata, which this tutorial links beside executable .text. In that
    case a successful call is not an NX defect; it is a defective test fixture.
    """
    elf = path.read_bytes()
    offset = struct.unpack_from('<Q', elf, 32)[0]
    entry_size, count = struct.unpack_from('<HH', elf, 54)
    for index in range(count):
        kind, flags, file_offset, _, _, file_size, _, _ = struct.unpack_from(
            '<II6Q', elf, offset + index * entry_size)
        payload = elf[file_offset:file_offset + file_size]
        if kind == 1 and flags & 2 and not flags & 1 and b'\xc3' + b'\0' * 15 in payload:
            return
    raise AssertionError('nxfault ret array is not in a writable, non-executable load segment')


def fill_until_rejected(vm, prefix, size, limit):
    """Fill through real guest syscalls, rather than manufacturing a full image."""
    for index in range(limit):
        start = len(vm.text())
        send_line(vm, f'run /bin/mem_test seed /{prefix}-{index}.txt {size}')
        output = vm.wait(PROMPT, start)
        if f'mem_test seeded {size} bytes' in output and 'run_exit_code=0' in output:
            continue
        if 'mem_test: seed failed' in output and 'run_exit_code=6' in output:
            return
        raise AssertionError(f'Unexpected disk-fill result: {output!r}')
    raise AssertionError(f'Disk did not fill after {limit} files of {size} bytes')


def verify_failed_save_preserves_file(vm, image):
    """ENOSPC must leave an existing file and the editor's working text intact."""
    sentinel = b'protected_sentinel'
    vm.shell('run /bin/writer /protected.txt protected_sentinel', serial=True)
    if file_bytes(image, '/protected.txt') != sentinel:
        raise AssertionError('Failed-save test did not create its sentinel')
    # First fill quickly with large files, then consume the remaining small gaps.
    # Fixed bounds also catch unexpected inode exhaustion or a broken allocator.
    fill_until_rejected(vm, 'fill-big', 60000, 12)
    fill_until_rejected(vm, 'fill-small', 4096, 20)
    free_blocks = struct.unpack_from('<17I', image.read_bytes(), 8)[16]
    if free_blocks >= 8:
        raise AssertionError(f'Disk-fill precondition failed: {free_blocks} free blocks remain')
    before = file_bytes(image, '/protected.txt')
    editor = Editor(vm, '/protected.txt', 'edit: loaded 18 bytes')
    for index in range(20):
        editor.command(f'append edit-{index:02d}-' + 'x' * 390)
    editor.command('save', ('edit: save failed; edits remain in memory',))
    if file_bytes(image, '/protected.txt') != before:
        raise AssertionError('ENOSPC save changed or truncated the existing file')
    editor.command('print', ('1: protected_sentinel', '21: edit-19-'))
    editor.command('quit', ('edit: unsaved changes',))
    editor.quit(discard=True)
    if file_bytes(image, '/protected.txt') != sentinel:
        raise AssertionError('Failed save followed by quit! changed the old file')
    vm.shell('run /bin/echo shell_after_failed_save', ('shell_after_failed_save',), serial=True)


def run_fault(vm, program, marker):
    # VM.shell intentionally expects success for most run commands; these exit 142.
    start = len(vm.text())
    send_line(vm, 'run /bin/' + program)
    output = vm.wait(PROMPT, start)
    for expected in (marker, 'user_fault_vector=14', 'run_exit_code=142'):
        if expected not in output:
            raise AssertionError(f'{program}: missing {expected!r}: {output!r}')
    vm.shell('run /bin/echo fault_isolated', ('fault_isolated',), serial=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', required=True)
    parser.add_argument('--build-dir', required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    logs = build / 'memory-user-test'
    logs.mkdir(exist_ok=True)
    original_digest = digest(build / 'data.img')
    vm = None
    try:
        with tempfile.TemporaryDirectory(prefix='os64-memory-', dir='/tmp') as directory:
            temp = Path(directory)
            data = temp / 'data.img'
            shutil.copyfile(build / 'data_volume.bin', data)
            try:
                assert (build / 'user' / 'mem_test.elf').stat().st_size > 4096
                verify_nx_fixture(build / 'user' / 'nxfault.elf')
                vm = VM(args.qemu, build, data, logs, temp, 1)
                readme_before = file_bytes(data, '/readme.txt')
                vm.shell('run /bin/badptr', ('badptr replace path rejected',
                         'badptr replace buffer rejected', 'badptr replace size rejected'), serial=True)
                if file_bytes(data, '/readme.txt') != readme_before:
                    raise AssertionError('Invalid replace_file arguments changed the target file')
                free_before = count_pages(vm)
                for _ in range(12):
                    vm.shell('run /bin/mem_test', ('mem_test large_elf_ok',
                             'mem_test large_stack_ok', 'mem_test brk_zero_reject_ok',
                             'mem_test heap_1m_reuse_ok'), serial=True)
                if count_pages(vm) != free_before:
                    raise AssertionError('Repeated dynamic-heap processes leaked physical pages')
                run_fault(vm, 'stackfault', 'stackfault: testing unmapped stack guard')
                if 'nx_enabled=1' in vm.text():
                    run_fault(vm, 'nxfault', 'nxfault: testing non-executable data')
                elif 'nx_enabled=0' not in vm.text():
                    raise AssertionError('The kernel did not report its NX support')
                if count_pages(vm) != free_before:
                    raise AssertionError('Faulted processes leaked pages')

                editor = Editor(vm, '/editor.txt', 'edit: new file')
                editor.command('append first line')
                editor.command('append third line')
                editor.command('insert 2 second line')
                editor.command('delete 1')
                editor.command('print', ('1: second line', '2: third line'))
                editor.command('delete 0', ('edit: line number out of range',))
                editor.command('quit', ('edit: unsaved changes',))
                editor.command('append ' + 'x' * 600, ('edit: command too long',))
                editor.command('save', ('edit: saved 23 bytes',))
                editor.quit()
                expected = b'second line\nthird line\n'
                if file_bytes(data, '/editor.txt') != expected:
                    raise AssertionError('Editor did not write exactly the edited text')

                # > 4 KiB text exercises dynamic buffer growth and indirect disk blocks.
                editor = Editor(vm, '/editor.txt', 'edit: loaded 23 bytes')
                lines = [f'line-{i:02d}-' + 'z' * 180 for i in range(35)]
                for line in lines:
                    editor.command('append ' + line)
                expected += b''.join((line + '\n').encode() for line in lines)
                editor.command('save', (f'edit: saved {len(expected)} bytes',))
                editor.quit()
                if file_bytes(data, '/editor.txt') != expected:
                    raise AssertionError('Large edit lost or changed bytes')

                # Refusing an oversized file must not truncate it or start an editor.
                vm.shell('run /bin/mem_test seed /too-large.txt 40000',
                         ('mem_test seeded 40000 bytes',), serial=True)
                large_before = file_bytes(data, '/too-large.txt')
                start = len(vm.text())
                send_line(vm, 'run /bin/edit /too-large.txt')
                output = vm.wait(PROMPT, start)
                if 'file exceeds 32768 bytes; original file unchanged' not in output or 'run_exit_code=2' not in output:
                    raise AssertionError(f'Oversized file was not rejected: {output!r}')
                if file_bytes(data, '/too-large.txt') != large_before:
                    raise AssertionError('Loading an oversized file changed it')

                # Replacing a file with shorter text must remove its old trailing bytes.
                vm.shell('run /bin/writer /short.txt old_long_content', serial=True)
                editor = Editor(vm, '/short.txt', 'edit: loaded 16 bytes')
                editor.command('delete 1')
                editor.command('append new')
                editor.command('save', ('edit: saved 4 bytes',))
                editor.quit()
                if file_bytes(data, '/short.txt') != b'new\n':
                    raise AssertionError('Save left a stale suffix from the old file')
                vm.shutdown()
                vm = None

                vm = VM(args.qemu, build, data, logs, temp, 2)
                editor = Editor(vm, '/editor.txt', f'edit: loaded {len(expected)} bytes')
                editor.command('print', ('1: second line', '2: third line', '37: line-34-'))
                editor.command('append discarded edit')
                editor.quit(discard=True)
                if file_bytes(data, '/editor.txt') != expected:
                    raise AssertionError('quit! changed the saved file')
                vm.shell('run /bin/cat /short.txt', ('new',), serial=True)
                verify_failed_save_preserves_file(vm, data)
                vm.shutdown()
                vm = None
                print(f'Memory/user regression passed: large ELF, 1 MiB heap, guards, editor, cold boot, invalid pointers and ENOSPC save rollback. Logs: {logs}')
            except Exception:
                if vm is not None:
                    print(vm.text()[-7000:])
                raise
            finally:
                if vm is not None:
                    vm.stop()
    finally:
        if digest(build / 'data.img') != original_digest:
            raise AssertionError('Memory/user regression changed the user data disk')


if __name__ == '__main__':
    main()
