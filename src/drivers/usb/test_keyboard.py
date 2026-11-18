#!/usr/bin/env python3
"""Build isolated test kernels and exercise USB input through QMP (QEMU/OVMF).
No production boot images are overwritten. Run from any directory.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import select
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
OVMF = Path('/usr/share/OVMF')
FLAGS = ['clang', '-target', 'x86_64-pc-windows-msvc', '-ffreestanding',
         '-fno-builtin', '-nostdlib', '-mno-red-zone', '-mgeneral-regs-only',
         '-fno-stack-protector', '-fuse-ld=lld']
PROBE = r'''
#include "types.h"
static void byte(uint8_t c) { __asm__ volatile("outb %0, $0xe9" :: "a"(c)); }
void usb_test_text(const char* p) { while (*p) byte(*p++); }
static void hex(uint8_t c) { const char* h="0123456789abcdef"; byte(h[c>>4]); byte(h[c&15]); }
void usb_test_event(uint8_t scan, char ascii, int down) {
    usb_test_text("KEY "); hex(scan); byte(' '); hex((uint8_t)ascii);
    byte(' '); byte(down?'1':'0'); byte('\n');
}
void usb_test_line(const char* p) { usb_test_text("LINE "); usb_test_text(p); byte('\n'); }
'''


def run(command):
    subprocess.run(command, check=True, cwd=ROOT)


def host(directory):
    for name in ('keyboard', 'xhci'):
        executable = directory / (name + '-host')
        run(['clang', '-x', 'c', '-DBUILD_KERNEL', '-O1', '-g', '-fno-builtin',
             '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror',
             str(ROOT / f'src/drivers/usb/test_{name}_host.inc'), '-o', str(executable)])
        subprocess.run([str(executable)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})


def build(directory):
    tree = directory / 'test_env'
    shutil.copytree(ROOT / 'test_env', tree)
    (tree / 'EFI/BOOT').mkdir(parents=True, exist_ok=True)
    console = (ROOT / 'src/console.c').read_text()
    console = 'void usb_test_text(const char*);\n' + console
    console = console.replace('void con_write(char* string, uint8_t color){',
                              'void con_write(char* string, uint8_t color){\n    usb_test_text(string);')
    (directory / 'console.c').write_text(console)
    kernel = (ROOT / 'src/kernel.c').read_text()
    kernel = 'void usb_test_text(const char*);\nvoid usb_test_line(const char*);\n' + kernel
    # Keep the timer, but avoid a full framebuffer copy on each headless TCG tick.
    kernel = kernel.replace('    present();\n}', '    /* Headless test callback. */\n}')
    kernel = kernel.replace('void on_input(char* input){',
                            'void on_input(char* input){\n    usb_test_line(input);')
    kernel = kernel.replace('    while(1){\n        poll_events();',
                            '    usb_test_text("INPUT_READY\\n");\n    while(1){\n        poll_events();')
    (directory / 'kernel.c').write_text(kernel)
    source = (ROOT / 'src/input_subsystem.c').read_text()
    source = '#include "types.h"\nvoid usb_test_event(uint8_t, char, int);\n' + source
    for table in ['scan_to_ascii', 'scan_to_ascii_shift']:
        call = f'key_handler(scan_code,{table}[normalized_scan],is_down);'
        source = source.replace(call, f'usb_test_event(scan_code,{table}[normalized_scan],is_down);\n        ' + call)
    (directory / 'input_subsystem.c').write_text(source)
    (directory / 'probe.c').write_text(PROBE)
    common = ['-I', str(ROOT / 'src')]
    run([*FLAGS, *common, '-Wl,-entry:efi_main', '-Wl,-subsystem:efi_application',
         '-o', str(tree / 'EFI/BOOT/bootx64.efi'), str(directory / 'console.c'),
         str(directory / 'probe.c'), *[str(ROOT / 'src' / name) for name in
         ('main.c', 'console_renderer.c', 'util.c', 'display_mgr.c')]])
    sources = sorted(p for p in (ROOT / 'src').rglob('*') if p.suffix in ('.c', '.S')
                     and 'programs' not in p.parts and p.name not in
                     ('main.c', 'kernel.c', 'console.c', 'input_subsystem.c'))
    run([*FLAGS, *common, '-DBUILD_KERNEL', '-Wl,-entry:kernel_entry', '-Wl,-subsystem:native',
         '-Wl,-base:0xffffffff80000000', '-Wl,-fixed', '-Wl,-dynamicbase:no',
         '-o', str(tree / 'EFI/BOOT/kernel.exe'), *map(str, sources),
         *[str(directory / name) for name in ('kernel.c', 'console.c', 'input_subsystem.c', 'probe.c')]])
    spec = importlib.util.spec_from_file_location('fat_builder', ROOT / 'make_fat32_image.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    disk = directory / 'disk.img'
    builder.build_image(tree, disk)
    return disk


class Machine:
    def __init__(self, directory, disk, name, devices, memory='128M'):
        self.log = directory / (name + '.debug')
        self.stderr = directory / (name + '.stderr')
        variables = directory / (name + '.vars.fd')
        shutil.copyfile(OVMF / 'OVMF_VARS_4M.fd', variables)
        command = ['qemu-system-x86_64', '-machine', 'q35,accel=tcg,i8042=off',
                   '-cpu', 'max', '-m', memory, '-display', 'none', '-serial', 'none',
                   '-monitor', 'none', '-net', 'none', '-no-reboot', '-no-shutdown',
                   '-drive', f'if=pflash,format=raw,readonly=on,file={OVMF / "OVMF_CODE_4M.fd"}',
                   '-drive', f'if=pflash,format=raw,file={variables}',
                   '-drive', f'if=none,id=disk,format=raw,snapshot=on,file={disk}',
                   '-device', 'nvme,drive=disk,serial=TEST',
                   '-debugcon', f'file:{self.log}', '-global', 'isa-debugcon.iobase=0xe9',
                   '-qmp', 'stdio']
        for device in devices:
            command += ['-device', device]
        self.err = self.stderr.open('wb')
        self.process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.err, bufsize=0,
                                        env={**os.environ, 'TMPDIR': str(directory)})
        try:
            self.read()
            self.qmp('qmp_capabilities')
            self.wait('INPUT_READY', timeout=60)
        except BaseException:
            self.close()
            raise

    def read(self):
        if not select.select([self.process.stdout], [], [], 10)[0]:
            raise TimeoutError('QMP response timeout')
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError(self.stderr.read_text())
        return json.loads(line)

    def qmp(self, command, arguments=None):
        request = {'execute': command}
        if arguments is not None:
            request['arguments'] = arguments
        self.process.stdin.write(json.dumps(request).encode() + b'\n')
        while True:
            response = self.read()
            if 'error' in response:
                raise RuntimeError(response)
            if 'return' in response:
                return response['return']

    def text(self):
        return self.log.read_text(errors='replace') if self.log.exists() else ''

    def wait(self, marker, after=0, timeout=5):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if marker in self.text()[after:]:
                return
            if self.process.poll() is not None:
                break
            time.sleep(.005)
        raise AssertionError(f'Missing {marker!r}\n{self.text()[-8000:]}\n{self.stderr.read_text()}')

    def keys(self, changes):
        self.qmp('input-send-event', {'events': [
            {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': key}}}
            for key, down in changes]})

    def tap(self, key, scan, ascii):
        offset = len(self.text())
        self.keys([(key, True)])
        self.wait(f'KEY {scan:02x} {ascii:02x} 1', offset)
        offset = len(self.text())
        self.keys([(key, False)])
        self.wait(f'KEY {scan | 0x80:02x} {ascii:02x} 0', offset)

    def close(self):
        if hasattr(self, 'process'):
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if hasattr(self, 'process'):
            self.process.stdin.close()
            self.process.stdout.close()
        self.err.close()


def integration(directory):
    disk = build(directory)
    for name, devices in [('no-controller', []), ('empty-controller', ['qemu-xhci,id=xhci'])]:
        machine = Machine(directory, disk, name, devices)
        try:
            assert 'USB: boot keyboard ready' not in machine.text()
            print(f'PASS: {name} reaches input loop', flush=True)
        finally:
            machine.close()
    scenarios = [
        ('keyboard', '128M', ['qemu-xhci,id=xhci'], 'xhci.0', '2', None),
        ('mouse-then-keyboard', '8G', ['qemu-xhci,id=xhci',
            'usb-mouse,bus=xhci.0,port=1'], 'xhci.0', '2', None),
        ('second-controller', '128M', ['qemu-xhci,id=first',
            'qemu-xhci,id=xhci'], 'xhci.0', '2', None),
        ('second-controller-after-mouse', '8G', ['qemu-xhci,id=first',
            'usb-mouse,bus=first.0,port=1', 'qemu-xhci,id=xhci'], 'xhci.0', '2', None),
        ('hub-keyboard', '128M', ['qemu-xhci,id=xhci',
            'usb-hub,id=hub,bus=xhci.0,port=1'], 'xhci.0', '1.3', None),
        ('hub-mouse-then-keyboard', '128M', ['qemu-xhci,id=xhci',
            'usb-hub,id=hub,bus=xhci.0,port=1',
            'usb-mouse,bus=xhci.0,port=1.1'], 'xhci.0', '1.3', None),
        ('nested-hubs-second-controller', '8G', ['qemu-xhci,id=first',
            'qemu-xhci,id=xhci', 'usb-hub,id=outer,bus=xhci.0,port=1',
            'usb-hub,id=hub,bus=xhci.0,port=1.2'], 'xhci.0', '1.2.3', None),
        ('hub-detach', '128M', ['qemu-xhci,id=xhci',
            'usb-hub,id=hub,bus=xhci.0,port=1'], 'xhci.0', '1.3', 'hub'),
        ('nested-hub-detach', '128M', ['qemu-xhci,id=xhci',
            'usb-hub,id=outer,bus=xhci.0,port=1',
            'usb-hub,id=hub,bus=xhci.0,port=1.2'], 'xhci.0', '1.2.3', 'hub'),
        ('powered-hub', '128M', ['qemu-xhci,id=xhci',
            'usb-hub,id=hub,bus=xhci.0,port=1,port-power=on'], 'xhci.0', '1.3', None),
        ('empty-nested-hub-then-keyboard', '128M', ['qemu-xhci,id=xhci',
            'usb-hub,id=hub,bus=xhci.0,port=1',
            'usb-hub,id=empty,bus=xhci.0,port=1.1'], 'xhci.0', '1.3', None),
        ('five-hub-depth', '128M', ['qemu-xhci,id=xhci',
            *[f'usb-hub,id=hub{i},bus=xhci.0,port=' + '.'.join(['1'] * (i + 1))
              for i in range(5)]], 'xhci.0', '1.1.1.1.1.3', None),
        ('empty-hub-then-root-keyboard', '128M', ['qemu-xhci,id=xhci',
            'usb-hub,id=hub,bus=xhci.0,port=1'], 'xhci.0', '2', None),
    ]
    for name, memory, devices, bus, port, detach_hub in scenarios:
        machine = Machine(directory, disk, name, [*devices,
                          f'usb-kbd,id=keyboard,bus={bus},port={port}'], memory)
        try:
            assert 'USB: boot keyboard ready' in machine.text(), machine.text()
            machine.tap('a', 0x1e, ord('a'))
            offset = len(machine.text())
            machine.keys([('shift_r', True), ('a', True)])
            machine.wait('KEY 1e 41 1', offset)
            machine.keys([('a', False)])
            machine.wait('KEY 9e 41 0', offset)
            machine.keys([('shift_r', False)])
            machine.wait('KEY b6 00 0', offset)
            machine.tap('y', 0x15, ord('z'))  # Existing German layout.
            machine.tap('backspace', 0x0e, 8)
            # Real TTY editing and command submission, not just a test callback.
            for key, scan, ascii in [('f', 0x21, ord('f')), ('x', 0x2d, ord('x')),
                                     ('backspace', 0x0e, 8), ('s', 0x1f, ord('s'))]:
                machine.tap(key, scan, ascii)
            # Earlier keys left aA in the TTY; clear those before checking fs.
            for _ in range(4):
                machine.tap('backspace', 0x0e, 8)
            machine.tap('f', 0x21, ord('f'))
            machine.tap('s', 0x1f, ord('s'))
            machine.tap('ret', 0x1c, 10)
            machine.wait('LINE fs\n')
            for _ in range(140):  # >255 reports: wrap both transfer and event rings.
                machine.tap('a', 0x1e, ord('a'))
            offset = len(machine.text())
            machine.keys([('b', True)])
            machine.wait('KEY 30 62 1', offset)
            end = time.monotonic() + 3
            while machine.text()[offset:].count('KEY 30 62 1') < 3:
                assert time.monotonic() < end, 'Missing software repeat'
                time.sleep(.02)
            machine.keys([('b', False)])
            machine.wait('KEY b0 62 0', offset)
            assert 'xHCI: stopped' not in machine.text(), machine.text()
            assert 'xHCI: command completion error' not in machine.text(), machine.text()
            assert 'xHCI: control transfer error' not in machine.text(), machine.text()
            print(f'PASS: {name} ({memory}): typing, release, Shift, German layout, TTY, ring wrap, repeat', flush=True)
            offset = len(machine.text())
            machine.keys([('a', True)])
            machine.wait('KEY 1e 61 1', offset)
            machine.qmp('device_del', {'id': detach_hub or 'keyboard'})
            machine.wait('KEY 9e 61 0', offset)
            assert 'xHCI: stopped' not in machine.text(), machine.text()
            assert 'xHCI: control transfer error' not in machine.text(), machine.text()
            print(f'PASS: {detach_hub or "keyboard"} detach releases held key', flush=True)
        finally:
            machine.close()


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='customos-usb-') as temporary:
        host(Path(temporary))
        integration(Path(temporary))
