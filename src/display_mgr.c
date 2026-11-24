#include "types.h"
#include "display_mgr.h"
#include "console_renderer.h"
#include "util.h"

#ifdef BUILD_KERNEL
#include "memory.h"
#include "vmm.h"
#endif

static uint16_t stride;
static uint16_t height;
static uint16_t visible_width;
static uint32_t *fb;
static uint32_t *back_fb;
static uint64_t fb_size;
static bool double_buffered = false;

void dp_set_FB(uint32_t *fb_base, uint16_t row_stride, uint16_t h, uint16_t w){
    stride = row_stride;
    height = h;
    visible_width = w;
    fb = fb_base;
    fb_size = (uint64_t)stride * height * sizeof(*fb);
    double_buffered = false;
    cr_set_FB(fb,stride,height,visible_width);
}

#ifdef BUILD_KERNEL
void setup_double_buffer(){
    back_fb = (uint32_t*)vm_alloc(fb_size,VM_NULL,VM_FLAGS_DEFAULT);
    memory_copy(back_fb,fb,fb_size);
    cr_set_FB(back_fb,stride,height,visible_width);
    double_buffered = true;
}
void move_fb(){
    fb = (uint32_t*)((uint64_t)fb + HHDM_BASE);
    cr_set_FB(fb,stride,height,visible_width);
}
#endif


void present(){
    if(double_buffered){
        framebuffer_copy(fb,back_fb,fb_size);
    }
}

fb_t get_fb(){
    fb_t fb_desc;
    fb_desc.w = stride;
    fb_desc.h = height;
    fb_desc.visible_width = visible_width;
    if(double_buffered){
        fb_desc.fb = &back_fb[0];
    }else{
        fb_desc.fb = &fb[0];
    }
    
    return fb_desc;
}
