#ifndef USER_HEAP_H
#define USER_HEAP_H
void um_heap_init();
void um_free(uint64_t addr);
uint64_t um_mmap(uint64_t base, uint64_t size, uint64_t mmu_flags);
uint64_t um_alloc(uint64_t size);
void set_heap_base(uint64_t base);
void um_heap_dealloc();
#endif