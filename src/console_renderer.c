#include "types.h"
//#include "font8x8_basic.h"
#include "font8x8_256.h"
#include "console_renderer.h"
#include "display_mgr.h"

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

static uint16_t stride;
static uint16_t width;
static uint16_t height;
static uint16_t offset_x;
static uint16_t offset_y;
static uint32_t *fb;
static bool redraw_all = true;

void set_fontsize(uint8_t size){
    fontsize = size;
}

static void fill_rect(uint32_t *fb_base, int x, int y, int w, int h, uint32_t color){
    int end_x = x + w;
    int end_y = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (end_x > width) end_x = width;
    if (end_y > height) end_y = height;
    for(int py=y;py<end_y;py++){
        for(int px=x;px<end_x;px++){
            fb_base[(uint64_t)py * stride + px] = color;
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

void cr_set_FB(uint32_t *fb_base, uint16_t row_stride, uint16_t h, uint16_t visible_width){
    stride = row_stride;
    width = visible_width < stride ? visible_width : stride;
    height = h;
    if(width < (16*TERMINAL_W) || height < (16*TERMINAL_H)){
        set_fontsize(1);
    }else{
        set_fontsize(2);
    }
    fb = fb_base;
    int terminal_width = TERMINAL_W * fontsize * 8;
    int terminal_height = TERMINAL_H * fontsize * 8;
    offset_x = width >= terminal_width ? (width - terminal_width) / 2 : 0;
    offset_y = height >= terminal_height ? (height - terminal_height) / 2 : 0;
    redraw_all = true;
}

void draw_cursor(int x, int y){
    int size = fontsize * 8;
    int left = x * size + offset_x;
    int top = y * size + offset_y;
    fill_rect(fb, left, top, size, 1, 0x00FFFFFF);
    fill_rect(fb, left, top + size - 1, size, 1, 0x00FFFFFF);
    fill_rect(fb, left, top, 1, size, 0x00FFFFFF);
    fill_rect(fb, left + size - 1, top, 1, size, 0x00FFFFFF);
}

static bool cursor_visible = false;
static terminal_character *viewport_ptr;
void set_draw_cursor(bool draw){
    cursor_visible = draw;
    if (!viewport_ptr) return;

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
    //present();
}

void draw_chars(terminal_character *viewport, int cursor_x, int cursor_y){
    viewport_ptr = viewport;
    for(int y=0;y<40;y++){
        for(int x=0;x<64;x++){
            terminal_character current = terminal_current[y*64+x];
            terminal_character tc = viewport[y*64+x];
            uint8_t color = tc.color;
            
            if(
                redraw_all ||
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
    redraw_all = false;
    //present();
}
