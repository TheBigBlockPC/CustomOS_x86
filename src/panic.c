#include "types.h"
#include "console.h"
#include "panic.h"
#include "interrupts/interrupts.h"
#include "util.h"

void panic(char* message){
    __asm__ volatile("cli" ::: "memory");
    con_write("Kernel panic\n",0x01);
    con_write(message,0x07);
    while(1){
        __asm__ __volatile__("hlt");
    }
}

void exception_dump(exception_frame_t *f)
{
    if (!f)
        return;

    con_write("\n================ CPU EXCEPTION ================\n", 0x07);
    con_write("Exception: ", 0x07);
    print_uint64(f->vector);
    con_write("\n", 0x07);
    con_write("Error Code: ", 0x07);
    print_uint64_full(f->error_code);
    con_write("\n", 0x07);

    if (f->vector == 14) {
        con_write("Fault Address: ", 0x07);
        print_uint64_full(f->cr2);
        con_write("\n", 0x07);
    }

    con_write("\n---------------- CPU STATE ----------------\n", 0x07);
    con_write("RIP:    ", 0x07);
    print_uint64_full(f->rip);
    con_write("\n", 0x07);
    con_write("RSP:    ", 0x07);
    print_uint64_full(f->rsp);
    con_write("\n", 0x07);
    con_write("RBP:    ", 0x07);
    print_uint64_full(f->rbp);
    con_write("\n", 0x07);
    con_write("RFLAGS: ", 0x07);
    print_uint64_full(f->rflags);
    con_write("\n", 0x07);

    con_write("\n------------ GENERAL REGISTERS ------------\n", 0x07);
    con_write("RAX: ", 0x07);
    print_uint64_full(f->rax);
    con_write("  RBX: ", 0x07);
    print_uint64_full(f->rbx);
    con_write("\n", 0x07);
    con_write("RCX: ", 0x07);
    print_uint64_full(f->rcx);
    con_write("  RDX: ", 0x07);
    print_uint64_full(f->rdx);
    con_write("\n", 0x07);
    con_write("RSI: ", 0x07);
    print_uint64_full(f->rsi);
    con_write("  RDI: ", 0x07);
    print_uint64_full(f->rdi);
    con_write("\n", 0x07);
    con_write("R8:  ", 0x07);
    print_uint64_full(f->r8);
    con_write("  R9:  ", 0x07);
    print_uint64_full(f->r9);
    con_write("\n", 0x07);
    con_write("R10: ", 0x07);
    print_uint64_full(f->r10);
    con_write("  R11: ", 0x07);
    print_uint64_full(f->r11);
    con_write("\n", 0x07);
    con_write("R12: ", 0x07);
    print_uint64_full(f->r12);
    con_write("  R13: ", 0x07);
    print_uint64_full(f->r13);
    con_write("\n", 0x07);
    con_write("R14: ", 0x07);
    print_uint64_full(f->r14);
    con_write("  R15: ", 0x07);
    print_uint64_full(f->r15);
    con_write("\n", 0x07);

    con_write("\n------------- CONTROL REGISTERS -----------\n", 0x07);
    con_write("CR0: ", 0x07);
    print_uint64_full(f->cr0);
    con_write("\n", 0x07);
    con_write("CR2: ", 0x07);
    print_uint64_full(f->cr2);
    con_write("\n", 0x07);
    con_write("CR3: ", 0x07);
    print_uint64_full(f->cr3);
    con_write("\n", 0x07);
    con_write("CR4: ", 0x07);
    print_uint64_full(f->cr4);
    con_write("\n", 0x07);

    con_write("\n------------- SEGMENT REGISTERS -----------\n", 0x07);
    con_write("CS: ", 0x07);
    print_uint64_full(f->cs & 0xffff);
    con_write("  SS: ", 0x07);
    print_uint64_full(f->ss & 0xffff);
    con_write("\n", 0x07);
    con_write("DS: ", 0x07);
    print_uint64_full(f->ds & 0xffff);
    con_write("  ES: ", 0x07);
    print_uint64_full(f->es & 0xffff);
    con_write("\n", 0x07);
    con_write("FS: ", 0x07);
    print_uint64_full(f->fs & 0xffff);
    con_write("  GS: ", 0x07);
    print_uint64_full(f->gs & 0xffff);
    con_write("\n", 0x07);

    if (f->vector == 14) {
        uint64_t e = f->error_code;

        con_write("\n------------- PAGE FAULT INFO -------------\n", 0x07);
        con_write("Present:    ", 0x07);
        con_write((e & 1) ? "Yes" : "No", 0x07);
        con_write("\n", 0x07);
        con_write("Access:     ", 0x07);
        con_write((e & 2) ? "Write" : "Read", 0x07);
        con_write("\n", 0x07);
        con_write("Privilege:  ", 0x07);
        con_write((e & 4) ? "User" : "Supervisor", 0x07);
        con_write("\n", 0x07);
        con_write("Reserved:   ", 0x07);
        con_write((e & 8) ? "Violation" : "No", 0x07);
        con_write("\n", 0x07);
        con_write("Instruction Fetch: ", 0x07);
        con_write((e & 16) ? "Yes" : "No", 0x07);
        con_write("\n", 0x07);
        con_write("Protection Key:    ", 0x07);
        con_write((e & 32) ? "Violation" : "No", 0x07);
        con_write("\n", 0x07);
        con_write("Shadow Stack:      ", 0x07);
        con_write((e & 64) ? "Yes" : "No", 0x07);
        con_write("\n", 0x07);
    }

    con_write("\nFX State: ", 0x07);
    print_uint64_full((uint64_t)f->fx_state);
    con_write("\n", 0x07);
    con_write("===============================================\n", 0x07);
    panic("exception");
}

void setup_default_exceptionhanders(){
    for(int i=0;i<=32;i++){
        exceptions_set_handler(i,exception_dump);
    }
}