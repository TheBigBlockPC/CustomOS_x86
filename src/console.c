#include "types.h"
#include "console_renderer.h"
#include "console.h"

#define TERMINAL_W 64
#define TERMINAL_H 40

static terminal_character terminal[TERMINAL_W*TERMINAL_H];
static int cursor_x;
static int cursor_y;

static void fixup_cursor(){
    int cursor_y_offset = cursor_x/TERMINAL_W;
    if(cursor_x < 0){
        cursor_y_offset--;
    }
    cursor_x = cursor_x + cursor_y_offset * -TERMINAL_W;
    cursor_y += cursor_y_offset;
}

static void autoscroll(){
    
    if(cursor_y > TERMINAL_H-1){
        int scroll = cursor_y - (TERMINAL_H-1);
        cursor_y = TERMINAL_H-2;

        int offset = TERMINAL_W * scroll;
        int n_elements = (TERMINAL_W*TERMINAL_H);// - offset;
        for(int i = 0; i<(n_elements-offset); i++){
            terminal[i] = terminal[i+offset];
        }
        for(int i = n_elements-offset; i<n_elements; i++){
            terminal[i] = (terminal_character){0,0};
        }
    }
}

static void put(char c, uint8_t color){
    int index = cursor_x + cursor_y * TERMINAL_W;
    if(c == '\n'){
        cursor_x = 0;
        cursor_y++;
        autoscroll();
        draw_chars(terminal);
        return;
    }
    terminal[index] = (terminal_character){
        .character = c,
        .color = color
    };
    cursor_x++;
    fixup_cursor();
    autoscroll();
    draw_chars(terminal);
}

void con_clear(){
    for(int i=0;i<(TERMINAL_W*TERMINAL_H);i++){
        terminal[i] = (terminal_character){
            .character = 0,
            .color = 0
        };
    }
    draw_chars(terminal);
}

void con_write(char* string, uint8_t color){
    while(*string){
        put(*string,color);
        ++string;
    }
}