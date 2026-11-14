/* Runs inside the instrumented UEFI/QEMU foundation kernel. */
#include "types.h"
#include "acpi.h"
#include "hpet.h"
#include "interrupts/apic.h"
#include "interrupts/lapic_timer.h"

static volatile uint32_t ticks;
static void count_tick(void) { ticks++; }
void timer_boot_tick(void) { ticks++; }

static void debug(char* text) {
    while (*text) __asm__ volatile("outb %0, $0xe9" :: "a"((uint8_t)*text++));
}
#define REQUIRE(condition) do { if (!(condition)) { \
    debug("FAIL: " #condition "\n"); \
    __asm__ volatile("outb %0, $0xf4" :: "a"((uint8_t)0x11)); \
    for (;;) __asm__ volatile("cli; hlt"); } } while (0)

static void wait_ms(uint64_t duration) {
    uint64_t start = hpet_millis();
    while (hpet_millis() - start < duration) __asm__ volatile("pause");
}

void timer_selftest(void) {
    /* HPET contributes only its counter; every comparator's interrupt and
     * FSB/MSI enable must remain off throughout LAPIC operation. */
    const uint8_t* table = (const uint8_t*)acpi_find_table("HPET");
    volatile uint8_t* base = (volatile uint8_t*)(*(const uint64_t*)(table + 44));
    uint64_t caps = *(volatile uint64_t*)base;
    REQUIRE((*(volatile uint64_t*)(base + 0x10) & 3) == 1);
    for (uint32_t i = 0; i <= ((caps >> 8) & 31); i++)
        REQUIRE(!(*(volatile uint64_t*)(base + 0x100 + i * 0x20) & ((1U << 2) | (1U << 14))));
    REQUIRE(lapic_timer_frequency() > 0);
    REQUIRE(lapic_timer_start(0x34, 60, count_tick));
    REQUIRE((lapic_read(0x320) & 0x700FF) == ((1U << 17) | 0x34));
    REQUIRE((lapic_read(0x3E0) & 0xB) == 3);
    ticks = 0;
    wait_ms(500);
    REQUIRE(ticks >= 28 && ticks <= 32);

    lapic_timer_stop();
    uint32_t stopped = ticks;
    REQUIRE(lapic_read(0x320) & (1U << 16));
    REQUIRE(lapic_read(0x380) == 0);
    wait_ms(100);
    REQUIRE(ticks == stopped);

    ticks = 0;
    REQUIRE(lapic_timer_start(0x34, 120, count_tick));
    wait_ms(250);
    REQUIRE(ticks >= 28 && ticks <= 32);
    uint32_t reload = lapic_read(0x380);
    REQUIRE(!lapic_timer_start(0x34, 0, count_tick));
    REQUIRE(!lapic_timer_start(0x1F, 60, count_tick));
    REQUIRE(!lapic_timer_start(0xFE, 60, count_tick));
    REQUIRE(!lapic_timer_start(0x34, 60, 0));
    REQUIRE(!lapic_timer_start(0x35, 60, count_tick));
    REQUIRE(lapic_read(0x380) == reload);
    REQUIRE(lapic_timer_start(0x34, 60, timer_boot_tick));
    debug("LAPIC_TIMER_OK\n");
}

void timer_test_complete(void) {
    timer_selftest();
    __asm__ volatile("outb %0, $0xf4" :: "a"((uint8_t)0x10));
    for (;;) __asm__ volatile("cli; hlt");
}
