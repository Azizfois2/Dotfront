#include "game.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static void update_stats(Game *g);

int game_spawn_unit(Game *g, int owner, Vec2 pos, int type) {
    if (type < 0 || type >= UT_COUNT) type = UT_SOLDIER;
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (u->alive) continue;
        memset(u, 0, sizeof(*u));
        u->alive = 1;
        u->owner = owner;
        u->type = (uint8_t)type;
        u->order = ORD_MOVE;
        u->max_hp = UNIT_TYPE[type].hp;
        u->hp = u->max_hp;
        u->pos = pos;
        u->pos.x = clampf(u->pos.x, UNIT_R, (float)g->w.width - UNIT_R);
        u->pos.y = clampf(u->pos.y, UNIT_R, (float)g->w.height - UNIT_R);
        if (!world_walkable(&g->w, u->pos.x, u->pos.y)) {
            /* nudge out of a mountain until we find ground */
            for (int k = 0; k < 24; k++) {
                float a = (float)k * 0.785398f;
                Vec2 p = v2(u->pos.x + cosf(a) * 18.0f, u->pos.y + sinf(a) * 18.0f);
                if (world_walkable(&g->w, p.x, p.y)) { u->pos = p; break; }
            }
        }
        g->built[owner]++;
        return i;
    }
    return -1;
}

void game_init(Game *g, uint32_t seed, int players) {
    memset(g, 0, sizeof(*g));
    if (players < 2) players = 2;
    if (players > MAX_PLAYERS) players = MAX_PLAYERS;
    g->players = players;
    g->local = P_BLUE;
    g->sel_city = -1;
    g->winner = -1;
    g->menu = 1;
    g->sound_on = 1;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        g->difficulty[p] = DIFF_NORMAL;
        g->ai_enabled[p] = (p != g->local);
        for (int i = 0; i < MAX_CITIES; i++) g->city_last_owner[p][i] = -2;
    }
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++) g->terr_owner[r][c] = -1;

    Vec2 pos[MAX_CITIES];
    int n = 0;
    world_generate(&g->w, seed, players, pos, &n);
    g->ncity = n;
    for (int i = 0; i < n; i++) {
        memset(&g->cities[i], 0, sizeof(City));
        g->cities[i].pos = pos[i];
        g->cities[i].owner = -1;
        g->cities[i].cap_owner = -1.0f;
        g->cities[i].prod_type = UT_SOLDIER;
    }

    /* The human faction starts at the city closest to the map centre, so the
     * opening camera view is genuinely centred on the player. The AI factions
     * are then seeded from the farthest city and spread with greedy
     * farthest-point sampling. */
    int chosen[MAX_PLAYERS] = { 0 };
    int used[MAX_CITIES] = { 0 };
    Vec2 mid = v2((float)g->w.width * 0.5f, (float)g->w.height * 0.5f);
    int a = 0;
    float best_center = 1e30f;
    for (int i = 0; i < n; i++) {
        float d = vdist(pos[i], mid);
        if (d < best_center) { best_center = d; a = i; }
    }
    chosen[0] = a;
    used[a] = 1;
    if (players > 1) {
        int b = 0;
        float far = -1.0f;
        for (int i = 0; i < n; i++) {
            if (used[i]) continue;
            float d = vdist(pos[i], pos[a]);
            if (d > far) { far = d; b = i; }
        }
        chosen[1] = b;
        used[b] = 1;
    }
    for (int k = 2; k < players; k++) {
        int pick = -1;
        float best = -1.0f;
        for (int i = 0; i < n; i++) {
            if (used[i]) continue;
            float nearest = 1e30f;
            for (int j = 0; j < k; j++) {
                float d = vdist(pos[i], pos[chosen[j]]);
                if (d < nearest) nearest = d;
            }
            if (nearest > best) { best = nearest; pick = i; }
        }
        if (pick < 0) pick = k % n;
        chosen[k] = pick;
        used[pick] = 1;
    }

    for (int p = 0; p < players; p++) {
        int cap = chosen[p];
        g->capital[p] = g->cities[cap].pos;
        g->cities[cap].owner = p;
        for (int k = 0; k < 4; k++) {
            float ang = (float)k * 1.5707963f + 0.4f + (float)p * 0.17f;
            Vec2 spawn = v2(g->cities[cap].pos.x + cosf(ang) * 20.0f,
                            g->cities[cap].pos.y + sinf(ang) * 20.0f);
            game_spawn_unit(g, p, spawn, UT_SOLDIER);
        }
    }
    g->time = 0.0f;
    g->end_time = 0.0f;
    g->paused = 1;   /* wait for the player to press space */
    update_stats(g);
    game_update_vision(g);
    game_update_territory(g);
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

int game_unit_cap(const Game *g) {
    int n = g->players > 0 ? g->players : 2;
    int cap = MAX_UNITS / n;
    if (cap > UNIT_CAP) cap = UNIT_CAP;
    if (cap < 14) cap = 14;
    return cap;
}

static void update_cities(Game *g, float dt) {
    int cap = game_unit_cap(g);
    int owned_count[MAX_PLAYERS];
    for (int p = 0; p < MAX_PLAYERS; p++) owned_count[p] = g->units_owned[p];
    for (int i = 0; i < g->ncity; i++) {
        City *c = &g->cities[i];
        int counts[MAX_PLAYERS] = { 0 };
        for (int p = 0; p < g->players; p++) counts[p] = count_in_city(g, i, p);

        int best = -1, best_n = 0, second_n = 0;
        for (int p = 0; p < g->players; p++) {
            if (counts[p] > best_n) {
                second_n = best_n;
                best_n = counts[p];
                best = p;
            } else if (counts[p] > second_n) {
                second_n = counts[p];
            }
        }
        int contender = -1;
        int diff = 0;
        if (best_n > 0 && best != c->owner) {
            int held = (c->owner >= 0 && c->owner < g->players) ? counts[c->owner] : 0;
            if (best_n > held) {
                contender = best;
                diff = best_n - (held > second_n ? held : second_n);
                if (diff < 1) diff = 1;
            }
        }

        if (contender >= 0 && c->owner != contender) {
            float rate = 1.0f + 0.25f * (float)diff;
            if (rate > 2.0f) rate = 2.0f;
            if (c->fortified) rate *= 0.68f;   /* walls slow the flip */
            c->cap += dt * rate / CAPTURE_TIME;
            c->cap_owner = (float)contender;
            if (c->cap >= 1.0f) {
                c->owner = contender;
                c->cap = 0.0f;
                c->cap_owner = -1.0f;
                c->spawn_t = 0.0f;
                c->has_rally = 0;
                c->prod_type = UT_SOLDIER;
                c->qlen = 0;
                c->fortified = 0;
                c->fort_t = 0.0f;
                g->captures[contender]++;
                g->sfx |= (uint16_t)SFX_CAPTURE;
            }
        } else {
            c->cap = fmaxf(0.0f, c->cap - dt * 0.8f);
            if (c->cap <= 0.0f) c->cap_owner = -1.0f;
        }

        if (c->fort_t > 0.0f) {
            c->fort_t -= dt;
            if (c->fort_t <= 0.0f) {
                c->fort_t = 0.0f;
                c->fortified = 1;
                g->sfx |= (uint16_t)SFX_CAPTURE;
            }
        }
        if (c->owner < 0 || c->fort_t > 0.0f) continue;

        int count = owned_count[c->owner];
        int pt = c->prod_type;
        if (pt < 0 || pt >= UT_COUNT) pt = UT_SOLDIER;
        c->spawn_t += dt;
        float interval = (PROD_BASE + PROD_PER_UNIT * (float)count) *
                         UNIT_TYPE[pt].build_time;
        if (count < cap && c->spawn_t >= interval) {
            c->spawn_t = 0.0f;
            Vec2 p = v2(c->pos.x + rnd_range(&g->w.seed, -6.0f, 6.0f),
                        c->pos.y + rnd_range(&g->w.seed, -6.0f, 6.0f));
            int id = game_spawn_unit(g, c->owner, p, pt);
            if (id >= 0) owned_count[c->owner]++;
            if (id >= 0 && c->qlen > 0) {
                c->prod_type = c->queue[0];
                for (int q = 1; q < c->qlen; q++) c->queue[q - 1] = c->queue[q];
                c->qlen--;
            }
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
    int c0 = (int)clampf((pos.x - radius) / CELL, 0, g->w.cols - 1);
    int c1 = (int)clampf((pos.x + radius) / CELL, 0, g->w.cols - 1);
    int r0 = (int)clampf((pos.y - radius) / CELL, 0, g->w.rows - 1);
    int r1 = (int)clampf((pos.y + radius) / CELL, 0, g->w.rows - 1);
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
        if (g->cities[i].owner == player) {
            float r = 200.0f + (g->cities[i].fortified ? 70.0f : 0.0f);
            vision_stamp(g, player, g->cities[i].pos, r);
        }
    }
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive || u->owner != player) continue;
        uint8_t t = world_terrain_at(&g->w, u->pos.x, u->pos.y);
        int ut = u->type;
        if (ut < 0 || ut >= UT_COUNT) ut = UT_SOLDIER;
        float radius = UNIT_TYPE[ut].vision;
        if (t == T_HILL) radius *= 1.50f;
        else if (t == T_FOREST) radius *= 0.76f;
        else if (t == T_WATER) radius *= 0.86f;
        vision_stamp(g, player, u->pos, radius);
    }
    for (int r = 0; r < g->w.rows; r++)
        for (int c = 0; c < g->w.cols; c++)
            if (g->vis[player][r][c] > g->seen[player][r][c])
                g->seen[player][r][c] = g->vis[player][r][c];

    /* remember the last owner seen for each city (stale intel is still intel) */
    for (int i = 0; i < g->ncity; i++) {
        int c = (int)clampf(g->cities[i].pos.x / CELL, 0, g->w.cols - 1);
        int r = (int)clampf(g->cities[i].pos.y / CELL, 0, g->w.rows - 1);
        if (g->vis[player][r][c] > 26) {
            g->city_known[player][i] = 1;
            g->city_last_owner[player][i] = (int8_t)g->cities[i].owner;
        }
    }
}

void game_update_vision(Game *g) {
    for (int p = 0; p < g->players; p++) update_vision(g, p);
}

/* City influence makes the territory map. Overlapping cities are resolved by
 * distance; neutral cells get -1. Rebuilt a few times per second. */
void game_update_territory(Game *g) {
    /* Voronoi-style territory: the nearest owned city claims each cell, with
     * a terrain-derived wobble so borders bend around hills and forests
     * instead of forming perfect circles. */
    float claim_r = TERRITORY_RADIUS * 1.62f;
    for (int r = 0; r < g->w.rows; r++) {
        for (int c = 0; c < g->w.cols; c++) {
            float x = ((float)c + 0.5f) * CELL;
            float y = ((float)r + 0.5f) * CELL;
            float wobble = (world_elev_at(&g->w, x, y) - 0.5f) * 58.0f
                         + (world_forest_at(&g->w, x, y) - 0.5f) * 26.0f;
            int best = -1;
            float best_d = claim_r;
            for (int i = 0; i < g->ncity; i++) {
                const City *city = &g->cities[i];
                if (city->owner < 0) continue;
                float dx = x - city->pos.x;
                float dy = y - city->pos.y;
                float d = sqrtf(dx * dx + dy * dy) + wobble;
                if (d < best_d) {
                    best_d = d;
                    best = city->owner;
                }
            }
            g->terr_owner[r][c] = (int8_t)best;
        }
    }
    g->terr_t = 0.4f;
}

int game_territory_at(const Game *g, float x, float y) {
    int c = (int)clampf(x / CELL, 0, g->w.cols - 1);
    int r = (int)clampf(y / CELL, 0, g->w.rows - 1);
    return g->terr_owner[r][c];
}

static float grid_bilinear(const Game *g, const uint8_t grid[MAX_ROWS][MAX_COLS],
                           float x, float y) {
    float gx = clampf(x / CELL - 0.5f, 0.0f, (float)(g->w.cols - 1));
    float gy = clampf(y / CELL - 0.5f, 0.0f, (float)(g->w.rows - 1));
    int c = (int)gx, r = (int)gy;
    int c1 = c + 1 < g->w.cols ? c + 1 : c;
    int r1 = r + 1 < g->w.rows ? r + 1 : r;
    float fx = gx - (float)c, fy = gy - (float)r;
    float a = (float)grid[r][c], b = (float)grid[r][c1];
    float d = (float)grid[r1][c], e = (float)grid[r1][c1];
    return lerpf(lerpf(a, b, fx), lerpf(d, e, fx), fy) / 255.0f;
}

float game_visibility_at(const Game *g, int player, float x, float y) {
    return grid_bilinear(g, g->vis[player], x, y);
}

float game_seen_at(const Game *g, int player, float x, float y) {
    return grid_bilinear(g, g->seen[player], x, y);
}

int game_known_city_owner(const Game *g, int player, int city) {
    if (city < 0 || city >= g->ncity || !g->city_known[player][city]) return -2;
    return g->city_last_owner[player][city];
}

int game_known_city_count(const Game *g, int player, int owner) {
    int n = 0;
    for (int i = 0; i < g->ncity; i++)
        if (game_known_city_owner(g, player, i) == owner) n++;
    return n;
}

int game_visible_unit_count(const Game *g, int player, int owner) {
    int n = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner != owner) continue;
        if (owner == player || game_visibility_at(g, player, u->pos.x, u->pos.y) >= 0.25f) n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */

static void update_stats(Game *g) {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        g->cities_owned[p] = 0;
        g->units_owned[p] = 0;
    }
    for (int i = 0; i < g->ncity; i++) {
        int o = g->cities[i].owner;
        if (o >= 0 && o < MAX_PLAYERS) g->cities_owned[o]++;
    }
    for (int i = 0; i < MAX_UNITS; i++) {
        if (g->units[i].alive) {
            int o = g->units[i].owner;
            if (o < 0 || o >= MAX_PLAYERS) continue;
            g->units_owned[o]++;
            if (g->units_owned[o] > g->peak[o]) g->peak[o] = g->units_owned[o];
        }
    }
}

void game_update(Game *g, float dt) {
    if (g->notice_t > 0.0f) g->notice_t -= dt;
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
    g->terr_t -= dt;
    if (g->terr_t <= 0.0f) game_update_territory(g);

    int alive = 0, last = -1;
    for (int p = 0; p < g->players; p++) {
        if (g->units_owned[p] > 0 || g->cities_owned[p] > 0) { alive++; last = p; }
    }
    if (alive <= 1) {
        g->winner = alive == 1 ? last : -2;
        g->end_time = 0.0f;
        g->sfx |= (uint16_t)SFX_WIN;
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
    /* Prefer the city when the click lands on its disc. Units standing on the
     * city can still be box-selected or picked by clicking beside it. */
    int city = -1;
    for (int i = 0; i < g->ncity; i++) {
        if (vdist(g->cities[i].pos, world) <= CITY_R + 8.0f) { city = i; break; }
    }
    if (city >= 0) {
        if (!additive) memset(g->selected, 0, sizeof(g->selected));
        g->sel_city = city;
    } else {
        int best = -1;
        float best_d = 16.0f;
        for (int i = 0; i < MAX_UNITS; i++) {
            Unit *u = &g->units[i];
            if (!u->alive || u->owner != g->local) continue;
            float d = vdist(u->pos, world);
            if (d < best_d) { best = i; best_d = d; }
        }
        if (best >= 0) {
            if (!additive) memset(g->selected, 0, sizeof(g->selected));
            g->selected[best] = additive ? !g->selected[best] : 1;
            g->sel_city = -1;
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
        if (!u->alive || u->owner != g->local) continue;
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
        g->selected[i] = g->units[i].alive && g->units[i].owner == g->local;
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
    route[rn - 1].x = clampf(route[rn - 1].x, UNIT_R, (float)g->w.width - UNIT_R);
    route[rn - 1].y = clampf(route[rn - 1].y, UNIT_R, (float)g->w.height - UNIT_R);
    memcpy(u->path, route, sizeof(Vec2) * (size_t)rn);
    u->plen = rn;
    u->pi = 0;
}

void game_order_selected(Game *g, const Vec2 *pts, int n) {
    if (n <= 0) return;
    if (g->sel_city >= 0) {
        City *c = &g->cities[g->sel_city];
        if (c->owner == g->local) {
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
        g->units[i].order = ORD_MOVE;
        k++;
    }
}

void game_set_city_rally(Game *g, Vec2 world) {
    if (g->sel_city < 0) return;
    City *c = &g->cities[g->sel_city];
    if (c->owner != g->local) return;
    c->rally = world;
    c->has_rally = 1;
}


/* ------------------------------------------------------------------ */
/* Attack-move / hold / stop                                           */
/* ------------------------------------------------------------------ */

static void order_selected_to(Game *g, Vec2 target, int order) {
    int k = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!g->selected[i] || !u->alive) continue;
        float a = (float)k * 2.39996f;
        float r = 9.0f + (float)(k % 4) * 7.0f;
        Vec2 base[1] = { target };
        order_unit(g, u, base, 1, v2(cosf(a) * r, sinf(a) * r));
        u->order = (uint8_t)order;
        k++;
    }
}

void game_order_attack_move(Game *g, Vec2 target) {
    order_selected_to(g, target, ORD_ATTACK_MOVE);
}

void game_order_hold(Game *g) {
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!g->selected[i] || !g->units[i].alive) continue;
        g->units[i].plen = g->units[i].pi = 0;
        g->units[i].order = ORD_HOLD;
    }
}

void game_order_stop(Game *g) {
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!g->selected[i] || !g->units[i].alive) continue;
        g->units[i].plen = g->units[i].pi = 0;
        g->units[i].order = ORD_MOVE;
    }
}

void game_fortify_city(Game *g) {
    if (g->sel_city < 0) return;
    City *c = &g->cities[g->sel_city];
    if (c->owner != g->local || c->fortified || c->fort_t > 0.0f) return;
    c->fort_t = 10.0f;
    game_notice(g, "FORTIFYING CITY - 10s");
}

void game_set_city_production(Game *g, int type) {
    if (g->sel_city < 0 || type < 0 || type >= UT_COUNT) return;
    City *c = &g->cities[g->sel_city];
    if (c->owner != g->local) return;
    c->prod_type = (uint8_t)type;
    char buf[48];
    snprintf(buf, sizeof(buf), "CITY PRODUCING %s", UNIT_TYPE[type].name);
    game_notice(g, buf);
}

void game_queue_city_production(Game *g, int type) {
    if (g->sel_city < 0 || type < 0 || type >= UT_COUNT) return;
    City *c = &g->cities[g->sel_city];
    if (c->owner != g->local) return;
    if (c->qlen >= CITY_QUEUE_MAX) {
        game_notice(g, "PRODUCTION QUEUE FULL");
        return;
    }
    c->queue[c->qlen++] = (uint8_t)type;
    char buf[48];
    snprintf(buf, sizeof(buf), "QUEUED %s (%d)", UNIT_TYPE[type].name, c->qlen);
    game_notice(g, buf);
}

/* ------------------------------------------------------------------ */
/* Save / load                                                         */
/* ------------------------------------------------------------------ */

#define SAVE_MAGIC   0x574F4443u   /* WODC */
#define SAVE_VERSION 8u

void game_notice(Game *g, const char *text) {
    snprintf(g->notice, sizeof(g->notice), "%s", text);
    g->notice_t = 2.5f;
}

int game_save(const Game *g, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t magic = SAVE_MAGIC, version = SAVE_VERSION, size = (uint32_t)sizeof(Game);
    int ok = fwrite(&magic, 4, 1, f) == 1 &&
             fwrite(&version, 4, 1, f) == 1 &&
             fwrite(&size, 4, 1, f) == 1 &&
             fwrite(g, sizeof(Game), 1, f) == 1;
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static int valid_saved_game(const Game *g) {
    if (g->ncity < 1 || g->ncity > MAX_CITIES) return 0;
    if (g->players < 2 || g->players > MAX_PLAYERS) return 0;
    if (g->local < 0 || g->local >= g->players) return 0;
    if (g->w.cols < 1 || g->w.cols > MAX_COLS) return 0;
    if (g->w.rows < 1 || g->w.rows > MAX_ROWS) return 0;
    for (int p = 0; p < g->players; p++)
        if (g->difficulty[p] < 0 || g->difficulty[p] >= DIFF_COUNT) return 0;
    if (g->winner < -2 || g->winner >= g->players) return 0;
    if (g->eliminated < 0 || g->eliminated > 1) return 0;
    if (g->spectator < 0 || g->spectator > 1) return 0;
    if (!isfinite(g->time) || g->time < 0.0f) return 0;
    for (int i = 0; i < g->ncity; i++) {
        const City *c = &g->cities[i];
        if (c->owner < -1 || c->owner >= g->players) return 0;
        if (c->prod_type >= UT_COUNT) return 0;
        if (c->qlen > CITY_QUEUE_MAX) return 0;
        for (int q = 0; q < c->qlen; q++)
            if (c->queue[q] >= UT_COUNT) return 0;
        if (c->fortified > 1) return 0;
        if (!isfinite(c->fort_t) || c->fort_t < 0.0f) return 0;
        if (!isfinite(c->pos.x) || !isfinite(c->pos.y)) return 0;
    }
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive) continue;
        if (u->owner < 0 || u->owner >= g->players) return 0;
        if (u->type >= UT_COUNT || u->order >= ORD_COUNT) return 0;
        if (!isfinite(u->pos.x) || !isfinite(u->pos.y) || !isfinite(u->hp)) return 0;
    }
    return 1;
}

int game_load(Game *g, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t magic = 0, version = 0, size = 0;
    int ok = fread(&magic, 4, 1, f) == 1 &&
             fread(&version, 4, 1, f) == 1 &&
             fread(&size, 4, 1, f) == 1;
    if (!ok || magic != SAVE_MAGIC || version != SAVE_VERSION ||
        size != (uint32_t)sizeof(Game)) {
        fclose(f);
        return 0;
    }
    Game tmp;
    ok = fread(&tmp, sizeof(Game), 1, f) == 1;
    fclose(f);
    if (!ok || !valid_saved_game(&tmp)) return 0;
    *g = tmp;
    g->menu = 0;
    g->save_available = 1;
    g->terr_t = 0.0f;
    for (int i = 0; i < MAX_UNITS; i++) g->selected[i] = 0;
    g->nsel = 0;
    g->sel_city = -1;
    g->sfx = 0;
    game_notice(g, "MATCH LOADED");
    return 1;
}

int game_save_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}
