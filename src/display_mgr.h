#ifndef DISPLAY_MGR_H
#define DISPLAY_MGR_H

typedef struct {
    uint16_t w;
    uint16_t h;
    uint32_t* fb;
} fb_t;

void dp_set_FB(uint32_t *fb_base, uint16_t w, uint16_t h);

#ifdef BUILD_KERNEL
void setup_double_buffer();
#endif

void present();
fb_t get_fb();

#endif