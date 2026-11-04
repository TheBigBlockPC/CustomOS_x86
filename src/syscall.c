#include "types.h"
#include "console.h"

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr"
        :
        : "c"(msr),
          "a"((uint32_t)value),
          "d"((uint32_t)(value >> 32)));
}

extern void syscall_entry(void);

// C syscall handler
void syscall_test_handler(void) {
    con_write("syscall",0x02);
    // Your test code here
}

__asm__(
    ".global syscall_entry\n"
    "syscall_entry:\n"
    "    push %rcx\n"
    "    push %r11\n"
    "    push %rbp\n"
    "    mov %rsp, %rbp\n"
    "    and $-16, %rsp\n"
    "    call syscall_test_handler\n"
    "    mov %rbp, %rsp\n"
    "    pop %rbp\n"
    "    pop %r11\n"
    "    pop %rcx\n"
    "    jmp *%rcx\n"
);

void syscall_init(void) {
    // Kernel CS = 0x08, SS = 0x10
    wrmsr(0xC0000081, (uint64_t)0x08 << 32);

    // Kernel syscall entry address
    wrmsr(0xC0000082, (uint64_t)syscall_entry);

    // No flag masking for initial test
    wrmsr(0xC0000084, 0);

    // Enable SYSCALL
    uint64_t efer = rdmsr(0xC0000080);
    wrmsr(0xC0000080, efer | 1ULL);
}