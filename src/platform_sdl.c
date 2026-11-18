#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

struct Platform {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    uint32_t     *pixels;
    int           w, h;
    int           quit;
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
    p->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              w, h, SDL_WINDOW_SHOWN);
    if (!p->win) goto fail;

    p->ren = SDL_CreateRenderer(p->win, -1,
                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!p->ren)
        p->ren = SDL_CreateRenderer(p->win, -1, SDL_RENDERER_SOFTWARE);
    if (!p->ren) goto fail;

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

int plat_poll(Platform *p, PlatEvent *ev) {
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
        default:
            break;
        }
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
