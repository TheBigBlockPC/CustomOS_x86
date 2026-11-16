/* Linked only into the temporary kernels built by run_usermode.py. */
#include "types.h"
#include "memory.h"
#include "paging.h"
#include "usermode.h"
#include "vmm.h"
#include "hpet.h"
#include "interrupts/lapic_timer.h"
#include "util.h"
#include "display_mgr.h"
#include "console.h"

extern uint8_t exit_probe_start[], exit_probe_end[];
extern uint64_t enter_user(uint64_t entry, uint64_t stack);
extern uint64_t kernel_saved_rsp;
static volatile uint64_t ticks;
static volatile uint64_t* shared;
static bool waiting;
static fb_t front;

static void debug(char* text) {
    while (*text) __asm__ volatile("outb %0, $0xe9" :: "a"((uint8_t)*text++));
}
static void finish(uint8_t code) {
    __asm__ volatile("outb %0, $0xf4" :: "a"(code));
    for (;;) __asm__ volatile("cli; hlt");
}
#define REQUIRE(condition) do { if (!(condition)) { \
    debug("FAIL: " #condition "\n"); finish(0x11); } } while (0)

void usermode_tick(void) {
    ticks++;
    if (waiting && kernel_saved_rsp) shared[1]++;
}
static uint64_t flags(void) {
    uint64_t value;
    __asm__ volatile("pushfq; popq %0" : "=r"(value) :: "memory");
    return value;
}
static void map_user(uint64_t address, uint64_t permissions) {
    mmu_map_4k(address, mem_alloc_page(), permissions | PAGE_USER);
}

/* Called by the runner's instrumented panic.c, just before its halt loop. */
void usermode_panic_check(void) {
    REQUIRE(USERMODE_CASE == 4);
    REQUIRE(!(flags() & 0x200));
    fb_t back = get_fb();
    REQUIRE(back.fb != front.fb);
    uint64_t pixels = (uint64_t)front.w * front.h;
    for (uint64_t i = 0; i < pixels; i++) REQUIRE(front.fb[i] == back.fb[i]);
    static console_state_t state;
    con_export_state(&state);
    const char* message = "Kernel panic";
    bool found = false;
    for (uint64_t i = 0; i < TERMINAL_W * TERMINAL_H; i++) {
        uint64_t j = 0;
        while (message[j] && i + j < TERMINAL_W * TERMINAL_H &&
               state.cells[i + j].character == message[j]) j++;
        if (!message[j]) found = true;
    }
    REQUIRE(found);
    debug("PANIC_FRAMEBUFFER_PRESENTED_OK\n");
    finish(0x10);
}

void usermode_selftest(void) {
    debug("USERMODE_START\n");
#if USERMODE_CASE == 4
    front = get_fb();
    setup_double_buffer();
    /* A real #PF enters panic with IF cleared; its output must reach the front
     * buffer without waiting for the timer callback. */
    *(volatile uint64_t*)0x700000 = 1;
    REQUIRE(false);
#elif USERMODE_CASE == 3
    /* Packed allocations can share a page, but must never replace its backing
     * or overlap the preceding allocation. Aligned callers need private pages. */
    uint8_t* first = vm_alloc(3 * 1024 * 1024 + 3, VM_NULL);
    first[0] = 0xa5;
    first[3 * 1024 * 1024 + 2] = 0x5a;
    uint8_t* second = vm_alloc(7, VM_NULL);
    REQUIRE((uint64_t)second >= (uint64_t)first + 3 * 1024 * 1024 + 3);
    second[0] = 0x12;
    second[6] = 0x34;
    uint8_t* third = vm_alloc(PAGE_SIZE, VM_PAGE_ALIGNED);
    REQUIRE(((uint64_t)third & (PAGE_SIZE - 1)) == 0);
    REQUIRE((uint64_t)third >= (uint64_t)second + 7);
    third[0] = 0x56;
    third[PAGE_SIZE - 1] = 0x78;
    uint8_t* fourth = vm_alloc(2 * PAGE_SIZE + 1, VM_NULL);
    REQUIRE((uint64_t)fourth >= (uint64_t)third + PAGE_SIZE);
    fourth[2 * PAGE_SIZE] = 0x9a;
    REQUIRE(first[0] == 0xa5 && first[3 * 1024 * 1024 + 2] == 0x5a);
    REQUIRE(second[0] == 0x12 && second[6] == 0x34);
    REQUIRE(third[0] == 0x56 && third[PAGE_SIZE - 1] == 0x78);
    debug("VMM_PACKED_AND_ALIGNED_OK\n");
#else
    /* Bypass VMM to isolate the privilege-transition failures. */
    map_user(0x400000, PAGE_RW);
    memory_copy((void*)0x400000, exit_probe_start, exit_probe_end - exit_probe_start);
    map_user(0x500000, PAGE_RW | PAGE_NX);
    map_user(0x600000, PAGE_RW | PAGE_NX);
    shared = (volatile uint64_t*)0x500000;
    volatile uint64_t canary[256];
    for (uint64_t i = 0; i < 256; i++) canary[i] = 0xabcd0000 + i;
#if USERMODE_CASE == 1
    lapic_timer_stop();
    REQUIRE(flags() & 0x200);
    REQUIRE(enter_user(0x400000, 0x601000) == 0x1234);
    REQUIRE(flags() & 0x200);
    __asm__ volatile("cli" ::: "memory");
    REQUIRE(enter_user(0x400000, 0x601000) == 0x1234);
    REQUIRE(!(flags() & 0x200));
    debug("EXIT_RESTORES_CALLER_IF_OK\n");
    __asm__ volatile("sti" ::: "memory");
    um_test();
    REQUIRE(flags() & 0x200);
    um_test();
    REQUIRE(flags() & 0x200);
    debug("USERMODE_EXEC_AND_CLEANUP_OK\n");
#else
    __asm__ volatile("cli" ::: "memory");
    waiting = true;
    shared[0] = 1;
    debug("ENTERING_USER_WITH_TIMER\n");
    REQUIRE(enter_user(0x400000, 0x601000) == 0x1234);
    waiting = false;
    REQUIRE(shared[1] > 0);
    REQUIRE(!(flags() & 0x200));
    debug("USER_TIMER_RETURN_OK\n");
    __asm__ volatile("sti" ::: "memory");
    uint64_t before = ticks;
    uint64_t start = hpet_millis();
    while (hpet_millis() - start < 100) __asm__ volatile("pause");
    REQUIRE(ticks > before);
#endif
    for (uint64_t i = 0; i < 256; i++) REQUIRE(canary[i] == 0xabcd0000 + i);
    debug("KERNEL_STACK_INTACT_OK\n");
#endif
    finish(0x10);
}
