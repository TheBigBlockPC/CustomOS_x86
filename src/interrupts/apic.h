#ifndef APIC_H
#define APIC_H
#include "../types.h"
void apic_init(void);

/* Register offsets use the xAPIC byte layout; accessors handle x2APIC too. */
bool lapic_is_ready(void);
uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t value);
uint32_t lapic_get_id(void);
void lapic_eoi(void);

bool ioapic_route_irq(
    uint32_t gsi,
    uint8_t vector,
    bool level_triggered,
    bool active_low
);
void ioapic_debug(uint32_t gsi);
void lapic_debug(uint8_t vector);
#endif
