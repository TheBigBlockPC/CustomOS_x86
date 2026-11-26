#ifndef USER_MEMORY_H
#define USER_MEMORY_H
#include "process.h"

void um_create_segment(process_t* process,uint64_t user_base, void* data, uint64_t size, uint64_t flags, uint64_t padding);
void um_init(process_t* process);
uint64_t um_alloc(process_t* process,uint64_t size);
uint64_t um_mmap(process_t* process,uint64_t base, uint64_t size, uint64_t mmu_flags);
void um_free(process_t* process,uint64_t addr);
void um_heap_dealloc(process_t* process);
void dealloc_segments(process_t* process);
#endif