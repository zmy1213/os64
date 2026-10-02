#!/usr/bin/env python3
"""Remove generated outputs while retaining the user's persistent disk."""
from pathlib import Path
import shutil
build = Path(__file__).resolve().parent.parent / 'build'
if build.is_dir():
    for path in build.iterdir():
        if path.name == 'data.img':
            continue
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
print('Build outputs removed; persistent data.img retained.')
