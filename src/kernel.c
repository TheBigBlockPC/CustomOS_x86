#include "types.h"
#include "console.h"
#include "input_subsystem.h"

static void key_event(uint8_t scancode, char ascii, int k_down){
    if(!k_down) return;
    char str[] = {ascii,0x00};
    con_write((char*)(&str[0]),0x07);
}
int k_main(){
    con_write("kernel running\n",0x07);
    for(int i=0;i<256;i++){
        char str[] = {i,0x00};
        con_write((char*)(&str[0]),0x07);
    }
    set_kb_handler(&key_event);
    while(1){
        poll_events();
    }
    
    return 0;
}