#include "paging.h"
#include "memory.h"
#include "panic.h"
#include "util.h"
#include "console.h"

#define PAGE_TABLE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PAGE_2M_ADDR_MASK    0x000FFFFFFFE00000ULL
#define MAP_FLAGS (PAGE_RW | PAGE_USER | PAGE_PWT | PAGE_PCD | PAGE_NX)

typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) page_table_t;
static page_table_t PML4;
static bool mmu_active;
static uint64_t physical_mask;

static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static bool canonical(uint64_t address)
{
    return address <= 0x00007fffffffffffULL || address >= 0xffff800000000000ULL;
}

static void validate_mapping(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags)
{
    if (!physical_mask) {
        uint32_t a, b, c, d;
        cpuid(0x80000000, &a, &b, &c, &d);
        uint32_t bits = 36;
        if (a >= 0x80000008) {
            cpuid(0x80000008, &a, &b, &c, &d);
            bits = a & 0xff;
        }
        if (bits < 32 || bits > 52) panic("MMU: unsupported physical address width");
        physical_mask = ((1ULL << bits) - 1) & PAGE_TABLE_ADDR_MASK;
    }
    if ((virt & (size - 1)) || (phys & (size - 1)) || !canonical(virt) ||
        !canonical(virt + size - 1) || (phys & ~physical_mask) || (flags & ~MAP_FLAGS))
        panic("MMU: invalid mapping address or flags");
}

static page_table_t* table_at(uint64_t entry)
{
    return mem_phys_to_virt(entry & PAGE_TABLE_ADDR_MASK);
}

static void flush_page(uint64_t address)
{
    if (mmu_active) __asm__ volatile("invlpg (%0)" :: "r"(address) : "memory");
}

static page_table_t* next_table(page_table_t* table, uint16_t index, uint64_t flags)
{
    uint64_t* entry = &table->entries[index];
    if (!(*entry & PAGE_PRESENT))
        *entry = mem_alloc_page() | PAGE_PRESENT | PAGE_RW;
    if (*entry & PAGE_PS) panic("MMU: unexpected huge parent entry");
    /* Restrictions belong on leaves; parents must permit user mappings. */
    if (flags & PAGE_USER) *entry |= PAGE_USER;
    return table_at(*entry);
}

static page_table_t* get_pd(uint64_t address, uint64_t flags)
{
    page_table_t* pdp = next_table(&PML4, (address >> 39) & 511, flags);
    return next_table(pdp, (address >> 30) & 511, flags);
}

void mmu_map_4k(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags)
{
    validate_mapping(virtual_addr, physical_addr, PAGE_SIZE_4K, flags);
    page_table_t* pd = get_pd(virtual_addr, flags);
    uint64_t* pde = &pd->entries[(virtual_addr >> 21) & 511];
    if ((*pde & (PAGE_PRESENT | PAGE_PS)) == (PAGE_PRESENT | PAGE_PS)) {
        uint64_t old = *pde;
        uint64_t pt_phys = mem_alloc_page();
        page_table_t* pt = table_at(pt_phys);
        uint64_t inherited = old & (MAP_FLAGS | PAGE_ACCESSED | PAGE_DIRTY);
        for (uint64_t i = 0; i < 512; i++)
            pt->entries[i] = ((old & PAGE_2M_ADDR_MASK) + i * PAGE_SIZE_4K) |
                             inherited | PAGE_PRESENT;
        *pde = pt_phys | PAGE_PRESENT | PAGE_RW | (old & PAGE_USER);
        /* INVLPG also invalidates the former large-page translation. */
        flush_page(virtual_addr);
    }
    page_table_t* pt = next_table(pd, (virtual_addr >> 21) & 511, flags);
    pt->entries[(virtual_addr >> 12) & 511] = physical_addr | flags | PAGE_PRESENT;
    flush_page(virtual_addr);
}

void mmu_unmap_4k(uint64_t virtual_addr)
{
    page_table_t* pd = get_pd(virtual_addr, 0);
    uint64_t* pde = &pd->entries[(virtual_addr >> 21) & 511];
    if ((*pde & (PAGE_PRESENT | PAGE_PS)) == (PAGE_PRESENT | PAGE_PS)) {
        uint64_t old = *pde;
        uint64_t pt_phys = mem_alloc_page();
        page_table_t* pt = table_at(pt_phys);
        uint64_t inherited = old & (MAP_FLAGS | PAGE_ACCESSED | PAGE_DIRTY);
        for (uint64_t i = 0; i < 512; i++)
            pt->entries[i] = ((old & PAGE_2M_ADDR_MASK) + i * PAGE_SIZE_4K) |
                             inherited | PAGE_PRESENT;
        *pde = pt_phys | PAGE_PRESENT | PAGE_RW | (old & PAGE_USER);
        /* INVLPG also invalidates the former large-page translation. */
        flush_page(virtual_addr);
    }
    page_table_t* pt = next_table(pd, (virtual_addr >> 21) & 511, 0);
    pt->entries[(virtual_addr >> 12) & 511] = 0;
    flush_page(virtual_addr);
}

void mmu_map_2m(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags)
{
    validate_mapping(virtual_addr, physical_addr, PAGE_SIZE_2M, flags);
    page_table_t* pd = get_pd(virtual_addr, flags);
    uint64_t* entry = &pd->entries[(virtual_addr >> 21) & 511];
    if ((*entry & PAGE_PRESENT) && !(*entry & PAGE_PS))
        panic("MMU: 2M mapping would overwrite a 4K table");
    *entry = physical_addr | flags | PAGE_PRESENT | PAGE_PS;
    flush_page(virtual_addr);
}

void identitymap_4k(uint64_t address, uint64_t flags) { mmu_map_4k(address, address, flags); }
void identitymap_2m(uint64_t address, uint64_t flags) { mmu_map_2m(address, address, flags); }

void mmu_identity_map_range(uint64_t address, uint64_t length, uint64_t flags)
{
    if (!length) return;
    if (length - 1 > ~address) panic("MMU: mapping range overflow");
    uint64_t last = (address + length - 1) & ~(PAGE_SIZE_4K - 1);
    uint64_t page = address & ~(PAGE_SIZE_4K - 1);
    validate_mapping(page, page, PAGE_SIZE_4K, flags);
    validate_mapping(last, last, PAGE_SIZE_4K, flags);
    if (page <= 0x00007fffffffffffULL && last > 0x00007fffffffffffULL)
        panic("MMU: range crosses the canonical address hole");
    for (;;) {
        identitymap_4k(page, flags);
        if (page == last) break;
        page += PAGE_SIZE_4K;
    }
}

void set_mmu(void)
{
    uint32_t a, b, c, d;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a < 0x80000001) panic("MMU: missing NX capability");
    cpuid(0x80000001, &a, &b, &c, &d);
    if (!(d & (1u << 20))) panic("MMU: NX pages unsupported");
    cpuid(1, &a, &b, &c, &d);
    if (!(d & (1u << 16))) panic("MMU: PAT unsupported");
    __asm__ volatile("cli" ::: "memory");
    __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080));
    a |= 1u << 11; /* EFER.NXE before loading tables containing NX bits. */
    __asm__ volatile("wrmsr" :: "a"(a), "d"(d), "c"(0xc0000080) : "memory");
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1ULL << 16; /* CR0.WP: read-only pages apply in ring zero too. */
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & (1ULL << 12)) panic("MMU: five-level firmware paging unsupported");
    /* Drop firmware global/PCID translations; this kernel uses neither. */
    cr4 &= ~((1ULL << 7) | (1ULL << 17));
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
    /* Establish the PAT contract: slot 0 = WB RAM, slot 3 (PCD|PWT) = UC.
     * Change memory types with caching disabled and caches/TLBs flushed. */
    uint64_t uncached_cr0 = (cr0 | (1ULL << 30)) & ~(1ULL << 29);
    __asm__ volatile("mov %0, %%cr0; wbinvd" :: "r"(uncached_cr0) : "memory");
    __asm__ volatile("wrmsr" :: "c"(0x277), "a"(0x00070406u), "d"(0x00070406u) : "memory");
    __asm__ volatile("wbinvd" ::: "memory");
    __asm__ volatile("mov %0, %%cr3" :: "r"(mem_kernel_to_phys(&PML4)) : "memory");
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    mmu_active = true;
}

int get_mmu_active(void) { return mmu_active; }

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline uint64_t *phys_to_table(uint64_t phys) {
    return (uint64_t *)(mem_phys_to_virt(phys));
}

bool virt_to_phys(uint64_t va, uint64_t *pa) {
    // Check LA48 canonicality
    uint64_t upper = va >> 48;
    uint64_t sign = (va >> 47) & 1;

    if (upper != (sign ? 0xFFFF : 0))
        return false;

    uint64_t indices[4] = {
        (va >> 39) & 0x1FF,
        (va >> 30) & 0x1FF,
        (va >> 21) & 0x1FF,
        (va >> 12) & 0x1FF
    };

    uint64_t table_phys = read_cr3() & PAGE_TABLE_ADDR_MASK;
    for (int level = 0; level < 4; level++) {
        
        uint64_t *table = phys_to_table(table_phys);
        uint64_t entry = table[indices[level]];

        if (!(entry & PAGE_PRESENT))
            return false;

        // Huge pages: 1 GiB at PDPT, 2 MiB at PD
        if ((level == 1 || level == 2) &&
            (entry & PAGE_HUGE)) {

            unsigned shift = (level == 1) ? 30 : 21;
            uint64_t offset_mask = (1ULL << shift) - 1;

            *pa = (entry & PAGE_TABLE_ADDR_MASK & ~offset_mask)
                | (va & offset_mask);

            return true;
        }

        table_phys = entry & PAGE_TABLE_ADDR_MASK;
    }

    *pa = table_phys | (va & 0xFFF);
    return true;
}