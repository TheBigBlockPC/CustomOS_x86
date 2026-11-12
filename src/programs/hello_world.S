.intel_syntax noprefix
.global _start
.text

_start:
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
    # exit the program
    # ====================================

    mov rax, 3
    syscall


message:
    .ascii "hello from ELF file\n"
message_end:
