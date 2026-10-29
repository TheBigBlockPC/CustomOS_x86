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
#include "panic.h"
#include "vmm.h"

enum Mode {
    BASE,
    FILESYSTEM
};

enum Mode k_sehll_state = BASE;

void parse_kernel_shell(char* input){
    if(str_start_comp(input,"fs")){
        k_sehll_state = FILESYSTEM;
    }

    if(str_start_comp(input,"C_PF")){
        char* INVALLID_ADDR = (char*)0xFFFFFFFFFF;
        *INVALLID_ADDR=0;
    }
    if(str_start_comp(input,"C_DZ")){
        int a = 0;
        int b = 0;
        int c = a/b;
    }
}
uint64_t fs_pos = 0;
void parse_fs_shell(char* input){
    if(str_start_comp(input,"exit")){
        k_sehll_state = BASE;
    }else if(str_start_comp(input,"ls")){
        fs_ls(fs_pos);
    }else if(str_start_comp(input,"cd ")){
        uint64_t prev_fs_pos = fs_pos;
        fs_pos = fs_cd(fs_pos,(input+3));
        if(fs_pos == 0){
            con_write("FS: INVALLID PATH",0x01);
            fs_pos  = prev_fs_pos;
        }
    }else if(str_start_comp(input,"cat ")){
        fs_cat(fs_pos,(input+4));
    }else{
        con_write("FS: INVALLID COMMAND",0x01);
    }
}

void on_input(char* input){
    //con_write("Echo: ",0x07);
    //con_write(input,0x01);
    //con_write("\n",0x07);
    if(k_sehll_state == BASE){
        parse_kernel_shell(input);
    }
    else if(k_sehll_state == FILESYSTEM){
        parse_fs_shell(input);
    }

    if(k_sehll_state == BASE){
        con_write("\n> ",0x07);
    }
    if(k_sehll_state == FILESYSTEM){
        con_write("\nFS > ",0x07);
    }
    tty_allow_input();
}

void k_main(){
    con_write("kernel running\n",0x07);
    gdt_init();
    interrupts_init();
    setup_default_exceptionhanders();
    setup_hhdm();
    set_mmu();
    vmm_init();
    init_acpi();
    pci_init();

    block_storage_init();
    fs_init();
    fs_pos = fs_root();
    tty_init();
    tty_set_send_event(&on_input);
    tty_allow_input();
    tty_print("> ");
    while(1){
        poll_events();
    }
    
    return;
}
