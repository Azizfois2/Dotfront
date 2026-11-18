#ifndef WOD_WORLD_H
#define WOD_WORLD_H

#include "common.h"

#define MAP_CITIES 12
#define MAP_MARGIN 70.0f
#define MAP_CITY_SPACING 215.0f

typedef struct {
    uint8_t terr[ROWS][COLS];
    float   elev[ROWS + 1][COLS + 1];
    float   forest[ROWS + 1][COLS + 1];
    uint32_t seed;
} World;

void    world_generate(World *w, uint32_t seed, Vec2 *city_pos, int *out_count);
uint8_t world_terrain_at(const World *w, float x, float y);
float   world_elev_at(const World *w, float x, float y);
float   world_forest_at(const World *w, float x, float y);
int     world_walkable(const World *w, float x, float y);
int     world_route(const World *w, Vec2 from, Vec2 to, Vec2 *out, int max_out);
uint32_t rnd_next(uint32_t *s);
float   rnd_f(uint32_t *s);       /* [0,1) */
float   rnd_range(uint32_t *s, float a, float b);

#endif /* WOD_WORLD_H */
