#ifndef RENDER_H
#define RENDER_H

#include "common.h"
#include "game.h"
#include "gfx.h"

#define VIEW_PATH_MAX 64

typedef struct {
    uint32_t *terrain;   /* full world pre-render, tw x th */
    int tw, th;
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
    /* staged waypoints (Shift+RMB). Space sends, C clears. */
    int   stage_active;
    Vec2  stage[VIEW_PATH_MAX];
    int   stage_len;
    int   stage_marks[8];
    int   stage_marks_n;
    int   show_help;     /* tutorial / controls overlay */
    int   help_was_paused;
    char  net_status[96];  /* LAN host/client badge, empty when offline */
    int   mouse_x, mouse_y;
} View;

void render_init(Renderer *r, const Game *g);
void render_free(Renderer *r);
void render_world(FB *fb, const Game *g, Renderer *r, const View *v);
void render_hud(FB *fb, const Game *g, const Renderer *r, const View *v);

enum {
    UI_NONE = 0,
    UI_MENU_START, UI_MENU_CONTINUE, UI_MENU_LEFT, UI_MENU_RIGHT,
    UI_MENU_EASY, UI_MENU_NORMAL, UI_MENU_HARD,
    UI_MENU_NEW, UI_MENU_SOUND, UI_MENU_HELP, UI_MENU_QUIT,
    UI_PAUSE_RESUME,
    UI_ELIM_RESTART, UI_ELIM_SPECTATE, UI_ELIM_QUIT,
    UI_WIN_RESTART, UI_WIN_LOAD, UI_WIN_QUIT,
    UI_HELP_CLOSE,
    UI_PROD_SCOUT, UI_PROD_SOLDIER, UI_PROD_HEAVY, UI_CITY_FORTIFY
};

/* Returns the overlay action whose hit box contains (mx,my), or UI_NONE. */
int ui_hit_test(const Game *g, const View *v, int mx, int my);

static inline Vec2 w2s(const View *v, Vec2 w) {
    return v2((w.x - v->cam.x) * v->zoom, (w.y - v->cam.y) * v->zoom);
}
static inline uint32_t owner_color(int o) {
    static const uint32_t c[MAX_PLAYERS] = {
        C_BLUE, C_RED, C_GREEN, C_GOLD, C_VIOLET, C_CYAN
    };
    return (o >= 0 && o < MAX_PLAYERS) ? c[o] : C_NEUTRAL;
}
static inline uint32_t owner_dark(int o) {
    static const uint32_t c[MAX_PLAYERS] = {
        C_BLUE_DARK, C_RED_DARK, C_GREEN_DARK, C_GOLD_DARK,
        C_VIOLET_DARK, C_CYAN_DARK
    };
    return (o >= 0 && o < MAX_PLAYERS) ? c[o] : 0xFF707070;
}
static inline const char *owner_name(int o) {
    static const char *n[MAX_PLAYERS] = {
        "BLUE", "RED", "GREEN", "GOLD", "VIOLET", "CYAN"
    };
    return (o >= 0 && o < MAX_PLAYERS) ? n[o] : "NEUTRAL";
}

#endif /* RENDER_H */
