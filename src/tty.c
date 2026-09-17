#include "types.h"
#include "input_subsystem.h"
#include "console.h"
#include "tty.h"

#define TTY_MAX_INPUT 256

static char input_buffer[TTY_MAX_INPUT+1];
static int input_chars = 0;

static char allow_typing = 0;

static send_event on_send;

static void key_event(uint8_t scancode, char ascii, int k_down){
    if(!allow_typing) return;
    if(!k_down) return;
    if(ascii == '\n'){
        con_write("\n",0x07);
        allow_typing = 0;
        on_send((char*)(&input_buffer[0]));
        input_chars=0;
        for(int i = 0;i<TTY_MAX_INPUT;i++){
            input_buffer[i] = 0;
        }
        return;
    }
    if(ascii == '\b'){
        if(input_chars==0) return;
        input_chars--;
        input_buffer[input_chars] = 0;
        con_write("\b",0x07);
        con_delete_cell();
        return;
    }
    if(input_chars>=TTY_MAX_INPUT) return;
    input_buffer[input_chars] = ascii;
    input_chars++;
    char str[] = {ascii,0x00};
    con_write((char*)(&str[0]),0x07);
}

void tty_set_send_event(send_event on_send_callback){
    on_send = on_send_callback;
}

void tty_init(){
    con_write("TTY init\n",0x07);
    set_kb_handler(&key_event);
}

void tty_allow_input(){
    allow_typing = 1;
}

void tty_print(char* string){
    con_write(string,0x07);
}

void tty_print_err(char* string){
    con_write(string,0x01);
}