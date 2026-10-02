#!/usr/bin/env python3
"""Append an isolated /init and the same-source benchmark to Alpine initramfs."""
import argparse
import gzip
import hashlib
import json
import stat
from pathlib import Path

INIT = b'''#!/bin/busybox sh
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev
echo linux_kernel=$(/bin/busybox uname -r)
echo linux_online_cpus=$(/bin/busybox cat /sys/devices/system/cpu/online)
echo linux_bench_ready
while true; do
  printf 'linux-bench %% '
  IFS= read -r command || break
  if [ "$command" = shutdown ]; then /bin/busybox poweroff -f; fi
  /bin/busybox sh -c "$command"
  echo linux_exit_code=$?
done
/bin/busybox poweroff -f
'''

def newc(files):
    archive = bytearray()
    for inode, (name, mode, data) in enumerate(files + [('TRAILER!!!', 0, b'')], 1):
        name = name.encode() + b'\0'
        fields = [inode, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0]
        archive += b'070701' + ''.join(f'{field:08x}' for field in fields).encode() + name
        archive += b'\0' * (-len(archive) % 4)
        archive += data
        archive += b'\0' * (-len(archive) % 4)
    return bytes(archive)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    args = parser.parse_args()
    destination = args.build_dir / 'linux-benchmark'
    kernel, initramfs = args.assets / 'vmlinuz-virt', args.assets / 'initramfs-virt'
    if not kernel.is_file() or not initramfs.is_file():
        raise SystemExit('Download Alpine official x86_64 netboot vmlinuz-virt and initramfs-virt into the assets directory first; see PERFORMANCE_TUTORIAL.md.')
    files = [('init', stat.S_IFREG | 0o755, INIT)]
    files += [(f'bin/{name}', stat.S_IFREG | 0o755, (destination / name).read_bytes())
              for name in ['bench', 'bench_ipc']]
    combined = initramfs.read_bytes() + gzip.compress(newc(files), mtime=0)
    (destination / 'initramfs-benchmark.gz').write_bytes(combined)
    manifest = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in [kernel, initramfs, destination / 'bench', destination / 'bench_ipc',
                             destination / 'initramfs-benchmark.gz']}
    (destination / 'assets.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Linux benchmark initramfs: {destination / "initramfs-benchmark.gz"}')

if __name__ == '__main__':
    main()
