#!/usr/bin/env python3
"""Build reproducible OS64FS V3 fixtures and a separate persistent root template."""
import argparse
import struct
from pathlib import Path

BLOCK = 512
INVALID = 0xffffffff
README = b'os64fs readme: the 64-bit kernel now mounts a real read-only filesystem.'
NOTES = b'os64fs notes: next steps are write support, cache, and real disk drivers.'
GUIDE = b'os64fs guide: stage2 only preloads raw sectors. the block device layer turns that memory into sector reads, and the filesystem layer resolves paths like docs/guide.txt inside the 64-bit kernel.'

class Volume:
    def __init__(self, sectors=128, inodes=32):
        self.sectors, self.inodes = sectors, inodes
        self.table_sectors = (inodes * 64 + BLOCK - 1) // BLOCK
        self.data_start = 3 + self.table_sectors
        self.data_count = sectors - self.data_start
        if inodes > 4096 or self.data_count > 4096:
            raise ValueError('This image builder uses one sector per bitmap')
        self.nodes = [(0, 0, 0, b'')]
        self.root = self.directory()

    def directory(self):
        self.nodes.append((2, 493, 1, []))
        return len(self.nodes) - 1

    def file(self, payload, mode=420):
        self.nodes.append((1, mode, 1, payload))
        return len(self.nodes) - 1

    def entry(self, directory, name, inode):
        if len(name.encode()) > 56:
            raise ValueError('Directory name too long')
        self.nodes[directory][3].append((inode, self.nodes[inode][0], name))

    def encode(self):
        image = bytearray(self.sectors * BLOCK)
        inode_bitmap = bytearray(BLOCK)
        data_bitmap = bytearray(BLOCK)
        next_block = 0
        def allocate():
            nonlocal next_block
            if next_block >= self.data_count:
                raise ValueError('Data volume full')
            index = next_block
            next_block += 1
            data_bitmap[index // 8] |= 1 << (index % 8)
            return index
        def block_offset(index):
            return (self.data_start + index) * BLOCK
        for inode, (kind, mode, links, content) in enumerate(self.nodes):
            if kind == 2:
                content = b''.join(struct.pack('<IHBB56s', n, t, len(name.encode()), 0, name.encode())
                                   for n, t, name in content)
            blocks = (len(content) + BLOCK - 1) // BLOCK
            indirect = allocate() if blocks > 8 else INVALID
            indexes = [allocate() for _ in range(blocks)]
            if blocks > 136:
                raise ValueError('File exceeds direct plus single-indirect capacity')
            direct = indexes[:8] + [INVALID] * (8 - min(blocks, 8))
            offset = 3 * BLOCK + inode * 64
            struct.pack_into('<IHHII8I4I', image, offset,
                             inode, kind, links, len(content), mode, *direct,
                             indirect, blocks, 0, 0)
            for logical, index in enumerate(indexes):
                chunk = content[logical * BLOCK:(logical + 1) * BLOCK]
                image[block_offset(index):block_offset(index) + len(chunk)] = chunk
            if indirect != INVALID:
                pointers = indexes[8:] + [INVALID] * (128 - len(indexes[8:]))
                struct.pack_into('<128I', image, block_offset(indirect), *pointers)
            inode_bitmap[inode // 8] |= 1 << (inode % 8)
        used_inodes = len(self.nodes) - 1
        if len(self.nodes) > self.inodes:
            raise ValueError('Inode volume full')
        values = [3, self.sectors, 1, 1, 2, 1, 3, self.table_sectors,
                  self.inodes, 64, self.data_start, self.data_count, BLOCK,
                  1, 64, self.inodes - 1 - used_inodes, self.data_count - next_block]
        struct.pack_into('<8s17I12sII', image, 0, b'OS64FSV3', *values, b'os64-root', 0, 0)
        image[BLOCK:2 * BLOCK] = inode_bitmap
        image[2 * BLOCK:3 * BLOCK] = data_bitmap
        return image

def fixture(build, sectors=128, inodes=32, interactive=False):
    v = Volume(sectors, inodes)
    # 启动自测按字节比较历史 fixture，因此只更新交互盘的说明文字。
    readme = v.file((b'os64fs readme: this teaching OS has user programs, a dynamic heap, '
                     b'a line editor, and persistent ATA storage.\n') if interactive else README)
    notes = v.file((b'os64fs notes: run /bin/edit /note.txt to edit text. Use save, then quit. '
                    b'Changes stay in RAM until save. There is no power-loss journal yet.\n') if interactive else NOTES)
    docs = v.directory()
    guide = v.file((b'os64fs guide: BIOS loads the kernel from disk.img. The boot self-test uses '
                   b'an immutable RAM fixture, then the shell mounts data.img through ATA PIO. '
                   b'User tools call the kernel to read and replace files; save flushes writes. '
                   b'The filesystem has I/O rollback, but no journal for unexpected power loss.\n')
                  if interactive else GUIDE)
    hello = v.file((build / 'hello.bin').read_bytes(), 493)
    elf = v.file((build / 'hello.elf').read_bytes(), 493)
    big = v.file(b''.join(bytes([65 + i]) * BLOCK for i in range(10)))
    for name, inode in [('readme.txt', readme), ('notes.txt', notes), ('docs', docs)]:
        v.entry(v.root, name, inode)
    for name, inode in [('guide.txt', guide), ('hello.bin', hello), ('hello.elf', elf), ('big.txt', big)]:
        v.entry(docs, name, inode)
    return v

p = argparse.ArgumentParser()
p.add_argument('--build-dir', type=Path, required=True)
args = p.parse_args()
b = args.build_dir
(b / 'boot_volume.bin').write_bytes(fixture(b).encode())
v = fixture(b, sectors=1024, inodes=64, interactive=True)
bin_dir = v.directory()
v.entry(v.root, 'bin', bin_dir)
for program in sorted((b / 'user').glob('*.elf')):
    if program.name.endswith('.unstripped.elf'):
        continue
    content = program.read_bytes()
    if len(content) > 136 * BLOCK:
        raise SystemExit(f'{program.name}: executable exceeds the 69632-byte OS64FS file limit')
    v.entry(bin_dir, program.stem, v.file(content, 493))
(b / 'data_volume.bin').write_bytes(v.encode())
