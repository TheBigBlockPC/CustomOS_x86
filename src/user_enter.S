.intel_syntax noprefix

.global enter_user

enter_user:
    # Microsoft x64 calling convention
    # RCX = User RIP
    # RDX = User RSP

    mov ax, 0x23
    mov ds, ax
    mov es, ax

    push 0x23       # User SS
    push rdx        # User RSP
    push 0x02       # RFLAGS
    push 0x1B       # User CS
    push rcx        # User RIP

    iretq