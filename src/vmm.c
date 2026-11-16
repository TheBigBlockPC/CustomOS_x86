#include "memory.h"
#include "console.h"
#include "paging.h"
#include "util.h"
#include "panic.h"
#include "vmm.h"

static uint64_t total_virtual_pages = 0;
static uint64_t free_virtual_pages = 0;
static uint64_t bitmap_pages = 0;

typedef enum {
    VM_ALLOC_FREE,
    VM_ALLOC_USED,
    VM_ALLOC_EXCLUSIVE
} alloc_state;

typedef struct vm_region {
    alloc_state state;
    uint64_t base;
    uint64_t size;
    struct vm_region* next;
    struct vm_region* prev;
} vm_region_t;

typedef struct vm_region_page_header {
    struct vm_region_page* next;
    uint64_t used_count;
} vm_region_page_header_t;

#define VM_REGIONS_PER_PAGE \
    ((PAGE_SIZE - sizeof(vm_region_page_header_t)) \
    / sizeof(vm_region_t))

typedef struct vm_region_page {
    vm_region_page_header_t header;
    vm_region_t regions[VM_REGIONS_PER_PAGE];
} vm_region_page_t;

vm_region_page_t* genisis_page = (vm_region_page_t*)VM_BASE;
/*
static void bitmap_set(uint64_t page) {
    uint8_t* bitmap = (uint8_t*)VM_BASE;

    uint64_t index = page / 8;
    uint8_t shift = (page % 8);

    bitmap[index] |= 1 << shift;
}

static void bitmap_clear(uint64_t page) {
    uint8_t* bitmap = (uint8_t*)VM_BASE;

    uint64_t index = page / 8;
    uint8_t shift = (page % 8);

    bitmap[index] &= (uint8_t)~(1 << shift);
}

static uint8_t bitmap_get(uint64_t page) {
    uint8_t* bitmap = (uint8_t*)VM_BASE;

    return (bitmap[page / 8] >> (page % 8)) & 1;
}
*/

void vmm_init(){
    con_write("VMM: initializing VMM\n",0x07);
    //total_virtual_pages = mem_get_free_page_count();
    uint64_t pmm_addr = mem_alloc_page();
    mmu_map_4k(VM_BASE,pmm_addr,PAGE_RW);
    genisis_page->header.used_count = 1;
    genisis_page->header.next = (vm_region_page_t*)0;
    genisis_page->regions[0].base = VM_BASE;
    genisis_page->regions[0].size = PAGE_SIZE_4K;
    genisis_page->regions[0].next = (vm_region_t*)0;
    genisis_page->regions[0].prev = (vm_region_t*)0;
    genisis_page->regions[0].state = VM_ALLOC_EXCLUSIVE;
    con_write("VMM: ready\n",0x02);
}

void* vm_alloc(uint64_t size, uint64_t flags){
    con_write("allocating memory, flags=",0x02);
    print_uint64(flags);
    con_write("\n",0x02);
    if (!size) return 0;
    if (flags & ~((uint64_t)VM_PAGE_ALIGNED)) panic("VMM: invalid allocation flags");
    uint64_t used_count = genisis_page->header.used_count;
    if (used_count >= VM_REGIONS_PER_PAGE) panic("VMM: region metadata full");
    vm_region_t* last_block = &(genisis_page->regions[0]);
    while(last_block->next != 0){
        last_block = last_block->next;
    }
    /* Region bases are absolute virtual addresses, including VM_BASE. */
    if (last_block->size > ~last_block->base) panic("VMM: address overflow");
    uint64_t base = last_block->base + last_block->size;
    if (base > ~0ULL - (PAGE_SIZE_4K - 1)) panic("VMM: address overflow");
    if(flags & VM_PAGE_ALIGNED){
        con_write("page aligned\n",0x03);
        base = (base + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    }else{
        con_write("not page aligned\n",0x01);
    }
    if (size > ~base - (PAGE_SIZE_4K - 1)) panic("VMM: allocation size overflow");
    uint64_t pagealigned_base = base & ~(PAGE_SIZE_4K-1);
    uint64_t pagealigned_end = (base + size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K-1);
    uint64_t n_pages = (pagealigned_end-pagealigned_base) / PAGE_SIZE_4K;
    for(uint64_t i = 0;i<n_pages;i++){
        uint64_t virt = i * PAGE_SIZE_4K + pagealigned_base;
        uint64_t existing_phys;
        /* Packed allocations can share the previous allocation's last page. */
        if (virt_to_phys(virt, &existing_phys)) continue;
        uint64_t page_phys = mem_alloc_page();
        mmu_map_4k(virt,page_phys,PAGE_RW | PAGE_NX);
    }

    genisis_page->regions[used_count].base = base;
    genisis_page->regions[used_count].size = size;
    genisis_page->regions[used_count].prev = last_block;
    genisis_page->regions[used_count].next = 0;
    genisis_page->regions[used_count].state = VM_ALLOC_USED;
    last_block->next = &(genisis_page->regions[used_count]);
    genisis_page->header.used_count++;
    con_write("done\n",0x05);
    return (void*)base;
}
/*
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
}*/

void vm_free(void* addr){
    /*if((uint64_t)addr < VM_BASE || (uint64_t)addr > VM_BASE + total_virtual_pages * PAGE_SIZE_4K){
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
    } while(!end_free(base));*/
}
