#include "render.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Terrain pre-render                                                  */
/* ------------------------------------------------------------------ */

static uint32_t mix_color(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    int ar = (int)((a >> 16) & 255), ag = (int)((a >> 8) & 255), ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255), bg = (int)((b >> 8) & 255), bb = (int)(b & 255);
    return rgb((uint8_t)lerpf((float)ar, (float)br, t),
               (uint8_t)lerpf((float)ag, (float)bg, t),
               (uint8_t)lerpf((float)ab, (float)bb, t));
}

static uint32_t terrain_pixel(const World *w, float x, float y) {
    float e = world_elev_at(w, x, y);
    float ex = world_elev_at(w, x + 3.0f, y);
    float ey = world_elev_at(w, x, y + 3.0f);
    float shade = clampf(1.0f - ((ex - e) + (ey - e)) * 3.0f, 0.76f, 1.16f);

    uint32_t c;
    if (e < 0.215f) {
        c = mix_color(rgb(12, 44, 78), rgb(48, 126, 166), e / 0.215f);
    } else if (e < 0.235f) {
        c = rgb(204, 190, 133);
    } else if (e < 0.55f) {
        float f = world_forest_at(w, x, y);
        float t = (e - 0.235f) / (0.55f - 0.235f);
        uint32_t grass = mix_color(rgb(88, 146, 78), rgb(116, 174, 94), t);
        uint32_t trees = mix_color(rgb(39, 84, 47), rgb(55, 114, 64), t);
        float tint = smoothf(clampf((f - 0.53f) / 0.09f, 0.0f, 1.0f));
        c = mix_color(grass, trees, tint);
    } else if (e < 0.68f) {
        c = mix_color(rgb(148, 143, 114), rgb(174, 169, 138), (e - 0.55f) / 0.13f);
    } else {
        c = mix_color(rgb(108, 108, 106), rgb(206, 206, 200),
                      clampf((e - 0.68f) / 0.30f, 0.0f, 1.0f));
    }

    /* subtle grain so large fields are not flat */
    int ix = (int)x, iy = (int)y;
    uint32_t h = (uint32_t)ix * 374761393u + (uint32_t)iy * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    float grain = 1.0f + (((float)((h ^ (h >> 16)) & 255u) / 255.0f) - 0.5f) * 0.07f;
    return scale_color(c, shade * grain);
}

void render_init(Renderer *r, const Game *g) {
    memset(r, 0, sizeof(*r));
    r->terrain = malloc((size_t)WORLD_W * WORLD_H * sizeof(uint32_t));
    for (int y = 0; y < WORLD_H; y++) {
        for (int x = 0; x < WORLD_W; x++) {
            r->terrain[y * WORLD_W + x] =
                terrain_pixel(&g->w, (float)x + 0.5f, (float)y + 0.5f);
        }
    }

    r->mw = 184;
    r->mh = 104;
    r->mini = malloc((size_t)r->mw * r->mh * sizeof(uint32_t));
    for (int y = 0; y < r->mh; y++) {
        for (int x = 0; x < r->mw; x++) {
            int sx = x * WORLD_W / r->mw;
            int sy = y * WORLD_H / r->mh;
            uint32_t c = r->terrain[sy * WORLD_W + sx];
            r->mini[y * r->mw + x] = scale_color(c, 0.88f);
        }
    }
}

void render_free(Renderer *r) {
    free(r->terrain);
    free(r->mini);
    r->terrain = NULL;
    r->mini = NULL;
}

/* ------------------------------------------------------------------ */
/* Terrain blit (bilinear, screen-space)                               */
/* ------------------------------------------------------------------ */

static uint8_t fog_cells[ROWS][COLS];

static void build_fog_cells(const Game *g) {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            float vis = (float)g->vis[P_BLUE][r][c] / 255.0f;
            float seen = (float)g->seen[P_BLUE][r][c] / 255.0f;
            float fog = (1.0f - seen) * 0.88f + seen * (1.0f - vis) * 0.58f;
            fog_cells[r][c] = (uint8_t)(clampf(fog, 0.0f, 1.0f) * 255.0f);
        }
    }
}

static void blit_terrain(FB *fb, const Game *g, const Renderer *r, const View *v) {
    build_fog_cells(g);
    float inv = 1.0f / v->zoom;
    int32_t gxfp = (int32_t)((((v->cam.x + 0.5f * inv) / CELL) - 0.5f) * 65536.0f);
    int32_t gyfp = (int32_t)((((v->cam.y + 0.5f * inv) / CELL) - 0.5f) * 65536.0f);
    int32_t dxfp = (int32_t)((inv / CELL) * 65536.0f);
    int32_t dyfp = dxfp;
    for (int sy = 0; sy < fb->h; sy++) {
        float wy = v->cam.y + (float)sy * inv;
        int iy = gyfp >> 16;
        int fyi = (gyfp >> 8) & 255;
        if (iy < 0) { iy = 0; fyi = 0; }
        if (iy > ROWS - 2) { iy = ROWS - 2; fyi = 255; }
        int32_t gxrow = gxfp;
        if (wy < 0.0f || wy >= (float)WORLD_H) { gyfp += dyfp; continue; }
        int y0 = (int)wy;
        if (y0 > WORLD_H - 1) y0 = WORLD_H - 1;
        uint32_t *row = &fb->px[sy * fb->w];
        for (int sx = 0; sx < fb->w; sx++, gxrow += dxfp) {
            float wx = v->cam.x + (float)sx * inv;
            if (wx < 0.0f || wx >= (float)WORLD_W) continue;
            int x0 = (int)wx;
            if (x0 > WORLD_W - 1) x0 = WORLD_W - 1;
            uint32_t c = r->terrain[y0 * WORLD_W + x0];
            uint32_t rr = (c >> 16) & 255;
            uint32_t gg = (c >> 8) & 255;
            uint32_t bb = c & 255;
            /* fog of war from the per-cell alpha map, bilinear + fixed point */
            int ix = gxrow >> 16;
            int fxi = (gxrow >> 8) & 255;
            if (ix < 0) { ix = 0; fxi = 0; }
            if (ix > COLS - 2) { ix = COLS - 2; fxi = 255; }
            int f00 = fog_cells[iy][ix], f10 = fog_cells[iy][ix + 1];
            int f01 = fog_cells[iy + 1][ix], f11 = fog_cells[iy + 1][ix + 1];
            int ft = f00 + (((f10 - f00) * fxi) >> 8);
            int fbt = f01 + (((f11 - f01) * fxi) >> 8);
            int f = ft + (((fbt - ft) * fyi) >> 8);
            rr = (uint32_t)((int)rr - ((((int)rr - 7) * f) >> 8));
            gg = (uint32_t)((int)gg - ((((int)gg - 11) * f) >> 8));
            bb = (uint32_t)((int)bb - ((((int)bb - 20) * f) >> 8));
            row[sx] = 0xFF000000u | (rr << 16) | (gg << 8) | bb;
        }
        gyfp += dyfp;
    }
}

/* ------------------------------------------------------------------ */
/* Entities                                                            */
/* ------------------------------------------------------------------ */

static void draw_path(FB *fb, const View *v, const Vec2 *pts, int n,
                      uint32_t color, uint8_t alpha, float dotr) {
    if (n == 0) return;
    Vec2 prev = w2s(v, pts[0]);
    float step = 13.0f;
    for (int i = 1; i < n; i++) {
        Vec2 cur = w2s(v, pts[i]);
        float d = vdist(prev, cur);
        int dots = (int)(d / step);
        if (dots < 1) dots = 1;
        for (int k = 0; k <= dots; k++) {
            float t = (float)k / (float)dots;
            gfx_disc_a(fb, lerpf(prev.x, cur.x, t), lerpf(prev.y, cur.y, t),
                       dotr, color, alpha);
        }
        prev = cur;
    }
}

static void draw_city(FB *fb, const Game *g, const View *v, int idx) {
    const City *c = &g->cities[idx];
    Vec2 s = w2s(v, c->pos);
    float z = v->zoom;
    float r = CITY_R * z;
    if (s.x < -140.0f * z || s.y < -140.0f * z ||
        s.x > (float)fb->w + 140.0f * z || s.y > (float)fb->h + 140.0f * z) return;
    if (game_seen_at(g, P_BLUE, c->pos.x, c->pos.y) < 0.05f) return;

    /* current owner only while the city is actually in sight; otherwise show
     * the last owner the player scouted (or neutral if never scouted). */
    int shown_owner = c->owner;
    float vis = game_visibility_at(g, P_BLUE, c->pos.x, c->pos.y);
    if (vis < 0.18f) {
        shown_owner = game_known_city_owner(g, P_BLUE, idx);
        if (shown_owner < 0) shown_owner = -1;
    }

    if (shown_owner >= 0) {
        gfx_disc_a(fb, s.x, s.y, 110.0f * z, owner_color(shown_owner), 18);
    }

    /* flag */
    float pole_h = 34.0f * z;
    float pole_x = s.x, base_y = s.y - r * 0.5f, top_y = base_y - pole_h;
    gfx_line(fb, pole_x, base_y, pole_x, top_y, fmaxf(1.0f, 2.0f * z), 0xFF33333C);
    float fw = 18.0f * z, fh = 12.0f * z;
    uint32_t fc = shown_owner >= 0 ? owner_color(shown_owner) : C_NEUTRAL;
    gfx_tri(fb, pole_x, top_y, pole_x + fw, top_y + fh * 0.5f,
            pole_x, top_y + fh, fc);
    gfx_line(fb, pole_x, top_y, pole_x + fw, top_y + fh * 0.5f, 1.0f, 0xFF202026);
    gfx_line(fb, pole_x + fw, top_y + fh * 0.5f, pole_x, top_y + fh, 1.0f, 0xFF202026);

    /* base disc */
    gfx_disc(fb, s.x, s.y, r + 2.0f, 0xFF1A1A20);
    gfx_disc(fb, s.x, s.y, r, shown_owner >= 0 ? owner_dark(shown_owner) : 0xFF8A8A8A);
    gfx_disc(fb, s.x, s.y, r * 0.62f, fc);
    gfx_disc(fb, s.x, s.y, r * 0.22f, 0xFFF7F1DF);

    /* capture progress */
    if (vis >= 0.18f && c->cap > 0.001f && c->cap_owner >= 0.0f &&
        c->owner != (int)c->cap_owner) {
        int who = (int)c->cap_owner;
        gfx_arc(fb, s.x, s.y, r + 6.0f * z, fmaxf(1.5f, 3.0f * z),
                -1.5707963f, -1.5707963f + c->cap * 6.2831853f, owner_color(who));
    }

    if (idx == g->sel_city) {
        gfx_ring(fb, s.x, s.y, r + 9.0f * z, 2.0f, C_WHITE);
    }
}

static int unit_in_combat(const Game *g, const Unit *u) {
    for (int j = 0; j < MAX_UNITS; j++) {
        const Unit *o = &g->units[j];
        if (!o->alive || o->owner == u->owner) continue;
        if (vdist(u->pos, o->pos) < ATTACK_RANGE) return 1;
    }
    return 0;
}

static void draw_unit(FB *fb, const Game *g, const View *v, int idx) {
    const Unit *u = &g->units[idx];
    if (!u->alive) return;
    if (u->owner != P_BLUE &&
        game_visibility_at(g, P_BLUE, u->pos.x, u->pos.y) < 0.30f) return;
    float z = v->zoom;
    Vec2 pos = u->pos;
    if (unit_in_combat(g, u)) {
        pos.x += sinf(g->time * 28.0f + (float)idx) * 1.3f;
        pos.y += cosf(g->time * 31.0f + (float)idx) * 1.3f;
    }
    Vec2 s = w2s(v, pos);
    float r = fmaxf(3.0f, UNIT_R * z);
    if (s.x < -20 || s.y < -20 || s.x > (float)fb->w + 20 || s.y > (float)fb->h + 20) return;

    uint32_t col = owner_color(u->owner);
    if (u->flash > 0.0f) col = C_WHITE;
    if (g->selected[idx]) gfx_ring(fb, s.x, s.y, r + 3.5f, 2.0f, C_WHITE);
    gfx_disc(fb, s.x, s.y, r + 1.4f, 0xFF15151A);
    gfx_disc(fb, s.x, s.y, r, col);

    if (u->hp < MAX_HP - 0.5f || g->selected[idx]) {
        float bw = fmaxf(10.0f, 15.0f * z);
        float bh = fmaxf(3.0f, 2.5f * z);
        float bx = s.x - bw * 0.5f;
        float by = s.y - r - bh - 4.0f;
        gfx_fill(fb, (int)bx - 1, (int)by - 1, (int)bw + 2, (int)bh + 2, 0xFF15151A);
        float frac = clampf(u->hp / MAX_HP, 0.0f, 1.0f);
        uint32_t hc = frac > 0.55f ? 0xFF5BE06B : (frac > 0.25f ? 0xFFE0C34B : 0xFFE05B5B);
        gfx_fill(fb, (int)bx, (int)by, (int)(bw * frac), (int)bh, hc);
    }
}

/* ------------------------------------------------------------------ */
/* World frame                                                         */
/* ------------------------------------------------------------------ */

void render_world(FB *fb, const Game *g, Renderer *r, const View *v) {
    if (!r->screen || r->sw != fb->w || r->sh != fb->h) {
        free(r->screen);
        r->screen = malloc((size_t)fb->w * (size_t)fb->h * sizeof(uint32_t));
        r->sw = fb->w;
        r->sh = fb->h;
        r->cache_valid = 0;
    }
    int refresh = !r->cache_valid ||
                  r->cache_cam.x != v->cam.x || r->cache_cam.y != v->cam.y ||
                  r->cache_zoom != v->zoom || (r->cache_age++ % 8) == 0;
    if (refresh) {
        FB terr = { r->screen, fb->w, fb->h };
        gfx_clear(&terr, 0xFF0A1119);
        blit_terrain(&terr, g, r, v);
        r->cache_cam = v->cam;
        r->cache_zoom = v->zoom;
        r->cache_valid = 1;
    }
    memcpy(fb->px, r->screen, (size_t)fb->w * (size_t)fb->h * sizeof(uint32_t));

    /* city auras are drawn by draw_city */

    /* own paths + rally lines (never reveal the opponent's intent) */
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner != P_BLUE || u->pi >= u->plen) continue;
        draw_path(fb, v, &u->path[u->pi], u->plen - u->pi,
                  owner_color(u->owner), 70, fmaxf(1.2f, 1.6f * v->zoom));
    }
    for (int i = 0; i < g->ncity; i++) {
        const City *c = &g->cities[i];
        if (c->owner != P_BLUE || !c->has_rally) continue;
        Vec2 s = w2s(v, c->pos), e = w2s(v, c->rally);
        gfx_line(fb, s.x, s.y, e.x, e.y, fmaxf(1.0f, 1.5f * v->zoom), 0xFF7FB2FF);
        gfx_ring(fb, e.x, e.y, 5.0f, 1.5f, 0xFF7FB2FF);
    }

    for (int i = 0; i < g->ncity; i++) draw_city(fb, g, v, i);
    for (int i = 0; i < MAX_UNITS; i++) draw_unit(fb, g, v, i);

    /* active path preview */
    if (v->path_active && v->plen > 0) {
        if (g->nsel > 0) {
            for (int i = 0; i < MAX_UNITS; i++) {
                if (!g->selected[i] || !g->units[i].alive) continue;
                Vec2 s = w2s(v, g->units[i].pos), e = w2s(v, v->path[0]);
                gfx_line(fb, s.x, s.y, e.x, e.y, 1.0f, 0xFF8FD8FF);
            }
        }
        draw_path(fb, v, v->path, v->plen, C_WHITE, 180, 2.0f);
        Vec2 last = w2s(v, v->path[v->plen - 1]);
        gfx_ring(fb, last.x, last.y, 7.0f, 2.0f, C_WHITE);
    }

    /* selection box */
    if (v->box_active) {
        Vec2 a = w2s(v, v->box_a), b = w2s(v, v->box_b);
        float x0 = fminf(a.x, b.x), x1 = fmaxf(a.x, b.x);
        float y0 = fminf(a.y, b.y), y1 = fmaxf(a.y, b.y);
        gfx_fill_a(fb, (int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0),
                   0xFF9FD8FF, 28);
        gfx_rect_a(fb, x0, y0, x1 - x0, y1 - y0, 0xFF9FD8FF, 200);
    }
}
