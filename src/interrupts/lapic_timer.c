#include "lapic_timer.h"
#include "apic.h"
#include "../hpet.h"
#include "../console.h"
#include "../util.h"

#define LVT_TIMER           0x320
#define TIMER_INITIAL       0x380
#define TIMER_CURRENT       0x390
#define TIMER_DIVIDE        0x3E0
#define TIMER_MASKED        (1U << 16)
#define TIMER_PERIODIC      (1U << 17)
#define DIVIDE_BY_16        0x3
#define CALIBRATION_VECTOR  0xFE
#define CALIBRATION_POLLS   1000000U

/* The kernel currently boots only the BSP. These become per-CPU state when
 * application processors are brought up; a local timer belongs to its CPU. */
static uint64_t timer_frequency;
static uint8_t timer_vector;
static bool vector_reserved;
static bool timer_running;
static isr_t timer_callback;

static uint64_t timer_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void timer_irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
}

static void timer_isr(void) {
    /* Keep the polling clock extended even when no foreground code reads it. */
    (void)hpet_read_counter();
    if (timer_running) timer_callback();
    lapic_eoi();
}

static bool calibration_sample(uint64_t reference_hz, uint64_t* result) {
    uint64_t target = reference_hz / 100; /* 10 ms, measured in HPET ticks. */
    if (!target) return false;

    lapic_write(TIMER_INITIAL, 0xFFFFFFFFU);
    uint64_t start = hpet_read_counter();
    uint32_t initial = lapic_read(TIMER_CURRENT);
    uint64_t elapsed = 0;
    uint32_t current = initial;
    for (uint32_t poll = 0; poll < CALIBRATION_POLLS; poll++) {
        elapsed = hpet_read_counter() - start;
        current = lapic_read(TIMER_CURRENT);
        if (!current || elapsed >= target) break;
        __asm__ volatile("pause");
    }
    lapic_write(TIMER_INITIAL, 0);
    if (elapsed < target || !current || current >= initial) return false;

    uint64_t counts = initial - current;
    /* Use the actual HPET elapsed time, including polling/virtualization delay,
     * rather than assuming the loop finished at exactly 10 ms. */
    if (counts > (~0ULL - elapsed / 2) / reference_hz) return false;
    *result = (counts * reference_hz + elapsed / 2) / elapsed;
    return *result != 0;
}

bool lapic_timer_init(void) {
    if (!lapic_is_ready() || !hpet_frequency()) return false;
    uint64_t flags = timer_irq_save();
    if (timer_frequency) {
        timer_irq_restore(flags);
        return true;
    }

    /* One-shot mode while calibrating, with a valid masked vector. These
     * accessors use MMIO in xAPIC mode and MSRs in x2APIC mode. */
    lapic_write(LVT_TIMER, TIMER_MASKED | CALIBRATION_VECTOR);
    lapic_write(TIMER_INITIAL, 0);
    lapic_write(TIMER_DIVIDE, DIVIDE_BY_16);
    uint64_t samples[3];
    for (uint32_t i = 0; i < 3; i++) {
        if (!calibration_sample(hpet_frequency(), &samples[i])) {
            timer_irq_restore(flags);
            return false;
        }
    }
    /* Median rejects one unusually delayed or noisy measurement. */
    for (uint32_t i = 0; i < 2; i++) {
        for (uint32_t j = i + 1; j < 3; j++) {
            if (samples[j] < samples[i]) {
                uint64_t swap = samples[i];
                samples[i] = samples[j];
                samples[j] = swap;
            }
        }
    }
    timer_frequency = samples[1];
    con_write("LAPIC timer frequency (divide 16): ", 0x03);
    print_uint64_dec(timer_frequency);
    con_write(" Hz\n", 0x03);
    timer_irq_restore(flags);
    return true;
}

bool lapic_timer_start(uint8_t vector, uint32_t hz, isr_t callback) {
    if (vector < 0x20 || vector >= 0xFE || !hz || !callback) return false;
    uint64_t flags = timer_irq_save();
    if ((vector_reserved && vector != timer_vector) ||
        (!vector_reserved && interrupts_get_isr(vector)) || !lapic_timer_init()) {
        timer_irq_restore(flags);
        return false;
    }

    uint64_t count = (timer_frequency + hz / 2) / hz;
    if (hz > timer_frequency || !count || count > 0xFFFFFFFFULL) {
        timer_irq_restore(flags);
        return false;
    }
    /* Stop before changing the callback or reload value. Install the handler
     * before unmasking; writing the initial count last starts the timer. */
    lapic_write(LVT_TIMER, TIMER_MASKED | vector);
    lapic_write(TIMER_INITIAL, 0);
    timer_vector = vector;
    vector_reserved = true;
    timer_callback = callback;
    interrupts_set_isr(vector, timer_isr);
    lapic_write(TIMER_DIVIDE, DIVIDE_BY_16);
    timer_running = true;
    lapic_write(LVT_TIMER, TIMER_PERIODIC | vector);
    lapic_write(TIMER_INITIAL, (uint32_t)count);
    timer_irq_restore(flags);
    return true;
}

void lapic_timer_stop(void) {
    if (!lapic_is_ready()) return;
    uint64_t flags = timer_irq_save();
    timer_running = false;
    uint8_t vector = vector_reserved ? timer_vector : CALIBRATION_VECTOR;
    lapic_write(LVT_TIMER, TIMER_MASKED | vector);
    lapic_write(TIMER_INITIAL, 0);
    timer_irq_restore(flags);
}

uint64_t lapic_timer_frequency(void) {
    return timer_frequency;
}
