#include "types.h"
#include "cpu.h"

#define KERNEL_STACK_SIZE (64 * 1024)

static _Alignas(16)
uint8_t kernel_stack[KERNEL_STACK_SIZE];


/*
 * Force this tiny trampoline to use the Microsoft x64 ABI:
 *
 * RCX = stack_top
 * RDX = entry
 *
 * "naked" prevents the compiler from creating a normal stack frame,
 * because we're about to replace RSP ourselves.
 */
__attribute__((ms_abi, naked, noreturn))
static void cpu_switch_stack(
    void *stack_top,
    kernel_entry_t entry)
{
    __asm__ volatile(
        /*
         * Replace the firmware-provided stack.
         */
        "mov %rcx, %rsp\n"

        /*
         * Make RSP 16-byte aligned before calling kernel_main().
         */
        "and $-16, %rsp\n"

        /*
         * Microsoft x64 ABI requires the caller to provide
         * 32 bytes of shadow space.
         */
        "sub $32, %rsp\n"

        /*
         tun the kernel
         */
        "call *%rdx\n"

        /*
         halt the CPU if the kernel returns
         */
        "cli\n"
        "1:\n"
        "hlt\n"
        "jmp 1b\n"
    );
}


__attribute__((noreturn))
void cpu_enter_kernel(kernel_entry_t entry)
{
    void *stack_top =
        kernel_stack + sizeof(kernel_stack);

    cpu_switch_stack(stack_top, entry);

    __builtin_unreachable();
}