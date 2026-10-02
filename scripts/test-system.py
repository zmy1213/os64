#!/usr/bin/env python3
"""Exercise the live shell via QMP keyboard and preserve data over cold boots."""
import argparse
import hashlib
import json
import re
import shutil
import socket
import subprocess
import tempfile
import time
import threading
from pathlib import Path

PROMPT = 'os64 % '
KEYS = {' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus', '_': 'shift-minus',
        '\n': 'ret', '=': 'equal', '"': 'shift-apostrophe', "'": 'apostrophe',
        ':': 'shift-semicolon', '&': 'shift-7', '?': 'shift-slash'}

class VM:
    def __init__(self, qemu, build, data, logs, temp, boot):
        self.log = logs / f'boot-{boot}.serial.log'
        self.err = logs / f'boot-{boot}.qemu.log'
        self.qmp_path = temp / 'qmp.sock'
        self.qmp_path.unlink(missing_ok=True)
        self.log.write_bytes(b'')
        self.serial_path = temp / 'serial.sock'
        self.serial_path.unlink(missing_ok=True)
        self.serial_stopped = threading.Event()
        self.err_handle = self.err.open('wb')
        self.process = subprocess.Popen([
            qemu, '-m', '128M', '-boot', 'a', '-display', 'none', '-no-reboot',
            '-drive', f'format=raw,file={build / "disk.img"},if=floppy,index=0',
            '-drive', f'format=raw,file={data},if=ide,index=0',
            '-chardev', f'socket,id=com1,path={self.serial_path},server=on,wait=off,logfile={self.log}',
            '-serial', 'chardev:com1', '-monitor', 'none',
            '-qmp', f'unix:{self.qmp_path},server=on,wait=off',
            '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04'
        ], stdout=self.err_handle, stderr=self.err_handle)
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        deadline = time.monotonic() + 5
        while True:
            try:
                self.socket.connect(str(self.qmp_path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if self.process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f'QEMU did not start; see {self.err}')
                time.sleep(.03)
        self.stream = self.socket.makefile('rwb', buffering=0)
        self.stream.readline()
        self.command('qmp_capabilities')
        self.serial_socket = socket.socket(socket.AF_UNIX)
        self.serial_socket.settimeout(1)
        self.serial_socket.connect(str(self.serial_path))
        self.serial_reader = threading.Thread(target=self.drain_serial, daemon=True)
        self.serial_reader.start()
        self.wait("shell_thread_started_tid=", timeout=30)
        self.wait(PROMPT, self.text().rfind("shell_thread_started_tid="), timeout=10)
        if "storage_backend=ata-pio" not in self.text():
            self.stop()
            raise AssertionError("Persistent ATA root was not mounted")

    def drain_serial(self):
        while not self.serial_stopped.is_set():
            try:
                if not self.serial_socket.recv(65536):
                    break
            except socket.timeout:
                continue
            except OSError:
                break

    def text(self):
        return self.log.read_text(errors='replace')

    def command(self, execute, arguments=None):
        request = {'execute': execute}
        if arguments is not None:
            request['arguments'] = arguments
        self.stream.write(json.dumps(request).encode() + b'\n')
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError('QMP disconnected')
            response = json.loads(line)
            if 'error' in response:
                raise RuntimeError(f'QMP error: {response["error"]}')
            if 'return' in response:
                return response['return']

    def wait(self, marker, start=0, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            output = self.text()[start:]
            if marker in output:
                return output
            if self.process.poll() is not None:
                raise RuntimeError(f'QEMU exited before {marker!r}; see {self.log}')
            time.sleep(.03)
        raise RuntimeError(f'Timed out waiting for {marker!r}; see {self.log}')

    def type_line(self, line):
        for char in line + '\n':
            if char.isdigit() or 'a' <= char <= 'z':
                key = char
            elif 'A' <= char <= 'Z':
                key = 'shift-' + char.lower()
            else:
                key = KEYS[char]
            self.command('human-monitor-command', {'command-line': f'sendkey {key} 1'})
            time.sleep(.014)
    def shell(self, line, expected=(), forbidden=(), timeout=15, serial=False):
        start = len(self.text())
        if serial:
            for char in line + '\n':
                self.serial_socket.sendall(char.encode())
                time.sleep(.005)
        else:
            self.type_line(line)
        output = self.wait(PROMPT, start, timeout)
        if line.startswith('run ') and '/bin/fault' not in line and '/bin/ud2' not in line:
            if 'run_exit_code=0' not in output:
                raise AssertionError(f'{line!r}: user program did not exit successfully: {output!r}')
        for marker in expected:
            if marker not in output:
                raise AssertionError(f'{line!r}: missing {marker!r} in {output!r}')
        for marker in forbidden:
            if marker in output:
                raise AssertionError(f'{line!r}: unexpected {marker!r} in {output!r}')
        return output

    def shutdown(self):
        self.type_line('shutdown')
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            raise AssertionError('Guest shutdown did not power off QEMU')
        self.stop()

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.serial_stopped.set()
        if hasattr(self, 'serial_socket'):
            self.serial_socket.close()
        if hasattr(self, 'serial_reader'):
            self.serial_reader.join(timeout=1)
        if hasattr(self, 'stream'):
            self.stream.close()
        self.socket.close()
        self.err_handle.close()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--qemu', required=True)
    p.add_argument('--build-dir', type=Path, required=True)
    args = p.parse_args()
    b = args.build_dir.resolve()
    logs = b / 'system-test'
    logs.mkdir(exist_ok=True)
    original_digest = digest(b / 'data.img')
    vm = None
    # Short path avoids macOS's Unix-domain socket pathname limit.
    with tempfile.TemporaryDirectory(prefix='os64-system-', dir='/tmp') as directory:
        temp = Path(directory)
        data = temp / 'data.img'
        shutil.copyfile(b / 'data_volume.bin', data)
        try:
            vm = VM(args.qemu, b, data, logs, temp, 1)
            vm.shell('help', ('run', 'sync'))
            vm.shell('run /bin/echo serial_input_ok', ('serial_input_ok',), serial=True)
            long_args = ' '.join('quoted_argument_' + str(i) for i in range(6))
            vm.shell('run /bin/echo ' + long_args, (long_args,))
            vm.shell('run /bin/echo \"quoted argument with spaces\"', ('quoted argument with spaces',))
            vm.shell('ls /bin', ('hello', 'writer', 'spawn_test'))
            vm.shell('run /bin/hello alpha beta', ('hello from os64 userland', 'argc=3', 'argv[1]=alpha', 'argv[2]=beta'))
            vm.shell('run /bin/echo keyboard_input_ok', ('keyboard_input_ok',))
            vm.shell('run /bin/cat /readme.txt', ('os64fs readme:',))
            vm.shell('run /bin/badptr', ('badptr rejected', 'badptr path rejected', 'badptr flags rejected'))
            vm.shell('run /bin/spawn_test', ('spawn child ok', 'spawn_test child_status=0'))
            vm.shell('run /bin/fault', ('fault: testing isolated invalid memory access', 'user_fault_vector=14', 'run_exit_code=142'))
            vm.shell('run /bin/echo after_page_fault_ok', ('after_page_fault_ok',))
            vm.shell('run /bin/ud2', ('ud2: testing isolated invalid opcode', 'user_fault_vector=6', 'run_exit_code=134'))
            vm.shell('run /bin/echo after_ud2_ok', ('after_ud2_ok',))
            vm.shell('run /bin/sleep 50', ('sleep started', 'sleep finished'))
            vm.shell('run /bin/spin 10000000', ('spin started', 'spin finished'))
            heap_before = vm.shell('heap')
            allocations_before = int(re.search(r'heap_active_allocations=(\d+)', heap_before).group(1))
            memory_before = vm.shell('mem')
            free_before = int(re.search(r'mem_free_pages=(\d+)', memory_before).group(1))
            for run in range(55):
                vm.shell('run /bin/echo cycle', ('cycle',))
            for orphan in range(12):
                child_start = len(vm.text())
                vm.shell('run /bin/spawn_test orphan', ('spawn_test orphan_started',))
                vm.wait('spawn child ok', child_start)
            process_list = vm.shell('ps')
            if 'exited' in process_list or 'finished' in process_list:
                raise AssertionError('Orphan processes were not reaped')
            memory_after = vm.shell('mem')
            free_after = int(re.search(r'mem_free_pages=(\d+)', memory_after).group(1))
            heap_after = vm.shell('heap')
            allocations_after = int(re.search(r'heap_active_allocations=(\d+)', heap_after).group(1))
            if allocations_after != allocations_before:
                raise AssertionError(f'Process heap leak: {allocations_before} -> {allocations_after}')
            if free_after != free_before:
                raise AssertionError(f'Process page leak: {free_before} -> {free_after}')
            vm.shell('run /bin/writer /persist.txt persistent123', ('writer readback=persistent123',))
            vm.shell('run /bin/fs_test /large.txt', ('fs_test direct_indirect_ok',))
            vm.shell('mkdir /tmp', forbidden=('failed',))
            vm.shell('write /tmp/note.txt shell_storage_ok', forbidden=('failed',))
            vm.shell('append /tmp/note.txt _append', forbidden=('failed',))
            vm.shell('cat /tmp/note.txt', ('shell_storage_ok_append',))
            vm.shell('touch /delete.txt', forbidden=('failed',))
            vm.shell('rm /delete.txt', forbidden=('failed',))
            vm.shell('ps')
            vm.shell('sync', ('sync ok',))
            vm.shutdown()
            vm = None
            vm = VM(args.qemu, b, data, logs, temp, 2)
            vm.shell('run /bin/cat /persist.txt', ('persistent123',))
            vm.shell('run /bin/fs_test /large.txt verify', ('fs_test direct_indirect_ok',))
            vm.shell('cat /tmp/note.txt', ('shell_storage_ok_append',))
            vm.shell('cat /delete.txt', ('not found',))
            vm.shell('rm /persist.txt', forbidden=('failed',))
            vm.shell('rm /large.txt', forbidden=('failed',))
            vm.shell('sync', ('sync ok',))
            vm.shutdown()
            vm = None
            vm = VM(args.qemu, b, data, logs, temp, 3)
            vm.shell('cat /persist.txt', ('not found',))
            vm.shell('cat /large.txt', ('not found',))
            vm.shell('cat /tmp/note.txt', ('shell_storage_ok_append',))
            print(f'System regression passed: userland, process isolation, file I/O, three boots. Logs: {logs}')
        except Exception:
            if vm is not None:
                print(vm.text()[-6000:])
            raise
        finally:
            if vm is not None:
                vm.stop()
    if digest(b / 'data.img') != original_digest:
        raise AssertionError('System regression changed the user data disk')

if __name__ == '__main__':
    main()
