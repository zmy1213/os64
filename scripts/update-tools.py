#!/usr/bin/env python3
"""Update /bin on an existing data image, retaining other files and a full backup."""
import argparse
import fcntl
import errno
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone

def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, default=root / 'build/data.img')
    args = parser.parse_args()
    image = args.image.resolve()
    programs = sorted(p for p in (root / 'build/user').glob('*.elf')
                      if not p.name.endswith('.unstripped.elf'))
    if not programs:
        raise SystemExit('Build user tools first: make build')
    compiler = os.environ.get('HOST_CXX') or shutil.which('clang++') or shutil.which('g++')
    if compiler is None:
        raise SystemExit('A host C++ compiler is required')
    executable = root / 'build/update-tools-host'
    sources = ['tools/update_tools.cpp', 'kernel/fs/os64fs.cpp', 'kernel/fs/file.cpp', 'kernel/fs/directory.cpp',
               'kernel/storage/block_device.cpp', 'kernel/storage/boot_volume.cpp',
               'kernel/runtime/runtime.cpp']
    subprocess.run([compiler, '-std=c++17', '-O1', '-I', str(root / 'kernel'),
                    *(str(root / s) for s in sources), '-o', str(executable)], check=True)
    with image.open('r+b') as original:
        try:
            # QEMU also uses file locks. Stop the VM before updating its mounted disk.
            fcntl.lockf(original, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Data disk is in use. Shut down QEMU before updating tools.')
        before = original.read()
        original_identity = os.fstat(original.fileno())
        with tempfile.TemporaryDirectory(prefix='.os64-update-', dir=image.parent) as temp:
            staged = Path(temp) / 'data.img'
            subprocess.run([str(executable), str(image), str(staged), *map(str, programs)], check=True)
            # Read via the already locked descriptor. With POSIX record locks,
            # closing another descriptor for this file would release our lock.
            original.seek(0)
            current_identity = image.stat()
            if ((current_identity.st_dev, current_identity.st_ino) !=
                    (original_identity.st_dev, original_identity.st_ino) or
                    hashlib.sha256(original.read()).digest() != hashlib.sha256(before).digest()):
                raise SystemExit('Data disk changed during update; staged update discarded')
            stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
            backup = image.with_name(image.name + '.backup-' + stamp)
            with backup.open('xb') as out:
                out.write(before)
                out.flush()
                os.fsync(out.fileno())
            with staged.open('r+b') as out:
                os.fsync(out.fileno())
            os.replace(staged, image)
            # Persist the rename where directory fsync is supported. Some host
            # filesystems reject it; the complete old image is still in backup.
            directory_fd = os.open(image.parent, os.O_RDONLY)
            try:
                try:
                    os.fsync(directory_fd)
                except OSError as error:
                    if error.errno not in (errno.EINVAL, errno.ENOTSUP):
                        raise
            finally:
                os.close(directory_fd)
            print(f'Updated /bin; user files retained. Backup: {backup}')

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f'Tool update failed (exit {error.returncode}); original data disk retained.')
