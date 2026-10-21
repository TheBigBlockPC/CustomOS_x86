/* Hosted regression tests using low fixed mappings as fake physical RAM.
 * No system headers: the kernel currently owns its integer/size typedefs. */
#include "../src/memory.c"
#include "../src/paging.c"

extern int printf(const char*, ...);
extern void exit(int) __attribute__((noreturn));
extern void* mmap(void*, size_t, int, int, int, int64_t);
extern int _setjmp(void*) __attribute__((returns_twice));
extern void longjmp(void*, int) __attribute__((noreturn));

static uint64_t panic_jump[32];
static bool expecting_panic;
static int checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
#define EXPECT_PANIC(expression) do { expecting_panic = true; \
    if (!_setjmp(panic_jump)) { expression; CHECK(false); } \
    expecting_panic = false; } while (0)

void panic(char* message)
{
    if (expecting_panic) longjmp(panic_jump, 1);
    printf("Unexpected panic: %s\n", message);
    exit(1);
}
void con_write(char* s, uint8_t c) { (void)s; (void)c; }
void print_uint64(uint64_t n) { (void)n; }
void memory_zero(void* p, uint64_t n) { for (uint64_t i = 0; i < n; i++) ((uint8_t*)p)[i] = 0; }
void memory_set(void* p, uint64_t n, uint8_t v) { for (uint64_t i = 0; i < n; i++) ((uint8_t*)p)[i] = v; }
uint64_t int_div_round_up(uint64_t a, uint64_t b) { return a / b + (a % b != 0); }

static void reset(void)
{
    memory_zero(free_ranges, sizeof(free_ranges));
    free_range_count = total_page_count = highest_page = free_page_count = 0;
    bitmap_phys = bitmap_pages = next_page = 0;
    memory_initialized = false;
    memory_zero(&PML4, sizeof(PML4));
}

static uint64_t leaf(uint64_t address, bool* huge)
{
    uint64_t entry = PML4.entries[(address >> 39) & 511];
    if (!(entry & PAGE_PRESENT)) return 0;
    entry = table_at(entry)->entries[(address >> 30) & 511];
    if (!(entry & PAGE_PRESENT)) return 0;
    entry = table_at(entry)->entries[(address >> 21) & 511];
    *huge = (entry & PAGE_PS) != 0;
    if (!(entry & PAGE_PRESENT) || *huge) return entry;
    return table_at(entry)->entries[(address >> 12) & 511];
}

int main(void)
{
    CHECK(mmap((void*)0x100000, 0x400000, 3, 0x32, -1, 0) == (void*)0x100000);
    CHECK(mmap((void*)0x80000000, 0x40000, 3, 0x32, -1, 0) == (void*)0x80000000);
    reset();
    mem_set_kernel_image(0x100000, 0x3000);
    CHECK(mem_kernel_to_phys((void*)KERNEL_BASE) == 0x100000);
    CHECK(mem_kernel_to_phys((void*)(KERNEL_BASE + 0x2fff)) == 0x102fff);
    EXPECT_PANIC(mem_kernel_to_phys((void*)(KERNEL_BASE + 0x3000)));
    EXPECT_PANIC(mem_kernel_to_phys((void*)0x100000));
    CHECK(mem_phys_to_virt(0x101000) == (void*)0x101000);
    mmu_active = true;
    CHECK(mem_phys_to_virt(0x101000) == (void*)(KERNEL_BASE + 0x1000));
    CHECK(mem_phys_to_virt(0x103000) == (void*)(HHDM_BASE + 0x103000));
    mmu_active = false;
    mem_set_kernel_image(0, 0);
    mem_add_free_pages(0, 2);
    CHECK(free_ranges[0].start == PAGE_SIZE && free_ranges[0].page_count == 1);
    reset();
    EXPECT_PANIC(init_memory_subsystem());
    EXPECT_PANIC(mem_add_free_pages(1, 2));
    EXPECT_PANIC(mem_add_free_pages(HHDM_SIZE - PAGE_SIZE, 2));
    /* Highest descriptor first: the map need not be sorted. */
    mem_add_free_pages(0x80000000, 64);
    mem_add_free_pages(0x100000, 1024);
    EXPECT_PANIC(mem_add_free_pages(0x101000, 1));
    CHECK(highest_page == 0x80040000 / PAGE_SIZE);
    init_memory_subsystem();
    EXPECT_PANIC(init_memory_subsystem());
    EXPECT_PANIC(mem_add_free_pages(0x600000, 1));
    uint64_t initial_free = mem_get_free_page_count();
    CHECK(initial_free == 1088 - bitmap_pages);
    mem_free_page(0);
    mem_free_page(0x100001);
    mem_free_page(bitmap_phys);
    mem_free_page(0x600000); /* Hole, not allocatable. */
    mem_free_page(0x80040000); /* Exclusive end of highest range. */
    CHECK(mem_get_free_page_count() == initial_free);
    uint64_t allocated[1088];
    bool reached_high_memory = false;
    for (uint64_t i = 0; i < initial_free; i++) {
        allocated[i] = mem_alloc_page();
        CHECK(allocated[i] != 0);
        if (allocated[i] >= 0x80000000) reached_high_memory = true;
        for (uint64_t j = 0; j < PAGE_SIZE; j++) CHECK(((uint8_t*)allocated[i])[j] == 0);
        memory_set((void*)allocated[i], PAGE_SIZE, 0xaa);
    }
    CHECK(reached_high_memory);
    CHECK(mem_get_free_page_count() == 0);
    EXPECT_PANIC(mem_alloc_page());
    mem_free_page(allocated[0]);
    mem_free_page(allocated[0]);
    CHECK(mem_get_free_page_count() == 1);
    CHECK(mem_alloc_page() == allocated[0]);
    CHECK(*(uint64_t*)allocated[0] == 0);
    for (uint64_t i = 0; i < initial_free; i++) mem_free_page(allocated[i]);
    CHECK(mem_get_free_page_count() == initial_free);

    setup_hhdm();
    bool huge;
    CHECK((leaf(HHDM_BASE + 0x200000, &huge) & PAGE_PS) && huge);
    CHECK(leaf(HHDM_BASE + 0x100000, &huge) & PAGE_NX);
    CHECK(!huge);
    CHECK(!(leaf(HHDM_BASE + 0x500000, &huge) & PAGE_PRESENT));
    CHECK(!(leaf(HHDM_BASE + 0x80040000, &huge) & PAGE_PRESENT));
    const uint64_t virt = 0x40000000;
    mmu_map_2m(virt, 0x200000, PAGE_RW | PAGE_NX);
    mmu_map_4k(virt + PAGE_SIZE, 0x80000000, PAGE_USER);
    uint64_t entry = leaf(virt + PAGE_SIZE, &huge);
    CHECK(!huge && (entry & PAGE_TABLE_ADDR_MASK) == 0x80000000);
    CHECK((entry & PAGE_USER) && !(entry & (PAGE_RW | PAGE_NX)));
    entry = leaf(virt + 2 * PAGE_SIZE, &huge);
    CHECK(!huge && (entry & PAGE_TABLE_ADDR_MASK) == 0x202000);
    CHECK((entry & (PAGE_RW | PAGE_NX)) == (PAGE_RW | PAGE_NX));
    uint64_t ancestor = PML4.entries[(virt >> 39) & 511];
    CHECK(ancestor & PAGE_USER);
    ancestor = table_at(ancestor)->entries[(virt >> 30) & 511];
    CHECK(ancestor & PAGE_USER);
    CHECK(table_at(ancestor)->entries[(virt >> 21) & 511] & PAGE_USER);
    EXPECT_PANIC(mmu_map_2m(virt, 0x200000, PAGE_RW));
    EXPECT_PANIC(mmu_map_4k(virt + 1, 0x200000, PAGE_RW));
    EXPECT_PANIC(mmu_map_4k(0x0000800000000000ULL, 0x200000, 0));
    EXPECT_PANIC(mmu_map_4k(virt, 1ULL << 60, 0));
    EXPECT_PANIC(mmu_map_4k(virt, 0x200000, PAGE_PS));
    EXPECT_PANIC(mmu_identity_map_range(~0ULL - 5, 20, 0));
    mmu_identity_map_range(0x600fff, 2, PAGE_NX);
    CHECK(leaf(0x600000, &huge) & PAGE_NX);
    CHECK(leaf(0x601000, &huge) & PAGE_NX);
    CHECK(!(leaf(0x602000, &huge) & PAGE_PRESENT));
    printf("PASS: %d allocator and page-table checks\n", checks);
    return 0;
}
