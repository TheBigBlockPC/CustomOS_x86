.text
.extern interrupts_dispatch
.extern exceptions_dispatch
.altmacro

/* Normalize the frame: CPU error code or a synthetic zero, then vector. */
.macro MAKE_INTERRUPT_STUB number
.globl interrupt_stub_\number
interrupt_stub_\number:
    .if (\number != 8) && (\number != 10) && (\number != 11) && (\number != 12) && (\number != 13) && (\number != 14) && (\number != 17) && (\number != 21) && (\number != 29) && (\number != 30)
        pushq $0
    .endif
    pushq $\number
    jmp interrupt_common
.endm

.set vector, 0
.rept 256
    MAKE_INTERRUPT_STUB %vector
    .set vector, vector + 1
.endr

interrupt_common:
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

    movq %cr0, %rax
    pushq %rax
    movq %cr2, %rax
    pushq %rax
    movq %cr3, %rax
    pushq %rax
    movq %cr4, %rax
    pushq %rax
    xorq %rax, %rax
    movw %ds, %ax
    pushq %rax
    movw %es, %ax
    pushq %rax
    movw %fs, %ax
    pushq %rax
    movw %gs, %ax
    pushq %rax
    pushq $0                      /* fx_state pointer */

    /* r12/r13 are already saved and callee-saved in the Microsoft x64 ABI. */
    movq %rsp, %r12
    cld
    andq $-16, %rsp
    subq $512, %rsp
    movq %rsp, %r13
    /* A #NM handler must not recursively fault while saving FPU state. All
     * kernel C is built with -mgeneral-regs-only, so skipping is safe. */
    testq $12, 64(%r12)           /* CR0.EM | CR0.TS */
    jnz 1f
    testq $512, 40(%r12)          /* CR4.OSFXSR */
    jz 1f
    fxsave64 (%r13)
    movq %r13, (%r12)
1:
    subq $32, %rsp               /* ABI shadow space */
    cmpq $32, 192(%r12)
    jae 2f
    movq %r12, %rcx
    call exceptions_dispatch
    jmp 3f
2:
    movq 192(%r12), %rcx
    call interrupts_dispatch
3:
    cmpq $0, (%r12)
    je 4f
    fxrstor64 (%r13)
4:
    movq %r12, %rsp
    addq $72, %rsp               /* snapshots, not restored control regs */
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
    addq $16, %rsp               /* vector and error code */
    iretq                        /* RIP, CS, RFLAGS, RSP, SS in 64-bit mode */

.data
.globl interrupt_stub_table
.p2align 3
interrupt_stub_table:
.macro MAKE_INTERRUPT_POINTER number
    .quad interrupt_stub_\number
.endm
.set vector, 0
.rept 256
    MAKE_INTERRUPT_POINTER %vector
    .set vector, vector + 1
.endr
