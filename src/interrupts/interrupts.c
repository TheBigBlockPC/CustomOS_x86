#include "../types.h"
#include "interrupts.h"



static isr_t handlers[256];

struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};

struct __attribute__((packed)) idtr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry idt[256];

void interrupts_set_isr(uint8_t index, isr_t handler)
{
    handlers[index] = handler;
}

static uint16_t get_cs(void)
{
    uint16_t cs;

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(cs)
    );

    return cs;
}

static void idt_set_entry(uint8_t index, void (*handler)(void))
{
    uint64_t address = (uint64_t)handler;

    idt[index].offset_low  = (uint16_t)(address & 0xFFFF);
    idt[index].offset_mid  = (uint16_t)((address >> 16) & 0xFFFF);
    idt[index].offset_high = (uint32_t)((address >> 32) & 0xFFFFFFFF);

    idt[index].selector = get_cs();
    idt[index].ist      = 0;
    idt[index].type_attr = 0x8E;
    idt[index].zero     = 0;
}

void interrupts_dispatch(uint8_t index)
{
    isr_t handler = handlers[index];

    if (handler != 0) {
        handler();
    }
}

extern void (*interrupt_stub_table[224])(void);

void interrupts_init(){

    for (uint16_t i = 32; i < 256; i++) {
        idt_set_entry(
            (uint8_t)i,
            interrupt_stub_table[i - 32]
        );
    }

    struct idtr descriptor = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt
    };

    __asm__ volatile (
        "lidt %0"
        :
        : "m"(descriptor)
        : "memory"
    );
}