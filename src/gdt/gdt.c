#include "../types.h"
#include "../tss.h"
#include "gdt.h"
#define GDT_ACCESS_PRESENT      (1u << 7)
#define GDT_ACCESS_RING0        (0u << 5)
#define GDT_ACCESS_DESCRIPTOR   (1u << 4)
#define GDT_ACCESS_EXECUTABLE   (1u << 3)
#define GDT_ACCESS_RW           (1u << 1)
#define GDT_ACCESS_RING3        (3u << 5)

#define GDT_FLAG_GRANULARITY    (1u << 3)
#define GDT_FLAG_LONG_MODE      (1u << 1)

static uint64_t gdt_descriptor(
    uint32_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t flags)
{
    uint64_t desc = 0;

    desc |= (uint64_t)(limit & 0xFFFF);
    desc |= (uint64_t)(base & 0xFFFF) << 16;
    desc |= (uint64_t)((base >> 16) & 0xFF) << 32;
    desc |= (uint64_t)access << 40;
    desc |= (uint64_t)((limit >> 16) & 0xF) << 48;
    desc |= (uint64_t)(flags & 0xF) << 52;
    desc |= (uint64_t)((base >> 24) & 0xFF) << 56;

    return desc;
}

static uint64_t gdt[7];

struct __attribute__((packed)) gdtr {
    uint16_t limit;
    uint64_t base;
};

extern void gdt_reload_segments(void);

void gdt_init(void)
{
    /*
     * Entry 0: mandatory null descriptor.
     */
    gdt[0] = 0;

    /*
     * Entry 1: 64-bit ring-0 code.
     * Selector = 1 * 8 = 0x08.
     */
    gdt[1] = gdt_descriptor(
        0,
        0xFFFFF,
        GDT_ACCESS_PRESENT |
        GDT_ACCESS_DESCRIPTOR |
        GDT_ACCESS_EXECUTABLE |
        GDT_ACCESS_RW,
        GDT_FLAG_GRANULARITY |
        GDT_FLAG_LONG_MODE
    );

    /*
     * Entry 2: ring-0 data.
     * Selector = 2 * 8 = 0x10.
     */
    gdt[2] = gdt_descriptor(
        0,
        0xFFFFF,
        GDT_ACCESS_PRESENT |
        GDT_ACCESS_RING0 |
        GDT_ACCESS_DESCRIPTOR |
        GDT_ACCESS_RW,
        GDT_FLAG_GRANULARITY
    );

    /*
     * Entry 3: 64-bit ring-3 code.
     * Selector = 3 * 8 = 0x18.
     */
    gdt[3] = gdt_descriptor(
        0,
        0xFFFFF,
        GDT_ACCESS_PRESENT |
        GDT_ACCESS_DESCRIPTOR |
        GDT_ACCESS_EXECUTABLE |
        GDT_ACCESS_RW |
        GDT_ACCESS_RING3,
        GDT_FLAG_GRANULARITY |
        GDT_FLAG_LONG_MODE
    );

    /*
     * Entry 4: ring-3 data.
     * Selector = 4 * 8 = 0x20.
     */
    gdt[4] = gdt_descriptor(
        0,
        0xFFFFF,
        GDT_ACCESS_PRESENT |
        GDT_ACCESS_DESCRIPTOR |
        GDT_ACCESS_RW |
        GDT_ACCESS_RING3,
        GDT_FLAG_GRANULARITY
    );

    struct gdtr descriptor = {
        .limit = sizeof(gdt) - 1,
        .base  = (uint64_t)gdt
    };

    __asm__ volatile (
        "lgdt %0"
        :
        : "m"(descriptor)
        : "memory"
    );

    gdt_reload_segments();
}

void gdt_set_tss(void) {
    uint64_t base = (uint64_t)tss_get_pointer();
    uint32_t limit = sizeof(tss64_t) - 1;

    uint64_t low =
        (limit & 0xFFFFULL) |
        ((base & 0xFFFFFFULL) << 16) |
        ((uint64_t)0x89 << 40) |
        (((uint64_t)(limit >> 16) & 0xF) << 48) |
        (((base >> 24) & 0xFFULL) << 56);

    uint64_t high = base >> 32;

    gdt[GDT_TSS_INDEX] = low;
    gdt[GDT_TSS_INDEX + 1] = high;
    gdt_reload_segments();
}