#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define PAGE_SIZE 4096ULL
#define HHDM_BASE 0xffff800000000000ULL
/* Leave the top 2 GiB of the upper canonical half for the kernel image. */
#define HHDM_SIZE 0x0000400000000000ULL

void mem_add_free_pages(uint64_t start, uint64_t page_count);
uint64_t mem_get_memory_size(void);
uint64_t mem_get_free_page_count(void);
void init_memory_subsystem(void);
/* Returns a zeroed physical page; panics on exhaustion. Single CPU only. */
uint64_t mem_alloc_page(void);
void mem_free_page(uint64_t address);
void mem_set_kernel_image(uint64_t physical, uint64_t size);
/* Physical address of a pointer into the contiguous, fixed-address kernel. */
uint64_t mem_kernel_to_phys(const void* pointer);
/* CPU pointer for managed RAM or the kernel image, not MMIO.
 * Uses identity addresses until setup_hhdm/set_mmu. */
void* mem_phys_to_virt(uint64_t address);
void setup_hhdm(void);

#endif
