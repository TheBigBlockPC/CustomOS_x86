#include "types.h"
#include "console_renderer.h"
#include "console.h"

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
        cursor_y = TERMINAL_H-1;

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
static void update_renderer(){
    draw_chars(terminal,cursor_x,cursor_y);
}

void con_export_state(console_state_t* state)
{
    for (int i = 0; i < TERMINAL_W * TERMINAL_H; i++) state->cells[i] = terminal[i];
    state->cursor_x = cursor_x;
    state->cursor_y = cursor_y;
}

void con_import_state(const console_state_t* state)
{
    for (int i = 0; i < TERMINAL_W * TERMINAL_H; i++) terminal[i] = state->cells[i];
    cursor_x = state->cursor_x;
    cursor_y = state->cursor_y;
    update_renderer();
}
static void put(char c, uint8_t color){
    int index = cursor_x + cursor_y * TERMINAL_W;
    if(c == '\n'){
        cursor_x = 0;
        cursor_y++;
        autoscroll();
        return;
    }
    if(c == '\b'){
        if (cursor_x == 0 && cursor_y == 0) return;
        cursor_x--;
        fixup_cursor();
        autoscroll();
        return;
    }
    terminal[index] = (terminal_character){
        .character = c,
        .color = color
    };
    cursor_x++;
    fixup_cursor();
    autoscroll();
}

void con_clear(){
    for(int i=0;i<(TERMINAL_W*TERMINAL_H);i++){
        terminal[i] = (terminal_character){
            .character = 0,
            .color = 0
        };
    }
    update_renderer();
}

void con_write(char* string, uint8_t color){
    while(*string){
        put(*string,color);
        ++string;
    }
    update_renderer();
}

void con_delete_cell(){
    int index = cursor_x + cursor_y * TERMINAL_W;
    while(index < TERMINAL_W * TERMINAL_H - 1 && terminal[index].character != 0){
        terminal[index].character = terminal[index+1].character;
        index++;
    }
    if (index == TERMINAL_W * TERMINAL_H - 1) terminal[index].character = 0;
    update_renderer();
}
