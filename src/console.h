#ifndef CONSOLE_H
#define CONSOLE_H
#include "console_renderer.h"

#define TERMINAL_W 64
#define TERMINAL_H 40

typedef struct {
    terminal_character cells[TERMINAL_W * TERMINAL_H];
    int cursor_x, cursor_y;
} console_state_t;

void con_export_state(console_state_t* state);
void con_import_state(const console_state_t* state);
void con_clear();
void con_write(char* string, uint8_t color);
void con_delete_cell();
#endif
