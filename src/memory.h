#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define PAGE_SIZE 4096ULL
#define HHDM_BASE 0xffff800000000000ULL
/* Direct map occupies the upper canonical half with four-level paging. */
#define HHDM_SIZE 0x0000800000000000ULL

void mem_add_free_pages(uint64_t start, uint64_t page_count);
uint64_t mem_get_memory_size(void);
uint64_t mem_get_free_page_count(void);
void init_memory_subsystem(void);
/* Returns a zeroed physical page; panics on exhaustion. Single CPU only. */
uint64_t mem_alloc_page(void);
void mem_free_page(uint64_t address);
/* CPU pointer for managed RAM, not MMIO. Valid after setup_hhdm/set_mmu. */
void* mem_phys_to_virt(uint64_t address);
void setup_hhdm(void);

#endif
