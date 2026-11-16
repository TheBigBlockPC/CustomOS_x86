#!/usr/bin/env python3
"""Check framebuffer layout and buffer bounds without graphics hardware."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='customos-display-host-') as directory:
        executable = Path(directory) / 'display-host'
        subprocess.run([
            'clang', '-O1', '-g', '-fsanitize=address,undefined',
            '-Wall', '-Wextra', '-Werror', '-DBUILD_KERNEL',
            '-I', str(ROOT / 'src'),
            str(ROOT / 'tests/display_host.c'),
            str(ROOT / 'src/display_mgr.c'),
            str(ROOT / 'src/console_renderer.c'),
            '-o', str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})


if __name__ == '__main__':
    main()
