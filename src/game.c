#include "game.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static void update_stats(Game *g);

int game_spawn_unit(Game *g, int owner, Vec2 pos) {
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (u->alive) continue;
        memset(u, 0, sizeof(*u));
        u->alive = 1;
        u->owner = owner;
        u->hp = MAX_HP;
        u->pos = pos;
        u->pos.x = clampf(u->pos.x, UNIT_R, (float)WORLD_W - UNIT_R);
        u->pos.y = clampf(u->pos.y, UNIT_R, (float)WORLD_H - UNIT_R);
        if (!world_walkable(&g->w, u->pos.x, u->pos.y)) {
            /* nudge out of a mountain until we find ground */
            for (int k = 0; k < 24; k++) {
                float a = (float)k * 0.785398f;
                Vec2 p = v2(u->pos.x + cosf(a) * 18.0f, u->pos.y + sinf(a) * 18.0f);
                if (world_walkable(&g->w, p.x, p.y)) { u->pos = p; break; }
            }
        }
        return i;
    }
    return -1;
}

void game_init(Game *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->sel_city = -1;
    g->winner = -1;

    Vec2 pos[MAP_CITIES];
    int n = 0;
    world_generate(&g->w, seed, pos, &n);
    g->ncity = n;
    for (int i = 0; i < n; i++) {
        memset(&g->cities[i], 0, sizeof(City));
        g->cities[i].pos = pos[i];
        g->cities[i].owner = -1;
        g->cities[i].cap_owner = -1.0f;
    }

    /* Capitals: the two most opposite cities. */
    int cap0 = 0, cap1 = 0;
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < n; i++) {
        float s = pos[i].x + pos[i].y;
        if (s < lo) { lo = s; cap0 = i; }
        if (s > hi) { hi = s; cap1 = i; }
    }
    if (cap0 == cap1 && n > 1) cap1 = (cap0 + 1) % n;
    g->capital[0] = g->cities[cap0].pos;
    g->capital[1] = g->cities[cap1].pos;
    g->cities[cap0].owner = P_BLUE;
    g->cities[cap1].owner = P_RED;

    for (int side = 0; side < 2; side++) {
        int cap = side == 0 ? cap0 : cap1;
        for (int k = 0; k < 4; k++) {
            float a = (float)k * 1.5707963f + 0.4f;
            Vec2 p = v2(g->cities[cap].pos.x + cosf(a) * 20.0f,
                        g->cities[cap].pos.y + sinf(a) * 20.0f);
            game_spawn_unit(g, side, p);
        }
    }
    g->time = 0.0f;
    g->end_time = 0.0f;
    g->paused = 1;   /* wait for the player to press space */
    update_stats(g);
    game_update_vision(g);
}

/* ------------------------------------------------------------------ */
/* City ownership, capture and production                              */
/* ------------------------------------------------------------------ */

static int count_in_city(const Game *g, int city, int owner) {
    int count = 0;
    Vec2 c = g->cities[city].pos;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner != owner) continue;
        if (vdist(u->pos, c) <= CITY_CAPTURE_R) count++;
    }
    return count;
}

static void update_cities(Game *g, float dt) {
    for (int i = 0; i < g->ncity; i++) {
        City *c = &g->cities[i];
        int blue = count_in_city(g, i, P_BLUE);
        int red = count_in_city(g, i, P_RED);
        int contender = -1;
        int diff = blue - red;

        if (blue > 0 && red == 0) contender = P_BLUE;
        else if (red > 0 && blue == 0) contender = P_RED;
        else if (diff > 0) contender = P_BLUE;   /* majority holds the square */
        else if (diff < 0) contender = P_RED;

        if (contender >= 0 && c->owner != contender) {
            float rate = 1.0f + 0.25f * (float)(diff > 0 ? diff : -diff);
            if (rate > 2.0f) rate = 2.0f;
            c->cap += dt * rate / CAPTURE_TIME;
            c->cap_owner = (float)contender;
            if (c->cap >= 1.0f) {
                c->owner = contender;
                c->cap = 0.0f;
                c->cap_owner = -1.0f;
                c->spawn_t = 0.0f;
                c->has_rally = 0;
            }
        } else {
            c->cap = fmaxf(0.0f, c->cap - dt * 0.8f);
            if (c->cap <= 0.0f) c->cap_owner = -1.0f;
        }

        if (c->owner < 0) continue;

        int count = g->units_owned[c->owner];
        c->spawn_t += dt;
        float interval = PROD_BASE + PROD_PER_UNIT * (float)count;
        if (count < UNIT_CAP && c->spawn_t >= interval) {
            c->spawn_t = 0.0f;
            Vec2 p = v2(c->pos.x + rnd_range(&g->w.seed, -6.0f, 6.0f),
                        c->pos.y + rnd_range(&g->w.seed, -6.0f, 6.0f));
            int id = game_spawn_unit(g, c->owner, p);
            if (id >= 0 && c->has_rally) {
                Unit *u = &g->units[id];
                int rn = world_route(&g->w, c->pos, c->rally, u->path, MAX_PATH);
                if (rn > 0) {
                    u->plen = rn;
                    u->pi = 0;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Frame update                                                        */
/* ------------------------------------------------------------------ */

/* ---------------- fog of war --------------------------------------- */

static void vision_stamp(Game *g, int player, Vec2 pos, float radius) {
    int c0 = (int)clampf((pos.x - radius) / CELL, 0, COLS - 1);
    int c1 = (int)clampf((pos.x + radius) / CELL, 0, COLS - 1);
    int r0 = (int)clampf((pos.y - radius) / CELL, 0, ROWS - 1);
    int r1 = (int)clampf((pos.y + radius) / CELL, 0, ROWS - 1);
    for (int r = r0; r <= r1; r++) {
        float cy = (float)r * CELL + CELL * 0.5f;
        for (int c = c0; c <= c1; c++) {
            float cx = (float)c * CELL + CELL * 0.5f;
            float d = sqrtf((cx - pos.x) * (cx - pos.x) + (cy - pos.y) * (cy - pos.y));
            if (d >= radius) continue;
            float t = 1.0f - d / radius;
            uint8_t v = (uint8_t)(255.0f * smoothf(clampf(t * 1.4f, 0.0f, 1.0f)));
            if (v > g->vis[player][r][c]) g->vis[player][r][c] = v;
        }
    }
}

static void update_vision(Game *g, int player) {
    memset(g->vis[player], 0, sizeof(g->vis[player]));
    for (int i = 0; i < g->ncity; i++) {
        if (g->cities[i].owner == player)
            vision_stamp(g, player, g->cities[i].pos, 200.0f);
    }
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive || u->owner != player) continue;
        uint8_t t = world_terrain_at(&g->w, u->pos.x, u->pos.y);
        float radius = 105.0f;
        if (t == T_HILL) radius = 165.0f;
        else if (t == T_FOREST) radius = 80.0f;
        else if (t == T_WATER) radius = 90.0f;
        vision_stamp(g, player, u->pos, radius);
    }
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (g->vis[player][r][c] > g->seen[player][r][c])
                g->seen[player][r][c] = g->vis[player][r][c];
}

void game_update_vision(Game *g) {
    update_vision(g, P_BLUE);
    update_vision(g, P_RED);
}

static float grid_bilinear(const uint8_t grid[ROWS][COLS], float x, float y) {
    float gx = clampf(x / CELL - 0.5f, 0.0f, (float)(COLS - 1));
    float gy = clampf(y / CELL - 0.5f, 0.0f, (float)(ROWS - 1));
    int c = (int)gx, r = (int)gy;
    int c1 = c + 1 < COLS ? c + 1 : c;
    int r1 = r + 1 < ROWS ? r + 1 : r;
    float fx = gx - (float)c, fy = gy - (float)r;
    float a = (float)grid[r][c], b = (float)grid[r][c1];
    float d = (float)grid[r1][c], e = (float)grid[r1][c1];
    return lerpf(lerpf(a, b, fx), lerpf(d, e, fx), fy) / 255.0f;
}

float game_visibility_at(const Game *g, int player, float x, float y) {
    return grid_bilinear(g->vis[player], x, y);
}

float game_seen_at(const Game *g, int player, float x, float y) {
    return grid_bilinear(g->seen[player], x, y);
}

/* ------------------------------------------------------------------ */

static void update_stats(Game *g) {
    g->cities_owned[0] = g->cities_owned[1] = 0;
    g->units_owned[0] = g->units_owned[1] = 0;
    for (int i = 0; i < g->ncity; i++) {
        if (g->cities[i].owner >= 0) g->cities_owned[g->cities[i].owner]++;
    }
    for (int i = 0; i < MAX_UNITS; i++) {
        if (g->units[i].alive) g->units_owned[g->units[i].owner]++;
    }
}

void game_update(Game *g, float dt) {
    if (g->winner >= 0) {
        g->end_time += dt;
        return;
    }
    if (g->paused) return;

    g->time += dt;
    update_stats(g);
    update_cities(g, dt);
    units_update(g, dt);
    update_stats(g);
    game_update_vision(g);

    for (int p = 0; p < 2; p++) {
        if (g->units_owned[p] == 0 && g->cities_owned[p] == 0) {
            g->winner = 1 - p;
            g->end_time = 0.0f;
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Selection                                                           */
/* ------------------------------------------------------------------ */

void game_clear_selection(Game *g) {
    memset(g->selected, 0, sizeof(g->selected));
    g->nsel = 0;
    g->sel_city = -1;
}

void game_click_select(Game *g, Vec2 world, int additive) {
    int best = -1;
    float best_d = 16.0f;
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive || u->owner != P_BLUE) continue;
        float d = vdist(u->pos, world);
        if (d < best_d) { best = i; best_d = d; }
    }
    if (best >= 0) {
        if (!additive) memset(g->selected, 0, sizeof(g->selected));
        g->selected[best] = additive ? !g->selected[best] : 1;
        g->sel_city = -1;
    } else {
        int city = -1;
        for (int i = 0; i < g->ncity; i++) {
            if (vdist(g->cities[i].pos, world) <= CITY_R + 8.0f) { city = i; break; }
        }
        if (city >= 0) {
            memset(g->selected, 0, sizeof(g->selected));
            g->sel_city = city;
        } else if (!additive) {
            game_clear_selection(g);
        }
    }
    g->nsel = 0;
    for (int i = 0; i < MAX_UNITS; i++) g->nsel += g->selected[i] ? 1 : 0;
}

void game_box_select(Game *g, Vec2 a, Vec2 b, int additive) {
    float x0 = fminf(a.x, b.x), x1 = fmaxf(a.x, b.x);
    float y0 = fminf(a.y, b.y), y1 = fmaxf(a.y, b.y);
    if (!additive) memset(g->selected, 0, sizeof(g->selected));
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive || u->owner != P_BLUE) continue;
        if (u->pos.x >= x0 && u->pos.x <= x1 && u->pos.y >= y0 && u->pos.y <= y1)
            g->selected[i] = 1;
    }
    g->sel_city = -1;
    g->nsel = 0;
    for (int i = 0; i < MAX_UNITS; i++) g->nsel += g->selected[i] ? 1 : 0;
}

void game_select_all(Game *g) {
    int any = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        g->selected[i] = g->units[i].alive && g->units[i].owner == P_BLUE;
        any |= g->selected[i];
    }
    if (any) g->sel_city = -1;
    g->nsel = 0;
    for (int i = 0; i < MAX_UNITS; i++) g->nsel += g->selected[i] ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Orders                                                              */
/* ------------------------------------------------------------------ */

static int resample_path(const Vec2 *in, int n, Vec2 *out, int max) {
    if (n <= max) {
        memcpy(out, in, sizeof(Vec2) * (size_t)n);
        return n;
    }
    for (int i = 0; i < max; i++) {
        int idx = (int)((float)i * (float)(n - 1) / (float)(max - 1) + 0.5f);
        if (idx >= n) idx = n - 1;
        out[i] = in[idx];
    }
    return max;
}

static int line_clear(const Game *g, Vec2 a, Vec2 b) {
    float d = vdist(a, b);
    int steps = (int)(d / 8.0f) + 1;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        Vec2 p = v2(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t));
        if (world_terrain_at(&g->w, p.x, p.y) == T_MOUNTAIN) return 0;
    }
    return 1;
}

/* Turn a hand-drawn polyline into a mountain-free path. Clear segments are
 * kept exactly as drawn; blocked segments are replaced by a grid route. */
static int build_route(const Game *g, Vec2 start, const Vec2 *pts, int n,
                       Vec2 *out, int max) {
    int m = 0;
    Vec2 cur = start;
    for (int i = 0; i < n && m < max; i++) {
        Vec2 end = pts[i];
        if (vdist(cur, end) < 1.0f) continue;
        if (line_clear(g, cur, end)) {
            out[m++] = end;
        } else {
            Vec2 tmp[MAX_PATH];
            int rn = world_route(&g->w, cur, end, tmp, MAX_PATH);
            if (rn > 0) {
                for (int j = 0; j < rn && m < max; j++) out[m++] = tmp[j];
            } else {
                out[m++] = end;
            }
        }
        cur = end;
    }
    return m;
}

static void order_unit(Game *g, Unit *u, const Vec2 *base, int bn, Vec2 offset) {
    Vec2 route[MAX_PATH];
    int rn = build_route(g, u->pos, base, bn, route, MAX_PATH);
    if (rn <= 0) return;
    route[rn - 1] = vadd(route[rn - 1], offset);
    route[rn - 1].x = clampf(route[rn - 1].x, UNIT_R, (float)WORLD_W - UNIT_R);
    route[rn - 1].y = clampf(route[rn - 1].y, UNIT_R, (float)WORLD_H - UNIT_R);
    memcpy(u->path, route, sizeof(Vec2) * (size_t)rn);
    u->plen = rn;
    u->pi = 0;
}

void game_order_selected(Game *g, const Vec2 *pts, int n) {
    if (n <= 0) return;
    if (g->sel_city >= 0) {
        City *c = &g->cities[g->sel_city];
        if (c->owner == P_BLUE) {
            c->rally = pts[n - 1];
            c->has_rally = 1;
        }
        return;
    }
    Vec2 base[24];
    int bn = resample_path(pts, n, base, 24);
    int k = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!g->selected[i] || !g->units[i].alive) continue;
        float a = (float)k * 2.39996f;
        float r = 9.0f + (float)(k % 4) * 7.0f;
        order_unit(g, &g->units[i], base, bn, v2(cosf(a) * r, sinf(a) * r));
        k++;
    }
}

void game_set_city_rally(Game *g, Vec2 world) {
    if (g->sel_city < 0) return;
    City *c = &g->cities[g->sel_city];
    if (c->owner != P_BLUE) return;
    c->rally = world;
    c->has_rally = 1;
}
