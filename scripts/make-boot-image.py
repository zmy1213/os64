#!/usr/bin/env python3
"""Lay out the BIOS floppy without platform-specific truncate/dd options."""
import argparse
import struct
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--metadata-only', action='store_true')
args = p.parse_args()
b = args.build_dir
kernel = (b / 'kernel.bin').read_bytes()
sectors = (len(kernel) + 511) // 512
volume_start = 10 + sectors
if not 0 < sectors <= 2743:
    raise SystemExit('Kernel and 128-sector boot volume do not fit in the floppy')
elf = (b / 'kernel.elf').read_bytes()
header = struct.unpack_from('<16sHHIQQQIHHHHHH', elf)
if header[0][:4] != b'\x7fELF' or header[9] != 56:
    raise SystemExit('Invalid ELF64 kernel program headers')
for index in range(header[10]):
    segment = struct.unpack_from('<IIQQQQQQ', elf, header[5] + index * header[9])
    if segment[0] == 1 and segment[3] + segment[6] > 0x80000:
        raise SystemExit('Kernel load segment or BSS overlaps the boot volume at physical 0x80000')
metadata = f'''%define KERNEL_LOAD_ADDR 0x00010000
%define KERNEL_LOAD_SEGMENT 0x1000
%define KERNEL_LOAD_OFFSET 0x0000
%define KERNEL_START_SECTOR 10
%define KERNEL_SECTORS {sectors}
%define BOOT_VOLUME_LOAD_ADDR 0x00080000
%define BOOT_VOLUME_LOAD_SEGMENT 0x8000
%define BOOT_VOLUME_LOAD_OFFSET 0x0000
%define BOOT_VOLUME_START_SECTOR {volume_start}
%define BOOT_VOLUME_SECTORS 128
'''
(b / 'kernel_meta.inc').write_text(metadata)
if args.metadata_only:
    raise SystemExit(0)
stage1 = (b / 'stage1.bin').read_bytes()
stage2 = (b / 'stage2.bin').read_bytes()
volume = (b / 'boot_volume.bin').read_bytes()
if len(stage1) != 512 or stage1[510:512] != b'\x55\xaa':
    raise SystemExit('Invalid 512-byte BIOS stage1 or boot signature')
if len(stage2) != 4096 or len(volume) != 65536:
    raise SystemExit('Invalid stage2 or boot volume size')
image = bytearray(1474560)
image[:512] = stage1
image[512:4608] = stage2
image[4608:4608 + len(kernel)] = kernel
volume_offset = (volume_start - 1) * 512
image[volume_offset:volume_offset + len(volume)] = volume
(b / 'disk.img').write_bytes(image)
