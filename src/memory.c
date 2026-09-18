#include "types.h"
#include "memory.h"
#include "console.h"

#define MAX_FREE_RANGES 128
#define PAGE_SIZE 4096

static FreeRange free_ranges[MAX_FREE_RANGES];
static size_t free_range_count = 0;
static uint64_t total_page_count = 0;

static uint64_t bitmap_phys;
static uint8_t* page_bitmap;
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

static void print_uint64(uint64_t val){
    char hex_chars[16] = {
        '0','1','2','3','4','5','6','7',
        '8','9','A','B','C','D','E','F'
    };

    con_write("0x", 0x07);

    for(int i = 0; i < 16; i++){
        int shift = (15 - i) * 4;
        uint8_t digit = (val >> shift) & 0x0F;

        char seq[2] = {
            hex_chars[digit],
            '\0'
        };

        con_write(seq, 0x07);
    }
}

static uint64_t get_highest_page() {
    FreeRange last_range = free_ranges[free_range_count-1];
    return last_range.start / PAGE_SIZE + last_range.page_count;
}

static uint64_t int_div_round_up(uint64_t a, uint64_t b){
    return a / b + (a % b != 0);
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

static inline void bitmap_set(uint64_t page)
{
    page_bitmap[page / 8] |=
        (1u << (page % 8));
}

static inline void bitmap_clear(uint64_t page)
{
    page_bitmap[page / 8] &=
        ~(1u << (page % 8));
}

static inline int bitmap_test(uint64_t page)
{
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

    page_bitmap = (uint8_t*)bitmap_phys;
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
        con_write("failed to allocate page: OOM",0x01);
        return 0;
    }
    for (uint64_t page = 0;
         page < total_page_count;
         page++)
    {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_page_count--;
            return page * PAGE_SIZE;
        }
    }
    con_write("failed to allocate page: OOM",0x01);
    return 0;
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