#include "types.h"
#include "display_mgr.h"
#include "console_renderer.h"
#include "util.h"

#ifdef BUILD_KERNEL
#include "vmm.h"
#endif

static uint16_t width;
static uint16_t height;
static uint32_t *fb;
static uint32_t *back_fb;
static uint32_t fb_size;
static bool double_buffered = false;

void dp_set_FB(uint32_t *fb_base, uint16_t w, uint16_t h){
    width = w;
    height = h;
    fb = fb_base;
    fb_size = w*h*4;
    cr_set_FB(fb,w,h);
}

#ifdef BUILD_KERNEL
void setup_double_buffer(){
    back_fb = (uint32_t*)vm_alloc(fb_size);
    memory_copy(back_fb,fb,fb_size);
    cr_set_FB(back_fb,width,height);
    double_buffered = true;
}
#endif

void present(){
    if(double_buffered){
        framebuffer_copy(fb,back_fb,fb_size);
    }
}

fb_t get_fb(){
    fb_t fb_desc;
    fb_desc.w = width;
    fb_desc.h = height;
    if(double_buffered){
        fb_desc.fb = &back_fb[0];
    }else{
        fb_desc.fb = &fb[0];
    }
    
    return fb_desc;
}