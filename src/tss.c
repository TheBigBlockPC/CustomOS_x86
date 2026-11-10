#include "tss.h"
#include "gdt/gdt.h"
static tss64_t tss = {0};

void tss_init(uint64_t kernel_stack_top) {
    tss.rsp0 = kernel_stack_top;
    tss.iomap_base = sizeof(tss64_t);
}

tss64_t* tss_get_pointer(){
    return &tss;
}

void tss_load(void) {
    uint16_t selector = GDT_TSS_SELECTOR;

    __asm__ volatile (
        "ltr %0"
        :
        : "rm"(selector)
        : "memory"
    );
}