.text

.extern interrupts_dispatch


/*
 * Generate one tiny entry stub for every vector from 32 to 255.
 *
 * Every stub:
 *
 *     1. pushes its vector number
 *     2. jumps into the shared interrupt handler
 */

.altmacro

.macro MAKE_INTERRUPT_STUB number

.globl interrupt_stub_\number

interrupt_stub_\number:
    pushq $\number
    jmp interrupt_common

.endm


.set vector, 32

.rept 224
    MAKE_INTERRUPT_STUB %vector
    .set vector, vector + 1
.endr


/*
 * Common interrupt entry.
 *
 * On entry the CPU has already pushed:
 *
 *     RFLAGS
 *     CS
 *     RIP
 *
 * and the individual stub has pushed:
 *
 *     vector
 */
interrupt_common:

    /*
     * Save all general-purpose registers.
     */

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx

    pushq %rbp
    pushq %rsi
    pushq %rdi

    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15


    /*
     * Save where the register frame starts.
     *
     * r12 is already saved above, and is callee-saved.
     */
    movq %rsp, %r12


    /*
     * C code expects DF = 0.
     */
    cld


    /*
     * There are 15 saved registers:
     *
     *     15 * 8 = 120 bytes
     *
     * So the pushed vector number is at:
     *
     *     120(%r12)
     *
     * Microsoft x64 ABI:
     * first argument goes into RCX.
     */
    movq 120(%r12), %rcx


    /*
     * Align stack for the C call.
     */
    andq $-16, %rsp


    /*
     * Microsoft x64 ABI requires 32 bytes
     * of shadow space for every function call.
     */
    subq $32, %rsp


    call interrupts_dispatch


    /*
     * Restore our original interrupt stack.
     */
    movq %r12, %rsp


    /*
     * Restore registers in reverse order.
     */

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8

    popq %rdi
    popq %rsi
    popq %rbp

    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax


    /*
     * Remove the vector number pushed by
     * the individual stub.
     */
    addq $8, %rsp


    /*
     * Restore RIP, CS and RFLAGS and resume
     * the interrupted code.
     */
    iretq



/*
 * Array of pointers to all generated interrupt stubs.
 *
 * C sees this as:
 *
 * extern void (*interrupt_stub_table[224])(void);
 *
 * table[0]   -> interrupt_stub_32
 * table[1]   -> interrupt_stub_33
 * ...
 * table[223] -> interrupt_stub_255
 */

.data

.globl interrupt_stub_table
.p2align 3

interrupt_stub_table:


.macro MAKE_INTERRUPT_POINTER number
    .quad interrupt_stub_\number
.endm


.set vector, 32

.rept 224
    MAKE_INTERRUPT_POINTER %vector
    .set vector, vector + 1
.endr