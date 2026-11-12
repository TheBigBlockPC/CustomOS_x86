#include "types.h"
#include "vmm.h"
#include "paging.h"
#include "util.h"
#include "usermode.h"
#include "console.h"
#define USER_CODE_VA  0x400000ULL
#define USER_STACK_VA 0x3200000ULL

typedef struct {
    uint64_t user_va;
    void* kernel_va;
    uint64_t size;
} um_alloc_t;

static um_alloc_t allocations[256];
static int alloc_index = 0;
void* usr_allocate_kernel(uint64_t size,uint64_t usr_base,uint64_t flags){
    uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    void* buffer = vm_alloc(size);
    for(uint64_t i=0;i<n_pages;i++){
        uint64_t offset = i * PAGE_SIZE_4K;
        uint64_t phys = 0;
        uint64_t va = ((uint64_t)buffer) + offset;
        virt_to_phys(va,&phys);
        mmu_map_4k(usr_base+offset,phys,flags);
    }
    return buffer;
}
void unmap_range(uint64_t base, uint64_t n_bytes){
    uint64_t n_pages = int_div_round_up(n_bytes,PAGE_SIZE_4K);
    for(uint64_t i=0;i<n_pages;i++){
        mmu_unmap_4k(base+i*PAGE_SIZE_4K);
    }
}

void um_create_segment(uint64_t user_base, void* data, uint64_t size, uint64_t flags, uint64_t padding){
    void* memory = usr_allocate_kernel(size+padding,user_base,flags);
    memory_copy(memory+padding,data,size);

    allocations[alloc_index].kernel_va = memory;
    allocations[alloc_index].user_va = user_base;
    allocations[alloc_index].size = size;
    alloc_index++;
}

void um_dealloc(){
    for(int i=0;i<alloc_index;i++){
        vm_free(allocations[i].kernel_va);
        unmap_range(allocations[i].user_va,allocations[i].size);
    }
    alloc_index = 0;
}

#define USER_CS 0x1B
#define USER_DS 0x23

extern void enter_user(
    uint64_t user_rip,
    uint64_t user_rsp
);

extern void resume_kernel();

extern const uint8_t user_program_start[];
extern const uint8_t user_program_end[];
uint64_t kernel_saved_rsp = 0;

void um_eexecute(uint64_t entry){
    uint8_t* user_stack = (uint8_t*)usr_allocate_kernel(PAGE_SIZE_4K,USER_STACK_VA,PAGE_RW | PAGE_USER | PAGE_NX);
    enter_user(
        entry,
        USER_STACK_VA + PAGE_SIZE_4K
    );
    unmap_range(USER_STACK_VA,PAGE_SIZE_4K);
    vm_free(user_stack);
    um_dealloc();
}

void um_test(){
    uint64_t code_size =
        (uint64_t)user_program_end -
        (uint64_t)user_program_start;

    um_create_segment(USER_CODE_VA,user_program_start,code_size, PAGE_USER,0);
    con_write("running user code\n",0x03);
    um_eexecute(USER_CODE_VA);
}