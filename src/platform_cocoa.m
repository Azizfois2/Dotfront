#import <Cocoa/Cocoa.h>
#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

struct Platform {
    NSWindow          *win;
    NSView            *view;
    CGImageRef         image;
    CGColorSpaceRef    cs;
    CGDataProviderRef  provider;
    uint32_t          *pixels;
    int                w, h;
    int                quit;
    int                shift_down;
    double             wheel_acc;

    PlatEvent q[256];
    int qh, qt;
};

static void push_event(Platform *p, PlatEvent e) {
    int next = (p->qt + 1) % 256;
    if (next == p->qh) return;
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

static void push_mouse(Platform *p, PlatEventType type, int button, NSPoint pt) {
    PlatEvent e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.button = button;
    e.x = (int)pt.x;
    e.y = (int)pt.y;
    push_event(p, e);
}

static PlatKey key_from_event(NSEvent *e) {
    NSString *s = [e charactersIgnoringModifiers];
    if ([s length] > 0) {
        unichar c = [[s lowercaseString] characterAtIndex:0];
        switch (c) {
        case ' ':  return PK_SPACE;
        case 0x1b: return PK_ESC;
        case 'r':  return PK_R;
        case 'q':  return PK_Q;
        case 'z':  return PK_Z;
        case 'a':  return PK_A;
        case 'd':  return PK_D;
        case 'w':  return PK_W;
        case 's':  return PK_S;
        case '1':  return PK_1;
        case '2':  return PK_2;
        case '3':  return PK_3;
        default:   break;
        }
    }
    switch ([e keyCode]) {
    case 49:  return PK_SPACE;
    case 53:  return PK_ESC;
    case 123: return PK_LEFT;
    case 124: return PK_RIGHT;
    case 125: return PK_DOWN;
    case 126: return PK_UP;
    case 56:
    case 60:  return PK_SHIFT;
    default:  return PK_NONE;
    }
}

@interface WodView : NSView <NSWindowDelegate> {
    Platform *plat_ivar;
}
@property (nonatomic, assign) Platform *plat;
- (void)requestQuit:(id)sender;
@end

@implementation WodView
@synthesize plat = plat_ivar;

- (BOOL)isFlipped { return YES; }          /* top-left origin, matching the framebuffer */
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }

- (NSPoint)pointForEvent:(NSEvent *)e {
    return [self convertPoint:[e locationInWindow] fromView:nil];
}

- (void)keyDown:(NSEvent *)e {
    push_key(self.plat, key_from_event(e), PEV_KEYDOWN);
}

- (void)keyUp:(NSEvent *)e {
    push_key(self.plat, key_from_event(e), PEV_KEYUP);
}

- (void)flagsChanged:(NSEvent *)e {
    BOOL down = ([e modifierFlags] & NSEventModifierFlagShift) != 0;
    if ((int)down != self.plat->shift_down) {
        self.plat->shift_down = (int)down;
        push_key(self.plat, PK_SHIFT, down ? PEV_KEYDOWN : PEV_KEYUP);
    }
}

- (void)mouseDown:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEDOWN, 1, [self pointForEvent:e]);
}
- (void)rightMouseDown:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEDOWN, 3, [self pointForEvent:e]);
}
- (void)otherMouseDown:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEDOWN, 2, [self pointForEvent:e]);
}
- (void)mouseUp:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEUP, 1, [self pointForEvent:e]);
}
- (void)rightMouseUp:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEUP, 3, [self pointForEvent:e]);
}
- (void)otherMouseUp:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEUP, 2, [self pointForEvent:e]);
}
- (void)mouseMoved:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEMOVE, 0, [self pointForEvent:e]);
}
- (void)mouseDragged:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEMOVE, 0, [self pointForEvent:e]);
}
- (void)rightMouseDragged:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEMOVE, 0, [self pointForEvent:e]);
}
- (void)otherMouseDragged:(NSEvent *)e {
    push_mouse(self.plat, PEV_MOUSEMOVE, 0, [self pointForEvent:e]);
}

- (void)scrollWheel:(NSEvent *)e {
    double dy = [e scrollingDeltaY];
    if ([e hasPreciseScrollingDeltas]) dy *= 0.1;   /* trackpad: avoid hyper-fast zoom */
    self.plat->wheel_acc += dy;
    while (self.plat->wheel_acc >= 1.0) {
        self.plat->wheel_acc -= 1.0;
        PlatEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = PEV_WHEEL;
        ev.wheel = 1;
        NSPoint pt = [self pointForEvent:e];
        ev.x = (int)pt.x;
        ev.y = (int)pt.y;
        push_event(self.plat, ev);
    }
    while (self.plat->wheel_acc <= -1.0) {
        self.plat->wheel_acc += 1.0;
        PlatEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = PEV_WHEEL;
        ev.wheel = -1;
        NSPoint pt = [self pointForEvent:e];
        ev.x = (int)pt.x;
        ev.y = (int)pt.y;
        push_event(self.plat, ev);
    }
}

- (void)drawRect:(NSRect)rect {
    (void)rect;
    Platform *p = self.plat;
    if (!p || !p->image) return;
    /* The view is flipped, so the context is y-down; CGContextDrawImage
       then maps image row 0 to the top of the view. */
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(ctx);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    /* Our pixel buffer is top-down, while CGImage rows start at the bottom. */
    CGContextTranslateCTM(ctx, 0, p->h);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextDrawImage(ctx, CGRectMake(0, 0, p->w, p->h), p->image);
    CGContextRestoreGState(ctx);
}

- (void)windowWillClose:(NSNotification *)note {
    (void)note;
    if (self.plat) self.plat->quit = 1;
}

- (void)requestQuit:(id)sender {
    (void)sender;
    if (self.plat) self.plat->quit = 1;
}

@end

Platform *plat_open(const char *title, int w, int h) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        Platform *p = calloc(1, sizeof(*p));
        if (!p) return NULL;
        p->w = w;
        p->h = h;

        p->pixels = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
        if (!p->pixels) { free(p); return NULL; }
        memset(p->pixels, 0, (size_t)w * (size_t)h * sizeof(uint32_t));

        p->cs = CGColorSpaceCreateDeviceRGB();
        p->provider = CGDataProviderCreateWithData(NULL, p->pixels,
                                                   (size_t)w * (size_t)h * 4, NULL);
        p->image = CGImageCreate((size_t)w, (size_t)h, 8, 32, (size_t)w * 4,
                                 p->cs,
                                 kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little,
                                 p->provider, NULL, false, kCGRenderingIntentDefault);
        if (!p->image) { plat_close(p); return NULL; }

        NSRect rect = NSMakeRect(0, 0, w, h);
        WodView *view = [[WodView alloc] initWithFrame:rect];
        view.plat = p;

        NSWindow *win = [[NSWindow alloc]
            initWithContentRect:rect
                      styleMask:(NSWindowStyleMaskTitled |
                                 NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [win setTitle:[NSString stringWithUTF8String:title]];
        [win setContentView:view];
        [win setDelegate:view];
        [win setReleasedWhenClosed:NO];
        [win setAcceptsMouseMovedEvents:YES];
        [win center];
        [win makeKeyAndOrderFront:nil];
        [win makeFirstResponder:view];

        NSMenu *menubar = [[NSMenu alloc] init];
        NSMenuItem *appItem = [[NSMenuItem alloc] init];
        [menubar addItem:appItem];
        [NSApp setMainMenu:menubar];
        NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"WAR OF DOTS"];
        NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit WAR OF DOTS"
                                                          action:@selector(requestQuit:)
                                                   keyEquivalent:@"q"];
        [quitItem setTarget:view];
        [appMenu addItem:quitItem];
        [appItem setSubmenu:appMenu];

        [NSApp activateIgnoringOtherApps:YES];

        p->win = win;
        p->view = view;
        return p;
    }
}

uint32_t *plat_pixels(Platform *p) {
    return p->pixels;
}

void plat_present(Platform *p) {
    [p->view setNeedsDisplay:YES];
    [p->view displayIfNeeded];
}

int plat_poll(Platform *p, PlatEvent *ev) {
    @autoreleasepool {
        for (;;) {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
            if (!e) break;
            [NSApp sendEvent:e];
        }
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

double plat_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void plat_sleep(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

void plat_close(Platform *p) {
    if (!p) return;
    if (p->win) [p->win close];
    if (p->image) CGImageRelease(p->image);
    if (p->provider) CGDataProviderRelease(p->provider);
    if (p->cs) CGColorSpaceRelease(p->cs);
    free(p->pixels);
    free(p);
}
