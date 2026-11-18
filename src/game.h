#ifndef GAME_H
#define GAME_H

#include "world.h"

typedef struct {
    World    w;
    City     cities[MAX_CITIES];
    int      ncity;
    Unit     units[MAX_UNITS];
    uint8_t  selected[MAX_UNITS];
    int      nsel;
    int      sel_city;          /* index into cities, or -1 */
    int      paused;
    int      winner;            /* -1 playing, 0 blue, 1 red */
    int      ai_enabled[2];
    Vec2     capital[2];
    float    time;
    float    end_time;
    int      cities_owned[2];
    int      units_owned[2];
    float    ai_timer[2];
    int      ai_wave[2];
    /* fog of war: 0 = hidden, 255 = fully visible; seen keeps explored memory */
    uint8_t  vis[2][ROWS][COLS];
    uint8_t  seen[2][ROWS][COLS];
} Game;

void game_init(Game *g, uint32_t seed);
void game_update(Game *g, float dt);
void game_update_vision(Game *g);
float game_visibility_at(const Game *g, int player, float x, float y);
float game_seen_at(const Game *g, int player, float x, float y);

/* units.c */
void units_update(Game *g, float dt);
int  game_spawn_unit(Game *g, int owner, Vec2 pos);

/* selection + orders */
void game_clear_selection(Game *g);
void game_click_select(Game *g, Vec2 world, int additive);
void game_box_select(Game *g, Vec2 a, Vec2 b, int additive);
void game_select_all(Game *g);
void game_order_selected(Game *g, const Vec2 *pts, int n);
void game_set_city_rally(Game *g, Vec2 world);

#endif /* GAME_H */
