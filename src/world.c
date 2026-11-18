#include "world.h"
#include <stdlib.h>
#include <string.h>

const float TERRAIN_SPEED[T_COUNT]  = { 0.60f, 1.00f, 0.80f, 0.70f, 0.00f };
const float TERRAIN_ATTACK[T_COUNT] = { 0.50f, 1.00f, 0.75f, 1.50f, 0.00f };
const uint32_t TERRAIN_COLOR[T_COUNT] = {
    0xFF2C6E91, 0xFF4E8C4A, 0xFF2F5E36, 0xFF8B8B72, 0xFF6E6E6E
};
const char *TERRAIN_NAME[T_COUNT] = { "WATER", "PLAINS", "FOREST", "HILL", "MOUNTAIN" };

const char *world_style_name(int style) {
    switch (style) {
    case MAP_ARCHIPELAGO: return "ARCHIPELAGO";
    case MAP_LAKES:       return "LAKES";
    case MAP_HIGHLANDS:   return "HIGHLANDS";
    default:              return "ISLAND";
    }
}

int world_city_target(int players) {
    if (players < 2) players = 2;
    if (players > MAX_PLAYERS) players = MAX_PLAYERS;
    int n = players * 5;
    if (n < 12) n = 12;
    if (n > MAX_CITIES) n = MAX_CITIES;
    return n;
}

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

const char *world_shape_name(int shape) {
    switch (shape) {
    case SHAPE_TWIN:     return "TWIN ISLES";
    case SHAPE_ATOLL:    return "ATOLL";
    case SHAPE_FJORDS:   return "FJORDS";
    case SHAPE_SCATTER:  return "ISLAND CHAIN";
    case SHAPE_ISTHMUS:  return "ISTHMUS";
    case SHAPE_CRESCENT: return "CRESCENT";
    case SHAPE_RIFT:     return "RIFT";
    default:             return "CONTINENT";
    }
}

/* Signed-ish lobe: 1 at centre, 0 at its rim. */
static float lobe(float x, float y, float cx, float cy, float rx, float ry) {
    float dx = (x - cx) / rx, dy = (y - cy) / ry;
    return 1.0f - sqrtf(dx * dx + dy * dy);
}

static int shape_for_style(uint32_t seed, int style) {
    static const int set_island[4]    = { SHAPE_CONTINENT, SHAPE_TWIN,
                                          SHAPE_ISTHMUS, SHAPE_CRESCENT };
    static const int set_arch[4]      = { SHAPE_SCATTER, SHAPE_ATOLL,
                                          SHAPE_FJORDS, SHAPE_CRESCENT };
    static const int set_lakes[4]     = { SHAPE_CONTINENT, SHAPE_RIFT,
                                          SHAPE_FJORDS, SHAPE_ATOLL };
    static const int set_high[4]      = { SHAPE_CONTINENT, SHAPE_RIFT,
                                          SHAPE_ISTHMUS, SHAPE_TWIN };
    uint32_t h = seed ^ (seed >> 7) ^ (seed * 0x9E3779B9u >> 3);
    int i = (int)(h % 4u);
    if (style == MAP_ARCHIPELAGO) return set_arch[i];
    if (style == MAP_LAKES) return set_lakes[i];
    if (style == MAP_HIGHLANDS) return set_high[i];
    return set_island[i];
}

/* Distinct large-scale topologies rather than one warped central ellipse.
 * The shape is rotated by a seed-derived angle, so repeated archetypes still
 * enter the map from different directions. */
static float shape_bias(const World *w, float x, float y, float strength,
                        int shape, uint32_t seed) {
    float nx = (x / (float)w->width - 0.5f) * 2.0f;
    float ny = (y / (float)w->height - 0.5f) * 2.0f;

    float ang = (float)((seed >> 3) % 360u) * 0.01745329252f;
    float ca = cosf(ang), sa = sinf(ang);
    float rx = nx * ca - ny * sa;
    float ry = nx * sa + ny * ca;
    nx = rx;
    ny = ry;

    /* gentle domain warp keeps the topology readable but coasts organic */
    float wx = fbm(nx * 2.3f + 7.1f, ny * 2.3f - 3.7f, seed ^ 0x51A3u, 2);
    float wy = fbm(nx * 2.4f - 4.2f, ny * 2.1f + 8.5f, seed ^ 0x9C11u, 2);
    nx += (wx - 0.5f) * 0.18f;
    ny += (wy - 0.5f) * 0.18f;

    uint32_t rs = seed ^ 0xC0FFEEu;
    float phase = rnd_f(&rs) * 6.2831853f;
    float b = 0.0f;

    switch (shape) {
    case SHAPE_TWIN: {
        float a1 = lobe(nx, ny, -0.44f, -0.05f, 0.54f, 0.52f);
        float a2 = lobe(nx, ny, 0.45f, 0.06f, 0.52f, 0.50f);
        b = fmaxf(a1, a2) * 0.78f;
        if (seed & 1u)   /* occasional narrow land bridge */
            b = fmaxf(b, lobe(nx, ny, 0.0f, 0.0f, 0.30f, 0.11f) * 0.62f);
        break;
    }
    case SHAPE_ATOLL: {
        float r = sqrtf(nx * nx * 0.94f + ny * ny * 1.12f);
        b = 0.60f - fabsf(r - 0.54f) * 2.05f;
        /* break the ring with two or three navigable channels */
        float a = atan2f(ny, nx);
        for (int k = 0; k < 3; k++) {
            float g = a - (phase + (float)k * 2.12f);
            while (g > 3.14159265f) g -= 6.2831853f;
            while (g < -3.14159265f) g += 6.2831853f;
            b -= expf(-g * g * 26.0f) * 0.34f;
        }
        break;
    }
    case SHAPE_FJORDS: {
        float r = sqrtf(nx * nx * 0.86f + ny * ny * 1.10f);
        b = 0.90f - r * 1.03f;
        float a = atan2f(ny, nx);
        float inl = fabsf(sinf(a * 3.0f + phase));
        inl = powf(inl, 18.0f);
        b -= inl * smoothf(clampf((r - 0.24f) / 0.52f, 0.0f, 1.0f)) * 0.58f;
        float ridge = fbm(nx * 3.6f + 21.0f, ny * 1.15f - 9.0f,
                          seed ^ 0xB17Bu, 3);
        b -= smoothf(clampf((ridge - 0.56f) / 0.18f, 0.0f, 1.0f)) * 0.22f;
        break;
    }
    case SHAPE_SCATTER: {
        /* a curved island chain plus a few outliers, not uniform noise specks */
        for (int k = 0; k < 7; k++) {
            float t = (float)k / 6.0f;
            float cx = -0.72f + t * 1.44f;
            float cy = sinf(t * 3.1f + phase) * 0.34f;
            cx += (rnd_f(&rs) - 0.5f) * 0.13f;
            cy += (rnd_f(&rs) - 0.5f) * 0.13f;
            float rr = 0.20f + rnd_f(&rs) * 0.10f;
            b = fmaxf(b, lobe(nx, ny, cx, cy, rr, rr * 0.78f));
        }
        for (int k = 0; k < 2; k++) {
            float cx = (rnd_f(&rs) * 1.5f - 0.75f);
            float cy = (rnd_f(&rs) * 1.3f - 0.65f);
            float rr = 0.16f + rnd_f(&rs) * 0.09f;
            b = fmaxf(b, lobe(nx, ny, cx, cy, rr, rr));
        }
        b *= 0.76f;
        break;
    }
    case SHAPE_ISTHMUS: {
        float a1 = lobe(nx, ny, -0.48f, 0.0f, 0.50f, 0.50f);
        float a2 = lobe(nx, ny, 0.49f, 0.05f, 0.50f, 0.48f);
        float neck = lobe(nx, ny, 0.0f, 0.02f, 0.42f, 0.15f) * 0.74f;
        b = fmaxf(fmaxf(a1, a2), neck) * 0.82f;
        break;
    }
    case SHAPE_CRESCENT: {
        float r = sqrtf(nx * nx * 0.92f + ny * ny * 1.08f);
        b = 0.64f - fabsf(r - 0.56f) * 2.35f;
        float cut = smoothf(clampf((nx - 0.02f) / 0.62f, 0.0f, 1.0f));
        b -= cut * 0.80f;
        break;
    }
    case SHAPE_RIFT: {
        float r = sqrtf(nx * nx * 0.80f + ny * ny * 1.10f);
        b = 0.88f - r * 1.02f;
        /* a winding sea rift splits the continent into two halves */
        float wob = (fbm(nx * 1.8f - 4.0f, ny * 1.8f + 11.0f,
                         seed ^ 0xD11Fu, 2) - 0.5f) * 0.22f;
        float line = ry * 0.82f - rx * 0.57f + wob;
        b -= expf(-line * line / 0.0022f) * 0.78f;
        break;
    }
    default: {
        float b1 = lobe(nx, ny, 0.0f, 0.02f, 0.74f, 0.64f);
        float b2 = lobe(nx, ny, -0.34f, -0.14f, 0.46f, 0.40f);
        float b3 = lobe(nx, ny, 0.36f, 0.12f, 0.44f, 0.38f);
        float b4 = lobe(nx, ny, 0.02f, 0.40f, 0.38f, 0.28f);
        b = fmaxf(fmaxf(b1, b2), fmaxf(b3, b4)) * 0.80f;
        break;
    }
    }
    return b * strength * 1.45f;
}

typedef struct {
    float freq;
    float bias;
    float offset;
    float water;
    float hill;
    float mountain;
    float forest;
} MapTuning;

static MapTuning tuning_for(int style) {
    switch (style) {
    case MAP_ARCHIPELAGO:
        return (MapTuning){ 0.0215f, 0.24f, 0.17f, 0.255f, 0.50f, 0.68f, 0.55f };
    case MAP_LAKES:
        return (MapTuning){ 0.0130f, 0.50f, 0.10f, 0.260f, 0.57f, 0.71f, 0.61f };
    case MAP_HIGHLANDS:
        return (MapTuning){ 0.0160f, 0.62f, 0.08f, 0.190f, 0.47f, 0.59f, 0.62f };
    default:
        return (MapTuning){ 0.0125f, 0.58f, 0.00f, 0.220f, 0.55f, 0.68f, 0.58f };
    }
}

static uint8_t classify(float e, float f, const MapTuning *t) {
    if (e < t->water) return T_WATER;
    if (f > t->forest && e > t->water + 0.02f && e < t->hill) return T_FOREST;
    if (e < t->hill) return T_PLAINS;
    if (e < t->mountain) return T_HILL;
    return T_MOUNTAIN;
}

/* ---------- queries --------------------------------------------------- */
float world_elev_at(const World *w, float x, float y) {
    float gx = clampf(x / CELL, 0.0f, (float)(w->cols - 1e-3f));
    float gy = clampf(y / CELL, 0.0f, (float)(w->rows - 1e-3f));
    int c = (int)gx, r = (int)gy;
    float fx = gx - (float)c, fy = gy - (float)r;
    int c1 = c + 1 < w->cols + 1 ? c + 1 : c;
    int r1 = r + 1 < w->rows + 1 ? r + 1 : r;
    float a = w->elev[r][c], b = w->elev[r][c1];
    float d = w->elev[r1][c], e = w->elev[r1][c1];
    return lerpf(lerpf(a, b, fx), lerpf(d, e, fx), fy);
}

float world_forest_at(const World *w, float x, float y) {
    float gx = clampf(x / CELL, 0.0f, (float)(w->cols - 1e-3f));
    float gy = clampf(y / CELL, 0.0f, (float)(w->rows - 1e-3f));
    int c = (int)gx, r = (int)gy;
    float fx = gx - (float)c, fy = gy - (float)r;
    int c1 = c + 1 < w->cols ? c + 1 : c;
    int r1 = r + 1 < w->rows ? r + 1 : r;
    float a = w->forest[r][c], b = w->forest[r][c1];
    float d = w->forest[r1][c], e = w->forest[r1][c1];
    return lerpf(lerpf(a, b, fx), lerpf(d, e, fx), fy);
}

uint8_t world_terrain_at(const World *w, float x, float y) {
    int c = (int)clampf(x / CELL, 0, w->cols - 1);
    int r = (int)clampf(y / CELL, 0, w->rows - 1);
    return w->terr[r][c];
}

int world_walkable(const World *w, float x, float y) {
    return world_terrain_at(w, x, y) != T_MOUNTAIN;
}

/* ---------- generation ------------------------------------------------ */
static int reachable_mask(const World *w, uint8_t mask[MAX_ROWS][MAX_COLS],
                          int start_r, int start_c) {
    static int16_t queue[MAX_CELLS][2];
    int qh = 0, qt = 0, seen = 0;
    for (int r = 0; r < w->rows; r++)
        for (int c = 0; c < w->cols; c++) mask[r][c] = 0;
    if (w->terr[start_r][start_c] == T_MOUNTAIN) return 0;
    mask[start_r][start_c] = 1;
    queue[qt][0] = (int16_t)start_r; queue[qt][1] = (int16_t)start_c; qt++;
    static const int dc[4] = { 1, -1, 0, 0 };
    static const int dr[4] = { 0, 0, 1, -1 };
    while (qh < qt) {
        int r = queue[qh][0], c = queue[qh][1];
        qh++; seen++;
        for (int k = 0; k < 4; k++) {
            int nr = r + dr[k], nc = c + dc[k];
            if (nr < 0 || nc < 0 || nr >= w->rows || nc >= w->cols) continue;
            if (mask[nr][nc] || w->terr[nr][nc] == T_MOUNTAIN) continue;
            mask[nr][nc] = 1;
            queue[qt][0] = (int16_t)nr; queue[qt][1] = (int16_t)nc; qt++;
        }
    }
    return seen;
}

void world_generate(World *w, uint32_t seed, int players,
                    Vec2 *city_pos, int *out_count) {
    memset(w, 0, sizeof(*w));
    w->seed = seed;
    w->style = (int)(seed % MAP_STYLE_COUNT);
    if (players < 2) players = 2;
    if (players > MAX_PLAYERS) players = MAX_PLAYERS;

    /* Bigger maps for bigger wars: about 400k px^2 of land per player, with
     * the original 1600x900 as a floor, kept at 16:9. */
    float area = (float)MIN_WORLD_W * (float)MIN_WORLD_H;
    float want = (float)players * 400000.0f;
    if (want > area) area = want;
    int cols = (int)(sqrtf(area * 16.0f / 9.0f) / (float)CELL + 0.5f);
    if (cols < MIN_WORLD_W / CELL) cols = MIN_WORLD_W / CELL;
    if (cols > MAX_COLS) cols = MAX_COLS;
    int rows = (cols * 9 + 8) / 16;
    if (rows < MIN_WORLD_H / CELL) rows = MIN_WORLD_H / CELL;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    w->cols = cols;
    w->rows = rows;
    w->width = cols * CELL;
    w->height = rows * CELL;

    uint32_t s = seed ^ 0xA5A5F00Du ^ (uint32_t)w->style * 0x9E3779B9u;
    w->shape = shape_for_style(seed, w->style);
    MapTuning tune = tuning_for(w->style);

    /* elevation at grid corners */
    for (int r = 0; r <= w->rows; r++) {
        for (int c = 0; c <= w->cols; c++) {
            float x = (float)c * CELL, y = (float)r * CELL;
            /* warp the terrain noise as well, so coastlines curl and branch */
            float qx = fbm(x * tune.freq * 0.58f + 9.0f,
                           y * tune.freq * 0.58f - 5.0f, s ^ 0x2F71u, 2);
            float qy = fbm(x * tune.freq * 0.61f - 12.0f,
                           y * tune.freq * 0.55f + 6.0f, s ^ 0x83D5u, 2);
            float n = fbm(x * tune.freq + (qx - 0.5f) * 2.2f,
                          y * tune.freq + (qy - 0.5f) * 2.2f, s, 5);
            float f = fbm(x * 0.035f + 31.0f, y * 0.035f - 17.0f, s ^ 0x51ED27u, 3);
            float e = n * 0.88f + shape_bias(w, x, y, tune.bias, w->shape, s) * 0.92f
                      + tune.offset - 0.36f;
            /* open sea around the map border, with a wavy rather than a
             * ruler-straight rectangular edge */
            float wob = (fbm(x * 0.008f + 3.0f, y * 0.008f - 8.0f,
                             s ^ 0x4C1Bu, 2) - 0.5f) * 13.0f;
            int edge_cells = r < w->rows - r ? r : w->rows - r;
            int ed = c < w->cols - c ? c : w->cols - c;
            if (ed < edge_cells) edge_cells = ed;
            float edge = clampf(((float)edge_cells + wob) / 7.0f, 0.0f, 1.0f);
            e *= 0.24f + 0.76f * smoothf(edge);
            w->elev[r][c] = clampf(e, 0.0f, 1.0f);
            w->forest[r][c] = f;
        }
    }

    /* terrain per cell, sampled at the cell centre */
    for (int r = 0; r < w->rows; r++) {
        for (int c = 0; c < w->cols; c++) {
            float e = (w->elev[r][c] + w->elev[r + 1][c] +
                       w->elev[r][c + 1] + w->elev[r + 1][c + 1]) * 0.25f;
            float f = (w->forest[r][c] + w->forest[r + 1][c] +
                       w->forest[r][c + 1] + w->forest[r + 1][c + 1]) * 0.25f;
            w->terr[r][c] = classify(e, f, &tune);
        }
    }

    /* pick a seed cell for reachability near the lower-left */
    int sr = w->rows - 4, sc = 4;
    for (int radius = 0; radius < 48; radius++) {
        int found = 0;
        for (int r = w->rows - 4 - radius; r <= w->rows - 4 + radius && !found; r++) {
            for (int c = 4 - radius; c <= 4 + radius && !found; c++) {
                if (r < 0 || c < 0 || r >= w->rows || c >= w->cols) continue;
                if (w->terr[r][c] != T_MOUNTAIN && w->terr[r][c] != T_WATER) {
                    sr = r; sc = c; found = 1;
                }
            }
        }
        if (found) break;
    }
    uint8_t reach[MAX_ROWS][MAX_COLS];
    reachable_mask(w, reach, sr, sc);

    int target = world_city_target(players);
    int n = 0;
    float spacing = sqrtf((float)(w->width * w->height) / (float)target) * 0.72f;
    spacing = clampf(spacing, 105.0f, 240.0f);
    uint32_t tries = 0;
    while (n < target && tries < 160000) {
        tries++;
        if (tries % 6000 == 0 && spacing > 88.0f) spacing -= 8.0f;
        int c = (int)rnd_range(&s, 2, (float)(w->cols - 2));
        int r = (int)rnd_range(&s, 2, (float)(w->rows - 2));
        if (w->terr[r][c] != T_PLAINS || !reach[r][c]) continue;
        float x = (float)c * CELL + CELL * 0.5f;
        float y = (float)r * CELL + CELL * 0.5f;
        if (x < MAP_MARGIN || y < MAP_MARGIN ||
            x > (float)w->width - MAP_MARGIN ||
            y > (float)w->height - MAP_MARGIN) continue;
        int ok = 1;
        for (int i = 0; i < n; i++) {
            float dx = city_pos[i].x - x, dy = city_pos[i].y - y;
            if (dx * dx + dy * dy < spacing * spacing) { ok = 0; break; }
        }
        if (!ok) continue;
        city_pos[n++] = v2(x, y);
    }
    *out_count = n;
}

/* ---------- BFS router ------------------------------------------------ */
int world_route(const World *w, Vec2 from, Vec2 to, Vec2 *out, int max_out) {
    int sc = (int)clampf(from.x / CELL, 0, w->cols - 1);
    int sr = (int)clampf(from.y / CELL, 0, w->rows - 1);
    int tc = (int)clampf(to.x / CELL, 0, w->cols - 1);
    int tr = (int)clampf(to.y / CELL, 0, w->rows - 1);
    int cols = w->cols;

    if (w->terr[sr][sc] == T_MOUNTAIN) {
        int best = -1;
        float best_d = 1e30f;
        for (int r = 0; r < w->rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (w->terr[r][c] == T_MOUNTAIN) continue;
                float dx = (float)c + 0.5f - from.x / CELL;
                float dy = (float)r + 0.5f - from.y / CELL;
                float d = dx * dx + dy * dy;
                if (d < best_d) { best_d = d; best = r * cols + c; }
            }
        }
        if (best < 0) return 0;
        sr = best / cols; sc = best % cols;
    }
    if (w->terr[tr][tc] == T_MOUNTAIN) {
        int best = -1;
        float best_d = 1e30f;
        for (int r = 0; r < w->rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (w->terr[r][c] == T_MOUNTAIN) continue;
                float dx = (float)c + 0.5f - to.x / CELL;
                float dy = (float)r + 0.5f - to.y / CELL;
                float d = dx * dx + dy * dy;
                if (d < best_d) { best_d = d; best = r * cols + c; }
            }
        }
        if (best < 0) return 0;
        tr = best / cols; tc = best % cols;
    }

    static int16_t prev[MAX_CELLS];
    static uint8_t seen[MAX_CELLS];
    static int16_t queue[MAX_CELLS];
    memset(seen, 0, (size_t)(w->rows * cols));
    int start = sr * cols + sc, goal = tr * cols + tc;
    int qh = 0, qt = 0;
    queue[qt++] = (int16_t)start;
    seen[start] = 1;
    prev[start] = -1;
    static const int dc[4] = { 1, -1, 0, 0 };
    static const int dr[4] = { 0, 0, 1, -1 };
    int found = 0;
    while (qh < qt) {
        int cur = queue[qh++];
        if (cur == goal) { found = 1; break; }
        int r = cur / cols, c = cur % cols;
        for (int k = 0; k < 4; k++) {
            int nr = r + dr[k], nc = c + dc[k];
            if (nr < 0 || nc < 0 || nr >= w->rows || nc >= cols) continue;
            if (w->terr[nr][nc] == T_MOUNTAIN) continue;
            int ni = nr * cols + nc;
            if (seen[ni]) continue;
            seen[ni] = 1;
            prev[ni] = (int16_t)cur;
            queue[qt++] = (int16_t)ni;
        }
    }
    if (!found) return 0;

    static int16_t rev[MAX_CELLS];
    int rn = 0;
    for (int cur = goal; cur != -1 && rn < MAX_CELLS; cur = prev[cur])
        rev[rn++] = (int16_t)cur;
    int n = 0;
    for (int i = rn - 1; i >= 0 && n < max_out; i--) {
        int r = rev[i] / cols, c = rev[i] % cols;
        out[n++] = v2(((float)c + 0.5f) * CELL, ((float)r + 0.5f) * CELL);
    }
    out[n++] = v2(clampf(to.x, UNIT_R, (float)w->width - UNIT_R),
                  clampf(to.y, UNIT_R, (float)w->height - UNIT_R));
    return n;
}
