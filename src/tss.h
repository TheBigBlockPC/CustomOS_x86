#ifndef TSS_H
#define TSS_H
#include "types.h"

typedef struct __attribute__((packed)) {
    uint32_t reserved0;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t reserved1;

    uint64_t ist[7];

    uint64_t reserved2;
    uint16_t reserved3;

    uint16_t iomap_base;
} tss64_t;

void tss_init(uint64_t kernel_stack_top);
tss64_t* tss_get_pointer();
void tss_load(void);
#endif