#include "ai.h"
#include <string.h>

static void give_order(Game *g, Unit *u, Vec2 target, int slot) {
    Vec2 pts[MAX_PATH];
    int n = world_route(&g->w, u->pos, target, pts, MAX_PATH - 1);
    if (n <= 0) {
        pts[0] = target;
        n = 1;
    }
    float a = (float)slot * 2.39996f;
    float r = 8.0f + (float)(slot % 4) * 8.0f;
    int last = n - 1;
    pts[last] = v2(clampf(pts[last].x + cosf(a) * r, UNIT_R, (float)WORLD_W - UNIT_R),
                   clampf(pts[last].y + sinf(a) * r, UNIT_R, (float)WORLD_H - UNIT_R));
    memcpy(u->path, pts, sizeof(Vec2) * (size_t)n);
    u->plen = n;
    u->pi = 0;
}

static int enemy_near_city(const Game *g, int city, int owner, float radius) {
    int count = 0;
    Vec2 c = g->cities[city].pos;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner == owner) continue;
        if (vdist(u->pos, c) < radius) count++;
    }
    return count;
}

static int pick_threatened_city(const Game *g, int p) {
    int best = -1, best_n = 0;
    for (int i = 0; i < g->ncity; i++) {
        if (g->cities[i].owner != p) continue;
        int n = enemy_near_city(g, i, p, 200.0f);
        if (n > best_n) { best_n = n; best = i; }
    }
    return best;
}

static int pick_target_city(const Game *g, int p, Vec2 from) {
    int best = -1;
    float best_score = 1e30f;
    for (int i = 0; i < g->ncity; i++) {
        if (g->cities[i].owner == p) continue;
        float d = vdist(from, g->cities[i].pos);
        float score = d * (g->cities[i].owner < 0 ? 0.90f : 1.0f);
        if (score < best_score) { best_score = score; best = i; }
    }
    return best;
}

static int idle_units(const Game *g, int p) {
    int n = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner != p) continue;
        if (u->pi >= u->plen) n++;
    }
    return n;
}

void ai_update(Game *g, int p, float dt) {
    if (g->winner >= 0 || g->paused) return;
    g->ai_timer[p] -= dt;
    if (g->ai_timer[p] > 0.0f) return;
    g->ai_timer[p] = 0.75f;

    Vec2 home = g->capital[p];
    int units_home = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        if (g->units[i].alive && g->units[i].owner == p) { home = g->units[i].pos; units_home++; break; }
    }
    if (units_home == 0) home = g->capital[p];

    /* 1) keep a small garrison: send only nearby idle units, then keep
     *    pushing with the rest so the army never oscillates forever. */
    int threat = pick_threatened_city(g, p);
    if (threat >= 0) {
        Vec2 target = g->cities[threat].pos;
        int ordered = 0;
        for (int i = 0; i < MAX_UNITS && ordered < 4; i++) {
            Unit *u = &g->units[i];
            if (!u->alive || u->owner != p) continue;
            if (u->pi < u->plen) continue;
            if (vdist(u->pos, target) > 480.0f) continue;
            give_order(g, u, target, ordered);
            ordered++;
        }
    }

    /* 2) attack when enough idle units are gathered */
    int idle = idle_units(g, p);
    int wave = 3 + g->cities_owned[p];
    if (wave > 8) wave = 8;
    g->ai_wave[p] = wave;
    if (idle < wave) return;

    int target = pick_target_city(g, p, home);
    if (target < 0) return;
    Vec2 tpos = g->cities[target].pos;
    int slot = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive || u->owner != p) continue;
        if (u->pi < u->plen) continue; /* already busy */
        give_order(g, u, tpos, slot++);
    }
}
