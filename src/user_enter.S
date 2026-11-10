.intel_syntax noprefix

.global enter_user

enter_user:
    push 0x23       # User SS
    push rsi        # User RSP
    push 0x02       # RFLAGS (interrupts disabled)
    push 0x1B       # User CS
    push rdi        # User RIP

    iretq