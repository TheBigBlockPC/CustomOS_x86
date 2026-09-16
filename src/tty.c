#include "types.h"
#include "input_subsystem.h"
#include "console.h"
#include "tty.h"

#define TTY_MAX_INPUT 256

static char input_buffer[TTY_MAX_INPUT+1];
static int input_chars = 0;

static void key_event(uint8_t scancode, char ascii, int k_down){
    if(!k_down) return;
    if(ascii == '\b'){
        if(input_chars==0) return;
        input_chars--;
        con_write("\b",0x07);
        con_delete_cell();
        return;
    }
    if(input_chars>=TTY_MAX_INPUT) return;
    input_chars++;
    char str[] = {ascii,0x00};
    con_write((char*)(&str[0]),0x07);
}

void tty_init(){
    con_write("TTY init\n",0x07);
    set_kb_handler(&key_event);
    con_write("> ",0x07);
}