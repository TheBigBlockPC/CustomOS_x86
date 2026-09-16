#include "types.h"
#include "console.h"
#include "input_subsystem.h"
#include "tty.h"

int k_main(){
    con_write("kernel running\n",0x07);
    tty_init();
    while(1){
        poll_events();
    }
    
    return 0;
}