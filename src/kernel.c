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
#include "hpet.h"
#include "syscall.h"
#include "tss.h"
#include "usermode.h"
#include "display_mgr.h"
#include "interrupts/apic.h"
#include "interrupts/lapic_timer.h"
#include "drivers/usb/usb.h"
enum Mode {
    BASE,
    FILESYSTEM
};

static uint64_t bin_folder = 0;

enum Mode k_sehll_state = BASE;

void parse_kernel_shell(char* input){
    if(str_start_comp(input,"fs")){
        k_sehll_state = FILESYSTEM;
        return;
    }

    if(str_start_comp(input,"C_PF")){
        char* INVALLID_ADDR = (char*)0xFFFFFFFFFF;
        *INVALLID_ADDR=0;
        return;
    }
    if(str_start_comp(input,"C_DZ")){
        int a = 0;
        int b = 0;
        int c = a/b;
        return;
    }
    if(str_start_comp(input,"EXEC")){
        um_test();
        return;
    }
    fs_exec(bin_folder,input);
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
    }else if(str_start_comp(input,"exec ")){
        fs_exec(fs_pos,(input+5));
    }else{
        con_write("FS: INVALLID COMMAND",0x01);
    }
}

static uint64_t auto_demo_timer = 0;
void on_input(char* input){
    auto_demo_timer = 999999999;
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

void on_timer(void){
    present();
}
void k_main(){
    con_write("kernel running\n",0x07);

    gdt_init();
    gdt_set_tss();
    tss_load();

    interrupts_init();

    setup_default_exceptionhanders();
    setup_hhdm();

    set_mmu();
    vmm_init();

    init_acpi();
    apic_init();
    pci_init();
    hpet_init();
    usb_init();

    if (!lapic_timer_start(0x34, 60, on_timer))
        panic("LAPIC timer failed");
    asm volatile("sti" ::: "memory");

    setup_double_buffer();

    block_storage_init();
    syscall_init();

    fs_init();
    fs_pos = fs_root();

    tty_init();
    tty_set_send_event(&on_input);
    tty_allow_input();

    uint64_t start = hpet_millis();
    present();
    uint64_t end = hpet_millis();
    con_write("Present call transfer: ",0x02);
    print_uint64_dec(end-start);
    con_write("ms\n",0x02);
    
    void* data = vm_alloc(256,VM_PAGE_ALIGNED);
    void* data2 = vm_alloc(65536,VM_PAGE_ALIGNED);
    vm_free(data);
    print_alloc();

    tty_print("> ");
    uint64_t next_cursor_update = hpet_millis()+250;
    bool cursor_visible = false;
    /*__asm__ volatile (
        "syscall"
    );*/
    auto_demo_timer = hpet_millis() + 3000;
    uint64_t fs_pos = fs_root();
    bin_folder = fs_cd(fs_pos,"BIN");
    while(1){
        poll_events();
        if(hpet_millis() > next_cursor_update){
            next_cursor_update = hpet_millis()+500;
            cursor_visible = !cursor_visible;
            set_draw_cursor(cursor_visible);
            //hpet_debug();
            //present();
        }

        if(hpet_millis() > auto_demo_timer){
            //
            //fs_exec(fs_pos,"C_HELLO");
        }
    }
    
    return;
}
