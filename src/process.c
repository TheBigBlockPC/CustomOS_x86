#include "types.h"
#include "interrupts/interrupts.h"
#include "paging.h"
#include "tss.h"
#include "syscall.h"
#include "util.h"
#include "panic.h"
#include "vmm.h"
#include "process.h"
#include "user_memory.h"

static bool reschedule_requested = false;
static bool sched_initialized = false;
static process_t* current_process = 0;
void scheduler_request(void)
{
    reschedule_requested = true;
}

bool scheduler_requested(void)
{
    return reschedule_requested;
}

static process_t* scheduler_get_next(void)
{
    if (!current_process)
        return 0;

    process_t* candidate = current_process->next;

    do {
        if (candidate->state == PROCESS_READY)
            return candidate;

        candidate = candidate->next;

    } while (candidate != current_process->next);

    return 0;
}

exception_frame_t* scheduler_switch(
    exception_frame_t* frame)
{
    if(!sched_initialized) return frame;
    if(!current_process) panic("");
    current_process->context = frame;
    if(current_process->state != PROCESS_TERMINATED) current_process->state = PROCESS_READY;

    process_t* next = scheduler_get_next();

    current_process = next;
    next->state = PROCESS_RUNNING;

    mmu_set_address_space(next->pml4);

    tss_get_pointer()->rsp0 =
        next->kernel_stack_top;

    syscall_set_stack(next->kernel_stack_top);
    return next->context;
}

static process_t kernel_process;
static process_t* process_list = 0;

static uint64_t next_pid = 1;

void process_init(void)
{
    //kernel_process = (process_t){0};
    memory_set(&kernel_process,sizeof(process_t),0);
    kernel_process.pid = 0;
    kernel_process.state = PROCESS_RUNNING;

    kernel_process.pml4 =
        mmu_get_default_address_space();

    kernel_process.kernel_stack = 0;

    kernel_process.kernel_stack_top =
        tss_get_pointer()->rsp0;

    kernel_process.context = 0;

    // Circular doubly linked list.
    kernel_process.next = &kernel_process;
    kernel_process.prev = &kernel_process;

    process_list = &kernel_process;
    current_process = &kernel_process;
    sched_initialized = true;
}

#define KERNEL_STACK_SIZE 0x4000

process_t* process_create(void)
{
    process_t* process = vm_alloc(
        sizeof(process_t), 0, 0
    );

    if (!process)
        return 0;

    memory_zero(process, sizeof(process_t));

    process->pid = next_pid++;
    process->state = PROCESS_CREATED;

    process->pml4 = mmu_create_address_space();

    process->kernel_stack = vm_alloc(
        KERNEL_STACK_SIZE,
        VM_PAGE_ALIGNED,
        VM_FLAGS_FULL_PAGES
    );

    process->kernel_stack_top =
        (uint64_t)process->kernel_stack +
        KERNEL_STACK_SIZE;

    um_init(process);
    return process;
}

void process_init_context(
    process_t* process,
    uint64_t entry,
    uint64_t user_stack_top)
{
    exception_frame_t* frame =
        (exception_frame_t*)(
            process->kernel_stack_top -
            sizeof(exception_frame_t)
        );

    memory_zero(frame, sizeof(exception_frame_t));

    frame->rip = entry;
    frame->rsp = user_stack_top;

    frame->cs = 0x1B;
    frame->ss = 0x23;

    frame->rflags = 0x202;

    process->context = frame;
}

void process_make_ready(process_t* process)
{
    process_t* last = kernel_process.prev;

    process->next = &kernel_process;
    process->prev = last;

    last->next = process;
    kernel_process.prev = process;

    process->state = PROCESS_READY;
}

process_t* process_get_current(){
    return current_process;
}

void* proc_allocate_kernel(uint64_t size,uint64_t usr_base,uint64_t flags){
    uint64_t n_pages = int_div_round_up(size,PAGE_SIZE_4K);
    void* buffer = vm_alloc(size,VM_PAGE_ALIGNED,VM_FLAGS_FULL_PAGES);
    for(uint64_t i=0;i<n_pages;i++){
        uint64_t offset = i * PAGE_SIZE_4K;
        uint64_t phys = 0;
        uint64_t va = ((uint64_t)buffer) + offset;
        virt_to_phys(va,&phys);
        mmu_map_4k(usr_base+offset,phys,flags);
    }
    return buffer;
}

void process_create_user_stack(process_t* process){
    process->user_stack_kernel = proc_allocate_kernel(process->user_stack_size,process->user_stack_base,PAGE_RW | PAGE_USER | PAGE_NX);
}

void process_destroy(process_t* process){
    if(process->prev){
        process->prev->next = process->next;
        process->next->prev = process->prev;
    }
    if(process->kernel_stack){
        vm_free(process->kernel_stack);
    }
    if(process->user_stack_kernel){
        //unmap_range(process->user_stack_base,process->user_stack_size);
        vm_free(process->user_stack_kernel);
    }
    um_heap_dealloc(process);
    dealloc_segments(process);
    list_destroy(&process->segment_allocations);
    if(process->pml4){
        mmu_destroy_adress_spece(process->pml4);
    }
    list_destroy(&process->segment_allocations);
    vm_free(process);
}