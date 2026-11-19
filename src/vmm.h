#ifndef VMM_H
#define VMM_H

#include "types.h"

#define VM_BASE 0xFFFFC00000000000ULL

#define VM_NULL 0
#define VM_PAGE_ALIGNED 4096

#define VM_FLAGS_DEFAULT 0
#define VM_FLAGS_FULL_PAGES 1

void vmm_init();
void* vm_alloc(uint64_t size, uint64_t alignment, uint8_t flags);
void vm_free(void* addr);
void print_alloc();
#endif