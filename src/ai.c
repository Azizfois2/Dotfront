#include "ai.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Difficulty profiles                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    float react;          /* seconds between decisions */
    int   wave_base;      /* idle units before a push */
    int   wave_per_city;  /* extra units per owned city */
    int   wave_cap;
    int   defend_units;   /* idle units sent to a threatened city */
    float defend_radius;  /* how far attackers are noticed */
} AiProfile;

/* Easy is slow and hesitant, normal is the original pace, hard reacts fast
 * and keeps a larger defence. */
static const AiProfile PROFILES[DIFF_COUNT] = {
    { 1.80f, 4, 1,  9, 2, 160.0f },
    { 0.75f, 3, 1,  8, 4, 210.0f },
    { 0.45f, 3, 1, 10, 6, 280.0f },
};

static const AiProfile *profile_for(const Game *g, int p) {
    int d = g->difficulty[p];
    if (d < 0 || d >= DIFF_COUNT) d = DIFF_NORMAL;
    return &PROFILES[d];
}

/* ------------------------------------------------------------------ */
/* Movement helpers                                                    */
/* ------------------------------------------------------------------ */

static void give_order(Game *g, Unit *u, Vec2 target, int slot, int order) {
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
    u->order = (uint8_t)order;
}

static Vec2 army_center(const Game *g, int p, Vec2 *anchor) {
    Vec2 sum = v2(0, 0);
    int n = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner != p) continue;
        sum = vadd(sum, u->pos);
        if (n == 0) *anchor = u->pos;
        n++;
    }
    return n ? vmul(sum, 1.0f / (float)n) : g->capital[p];
}

static int idle_units(const Game *g, int p) {
    int n = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (u->alive && u->owner == p && u->pi >= u->plen) n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Fog-aware scouting and targeting                                    */
/* ------------------------------------------------------------------ */

static int visible_enemies_near(const Game *g, int p, Vec2 c, float radius) {
    int n = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner == p) continue;
        if (vdist(u->pos, c) > radius) continue;
        if (game_visibility_at(g, p, u->pos.x, u->pos.y) < 0.25f) continue;
        n++;
    }
    return n;
}

static int pick_threatened_city(const Game *g, int p, const AiProfile *pr) {
    int best = -1, best_n = 0;
    for (int i = 0; i < g->ncity; i++) {
        if (g->cities[i].owner != p) continue;
        int n = visible_enemies_near(g, p, g->cities[i].pos, pr->defend_radius);
        if (n > best_n) { best_n = n; best = i; }
    }
    return best;
}

/* Choose a city the AI has actually seen. The remembered owner is used so a
 * city that changed hands in the fog is not assumed to still be an enemy. */
static int pick_target_city(const Game *g, int p, Vec2 from) {
    int best = -1;
    float best_score = 1e30f;
    for (int i = 0; i < g->ncity; i++) {
        int known = game_known_city_owner(g, p, i);
        if (known == -2 || known == p) continue;
        float d = vdist(from, g->cities[i].pos);
        float score = d * (known < 0 ? 0.85f : 1.0f);
        if (score < best_score) { best_score = score; best = i; }
    }
    return best;
}

static int pick_visible_enemy(const Game *g, int p, Vec2 from) {
    int best = -1;
    float best_d = 1e30f;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive || u->owner == p) continue;
        if (game_visibility_at(g, p, u->pos.x, u->pos.y) < 0.25f) continue;
        float d = vdist(from, u->pos);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

/* Nearest patch of map the AI has never seen, preferring points it can
 * actually path to. A few candidates are tested so a mountain or a separate
 * island does not stall the whole army. */
static int pick_explore_point(const Game *g, int p, Vec2 from, Vec2 anchor, Vec2 *out) {
    Vec2 cand[6];
    float dist[6];
    int nc = 0;

    for (int r = 1; r < ROWS; r += 2) {
        for (int c = 1; c < COLS; c += 2) {
            float x = ((float)c + 0.5f) * (float)CELL;
            float y = ((float)r + 0.5f) * (float)CELL;
            if (!world_walkable(&g->w, x, y)) continue;
            if (game_seen_at(g, p, x, y) > 0.22f) continue;
            float d = vdist(from, v2(x, y));
            if (nc < 6) {
                cand[nc] = v2(x, y);
                dist[nc] = d;
                nc++;
            } else {
                int worst = 0;
                for (int k = 1; k < 6; k++)
                    if (dist[k] > dist[worst]) worst = k;
                if (d < dist[worst]) { cand[worst] = v2(x, y); dist[worst] = d; }
            }
        }
    }

    Vec2 route[MAX_PATH];
    for (int k = 0; k < nc; k++) {
        if (world_route(&g->w, anchor, cand[k], route, MAX_PATH) > 0) {
            *out = cand[k];
            return 1;
        }
    }
    return 0;
}

/* Each city keeps a production role; higher difficulties field a mix. */
static void choose_production(Game *g, int p, int diff) {
    for (int i = 0; i < g->ncity; i++) {
        City *c = &g->cities[i];
        if (c->owner != p) continue;
        int t = UT_SOLDIER;
        if (diff == DIFF_NORMAL && i % 3 == 0) t = UT_SCOUT;
        if (diff == DIFF_HARD) {
            if (i % 5 == 0) t = UT_HEAVY;
        }
        c->prod_type = (uint8_t)t;
    }
}

/* ------------------------------------------------------------------ */
/* Main decision loop                                                  */
/* ------------------------------------------------------------------ */

void ai_update(Game *g, int p, float dt) {
    if (g->winner >= 0 || g->paused || p < 0 || p > 1) return;
    const AiProfile *pr = profile_for(g, p);
    int diff = g->difficulty[p];
    if (diff < 0 || diff >= DIFF_COUNT) diff = DIFF_NORMAL;
    choose_production(g, p, diff);

    g->ai_timer[p] -= dt;
    if (g->ai_timer[p] > 0.0f) return;
    g->ai_timer[p] = pr->react;

    Vec2 anchor = g->capital[p];
    Vec2 center = army_center(g, p, &anchor);
    if (idle_units(g, p) == 0 && g->units_owned[p] == 0) return;

    /* 1) defend a city the AI can actually see being attacked */
    int threat = pick_threatened_city(g, p, pr);
    if (threat >= 0) {
        Vec2 target = g->cities[threat].pos;
        int sent = 0;
        for (int i = 0; i < MAX_UNITS && sent < pr->defend_units; i++) {
            Unit *u = &g->units[i];
            if (!u->alive || u->owner != p) continue;
            if (u->pi < u->plen) continue;
            if (vdist(u->pos, target) > 520.0f) continue;
            give_order(g, u, target, sent, ORD_MOVE);
            sent++;
        }
    }

    /* 2) gather a wave, then attack a remembered city or scout */
    int idle = idle_units(g, p);
    int wave = pr->wave_base + pr->wave_per_city * g->cities_owned[p];
    if (wave > pr->wave_cap) wave = pr->wave_cap;
    g->ai_wave[p] = wave;
    if (idle < wave) return;

    Vec2 goal;
    int have_goal = 0;

    int target = pick_target_city(g, p, center);
    if (target >= 0) {
        goal = g->cities[target].pos;
        have_goal = 1;
    } else {
        int enemy = pick_visible_enemy(g, p, center);
        if (enemy >= 0) {
            goal = g->units[enemy].pos;
            have_goal = 1;
        } else if (pick_explore_point(g, p, center, anchor, &goal)) {
            have_goal = 1;
        }
    }
    if (!have_goal) return;

    int slot = 0;
    for (int i = 0; i < MAX_UNITS && slot < wave; i++) {
        Unit *u = &g->units[i];
        if (!u->alive || u->owner != p) continue;
        if (u->pi < u->plen) continue; /* already busy */
        give_order(g, u, goal, slot++, ORD_MOVE);
    }
}
