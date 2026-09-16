#ifndef CONSOLE_RENDERER_H
#define CONSOLE_RENDERER_H
#include "types.h"
typedef struct {
    char character;
    uint8_t color;
} terminal_character;

void set_FB(uint32_t *fb_base, uint16_t w, uint16_t h);
void draw_chars(terminal_character *viewport);
#endif