#include "world.h"
#include <stdlib.h>
#include <string.h>

const float TERRAIN_SPEED[T_COUNT]  = { 0.60f, 1.00f, 0.80f, 0.70f, 0.00f };
const float TERRAIN_ATTACK[T_COUNT] = { 0.50f, 1.00f, 0.75f, 1.50f, 0.00f };
const uint32_t TERRAIN_COLOR[T_COUNT] = {
    0xFF2C6E91, 0xFF4E8C4A, 0xFF2F5E36, 0xFF8B8B72, 0xFF6E6E6E
};
const char *TERRAIN_NAME[T_COUNT] = { "WATER", "PLAINS", "FOREST", "HILL", "MOUNTAIN" };

/* ---------- deterministic randomness --------------------------------- */
uint32_t rnd_next(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}
float rnd_f(uint32_t *s) { return (float)(rnd_next(s) & 0xFFFFFF) / 16777216.0f; }
float rnd_range(uint32_t *s, float a, float b) { return a + rnd_f(s) * (b - a); }

/* ---------- value noise ---------------------------------------------- */
static uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = seed ^ ((uint32_t)x * 374761393u) ^ ((uint32_t)y * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float grad01(int x, int y, uint32_t seed) {
    return (float)(hash2(x, y, seed) & 0xFFFF) / 65535.0f;
}

static float vnoise(float x, float y, uint32_t seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = smoothf(x - (float)xi), fy = smoothf(y - (float)yi);
    float a = grad01(xi, yi, seed);
    float b = grad01(xi + 1, yi, seed);
    float c = grad01(xi, yi + 1, seed);
    float d = grad01(xi + 1, yi + 1, seed);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

static float fbm(float x, float y, uint32_t seed, int octaves) {
    float amp = 0.5f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += vnoise(x * freq, y * freq, seed + (uint32_t)i * 101u) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / norm;
}

/* Island bias: high in the middle, falls to zero at the map edges. */
static float island_bias(float x, float y) {
    float cx = (float)WORLD_W * 0.5f, cy = (float)WORLD_H * 0.5f;
    float dx = (x - cx) / cx, dy = (y - cy) / cy;
    float d = sqrtf(dx * dx + dy * dy) / 1.41421356f;
    if (d <= 0.5f) return 0.5f + fmaxf(d, 0.25f);
    return fmaxf(0.0f, 1.0f - (d - 0.5f) * 2.0f);
}

static uint8_t classify(float e, float f) {
    if (e < 0.22f) return T_WATER;
    if (f > 0.58f && e > 0.24f && e < 0.58f) return T_FOREST;
    if (e < 0.55f) return T_PLAINS;
    if (e < 0.68f) return T_HILL;
    return T_MOUNTAIN;
}

/* ---------- queries --------------------------------------------------- */
float world_elev_at(const World *w, float x, float y) {
    float gx = clampf(x / CELL, 0.0f, (float)(COLS - 1e-3f));
    float gy = clampf(y / CELL, 0.0f, (float)(ROWS - 1e-3f));
    int c = (int)gx, r = (int)gy;
    float fx = gx - (float)c, fy = gy - (float)r;
    int c1 = c + 1 < COLS + 1 ? c + 1 : c;
    int r1 = r + 1 < ROWS + 1 ? r + 1 : r;
    float a = w->elev[r][c], b = w->elev[r][c1];
    float d = w->elev[r1][c], e = w->elev[r1][c1];
    return lerpf(lerpf(a, b, fx), lerpf(d, e, fx), fy);
}

float world_forest_at(const World *w, float x, float y) {
    float gx = clampf(x / CELL, 0.0f, (float)(COLS - 1e-3f));
    float gy = clampf(y / CELL, 0.0f, (float)(ROWS - 1e-3f));
    int c = (int)gx, r = (int)gy;
    float fx = gx - (float)c, fy = gy - (float)r;
    int c1 = c + 1 < COLS ? c + 1 : c;
    int r1 = r + 1 < ROWS ? r + 1 : r;
    float a = w->forest[r][c], b = w->forest[r][c1];
    float d = w->forest[r1][c], e = w->forest[r1][c1];
    return lerpf(lerpf(a, b, fx), lerpf(d, e, fx), fy);
}

uint8_t world_terrain_at(const World *w, float x, float y) {
    int c = (int)clampf(x / CELL, 0, COLS - 1);
    int r = (int)clampf(y / CELL, 0, ROWS - 1);
    return w->terr[r][c];
}

int world_walkable(const World *w, float x, float y) {
    return world_terrain_at(w, x, y) != T_MOUNTAIN;
}

/* ---------- generation ------------------------------------------------ */
static int reachable_mask(const World *w, uint8_t mask[ROWS][COLS],
                          int start_r, int start_c) {
    int queue[ROWS * COLS][2];
    int qh = 0, qt = 0, seen = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) mask[r][c] = 0;
    if (w->terr[start_r][start_c] == T_MOUNTAIN) return 0;
    mask[start_r][start_c] = 1;
    queue[qt][0] = start_r; queue[qt][1] = start_c; qt++;
    static const int dc[4] = { 1, -1, 0, 0 };
    static const int dr[4] = { 0, 0, 1, -1 };
    while (qh < qt) {
        int r = queue[qh][0], c = queue[qh][1];
        qh++; seen++;
        for (int k = 0; k < 4; k++) {
            int nr = r + dr[k], nc = c + dc[k];
            if (nr < 0 || nc < 0 || nr >= ROWS || nc >= COLS) continue;
            if (mask[nr][nc] || w->terr[nr][nc] == T_MOUNTAIN) continue;
            mask[nr][nc] = 1;
            queue[qt][0] = nr; queue[qt][1] = nc; qt++;
        }
    }
    return seen;
}

void world_generate(World *w, uint32_t seed, Vec2 *city_pos, int *out_count) {
    w->seed = seed;
    uint32_t s = seed ^ 0xA5A5F00Du;

    /* elevation at grid corners */
    for (int r = 0; r <= ROWS; r++) {
        for (int c = 0; c <= COLS; c++) {
            float x = (float)c * CELL, y = (float)r * CELL;
            float n = fbm(x * 0.0125f, y * 0.0125f, s, 4);
            float f = fbm(x * 0.035f + 31.0f, y * 0.035f - 17.0f, s ^ 0x51ED27u, 3);
            float e = n * 0.90f + island_bias(x, y) * 0.55f - 0.44f;
            /* guarantee open sea around the map border */
            int edge_cells = r < ROWS - r ? r : ROWS - r;
            int ed = c < COLS - c ? c : COLS - c;
            if (ed < edge_cells) edge_cells = ed;
            float edge = clampf((float)edge_cells / 5.0f, 0.0f, 1.0f);
            e *= 0.30f + 0.70f * smoothf(edge);
            w->elev[r][c] = clampf(e, 0.0f, 1.0f);
            w->forest[r][c] = f;
        }
    }

    /* terrain per cell, sampled at the cell centre */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            float e = (w->elev[r][c] + w->elev[r + 1][c] +
                       w->elev[r][c + 1] + w->elev[r + 1][c + 1]) * 0.25f;
            float f = (w->forest[r][c] + w->forest[r + 1][c] +
                       w->forest[r][c + 1] + w->forest[r + 1][c + 1]) * 0.25f;
            w->terr[r][c] = classify(e, f);
        }
    }

    /* pick a seed cell for reachability near the lower-left */
    int sr = ROWS - 4, sc = 4;
    for (int radius = 0; radius < 40; radius++) {
        int found = 0;
        for (int r = ROWS - 4 - radius; r <= ROWS - 4 + radius && !found; r++) {
            for (int c = 4 - radius; c <= 4 + radius && !found; c++) {
                if (r < 0 || c < 0 || r >= ROWS || c >= COLS) continue;
                if (w->terr[r][c] != T_MOUNTAIN && w->terr[r][c] != T_WATER) {
                    sr = r; sc = c; found = 1;
                }
            }
        }
        if (found) break;
    }
    uint8_t reach[ROWS][COLS];
    reachable_mask(w, reach, sr, sc);

    /* cities: rejection sampling on connected plains */
    int n = 0;
    float spacing = MAP_CITY_SPACING;
    uint32_t tries = 0;
    while (n < MAP_CITIES && tries < 60000) {
        tries++;
        if (tries % 4000 == 0 && spacing > 130.0f) spacing -= 12.0f;
        int c = (int)rnd_range(&s, 2, (float)(COLS - 2));
        int r = (int)rnd_range(&s, 2, (float)(ROWS - 2));
        if (w->terr[r][c] != T_PLAINS || !reach[r][c]) continue;
        float x = (float)c * CELL + CELL * 0.5f;
        float y = (float)r * CELL + CELL * 0.5f;
        if (x < MAP_MARGIN || y < MAP_MARGIN ||
            x > WORLD_W - MAP_MARGIN || y > WORLD_H - MAP_MARGIN) continue;
        int ok = 1;
        for (int i = 0; i < n; i++) {
            if (vdist(v2(x, y), city_pos[i]) < spacing) { ok = 0; break; }
        }
        if (ok) city_pos[n++] = v2(x, y);
    }
    *out_count = n;
}


/* Breadth-first grid route around mountains. Returns the number of waypoints
 * written, always ending at `to` (if reachable). Shared by the AI and by
 * player-issued paths. */
int world_route(const World *w, Vec2 from, Vec2 to, Vec2 *out, int max_out) {
    int sc = (int)clampf(from.x / CELL, 0, COLS - 1);
    int sr = (int)clampf(from.y / CELL, 0, ROWS - 1);
    int tc = (int)clampf(to.x / CELL, 0, COLS - 1);
    int tr = (int)clampf(to.y / CELL, 0, ROWS - 1);

    for (int pass = 0; pass < 2; pass++) {
        int *r = pass == 0 ? &sr : &tr;
        int *c = pass == 0 ? &sc : &tc;
        if (w->terr[*r][*c] != T_MOUNTAIN) continue;
        int found = 0;
        for (int rad = 1; rad < 6 && !found; rad++)
            for (int dr = -rad; dr <= rad && !found; dr++)
                for (int dc = -rad; dc <= rad && !found; dc++) {
                    int rr = *r + dr, cc = *c + dc;
                    if (rr < 0 || cc < 0 || rr >= ROWS || cc >= COLS) continue;
                    if (w->terr[rr][cc] != T_MOUNTAIN) { *r = rr; *c = cc; found = 1; }
                }
    }

    static int16_t prev[ROWS * COLS];
    static uint8_t seen[ROWS * COLS];
    static int16_t queue[ROWS * COLS];
    memset(seen, 0, sizeof(seen));
    int qh = 0, qt = 0;
    int start = sr * COLS + sc, goal = tr * COLS + tc;
    prev[start] = -1;
    seen[start] = 1;
    queue[qt++] = (int16_t)start;

    static const int dr4[4] = { 1, -1, 0, 0 };
    static const int dc4[4] = { 0, 0, 1, -1 };
    int found = 0;
    while (qh < qt) {
        int cur = queue[qh++];
        if (cur == goal) { found = 1; break; }
        int r = cur / COLS, c = cur % COLS;
        for (int k = 0; k < 4; k++) {
            int nr = r + dr4[k], nc = c + dc4[k];
            if (nr < 0 || nc < 0 || nr >= ROWS || nc >= COLS) continue;
            int ni = nr * COLS + nc;
            if (seen[ni] || w->terr[nr][nc] == T_MOUNTAIN) continue;
            seen[ni] = 1;
            prev[ni] = (int16_t)cur;
            queue[qt++] = (int16_t)ni;
        }
    }
    if (!found) return 0;

    static int16_t rev[ROWS * COLS];
    int len = 0;
    for (int cur = goal; cur != -1 && len < ROWS * COLS; cur = prev[cur])
        rev[len++] = (int16_t)cur;

    int n = 0;
    for (int i = len - 2; i >= 0 && n < max_out - 1; i--) {
        int r = rev[i] / COLS, c = rev[i] % COLS;
        out[n++] = v2((float)c * CELL + CELL * 0.5f,
                      (float)r * CELL + CELL * 0.5f);
    }
    out[n++] = v2(clampf(to.x, UNIT_R, (float)WORLD_W - UNIT_R),
                  clampf(to.y, UNIT_R, (float)WORLD_H - UNIT_R));
    return n;
}
