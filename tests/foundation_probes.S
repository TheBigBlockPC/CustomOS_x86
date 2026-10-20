.text
.globl test_register_frame
test_register_frame:
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $16, %rsp
    movdqu %xmm0, (%rsp)
    pcmpeqb %xmm0, %xmm0
    movq $1, %rax
    movq $2, %rbx
    movq $3, %rcx
    movq $4, %rdx
    movq $5, %rbp
    movq $6, %rsi
    movq $7, %rdi
    movq $8, %r8
    movq $9, %r9
    movq $10, %r10
    movq $11, %r11
    movq $12, %r12
    movq $13, %r13
    movq $14, %r14
    movq $15, %r15
    stc
    std
    int3
    pushfq
    cld
    cmpq $101, %rax
    jne 1f
    cmpq $2, %rbx
    jne 1f
    cmpq $3, %rcx
    jne 1f
    cmpq $4, %rdx
    jne 1f
    cmpq $5, %rbp
    jne 1f
    cmpq $6, %rsi
    jne 1f
    cmpq $7, %rdi
    jne 1f
    cmpq $8, %r8
    jne 1f
    cmpq $9, %r9
    jne 1f
    cmpq $10, %r10
    jne 1f
    cmpq $11, %r11
    jne 1f
    cmpq $12, %r12
    jne 1f
    cmpq $13, %r13
    jne 1f
    cmpq $14, %r14
    jne 1f
    cmpq $15, %r15
    jne 1f
    movq (%rsp), %rax
    andq $0x401, %rax
    cmpq $0x401, %rax
    jne 1f
    movd %xmm0, %eax
    cmpl $0xffffffab, %eax
    jne 1f
    movl $1, %eax
    jmp 2f
1:
    xorl %eax, %eax
2:
    addq $8, %rsp
    movdqu (%rsp), %xmm0
    addq $16, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    ret

.globl test_ud, test_ud_resume
test_ud:
    ud2
test_ud_resume:
    ret

.globl test_gp, test_gp_resume
test_gp:
    movw $0xfff8, %ax
    movw %ax, %ds
test_gp_resume:
    ret

.globl test_write, test_write_resume
test_write:
    movb $1, (%rcx)
test_write_resume:
    ret

.globl test_execute, test_execute_resume
test_execute:
    jmp *%rcx
test_execute_resume:
    ret
