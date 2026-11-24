#include "types.h"
#include "util.h"
#include "paging.h"
#include "console.h"
#include "memory.h"
#include "user_heap.h"

#define UM_HEAP_NULL      0b00000000
#define UM_HEAP_FREE_PAGE 0b00000001
#define UM_HEAP_MMAP_PAGE 0b00000010

typedef struct
{
    uint64_t user_va;
    uint64_t size;
    uint8_t flags;
} heap_region_t;

#define HEAP_ALLOC_LIMIT 256

static heap_region_t heap_allocations[HEAP_ALLOC_LIMIT];

static uint64_t heap_base;
#define MMAP_BASE 0x0000100000000000ULL

void um_heap_init(){
    for(int i=0;i<HEAP_ALLOC_LIMIT;i++){
        heap_allocations[i].user_va = UINT64_MAX;
        heap_allocations[i].size = 0;
        heap_allocations[i].flags = UM_HEAP_NULL;
    }
}

uint64_t align(uint64_t val){
    return int_div_round_up(val,PAGE_SIZE_4K) * PAGE_SIZE_4K;
}


void set_heap_base(uint64_t base){
    heap_base = align(base);
}

uint64_t allocate_adress(uint64_t size,bool mmap){
    size = align(size);
    for(int i=0;i<HEAP_ALLOC_LIMIT;i++){
        uint64_t previous_end = heap_base;
        if(i!=0){
            previous_end = heap_allocations[i-1].user_va + heap_allocations[i-1].size;
        }
        if(mmap && (previous_end< MMAP_BASE)){
            previous_end = MMAP_BASE;
        }
        uint64_t start = heap_allocations[i].user_va;
        uint64_t gap = start - previous_end;
        if(gap >= size){
            return previous_end;
        }
    }
    con_write("out of allocations\n",0x01);
    return 0;
}

void insert(uint64_t addr, uint64_t size, uint8_t flags){
    for(int i=0;i<HEAP_ALLOC_LIMIT;i++){
        uint64_t previous_end = heap_base;
        if(i!=0){
            previous_end = heap_allocations[i-1].user_va + heap_allocations[i-1].size;
        }
        if(addr >= previous_end){
            for(int i2=HEAP_ALLOC_LIMIT-1;i2 > i;i2--){
                heap_allocations[i2] = heap_allocations[i2-1];
            }
            heap_allocations[i].user_va = addr;
            heap_allocations[i].size = size;
            heap_allocations[i].flags = flags;
            return;
        }
    }
}

uint64_t get_index(uint64_t addr){
    for(int i=0;i<HEAP_ALLOC_LIMIT;i++){
        if(heap_allocations[i].user_va == addr){
            return i;
        }
    }
    return -1;
}

void remove(uint64_t index){
    for(int i2=index;i2 < HEAP_ALLOC_LIMIT-1;i2++){
        heap_allocations[i2] = heap_allocations[i2+1];
    }
    heap_allocations[HEAP_ALLOC_LIMIT-1].user_va = UINT64_MAX;
}

uint64_t um_alloc(uint64_t size){
    uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    uint64_t addr = allocate_adress(size,false);
    insert(addr,size,UM_HEAP_NULL);
    for(int i=0;i<n_pages;i++){
        uint64_t phys = mem_alloc_page();
        uint64_t user_va = addr + i * PAGE_SIZE_4K;
        mmu_map_4k(user_va,phys,PAGE_NX | PAGE_RW | PAGE_USER);
    }
    return addr;
}

uint64_t um_mmap(uint64_t base, uint64_t size, uint64_t mmu_flags){
    uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    uint64_t addr = allocate_adress(size,true);
    insert(addr,size,UM_HEAP_MMAP_PAGE);
    for(int i=0;i<n_pages;i++){
        uint64_t offset = i * PAGE_SIZE_4K;
        uint64_t phys;
        virt_to_phys(base+offset,&phys);
        uint64_t user_va = addr+offset;
        mmu_map_4k(user_va,phys,mmu_flags);
    }
    return addr;
}

void um_free(uint64_t addr){
    uint64_t index = get_index(addr);
    uint64_t size = heap_allocations[index].size;
    uint8_t flags = heap_allocations[index].flags;
    remove(index);
    
    uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    
    for(int i=0;i<n_pages;i++){
        uint64_t phys;
        uint64_t user_va = addr + i * PAGE_SIZE_4K;
        mmu_unmap_4k(user_va);
        if(!(flags & UM_HEAP_MMAP_PAGE)){
            virt_to_phys(user_va,&phys);
            mem_free_page(phys);
        }
    }
}

void um_heap_dealloc(){
    for(int i=0;i<HEAP_ALLOC_LIMIT;i++){
        uint64_t size = heap_allocations[i].size;
        uint64_t addr = heap_allocations[i].user_va;
        uint8_t flags = heap_allocations[i].flags;
        if(addr == UINT64_MAX) um_heap_init();
        uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    
        for(int i=0;i<n_pages;i++){
            uint64_t phys;
            uint64_t user_va = addr + i * PAGE_SIZE_4K;
            mmu_unmap_4k(user_va);
            if(!(flags & UM_HEAP_MMAP_PAGE)){
                virt_to_phys(user_va,&phys);
                mem_free_page(phys);
            }
        }
    }
    um_heap_init();
}