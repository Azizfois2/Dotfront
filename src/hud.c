#include "render.h"
#include "font.h"
#include <stdio.h>
#include <string.h>

static void fmt_time(char *buf, size_t n, float t) {
    int secs = (int)t;
    snprintf(buf, n, "%d:%02d", secs / 60, secs % 60);
}

static void shadow_text(FB *fb, int cx, int y, const char *s, int scale, uint32_t color) {
    gfx_text_center(fb, cx + 2, y + 2, s, scale, 0x99000000);
    gfx_text_center(fb, cx, y, s, scale, color);
}

/* ------------------------------------------------------------------ */

static void draw_minimap(FB *fb, const Game *g, const Renderer *r, const View *v) {
    int mx = fb->w - r->mw - 14;
    int my = fb->h - r->mh - 34;
    gfx_fill(fb, mx - 4, my - 4, r->mw + 8, r->mh + 8, 0xCC0A0F15);
    for (int y = 0; y < r->mh; y++) {
        for (int x = 0; x < r->mw; x++) {
            float wx = ((float)x + 0.5f) / (float)r->mw * (float)WORLD_W;
            float wy = ((float)y + 0.5f) / (float)r->mh * (float)WORLD_H;
            int cc = (int)(wx / CELL), rr2 = (int)(wy / CELL);
            if (cc > COLS - 1) cc = COLS - 1;
            if (rr2 > ROWS - 1) rr2 = ROWS - 1;
            float vis = (float)g->vis[P_BLUE][rr2][cc] / 255.0f;
            float seen = (float)g->seen[P_BLUE][rr2][cc] / 255.0f;
            float fog = (1.0f - seen) * 0.88f + seen * (1.0f - vis) * 0.55f;
            uint32_t c0 = r->mini[y * r->mw + x];
            uint32_t rr = (uint32_t)lerpf((float)((c0 >> 16) & 255), 7.0f, fog);
            uint32_t gg = (uint32_t)lerpf((float)((c0 >> 8) & 255), 11.0f, fog);
            uint32_t bb = (uint32_t)lerpf((float)(c0 & 255), 20.0f, fog);
            gfx_px(fb, mx + x, my + y, 0xFF000000u | (rr << 16) | (gg << 8) | bb);
        }
    }

    for (int i = 0; i < g->ncity; i++) {
        const City *c = &g->cities[i];
        if (game_seen_at(g, P_BLUE, c->pos.x, c->pos.y) < 0.05f) continue;
        int owner = game_known_city_owner(g, P_BLUE, i);
        if (owner < 0) owner = -1;
        int x = mx + (int)(c->pos.x / WORLD_W * (float)r->mw);
        int y = my + (int)(c->pos.y / WORLD_H * (float)r->mh);
        gfx_disc(fb, (float)x, (float)y, 2.6f, 0xFF15151A);
        gfx_disc(fb, (float)x, (float)y, 1.9f, owner_color(owner));
    }
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive) continue;
        if (u->owner != P_BLUE &&
            game_visibility_at(g, P_BLUE, u->pos.x, u->pos.y) < 0.30f) continue;
        int x = mx + (int)(u->pos.x / WORLD_W * (float)r->mw);
        int y = my + (int)(u->pos.y / WORLD_H * (float)r->mh);
        gfx_px(fb, x, y, owner_color(u->owner));
        gfx_px(fb, x + 1, y, owner_color(u->owner));
        gfx_px(fb, x, y + 1, owner_color(u->owner));
        gfx_px(fb, x + 1, y + 1, owner_color(u->owner));
    }

    float cw = (float)v->w / v->zoom / WORLD_W * (float)r->mw;
    float ch = (float)v->h / v->zoom / WORLD_H * (float)r->mh;
    float cx = v->cam.x / WORLD_W * (float)r->mw;
    float cy = v->cam.y / WORLD_H * (float)r->mh;
    gfx_rect_a(fb, (float)mx + cx, (float)my + cy,
               fminf(cw, (float)r->mw), fminf(ch, (float)r->mh), C_WHITE, 210);
    gfx_rect(fb, (float)mx - 4, (float)my - 4,
             (float)r->mw + 8, (float)r->mh + 8, 0xFF4A5568);
}

static void draw_topbar(FB *fb, const Game *g) {
    char buf[96];
    gfx_fill_a(fb, 0, 0, fb->w, 46, 0xFF080B10, 205);
    gfx_fill_a(fb, 0, 46, fb->w, 2, 0xFF2A3442, 255);

    gfx_disc(fb, 22.0f, 23.0f, 7.0f, C_BLUE);
    snprintf(buf, sizeof(buf), "BLUE  CITIES %d  UNITS %d",
             g->cities_owned[P_BLUE], g->units_owned[P_BLUE]);
    gfx_text(fb, 36, 16, buf, 2, C_BLUE);

    char t[16];
    fmt_time(t, sizeof(t), g->time);
    gfx_text_center(fb, fb->w / 2, 16, t, 2, C_WHITE);

    /* only scouted cities and currently visible red units are known */
    snprintf(buf, sizeof(buf), "CITIES %d  SEEN %d  RED",
             game_known_city_count(g, P_BLUE, P_RED),
             game_visible_unit_count(g, P_BLUE, P_RED));
    gfx_text_right(fb, fb->w - 36, 16, buf, 2, C_RED);
    gfx_disc(fb, (float)fb->w - 22.0f, 23.0f, 7.0f, C_RED);

    const char *dn = g->difficulty[P_RED] == DIFF_EASY ? "EASY"
                   : g->difficulty[P_RED] == DIFF_HARD ? "HARD" : "NORMAL";
    char dline[24];
    snprintf(dline, sizeof(dline), "AI %s", dn);
    gfx_text_right(fb, fb->w - 36, 32, dline, 1, 0xFFB98A8A);
}

static void draw_selection_info(FB *fb, const Game *g) {
    char buf[128];
    int y = fb->h - 60;
    if (g->nsel > 0) {
        int n[UT_COUNT] = {0, 0, 0};
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!g->selected[i] || !g->units[i].alive) continue;
            int t = g->units[i].type;
            if (t >= 0 && t < UT_COUNT) n[t]++;
        }
        snprintf(buf, sizeof(buf),
                 "%d UNITS  %d SCOUT  %d SOLDIER  %d HEAVY  -  RMB MOVE  G ATTACK-MOVE",
                 g->nsel, n[UT_SCOUT], n[UT_SOLDIER], n[UT_HEAVY]);
    } else if (g->sel_city >= 0 && g->cities[g->sel_city].owner == P_BLUE) {
        const City *c = &g->cities[g->sel_city];
        int t = c->prod_type;
        if (t < 0 || t >= UT_COUNT) t = UT_SOLDIER;
        if (c->fort_t > 0.0f)
            snprintf(buf, sizeof(buf), "FORTIFYING  %.1fs  -  RMB RALLY", (double)c->fort_t);
        else if (c->fortified)
            snprintf(buf, sizeof(buf), "CITY FORTIFIED  %s  -  1/2/3  -  RMB RALLY",
                     UNIT_TYPE[t].name);
        else
            snprintf(buf, sizeof(buf), "CITY %s  -  1/2/3  -  F FORTIFY  -  RMB RALLY",
                     UNIT_TYPE[t].name);
    } else if (g->sel_city >= 0) {
        snprintf(buf, sizeof(buf), "ENEMY / NEUTRAL CITY  -  MOVE UNITS INSIDE TO CAPTURE");
    } else {
        snprintf(buf, sizeof(buf), "LEFT DRAG TO BOX-SELECT  -  RIGHT DRAG TO MOVE");
    }
    gfx_fill_a(fb, 10, y - 4, font_text_width(buf, 2) + 16, 24, 0xFF080B10, 165);
    gfx_text(fb, 18, y, buf, 2, 0xFFCFE3F5);
}

static void draw_help(FB *fb, const Game *g) {
    (void)g;
    int h = 24;
    int y = fb->h - h;
    gfx_fill_a(fb, 0, y, fb->w, h, 0xFF080B10, 200);
    const char *help =
        "LMB SELECT   RMB MOVE   G ATTACK-MOVE   H HOLD   X STOP   Z ALL   "
        "SPACE PAUSE   F5 SAVE   F9 LOAD   F FORTIFY   M SOUND   R NEW   WASD PAN   WHEEL ZOOM";
    gfx_text(fb, 12, y + 8, help, 1, 0xFF8C9AAB);
}

static void draw_menu(FB *fb, const Game *g) {
    gfx_fill_a(fb, 0, 0, fb->w, fb->h, 0xFF05070A, 205);
    int cx = fb->w / 2;
    int y = fb->h / 2 - 190;
    shadow_text(fb, cx, y, "WAR OF DOTS", 7, C_WHITE);

    char line[96];
    snprintf(line, sizeof(line), "SEED %u    %d CITIES", g->w.seed, g->ncity);
    shadow_text(fb, cx, y + 86, line, 2, 0xFF9FB4C8);

    shadow_text(fb, cx, y + 140, "SPACE   START MATCH", 3, C_WHITE);
    if (g->save_available)
        shadow_text(fb, cx, y + 184, "F9   CONTINUE SAVED MATCH", 2, 0xFFBFD4E6);

    const char *names[DIFF_COUNT] = { "EASY", "NORMAL", "HARD" };
    int total = 0;
    for (int i = 0; i < DIFF_COUNT; i++) total += font_text_width(names[i], 2) + 46;
    int x = cx - total / 2;
    for (int i = 0; i < DIFF_COUNT; i++) {
        char item[32];
        snprintf(item, sizeof(item), "%d %s", i + 1, names[i]);
        uint32_t col = (g->difficulty[P_RED] == i) ? C_WHITE : 0xFF71808F;
        gfx_text(fb, x, y + 238, item, 2, col);
        x += font_text_width(item, 2) + 46;
    }

    shadow_text(fb, cx, y + 286, "R NEW MAP     M SOUND ON/OFF     Q QUIT", 2, 0xFF9FB4C8);
    shadow_text(fb, cx, y + 336, "LMB SELECT   RMB MOVE   G ATTACK-MOVE   H HOLD   X STOP",
                1, 0xFF71808F);
    shadow_text(fb, cx, y + 356, "1/2/3 CITY UNIT   F FORTIFY   F5 SAVE   F9 LOAD", 1, 0xFF71808F);
}

void render_hud(FB *fb, const Game *g, const Renderer *r, const View *v) {
    draw_topbar(fb, g);
    draw_selection_info(fb, g);
    draw_minimap(fb, g, r, v);
    draw_help(fb, g);

    if (g->notice_t > 0.0f && !g->menu && g->winner < 0) {
        int w = font_text_width(g->notice, 2) + 28;
        gfx_fill_a(fb, (fb->w - w) / 2, fb->h - 104, w, 28, 0xFF080B10, 220);
        gfx_text_center(fb, fb->w / 2, fb->h - 96, g->notice, 2, C_WHITE);
    }

    if (g->winner >= 0) {
        char line[64];
        gfx_fill_a(fb, 0, 0, fb->w, fb->h, 0xFF05070A, 165);
        int cy = fb->h / 2 - 60;
        const char *who = g->winner == P_BLUE ? "BLUE WINS" : "RED WINS";
        shadow_text(fb, fb->w / 2, cy, who, 7, owner_color(g->winner));
        char tm[32];
        fmt_time(tm, sizeof(tm), g->time);
        snprintf(line, sizeof(line), "FINAL TIME  %s", tm);
        shadow_text(fb, fb->w / 2, cy + 76, line, 3, C_WHITE);

        snprintf(line, sizeof(line), "CAPTURES %d - %d    BUILT %d - %d",
                 g->captures[0], g->captures[1], g->built[0], g->built[1]);
        shadow_text(fb, fb->w / 2, cy + 120, line, 2, 0xFFCFE3F5);
        snprintf(line, sizeof(line), "LOST %d - %d    PEAK ARMY %d - %d",
                 g->lost[0], g->lost[1], g->peak[0], g->peak[1]);
        shadow_text(fb, fb->w / 2, cy + 146, line, 2, 0xFFCFE3F5);
        shadow_text(fb, fb->w / 2, cy + 188, "R NEW MATCH    F9 LOAD    Q QUIT", 2, 0xFFBFD4E6);
    } else if (g->menu) {
        draw_menu(fb, g);
    } else if (g->paused) {
        gfx_fill_a(fb, 0, 0, fb->w, fb->h, 0xFF05070A, 120);
        int starting = g->time < 0.01f;
        shadow_text(fb, fb->w / 2, fb->h / 2 - 40,
                    starting ? "WAR OF DOTS" : "PAUSED", 7, C_WHITE);
        shadow_text(fb, fb->w / 2, fb->h / 2 + 34,
                    starting ? "SPACE TO START" : "SPACE TO RESUME", 3, 0xFFBFD4E6);
    }
}
