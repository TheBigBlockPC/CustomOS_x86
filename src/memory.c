#include "types.h"
#include "memory.h"
#include "console.h"
#include "paging.h"
#include "util.h"
#include "panic.h"

#define MAX_FREE_RANGES 128
#define PAGE_SIZE 4096
#define PAGE_SIZE_2M   0x200000ULL
#define PAGE_2M_ADDR_MASK     0x000FFFFFFFE00000ULL

#define HHDM_BASE 0xffff800000000000ULL

static FreeRange free_ranges[MAX_FREE_RANGES];
static size_t free_range_count = 0;
static uint64_t total_page_count = 0;

static uint64_t bitmap_phys;
//static uint8_t* page_bitmap;
static uint64_t free_page_count = 0;

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
    free_range_count++;
}


uint64_t mem_get_memory_size(){
    return total_page_count * PAGE_SIZE;
}

static uint64_t get_highest_page() {
    FreeRange last_range = free_ranges[free_range_count-1];
    return last_range.start / PAGE_SIZE + last_range.page_count;
}

static uint64_t allocate_bitmap(uint64_t page_count){
    for(int i = 0; i < free_range_count; i++){
        FreeRange r = free_ranges[i];
        if(r.page_count < page_count){
            continue;
        }
        uint64_t result = free_ranges[i].start;
        free_ranges[i].start += page_count * PAGE_SIZE;
        free_ranges[i].page_count -= page_count;
        return result;
    }
    con_write("failed to allocate bitmap",0x01);
    while(1){}
}

static inline uint8_t* get_page_bitmap(){
    if(get_mmu_active()){
        return (uint8_t*)(bitmap_phys+HHDM_BASE);
    }else{
        return (uint8_t*)bitmap_phys;
    }
}

static void clear_page(uint64_t phys){
    uint64_t hhdm_offset = get_mmu_active() * HHDM_BASE;
    for(uint64_t offset = 0;offset < PAGE_SIZE;offset++){
        uint8_t* addr = (uint8_t*)(phys+offset+ hhdm_offset);
        *addr = 0;
    }
}

static inline void bitmap_set(uint64_t page)
{
    uint8_t* page_bitmap = get_page_bitmap();
    page_bitmap[page / 8] |=
        (1u << (page % 8));
}

static inline void bitmap_clear(uint64_t page)
{
    uint8_t* page_bitmap = get_page_bitmap();
    page_bitmap[page / 8] &=
        ~(1u << (page % 8));
}

static inline int bitmap_test(uint64_t page)
{
    uint8_t* page_bitmap = get_page_bitmap();
    return
        (page_bitmap[page / 8] >>
         (page % 8)) & 1;
}

static void mem_mark_range_free(
    uint64_t start,
    uint64_t page_count)
{
    uint64_t first_page =
        start / PAGE_SIZE;

    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t page = first_page + i;

        bitmap_clear(page);
        free_page_count++;
    }
}

void init_memory_subsystem(){
    con_write("initializing memory\n",0x07);
    uint64_t highest_page = get_highest_page();
    uint64_t bitmap_size = int_div_round_up(highest_page, 8);
    uint64_t number_of_bitmap_pages = int_div_round_up(bitmap_size, PAGE_SIZE);
    con_write("found highest page at ",0x02);
    print_uint64(highest_page);
    con_write("\n",0x07);
    con_write("bitmap size ",0x02);
    print_uint64(number_of_bitmap_pages);
    con_write("\n",0x07);

    bitmap_phys = allocate_bitmap(number_of_bitmap_pages);
    con_write("bitmap allocated at ",0x02);
    print_uint64(bitmap_phys);
    con_write("\n",0x07);

    uint8_t* page_bitmap = get_page_bitmap();
    for (uint64_t i = 0; i < bitmap_size; i++){
        page_bitmap[i] = 0xFF;
    }

    for (size_t i = 0; i < free_range_count; i++) {
        mem_mark_range_free(
            free_ranges[i].start,
            free_ranges[i].page_count
        );
    }

    con_write("free pages: ",0x02);
    print_uint64(free_page_count);
    con_write("\n",0x07);
}

uint64_t mem_alloc_page(void)
{
    if (free_page_count == 0){
        panic("failed to allocate page: OOM");
        return 0;
    }
    for (uint64_t page = 0;
         page < total_page_count;
         page++)
    {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            clear_page(page * PAGE_SIZE);
            free_page_count--;
            return page * PAGE_SIZE;
        }
    }
    panic("failed to allocate page: OOM");
}

void mem_free_page(uint64_t address){
    if (address & (PAGE_SIZE - 1)) {
        con_write("mem_free_page: unaligned address\n", 0x01);
        return;
    }
    uint64_t page = address / PAGE_SIZE;
    if (page == 0 || page >= total_page_count) {
        con_write("mem_free_page: invalid address\n", 0x01);
        return;
    }

    if (!bitmap_test(page)) {
        con_write("mem_free_page: double free\n", 0x01);
        return;
    }

    bitmap_clear(page);
    free_page_count++;
}

void setup_hhdm(){
    
    for(int i=0;i<free_range_count;i++){
        FreeRange r = free_ranges[i];
        uint64_t n_chunks =int_div_round_up(r.page_count*PAGE_SIZE,PAGE_SIZE_2M) + 1;
        uint64_t base = r.start & PAGE_2M_ADDR_MASK;
        for(uint64_t chunk=0; chunk < n_chunks; chunk ++){
            uint64_t addr = base + PAGE_SIZE_2M*chunk;
            mmu_map_2m(HHDM_BASE+addr,addr);
        }
    }
    uint64_t highest_page = get_highest_page();
    uint64_t bitmap_size = int_div_round_up(highest_page, 8);
    uint64_t MMU_bitmap = int_div_round_up(bitmap_size, PAGE_SIZE_2M) + 1;
    uint64_t base = bitmap_phys & PAGE_2M_ADDR_MASK;
    for(uint64_t chunk=0; chunk < MMU_bitmap; chunk ++){
        uint64_t addr = base + PAGE_SIZE_2M*chunk;
        mmu_map_2m(HHDM_BASE+addr,addr);
    }
   /*
   uint64_t highest_page = get_highest_page();
   uint64_t n_chunks = int_div_round_up(highest_page * PAGE_SIZE, PAGE_SIZE_2M);
   for(uint64_t chunk=0; chunk < n_chunks; chunk ++){
        uint64_t addr = PAGE_SIZE_2M*chunk;
        mmu_map_2m(HHDM_BASE+addr,addr);
    }*/
}