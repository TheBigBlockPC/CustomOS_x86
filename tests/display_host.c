#include "console.h"
#include "display_mgr.h"
#include "util.h"
#include "vmm.h"

extern int printf(const char*, ...);
extern void exit(int) __attribute__((noreturn));
extern void* malloc(__SIZE_TYPE__);
extern void free(void*);

static int checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

#define UNTOUCHED 0x12345678U
#define GUARD_PIXELS 16

static uint64_t allocated_bytes, copied_bytes, presented_bytes;
static uint32_t* back_buffer;
static terminal_character viewport[TERMINAL_W * TERMINAL_H];

/* Substitute only allocation and memory copies; exercise the real renderer
 * and display manager, including the transition to double buffering. */
void* vm_alloc(uint64_t bytes) {
    allocated_bytes = bytes;
    back_buffer = malloc(bytes);
    CHECK(back_buffer != 0);
    return back_buffer;
}

void memory_copy(void* dst, const void* src, uint64_t bytes) {
    copied_bytes = bytes;
    for (uint64_t i = 0; i < bytes; i++)
        ((uint8_t*)dst)[i] = ((const uint8_t*)src)[i];
}

void framebuffer_copy(void* dst, const void* src, uint64_t bytes) {
    presented_bytes = bytes;
    memory_copy(dst, src, bytes);
}

static void fill_viewport(uint8_t color) {
    for (int i = 0; i < TERMINAL_W * TERMINAL_H; i++)
        viewport[i] = (terminal_character){' ', color};
}

static void verify_surface(const uint32_t* surface, int stride, int width,
                           int height, int cell_size, int left, int top,
                           uint32_t color, bool cursor) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < stride; x++) {
            uint32_t expected = UNTOUCHED;
            if (x < width && x >= left && x < left + TERMINAL_W * cell_size &&
                y >= top && y < top + TERMINAL_H * cell_size)
                expected = color;
            int cx = x - (left + (TERMINAL_W - 1) * cell_size);
            int cy = y - (top + (TERMINAL_H - 1) * cell_size);
            if (cursor && x < width && cx >= 0 && cx < cell_size &&
                cy >= 0 && cy < cell_size &&
                (cx == 0 || cy == 0 || cx == cell_size - 1 || cy == cell_size - 1))
                expected = 0x00FFFFFF;
            if (surface[(uint64_t)y * stride + x] != expected) {
                printf("FAIL %dx%d stride %d at (%d,%d): got %08x, expected %08x\n",
                       width, height, stride, x, y,
                       surface[(uint64_t)y * stride + x], expected);
                exit(1);
            }
        }
    }
    checks++;
}

static void check_mode(int width, int stride, int height, int cell_size,
                       int left, int top) {
    uint64_t pixels = (uint64_t)stride * height;
    uint64_t bytes = pixels * sizeof(uint32_t);
    uint32_t* storage = malloc(bytes + 2 * GUARD_PIXELS * sizeof(uint32_t));
    CHECK(storage != 0);
    for (uint64_t i = 0; i < pixels + 2 * GUARD_PIXELS; i++) storage[i] = UNTOUCHED;
    uint32_t* front = storage + GUARD_PIXELS;

    dp_set_FB(front, stride, height, width);
    fb_t descriptor = get_fb();
    CHECK(descriptor.fb == front && descriptor.w == stride &&
          descriptor.h == height && descriptor.visible_width == width);
    fill_viewport(0x17); /* Blank glyph with red background. */
    draw_chars(viewport, TERMINAL_W - 1, TERMINAL_H - 1);
    verify_surface(front, stride, width, height, cell_size, left, top, 0x00FF0000, false);

    /* Rebinding the same contents to a fresh framebuffer must repaint it. */
    for (uint64_t i = 0; i < pixels; i++) front[i] = UNTOUCHED;
    dp_set_FB(front, stride, height, width);
    draw_chars(viewport, TERMINAL_W - 1, TERMINAL_H - 1);
    verify_surface(front, stride, width, height, cell_size, left, top, 0x00FF0000, false);

    setup_double_buffer();
    CHECK(allocated_bytes == bytes && copied_bytes == bytes);
    CHECK(get_fb().fb == back_buffer);
    verify_surface(back_buffer, stride, width, height, cell_size, left, top, 0x00FF0000, false);
    fill_viewport(0x27); /* Green background in the back buffer only. */
    draw_chars(viewport, TERMINAL_W - 1, TERMINAL_H - 1);
    verify_surface(front, stride, width, height, cell_size, left, top, 0x00FF0000, false);
    verify_surface(back_buffer, stride, width, height, cell_size, left, top, 0x0000FF00, false);
    set_draw_cursor(true);
    verify_surface(back_buffer, stride, width, height, cell_size, left, top, 0x0000FF00, true);
    set_draw_cursor(false);
    verify_surface(back_buffer, stride, width, height, cell_size, left, top, 0x0000FF00, false);

    /* Include the last padding pixel: presentation must copy whole rows. */
    back_buffer[pixels - 1] = 0xABCDEF01;
    present();
    CHECK(presented_bytes == bytes);
    for (uint64_t i = 0; i < pixels; i++) {
        if (front[i] != back_buffer[i]) CHECK(front[i] == back_buffer[i]);
    }
    checks++;
    for (int i = 0; i < GUARD_PIXELS; i++) {
        CHECK(storage[i] == UNTOUCHED);
        CHECK(front[pixels + i] == UNTOUCHED);
    }

    /* A new framebuffer must leave double buffering until set up again. */
    dp_set_FB(front, stride, height, width);
    CHECK(get_fb().fb == front);
    free(back_buffer);
    free(storage);
}

int main(void) {
    set_draw_cursor(false); /* Safe before the first draw. */
    check_mode(1920, 1920, 1080, 16, 448, 220);
    check_mode(1920, 2048, 1080, 16, 448, 220);
    check_mode(800, 1024, 768, 8, 144, 224); /* Padding must not select 2x font. */
    check_mode(512, 1024, 640, 8, 0, 160);
    check_mode(512, 512, 320, 8, 0, 0);
    check_mode(1024, 1024, 640, 16, 0, 0);
    check_mode(1279, 1280, 719, 16, 127, 39); /* Odd margins round down. */
    /* Loader rejects undersized modes; the renderer still clips safely if
     * called directly, including a partially visible cursor. */
    check_mode(510, 512, 318, 8, 0, 0);
    check_mode(480, 512, 300, 8, 0, 0);
    printf("PASS: %d framebuffer layout/bounds checks across 9 modes\n", checks);
    return 0;
}
