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
    int      menu;              /* start menu overlay */
    uint8_t  save_available;
    uint8_t  sound_on;
    int      players;           /* 2..MAX_PLAYERS */
    int      local;             /* faction controlled by the keyboard/mouse */
    int      winner;            /* -1 playing, P_* winner, -2 draw */
    int      eliminated;        /* local faction lost; choice screen is up */
    int      spectator;         /* local faction lost and is now observing */
    /* match summary */
    int      built[MAX_PLAYERS];
    int      lost[MAX_PLAYERS];
    int      captures[MAX_PLAYERS];
    int      peak[MAX_PLAYERS];
    char     notice[48];
    float    notice_t;
    uint16_t sfx;
    int      ai_enabled[MAX_PLAYERS];
    Vec2     capital[MAX_PLAYERS];
    float    time;
    float    end_time;
    int      cities_owned[MAX_PLAYERS];
    int      units_owned[MAX_PLAYERS];
    float    ai_timer[MAX_PLAYERS];
    int      ai_wave[MAX_PLAYERS];
    int      difficulty[MAX_PLAYERS];     /* DIFF_EASY / NORMAL / HARD */
    /* what each player has learned about cities (fog-aware AI + HUD) */
    int8_t   city_last_owner[MAX_PLAYERS][MAX_CITIES];  /* -2 = never seen */
    uint8_t  city_known[MAX_PLAYERS][MAX_CITIES];
    /* fog of war: 0 = hidden, 255 = fully visible; seen keeps explored memory */
    uint8_t  vis[MAX_PLAYERS][MAX_ROWS][MAX_COLS];
    uint8_t  seen[MAX_PLAYERS][MAX_ROWS][MAX_COLS];
    /* territory: owner of each grid cell, rebuilt from city influence */
    int8_t   terr_owner[MAX_ROWS][MAX_COLS];
    float    terr_t;
} Game;

void game_init(Game *g, uint32_t seed, int players);
int  game_save(const Game *g, const char *path);
int  game_load(Game *g, const char *path);
int  game_save_exists(const char *path);
void game_notice(Game *g, const char *text);
void game_update(Game *g, float dt);
void game_update_vision(Game *g);
void game_update_territory(Game *g);
int  game_unit_cap(const Game *g);
int  game_territory_at(const Game *g, float x, float y);  /* -1 neutral */
float game_visibility_at(const Game *g, int player, float x, float y);
float game_seen_at(const Game *g, int player, float x, float y);
int  game_known_city_owner(const Game *g, int player, int city); /* -2 unknown */
int  game_known_city_count(const Game *g, int player, int owner);
int  game_visible_unit_count(const Game *g, int player, int owner);

/* units.c */
void units_update(Game *g, float dt);
int  game_spawn_unit(Game *g, int owner, Vec2 pos, int type);
void game_set_city_production(Game *g, int type);
void game_queue_city_production(Game *g, int type);
void game_fortify_city(Game *g);
void game_order_attack_move(Game *g, Vec2 target);
void game_order_hold(Game *g);
void game_order_stop(Game *g);

/* selection + orders */
void game_clear_selection(Game *g);
void game_click_select(Game *g, Vec2 world, int additive);
void game_box_select(Game *g, Vec2 a, Vec2 b, int additive);
void game_select_all(Game *g);
void game_order_selected(Game *g, const Vec2 *pts, int n);
void game_set_city_rally(Game *g, Vec2 world);

#endif /* GAME_H */
