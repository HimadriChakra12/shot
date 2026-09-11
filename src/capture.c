#include "capture.h"
#include "xutil.h"

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <stdlib.h>  

XImage           *img = NULL;
unsigned long int r, g, b;

int screenshot(void) {
    img = XGetImage(
        disp, root,
        0, 0,
        W, H,
        AllPlanes,
        ZPixmap
    );
    if (!img) return 0;

    r = img->red_mask;
    g = img->green_mask;
    b = img->blue_mask;
    return 1;
}

int capture_window_rect(Rect *out_rect) {
    Cursor cursor = XCreateFontCursor(disp, XC_left_ptr);

    if (XGrabPointer(disp, root, False,
                     ButtonPressMask,
                     GrabModeAsync, GrabModeAsync,
                     root, cursor, CurrentTime) != GrabSuccess) {
        XFreeCursor(disp, cursor);
        return 0;
    }

    XEvent ev;
    Window clicked = None;
    int cancelled = 0;
    while (1) {
        XNextEvent(disp, &ev);
        if (ev.type == ButtonPress) {
            if (ev.xbutton.button == Button3) {
                cancelled = 1;
            } else {
                clicked = ev.xbutton.subwindow;
                if (clicked == None) clicked = root; 
            }
            break;
        }
        if (ev.type == KeyPress) { cancelled = 1; break; }
    }

    XUngrabPointer(disp, CurrentTime);
    XFreeCursor(disp, cursor);
    XFlush(disp);

    if (cancelled) return 0;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(disp, clicked, &wa)) return 0;

    int wx, wy;
    Window dummy;
    XTranslateCoordinates(disp, clicked, root, 0, 0, &wx, &wy, &dummy);

    wx -= wa.border_width;
    wy -= wa.border_width;
    int ww = wa.width  + 2 * wa.border_width;
    int wh = wa.height + 2 * wa.border_width;

    if (wx < 0) { ww += wx; wx = 0; }
    if (wy < 0) { wh += wy; wy = 0; }
    if (wx + ww > W) ww = W - wx;
    if (wy + wh > H) wh = H - wy;
    if (ww < 1 || wh < 1) return 0;

    if (out_rect) {
        out_rect->x = wx;
        out_rect->y = wy;
        out_rect->w = ww;
        out_rect->h = wh;
    }
    return 1;
}

int capture_window(Rect *out_rect) {
    Rect wr = {0, 0, 0, 0};
    if (!capture_window_rect(&wr)) return 0;

    img = XGetImage(disp, root, wr.x, wr.y, wr.w, wr.h, AllPlanes, ZPixmap);
    if (!img) return 0;

    r = img->red_mask;
    g = img->green_mask;
    b = img->blue_mask;

    if (out_rect) *out_rect = wr;
    return 1;
}
