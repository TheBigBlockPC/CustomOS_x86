#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "../types.h"

typedef void (*isr_t)(void);

/* FXSAVE64 format: x87/MMX and XMM0-15. AVX/XSAVE state is not enabled by
 * this kernel. NULL when CR0.EM/TS or CR4.OSFXSR prevents a safe snapshot. */
typedef struct {
    uint16_t fcw, fsw;
    uint8_t ftw, reserved;
    uint16_t fop;
    uint64_t fip, fdp;
    uint32_t mxcsr, mxcsr_mask;
    uint8_t st_mm[8][16];
    uint8_t xmm[16][16];
    uint8_t reserved_tail[96];
} exception_fx_state_t;

/* Exact stack layout in interrupts.S. GPRs and the return frame are restored
 * from this structure, so handlers may adjust RIP to recover deliberately.
 * Control/DS/ES/FS/GS values are snapshots only. CR2 is meaningful for #PF.
 * No SWAPGS/user-mode entry convention or IST stack is implemented yet. */
typedef struct {
    exception_fx_state_t* fx_state;
    uint64_t gs, fs, es, ds;
    uint64_t cr4, cr3, cr2, cr0;
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} exception_frame_t;

typedef void (*exception_handler_t)(exception_frame_t* frame);

void interrupts_init(void);
void interrupts_set_isr(uint8_t index, isr_t handler);
/* Vectors 0..31; NULL unregisters. Handlers execute with interrupts masked. */
void exceptions_set_handler(uint8_t vector, exception_handler_t handler);

#endif
