#ifndef WOD_WORLD_H
#define WOD_WORLD_H

#include "common.h"

#define MAP_MARGIN 70.0f
#define MAX_CELLS  (MAX_ROWS * MAX_COLS)

/* Large-scale landmass template, independent of the terrain style. */
enum { SHAPE_CONTINENT = 0, SHAPE_TWIN, SHAPE_ATOLL, SHAPE_FJORDS,
       SHAPE_SCATTER, SHAPE_ISTHMUS, SHAPE_CRESCENT, SHAPE_RIFT,
       SHAPE_COUNT };

typedef struct {
    int      cols, rows;       /* active grid */
    int      width, height;    /* active world size in pixels */
    int      style;            /* MAP_* */
    int      shape;            /* SHAPE_* */
    uint8_t  terr[MAX_ROWS][MAX_COLS];
    float    elev[MAX_ROWS + 1][MAX_COLS + 1];
    float    forest[MAX_ROWS + 1][MAX_COLS + 1];
    uint32_t seed;
} World;

int     world_city_target(int players);
const char *world_style_name(int style);
const char *world_shape_name(int shape);
void    world_generate(World *w, uint32_t seed, int players,
                       Vec2 *city_pos, int *out_count);
uint8_t world_terrain_at(const World *w, float x, float y);
float   world_elev_at(const World *w, float x, float y);
float   world_forest_at(const World *w, float x, float y);
int     world_walkable(const World *w, float x, float y);
int     world_route(const World *w, Vec2 from, Vec2 to, Vec2 *out, int max_out);
uint32_t rnd_next(uint32_t *s);
float   rnd_f(uint32_t *s);       /* [0,1) */
float   rnd_range(uint32_t *s, float a, float b);

#endif /* WOD_WORLD_H */
