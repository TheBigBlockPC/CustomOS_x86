#include "types.h"
#include "console.h"
#include "input_subsystem.h"
#include "tty.h"
#include "paging.h"
#include "memory.h"
#include "interrupts/interrupts.h"
#include "gdt/gdt.h"
void on_input(char* input){
    con_write("Echo: ",0x07);
    con_write(input,0x01);
    con_write("\n> ",0x07);
    tty_allow_input();
}

void test_interrupt(){
    con_write("hello fron interrupt 1\n",0x03);
    
}
void test_interrupt2(){
    con_write("hello fron interrupt 2\n",0x03);
    
}
void k_main(){
    setup_hhdm();
    set_mmu();
    gdt_init();
    interrupts_init();
    interrupts_set_isr(0x21,test_interrupt2);
    con_write("kernel running\n",0x07);
    tty_init();
    tty_set_send_event(&on_input);
    tty_allow_input();
    tty_print("> ");
    while(1){
        poll_events();
    }
    
    return;
}