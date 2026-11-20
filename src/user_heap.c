#include "types.h"
#include "util.h"
#include "paging.h"
#include "console.h"

#define UM_HEAP_NULL      0b00000000
#define UM_HEAP_FREE_PAGE 0b00000001

typedef struct
{
    uint64_t user_va;
    uint64_t size;
    uint8_t flags;
} heap_region_t;

#define HEAP_ALLOC_LIMIT 256

static heap_region_t heap_allocations[HEAP_ALLOC_LIMIT];

static uint64_t heap_base;

void init(){
    for(int i=0;i<HEAP_ALLOC_LIMIT;i++){
        heap_allocations[i].user_va = UINT64_MAX;
        heap_allocations[i].size = 0;
        heap_allocations[i].flags = UM_HEAP_NULL;
    }
}

void set_heap_base(uint64_t base){
    heap_base = base;
}

uint64_t align(uint64_t val){
    return int_div_round_up(val,PAGE_SIZE_4K) * PAGE_SIZE_4K;
}

uint64_t allocate_adress(uint64_t size){
    size = align(size);
    for(int i=0;i<HEAP_ALLOC_LIMIT;i++){
        uint64_t previous_end = heap_base;
        if(i!=0){
            previous_end = heap_allocations[i-1].user_va + heap_allocations[i-1].size;
        }
        uint64_t start = heap_allocations[i].user_va;
        uint64_t gap = start - previous_end;
        if(gap >= size){
            return start;
        }
    }
    con_write("out of allocations\n",0x01);
    return 0;
}