#ifndef DISPLAY_MGR_H
#define DISPLAY_MGR_H

#include "types.h"

typedef struct {
    uint16_t w; /* Row stride in pixels, including padding. */
    uint16_t h;
    uint32_t* fb;
    uint16_t visible_width; /* Visible pixels per row, excluding padding. */
} fb_t;

void dp_set_FB(uint32_t *fb_base, uint16_t row_stride, uint16_t h, uint16_t w);

#ifdef BUILD_KERNEL
void setup_double_buffer();
void move_fb();
#endif

void present();
fb_t get_fb();

#endif
