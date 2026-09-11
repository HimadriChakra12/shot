#define _POSIX_C_SOURCE 200809L

#include "select.h"
#include "xutil.h"
#include "capture.h"
#include "scripts.h"
#include "../config.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <time.h>

// ── Results ───────────────────────────────────────────────────────────────────
static Rect   rect;
static Action chosen_action     = ACTION_NONE;
static int    chosen_script_idx = -1;
static Script loaded_scripts[MAX_SCRIPTS];
static int    nscripts          = 0;

int     select_x(void)          { return rect.x; }
int     select_y(void)          { return rect.y; }
int     select_w(void)          { return rect.w; }
int     select_h(void)          { return rect.h; }
Action  select_action(void)     { return chosen_action; }
int     select_script_idx(void) { return chosen_script_idx; }
Script *select_scripts(void)    { return loaded_scripts; }
int     select_nscripts(void)   { return nscripts; }

// ── Zone hit-testing ──────────────────────────────────────────────────────────
typedef enum {
    ZONE_NONE = 0, ZONE_BODY,
    ZONE_TL, ZONE_T, ZONE_TR,
    ZONE_L,          ZONE_R,
    ZONE_BL, ZONE_B, ZONE_BR,
} Zone;

static Zone hit_zone(int mx, int my) {
    int x2 = rect.x + rect.w, y2 = rect.y + rect.h, hs = OPTHANDLESIZE;
    if (mx < rect.x-hs || mx > x2+hs || my < rect.y-hs || my > y2+hs)
        return ZONE_NONE;
    int L = mx <= rect.x+hs, R = mx >= x2-hs;
    int T = my <= rect.y+hs, B = my >= y2-hs;
    if (T && L) return ZONE_TL;
    if (T && R) return ZONE_TR;
    if (B && L) return ZONE_BL;
    if (B && R) return ZONE_BR;
    if (T) return ZONE_T;
    if (B) return ZONE_B;
    if (L) return ZONE_L;
    if (R) return ZONE_R;
    return ZONE_BODY;
}

static unsigned int zone_cursor(Zone z) {
    switch (z) {
        case ZONE_TL: case ZONE_BR:            return XC_sizing;
        case ZONE_TR: case ZONE_BL:            return XC_sizing;
        case ZONE_T:  case ZONE_B:             return XC_sb_v_double_arrow;
        case ZONE_L:  case ZONE_R:             return XC_sb_h_double_arrow;
        case ZONE_BODY:                        return XC_fleur;
        default:                               return XC_crosshair;
    }
}

// ── Rect manipulation ─────────────────────────────────────────────────────────
static void rect_clamp(void) {
    if (rect.w < 2) rect.w = 2;
    if (rect.h < 2) rect.h = 2;
    if (rect.x < 0)           { rect.w += rect.x; rect.x = 0; }
    if (rect.y < 0)           { rect.h += rect.y; rect.y = 0; }
    if (rect.x + rect.w > W)    rect.w = W - rect.x;
    if (rect.y + rect.h > H)    rect.h = H - rect.y;
    if (rect.w < 2) rect.w = 2;
    if (rect.h < 2) rect.h = 2;
}

static void apply_drag(Zone z, int dx, int dy) {
    if (z == ZONE_BODY) {
        // Move: preserve size, just clamp position
        rect.x += dx;
        rect.y += dy;
        if (rect.x < 0)              rect.x = 0;
        if (rect.y < 0)              rect.y = 0;
        if (rect.x + rect.w > W)     rect.x = W - rect.w;
        if (rect.y + rect.h > H)     rect.y = H - rect.h;
        return;
    }
    // Resize: adjust edges and clamp
    switch (z) {
        case ZONE_TL: rect.x+=dx; rect.w-=dx; rect.y+=dy; rect.h-=dy; break;
        case ZONE_TR: rect.w+=dx;              rect.y+=dy; rect.h-=dy; break;
        case ZONE_BL: rect.x+=dx; rect.w-=dx; rect.h+=dy;             break;
        case ZONE_BR: rect.w+=dx;              rect.h+=dy;             break;
        case ZONE_T:                           rect.y+=dy; rect.h-=dy; break;
        case ZONE_B:                           rect.h+=dy;             break;
        case ZONE_L:  rect.x+=dx; rect.w-=dx;                         break;
        case ZONE_R:  rect.w+=dx;                                      break;
        default: break;
    }
    rect_clamp();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

// Original screenshot pixels — kept clean so we can darken from scratch each frame.
static unsigned char *img_orig      = NULL;
static int            img_orig_size = 0;

static void drawing_init(void) {
    img_orig_size = img->height * img->bytes_per_line;
    free(img_orig);
    img_orig = malloc(img_orig_size);
    if (img_orig) memcpy(img_orig, img->data, img_orig_size);
}

// Darken one rectangular strip in img->data from img_orig.
static void dim_strip(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || !img_orig) return;
    unsigned int scale = 255 - OPTDIMALPHA;
    for (int row = y; row < y + h; row++) {
        unsigned char       *dst = (unsigned char *)img->data  + row * img->bytes_per_line + x * 4;
        const unsigned char *src = img_orig                    + row * img->bytes_per_line + x * 4;
        for (int col = 0; col < w; col++, dst += 4, src += 4) {
            dst[0] = (unsigned char)(src[0] * scale >> 8);
            dst[1] = (unsigned char)(src[1] * scale >> 8);
            dst[2] = (unsigned char)(src[2] * scale >> 8);
            dst[3] = src[3];
        }
    }
}

static void draw_handle(int cx, int cy) {
    int hs = OPTHANDLESIZE;
    XFillRectangle(disp, backbuffer, gc, cx-hs/2, cy-hs/2, hs, hs);
}

static void redraw(void) {
    // Restore clean pixels into img->data, then darken only the four outer strips.
    if (img_orig) memcpy(img->data, img_orig, img_orig_size);

    dim_strip(0,            0,            W,               rect.y);
    dim_strip(0,            rect.y+rect.h,W,               H-rect.y-rect.h);
    dim_strip(0,            rect.y,       rect.x,          rect.h);
    dim_strip(rect.x+rect.w,rect.y,       W-rect.x-rect.w, rect.h);

    XPutImage(disp, backbuffer, gc, img, 0, 0, 0, 0, W, H);

    XSetForeground(disp, gc, (OPTR<<16)|(OPTG<<8)|OPTB);
    XDrawRectangle(disp, backbuffer, gc, rect.x, rect.y, rect.w, rect.h);

    int mx = rect.x+rect.w/2, my = rect.y+rect.h/2;
    draw_handle(rect.x,          rect.y);
    draw_handle(mx,              rect.y);
    draw_handle(rect.x+rect.w,  rect.y);
    draw_handle(rect.x,          my);
    draw_handle(rect.x+rect.w,  my);
    draw_handle(rect.x,          rect.y+rect.h);
    draw_handle(mx,              rect.y+rect.h);
    draw_handle(rect.x+rect.w,  rect.y+rect.h);

    char label[32];
    snprintf(label, sizeof(label), "%d x %d", rect.w, rect.h);
    int ly = rect.y > 18 ? rect.y-6 : rect.y+rect.h+14;
    XDrawString(disp, backbuffer, gc, rect.x, ly, label, strlen(label));

    XCopyArea(disp, backbuffer, win, gc, 0, 0, W, H, 0, 0);
    XFlush(disp);
}

// ── Grab helpers ──────────────────────────────────────────────────────────────
// Grab keyboard on ROOT with async mode — override_redirect windows cannot
// receive XSetInputFocus so we can't grab on win. Grabbing on root with
// owner_events=False means ALL key events go only to us, nothing leaks.
static int grab_kbd(void) {
    // When launched via a WM keybind, the WM may still hold a keyboard grab
    // for a few milliseconds after spawning us. Retry until it releases.
    struct timespec ts = { 0, 5000000 }; // 5ms
    for (int i = 0; i < 40; i++) {       // up to 200ms total
        int r = XGrabKeyboard(disp, root, False,
                              GrabModeAsync, GrabModeAsync,
                              CurrentTime);
        if (r == GrabSuccess) {
            debug("XGrabKeyboard succeeded on attempt %d", i + 1);
            return 1;
        }
        nanosleep(&ts, NULL);
    }
    debug("XGrabKeyboard failed after retries");
    return 0;
}

static void ungrab_all(void) {
    XUngrabPointer(disp, CurrentTime);
    XUngrabKeyboard(disp, CurrentTime);
    XSync(disp, False);
}

static void coalesce_motion(XEvent *e) {
    XEvent next;
    while (XPending(disp)) {
        XPeekEvent(disp, &next);
        if (next.type != MotionNotify) break;
        XNextEvent(disp, e);
    }
}

// ── Post-selection key dispatch ───────────────────────────────────────────────
// Returns: 1 = action chosen, 0 = ignore, -1 = cancel
static int dispatch_key(KeySym ks) {
    if (ks == XK_Escape)       return -1;
    if (ks == OPTKEY_SAVE)     { chosen_action = ACTION_SAVE;     debug("ACTION_SAVE");     return 1; }
    if (ks == OPTKEY_COPY)     { chosen_action = ACTION_COPY;     debug("ACTION_COPY");     return 1; }
    if (ks == OPTKEY_ANNOTATE) { chosen_action = ACTION_ANNOTATE; debug("ACTION_ANNOTATE"); return 1; }

    if (ks >= XK_1 && ks <= XK_9) {
        int idx = (int)(ks - XK_1);
        if (idx < nscripts) {
            chosen_action     = ACTION_SCRIPT;
            chosen_script_idx = idx;
            debug("ACTION_SCRIPT %d (%s)", idx, loaded_scripts[idx].name);
            return 1;
        }
        return 0;
    }

#ifdef OPTSCRIPTBINDS
    {
        static const struct { KeySym sym; const char *name; } kb[] = OPTSCRIPTBINDS;
        for (size_t i = 0; i < sizeof(kb)/sizeof(kb[0]); i++) {
            if (ks != kb[i].sym) continue;
            for (int j = 0; j < nscripts; j++) {
                if (strcmp(loaded_scripts[j].name, kb[i].name) != 0) continue;
                chosen_action     = ACTION_SCRIPT;
                chosen_script_idx = j;
                debug("ACTION_SCRIPT name=%s idx=%d", kb[i].name, j);
                return 1;
            }
        }
    }
#endif
    return 0;
}

// ── Drag loop (shared between initial selection and re-selection) ─────────────
// Runs from first click through button release. Sets rect. Returns 0 on cancel.
static int do_drag(void) {
    XEvent e;
    int anchor_x, anchor_y;

    // Wait for left click
    while (1) {
        XNextEvent(disp, &e);
        if (e.type == ButtonPress) {
            if (e.xbutton.button == Button3) return 0;
            if (e.xbutton.button == Button1) {
                anchor_x = rect.x = e.xbutton.x;
                anchor_y = rect.y = e.xbutton.y;
                rect.w = rect.h = 0;
                break;
            }
        }
        if (e.type == KeyPress) {
            KeySym ks = XLookupKeysym(&e.xkey, 0);
            if (ks == XK_Escape) return 0;
        }
    }

    // Drag until release
    while (1) {
        XNextEvent(disp, &e);
        if (e.type == MotionNotify) {
            coalesce_motion(&e);
            int cx = e.xmotion.x, cy = e.xmotion.y;
            rect.x = anchor_x < cx ? anchor_x : cx;
            rect.y = anchor_y < cy ? anchor_y : cy;
            rect.w = abs(cx - anchor_x);
            rect.h = abs(cy - anchor_y);
            redraw();
        } else if (e.type == ButtonRelease && e.xbutton.button == Button1) {
            if (rect.w < 2 || rect.h < 2) return 0;
            return 1;
        } else if (e.type == ButtonPress && e.xbutton.button == Button3) {
            return 0;
        } else if (e.type == KeyPress) {
            KeySym ks = XLookupKeysym(&e.xkey, 0);
            if (ks == XK_Escape) return 0;
        }
    }
}

// ── Main entry point ──────────────────────────────────────────────────────────
int run_selection(void) {
    nscripts          = scripts_load(loaded_scripts);
    chosen_action     = ACTION_NONE;
    chosen_script_idx = -1;

    // Capture screen and show overlay before grabbing anything.
    // The overlay must be visible before we grab so that it's the topmost
    // window receiving events.
    if (!screenshot()) return SELECT_ERROR;
    drawing_init();
    XMapRaised(disp, win);
    XSync(disp, False);
    XPutImage(disp, win, gc, img, 0, 0, 0, 0, W, H);
    XFlush(disp);

    // Wait for the keybind key to be physically released before grabbing.
    // Without this, the grab fails or the trigger key leaks into phase 1
    // when launched via a WM keybind (XFCE, i3, etc.).
    struct timespec ts = { 0, OPTGRABDELAY * 1000000L };
    nanosleep(&ts, NULL);

    // Grab pointer on root (async — events go to us, nothing leaks to Firefox).
    // Grab keyboard on root with owner_events=False — override_redirect windows
    // cannot be focused so we must grab on root.
    if (XGrabPointer(disp, root, False,
                     ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                     GrabModeAsync, GrabModeAsync,
                     None, XCreateFontCursor(disp, XC_crosshair),
                     CurrentTime) != GrabSuccess) {
        debug("pointer grab failed");
        return SELECT_CANCEL;
    }
    if (!grab_kbd()) {
        debug("keyboard grab failed");
        XUngrabPointer(disp, CurrentTime);
        return SELECT_CANCEL;
    }

    // ── Phase 1: mode ─────────────────────────────────────────────────────────
    Mode mode = MODE_REGION;
    {
        XEvent e;
        while (1) {
            XNextEvent(disp, &e);
            if (e.type == KeyPress) {
                KeySym ks = XLookupKeysym(&e.xkey, 0);
                debug("phase1 key: %lu", ks);
                if (ks == XK_Escape)          { ungrab_all(); return SELECT_CANCEL; }
                if (ks == OPTKEY_FULLSCREEN)  { mode = MODE_FULLSCREEN; break; }
                if (ks == OPTKEY_WINDOW)      { mode = MODE_WINDOW;     break; }
                break; // any other key = region
            }
            if (e.type == ButtonPress) {
                if (e.xbutton.button == Button3) { ungrab_all(); return SELECT_CANCEL; }
                XPutBackEvent(disp, &e);
                break; // click = region
            }
        }
    }
    debug("mode = %d (0=region 1=fullscreen)", mode);

    // ── Phase 2: capture rect ─────────────────────────────────────────────────
    if (mode == MODE_FULLSCREEN) {
        rect.x = 0; rect.y = 0; rect.w = W; rect.h = H;
        redraw();
    } else if (mode == MODE_WINDOW) {
        /* Release our grabs — capture_window_rect does its own pointer grab.
         * Unmap overlay so the user can see real window decorations to click. */
        ungrab_all();
        XUnmapWindow(disp, win);
        XSync(disp, False);

        /* User clicks a window; we get back its root-relative rect only. */
        Rect wr = {0, 0, 0, 0};
        if (!capture_window_rect(&wr)) return SELECT_CANCEL;
        rect = wr;

        /* Take a fresh full-screen shot (overlay is gone → real desktop pixels).
         * img is W×H — exactly what redraw() / XSubImage expect. */
        if (img) { XDestroyImage(img); img = NULL; }
        if (!screenshot()) return SELECT_CANCEL;
        drawing_init();

        /* Restore overlay and re-grab for phase 3. */
        XMapRaised(disp, win);
        XSync(disp, False);
        if (XGrabPointer(disp, root, False,
                         ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                         GrabModeAsync, GrabModeAsync,
                         None, XCreateFontCursor(disp, XC_crosshair),
                         CurrentTime) != GrabSuccess) {
            return SELECT_CANCEL;
        }
        if (!grab_kbd()) {
            XUngrabPointer(disp, CurrentTime);
            return SELECT_CANCEL;
        }
        redraw();
    } else {
        if (!do_drag()) { ungrab_all(); return SELECT_CANCEL; }
        redraw();
    }
    debug("rect: %d,%d %dx%d", rect.x, rect.y, rect.w, rect.h);

    // ── Phase 3: post-selection ───────────────────────────────────────────────
    // Rect committed. Wait for action key. Also allow drag/resize.
    xutil_set_cursor(XC_fleur);

    Zone dragging = ZONE_NONE;
    int px = 0, py = 0;
    XEvent e;

    while (1) {
        XNextEvent(disp, &e);

        if (e.type == MotionNotify) {
            coalesce_motion(&e);
            int cx = e.xmotion.x, cy = e.xmotion.y;
            if (dragging != ZONE_NONE) {
                apply_drag(dragging, cx-px, cy-py);
                redraw();
            } else {
                xutil_set_cursor(zone_cursor(hit_zone(cx, cy)));
            }
            px = cx; py = cy;

        } else if (e.type == ButtonPress && e.xbutton.button == Button1) {
            Zone z = hit_zone(e.xbutton.x, e.xbutton.y);
            if (z == ZONE_NONE) {
                // Clicked outside — re-select
                XPutBackEvent(disp, &e);
                if (!do_drag()) { ungrab_all(); return SELECT_CANCEL; }
                redraw();
            } else {
                dragging = z;
                px = e.xbutton.x;
                py = e.xbutton.y;
            }

        } else if (e.type == ButtonRelease && e.xbutton.button == Button1) {
            dragging = ZONE_NONE;

        } else if (e.type == ButtonPress && e.xbutton.button == Button3) {
            ungrab_all(); return SELECT_CANCEL;

        } else if (e.type == KeyPress) {
            KeySym ks = XLookupKeysym(&e.xkey, 0);
            debug("phase3 key: %lu", ks);
            int r = dispatch_key(ks);
            if (r == -1) { ungrab_all(); return SELECT_CANCEL; }
            if (r ==  1) break;
        }
    }

    ungrab_all();
    free(img_orig);
    img_orig = NULL;
    return SELECT_OK;
}
