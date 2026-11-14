/* Fake HPET/LAPIC clocks exercise the driver without privileged instructions.
 * run_timer_host.py replaces only the IRQ save/restore hardware boundary. */
#include "lapic_timer.h"
#include "apic.h"

extern int printf(const char*, ...);
extern void exit(int) __attribute__((noreturn));
static int checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static bool irqs_enabled;
static uint64_t model_irq_save(void) {
    uint64_t flags = irqs_enabled ? 1ULL << 9 : 0;
    irqs_enabled = false;
    return flags;
}
static void model_irq_restore(uint64_t flags) { irqs_enabled = (flags & (1ULL << 9)) != 0; }
#include "lapic_timer_model.inc"

static uint32_t registers[256];
static isr_t handlers[256];
static uint64_t reference_hz, model_hpet, model_step, loaded_at, rate_hz;
static uint32_t samples, callbacks, eois, writes;
static bool ready, stalled_hpet, stalled_lapic, noisy_sample;

bool lapic_is_ready(void) { return ready; }
uint64_t hpet_frequency(void) { return reference_hz; }
uint64_t hpet_read_counter(void) {
    if (!stalled_hpet) model_hpet += model_step;
    return model_hpet;
}
uint32_t lapic_read(uint32_t reg) {
    if (reg != TIMER_CURRENT) return registers[reg / 4];
    uint32_t initial = registers[TIMER_INITIAL / 4];
    if (stalled_lapic) return initial;
    uint64_t rate = noisy_sample && samples == 2 ? rate_hz / 2 : rate_hz;
    uint64_t elapsed = (model_hpet - loaded_at) * rate / reference_hz;
    return elapsed >= initial ? 0 : initial - (uint32_t)elapsed;
}
void lapic_write(uint32_t reg, uint32_t value) {
    CHECK(!irqs_enabled);
    writes++;
    registers[reg / 4] = value;
    if (reg == TIMER_INITIAL) {
        loaded_at = model_hpet;
        if (value == 0xFFFFFFFFU) {
            samples++;
            CHECK(registers[LVT_TIMER / 4] & TIMER_MASKED);
            CHECK(!(registers[LVT_TIMER / 4] & TIMER_PERIODIC));
        } else if (value) {
            CHECK(registers[LVT_TIMER / 4] == (TIMER_PERIODIC | timer_vector));
            CHECK(handlers[timer_vector] != 0);
        }
    }
}
void lapic_eoi(void) { eois++; }
void interrupts_set_isr(uint8_t index, isr_t handler) { handlers[index] = handler; }
isr_t interrupts_get_isr(uint8_t index) { return handlers[index]; }
void con_write(char* text, uint8_t color) { (void)text; (void)color; }
void print_uint64_dec(uint64_t value) { (void)value; }
static void callback(void) { callbacks++; }
static void foreign_isr(void) { }

static void reset(void) {
    for (uint32_t i = 0; i < 256; i++) { registers[i] = 0; handlers[i] = 0; }
    timer_frequency = 0;
    timer_vector = 0;
    vector_reserved = timer_running = false;
    timer_callback = 0;
    irqs_enabled = true;
    ready = true;
    stalled_hpet = stalled_lapic = noisy_sample = false;
    reference_hz = 10000000;
    rate_hz = 6250000; /* A 100 MHz LAPIC input divided by 16. */
    model_step = 1000;
    model_hpet = loaded_at = 0;
    samples = callbacks = eois = writes = 0;
}

int main(void) {
    reset();
    CHECK(!lapic_timer_start(31, 60, callback));
    CHECK(!lapic_timer_start(0xFE, 60, callback));
    CHECK(!lapic_timer_start(0xFF, 60, callback));
    CHECK(!lapic_timer_start(0x34, 0, callback));
    CHECK(!lapic_timer_start(0x34, 60, 0));
    CHECK(writes == 0 && irqs_enabled);
    handlers[0x34] = foreign_isr;
    CHECK(!lapic_timer_start(0x34, 60, callback));
    CHECK(handlers[0x34] == foreign_isr && writes == 0 && irqs_enabled);
    handlers[0x34] = 0;
    ready = false;
    CHECK(!lapic_timer_start(0x34, 60, callback));
    CHECK(writes == 0 && irqs_enabled);
    ready = true;
    reference_hz = 0;
    CHECK(!lapic_timer_start(0x34, 60, callback));
    CHECK(writes == 0 && irqs_enabled);

    reset();
    noisy_sample = true;
    CHECK(lapic_timer_start(0x34, 60, callback));
    CHECK(samples == 3 && lapic_timer_frequency() == rate_hz);
    CHECK(registers[TIMER_DIVIDE / 4] == DIVIDE_BY_16);
    CHECK(registers[TIMER_INITIAL / 4] == 104167);
    CHECK(irqs_enabled && callbacks == 0);
    uint64_t prior_hpet = model_hpet;
    handlers[0x34]();
    CHECK(callbacks == 1 && eois == 1 && model_hpet > prior_hpet);
    uint32_t prior_reload = registers[TIMER_INITIAL / 4];
    uint32_t prior_writes = writes;
    CHECK(!lapic_timer_start(0x35, 120, callback));
    CHECK(!lapic_timer_start(0x34, (uint32_t)rate_hz + 1, callback));
    CHECK(!lapic_timer_start(0x34, 0xFFFFFFFFU, callback));
    CHECK(registers[TIMER_INITIAL / 4] == prior_reload && writes == prior_writes);
    CHECK(irqs_enabled && timer_running);
    CHECK(lapic_timer_start(0x34, 120, callback));
    CHECK(samples == 3 && registers[TIMER_INITIAL / 4] == 52083);
    lapic_timer_stop();
    CHECK(irqs_enabled && !timer_running && registers[TIMER_INITIAL / 4] == 0);
    CHECK(registers[LVT_TIMER / 4] == (TIMER_MASKED | 0x34));
    handlers[0x34](); /* A pending delivery after stop must still get EOI. */
    CHECK(callbacks == 1 && eois == 2);
    CHECK(lapic_timer_start(0x34, 1000, callback));
    CHECK(samples == 3 && registers[TIMER_INITIAL / 4] == 6250);

    reset();
    irqs_enabled = false;
    CHECK(lapic_timer_start(0x70, 1, callback));
    CHECK(!irqs_enabled && registers[TIMER_INITIAL / 4] == rate_hz);
    lapic_timer_stop();
    CHECK(!irqs_enabled);

    reset();
    stalled_lapic = true;
    CHECK(!lapic_timer_start(0x34, 60, callback));
    CHECK(irqs_enabled && !timer_running && timer_frequency == 0);
    CHECK(registers[TIMER_INITIAL / 4] == 0 && (registers[LVT_TIMER / 4] & TIMER_MASKED));
    stalled_lapic = false;
    CHECK(lapic_timer_start(0x34, 60, callback)); /* Failed calibration is retryable. */

    reset();
    stalled_hpet = true;
    CHECK(!lapic_timer_start(0x34, 60, callback));
    CHECK(irqs_enabled && !timer_running && registers[TIMER_INITIAL / 4] == 0);
    CHECK(!handlers[0x34]);

    reset();
    model_step = 31000; /* Polling overshoots 10 ms: use actual elapsed time. */
    CHECK(lapic_timer_start(0x34, 60, callback));
    CHECK(lapic_timer_frequency() == rate_hz);

    reset();
    model_step = 10000000000ULL; /* Countdown expires before a sample completes. */
    CHECK(!lapic_timer_start(0x34, 60, callback));
    CHECK(registers[TIMER_INITIAL / 4] == 0 && irqs_enabled);
    printf("PASS: %d LAPIC timer calibration/configuration checks\n", checks);
    return 0;
}
