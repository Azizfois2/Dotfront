#ifndef RENDER_H
#define RENDER_H

#include "common.h"
#include "game.h"
#include "gfx.h"

#define VIEW_PATH_MAX 64

typedef struct {
    uint32_t *terrain;   /* full world pre-render, WORLD_W x WORLD_H */
    uint32_t *mini;      /* minimap thumbnail */
    int mw, mh;
    uint32_t *screen;    /* cached terrain+fog at screen resolution */
    int sw, sh;
    Vec2 cache_cam;      /* camera state the cache was built for */
    float cache_zoom;
    int cache_valid;
    int cache_age;
} Renderer;

typedef struct {
    Vec2  cam;           /* world coordinate of screen top-left */
    float zoom;
    int   w, h;          /* screen size */
    int   box_active;
    Vec2  box_a, box_b;  /* world coords */
    int   path_active;
    Vec2  path[VIEW_PATH_MAX];
    int   plen;
} View;

void render_init(Renderer *r, const Game *g);
void render_free(Renderer *r);
void render_world(FB *fb, const Game *g, Renderer *r, const View *v);
void render_hud(FB *fb, const Game *g, const Renderer *r, const View *v);

static inline Vec2 w2s(const View *v, Vec2 w) {
    return v2((w.x - v->cam.x) * v->zoom, (w.y - v->cam.y) * v->zoom);
}
static inline uint32_t owner_color(int o) {
    return o == P_BLUE ? C_BLUE : (o == P_RED ? C_RED : C_NEUTRAL);
}
static inline uint32_t owner_dark(int o) {
    return o == P_BLUE ? C_BLUE_DARK : (o == P_RED ? C_RED_DARK : 0xFF707070);
}

#endif /* RENDER_H */
