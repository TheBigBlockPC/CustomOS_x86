#include "../types.h"
#include "interrupts.h"
#include "../console.h"
#include "../util.h"
#include "../panic.h"

_Static_assert(sizeof(exception_fx_state_t) == 512, "FXSAVE frame size");
_Static_assert(__builtin_offsetof(exception_frame_t, cr0) == 64, "CR0 assembly offset");
_Static_assert(__builtin_offsetof(exception_frame_t, vector) == 192, "vector assembly offset");
_Static_assert(__builtin_offsetof(exception_frame_t, rip) == 208, "CPU frame assembly offset");
_Static_assert(sizeof(exception_frame_t) == 248, "exception frame size");



static isr_t handlers[256];
static exception_handler_t exception_handlers[32];

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
    if (index >= 32) handlers[index] = handler;
}

void exceptions_set_handler(uint8_t vector, exception_handler_t handler)
{
    if (vector < 32) exception_handlers[vector] = handler;
}

void exceptions_dispatch(exception_frame_t* frame)
{
    if (frame->vector >= 32) panic("Invalid exception vector");
    exception_handler_t handler = exception_handlers[frame->vector];
    if (handler) {
        handler(frame);
        return;
    }
    con_write("Unhandled exception ", 0x01);
    print_uint64(frame->vector);
    con_write(" RIP=", 0x07);
    print_uint64(frame->rip);
    con_write(" error=", 0x07);
    print_uint64(frame->error_code);
    if (frame->vector == 14) {
        con_write(" CR2=", 0x07);
        print_uint64(frame->cr2);
    }
    con_write("\n", 0x07);
    panic("No exception handler registered");
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

extern void (*interrupt_stub_table[256])(void);

void interrupts_init(){

    for (uint16_t i = 0; i < 256; i++) {
        idt_set_entry(
            (uint8_t)i,
            interrupt_stub_table[i]
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