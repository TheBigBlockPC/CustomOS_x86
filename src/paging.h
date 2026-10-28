#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGE_SIZE_4K 0x1000ULL
#define PAGE_SIZE_2M 0x200000ULL
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_RW      (1ULL << 1)
#define PAGE_USER    (1ULL << 2)
#define PAGE_PWT     (1ULL << 3)
#define PAGE_PCD     (1ULL << 4)
#define PAGE_ACCESSED (1ULL << 5)
#define PAGE_DIRTY   (1ULL << 6)
#define PAGE_PS      (1ULL << 7)
#define PAGE_NX      (1ULL << 63)
#define PAGE_HUGE PAGE_PS
/* PRESENT/PS are supplied internally; flags=0 means supervisor, read-only,
 * executable. Changes invalidate translations on this CPU only. No SMP yet.
 * 4K mapping splits existing 2M leaves; 2M mapping never discards a 4K table. */
void mmu_map_4k(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);
void mmu_map_2m(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);
void identitymap_4k(uint64_t physical_addr, uint64_t flags);
void identitymap_2m(uint64_t physical_addr, uint64_t flags);
/* Arbitrary byte range, rounded out to 4K pages; length=0 is a no-op. */
void mmu_identity_map_range(uint64_t address, uint64_t length, uint64_t flags);
void set_mmu(void);
int get_mmu_active(void);
bool virt_to_phys(uint64_t va, uint64_t *pa);
#endif
