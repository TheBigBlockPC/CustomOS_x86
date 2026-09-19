#ifndef INTERRUPTS_H
#define INTERRUPTS_H

typedef void (*isr_t)(void);

void interrupts_init();
void interrupts_set_isr(uint8_t index, isr_t handler);
#endif