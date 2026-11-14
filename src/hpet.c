#include "hpet.h"
#include "acpi.h"
#include "paging.h"
#include "util.h"
#include "console.h"
#include "panic.h"

#define HPET_CAPABILITIES       0x000
#define HPET_CONFIG             0x010
#define HPET_INT_STATUS         0x020
#define HPET_COUNTER            0x0F0
#define HPET_TIMER_CONFIG(n)    (0x100 + (n) * 0x20)
#define HPET_ENABLE             (1ULL << 0)
#define HPET_COUNTER_64BIT      (1ULL << 13)

#define TIMER_INT_ENABLE        (1ULL << 2)
#define TIMER_PERIODIC          (1ULL << 3)
#define TIMER_VALUE_SET         (1ULL << 6)
#define TIMER_FSB_ENABLE        (1ULL << 14)
#define POLL_LIMIT              1000000U

/* ACPI HPET table, including the SDT header. */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t event_timer_block_id;
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} hpet_table_t;

_Static_assert(sizeof(hpet_table_t) == 56, "ACPI HPET table size");

static volatile uint8_t* hpet_base;
static uint64_t frequency;
static bool counter_64bit;
static uint32_t counter_last;
static uint64_t counter_high;
static inline void hpet_write(uint32_t offset, uint64_t value) {
    *(volatile uint64_t*)(hpet_base + offset) = value;
}

static inline uint64_t hpet_read(uint32_t offset) {
    return *(volatile uint64_t*)(hpet_base + offset);
}

static uint64_t cpu_flags(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    return flags;
}

static void cpu_interrupts(bool enabled) {
    if (enabled)
        __asm__ volatile("sti" ::: "memory");
    else
        __asm__ volatile("cli" ::: "memory");
}

uint64_t hpet_read_counter(void) {
    if (counter_64bit)
        return hpet_read(HPET_COUNTER);

    /* Extend 32-bit counters across wraps. The caller must sample at least
     * once per wrap; the LAPIC timer ISR does this during normal operation. */
    uint64_t flags = cpu_flags();
    cpu_interrupts(false);
    uint32_t low = (uint32_t)hpet_read(HPET_COUNTER);
    if (low < counter_last)
        counter_high += 1ULL << 32;
    counter_last = low;
    uint64_t ticks = counter_high | low;
    if (flags & (1ULL << 9)) cpu_interrupts(true);
    return ticks;
}

uint64_t hpet_millis(void) {
    uint64_t ticks = hpet_read_counter();
    return (ticks / frequency) * 1000 + (ticks % frequency) * 1000 / frequency;
}

void print_timer(void) {
    con_write("Time: ", 0x07);
    print_uint64_full(hpet_millis());
    con_write("\n", 0x07);
}

void sleep(uint64_t time) {
    uint64_t start = hpet_millis();
    while (hpet_millis() - start < time) __asm__ volatile("pause");
}

void hpet_init(void) {
    const hpet_table_t* table = (const hpet_table_t*)acpi_find_table("HPET");
    if (table->header.length < sizeof(*table) || table->address_space_id != 0 ||
        table->register_bit_offset != 0 || !table->address || (table->address & 7))
        panic("HPET: invalid MMIO table");

    hpet_base = (volatile uint8_t*)table->address;
    mmu_identity_map_range(table->address, 4096, PAGE_NX | PAGE_RW | PAGE_UC);
    uint64_t caps = hpet_read(HPET_CAPABILITIES);
    uint32_t period_fs = caps >> 32;
    if (!period_fs || period_fs > 100000000U)
        panic("HPET: invalid counter period");
    frequency = 1000000000000000ULL / period_fs;
    uint32_t timer_count = ((caps >> 8) & 0x1F) + 1;
    counter_64bit = (caps & HPET_COUNTER_64BIT) != 0;

    /* Take ownership from firmware before resetting the counter. Disable all
     * inherited comparator/MSI sources and legacy PIT/RTC replacement. */
    hpet_write(HPET_CONFIG, 0);
    (void)hpet_read(HPET_CONFIG);
    for (uint32_t timer = 0; timer < timer_count; timer++) {
        uint64_t cfg = hpet_read(HPET_TIMER_CONFIG(timer));
        cfg &= ~(TIMER_INT_ENABLE | TIMER_FSB_ENABLE | TIMER_PERIODIC | TIMER_VALUE_SET);
        hpet_write(HPET_TIMER_CONFIG(timer), cfg);
    }
    hpet_write(HPET_INT_STATUS, (1ULL << timer_count) - 1);
    hpet_write(HPET_COUNTER, 0);
    counter_high = counter_last = 0;
    hpet_write(HPET_CONFIG, HPET_ENABLE);
    for (uint32_t poll = 0; poll < POLL_LIMIT; poll++) {
        if (hpet_read_counter() != 0) {
            con_write("HPET frequency: ", 0x03);
            print_uint64_dec(frequency);
            con_write(" Hz\n", 0x03);
            return;
        }
        __asm__ volatile("pause");
    }
    panic("HPET: main counter is not advancing");
}

uint64_t hpet_frequency(void) {
    return frequency;
}

void hpet_debug(void) {
    con_write("\nHPET counter: ", 0x07);
    print_uint64_full(hpet_read_counter());
    con_write("\nHPET configuration: ", 0x07);
    print_uint64_full(hpet_read(HPET_CONFIG));
    con_write("\n", 0x07);
}
