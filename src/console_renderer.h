#ifndef CONSOLE_RENDERER_H
#define CONSOLE_RENDERER_H
#include "types.h"
typedef struct {
    char character;
    uint8_t color;
} terminal_character;

void set_fontsize(uint8_t size);
void cr_set_FB(uint32_t *fb_base, uint16_t w, uint16_t h);
void draw_chars(terminal_character *viewport, int cursor_x, int cursor_y);
void set_draw_cursor(bool draw);
#endif