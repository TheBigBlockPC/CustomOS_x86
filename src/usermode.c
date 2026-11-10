#include "types.h"
#include "vmm.h"
#include "paging.h"
#include "util.h"
#include "usermode.h"
#define USER_CODE_VA  0x400000ULL
#define USER_STACK_VA 0x800000ULL

void* usr_allocate_kernel(uint64_t size,uint64_t usr_base){
    uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    void* buffer = vm_alloc(size);
    for(uint64_t i=0;i<n_pages;i++){
        uint64_t offset = i * PAGE_SIZE_4K;
        uint64_t phys = 0;
        uint64_t va = ((uint64_t)buffer) + offset;
        virt_to_phys(va,&phys);
        mmu_map_4k(usr_base+offset,phys,PAGE_RW | PAGE_USER);
    }
    return buffer;
    //mmu_map_4k();
}

#define USER_CS 0x1B
#define USER_DS 0x23

__attribute__((noreturn))
extern void enter_user(
    uint64_t user_rip,
    uint64_t user_rsp
);

void um_test(){
    uint8_t* user_code = (uint8_t*)usr_allocate_kernel(PAGE_SIZE_4K,USER_CODE_VA);
    uint8_t* user_stack = (uint8_t*)usr_allocate_kernel(PAGE_SIZE_4K,USER_STACK_VA);

    user_code[0] = 0x0F;
    user_code[1] = 0x0B;

    enter_user(
        USER_CODE_VA,
        USER_STACK_VA + PAGE_SIZE_4K
    );
}