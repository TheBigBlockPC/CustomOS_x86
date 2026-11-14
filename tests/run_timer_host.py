#!/usr/bin/env python3
"""Test the LAPIC timer using fake clocks; substitute only privileged IF access."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / 'src/interrupts/lapic_timer.c').read_text()
    first = source.index('static uint64_t timer_irq_save(void) {')
    last = source.index('static void timer_isr(void) {', first)
    source = source[:first] + '''static uint64_t timer_irq_save(void) {
    return model_irq_save();
}
static void timer_irq_restore(uint64_t flags) {
    model_irq_restore(flags);
}

''' + source[last:]
    with tempfile.TemporaryDirectory(prefix='customos-timer-host-') as directory:
        temporary = Path(directory)
        (temporary / 'lapic_timer_model.inc').write_text(source)
        executable = temporary / 'timer-host'
        subprocess.run(['clang', '-O2', '-g', '-fsanitize=address,undefined',
                        '-Wall', '-Wextra', '-Werror', '-I', str(temporary),
                        '-I', str(ROOT / 'src/interrupts'),
                        str(ROOT / 'tests/lapic_timer_host.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})


if __name__ == '__main__':
    main()
