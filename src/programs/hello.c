#include "syscall.h"
int main(void) {
    sys_write("Hello from C!\n", 14);

    fb_t fb_desc;
    sys_gfx(&fb_desc);

    uint32_t* fb = fb_desc.fb;
    uint16_t width = fb_desc.w;
    uint16_t height = fb_desc.h;
    for(int y = 0;y<height;y++){
        for(int x = 0;x<width;x++){
            fb[x + y*width] = ((x << 16) & 0xFF0000) | ((y << 8) & 0xFF00);
        }
    }
    return 0;
}