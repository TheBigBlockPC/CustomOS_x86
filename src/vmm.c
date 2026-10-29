#include "memory.h"
#include "console.h"
#include "paging.h"
#include "util.h"
#include "panic.h"
#include "vmm.h"

static uint64_t total_virtual_pages = 0;
static uint64_t free_virtual_pages = 0;
static uint64_t bitmap_pages = 0;

static void bitmap_set(uint64_t page, bool alloc_start) {
    uint8_t* bitmap = (uint8_t*)VM_BASE;

    uint64_t index = page / 4;
    uint8_t shift = (page % 4) * 2;

    bitmap[index] &= (uint8_t)~(0b11u << shift);
    bitmap[index] |= (uint8_t)((alloc_start ? 0b11u : 0b01u) << shift);
}

static void bitmap_clear(uint64_t page) {
    uint8_t* bitmap = (uint8_t*)VM_BASE;

    uint64_t index = page / 4;
    uint8_t shift = (page % 4) * 2;

    bitmap[index] &= (uint8_t)~(0b11u << shift);
}

static uint8_t bitmap_get(uint64_t page) {
    uint8_t* bitmap = (uint8_t*)VM_BASE;

    return (bitmap[page / 4] >> ((page % 4) * 2)) & 0b11;
}

void vmm_init(){
    con_write("VMM: initializing VMM\n",0x07);
    total_virtual_pages = mem_get_free_page_count();
    uint64_t bitmap_size = int_div_round_up(total_virtual_pages, 4);
    bitmap_pages = int_div_round_up(bitmap_size, PAGE_SIZE_4K);
    free_virtual_pages = total_virtual_pages - bitmap_pages;
    
    con_write("VMM: allocating bitmap\n",0x07);
    for(int i=0;i<bitmap_pages;i++){
        uint64_t target_addr = VM_BASE + PAGE_SIZE_4K * i;
        uint64_t pmm_addr = mem_alloc_page();
        mmu_map_4k(target_addr,pmm_addr,PAGE_RW);
        bitmap_set(i,i==0);
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
            if(bitmap_get(base + offset) & 0b01){
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
        mmu_map_4k(virt,page_phys,PAGE_RW | PAGE_NX);
        bitmap_set(base+i,i==0);
    }
    return (void*)(base * PAGE_SIZE_4K + VM_BASE);
}

#define ALLOC_START 0b10
#define ALLOCATED 0b01

bool end_free(uint64_t page){
    if(page >= total_virtual_pages){
        return true;
    }
    uint8_t page_meta = bitmap_get(page);
    if(!(page_meta & ALLOCATED) | (page_meta & ALLOC_START)){
        return true;
    }
    return false;
}

void vm_free(void* addr){
    if(addr < VM_BASE || addr > VM_BASE + total_virtual_pages * PAGE_SIZE_4K){
        panic("invallid adress");
    }
    uint64_t base = ((uint64_t)addr - VM_BASE) / PAGE_SIZE_4K;
    uint8_t page_metadata = bitmap_get(base);
    if(!(page_metadata & ALLOCATED)){
        panic("double free");
    }
    if(!(page_metadata & ALLOC_START)){
        panic("invallid free");
    }
    do{
        bitmap_clear(base);
        uint64_t virt = base * PAGE_SIZE_4K + VM_BASE;
        uint64_t page_phys = 0;
        virt_to_phys(virt,&page_phys);
        if(page_phys == 0){
            panic("invallid mapping");
        }
        mem_free_page(page_phys);
        mmu_unmap_4k(virt);
        base++;
    } while(!end_free(base));
}