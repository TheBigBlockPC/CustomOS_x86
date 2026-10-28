#include "memory.h"
#include "console.h"
#include "paging.h"
#include "util.h"
#include "panic.h"
#include "vmm.h"

static uint64_t total_virtual_pages = 0;
static uint64_t free_virtual_pages = 0;
static uint64_t bitmap_pages = 0;

static void bitmap_set(uint64_t page) {
    ((char*)VM_BASE)[page / 8] |= 1u << (page % 8);
}
static void bitmap_clear(uint64_t page) {
    ((char*)VM_BASE)[page / 8] &= ~(1u << (page % 8));
}
static bool bitmap_get(uint64_t page) {
    return (((char*)VM_BASE)[page / 8] & (1u << (page % 8))) != 0;
}
void vmm_init(){
    con_write("VMM: initializing VMM\n",0x07);
    total_virtual_pages = mem_get_free_page_count();
    uint64_t bitmap_size = int_div_round_up(total_virtual_pages, 8);
    bitmap_pages = int_div_round_up(bitmap_size, PAGE_SIZE_4K);
    free_virtual_pages = total_virtual_pages - bitmap_pages;
    
    con_write("VMM: allocating bitmap\n",0x07);
    for(int i=0;i<bitmap_pages;i++){
        uint64_t target_addr = VM_BASE + PAGE_SIZE_4K * i;
        uint64_t pmm_addr = mem_alloc_page();
        mmu_map_4k(target_addr,pmm_addr,PAGE_RW);
        bitmap_set(i);
        uint64_t physical_addr = 0;
    }
    con_write("VMM: ready\n",0x02);
}


static uint64_t first_fit_alloc(uint64_t size){
    if(size == 0 || size > total_virtual_pages)
        return 0;
    uint64_t scan_region = total_virtual_pages - size;
    for(uint64_t base = 0; base <= scan_region; base++){
        bool valid = true;
        for(uint64_t offset = 0; offset < size; offset++){
            if(bitmap_get(base + offset)){
                valid = false;
                break;
            }
        }
        if(valid)
            return base;
    }
    return 0;
}

void* vm_alloc(uint64_t size){
    uint64_t n_pages = int_div_round_up(size, PAGE_SIZE_4K);
    bool running = true;
    uint64_t base = first_fit_alloc(n_pages);
    if(base == 0) return 0;
    for(uint64_t i = 0;i<n_pages;i++){
        uint64_t page_phys = mem_alloc_page();
        uint64_t virt = (base+i) * PAGE_SIZE_4K + VM_BASE;
        mmu_map_4k(virt,page_phys,PAGE_RW);
        bitmap_set(base+i);
    }
    return (void*)(base * PAGE_SIZE_4K + VM_BASE);
}