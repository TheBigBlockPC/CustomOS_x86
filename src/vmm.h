#ifndef VMM_H
#define VMM_H

#include "types.h"

#define VM_BASE 0xFFFFC00000000000ULL

#define VM_NULL 1
#define VM_PAGE_ALIGNED 4096

void vmm_init();
void* vm_alloc(uint64_t size, uint64_t flags);
void vm_free(void* addr);
void print_alloc();
#endif