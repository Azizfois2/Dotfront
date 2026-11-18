#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "platform.h"
#include "icon_rgba.h"

struct Platform {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    uint32_t     *pixels;
    int           w, h;
    int           quit;

    /* touch gesture state */
    int           touch_active;
    SDL_FingerID  touch_id;
    int           touch_x0, touch_y0, touch_x, touch_y;
    Uint32        touch_start;
    int           touch_button;     /* 0 pending, 1 left drag, 3 long-press move */
    int           touch_long;
    int           two_active;
    SDL_FingerID  two_id;
    int           two_x, two_y;
    float         pinch_dist;
    int           pending_up;
    int           pending_x, pending_y;
};

static PlatKey map_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_SPACE:    return PK_SPACE;
    case SDLK_ESCAPE:   return PK_ESC;
    case SDLK_r:        return PK_R;
    case SDLK_q:        return PK_Q;
    case SDLK_z:        return PK_Z;
    case SDLK_a:        return PK_A;
    case SDLK_d:        return PK_D;
    case SDLK_w:        return PK_W;
    case SDLK_s:        return PK_S;
    case SDLK_1:        return PK_1;
    case SDLK_2:        return PK_2;
    case SDLK_3:        return PK_3;
    case SDLK_4:        return PK_4;
    case SDLK_5:        return PK_5;
    case SDLK_6:        return PK_6;
    case SDLK_F5:       return PK_F5;
    case SDLK_f:        return PK_F;
    case SDLK_F9:       return PK_F9;
    case SDLK_m:        return PK_M;
    case SDLK_g:        return PK_G;
    case SDLK_h:        return PK_H;
    case SDLK_x:        return PK_X;
    case SDLK_c:        return PK_C;
    case SDLK_t:        return PK_T;
    case SDLK_LEFT:     return PK_LEFT;
    case SDLK_RIGHT:    return PK_RIGHT;
    case SDLK_UP:       return PK_UP;
    case SDLK_DOWN:     return PK_DOWN;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:   return PK_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL:    return PK_CTRL;
    default:            return PK_NONE;
    }
}

Platform *plat_open(const char *title, int w, int h) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return NULL;
    }
    Platform *p = calloc(1, sizeof(*p));
    if (!p) { SDL_Quit(); return NULL; }
    p->w = w;
    p->h = h;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    p->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              w, h, SDL_WINDOW_SHOWN);
    if (!p->win) goto fail;
    SDL_Surface *icon = SDL_CreateRGBSurfaceFrom((void *)wod_icon_rgba,
        WOD_ICON_W, WOD_ICON_H, 32, WOD_ICON_W * 4,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (icon) {
        SDL_SetWindowIcon(p->win, icon);
        SDL_FreeSurface(icon);
    }

    p->ren = SDL_CreateRenderer(p->win, -1,
                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!p->ren)
        p->ren = SDL_CreateRenderer(p->win, -1, SDL_RENDERER_SOFTWARE);
    if (!p->ren) goto fail;
    SDL_RenderSetLogicalSize(p->ren, w, h);

    p->tex = SDL_CreateTexture(p->ren, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!p->tex) goto fail;

    p->pixels = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!p->pixels) goto fail;
    memset(p->pixels, 0, (size_t)w * (size_t)h * sizeof(uint32_t));
    return p;

fail:
    fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
    plat_close(p);
    return NULL;
}

uint32_t *plat_pixels(Platform *p) {
    return p->pixels;
}

void plat_present(Platform *p) {
    SDL_UpdateTexture(p->tex, NULL, p->pixels, p->w * (int)sizeof(uint32_t));
    SDL_RenderClear(p->ren);
    SDL_RenderCopy(p->ren, p->tex, NULL, NULL);
    SDL_RenderPresent(p->ren);
}

static void touch_begin_right(Platform *p, PlatEvent *ev) {
    ev->type = PEV_MOUSEDOWN;
    ev->button = 3;
    ev->x = p->touch_x0;
    ev->y = p->touch_y0;
    p->touch_button = 3;
    p->touch_long = 1;
}

static int touch_two_points(Platform *p, int *mx, int *my, float *dist) {
    if (!p->touch_active || !p->two_active) return 0;
    *mx = (p->touch_x + p->two_x) / 2;
    *my = (p->touch_y + p->two_y) / 2;
    float dx = (float)(p->touch_x - p->two_x);
    float dy = (float)(p->touch_y - p->two_y);
    *dist = sqrtf(dx * dx + dy * dy);
    return 1;
}

int plat_poll(Platform *p, PlatEvent *ev) {
    if (p->pending_up) {
        p->pending_up = 0;
        ev->type = PEV_MOUSEUP;
        ev->button = 1;
        ev->x = p->pending_x;
        ev->y = p->pending_y;
        return 1;
    }
    SDL_Event se;
    while (SDL_PollEvent(&se)) {
        switch (se.type) {
        case SDL_QUIT:
            p->quit = 1;
            ev->type = PEV_QUIT;
            return 1;
        case SDL_KEYDOWN: {
            if (se.key.repeat) continue;
            PlatKey k = map_key(se.key.keysym.sym);
            if (k == PK_NONE) continue;
            ev->type = PEV_KEYDOWN;
            ev->key = k;
            return 1;
        }
        case SDL_KEYUP: {
            PlatKey k = map_key(se.key.keysym.sym);
            if (k == PK_NONE) continue;
            ev->type = PEV_KEYUP;
            ev->key = k;
            return 1;
        }
        case SDL_MOUSEBUTTONDOWN:
            ev->type = PEV_MOUSEDOWN;
            ev->button = se.button.button == SDL_BUTTON_LEFT ? 1 :
                         se.button.button == SDL_BUTTON_MIDDLE ? 2 : 3;
            ev->x = se.button.x;
            ev->y = se.button.y;
            return 1;
        case SDL_MOUSEBUTTONUP:
            ev->type = PEV_MOUSEUP;
            ev->button = se.button.button == SDL_BUTTON_LEFT ? 1 :
                         se.button.button == SDL_BUTTON_MIDDLE ? 2 : 3;
            ev->x = se.button.x;
            ev->y = se.button.y;
            return 1;
        case SDL_MOUSEMOTION:
            ev->type = PEV_MOUSEMOVE;
            ev->x = se.motion.x;
            ev->y = se.motion.y;
            return 1;
        case SDL_MOUSEWHEEL: {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            ev->type = PEV_WHEEL;
            ev->wheel = se.wheel.y;
            ev->x = mx;
            ev->y = my;
            return 1;
        }
        case SDL_FINGERDOWN: {
            int x = (int)(se.tfinger.x * (float)p->w);
            int y = (int)(se.tfinger.y * (float)p->h);
            if (!p->touch_active) {
                p->touch_active = 1;
                p->touch_id = se.tfinger.fingerId;
                p->touch_x0 = p->touch_x = x;
                p->touch_y0 = p->touch_y = y;
                p->touch_start = SDL_GetTicks();
                p->touch_button = 0;
                p->touch_long = 0;
            } else if (!p->two_active && p->touch_button == 0 &&
                       se.tfinger.fingerId != p->touch_id) {
                p->two_active = 1;
                p->two_id = se.tfinger.fingerId;
                p->two_x = x;
                p->two_y = y;
                int mx, my;
                float d;
                if (touch_two_points(p, &mx, &my, &d)) {
                    p->pinch_dist = d;
                    ev->type = PEV_MOUSEDOWN;
                    ev->button = 2;
                    ev->x = mx;
                    ev->y = my;
                    return 1;
                }
            }
            continue;
        }
        case SDL_FINGERMOTION: {
            int x = (int)(se.tfinger.x * (float)p->w);
            int y = (int)(se.tfinger.y * (float)p->h);
            if (p->two_active &&
                (se.tfinger.fingerId == p->touch_id ||
                 se.tfinger.fingerId == p->two_id)) {
                if (se.tfinger.fingerId == p->touch_id) {
                    p->touch_x = x;
                    p->touch_y = y;
                } else {
                    p->two_x = x;
                    p->two_y = y;
                }
                int mx, my;
                float d;
                if (touch_two_points(p, &mx, &my, &d)) {
                    if (fabsf(d - p->pinch_dist) > 22.0f) {
                        ev->type = PEV_WHEEL;
                        ev->wheel = d > p->pinch_dist ? 1 : -1;
                        ev->x = mx;
                        ev->y = my;
                        p->pinch_dist = d;
                        return 1;
                    }
                    ev->type = PEV_MOUSEMOVE;
                    ev->x = mx;
                    ev->y = my;
                    return 1;
                }
            } else if (p->touch_active && se.tfinger.fingerId == p->touch_id) {
                p->touch_x = x;
                p->touch_y = y;
                if (!p->touch_button) {
                    int dx = x - p->touch_x0, dy = y - p->touch_y0;
                    if (dx * dx + dy * dy > 100) {
                        ev->type = PEV_MOUSEDOWN;
                        ev->button = 1;
                        ev->x = p->touch_x0;
                        ev->y = p->touch_y0;
                        p->touch_button = 1;
                        return 1;
                    }
                } else {
                    ev->type = PEV_MOUSEMOVE;
                    ev->x = x;
                    ev->y = y;
                    return 1;
                }
            }
            continue;
        }
        case SDL_FINGERUP: {
            int x = (int)(se.tfinger.x * (float)p->w);
            int y = (int)(se.tfinger.y * (float)p->h);
            if (p->two_active) {
                if (se.tfinger.fingerId == p->touch_id ||
                    se.tfinger.fingerId == p->two_id) {
                    p->two_active = 0;
                    p->touch_active = 0;
                    ev->type = PEV_MOUSEUP;
                    ev->button = 2;
                    ev->x = x;
                    ev->y = y;
                    return 1;
                }
            } else if (p->touch_active && se.tfinger.fingerId == p->touch_id) {
                if (p->touch_button) {
                    ev->type = PEV_MOUSEUP;
                    ev->button = p->touch_button;
                    ev->x = x;
                    ev->y = y;
                } else {
                    ev->type = PEV_MOUSEDOWN;
                    ev->button = 1;
                    ev->x = x;
                    ev->y = y;
                    p->pending_up = 1;
                    p->pending_x = x;
                    p->pending_y = y;
                }
                p->touch_active = 0;
                p->touch_button = 0;
                return 1;
            }
            continue;
        }
        default:
            break;
        }
    }
    if (p->touch_active && !p->touch_button && !p->two_active &&
        SDL_GetTicks() - p->touch_start > 450) {
        touch_begin_right(p, ev);
        return 1;
    }
    return 0;
}

void plat_sound(int kind) {
    (void)kind;   /* audio is not initialised in the SDL2 build */
}

double plat_time(void) {
    return (double)SDL_GetPerformanceCounter() /
           (double)SDL_GetPerformanceFrequency();
}

void plat_sleep(double seconds) {
    if (seconds <= 0.0) return;
    SDL_Delay((Uint32)(seconds * 1000.0));
}

void plat_close(Platform *p) {
    if (!p) return;
    free(p->pixels);
    if (p->tex) SDL_DestroyTexture(p->tex);
    if (p->ren) SDL_DestroyRenderer(p->ren);
    if (p->win) SDL_DestroyWindow(p->win);
    SDL_Quit();
    free(p);
}
