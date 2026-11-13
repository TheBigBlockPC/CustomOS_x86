#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_WRITE 1
#define SYS_ADD   2
#define SYS_EXIT  3
#define SYS_GFX   4

static inline uint64_t syscall2(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1
) {
    uint64_t result;

    asm volatile(
        "syscall"
        : "=a"(result)
        : "a"(number),
          "D"(arg0),
          "S"(arg1)
        : "rcx", "r11", "memory"
    );

    return result;
}

static inline uint64_t sys_add(
    uint64_t a,
    uint64_t b
) {
    return syscall2(SYS_ADD, a, b);
}

static inline void sys_write(
    const char* text,
    uint64_t length
) {
    syscall2(
        SYS_WRITE,
        (uint64_t)text,
        length
    );
}

typedef struct {
    uint16_t w;
    uint16_t h;
    uint32_t* fb;
} fb_t;

static inline void sys_gfx(
    fb_t* fb_desc
) {
    syscall2(
        SYS_GFX,
        (uint64_t)fb_desc,
        0
    );
}
#endif