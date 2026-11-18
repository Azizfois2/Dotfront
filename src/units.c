#include "game.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Movement, supply and combat for every living unit.                 */
/* ------------------------------------------------------------------ */

static float nearest_owned_city_dist(const Game *g, int owner, Vec2 pos) {
    float best = 1e30f;
    for (int i = 0; i < g->ncity; i++) {
        if (g->cities[i].owner != owner) continue;
        float d = vdist(pos, g->cities[i].pos);
        if (d < best) best = d;
    }
    return best;
}

static void move_unit(Game *g, Unit *u, float dt) {
    if (u->pi >= u->plen) return;
    Vec2 target = u->path[u->pi];
    float d = vdist(u->pos, target);
    float speed = UNIT_SPEED * TERRAIN_SPEED[world_terrain_at(&g->w, u->pos.x, u->pos.y)];
    if (speed <= 0.0f) return;
    if (d < 7.0f) {
        u->pi++;
        return;
    }
    float step = speed * dt;
    if (step > d) step = d;
    Vec2 dir = vnorm(vsub(target, u->pos));
    Vec2 np = vadd(u->pos, vmul(dir, step));
    if (world_walkable(&g->w, np.x, np.y)) {
        u->pos = np;
        return;
    }
    /* Slide along mountains instead of sticking to them. */
    Vec2 nx = v2(np.x, u->pos.y), ny = v2(u->pos.x, np.y);
    if (world_walkable(&g->w, nx.x, nx.y)) u->pos = nx;
    else if (world_walkable(&g->w, ny.x, ny.y)) u->pos = ny;
    else u->pi++; /* blocked by a mountain: skip this waypoint */
}

static void separate_units(Game *g) {
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *a = &g->units[i];
        if (!a->alive) continue;
        for (int j = i + 1; j < MAX_UNITS; j++) {
            Unit *b = &g->units[j];
            if (!b->alive) continue;
            float gap = (a->owner == b->owner) ? ALLY_GAP : ENEMY_GAP;
            float d = vdist(a->pos, b->pos);
            if (d >= gap || d < 1e-4f) continue;
            Vec2 n = vnorm(vsub(b->pos, a->pos));
            float push = (gap - d) * 0.5f;
            Vec2 na = vsub(a->pos, vmul(n, push));
            Vec2 nb = vadd(b->pos, vmul(n, push));
            if (world_walkable(&g->w, na.x, na.y)) a->pos = na;
            if (world_walkable(&g->w, nb.x, nb.y)) b->pos = nb;
        }
    }
}

void units_update(Game *g, float dt) {
    float dmg[MAX_UNITS];
    memset(dmg, 0, sizeof(dmg));

    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive) continue;

        if (u->flash > 0.0f) u->flash -= dt;

        /* supply: heal near friendly cities, starve far from them */
        float d = nearest_owned_city_dist(g, u->owner, u->pos);
        float heal = (d < SUPPLY_CLOSE) ? 5.0f
                   : (d < SUPPLY_FAR)   ? 1.5f
                                        : -1.5f;
        u->hp = clampf(u->hp + heal * dt, -1.0f, MAX_HP);

        move_unit(g, u, dt);
    }

    separate_units(g);

    /* simultaneous combat: every unit hits the closest enemy in range */
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive) continue;
        Unit *best = NULL;
        float best_d = ATTACK_RANGE;
        for (int j = 0; j < MAX_UNITS; j++) {
            Unit *o = &g->units[j];
            if (!o->alive || o->owner == u->owner) continue;
            float dd = vdist(u->pos, o->pos);
            if (dd < best_d) { best_d = dd; best = o; }
        }
        if (best) {
            uint8_t t = world_terrain_at(&g->w, u->pos.x, u->pos.y);
            float power = UNIT_DPS * TERRAIN_ATTACK[t];
            dmg[best - g->units] += power * dt;
        }
    }

    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive) continue;
        if (dmg[i] > 0.0f) {
            u->hp -= dmg[i];
            u->flash = 0.12f;
        }
        if (u->hp <= 0.0f) {
            u->alive = 0;
            u->plen = 0;
        }
    }

    /* clamp to world bounds */
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive) continue;
        u->pos.x = clampf(u->pos.x, UNIT_R, (float)WORLD_W - UNIT_R);
        u->pos.y = clampf(u->pos.y, UNIT_R, (float)WORLD_H - UNIT_R);
    }
}
