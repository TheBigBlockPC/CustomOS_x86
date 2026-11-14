#ifndef HPET_H
#define HPET_H

#include "types.h"

/* HPET is a polling clock only. All comparator interrupts, MSI delivery and
 * legacy PIT/RTC replacement are disabled by initialization. */
void hpet_init(void);
uint64_t hpet_frequency(void);
/* BSP only. A 32-bit counter must be sampled at least once per wrap; the LAPIC
 * timer ISR samples it too. Returns extended ticks, not milliseconds. */
uint64_t hpet_read_counter(void);
uint64_t hpet_millis(void);
void hpet_debug(void);
#endif
