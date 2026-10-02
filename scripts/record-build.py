#!/usr/bin/env python3
"""Record the actual build configuration and binary fingerprints for benchmarks."""
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

build = Path(sys.argv[1])
root = Path(__file__).resolve().parent.parent

def command(argv):
    return subprocess.check_output(argv, cwd=root, text=True).strip()

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

sources = sorted(p for directory in ('boot', 'kernel', 'user', 'scripts')
                 for p in (root / directory).rglob('*') if p.is_file() and
                 p.suffix in ('.cpp', '.hpp', '.asm', '.ld', '.sh', '.py'))
aggregate = hashlib.sha256()
for path in [root / 'Makefile', *sources]:
    aggregate.update(str(path.relative_to(root)).encode() + b'\0' + path.read_bytes() + b'\0')
manifest = {
    'created_utc': datetime.now(timezone.utc).isoformat(),
    'git_revision': command(['git', 'rev-parse', 'HEAD']),
    'working_tree_modified': bool(command(['git', 'status', '--porcelain'])),
    'source_sha256': aggregate.hexdigest(),
    'kernel_optimization': '-O' + sys.argv[2],
    'user_optimization': '-Os',
    'user_frame_pointer': True,
    'network_irq_requested': bool(int(sys.argv[5])),
    'compiler': command([sys.argv[3], '--version']).splitlines()[0],
    'assembler': command([sys.argv[4], '--version']).splitlines()[0],
    'kernel_binary_bytes': (build / 'kernel.bin').stat().st_size,
    'kernel_binary_sha256': digest(build / 'kernel.bin'),
    'boot_disk_sha256': digest(build / 'disk.img'),
    'user_binaries': {p.name: digest(p) for p in sorted((build / 'user').glob('*.elf'))
                      if not p.name.endswith('.unstripped.elf')},
}
(build / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
