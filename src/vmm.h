#ifndef VMM_H
#define VMM_H

#include "types.h"

#define VM_BASE 0xFFFFC00000000000ULL

void vmm_init();
void* vm_alloc(uint64_t size);
#endif