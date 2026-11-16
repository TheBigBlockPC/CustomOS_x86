.intel_syntax noprefix

.global enter_user
.global resume_kernel

.extern kernel_saved_rsp

enter_user:
    # SYS_EXIT bypasses IRETQ, so preserve the kernel caller's flags explicitly.
    pushfq
    cli

    # Save Microsoft x64 nonvolatile registers
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15

    # Save the original kernel RSP
    mov [rip + kernel_saved_rsp], rsp

    # RCX = user RIP
    # RDX = user RSP

    push 0x23
    push rdx
    push 0x202
    push 0x1B
    push rcx

    iretq


# Called from SYS_EXIT, already in Ring 0
# RAX = exit code
resume_kernel:
    # Restore the original kernel stack
    mov rsp, [rip + kernel_saved_rsp]
    mov qword ptr [rip + kernel_saved_rsp], 0

    # Restore original kernel registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx

    # Restore IF only after the original stack and registers are usable again.
    popfq

    # Return to the C caller of enter_user
    ret
