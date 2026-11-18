#include "game.h"
#include <string.h>

const UnitType UNIT_TYPE[UT_COUNT] = {
    {  60.0f, 1.35f,  6.0f, 26.0f, 190.0f, 4.0f, 0.70f, "SCOUT"   },
    { 100.0f, 1.00f, 12.0f, 30.0f, 105.0f, 5.0f, 1.00f, "SOLDIER" },
    { 170.0f, 0.72f, 22.0f, 34.0f,  90.0f, 6.5f, 1.80f, "HEAVY"   },
};

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
    if (u->order == ORD_HOLD || u->pi >= u->plen) return;
    int ut = u->type;
    if (ut < 0 || ut >= UT_COUNT) ut = UT_SOLDIER;

    /* attack-move: stop and fight anything visible that comes close */
    if (u->order == ORD_ATTACK_MOVE) {
        for (int j = 0; j < MAX_UNITS; j++) {
            const Unit *o = &g->units[j];
            if (!o->alive || o->owner == u->owner) continue;
            if (vdist(u->pos, o->pos) <= UNIT_TYPE[ut].range &&
                game_visibility_at(g, u->owner, o->pos.x, o->pos.y) >= 0.25f)
                return;
        }
    }

    Vec2 target = u->path[u->pi];
    float d = vdist(u->pos, target);
    float speed = UNIT_SPEED * UNIT_TYPE[ut].speed *
                  TERRAIN_SPEED[world_terrain_at(&g->w, u->pos.x, u->pos.y)];
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
            float push = (gap - d) * 0.28f;
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
        float maxhp = u->max_hp > 0.0f ? u->max_hp : MAX_HP;
        u->hp = clampf(u->hp + heal * dt, -1.0f, maxhp);

        move_unit(g, u, dt);
    }

    separate_units(g);

    /* Unjam units that have stopped making progress (dense columns, bad
     * waypoints). Clearing the path lets the AI issue a fresh route. */
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive) continue;
        if (u->pi >= u->plen) {
            u->stall_t = 0.0f;
        } else if (vdist(u->pos, u->last_pos) < 0.20f) {
            u->stall_t += dt;
            if (u->stall_t > 2.0f) {
                u->pi = u->plen;
                u->stall_t = 0.0f;
            }
        } else {
            u->stall_t = 0.0f;
        }
        u->last_pos = u->pos;
    }

    /* simultaneous combat: every unit hits the closest enemy in range */
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &g->units[i];
        if (!u->alive) continue;
        int ut = u->type;
        if (ut < 0 || ut >= UT_COUNT) ut = UT_SOLDIER;
        Unit *best = NULL;
        float best_d = UNIT_TYPE[ut].range;
        for (int j = 0; j < MAX_UNITS; j++) {
            Unit *o = &g->units[j];
            if (!o->alive || o->owner == u->owner) continue;
            float dd = vdist(u->pos, o->pos);
            if (dd < best_d) { best_d = dd; best = o; }
        }
        if (best) {
            uint8_t t = world_terrain_at(&g->w, u->pos.x, u->pos.y);
            float power = UNIT_TYPE[ut].dps * TERRAIN_ATTACK[t];
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
            g->lost[u->owner]++;
            g->sfx |= (uint16_t)SFX_DEATH;
            if (g->selected[i]) {
                g->selected[i] = 0;
                if (g->nsel > 0) g->nsel--;
            }
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
