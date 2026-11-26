#include "types.h"
#include "process.h"
#include "paging.h"
#include "user_memory.h"
#include "util.h"
#include "memory.h"
#include "console.h"
#include "vmm.h"

#define UM_HEAP_NULL      0b00000000
#define UM_HEAP_FREE_PAGE 0b00000001
#define UM_HEAP_MMAP_PAGE 0b00000010
#define HEAP_ALLOC_LIMIT 256
#define MMAP_BASE 0x0000100000000000ULL

typedef struct {
    uint64_t user_va;
    void* kernel_va;
    uint64_t size;
} um_alloc_t;

typedef struct
{
    uint64_t user_va;
    uint64_t size;
    uint8_t flags;
} heap_region_t;

void unmap_range(uint64_t base, uint64_t n_bytes){
    uint64_t n_pages = int_div_round_up(n_bytes,PAGE_SIZE_4K);
    for(uint64_t i=0;i<n_pages;i++){
        mmu_unmap_4k(base+i*PAGE_SIZE_4K);
    }
}

// segment alloc
void um_create_segment(process_t* process,uint64_t user_base, void* data, uint64_t size, uint64_t flags, uint64_t padding){
    void* memory = proc_allocate_kernel(size+padding,user_base,flags);
    memory_copy(memory+padding,data,size);
    um_alloc_t segment;
    segment.kernel_va = memory;
    segment.user_va = user_base;
    segment.size = size;
    list_push_element(&process->segment_allocations,&segment);
}

void dealloc_segments(process_t* process){
    um_alloc_t* heap_allocations = (um_alloc_t*)process->segment_allocations.data;
    for(int i=0;i<process->segment_allocations.count;i++){
        um_alloc_t* segment = &heap_allocations[i];
        vm_free(segment->kernel_va);
    }
}
void um_init(process_t* process){
    process->segment_allocations = create_dynamic_list(sizeof(um_alloc_t));
    process->heap_allocations = create_dynamic_list(sizeof(heap_region_t));
}

// heap
uint64_t align(uint64_t val){
    return int_div_round_up(val,PAGE_SIZE_4K) * PAGE_SIZE_4K;
}

uint64_t allocate_adress(process_t* process,uint64_t size,bool mmap){
    size = align(size);
    heap_region_t* heap_allocations = (heap_region_t*)process->heap_allocations.data;
    for(int i=0;i<process->heap_allocations.count;i++){
        uint64_t previous_end = process->heap_base;
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
    uint64_t end = process->heap_allocations.count - 1;
    if(end == UINT64_MAX){
        if(mmap){
            return MMAP_BASE;
        }else{
            return process->heap_base;
        }
    }
    return heap_allocations[end].user_va + heap_allocations[end].size;
}

void insert(process_t* process,uint64_t addr, uint64_t size, uint8_t flags){
    heap_region_t* heap_allocations = (heap_region_t*)process->heap_allocations.data;
    
    for(int i=0;i<process->heap_allocations.count;i++){
        uint64_t previous_end = process->heap_base;
        if(i!=0){
            previous_end = heap_allocations[i-1].user_va + heap_allocations[i-1].size;
        }
        if(addr >= previous_end){
            heap_region_t region;
            region.user_va = addr;
            region.size = size;
            region.flags = flags;
            list_insert_element(&process->heap_allocations,i,&region);
            return;
        }
    }
}

uint64_t get_index(process_t* process,uint64_t addr){
    heap_region_t* heap_allocations = (heap_region_t*)process->heap_allocations.data;
    
    for(int i=0;i<process->heap_allocations.count;i++){
        if(heap_allocations[i].user_va == addr){
            return i;
        }
    }
    return -1;
}

uint64_t um_alloc(process_t* process,uint64_t size){
    uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    uint64_t addr = allocate_adress(process,size,false);
    insert(process,addr,size,UM_HEAP_NULL);
    for(int i=0;i<n_pages;i++){
        uint64_t phys = mem_alloc_page();
        uint64_t user_va = addr + i * PAGE_SIZE_4K;
        mmu_map_4k(user_va,phys,PAGE_NX | PAGE_RW | PAGE_USER);
    }
    return addr;
}

uint64_t um_mmap(process_t* process,uint64_t base, uint64_t size, uint64_t mmu_flags){
    uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    uint64_t addr = allocate_adress(process,size,true);
    insert(process,addr,size,UM_HEAP_MMAP_PAGE);
    for(int i=0;i<n_pages;i++){
        uint64_t offset = i * PAGE_SIZE_4K;
        uint64_t phys;
        virt_to_phys(base+offset,&phys);
        uint64_t user_va = addr+offset;
        mmu_map_4k(user_va,phys,mmu_flags);
    }
    return addr;
}

void um_free(process_t* process,uint64_t addr){
    heap_region_t* heap_allocations = (heap_region_t*)process->heap_allocations.data;
    
    uint64_t index = get_index(process,addr);
    uint64_t size = heap_allocations[index].size;
    uint8_t flags = heap_allocations[index].flags;
    list_remove_element(&process->heap_allocations,index);

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

void um_heap_dealloc(process_t* process){
    heap_region_t* heap_allocations = (heap_region_t*)process->heap_allocations.data;
    
    for(int i=0;i<process->heap_allocations.count;i++){
        uint64_t size = heap_allocations[i].size;
        uint64_t addr = heap_allocations[i].user_va;
        uint8_t flags = heap_allocations[i].flags;
        if(addr == UINT64_MAX) return;
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
}