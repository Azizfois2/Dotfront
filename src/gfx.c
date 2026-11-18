#include "gfx.h"
#include "font.h"
#include "common.h"
#include <string.h>
#include <stdlib.h>

void gfx_clear(FB *fb, uint32_t color) {
    int n = fb->w * fb->h;
    for (int i = 0; i < n; i++) fb->px[i] = color;
}

void gfx_px(FB *fb, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    fb->px[y * fb->w + x] = color;
}

void gfx_blend_px(FB *fb, int x, int y, uint32_t color, uint8_t a) {
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h || a == 0) return;
    uint32_t *d = &fb->px[y * fb->w + x];
    if (a == 255) { *d = color; return; }
    uint32_t sa = 255u - a;
    uint32_t rb = (((color >> 16) & 0xFF) * a + ((*d >> 16) & 0xFF) * sa) / 255u;
    uint32_t gb = (((color >> 8) & 0xFF) * a + ((*d >> 8) & 0xFF) * sa) / 255u;
    uint32_t bb = ((color & 0xFF) * a + (*d & 0xFF) * sa) / 255u;
    *d = 0xFF000000u | (rb << 16) | (gb << 8) | bb;
}

void gfx_fill(FB *fb, int x, int y, int w, int h, uint32_t color) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > fb->w ? fb->w : x + w;
    int y1 = y + h > fb->h ? fb->h : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &fb->px[yy * fb->w];
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

void gfx_fill_a(FB *fb, int x, int y, int w, int h, uint32_t color, uint8_t a) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > fb->w ? fb->w : x + w;
    int y1 = y + h > fb->h ? fb->h : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) gfx_blend_px(fb, xx, yy, color, a);
}

void gfx_rect(FB *fb, float x, float y, float w, float h, uint32_t color) {
    gfx_line(fb, x, y, x + w, y, 1.0f, color);
    gfx_line(fb, x + w, y, x + w, y + h, 1.0f, color);
    gfx_line(fb, x + w, y + h, x, y + h, 1.0f, color);
    gfx_line(fb, x, y + h, x, y, 1.0f, color);
}

void gfx_rect_a(FB *fb, float x, float y, float w, float h, uint32_t color, uint8_t a) {
    int x0 = (int)x, y0 = (int)y, x1 = (int)(x + w), y1 = (int)(y + h);
    for (int i = x0; i < x1; i++) { gfx_blend_px(fb, i, y0, color, a); gfx_blend_px(fb, i, y1, color, a); }
    for (int i = y0; i < y1; i++) { gfx_blend_px(fb, x0, i, color, a); gfx_blend_px(fb, x1, i, color, a); }
}

/* Thick line: Bresenham spine + small discs at each step. */
void gfx_line(FB *fb, float x0, float y0, float x1, float y1, float thick, uint32_t color) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    int steps = (int)(len / (thick * 0.5f + 0.5f));
    if (steps < 1) steps = 1;
    if (steps > 20000) steps = 20000;
    float r = thick * 0.5f;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float px = x0 + dx * t, py = y0 + dy * t;
        if (r <= 0.6f) gfx_px(fb, (int)px, (int)py, color);
        else gfx_disc(fb, px, py, r, color);
    }
}

/* Filled triangle (barycentric, no AA). */
void gfx_tri(FB *fb, float ax, float ay, float bx, float by,
             float cx, float cy, uint32_t color) {
    int minx = (int)floorf(fminf(ax, fminf(bx, cx)));
    int maxx = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
    int miny = (int)floorf(fminf(ay, fminf(by, cy)));
    int maxy = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= fb->w) maxx = fb->w - 1;
    if (maxy >= fb->h) maxy = fb->h - 1;
    float d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (fabsf(d) < 1e-6f) return;
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / d;
            float w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / d;
            float w2 = 1.0f - w0 - w1;
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                fb->px[y * fb->w + x] = color;
            }
        }
    }
}

/* Anti-aliased filled circle. */
void gfx_disc(FB *fb, float cx, float cy, float r, uint32_t color) {
    if (r <= 0.0f) return;
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = clampf(r + 0.5f - d, 0.0f, 1.0f);
            if (cov > 0.0f) gfx_blend_px(fb, x, y, color, (uint8_t)(cov * 255.0f));
        }
    }
}

void gfx_ring(FB *fb, float cx, float cy, float r, float thick, uint32_t color) {
    gfx_arc(fb, cx, cy, r, thick, 0.0f, 6.2831853f, color);
}

void gfx_disc_a(FB *fb, float cx, float cy, float r, uint32_t color, uint8_t a) {
    if (r <= 0.0f) return;
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = clampf(r + 0.5f - d, 0.0f, 1.0f);
            if (cov > 0.0f) gfx_blend_px(fb, x, y, color, (uint8_t)(cov * (float)a));
        }
    }
}

void gfx_arc(FB *fb, float cx, float cy, float r, float thick,
             float a0, float a1, uint32_t color) {
    float len = (a1 - a0) * r;
    int steps = (int)(len / 2.0f);
    if (steps < 3) steps = 3;
    if (steps > 4096) steps = 4096;
    float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
    for (int i = 1; i <= steps; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)steps;
        float nx = cx + cosf(a) * r, ny = cy + sinf(a) * r;
        gfx_line(fb, px, py, nx, ny, thick, color);
        px = nx; py = ny;
    }
}

void gfx_text(FB *fb, int x, int y, const char *s, int scale, uint32_t color) {
    gfx_text_a(fb, x, y, s, scale, color, 255);
}

void gfx_text_a(FB *fb, int x, int y, const char *s, int scale, uint32_t color, uint8_t a) {
    if (scale < 1) scale = 1;
    int cx = x;
    for (const char *p = s; *p; p++) {
        const uint8_t *g = font_glyph(*p);
        for (int row = 0; row < GLYPH_H; row++) {
            for (int col = 0; col < GLYPH_W; col++) {
                if (g[row] & (1u << (GLYPH_W - 1 - col))) {
                    int px = cx + col * scale, py = y + row * scale;
                    if (a == 255) gfx_fill(fb, px, py, scale, scale, color);
                    else gfx_fill_a(fb, px, py, scale, scale, color, a);
                }
            }
        }
        cx += GLYPH_ADV * scale;
    }
}

void gfx_text_center(FB *fb, int cx, int y, const char *s, int scale, uint32_t color) {
    gfx_text(fb, cx - font_text_width(s, scale) / 2, y, s, scale, color);
}

void gfx_text_right(FB *fb, int xr, int y, const char *s, int scale, uint32_t color) {
    gfx_text(fb, xr - font_text_width(s, scale), y, s, scale, color);
}
