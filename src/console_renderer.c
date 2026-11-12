#include "types.h"
//#include "font8x8_basic.h"
#include "font8x8_256.h"
#include "console_renderer.h"

#define TERMINAL_W 64
#define TERMINAL_H 40

static terminal_character terminal_current[TERMINAL_W*TERMINAL_H];

static uint8_t fontsize = 1;
static int prev_cursor_x;
static int prev_cursor_y;

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
static uint16_t offset_x;
static uint16_t offset_y;
static uint32_t *fb;

void set_fontsize(uint8_t size){
    fontsize = size;
}

static void fill_rect(uint32_t *fb_base, int x, int y, int w, int h, uint32_t color){
    int start = x + y * width;
    for(int py=0;py<h;py++){
        for(int px=0;px<w;px++){
            int offset = px + py * width;
            if(offset < 0 | offset > width * height) continue;
            fb_base[start+offset] = color;
        }
    }
}
static void draw_glyph(uint32_t *fb_base, int x, int y, char glyph, uint32_t fg_color, uint32_t bg_color){
    for(int py=0;py<8;py++){
        uint8_t row = 0x00;
        row = font8x8_256[(unsigned char)glyph][py];
        //if(glyph >= 0){
        //    row = font8x8_basic[glyph][py];
        //}else{
        //    row = font8x8_basic[128][py];
        //}
        for(int px=0;px<8;px++){
            if(row & (1 << px)){
                fill_rect(fb_base,x+px*fontsize,y+py*fontsize,fontsize,fontsize,fg_color);
            }else{
                fill_rect(fb_base,x+px*fontsize,y+py*fontsize,fontsize,fontsize,bg_color);
            }
        }
    }
}

void set_FB(uint32_t *fb_base, uint16_t w, uint16_t h){
    
    if(w < (16*64) || h < (16*40)){
        set_fontsize(1);
    }else{
        set_fontsize(2);
    }
    width = w;
    height = h;
    fb = fb_base;
    offset_x = (width - 64*(fontsize*8))/2;
    offset_y = (height - 40*(fontsize*8))/2;
}

void draw_cursor(int x, int y){
    int base = (x*(fontsize*8) + offset_x)+(y*(fontsize*8) + offset_y)*width;
    for(int i=0;i<(fontsize*8);i++){
        fb[base+i] = 0x00FFFFFF;
        fb[base+i+(fontsize*8-1)*width] = 0x00FFFFFF;
        fb[base+i*width] = 0x00FFFFFF;
        fb[base+i*width+(fontsize*8-1)] = 0x00FFFFFF;
    }
}

static bool cursor_visible = false;
static terminal_character *viewport_ptr;
void set_draw_cursor(bool draw){
    cursor_visible = draw;

    int x = prev_cursor_x;
    int y = prev_cursor_y;
    terminal_character tc = viewport_ptr[y*64+x];
    uint8_t color = tc.color;
    draw_glyph(fb,x*(fontsize*8)+offset_x, y*(fontsize*8)+offset_y,
        tc.character,
        colors[color & 0x0F],
        colors[(color >> 4) & 0x0F]
    );
    if(draw){
        draw_cursor(x,y);
    }
}

void draw_chars(terminal_character *viewport, int cursor_x, int cursor_y){
    viewport_ptr = viewport;
    for(int y=0;y<40;y++){
        for(int x=0;x<64;x++){
            terminal_character current = terminal_current[y*64+x];
            terminal_character tc = viewport[y*64+x];
            uint8_t color = tc.color;
            
            if(
                current.character != tc.character ||
                current.color != tc.color ||
                (cursor_x == x && cursor_y == y) ||
                (prev_cursor_x == x && prev_cursor_y == y)
            ){
                terminal_current[y*64+x] = tc;
                draw_glyph(fb,x*(fontsize*8)+offset_x, y*(fontsize*8)+offset_y,
                    tc.character,
                    colors[color & 0x0F],
                    colors[(color >> 4) & 0x0F]
                );
            }
            if(cursor_x == x && cursor_y == y){
                if(cursor_visible){
                    draw_cursor(x,y);
                }
            }
        }
    }

    prev_cursor_x = cursor_x;
    prev_cursor_y = cursor_y;
}