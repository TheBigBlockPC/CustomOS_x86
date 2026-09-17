#ifndef MEMORY_H
#define MEMORY_H

void mem_add_free_pages(uint64_t start, uint64_t page_count);
uint64_t mem_get_memory_size();

#endif