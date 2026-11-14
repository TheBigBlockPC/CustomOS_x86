#ifndef LAPIC_TIMER_H
#define LAPIC_TIMER_H

#include "interrupts.h"

/* BSP only, after apic_init() and hpet_init(). Measures the divided LAPIC clock
 * against HPET over three 10 ms samples, with the timer interrupt masked.
 * Initialization is cached; start() calls it automatically on first use. */
bool lapic_timer_init(void);
/* vector is an IDT interrupt vector (0x20..0xFD), not a PIC IRQ or GSI.
 * The first successful start reserves this unused vector. Further starts may
 * change frequency/callback on the same vector. Invalid requests leave a
 * running timer unchanged. Preserves IF; callback runs in interrupt context
 * after the caller enables interrupts. The driver sends LAPIC EOI. */
bool lapic_timer_start(uint8_t vector, uint32_t hz, isr_t callback);
/* Stops counting and masks delivery. The vector remains reserved so a pending
 * interrupt can still be acknowledged safely, without calling the callback. */
void lapic_timer_stop(void);
/* Calibrated counter ticks/second with the driver's divide-by-16 setting. */
uint64_t lapic_timer_frequency(void);

#endif
