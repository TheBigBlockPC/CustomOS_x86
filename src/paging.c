#include "types.h"
#include "memory.h"
#include "console.h"
#include "paging.h"
#include "panic.h"

#define PAGE_PRESENT   (1ULL << 0)
#define PAGE_RW        (1ULL << 1)
#define PAGE_USER      (1ULL << 2)
#define PAGE_PWT       (1ULL << 3)
#define PAGE_PCD       (1ULL << 4)
#define PAGE_ACCESSED  (1ULL << 5)
#define PAGE_DIRTY     (1ULL << 6)
#define PAGE_PS (1ULL << 7)

#define PAGE_SIZE_4K   0x1000ULL
#define PAGE_SIZE_2M   0x200000ULL

#define PAGE_TABLE_ADDR_MASK  0x000FFFFFFFFFF000ULL
#define PAGE_2M_ADDR_MASK     0x000FFFFFFFE00000ULL
#define VAL_2M_ADDR_MASK     0xFFFFFFFFFFE00000ULL
#define HHFM_BASE 0xffff800000000000ULL
typedef uint64_t pte_t;

typedef struct {
    pte_t entries[512];
} __attribute__((aligned(4096))) page_table_t;


static page_table_t PML4
    __attribute__((aligned(4096)));

static uint8_t mmu_active = 0;

static uint64_t get_PDP(uint16_t index){
    pte_t entry = PML4.entries[index];
    if(entry & PAGE_PRESENT){
        return entry & PAGE_TABLE_ADDR_MASK;
    }else{
        //con_write("allocated a PDP\n",0x02);
        uint64_t PDP_addr = mem_alloc_page();
        PML4.entries[index] = PDP_addr & PAGE_TABLE_ADDR_MASK | PAGE_PRESENT | PAGE_RW;
        return PDP_addr;
    }
}
static uint64_t get_PD(uint64_t pdp_phys, uint16_t index)
{
    page_table_t *pdp;

    if (mmu_active)
        pdp = (page_table_t *)(HHFM_BASE + pdp_phys);
    else
        pdp = (page_table_t *)pdp_phys;

    pte_t entry = pdp->entries[index];

    if (entry & PAGE_PRESENT) {
        uint64_t pd_phys =
            entry & PAGE_TABLE_ADDR_MASK;

        if (mmu_active)
            return HHFM_BASE + pd_phys;
        else
            return pd_phys;
    }

    uint64_t pd_phys = mem_alloc_page();

    pdp->entries[index] =
        (pd_phys & PAGE_TABLE_ADDR_MASK)
        | PAGE_PRESENT
        | PAGE_RW;

    if (mmu_active)
        return HHFM_BASE + pd_phys;
    else
        return pd_phys;
}
static void set_PD_entry(uint64_t pd_base,uint16_t index,uint64_t addr){
    page_table_t* pdp = (page_table_t*)pd_base;
    uint64_t new_val = addr & PAGE_2M_ADDR_MASK | PAGE_PRESENT | PAGE_RW | PAGE_PS;
    if((pdp->entries[index] & PAGE_PRESENT) && (pdp->entries[index] != new_val)){
        //con_write("overlapping page \n",0x03);
    }
    pdp->entries[index] = new_val;
}
static inline void invlpg(void *addr)
{
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(addr)
        : "memory"
    );
}
void mmu_map_2m(uint64_t virtual_addr,uint64_t physical_addr){
    if(physical_addr & ~VAL_2M_ADDR_MASK){
        panic("the physical adress must align in 2 mb\n");
    }
    if(virtual_addr & ~VAL_2M_ADDR_MASK){
        panic("the virtual adress must align in 2 mb\n");
    }
    uint16_t pml4_index = (virtual_addr >> 39) & 0x1FF;
    uint16_t pdp = (virtual_addr >> 30) & 0x1FF;
    uint16_t pd = (virtual_addr >> 21) & 0x1FF;

    uint64_t pdp_base = get_PDP(pml4_index);
    uint64_t pd_base = get_PD(pdp_base,pdp);
    set_PD_entry(pd_base,pd,physical_addr);
    if(mmu_active){
        invlpg((void *)virtual_addr);
    }
}

void identitymap_2m(uint64_t physical_addr){
    mmu_map_2m(physical_addr,physical_addr);
}

static inline void load_cr3(uint64_t pml4_phys)
{
    
    __asm__ volatile (
        "cli\n\t"
        "mov %0, %%cr3"
        :
        : "r"(pml4_phys)
        : "memory"
    );
}

void set_mmu(){
    uint64_t pml4_phys = (uint64_t)(&PML4);
    load_cr3(pml4_phys);
    mmu_active = 1;
}

int get_mmu_active(){
    return mmu_active == 1;
}