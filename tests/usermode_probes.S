.intel_syntax noprefix
.text
.global exit_probe_start
.global exit_probe_end

# Copied into a user page by usermode_kernel.c; no kernel references.
exit_probe_start:
    mov eax, 2
    mov edi, 100
    mov esi, 42
    syscall
    cmp rax, 142
    jne failed

    mov ebx, 0x500000
    cmp qword ptr [rbx], 0
    je done
wait_tick:
    pause
    cmp qword ptr [rbx + 8], 0
    je wait_tick
done:
    mov edi, 0x1234
    mov eax, 3
    syscall
failed:
    mov edi, 0xdead
    mov eax, 3
    syscall
    ud2
exit_probe_end:
