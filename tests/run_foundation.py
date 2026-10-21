#!/usr/bin/env python3
"""Run PMM/page-table host tests and instrumented UEFI boots in temporary files.
Requires clang, lld, QEMU and OVMF. Never rewrites the working boot image.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
OVMF = Path('/usr/share/OVMF')


def run(*args, **kwargs):
    return subprocess.run(*args, check=True, cwd=ROOT, **kwargs)


def main():
    with tempfile.TemporaryDirectory(prefix='customos-foundation-') as directory:
        temporary = Path(directory)
        host = temporary / 'memory-host'
        run(['clang', '-O2', '-Wall', '-Wextra', '-Werror',
             str(ROOT / 'tests/memory_host.c'), '-o', str(host)])
        run([str(host)])
        core = temporary / 'core-host'
        run(['clang', '-O1', '-g', '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror',
             str(ROOT / 'tests/core_host.c'), '-o', str(core)])
        run([str(core)], env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
        kernel = (ROOT / 'src/kernel.c').read_text()
        kernel = ('void foundation_selftest(void);\n'
                  'void foundation_boot_complete(unsigned long long root);\n' + kernel)
        kernel = kernel.replace('    set_mmu();', '    set_mmu();\n    foundation_selftest();')
        kernel = kernel.replace('    fs_pos = fs_root();',
                                '    fs_pos = fs_root();\n    foundation_boot_complete(fs_pos);')
        (temporary / 'kernel.c').write_text(kernel)
        sources = sorted(p for p in (ROOT / 'src').rglob('*')
                         if p.suffix in ('.c', '.S') and p.name not in ('kernel.c', 'main.c'))
        tree = temporary / 'test_env'
        shutil.copytree(ROOT / 'test_env', tree)
        flags = ['clang', '-target', 'x86_64-pc-windows-msvc', '-ffreestanding',
                 '-fno-builtin', '-nostdlib', '-mno-red-zone', '-mgeneral-regs-only',
                 '-fno-stack-protector', '-fuse-ld=lld']
        run([*flags, '-Wl,-entry:efi_main', '-Wl,-subsystem:efi_application',
             '-o', str(tree / 'EFI/BOOT/bootx64.efi'),
             *[str(ROOT / 'src' / name) for name in
               ('main.c', 'console.c', 'console_renderer.c', 'util.c')]])
        run([*flags, '-Wl,-entry:kernel_entry', '-Wl,-subsystem:native',
             '-Wl,-base:0xffffffff80000000', '-Wl,-fixed', '-Wl,-dynamicbase:no',
             '-I', str(ROOT / 'src'), '-o', str(tree / 'EFI/BOOT/kernel.exe'),
             *map(str, sources), str(temporary / 'kernel.c'),
             str(ROOT / 'tests/foundation_kernel.c'), str(ROOT / 'tests/foundation_probes.S')])
        spec = importlib.util.spec_from_file_location('fat_builder', ROOT / 'make_fat32_image.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        disk = temporary / 'disk.img'
        builder.build_image(tree, disk)
        for memory, storage in [('128M', 'nvme'), ('1G', 'sata'), ('8G', 'nvme')]:
            name = memory + '-' + storage
            variables = temporary / (name + '-vars.fd')
            shutil.copyfile(OVMF / 'OVMF_VARS_4M.fd', variables)
            debug = temporary / (name + '.debug')
            trace = temporary / (name + '.trace')
            command = ['qemu-system-x86_64', '-machine', 'q35,accel=tcg', '-cpu', 'max',
                       '-m', memory, '-display', 'none', '-serial', 'none', '-monitor', 'none',
                       '-net', 'none', '-no-reboot', '-no-shutdown',
                       '-drive', f'if=pflash,format=raw,readonly=on,file={OVMF / "OVMF_CODE_4M.fd"}',
                       '-drive', f'if=pflash,format=raw,file={variables}',
                       '-drive', f'if=none,id=testdisk,format=raw,file={disk}',
                       '-device', ('nvme,drive=testdisk,serial=TEST' if storage == 'nvme'
                                   else 'ide-hd,drive=testdisk,bus=ide.0'),
                       '-debugcon', f'file:{debug}', '-global', 'isa-debugcon.iobase=0xe9',
                       '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
                       '-d', 'int,guest_errors', '-D', str(trace)]
            print('Booting ' + name, flush=True)
            try:
                result = subprocess.run(command, cwd=ROOT, timeout=90, capture_output=True)
            except subprocess.TimeoutExpired:
                print(debug.read_text() if debug.exists() else 'No test markers', flush=True)
                print(trace.read_text()[-6000:] if trace.exists() else 'No trace', flush=True)
                raise
            markers = debug.read_text() if debug.exists() else ""
            print(markers, end='', flush=True)
            if (result.returncode != 33 or 'STORAGE_BOOT_OK' not in markers or
                    'HIGH_KERNEL_HANDOFF_OK' not in markers):
                print(result.stderr.decode(), trace.read_text()[-6000:] if trace.exists() else "No trace")
                raise RuntimeError(name + ' failed')
        print('PASS: all QEMU foundation and storage boots', flush=True)


if __name__ == '__main__':
    main()
