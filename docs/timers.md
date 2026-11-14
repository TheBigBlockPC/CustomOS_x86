# Local APIC periodic timer

HPET supplies a polling clock and the calibration reference. Its comparator
interrupts, FSB/MSI delivery, and legacy PIT/RTC replacement are disabled. The
60 Hz display interrupt comes from the CPU's local APIC timer, so it needs no
I/O APIC GSI or external interrupt polarity setting.

After initializing the IDT, paging, ACPI, APIC and HPET:

```c
#include "interrupts/lapic_timer.h"

static void on_tick(void) {
    /* Keep callbacks short. The driver sends EOI after this returns. */
}

if (!lapic_timer_start(0x34, 60, on_tick))
    panic("LAPIC timer failed");
/* Enable CPU interrupts when the rest of the kernel is ready. */
```

The first argument is the actual IDT vector, without an IRQ-base offset. Valid
vectors are `0x20` through `0xFD`; the chosen vector must have no existing ISR.
The second argument is the desired frequency in Hz. The driver rounds the
reload count to the nearest representable interval. Zero frequencies, null
callbacks and reload counts outside the 32-bit counter range fail.

The first start automatically calibrates the divide-by-16 LAPIC counter using
three 10 ms HPET measurements and takes their median. It uses measured elapsed
HPET ticks, including polling delays. Calibration runs with the local timer
masked and CPU interrupts disabled, never enables HPET interrupts, and has a
poll limit for a stalled clock. `lapic_timer_init()` can perform this step
explicitly during boot. It caches the calibration for later starts.

Calling `lapic_timer_start(0x34, 120, on_tick)` changes the frequency and callback
on the same reserved vector. Invalid requests preserve a running timer.
`lapic_timer_stop()` masks the timer and stops the counter. Its ISR remains
registered to acknowledge a possible pending delivery without calling the
callback; the vector stays reserved. These operations preserve the caller's
interrupt-enable flag. Callbacks run in interrupt context and must not send
another EOI.

This driver currently stores state for the BSP, matching the kernel's boot
model. APIC register access handles both xAPIC MMIO and an already enabled
x2APIC mode. HPET MMIO is uncached. Its 32-bit main-counter variant is extended
in software and must be read at least once per wrap, including while the local
timer is stopped. The periodic ISR also samples it while running.

## Validation

Run `python3 tests/run_timer_host.py` for simulated-clock checks with ASan and
UBSan. They cover calibration noise and polling overshoot, stalled clocks,
invalid requests, occupied vectors, callback dispatch, EOI, frequency changes,
stop/restart and preservation of the interrupt-enable flag.

Run `python3 tests/run_foundation.py --timer-only` for memory/exception checks
and three QEMU/OVMF timer boots (128 MiB, 1 GiB, 8 GiB). This mode finishes before
kernel storage initialization. Without the flag the suite also checks storage
(128 MiB NVMe, 1 GiB SATA, 8 GiB NVMe). The timer checks
verify disabled HPET interrupt sources, actual 60/120 Hz local interrupts,
stopping, restarting, and rejected configurations. These headless builds use a
small counter callback in place of the display copy: a byte-wise full-frame
copy on every interrupt can monopolize the emulated TCG CPU. Production builds
retain the display callback.

Hardware register behavior follows the [Intel SDM, volume 3A, chapter 10](https://cdrdv2-public.intel.com/835754/253668-sdm-vol-3a.pdf)
and the [IA-PC HPET specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf).
