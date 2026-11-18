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
        /* deep water -> turquoise shallows, with a soft band near the shore */
        float t = clampf(e / 0.215f, 0.0f, 1.0f);
        c = mix_color(rgb(10, 37, 68), rgb(45, 126, 158), t * t);
        float foam = smoothf(clampf((t - 0.82f) / 0.18f, 0.0f, 1.0f));
        c = mix_color(c, rgb(112, 174, 178), foam * 0.42f);
    } else if (e < 0.250f) {
        /* wet sand blending into grass removes the hard one-pixel coastline */
        float t = smoothf(clampf((e - 0.215f) / 0.035f, 0.0f, 1.0f));
        c = mix_color(rgb(196, 184, 132), rgb(123, 156, 91), t);
    } else if (e < 0.55f) {
        float f = world_forest_at(w, x, y);
        float t = (e - 0.250f) / (0.55f - 0.250f);
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
    r->tw = g->w.width;
    r->th = g->w.height;
    r->terrain = malloc((size_t)r->tw * r->th * sizeof(uint32_t));
    for (int y = 0; y < r->th; y++) {
        for (int x = 0; x < r->tw; x++) {
            r->terrain[y * r->tw + x] =
                terrain_pixel(&g->w, (float)x + 0.5f, (float)y + 0.5f);
        }
    }

    r->mw = 184;
    r->mh = 104;
    r->mini = malloc((size_t)r->mw * r->mh * sizeof(uint32_t));
    for (int y = 0; y < r->mh; y++) {
        for (int x = 0; x < r->mw; x++) {
            int sx = x * r->tw / r->mw;
            int sy = y * r->th / r->mh;
            uint32_t c = r->terrain[sy * r->tw + sx];
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

static uint8_t fog_cells[MAX_ROWS][MAX_COLS];

static void build_fog_cells(const Game *g) {
    int p = g->local;
    for (int r = 0; r < g->w.rows; r++) {
        for (int c = 0; c < g->w.cols; c++) {
            if (g->spectator) { fog_cells[r][c] = 0; continue; }
            float vis = (float)g->vis[p][r][c] / 255.0f;
            float seen = (float)g->seen[p][r][c] / 255.0f;
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
        if (iy > g->w.rows - 2) { iy = g->w.rows - 2; fyi = 255; }
        int32_t gxrow = gxfp;
        if (wy < 0.0f || wy >= (float)g->w.height) { gyfp += dyfp; continue; }
        int y0 = (int)wy;
        if (y0 > g->w.height - 1) y0 = g->w.height - 1;
        uint32_t *row = &fb->px[sy * fb->w];
        for (int sx = 0; sx < fb->w; sx++, gxrow += dxfp) {
            float wx = v->cam.x + (float)sx * inv;
            if (wx < 0.0f || wx >= (float)g->w.width) continue;
            int x0 = (int)wx;
            if (x0 > g->w.width - 1) x0 = g->w.width - 1;
            uint32_t c = r->terrain[y0 * r->tw + x0];
            uint32_t rr = (c >> 16) & 255;
            uint32_t gg = (c >> 8) & 255;
            uint32_t bb = c & 255;
            /* fog of war from the per-cell alpha map, bilinear + fixed point */
            int ix = gxrow >> 16;
            int fxi = (gxrow >> 8) & 255;
            if (ix < 0) { ix = 0; fxi = 0; }
            if (ix > g->w.cols - 2) { ix = g->w.cols - 2; fxi = 255; }
            /* nearby territory gets a faint faction tint, so ownership is
             * readable even before the border line is noticed */
            int town = g->terr_owner[iy][ix];
            if (town >= 0 && (g->spectator || g->seen[g->local][iy][ix] > 40)) {
                uint32_t oc = owner_color(town);
                rr = (rr * 86u + ((oc >> 16) & 255u) * 14u) / 100u;
                gg = (gg * 86u + ((oc >> 8) & 255u) * 14u) / 100u;
                bb = (bb * 86u + (oc & 255u) * 14u) / 100u;
            }
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

/* Territory edges are derived from the per-cell owner grid. Only edges the
 * local faction has explored are drawn, so borders obey fog of war. */
static void draw_borders(FB *fb, const Game *g, const View *v) {
    int c0 = (int)floorf(v->cam.x / CELL) - 1;
    int r0 = (int)floorf(v->cam.y / CELL) - 1;
    int c1 = (int)ceilf((v->cam.x + (float)fb->w / v->zoom) / CELL) + 1;
    int r1 = (int)ceilf((v->cam.y + (float)fb->h / v->zoom) / CELL) + 1;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > g->w.cols) c1 = g->w.cols;
    if (r1 > g->w.rows) r1 = g->w.rows;
    float thick = fmaxf(2.0f, 3.0f * v->zoom);
    for (int r = r0; r < r1; r++) {
        for (int c = c0; c < c1; c++) {
            if (!g->spectator && g->seen[g->local][r][c] < 55) continue;
            int o = g->terr_owner[r][c];
            if (c + 1 < g->w.cols) {
                int q = g->terr_owner[r][c + 1];
                if (o != q && (o >= 0 || q >= 0)) {
                    int own = o >= 0 ? o : q;
                    uint32_t col = scale_color(owner_color(own),
                                               (o >= 0 && q >= 0) ? 1.0f : 0.82f);
                    Vec2 a = w2s(v, v2((float)(c + 1) * CELL, (float)r * CELL));
                    Vec2 b = w2s(v, v2((float)(c + 1) * CELL, (float)(r + 1) * CELL));
                    gfx_line(fb, a.x, a.y, b.x, b.y, thick + 1.4f, 0xFF0A0F15);
                    gfx_line(fb, a.x, a.y, b.x, b.y, thick, col);
                }
            }
            if (r + 1 < g->w.rows) {
                int q = g->terr_owner[r + 1][c];
                if (o != q && (o >= 0 || q >= 0)) {
                    int own = o >= 0 ? o : q;
                    uint32_t col = scale_color(owner_color(own),
                                               (o >= 0 && q >= 0) ? 1.0f : 0.82f);
                    Vec2 a = w2s(v, v2((float)c * CELL, (float)(r + 1) * CELL));
                    Vec2 b = w2s(v, v2((float)(c + 1) * CELL, (float)(r + 1) * CELL));
                    gfx_line(fb, a.x, a.y, b.x, b.y, thick + 1.4f, 0xFF0A0F15);
                    gfx_line(fb, a.x, a.y, b.x, b.y, thick, col);
                }
            }
        }
    }
}

static void draw_city(FB *fb, const Game *g, const View *v, int idx) {
    const City *c = &g->cities[idx];
    Vec2 s = w2s(v, c->pos);
    float z = v->zoom;
    float r = CITY_R * z;
    if (s.x < -140.0f * z || s.y < -140.0f * z ||
        s.x > (float)fb->w + 140.0f * z || s.y > (float)fb->h + 140.0f * z) return;
    if (!g->spectator &&
        game_seen_at(g, g->local, c->pos.x, c->pos.y) < 0.05f) return;

    /* current owner only while the city is actually in sight; otherwise show
     * the last owner the player scouted (or neutral if never scouted). */
    int shown_owner = c->owner;
    float vis = g->spectator ? 1.0f
              : game_visibility_at(g, g->local, c->pos.x, c->pos.y);
    if (vis < 0.18f) {
        shown_owner = game_known_city_owner(g, g->local, idx);
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

    if (c->fortified) {
        for (int k = 0; k < 4; k++) {
            float a = -1.5707963f + (float)k * 1.5707963f + 0.22f;
            gfx_arc(fb, s.x, s.y, r + 7.0f * z, fmaxf(1.5f, 2.5f * z),
                    a, a + 1.13f, 0xFFE8C25A);
        }
    } else if (c->fort_t > 0.0f) {
        float prog = 1.0f - c->fort_t / 10.0f;
        if (prog < 0.0f) prog = 0.0f;
        if (prog > 1.0f) prog = 1.0f;
        gfx_arc(fb, s.x, s.y, r + 7.0f * z, fmaxf(1.5f, 2.5f * z),
                -1.5707963f, -1.5707963f + prog * 6.2831853f, 0xFFE8C25A);
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
    if (!g->spectator && u->owner != g->local &&
        game_visibility_at(g, g->local, u->pos.x, u->pos.y) < 0.30f) return;
    float z = v->zoom;
    Vec2 pos = u->pos;
    if (unit_in_combat(g, u)) {
        pos.x += sinf(g->time * 28.0f + (float)idx) * 1.3f;
        pos.y += cosf(g->time * 31.0f + (float)idx) * 1.3f;
    }
    Vec2 s = w2s(v, pos);
    int ut = u->type;
    if (ut < 0 || ut >= UT_COUNT) ut = UT_SOLDIER;
    float r = fmaxf(3.0f, UNIT_TYPE[ut].radius * z);
    if (s.x < -20 || s.y < -20 || s.x > (float)fb->w + 20 || s.y > (float)fb->h + 20) return;

    uint32_t col = owner_color(u->owner);
    if (u->flash > 0.0f) col = C_WHITE;
    if (g->selected[idx]) gfx_ring(fb, s.x, s.y, r + 3.5f, 2.0f, C_WHITE);
    gfx_disc(fb, s.x, s.y, r + 1.4f, 0xFF15151A);
    gfx_disc(fb, s.x, s.y, r, col);

    if (ut == UT_SCOUT) {
        gfx_disc(fb, s.x, s.y, r * 0.34f, 0xFFF5F5F5);
    } else if (ut == UT_HEAVY) {
        gfx_ring(fb, s.x, s.y, r + 1.0f, fmaxf(1.0f, 1.4f * z), 0xFF15151A);
        gfx_disc(fb, s.x, s.y, r * 0.30f, 0xFF15151A);
    }

    float maxhp = u->max_hp > 0.0f ? u->max_hp : MAX_HP;
    if (u->hp < maxhp - 0.5f || g->selected[idx]) {
        float bw = fmaxf(10.0f, 15.0f * z);
        float bh = fmaxf(3.0f, 2.5f * z);
        float bx = s.x - bw * 0.5f;
        float by = s.y - r - bh - 4.0f;
        gfx_fill(fb, (int)bx - 1, (int)by - 1, (int)bw + 2, (int)bh + 2, 0xFF15151A);
        float frac = clampf(u->hp / maxhp, 0.0f, 1.0f);
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

    draw_borders(fb, g, v);

    /* own paths + rally lines (never reveal the opponent's intent) */
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner != g->local || u->pi >= u->plen) continue;
        draw_path(fb, v, &u->path[u->pi], u->plen - u->pi,
                  owner_color(u->owner), 70, fmaxf(1.2f, 1.6f * v->zoom));
    }
    for (int i = 0; i < g->ncity; i++) {
        const City *c = &g->cities[i];
        if (c->owner != g->local || !c->has_rally) continue;
        Vec2 s = w2s(v, c->pos), e = w2s(v, c->rally);
        gfx_line(fb, s.x, s.y, e.x, e.y, fmaxf(1.0f, 1.5f * v->zoom), 0xFF7FB2FF);
        gfx_ring(fb, e.x, e.y, 5.0f, 1.5f, 0xFF7FB2FF);
    }

    for (int i = 0; i < g->ncity; i++) draw_city(fb, g, v, i);
    for (int i = 0; i < MAX_UNITS; i++) draw_unit(fb, g, v, i);

    /* staged waypoints: draw each committed stage and its marker */
    if (v->stage_len > 1) {
        draw_path(fb, v, v->stage, v->stage_len, C_GOLD, 150, fmaxf(1.2f, 1.7f * v->zoom));
        for (int i = 0; i < v->stage_marks_n; i++) {
            int m = v->stage_marks[i];
            if (m <= 0 || m >= v->stage_len) continue;
            Vec2 p = w2s(v, v->stage[m]);
            gfx_ring(fb, p.x, p.y, 6.0f, 1.5f, C_GOLD);
        }
        Vec2 s = w2s(v, v->stage[0]), e = w2s(v, v->stage[v->stage_len - 1]);
        gfx_line(fb, s.x, s.y, e.x, e.y, 1.0f, 0x66F2C14E);
    }

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
