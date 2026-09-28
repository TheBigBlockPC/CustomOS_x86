#include "types.h"
#include "console.h"
#include "input_subsystem.h"
#include "tty.h"
#include "paging.h"
#include "memory.h"
#include "interrupts/interrupts.h"
#include "gdt/gdt.h"
#include "acpi.h"
#include "pcie.h"
#include "util.h"
#include "drivers/sata.h"
#include "filesystem.h"

void on_input(char* input){
    con_write("Echo: ",0x07);
    con_write(input,0x01);
    con_write("\n> ",0x07);
    tty_allow_input();
}

void k_main(){
    con_write("kernel running\n",0x07);
    setup_hhdm();
    set_mmu();
    gdt_init();
    init_acpi();
    pci_init();
    interrupts_init();

    block_storage_init();
    fs_init();

    tty_init();
    tty_set_send_event(&on_input);
    tty_allow_input();
    tty_print("> ");
    while(1){
        poll_events();
    }
    
    return;
}
