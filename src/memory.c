#include "memory.h"
#include "console.h"
#include "paging.h"
#include "util.h"
#include "panic.h"

#define MAX_FREE_RANGES 128

static FreeRange free_ranges[MAX_FREE_RANGES];
static size_t free_range_count;
static uint64_t total_page_count, highest_page, free_page_count;
static uint64_t bitmap_phys, bitmap_pages, next_page;
static bool memory_initialized;

void mem_add_free_pages(uint64_t start, uint64_t page_count)
{
    if (memory_initialized) panic("PMM: memory map already initialized");
    if (!page_count) return;
    if ((start & (PAGE_SIZE - 1)) || start >= HHDM_SIZE ||
        page_count > (HHDM_SIZE - start) / PAGE_SIZE)
        panic("PMM: invalid physical memory range");
    /* Physical zero is never a successful allocation. */
    if (!start) {
        start = PAGE_SIZE;
        if (!--page_count) return;
    }
    if (free_range_count == MAX_FREE_RANGES) panic("PMM: too many memory ranges");
    uint64_t end = start + page_count * PAGE_SIZE;
    size_t index = 0;
    while (index < free_range_count && free_ranges[index].start < start) index++;
    if ((index && free_ranges[index - 1].start +
                  free_ranges[index - 1].page_count * PAGE_SIZE > start) ||
        (index < free_range_count && end > free_ranges[index].start))
        panic("PMM: overlapping memory ranges");
    for (size_t i = free_range_count; i > index; i--) free_ranges[i] = free_ranges[i - 1];
    free_ranges[index] = (FreeRange){start, page_count};
    free_range_count++;
    total_page_count += page_count;
    if (end / PAGE_SIZE > highest_page) highest_page = end / PAGE_SIZE;
}

uint64_t mem_get_memory_size(void) { return total_page_count * PAGE_SIZE; }
uint64_t mem_get_free_page_count(void) { return free_page_count; }

void* mem_phys_to_virt(uint64_t address)
{
    return (void*)(address + (get_mmu_active() ? HHDM_BASE : 0));
}

static uint8_t* get_page_bitmap(void) { return mem_phys_to_virt(bitmap_phys); }
static void bitmap_set(uint64_t page) { get_page_bitmap()[page / 8] |= 1u << (page % 8); }
static void bitmap_clear(uint64_t page) { get_page_bitmap()[page / 8] &= ~(1u << (page % 8)); }
static bool bitmap_test(uint64_t page) { return (get_page_bitmap()[page / 8] >> (page % 8)) & 1; }

void init_memory_subsystem(void)
{
    if (memory_initialized || !free_range_count) panic("PMM: invalid initialization");
    uint64_t bitmap_size = int_div_round_up(highest_page, 8);
    bitmap_pages = int_div_round_up(bitmap_size, PAGE_SIZE);
    for (size_t i = 0; i < free_range_count; i++) {
        if (free_ranges[i].page_count < bitmap_pages) continue;
        bitmap_phys = free_ranges[i].start;
        free_ranges[i].start += bitmap_pages * PAGE_SIZE;
        free_ranges[i].page_count -= bitmap_pages;
        break;
    }
    if (!bitmap_phys) panic("PMM: cannot reserve bitmap");
    memory_set(get_page_bitmap(), bitmap_pages * PAGE_SIZE, 0xff);
    /* Remaining ranges are also the authority for valid frees. */
    for (size_t i = 0; i < free_range_count; i++) {
        uint64_t first = free_ranges[i].start / PAGE_SIZE;
        for (uint64_t j = 0; j < free_ranges[i].page_count; j++) bitmap_clear(first + j);
        free_page_count += free_ranges[i].page_count;
    }
    memory_initialized = true;
    con_write("PMM free pages: ", 0x02);
    print_uint64(free_page_count);
    con_write("\n", 0x07);
}

uint64_t mem_alloc_page(void)
{
    if (!memory_initialized || !free_page_count) panic("PMM: out of physical pages");
    /* Skip holes, keep a scan hint, and wrap once to reuse earlier frees. */
    for (int pass = 0; pass < 2; pass++, next_page = 0) {
        for (size_t i = 0; i < free_range_count; i++) {
            uint64_t first = free_ranges[i].start / PAGE_SIZE;
            uint64_t end = first + free_ranges[i].page_count;
            uint64_t page = first > next_page ? first : next_page;
            for (; page < end; page++) {
                if (bitmap_test(page)) continue;
                bitmap_set(page);
                free_page_count--;
                next_page = page + 1;
                memory_zero(mem_phys_to_virt(page * PAGE_SIZE), PAGE_SIZE);
                return page * PAGE_SIZE;
            }
        }
    }
    panic("PMM: inconsistent free-page count");
}

void mem_free_page(uint64_t address)
{
    if (!memory_initialized || !address || (address & (PAGE_SIZE - 1)) ||
        address / PAGE_SIZE >= highest_page) {
        con_write("mem_free_page: invalid address\n", 0x01);
        return;
    }
    bool managed = false;
    for (size_t i = 0; i < free_range_count; i++) {
        FreeRange r = free_ranges[i];
        if (address >= r.start && (address - r.start) / PAGE_SIZE < r.page_count) {
            managed = true;
            break;
        }
    }
    if (!managed) {
        con_write("mem_free_page: reserved or unmanaged page\n", 0x01);
        return;
    }
    uint64_t page = address / PAGE_SIZE;
    if (!bitmap_test(page)) {
        con_write("mem_free_page: double free\n", 0x01);
        return;
    }
    bitmap_clear(page);
    free_page_count++;
    if (page < next_page) next_page = page;
}

static void map_ram_range(uint64_t start, uint64_t pages)
{
    while (pages) {
        if (!(start & (PAGE_SIZE_2M - 1)) && pages >= PAGE_SIZE_2M / PAGE_SIZE) {
            mmu_map_2m(HHDM_BASE + start, start, PAGE_RW | PAGE_NX);
            start += PAGE_SIZE_2M;
            pages -= PAGE_SIZE_2M / PAGE_SIZE;
        } else {
            mmu_map_4k(HHDM_BASE + start, start, PAGE_RW | PAGE_NX);
            start += PAGE_SIZE;
            pages--;
        }
    }
}

void setup_hhdm(void)
{
    if (!memory_initialized || get_mmu_active()) panic("PMM: invalid HHDM setup");
    for (size_t i = 0; i < free_range_count; i++)
        map_ram_range(free_ranges[i].start, free_ranges[i].page_count);
    map_ram_range(bitmap_phys, bitmap_pages);
}
