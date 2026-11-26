#ifndef PROCESS_H
#define PROCESS_H
#include "paging.h"
#include "interrupts/interrupts.h"
#include "dynamic_list.h"

#define SCHEDULER_VECTOR 0x81

typedef enum {
    PROCESS_CREATED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;

typedef struct process {
    // Identification
    uint64_t pid;
    process_state_t state;

    // Address space
    page_table_t* pml4;

    // Kernel stack
    void* kernel_stack;
    uint64_t kernel_stack_top;
    uint64_t saved_rsp;

    // User memory
    uint64_t user_stack_base;
    uint64_t user_stack_size;
    void* user_stack_kernel;
    
    dynamic_list_t segment_allocations;
    dynamic_list_t heap_allocations;
    uint64_t heap_base;

    // Termination
    int64_t exit_code;
    exception_frame_t* context;

    // Process list
    struct process* next;
    struct process* prev;

} process_t;

void process_init(void);
exception_frame_t* scheduler_switch(
    exception_frame_t* frame);
bool scheduler_requested(void);
void scheduler_request(void);
void process_make_ready(process_t* process);
void process_init_context(
    process_t* process,
    uint64_t entry,
    uint64_t user_stack_top);
process_t* process_create(void);
process_t* process_get_current();
void process_create_user_stack(process_t* process);
void process_destroy(process_t* process);
void* proc_allocate_kernel(uint64_t size,uint64_t usr_base,uint64_t flags);
#endif