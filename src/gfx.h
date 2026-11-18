#ifndef GFX_H
#include "common.h"
#define GFX_H

#include <stdint.h>

typedef struct {
    uint32_t *px;   /* 0xAARRGGBB */
    int w, h;
} FB;

void gfx_clear(FB *fb, uint32_t color);
void gfx_px(FB *fb, int x, int y, uint32_t color);
void gfx_blend_px(FB *fb, int x, int y, uint32_t color, uint8_t a);
void gfx_fill(FB *fb, int x, int y, int w, int h, uint32_t color);
void gfx_fill_a(FB *fb, int x, int y, int w, int h, uint32_t color, uint8_t a);
void gfx_rect(FB *fb, float x, float y, float w, float h, uint32_t color);
void gfx_rect_a(FB *fb, float x, float y, float w, float h, uint32_t color, uint8_t a);
void gfx_line(FB *fb, float x0, float y0, float x1, float y1, float thick, uint32_t color);
void gfx_tri(FB *fb, float x0, float y0, float x1, float y1,
             float x2, float y2, uint32_t color);
void gfx_disc(FB *fb, float cx, float cy, float r, uint32_t color);
void gfx_disc_a(FB *fb, float cx, float cy, float r, uint32_t color, uint8_t a);
void gfx_ring(FB *fb, float cx, float cy, float r, float thick, uint32_t color);
void gfx_arc(FB *fb, float cx, float cy, float r, float thick,
             float a0, float a1, uint32_t color);
void gfx_text(FB *fb, int x, int y, const char *s, int scale, uint32_t color);
void gfx_text_a(FB *fb, int x, int y, const char *s, int scale, uint32_t color, uint8_t a);
void gfx_text_center(FB *fb, int cx, int y, const char *s, int scale, uint32_t color);
void gfx_text_right(FB *fb, int xr, int y, const char *s, int scale, uint32_t color);

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
static inline uint32_t scale_color(uint32_t c, float f) {
    uint32_t r = (uint32_t)clampf((float)((c >> 16) & 255) * f, 0, 255);
    uint32_t g = (uint32_t)clampf((float)((c >> 8) & 255) * f, 0, 255);
    uint32_t b = (uint32_t)clampf((float)(c & 255) * f, 0, 255);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

#endif /* GFX_H */
