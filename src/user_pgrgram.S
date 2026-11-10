.intel_syntax noprefix

.section .text

.global user_program_start
.global user_program_end

user_program_start:

    # ====================================
    # Check initial CPL
    # ====================================

    mov ax, cs
    and eax, 3
    mov r12, rax

    # ====================================
    # SYS_WRITE
    # ====================================

    mov rax, 1

    # RDI = Pointer to message
    lea rdi, [rip + message]

    # RSI = Length of message
    lea rsi, [rip + message_end]
    sub rsi, rdi

    syscall

    # Save SYS_WRITE return value.
    mov r13, rax

    # ====================================
    # SYS_ADD
    # ====================================

    mov rax, 2

    mov rdi, 100
    mov rsi, 42

    syscall

    # Save addition result.
    mov r15, rax

    # ====================================
    # Check CPL after syscalls
    # ====================================

    mov ax, cs
    and eax, 3
    mov r14, rax

    # ====================================
    # Breakpoint for debugging
    # ====================================

    int3

hang:
    jmp hang

message:
    .ascii "Hello from Ring 3!\n"
message_end:

user_program_end: