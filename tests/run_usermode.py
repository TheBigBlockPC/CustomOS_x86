#!/usr/bin/env python3
"""Check SYS_EXIT, ring-3 timer delivery, VMM, and visible panic output in QEMU."""
import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OVMF = Path('/usr/share/OVMF')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['exit', 'timer', 'vmm', 'panic'])
    args = parser.parse_args()
    cases = {'exit': 1, 'timer': 2, 'vmm': 3, 'panic': 4}
    with tempfile.TemporaryDirectory(prefix='customos-usermode-') as directory:
        tmp = Path(directory)
        tree = tmp / 'tree'
        boot = tree / 'EFI/BOOT'
        boot.mkdir(parents=True)
        kernel = (ROOT / 'src/kernel.c').read_text()
        kernel = 'void usermode_selftest(void);\nvoid usermode_tick(void);\n' + kernel
        kernel = kernel.replace('lapic_timer_start(0x34, 60, on_timer)',
                                'lapic_timer_start(0x34, 60, usermode_tick)')
        kernel = kernel.replace('    setup_double_buffer();',
                                '    syscall_init();\n    usermode_selftest();')
        (tmp / 'kernel.c').write_text(kernel)
        panic = 'void usermode_panic_check(void);\n' + (ROOT / 'src/panic.c').read_text()
        panic = panic.replace('    while(1){', '    usermode_panic_check();\n    while(1){', 1)
        (tmp / 'panic.c').write_text(panic)
        sources = sorted(p for p in (ROOT / 'src').rglob('*')
                         if p.suffix in ('.c', '.S') and 'programs' not in p.parts
                         and p.name not in ('kernel.c', 'main.c', 'panic.c'))
        flags = ['clang', '-target', 'x86_64-pc-windows-msvc', '-ffreestanding',
                 '-fno-builtin', '-nostdlib', '-mno-red-zone', '-mgeneral-regs-only',
                 '-fno-stack-protector', '-fuse-ld=lld']
        subprocess.run([*flags, '-Wl,-entry:efi_main', '-Wl,-subsystem:efi_application',
                        '-o', str(boot / 'bootx64.efi'),
                        *[str(ROOT / 'src' / name) for name in
                          ('main.c', 'console.c', 'console_renderer.c', 'util.c', 'display_mgr.c')]],
                       check=True, cwd=ROOT)
        spec = importlib.util.spec_from_file_location('fat_builder', ROOT / 'make_fat32_image.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        for name in ([args.case] if args.case else cases):
            subprocess.run([*flags, '-DBUILD_KERNEL', f'-DUSERMODE_CASE={cases[name]}',
                            '-Wl,-entry:kernel_entry', '-Wl,-subsystem:native',
                            '-Wl,-base:0xffffffff80000000', '-Wl,-fixed', '-Wl,-dynamicbase:no',
                            '-I', str(ROOT / 'src'), '-o', str(boot / 'kernel.exe'),
                            *map(str, sources), str(tmp / 'kernel.c'), str(tmp / 'panic.c'),
                            str(ROOT / 'tests/usermode_kernel.c'),
                            str(ROOT / 'tests/usermode_probes.S')], check=True, cwd=ROOT)
            disk = tmp / 'disk.img'
            builder.build_image(tree, disk)
            variables = tmp / 'vars.fd'
            shutil.copyfile(OVMF / 'OVMF_VARS_4M.fd', variables)
            debug, trace = tmp / 'debug', tmp / 'trace'
            command = ['qemu-system-x86_64', '-machine', 'q35,accel=tcg', '-cpu', 'max',
                       '-m', '128M', '-display', 'none', '-serial', 'none', '-monitor', 'none',
                       '-net', 'none', '-no-reboot', '-no-shutdown',
                       '-drive', f'if=pflash,format=raw,readonly=on,file={OVMF / "OVMF_CODE_4M.fd"}',
                       '-drive', f'if=pflash,format=raw,file={variables}',
                       '-drive', f'format=raw,file={disk}',
                       '-debugcon', f'file:{debug}', '-global', 'isa-debugcon.iobase=0xe9',
                       '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
                       '-d', 'int,guest_errors', '-D', str(trace)]
            print('Booting usermode case: ' + name, flush=True)
            try:
                result = subprocess.run(command, cwd=ROOT, timeout=45, capture_output=True)
            except subprocess.TimeoutExpired:
                print(debug.read_text() if debug.exists() else 'No test markers')
                print(trace.read_text()[-6000:] if trace.exists() else 'No trace')
                raise
            print(debug.read_text(), end='', flush=True)
            if result.returncode != 33:
                print(result.stderr.decode(), trace.read_text()[-6000:])
                raise RuntimeError(name + ' failed')
        print('PASS: selected usermode/VMM QEMU checks', flush=True)


if __name__ == '__main__':
    main()
