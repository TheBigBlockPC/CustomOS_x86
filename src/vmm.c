#include "types.h"
#include "memory.h"
#include "console.h"
#include "paging.h"
#include "util.h"
#include "panic.h"
#include "vmm.h"

#define VM_DEFAULT_ALIGNMENT 16ULL
#define VM_PAGE_MASK ((uint64_t)PAGE_SIZE_4K - 1)

typedef enum {
    VM_ALLOC_FREE,
    VM_ALLOC_USED,
    VM_ALLOC_EXCLUSIVE,
    VM_ALLOC_UNUSED,
    VM_ALLOC_PROCESSING
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
    ((PAGE_SIZE_4K - sizeof(vm_region_page_header_t)) \
    / sizeof(vm_region_t))
//#define VM_REGIONS_PER_PAGE 4
typedef struct vm_region_page {
    vm_region_page_header_t header;
    vm_region_t regions[VM_REGIONS_PER_PAGE];
} vm_region_page_t;

_Static_assert(
    sizeof(vm_region_page_t) <= PAGE_SIZE_4K,
    "VMM: metadata exceeds page size"
);

vm_region_page_t* genisis_page =
    (vm_region_page_t*)VM_BASE;


// ============================================================
// Initialization
// ============================================================

void vmm_init()
{
    con_write("VMM: initializing VMM\n", 0x07);

    uint64_t pmm_addr = mem_alloc_page();

    if (!pmm_addr || (pmm_addr & VM_PAGE_MASK))
        panic("VMM: physical allocation failed");

    mmu_map_4k(
        VM_BASE,
        pmm_addr,
        PAGE_RW | PAGE_NX
    );

    genisis_page->header.used_count = 1;
    genisis_page->header.next = 0;

    for(int i=0;i<VM_REGIONS_PER_PAGE;i++){
        genisis_page->regions[i].state = VM_ALLOC_UNUSED;
    }
    vm_region_t* region = &genisis_page->regions[0];

    region->base = VM_BASE;
    region->size = PAGE_SIZE_4K;
    region->next = 0;
    region->prev = 0;
    region->state = VM_ALLOC_EXCLUSIVE;

    con_write("VMM: ready\n", 0x02);
}


// ============================================================
// Alignment
// ============================================================

uint64_t get_alignment_padding(uint64_t val, uint64_t align)
{
    if (!align)
        panic("VMM: zero alignment");

    uint64_t remainder = val % align;

    return remainder ? align - remainder : 0;
}


// ============================================================
// Region creation
// ============================================================

vm_region_t* find_unused(){
    vm_region_page_t* header = genisis_page;
    while(1){
        for(int i=0;i<VM_REGIONS_PER_PAGE;i++){
            vm_region_t* desc = &(header->regions[i]);
            if(desc->state == VM_ALLOC_UNUSED){
                header->header.used_count++;
                desc->state = VM_ALLOC_PROCESSING;
                return desc;
            }
        }
        vm_region_page_t* next = header->header.next;
        if(!next){
            panic("VMM: faild to find unused region");
        }
        header = next;
        //panic("VMM: region metadata full");
    }
}

static vm_region_t* insert_region_after(
    vm_region_t* prev,
    uint64_t base,
    uint64_t size,
    alloc_state state)
{
    vm_region_page_t* header = genisis_page;
    while(header->header.used_count >= VM_REGIONS_PER_PAGE){
        vm_region_page_t* next = header->header.next;
        if(!next){
            panic("VMM: faild to allocate region, no free metadataslot");
        }
        header = next;
    }
    //if (genisis_page->header.used_count >= VM_REGIONS_PER_PAGE)
    //    panic("VMM: region metadata full");

    vm_region_t* region = find_unused();

    region->base = base;
    region->size = size;
    region->state = state;

    region->prev = prev;
    region->next = prev->next;

    if (region->next)
        region->next->prev = region;

    prev->next = region;

    return region;
}


// ============================================================
// Virtual memory allocation
// ============================================================

void allocate_more_metadata(){
    vm_region_page_t* meta = genisis_page;
    while (meta->header.next)
    {
        meta = meta->header.next;
    }
    if(meta->header.used_count != VM_REGIONS_PER_PAGE-2){
        return;
    }
    //con_write("allocating new metadata\n",0x05);
    vm_region_page_t* new_meta = (vm_region_page_t*)vm_alloc(sizeof(vm_region_page_t),4096,VM_FLAGS_DEFAULT);
    new_meta->header.next = 0;
    new_meta->header.used_count = 0;
    meta->header.next = new_meta;
    for(int i=0;i<VM_REGIONS_PER_PAGE;i++){
        new_meta->regions[i].state = VM_ALLOC_UNUSED;
    }
}

void* vm_alloc(uint64_t size, uint64_t alignment, uint8_t flags)
{
    if (!size)
        return 0;

    if (!alignment)
        alignment = VM_DEFAULT_ALIGNMENT;

    if(flags & VM_FLAGS_FULL_PAGES){
        alignment = PAGE_SIZE_4K;
        size = int_div_round_up(size,PAGE_SIZE_4K) * PAGE_SIZE_4K;
    }
    bool used_free_block = false;

    uint64_t alignment_padding = 0;
    uint64_t free_padding = 0;

    vm_region_t* last_block = &genisis_page->regions[0];


    // --------------------------------------------------------
    // Find a suitable free region
    // --------------------------------------------------------

    while (1) {
        alignment_padding = get_alignment_padding(
            last_block->base,
            alignment
        );

        if (last_block->state == VM_ALLOC_FREE &&
            alignment_padding <= last_block->size &&
            size <= last_block->size - alignment_padding)
        {
            used_free_block = true;

            free_padding =
                last_block->size -
                alignment_padding -
                size;

            break;
        }

        if (!last_block->next)
            break;

        last_block = last_block->next;
    }


    // --------------------------------------------------------
    // Calculate the allocation address
    // --------------------------------------------------------

    uint64_t raw_base = last_block->base;

    if (!used_free_block) {

        if (last_block->size > UINT64_MAX - raw_base)
            panic("VMM: address overflow");

        raw_base += last_block->size;

        alignment_padding = get_alignment_padding(
            raw_base,
            alignment
        );
    }

    if (alignment_padding > UINT64_MAX - raw_base)
        panic("VMM: alignment overflow");

    uint64_t base = raw_base + alignment_padding;


    // --------------------------------------------------------
    // Overflow checks
    // --------------------------------------------------------

    if (base > UINT64_MAX - VM_PAGE_MASK)
        panic("VMM: address overflow");

    if (size > UINT64_MAX - base - VM_PAGE_MASK)
        panic("VMM: allocation size overflow");


    // --------------------------------------------------------
    // Calculate page boundaries
    // --------------------------------------------------------

    uint64_t pagealigned_base =
        base & ~VM_PAGE_MASK;

    uint64_t pagealigned_end =
        (base + size + VM_PAGE_MASK) & ~VM_PAGE_MASK;


    // --------------------------------------------------------
    // Check metadata capacity
    // --------------------------------------------------------

    //uint64_t required_regions =
    //    (!used_free_block) +
    //    (alignment_padding != 0) +
    //    (free_padding != 0);
    /*
    uint64_t used_count =
        genisis_page->header.used_count;

    if (used_count > VM_REGIONS_PER_PAGE ||
        required_regions > VM_REGIONS_PER_PAGE - used_count)
    {
        panic("VMM: region metadata full");
    }*/


    // --------------------------------------------------------
    // Map physical pages
    // --------------------------------------------------------

    for (uint64_t virt = pagealigned_base;
         virt < pagealigned_end;
         virt += PAGE_SIZE_4K)
    {
        uint64_t existing_phys;

        // Packed allocations can share pages.
        if (virt_to_phys(virt, &existing_phys))
            continue;

        uint64_t page_phys = mem_alloc_page();

        if (!page_phys || (page_phys & VM_PAGE_MASK))
            panic("VMM: physical allocation failed");

        mmu_map_4k(
            virt,
            page_phys,
            PAGE_RW | PAGE_NX
        );
    }


    // --------------------------------------------------------
    // Create allocation descriptors
    // --------------------------------------------------------

    vm_region_t* block;

    if (used_free_block) {

        if (alignment_padding) {

            // Keep the original descriptor as the free prefix.
            last_block->size = alignment_padding;

            // Insert the allocation after the prefix.
            block = insert_region_after(
                last_block,
                base,
                size,
                VM_ALLOC_USED
            );

        } else {

            // No prefix needed. Reuse the original descriptor.
            block = last_block;

            block->size = size;
            block->state = VM_ALLOC_USED;
        }

        // Create the remaining free suffix.
        if (free_padding) {

            insert_region_after(
                block,
                base + size,
                free_padding,
                VM_ALLOC_FREE
            );
        }

    } else {

        // Create a free region for the alignment padding.
        if (alignment_padding) {

            last_block = insert_region_after(
                last_block,
                raw_base,
                alignment_padding,
                VM_ALLOC_FREE
            );
        }

        // Append the actual allocation.
        insert_region_after(
            last_block,
            base,
            size,
            VM_ALLOC_USED
        );
    }
    allocate_more_metadata();
    return (void*)base;
}


// ============================================================
// Virtual memory freeing
// ============================================================

vm_region_page_t* get_header(vm_region_t* region){
    uint64_t addr = (uint64_t)region;
    uint64_t header_addr = addr & ~(PAGE_SIZE_4K-1);
    return (vm_region_page_t*)header_addr;
}

void merge_free_regions()
{
    vm_region_t* block = &genisis_page->regions[0];

    while (block && block->next) {
        vm_region_t* next = block->next;

        if (block->state == VM_ALLOC_FREE &&
            next->state == VM_ALLOC_FREE &&
            block->base + block->size == next->base)
        {
            block->size += next->size;
            block->next = next->next;

            if (block->next)
                block->next->prev = block;

            next->state = VM_ALLOC_UNUSED;
            next->next = 0;
            next->prev = 0;
            next->base = 0;
            next->size = 0;
            vm_region_page_t* header = get_header(next);
            header->header.used_count--;

            continue;
        }

        block = block->next;
    }
}

void vm_free(void* addr)
{
    if (!addr)
        return;

    uint64_t va = (uint64_t)addr;

    vm_region_t* block = &genisis_page->regions[0];


    // --------------------------------------------------------
    // Find the allocation
    // --------------------------------------------------------

    while (block && block->base != va)
        block = block->next;

    if (!block)
        panic("VMM: invalid free");

    if (block->state == VM_ALLOC_FREE)
        panic("VMM: double free");

    if (block->state == VM_ALLOC_EXCLUSIVE)
        panic("VMM: cannot free exclusive region");


    // --------------------------------------------------------
    // Calculate page boundaries
    // --------------------------------------------------------

    uint64_t pagealigned_base =
        block->base & ~VM_PAGE_MASK;

    uint64_t pagealigned_end =
        (block->base + block->size + VM_PAGE_MASK)
        & ~VM_PAGE_MASK;


    // --------------------------------------------------------
    // Find nearest occupied regions
    // --------------------------------------------------------

    vm_region_t* prev = block->prev;
    vm_region_t* next = block->next;

    while (prev && prev->state == VM_ALLOC_FREE)
        prev = prev->prev;

    while (next && next->state == VM_ALLOC_FREE)
        next = next->next;


    // --------------------------------------------------------
    // Protect shared pages
    // --------------------------------------------------------

    if (prev) {
        uint64_t prev_end = prev->base + prev->size;

        if (prev_end > pagealigned_base)
            pagealigned_base += PAGE_SIZE_4K;
    }

    if (next && pagealigned_base < pagealigned_end) {

        if (next->base < pagealigned_end)
            pagealigned_end -= PAGE_SIZE_4K;
    }


    // --------------------------------------------------------
    // Mark region as free
    // --------------------------------------------------------

    block->state = VM_ALLOC_FREE;


    // --------------------------------------------------------
    // Release unused physical pages
    // --------------------------------------------------------

    for (uint64_t virt = pagealigned_base;
         virt < pagealigned_end;
         virt += PAGE_SIZE_4K)
    {
        uint64_t page_phys;

        if (!virt_to_phys(virt, &page_phys))
            panic("VMM: allocation page not mapped");

        mmu_unmap_4k(virt);

        mem_free_page(page_phys);
    }
    merge_free_regions();
    // TODO: Merge adjacent free regions.
}


// ============================================================
// Debug output
// ============================================================

void print_alloc()
{
    vm_region_t* block = &genisis_page->regions[0];

    uint64_t count = 0;

    while (block) {

        if (++count > genisis_page->header.used_count)
            panic("VMM: circular region list");

        con_write("start: ", 0x05);
        print_uint64(block->base);

        con_write("\nend: ", 0x05);
        print_uint64(block->base + block->size - 1);

        con_write("\nsize: ", 0x05);
        print_uint64(block->size);

        con_write("\nstate: ", 0x05);

        switch (block->state) {
            case VM_ALLOC_FREE:
                con_write("FREE\n", 0x02);
                break;

            case VM_ALLOC_USED:
                con_write("USED\n", 0x01);
                break;

            case VM_ALLOC_EXCLUSIVE:
                con_write("EXCLUSIVE\n", 0x05);
                break;
            
            case VM_ALLOC_UNUSED:
                con_write("UNUSED\n", 0x07);
                break;
            
            case VM_ALLOC_PROCESSING:
                con_write("PROCESSING\n", 0x03);
                break;
        }

        block = block->next;
    }
}
