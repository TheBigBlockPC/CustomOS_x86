#include "types.h"
#include "font8x8_basic.h"
#include "console_renderer.h"

#define TERMINAL_W 64
#define TERMINAL_H 40

static terminal_character terminal_current[TERMINAL_W*TERMINAL_H];

static uint32_t colors[16] = {
    0x000000,
    0xFF0000,
    0x00FF00,
    0xFFFF00,
    0x0000FF,
    0xFF00FF,
    0x00FFFF,
    0xFFFFFF,
    0x000000,
    0x7F0000,
    0x007F00,
    0x7F7F00,
    0x00007F,
    0x7F007F,
    0x007F7F,
    0x7F7F7F
};

static uint16_t width;
static uint16_t height;
static uint32_t *fb;
static void fill_rect(uint32_t *fb_base, int x, int y, int w, int h, uint32_t color){
    int start = x + y * width;
    for(int py=0;py<h;py++){
        for(int px=0;px<w;px++){
            int offset = px + py * width;
            fb_base[start+offset] = color;
        }
    }
}
static void draw_glyph(uint32_t *fb_base, int x, int y, char glyph, uint32_t fg_color, uint32_t bg_color){
    for(int py=0;py<8;py++){
        uint8_t row = 0x00;
        if(glyph >= 0){
            row = font8x8_basic[glyph][py];
        }else{
            row = font8x8_basic[128][py];
        }
        for(int px=0;px<8;px++){
            if(row & (1 << px)){
                fill_rect(fb_base,x+px*2,y+py*2,2,2,fg_color);
            }else{
                fill_rect(fb_base,x+px*2,y+py*2,2,2,bg_color);
            }
        }
    }
}

void set_FB(uint32_t *fb_base, uint16_t w, uint16_t h){
    width = w;
    height = h;
    fb = fb_base;
}

void draw_chars(terminal_character *viewport){
    int offset_x = (width - 64*16)/2;
    int offset_y = (height - 40*16)/2;
    
    for(int y=0;y<40;y++){
        for(int x=0;x<64;x++){
            terminal_character current = terminal_current[y*64+x];
            terminal_character tc = viewport[y*64+x];
            uint8_t color = tc.color;
            
            if(
                current.character != tc.character ||
                current.color != tc.color
            ){
                terminal_current[y*64+x] = tc;
                draw_glyph(fb,x*16+offset_x, y*16+offset_y,
                    tc.character,
                    colors[color & 0x0F],
                    colors[(color >> 4) & 0x0F]
                );
            }        
        }
    }
}