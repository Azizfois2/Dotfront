#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

struct Platform {
    HWND      hwnd;
    HDC       memdc;
    HDC       windc;
    HBITMAP   dib;
    uint32_t *pixels;
    int       w, h;
    int       quit;

    PlatEvent q[256];
    int       qh, qt;

    LARGE_INTEGER freq, start;
    int wheel_acc;
};

static Platform *g_plat;

static PlatKey map_vk(WPARAM vk) {
    switch (vk) {
    case VK_SPACE:    return PK_SPACE;
    case VK_ESCAPE:   return PK_ESC;
    case 'R':         return PK_R;
    case 'Q':         return PK_Q;
    case 'Z':         return PK_Z;
    case 'A':         return PK_A;
    case 'D':         return PK_D;
    case 'W':         return PK_W;
    case 'S':         return PK_S;
    case '1':         return PK_1;
    case '2':         return PK_2;
    case '3':         return PK_3;
    case VK_F5:       return PK_F5;
    case 'F':         return PK_F;
    case VK_F9:       return PK_F9;
    case 'M':         return PK_M;
    case 'G':         return PK_G;
    case 'H':         return PK_H;
    case 'X':         return PK_X;
    case VK_LEFT:     return PK_LEFT;
    case VK_RIGHT:    return PK_RIGHT;
    case VK_UP:       return PK_UP;
    case VK_DOWN:     return PK_DOWN;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:   return PK_SHIFT;
    default:          return PK_NONE;
    }
}

static void push_event(Platform *p, PlatEvent e) {
    int next = (p->qt + 1) % 256;
    if (next == p->qh) return;   /* full: drop the oldest by ignoring */
    p->q[p->qt] = e;
    p->qt = next;
}

static void push_key(Platform *p, PlatKey k, PlatEventType type) {
    if (k == PK_NONE) return;
    PlatEvent e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.key = k;
    push_event(p, e);
}

static void push_mouse(Platform *p, PlatEventType type, int button, int x, int y) {
    PlatEvent e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.button = button;
    e.x = x;
    e.y = y;
    push_event(p, e);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Platform *p = g_plat;
    switch (msg) {
    case WM_CLOSE:
        if (p) p->quit = 1;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (p && p->memdc)
            BitBlt(dc, 0, 0, p->w, p->h, p->memdc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (!(lp & (1 << 30))) push_key(p, map_vk(wp), PEV_KEYDOWN);
        return 0;
    case WM_KEYUP:
        push_key(p, map_vk(wp), PEV_KEYUP);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        push_mouse(p, PEV_MOUSEDOWN, 1, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_RBUTTONDOWN:
        SetCapture(hwnd);
        push_mouse(p, PEV_MOUSEDOWN, 3, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MBUTTONDOWN:
        SetCapture(hwnd);
        push_mouse(p, PEV_MOUSEDOWN, 2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        push_mouse(p, PEV_MOUSEUP, 1, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_RBUTTONUP:
        ReleaseCapture();
        push_mouse(p, PEV_MOUSEUP, 3, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MBUTTONUP:
        ReleaseCapture();
        push_mouse(p, PEV_MOUSEUP, 2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        push_mouse(p, PEV_MOUSEMOVE, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(hwnd, &pt);
        p->wheel_acc += GET_WHEEL_DELTA_WPARAM(wp);
        while (p->wheel_acc >= WHEEL_DELTA) {
            p->wheel_acc -= WHEEL_DELTA;
            PlatEvent e;
            memset(&e, 0, sizeof(e));
            e.type = PEV_WHEEL;
            e.wheel = 1;
            e.x = pt.x;
            e.y = pt.y;
            push_event(p, e);
        }
        while (p->wheel_acc <= -WHEEL_DELTA) {
            p->wheel_acc += WHEEL_DELTA;
            PlatEvent e;
            memset(&e, 0, sizeof(e));
            e.type = PEV_WHEEL;
            e.wheel = -1;
            e.x = pt.x;
            e.y = pt.y;
            push_event(p, e);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

Platform *plat_open(const char *title, int w, int h) {
    SetProcessDPIAware();   /* keep 1280x720 as physical pixels on scaled desktops */
    Platform *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->w = w;
    p->h = h;
    g_plat = p;

    HINSTANCE inst = GetModuleHandle(NULL);
    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "WodCWindow";
    if (!RegisterClassEx(&wc)) { free(p); g_plat = NULL; return NULL; }

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, w, h };
    AdjustWindowRect(&rc, style, FALSE);
    p->hwnd = CreateWindowEx(0, "WodCWindow", title, style,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             NULL, NULL, inst, NULL);
    if (!p->hwnd) { free(p); g_plat = NULL; return NULL; }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;   /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    p->dib = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!p->dib) { plat_close(p); return NULL; }
    p->pixels = (uint32_t *)bits;

    HDC screen = GetDC(NULL);
    p->memdc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    SelectObject(p->memdc, p->dib);
    p->windc = GetDC(p->hwnd);

    ShowWindow(p->hwnd, SW_SHOW);
    UpdateWindow(p->hwnd);
    SetForegroundWindow(p->hwnd);
    SetFocus(p->hwnd);

    timeBeginPeriod(1);
    QueryPerformanceFrequency(&p->freq);
    QueryPerformanceCounter(&p->start);
    return p;
}

uint32_t *plat_pixels(Platform *p) {
    return p->pixels;
}

void plat_present(Platform *p) {
    BitBlt(p->windc, 0, 0, p->w, p->h, p->memdc, 0, 0, SRCCOPY);
}

int plat_poll(Platform *p, PlatEvent *ev) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) p->quit = 1;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (p->qh != p->qt) {
        *ev = p->q[p->qh];
        p->qh = (p->qh + 1) % 256;
        return 1;
    }
    if (p->quit) {
        ev->type = PEV_QUIT;
        return 1;
    }
    return 0;
}

void plat_sleep(double seconds) {
    if (seconds <= 0.0) return;
    DWORD ms = (DWORD)(seconds * 1000.0);
    if (ms > 0) Sleep(ms);
}

void plat_sound(int kind) {
    switch (kind) {
    case PSND_SELECT:  MessageBeep(MB_ICONQUESTION); break;
    case PSND_CAPTURE: MessageBeep(MB_ICONASTERISK); break;
    case PSND_DEATH:   MessageBeep(MB_ICONHAND);     break;
    case PSND_WIN:     MessageBeep(MB_ICONEXCLAMATION); break;
    default:           MessageBeep(MB_OK);           break;
    }
}

double plat_time(void) {
    Platform *p = g_plat;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - p->start.QuadPart) / (double)p->freq.QuadPart;
}

void plat_close(Platform *p) {
    if (!p) return;
    if (p->windc) ReleaseDC(p->hwnd, p->windc);
    if (p->memdc) DeleteDC(p->memdc);
    if (p->dib) DeleteObject(p->dib);
    if (p->hwnd) DestroyWindow(p->hwnd);
    timeEndPeriod(1);
    if (g_plat == p) g_plat = NULL;
    free(p);
}

/* Entry point for the -mwindows build: forward to the shared main(). */
int main(int argc, char **argv);

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    (void)inst; (void)prev; (void)cmdline; (void)show;
    return main(__argc, __argv);
}
