#include "types.h"
#include "console.h"
#include "util.h"
#include "console_renderer.h"
#include "usermode.h"
#include "paging.h"

#define SYS_WRITE 1
#define SYS_ADD   2
#define SYS_EXIT  3
#define SYS_GFX   4

#define SYSCALL_STACK_SIZE (16 * 1024)

#define USER_CODE_BASE 0x400000ULL
//#define USER_CODE_SIZE 0x80000000ULL
#define USER_LIMIT 0x0000800000000000ULL
#define USER_CODE_SIZE (USER_LIMIT-USER_CODE_BASE)

#define USER_FB_VA 0x0000000010000000ULL
// ============================================
// Kernel syscall stack
// ============================================

static _Alignas(16)
uint8_t syscall_stack[SYSCALL_STACK_SIZE];

// Global symbols accessed by syscall_entry.S
uint64_t syscall_kernel_rsp = 0;
uint64_t syscall_user_rsp = 0;

// ============================================
// MSR access
// ============================================

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;

    asm volatile(
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(msr)
    );

    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(
    uint32_t msr,
    uint64_t value
) {
    asm volatile(
        "wrmsr"
        :
        : "c"(msr),
          "a"((uint32_t)value),
          "d"((uint32_t)(value >> 32))
        : "memory"
    );
}

extern void syscall_entry(void);

// ============================================
// SYS_WRITE
// ============================================

// Temporary validation for the fixed user code
// page used by your current test.
// Not a general-purpose user-pointer validator.

static bool valid_user_buffer(
    uint64_t address,
    uint64_t length
) {
    if (length > 256)
        return false;

    if (address < USER_CODE_BASE)
        return false;

    uint64_t offset = address - USER_CODE_BASE;

    return offset < USER_CODE_SIZE &&
           length <= USER_CODE_SIZE - offset;
}

static uint64_t sys_write(
    uint64_t address,
    uint64_t length
) {
    if (!valid_user_buffer(address, length))
        return (uint64_t)-1;

    const char* text = (const char*)address;

    for (uint64_t i = 0; i < length; i++) {
        char character[2] = {
            text[i],
            '\0'
        };

        con_write(character, 0x07);
    }

    return length;
}

// ============================================
// SYS_ADD
// ============================================

static uint64_t sys_add(
    uint64_t a,
    uint64_t b
) {
    return a + b;
}

// ============================================
// SYS_GFX
// ============================================

static uint64_t sys_gfx(
    uint64_t fb_desc_ptr
) {
    if (!valid_user_buffer(fb_desc_ptr, sizeof(fb_desc_ptr)))
        return (uint64_t)-1;
    fb_t* fb_desc = (fb_t*)fb_desc_ptr;

    fb_t fb = get_fb();
    uint16_t w = fb.w;
    uint16_t h = fb.h;
    fb_desc->w = w;
    fb_desc->h = h;
    fb_desc->fb = (uint32_t*)USER_FB_VA;
    um_mmap(USER_FB_VA,(void*)(fb.fb), w * h * 4, PAGE_RW | PAGE_NX | PAGE_USER);
    return 0;
}

// ============================================
// Syscall dispatcher
// ============================================

uint64_t syscall_dispatch(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1
) {
    switch (number) {
        case SYS_WRITE:
            return sys_write(arg0, arg1);

        case SYS_ADD:
            return sys_add(arg0, arg1);
        case SYS_EXIT:
            return (uint64_t)-1;
            //reserved
        case SYS_GFX:
            return sys_gfx(arg0);
        default:
            con_write("Invallid SYSCALL\n",0x01);
            return (uint64_t)-1;
    }
}

// ============================================
// Initialize syscall subsystem
// ============================================

void syscall_init(void) {

    // Initialize kernel syscall stack.
    syscall_kernel_rsp =
        (uint64_t)(
            syscall_stack + SYSCALL_STACK_SIZE
        );

    // IA32_STAR
    // Kernel CS = 0x08
    // Kernel SS = 0x10

    wrmsr(
        0xC0000081,
        (uint64_t)0x08 << 32
    );

    // IA32_LSTAR: 64-bit syscall entry point.
    wrmsr(
        0xC0000082,
        (uint64_t)syscall_entry
    );

    // IA32_FMASK
    // Clear IF, TF, DF and AC on kernel entry.
    wrmsr(
        0xC0000084,
        (1ULL << 9)  |
        (1ULL << 8)  |
        (1ULL << 10) |
        (1ULL << 18)
    );

    // Enable SYSCALL in IA32_EFER.
    uint64_t efer = rdmsr(0xC0000080);

    wrmsr(
        0xC0000080,
        efer | 1ULL
    );
}