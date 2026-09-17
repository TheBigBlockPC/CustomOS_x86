#include "types.h"
#include "memory.h"
#include "console.h"

#define MAX_FREE_RANGES 128
#define PAGE_SIZE 4096

static FreeRange free_ranges[MAX_FREE_RANGES];
static size_t free_range_count = 0;
static uint64_t total_page_count = 0;

void mem_add_free_pages(uint64_t start, uint64_t page_count){
    if(free_range_count >= MAX_FREE_RANGES){
        con_write("out of pages",0x01);
        return;
    }
    total_page_count += page_count;
    free_ranges[free_range_count] = (FreeRange){
        .page_count = page_count,
        .start = start
    };
}

uint64_t mem_get_memory_size(){
    return total_page_count * PAGE_SIZE;
}