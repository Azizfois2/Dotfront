#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"
#include "icon_rgba.h"

static Platform *g_plat;

struct Platform {
    Display *dpy;
    Window   win;
    GC       gc;
    Atom     wm_delete;
    int      w, h;

    XImage          *img[2];
    XShmSegmentInfo  shm[2];
    uint32_t        *shmbuf[2];
    int              nbuf, shm_base;
    int              freebuf[2], curbuf, drawbuf;

    uint32_t *fbuf;   /* non-SHM fallback */
    int       quit;
};

static void set_window_icon(Platform *p) {
    const int w = WOD_ICON_W, h = WOD_ICON_H;
    unsigned long *data = malloc((size_t)(2 + w * h) * sizeof(unsigned long));
    if (!data) return;
    data[0] = (unsigned long)w;
    data[1] = (unsigned long)h;
    for (int i = 0; i < w * h; i++) {
        const unsigned char *c = &wod_icon_rgba[i * 4];
        data[2 + i] = ((unsigned long)c[3] << 24) |
                      ((unsigned long)c[0] << 16) |
                      ((unsigned long)c[1] << 8) | (unsigned long)c[2];
    }
    Atom prop = XInternAtom(p->dpy, "_NET_WM_ICON", False);
    XChangeProperty(p->dpy, p->win, prop, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)data, 2 + w * h);
    free(data);
}

static PlatKey map_key(KeySym ks) {
    switch (ks) {
    case XK_space:    return PK_SPACE;
    case XK_Escape:   return PK_ESC;
    case XK_r: case XK_R: return PK_R;
    case XK_q: case XK_Q: return PK_Q;
    case XK_z: case XK_Z: return PK_Z;
    case XK_a: case XK_A: return PK_A;
    case XK_d: case XK_D: return PK_D;
    case XK_w: case XK_W: return PK_W;
    case XK_s: case XK_S: return PK_S;
    case XK_1:        return PK_1;
    case XK_2:        return PK_2;
    case XK_3:        return PK_3;
    case XK_4:        return PK_4;
    case XK_5:        return PK_5;
    case XK_6:        return PK_6;
    case XK_F5:       return PK_F5;
    case XK_f: case XK_F: return PK_F;
    case XK_F9:       return PK_F9;
    case XK_m: case XK_M: return PK_M;
    case XK_g: case XK_G: return PK_G;
    case XK_h: case XK_H: return PK_H;
    case XK_x: case XK_X: return PK_X;
    case XK_c: case XK_C: return PK_C;
    case XK_t: case XK_T: return PK_T;
    case XK_Left:     return PK_LEFT;
    case XK_Right:    return PK_RIGHT;
    case XK_Up:       return PK_UP;
    case XK_Down:     return PK_DOWN;
    case XK_Shift_L:
    case XK_Shift_R:  return PK_SHIFT;
    case XK_Control_L:
    case XK_Control_R: return PK_CTRL;
    default:          return PK_NONE;
    }
}

void plat_sleep(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

void plat_sound(int kind) {
    static const int volume[PSND_COUNT] = { 35, 45, 70, 30, 90, 40 };
    if (!g_plat || !g_plat->dpy) return;
    if (kind < 0 || kind >= PSND_COUNT) kind = PSND_UI;
    XBell(g_plat->dpy, volume[kind]);
    XFlush(g_plat->dpy);
}

double plat_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Keep the window inside the WM work area so docks/panels do not cover it. */
static void center_in_workarea(Platform *p) {
    Atom wa = XInternAtom(p->dpy, "_NET_WORKAREA", False);
    Atom type;
    int fmt;
    unsigned long n = 0, after = 0;
    unsigned char *data = NULL;
    if (XGetWindowProperty(p->dpy, DefaultRootWindow(p->dpy), wa, 0, 4,
                           False, XA_CARDINAL, &type, &fmt, &n, &after,
                           &data) == Success && data && n >= 4) {
        long *v = (long *)data;
        long x = v[0] + (v[2] - p->w) / 2;
        long y = v[1] + (v[3] - p->h) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        XMoveWindow(p->dpy, p->win, (int)x, (int)y);

        /* Ask the WM for the same geometry; plain XMoveWindow is often
         * overridden by the initial placement policy. */
        Atom mr = XInternAtom(p->dpy, "_NET_MOVERESIZE_WINDOW", False);
        XEvent e;
        memset(&e, 0, sizeof(e));
        e.xclient.type = ClientMessage;
        e.xclient.window = p->win;
        e.xclient.message_type = mr;
        e.xclient.format = 32;
        e.xclient.data.l[0] = 0x0F00 | (1 << 12);
        e.xclient.data.l[1] = x;
        e.xclient.data.l[2] = y;
        e.xclient.data.l[3] = p->w;
        e.xclient.data.l[4] = p->h;
        XSendEvent(p->dpy, DefaultRootWindow(p->dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &e);
        XFree(data);
    }
    XFlush(p->dpy);
}

Platform *plat_open(const char *title, int w, int h) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open X display (try --headless)\n");
        return NULL;
    }
    Platform *p = calloc(1, sizeof(*p));
    if (!p) { XCloseDisplay(dpy); return NULL; }
    p->dpy = dpy;
    g_plat = p;
    p->w = w;
    p->h = h;

    int scr = DefaultScreen(dpy);
    p->win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0,
                                 (unsigned)w, (unsigned)h, 0, 0, 0);
    XSelectInput(dpy, p->win, ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask);
    p->wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, p->win, &p->wm_delete, 1);
    XStoreName(dpy, p->win, title);
    set_window_icon(p);
    XMapWindow(dpy, p->win);
    XSync(dpy, False);
    center_in_workarea(p);

    Bool detectable;
    XkbSetDetectableAutoRepeat(dpy, True, &detectable);

    p->gc = XCreateGC(dpy, p->win, 0, NULL);
    p->freebuf[0] = p->freebuf[1] = 1;

    /* Shared-memory presentation avoids a 3.7 MB client->server copy per frame. */
    if (XShmQueryExtension(dpy)) {
        p->shm_base = XShmGetEventBase(dpy);
        for (int i = 0; i < 2; i++) {
            p->img[i] = XShmCreateImage(dpy, DefaultVisual(dpy, scr),
                                        (unsigned)DefaultDepth(dpy, scr), ZPixmap,
                                        NULL, &p->shm[i], (unsigned)w, (unsigned)h);
            if (!p->img[i] || p->img[i]->bytes_per_line != w * 4) break;
            p->shm[i].shmid = shmget(IPC_PRIVATE,
                                     (size_t)p->img[i]->bytes_per_line *
                                     (size_t)p->img[i]->height,
                                     IPC_CREAT | 0600);
            if (p->shm[i].shmid < 0) break;
            p->shm[i].shmaddr = shmat(p->shm[i].shmid, NULL, 0);
            p->shm[i].readOnly = False;
            if (p->shm[i].shmaddr == (char *)-1) break;
            p->img[i]->data = p->shm[i].shmaddr;
            if (!XShmAttach(dpy, &p->shm[i])) break;
            XSync(dpy, False);
            shmctl(p->shm[i].shmid, IPC_RMID, 0);
            p->shmbuf[i] = (uint32_t *)p->img[i]->data;
            p->nbuf++;
        }
    }
    if (p->nbuf == 0) {
        p->fbuf = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
        if (!p->fbuf) { plat_close(p); return NULL; }
        p->img[0] = XCreateImage(dpy, DefaultVisual(dpy, scr),
                                 (unsigned)DefaultDepth(dpy, scr), ZPixmap, 0,
                                 (char *)p->fbuf, (unsigned)w, (unsigned)h, 32, 0);
    }
    return p;
}

uint32_t *plat_pixels(Platform *p) {
    if (p->nbuf == 0) return p->fbuf;
    if (!p->freebuf[p->curbuf]) {
        XSync(p->dpy, False);
        p->freebuf[0] = p->freebuf[1] = 1;
    }
    p->drawbuf = p->curbuf;
    return p->shmbuf[p->curbuf];
}

void plat_present(Platform *p) {
    if (p->nbuf > 0) {
        int b = p->drawbuf;
        if (p->nbuf > 1) {
            XShmPutImage(p->dpy, p->win, p->gc, p->img[b], 0, 0, 0, 0,
                         p->w, p->h, True);
            p->freebuf[b] = 0;
            p->curbuf = b ^ 1;
        } else {
            XShmPutImage(p->dpy, p->win, p->gc, p->img[0], 0, 0, 0, 0,
                         p->w, p->h, False);
            XSync(p->dpy, False);
        }
        XFlush(p->dpy);
    } else {
        XPutImage(p->dpy, p->win, p->gc, p->img[0], 0, 0, 0, 0,
                  p->w, p->h);
        XFlush(p->dpy);
    }
}

int plat_poll(Platform *p, PlatEvent *ev) {
    Display *dpy = p->dpy;
    while (XPending(dpy)) {
        XEvent e;
        XNextEvent(dpy, &e);
        if (p->nbuf > 0 && e.type == p->shm_base + ShmCompletion) {
            XShmCompletionEvent *ce = (XShmCompletionEvent *)&e;
            for (int i = 0; i < p->nbuf; i++)
                if (p->shm[i].shmseg == ce->shmseg)
                    p->freebuf[i] = 1;
            continue;
        }
        switch (e.type) {
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == p->wm_delete) {
                p->quit = 1;
                ev->type = PEV_QUIT;
                return 1;
            }
            break;
        case KeyPress: {
            PlatKey k = map_key(XLookupKeysym(&e.xkey, 0));
            if (k == PK_NONE) continue;
            ev->type = PEV_KEYDOWN;
            ev->key = k;
            return 1;
        }
        case KeyRelease: {
            PlatKey k = map_key(XLookupKeysym(&e.xkey, 0));
            if (k == PK_NONE) continue;
            ev->type = PEV_KEYUP;
            ev->key = k;
            return 1;
        }
        case ButtonPress:
            ev->x = e.xbutton.x;
            ev->y = e.xbutton.y;
            if (e.xbutton.button == Button4 || e.xbutton.button == Button5) {
                ev->type = PEV_WHEEL;
                ev->wheel = e.xbutton.button == Button4 ? 1 : -1;
            } else {
                ev->type = PEV_MOUSEDOWN;
                ev->button = e.xbutton.button == Button1 ? 1 :
                             e.xbutton.button == Button2 ? 2 : 3;
            }
            return 1;
        case ButtonRelease:
            ev->type = PEV_MOUSEUP;
            ev->x = e.xbutton.x;
            ev->y = e.xbutton.y;
            ev->button = e.xbutton.button == Button1 ? 1 :
                         e.xbutton.button == Button2 ? 2 : 3;
            return 1;
        case MotionNotify:
            ev->type = PEV_MOUSEMOVE;
            ev->x = e.xmotion.x;
            ev->y = e.xmotion.y;
            return 1;
        default:
            break;
        }
    }
    return 0;
}

void plat_close(Platform *p) {
    if (!p) return;
    if (p->nbuf > 0) {
        for (int i = 0; i < p->nbuf; i++) {
            XShmDetach(p->dpy, &p->shm[i]);
            shmdt(p->shm[i].shmaddr);
            p->img[i]->data = NULL;
            free(p->img[i]);
        }
    } else {
        if (p->img[0]) XDestroyImage(p->img[0]);
        p->fbuf = NULL;
    }
    XFreeGC(p->dpy, p->gc);
    XDestroyWindow(p->dpy, p->win);
    XCloseDisplay(p->dpy);
    if (g_plat == p) g_plat = NULL;
    free(p);
}
