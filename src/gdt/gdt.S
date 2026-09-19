.text

.globl gdt_reload_segments

gdt_reload_segments:

    /*
     * Reload the ordinary data segment registers.
     */
    mov $0x10, %ax

    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /*
     * Don't depend on old firmware FS/GS selectors.
     */
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs

    /*
     * Reload CS.
     *
     * CS cannot be changed with:
     *
     *     mov ..., %cs
     *
     * so manufacture a far-return frame:
     *
     *     new CS
     *     new RIP
     *
     * and use lretq.
     */
    pushq $0x08

    leaq 1f(%rip), %rax
    pushq %rax

    lretq

1:
    ret