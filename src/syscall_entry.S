.intel_syntax noprefix

.text

.global syscall_entry

.extern syscall_kernel_rsp
.extern syscall_user_rsp
.extern syscall_dispatch
.extern resume_kernel
syscall_exit:
    # RDI = exit code
    mov rax, rdi

    # Transfer to the other assembly file.
    jmp resume_kernel

syscall_entry:
    # =========================================
    # CPU is now in Ring 0.
    # RSP still points to the USER stack!
    # =========================================

    # Save user RSP without touching user memory.
    mov qword ptr [rip + syscall_user_rsp], rsp

    # Switch to kernel syscall stack.
    mov rsp, qword ptr [rip + syscall_kernel_rsp]
    
    # Check SYS_EXIT
    cmp rax, 3
    je syscall_exit
    
    # =========================================
    # Construct IRETQ return frame
    # =========================================

    push 0x23       # User SS
    push qword ptr [rip + syscall_user_rsp]
    push r11        # User RFLAGS
    push 0x1B       # User CS
    push rcx        # User RIP

    # =========================================
    # Preserve syscall registers
    # =========================================

    push rdx
    push r8
    push r9
    push r10

    # =========================================
    # Translate syscall ABI to Microsoft x64
    # =========================================

    # syscall_dispatch(number, arg0, arg1)
    #
    # RAX -> RCX = syscall number
    # RDI -> RDX = argument 1
    # RSI -> R8  = argument 2

    mov rcx, rax
    mov rdx, rdi
    mov r8, rsi

    # =========================================
    # Call C dispatcher
    # =========================================

    # 32 bytes shadow space
    # + 8 bytes for stack alignment
    sub rsp, 40

    call syscall_dispatch

    add rsp, 40

    # RAX now contains the syscall return value.

    # =========================================
    # Restore registers
    # =========================================

    pop r10
    pop r9
    pop r8
    pop rdx

    # =========================================
    # Return to Ring 3
    # =========================================

    iretq