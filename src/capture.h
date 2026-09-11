#pragma once

#include "state.h"

#include <X11/Xlib.h>

extern XImage           *img;
extern unsigned long int r, g, b;

int screenshot(void);
int capture_window_rect(Rect *out_rect);
int capture_window(Rect *out_rect);
