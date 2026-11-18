#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "game.h"
#include "ai.h"
#include "render.h"

#define WIN_W 1280
#define WIN_H 720
#define SIM_DT (1.0f / 60.0f)

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ================================================================== */
/* Headless AI-vs-AI test                                              */
/* ================================================================== */

static int run_headless(uint32_t seed, float seconds) {
    Game g;
    game_init(&g, seed);
    g.ai_enabled[0] = 1;
    g.ai_enabled[1] = 1;
    g.paused = 0;

    int tcount[T_COUNT] = {0};
    for (int r2 = 0; r2 < ROWS; r2++)
        for (int c2 = 0; c2 < COLS; c2++) tcount[g.w.terr[r2][c2]]++;
    for (int ci = 0; ci < g.ncity; ci++)
        printf("  city %2d  (%4.0f,%3.0f)  owner %d\n",
               ci, g.cities[ci].pos.x, g.cities[ci].pos.y, g.cities[ci].owner);
    printf("seed %u: %d cities, blue cap (%.0f,%.0f) red cap (%.0f,%.0f)  "
           "terrain water %d plains %d forest %d hill %d mountain %d\n",
           seed, g.ncity, g.capital[0].x, g.capital[0].y,
           g.capital[1].x, g.capital[1].y,
           tcount[T_WATER], tcount[T_PLAINS], tcount[T_FOREST],
           tcount[T_HILL], tcount[T_MOUNTAIN]);

    float t = 0.0f;
    int next_report = 15;
    while (t < seconds && g.winner < 0) {
        game_update(&g, SIM_DT);
        ai_update(&g, 0, SIM_DT);
        ai_update(&g, 1, SIM_DT);
        t += SIM_DT;
        if (t >= (float)next_report) {
            Vec2 avg[2] = { {0,0}, {0,0} };
            int busy[2] = {0,0}, near_city[2][MAX_CITIES];
            memset(near_city, 0, sizeof(near_city));
            for (int i = 0; i < MAX_UNITS; i++) {
                const Unit *u = &g.units[i];
                if (!u->alive) continue;
                avg[u->owner] = vadd(avg[u->owner], u->pos);
                if (u->pi < u->plen) busy[u->owner]++;
                for (int c = 0; c < g.ncity; c++)
                    if (vdist(u->pos, g.cities[c].pos) < CITY_CAPTURE_R)
                        near_city[u->owner][c]++;
            }
            for (int p2 = 0; p2 < 2; p2++)
                if (g.units_owned[p2]) avg[p2] = vmul(avg[p2], 1.0f / (float)g.units_owned[p2]);
            printf("  t=%3ds  blue: %d cities %2d units (%d busy, avg %.0f,%.0f)  "
                   "red: %d cities %2d units (%d busy, avg %.0f,%.0f)\n",
                   (int)t, g.cities_owned[0], g.units_owned[0], busy[0], avg[0].x, avg[0].y,
                   g.cities_owned[1], g.units_owned[1], busy[1], avg[1].x, avg[1].y);
            for (int c = 0; c < g.ncity; c++) {
                if (near_city[0][c] == 0 && near_city[1][c] == 0) continue;
                printf("      city %2d owner=%2d cap=%.2f  blue_in=%d red_in=%d\n",
                       c, g.cities[c].owner, g.cities[c].cap,
                       near_city[0][c], near_city[1][c]);
            }
            next_report += 15;
        }
    }
    printf("result: %s at t=%.1fs (blue %d cities/%d units, red %d cities/%d units)\n",
           g.winner < 0 ? "TIMEOUT" : (g.winner == 0 ? "BLUE WINS" : "RED WINS"),
           (double)t, g.cities_owned[0], g.units_owned[0],
           g.cities_owned[1], g.units_owned[1]);
    return g.winner < 0 ? 2 : 0;
}

/* ================================================================== */
/* Interactive game                                                    */
/* ================================================================== */

typedef struct {
    int left_down, middle_down, right_down;
    int shift_down;
    int dragging_box;
    Vec2 press_world;
    int press_x, press_y;
    int last_mx, last_my;
    int path_active;
    Vec2 path[VIEW_PATH_MAX];
    int plen;
    Vec2 last_path_pt;
    uint8_t down[256];
} Input;

static void clamp_camera(View *v) {
    float view_w = (float)v->w / v->zoom;
    float view_h = (float)v->h / v->zoom;
    float maxx = (float)WORLD_W - view_w;
    float maxy = (float)WORLD_H - view_h;
    if (maxx < 0.0f) v->cam.x = maxx * 0.5f;
    else v->cam.x = clampf(v->cam.x, 0.0f, maxx);
    if (maxy < 0.0f) v->cam.y = maxy * 0.5f;
    else v->cam.y = clampf(v->cam.y, 0.0f, maxy);
}

static Vec2 mouse_world(const View *v, int mx, int my) {
    return v2(v->cam.x + (float)mx / v->zoom, v->cam.y + (float)my / v->zoom);
}

static void reset_view(View *v, const Game *g) {
    (void)g;
    float fit = fmaxf((float)v->w / (float)WORLD_W, (float)v->h / (float)WORLD_H);
    v->zoom = fit * 0.90f;
    v->cam.x = ((float)WORLD_W - (float)v->w / v->zoom) * 0.5f;
    v->cam.y = ((float)WORLD_H - (float)v->h / v->zoom) * 0.5f;
    clamp_camera(v);
}

static void apply_path(Game *g, Input *in) {
    if (in->plen <= 0) return;
    if (g->nsel > 0) game_order_selected(g, in->path, in->plen);
    else if (g->sel_city >= 0 && g->cities[g->sel_city].owner == P_BLUE)
        game_set_city_rally(g, in->path[in->plen - 1]);
    in->plen = 0;
}

static void handle_key(Game *g, View *v, Renderer *r, Input *in,
                       KeySym ks, uint32_t *seed, int *running) {
    switch (ks) {
    case XK_Escape:
        game_clear_selection(g);
        break;
    case XK_z:
    case XK_Z:
        game_select_all(g);
        break;
    case XK_space:
        if (g->winner < 0) g->paused = !g->paused;
        break;
    case XK_r:
    case XK_R: {
        uint32_t s = (*seed) * 1664525u + 1013904223u;
        *seed = s;
        game_init(g, s);
        render_free(r);
        render_init(r, g);
        reset_view(v, g);
        in->plen = 0;
        in->path_active = 0;
        in->dragging_box = 0;
        printf("new game, seed %u\n", s);
        break;
    }
    case XK_q:
    case XK_Q:
        *running = 0;
        break;
    default:
        break;
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    uint32_t seed = (uint32_t)time(NULL);
    float headless = 0.0f;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc) headless = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--help")) {
            printf("usage: %s [--seed N] [--headless SECONDS]\n"
                   "  default        play blue vs the offline red AI\n"
                   "  --headless N   run AI vs AI for N simulated seconds\n", argv[0]);
            return 0;
        }
    }
    if (headless > 0.0f) return run_headless(seed, headless);

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open X display (try --headless)\n");
        return 1;
    }
    int scr = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, WIN_W, WIN_H,
                                     0, 0, 0);
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask);
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    XStoreName(dpy, win, "WAR OF DOTS - C");
    XMapWindow(dpy, win);

    Bool detectable;
    XkbSetDetectableAutoRepeat(dpy, True, &detectable);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    FB fb = { .w = WIN_W, .h = WIN_H };
    fb.px = malloc((size_t)WIN_W * WIN_H * sizeof(uint32_t));
    if (!fb.px) { fprintf(stderr, "out of memory\n"); return 1; }
    XImage *img = XCreateImage(dpy, DefaultVisual(dpy, scr),
                               (unsigned)DefaultDepth(dpy, scr), ZPixmap, 0,
                               (char *)fb.px, WIN_W, WIN_H, 32, 0);

    Game g;
    game_init(&g, seed);
    g.ai_enabled[1] = 1;
    printf("WAR OF DOTS (C) - seed %u, %d cities\n", seed, g.ncity);

    Renderer r;
    render_init(&r, &g);

    View v = { .w = WIN_W, .h = WIN_H };
    reset_view(&v, &g);
    Input in;
    memset(&in, 0, sizeof(in));

    int running = 1;
    double last = now_sec();
    int redraw = 1;

    while (running) {
        while (XPending(dpy)) {
            XEvent e;
            XNextEvent(dpy, &e);
            switch (e.type) {
            case ClientMessage:
                if ((Atom)e.xclient.data.l[0] == wm_delete) running = 0;
                break;
            case Expose:
                redraw = 1;
                break;
            case KeyPress: {
                KeySym ks = XLookupKeysym(&e.xkey, 0);
                if (e.xkey.keycode < 256 && !in.down[e.xkey.keycode]) {
                    in.down[e.xkey.keycode] = 1;
                    if (ks == XK_Shift_L || ks == XK_Shift_R) in.shift_down = 1;
                    else handle_key(&g, &v, &r, &in, ks, &seed, &running);
                }
                break;
            }
            case KeyRelease: {
                KeySym ks = XLookupKeysym(&e.xkey, 0);
                if (e.xkey.keycode < 256) in.down[e.xkey.keycode] = 0;
                if (ks == XK_Shift_L || ks == XK_Shift_R) in.shift_down = 0;
                break;
            }
            case ButtonPress: {
                int mx = e.xbutton.x, my = e.xbutton.y;
                if (e.xbutton.button == Button1) {
                    in.left_down = 1;
                    in.press_x = mx;
                    in.press_y = my;
                    in.press_world = mouse_world(&v, mx, my);
                    in.dragging_box = 0;
                } else if (e.xbutton.button == Button3) {
                    in.right_down = 1;
                    if (g.nsel > 0 ||
                        (g.sel_city >= 0 && g.cities[g.sel_city].owner == P_BLUE)) {
                        in.path_active = 1;
                        in.plen = 0;
                        in.path[in.plen++] = mouse_world(&v, mx, my);
                        in.last_path_pt = in.path[0];
                    }
                } else if (e.xbutton.button == Button2) {
                    in.middle_down = 1;
                    in.last_mx = mx;
                    in.last_my = my;
                } else if (e.xbutton.button == Button4 || e.xbutton.button == Button5) {
                    float old = v.zoom;
                    float nz = old * (e.xbutton.button == Button4 ? 1.15f : 1.0f / 1.15f);
                    nz = clampf(nz, 0.65f, 3.0f);
                    Vec2 w = mouse_world(&v, mx, my);
                    v.zoom = nz;
                    v.cam.x = w.x - (float)mx / nz;
                    v.cam.y = w.y - (float)my / nz;
                    clamp_camera(&v);
                }
                break;
            }
            case ButtonRelease: {
                int mx = e.xbutton.x, my = e.xbutton.y;
                if (e.xbutton.button == Button1) {
                    if (in.dragging_box) {
                        game_box_select(&g, in.press_world, mouse_world(&v, mx, my),
                                        in.shift_down);
                    } else {
                        game_click_select(&g, in.press_world, in.shift_down);
                    }
                    in.left_down = 0;
                    in.dragging_box = 0;
                } else if (e.xbutton.button == Button3) {
                    if (in.path_active) {
                        apply_path(&g, &in);
                        in.path_active = 0;
                    }
                    in.right_down = 0;
                } else if (e.xbutton.button == Button2) {
                    in.middle_down = 0;
                }
                break;
            }
            case MotionNotify: {
                int mx = e.xmotion.x, my = e.xmotion.y;
                if (in.left_down) {
                    int dx = mx - in.press_x, dy = my - in.press_y;
                    if (dx * dx + dy * dy > 36) in.dragging_box = 1;
                }
                if (in.middle_down) {
                    v.cam.x -= (float)(mx - in.last_mx) / v.zoom;
                    v.cam.y -= (float)(my - in.last_my) / v.zoom;
                    clamp_camera(&v);
                }
                if (in.path_active && in.plen < VIEW_PATH_MAX) {
                    Vec2 w = mouse_world(&v, mx, my);
                    if (vdist(w, in.last_path_pt) > 16.0f) {
                        in.path[in.plen++] = w;
                        in.last_path_pt = w;
                    }
                }
                in.last_mx = mx;
                in.last_my = my;
                break;
            }
            default:
                break;
            }
        }

        double t = now_sec();
        float dt = (float)(t - last);
        last = t;
        if (dt > 0.1f) dt = 0.1f;

        /* camera keys */
        float pan = 520.0f * dt / v.zoom;
        if (in.down[XKeysymToKeycode(dpy, XK_a)] || in.down[XKeysymToKeycode(dpy, XK_Left)])
            v.cam.x -= pan;
        if (in.down[XKeysymToKeycode(dpy, XK_d)] || in.down[XKeysymToKeycode(dpy, XK_Right)])
            v.cam.x += pan;
        if (in.down[XKeysymToKeycode(dpy, XK_w)] || in.down[XKeysymToKeycode(dpy, XK_Up)])
            v.cam.y -= pan;
        if (in.down[XKeysymToKeycode(dpy, XK_s)] || in.down[XKeysymToKeycode(dpy, XK_Down)])
            v.cam.y += pan;
        /* A is also "select all"; camera pan only while held is fine, but avoid
         * panning on a quick tap: only pan if the key is still down next frames. */

        clamp_camera(&v);
        game_update(&g, dt);
        if (g.ai_enabled[1]) ai_update(&g, 1, dt);

        v.box_active = in.dragging_box;
        if (in.dragging_box) {
            v.box_a = in.press_world;
            v.box_b = mouse_world(&v, in.last_mx, in.last_my);
        }
        v.path_active = in.path_active;
        v.plen = in.plen;
        memcpy(v.path, in.path, sizeof(Vec2) * (size_t)in.plen);

        (void)redraw;
        render_world(&fb, &g, &r, &v);
        render_hud(&fb, &g, &r, &v);
        XPutImage(dpy, win, gc, img, 0, 0, 0, 0, WIN_W, WIN_H);
        XFlush(dpy);

        double frame_end = now_sec();
        double spare = SIM_DT - (frame_end - t);
        if (spare > 0.0) {
            struct timespec ts;
            ts.tv_sec = (time_t)spare;
            ts.tv_nsec = (long)((spare - (double)ts.tv_sec) * 1e9);
            nanosleep(&ts, NULL);
        }
    }

    render_free(&r);
    XDestroyImage(img);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
