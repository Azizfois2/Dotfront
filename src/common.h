/* Shared types and tuning constants for the offline dot RTS. */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <math.h>

/* ---- world ---------------------------------------------------------- */
/* Maps are sized from the player count at runtime. These are only the
 * storage limits; every loop uses World.cols/rows and World.width/height. */
#define MAX_WORLD_W 2400
#define MAX_WORLD_H 1350
#define MIN_WORLD_W 1600
#define MIN_WORLD_H 900
#define CELL        20
#define MAX_COLS    (MAX_WORLD_W / CELL)   /* 120 */
#define MAX_ROWS    (MAX_WORLD_H / CELL)   /* 67 */

#define MAX_PLAYERS  6
#define MAX_UNITS    180
#define MAX_CITIES   32
#define MAX_PATH     256

/* ---- rules ---------------------------------------------------------- */
#define UNIT_R           5.0f
#define CITY_R           15.0f
#define CITY_CAPTURE_R   36.0f
#define ATTACK_RANGE     30.0f
#define ALLY_GAP         10.0f
#define ENEMY_GAP        20.0f
#define MAX_HP           100.0f
#define UNIT_SPEED       140.0f  /* px per second on plains */
#define UNIT_DPS         12.0f
#define CAPTURE_TIME     2.0f
#define PROD_BASE        7.0f
#define PROD_PER_UNIT    0.22f
#define UNIT_CAP         60   /* upper bound; per-player cap scales with player count */
#define SUPPLY_CLOSE     240.0f
#define SUPPLY_FAR       620.0f
#define TERRITORY_RADIUS 230.0f

enum { T_WATER, T_PLAINS, T_FOREST, T_HILL, T_MOUNTAIN, T_COUNT };
enum { P_BLUE = 0, P_RED = 1, P_GREEN = 2, P_GOLD = 3, P_VIOLET = 4, P_CYAN = 5 };
enum { MAP_ISLAND = 0, MAP_ARCHIPELAGO = 1, MAP_LAKES = 2, MAP_HIGHLANDS = 3,
       MAP_STYLE_COUNT };

/* AI profiles selectable with --difficulty or 1/2/3 in the menu. */
enum { DIFF_EASY = 0, DIFF_NORMAL = 1, DIFF_HARD = 2, DIFF_COUNT };

/* Unit roles. */
enum { UT_SCOUT = 0, UT_SOLDIER = 1, UT_HEAVY = 2, UT_COUNT };

/* Movement orders. */
enum { ORD_MOVE = 0, ORD_ATTACK_MOVE = 1, ORD_HOLD = 2, ORD_COUNT };

typedef struct {
    float hp;
    float speed;      /* multiplier on UNIT_SPEED */
    float dps;
    float range;
    float vision;     /* base radius, terrain modifies it */
    float radius;     /* drawn/separation size */
    float build_time; /* multiplier on city production interval */
    const char *name;
} UnitType;

extern const UnitType UNIT_TYPE[UT_COUNT];

/* One-frame sound events raised by game logic and consumed by main. */
enum {
    SFX_SELECT = 1 << 0, SFX_ORDER = 1 << 1, SFX_CAPTURE = 1 << 2,
    SFX_DEATH = 1 << 3, SFX_WIN = 1 << 4, SFX_UI = 1 << 5, SFX_COUNT = 6
};

typedef struct { float x, y; } Vec2;

/* Player colours + neutrals, 0xAARRGGBB (alpha unused, kept for clarity). */
#define C_BLUE      0xFF3D7BFF
#define C_BLUE_DARK 0xFF1E4FC4
#define C_RED       0xFFFF4D4D
#define C_RED_DARK  0xFFC42B2B
#define C_GREEN     0xFF4BD46B
#define C_GREEN_DARK 0xFF27883B
#define C_GOLD      0xFFF2C14E
#define C_GOLD_DARK 0xFFA87A16
#define C_VIOLET    0xFFB678FF
#define C_VIOLET_DARK 0xFF6E35B8
#define C_CYAN      0xFF4BD8DA
#define C_CYAN_DARK 0xFF1D8587
#define C_NEUTRAL   0xFFB8B8B8
#define C_BLACK     0xFF101014
#define C_WHITE     0xFFF5F5F5
#define C_CITY      0xFFF2C14E

typedef struct {
    int      owner;               /* -1 neutral, otherwise P_* */
    Vec2     pos;
    float    hp, max_hp;
    uint8_t  type;                /* UT_* */
    uint8_t  order;               /* ORD_* */
    Vec2     path[MAX_PATH];
    int      plen, pi;
    uint8_t  alive;
    float    flash;               /* hit flash timer, seconds */
    float    stall_t;             /* stalled-path watchdog */
    Vec2     last_pos;
} Unit;

typedef struct {
    Vec2     pos;
    int      owner;
    float    cap;                 /* capture progress 0..1 */
    float    cap_owner;           /* who is making progress (-1 none) */
    float    spawn_t;
    Vec2     rally;
    int      has_rally;
    uint8_t  prod_type;           /* UT_* */
    uint8_t  fortified;
    float    fort_t;              /* fortification build timer */
} City;

/* ---- small vector helpers ------------------------------------------ */
static inline Vec2 v2(float x, float y) { Vec2 v = { x, y }; return v; }
static inline Vec2 vadd(Vec2 a, Vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline Vec2 vsub(Vec2 a, Vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline Vec2 vmul(Vec2 a, float s) { return v2(a.x * s, a.y * s); }
static inline float vdot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
static inline float vlen(Vec2 a) { return sqrtf(vdot(a, a)); }
static inline float vdist(Vec2 a, Vec2 b) { return vlen(vsub(a, b)); }
static inline Vec2 vnorm(Vec2 a) {
    float l = vlen(a);
    return l > 1e-6f ? vmul(a, 1.0f / l) : v2(0, 0);
}
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothf(float t) { return t * t * (3.0f - 2.0f * t); }

/* Terrain multipliers, indexed by terrain id. */
extern const float    TERRAIN_SPEED[T_COUNT];
extern const float    TERRAIN_ATTACK[T_COUNT];
extern const uint32_t TERRAIN_COLOR[T_COUNT];
extern const char    *TERRAIN_NAME[T_COUNT];

#endif /* COMMON_H */
