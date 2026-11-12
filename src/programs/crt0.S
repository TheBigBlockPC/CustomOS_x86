.intel_syntax noprefix

.section .text
.global _start
.extern main

_start:
    xor ebp, ebp

    # System V ABI stack alignment
    and rsp, -16

    # Call the C application
    call main

    # Pass main's return value to SYS_EXIT
    mov edi, eax
    mov eax, 3
    syscall

    # SYS_EXIT should never return
    ud2