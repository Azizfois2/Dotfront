#ifndef WOD_PLATFORM_H
#define WOD_PLATFORM_H

#include <stdint.h>

#define WIN_W 1280
#define WIN_H 720

/* Platform-neutral keys used by the game loop. */
typedef enum {
    PK_NONE = 0,
    PK_SPACE, PK_ESC, PK_R, PK_Q, PK_Z,
    PK_A, PK_D, PK_W, PK_S,
    PK_1, PK_2, PK_3, PK_4, PK_5, PK_6,
    PK_F5, PK_F9, PK_M, PK_G, PK_H, PK_X, PK_F, PK_C, PK_T,
    PK_LEFT, PK_RIGHT, PK_UP, PK_DOWN,
    PK_SHIFT, PK_CTRL,
    PK_COUNT
} PlatKey;

typedef enum {
    PEV_NONE = 0,
    PEV_QUIT,
    PEV_KEYDOWN,
    PEV_KEYUP,
    PEV_MOUSEDOWN,
    PEV_MOUSEUP,
    PEV_MOUSEMOVE,
    PEV_WHEEL
} PlatEventType;

typedef struct {
    PlatEventType type;
    PlatKey key;
    int button;   /* 1 = left, 2 = middle, 3 = right */
    int x, y;     /* client/window coordinates */
    int wheel;    /* +1 = up, -1 = down, one notch per unit */
} PlatEvent;

/* Short UI/game sounds; backends may ignore them. */
enum {
    PSND_SELECT = 0, PSND_ORDER, PSND_CAPTURE, PSND_DEATH, PSND_WIN, PSND_UI,
    PSND_COUNT
};

typedef struct Platform Platform;

Platform *plat_open(const char *title, int w, int h);
void      plat_close(Platform *p);

/* Returns the back buffer to draw into. Call once per frame, before drawing. */
uint32_t *plat_pixels(Platform *p);

/* Presents the buffer most recently returned by plat_pixels(). */
void      plat_present(Platform *p);

/* Non-blocking event pump. Returns 1 and fills *ev when an event is available. */
int       plat_poll(Platform *p, PlatEvent *ev);

/* Monotonic seconds. */
double    plat_time(void);

/* Sleep for the given number of seconds (frame pacing). */
void      plat_sleep(double seconds);

/* Best-effort system sound, asynchronous where possible. */
void      plat_sound(int kind);

#endif /* WOD_PLATFORM_H */
