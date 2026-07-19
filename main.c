#include "config.h"
#include "src/xutil.h"
#include "src/capture.h"
#include "src/select.h"
#include "src/save.h"
#include "src/scripts.h"
#include "src/state.h"

#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ── Built-in actions ──────────────────────────────────────────────────────────

static void action_save(const char *path) {
    printf("%s\n", path);
}

static void action_copy(const char *path) {
    /* Build MIME type string from config: "image/png", "image/jpeg", "image/webp" */
    static char mime[32];
    const char *fmt = OPTFORMAT_TYPE;
    if (strcmp(fmt, "jpg") == 0) fmt = "jpeg"; /* normalise alias */
    snprintf(mime, sizeof(mime), "image/%s", fmt);

    char *args[] = {
        "xclip", "-selection", "clipboard", "-t", mime,
        (char *)path, NULL
    };
    execvp(args[0], args);
    perror("xclip"); // only reached if exec fails
}

static void action_annotate(const char *path) {
#ifdef OPTANNOTATE
    #ifdef OPTANNOTATE_FLAGS
        char *args[] = { OPTANNOTATE, OPTANNOTATE_FLAGS, (char *)path, NULL };
    #else
        char *args[] = { OPTANNOTATE, (char *)path, NULL };
    #endif
    execvp(args[0], args);
    perror(OPTANNOTATE);
#else
    (void)path;
    fprintf(stderr, "Annotation disabled (OPTANNOTATE not set)\n");
#endif
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argv;

    if (!xutil_init())
        die("Failed to open display / get root window size");

    // ── Headless full-screen mode (any CLI argument) ──────────────────────────
    if (argc > 1) {
        if (!screenshot()) die("Failed to capture screen");
        XSync(disp, False);

        char path[4096];
        if (save_image_path(path, sizeof(path)) != 0)
            die("Failed to save screenshot");

        action_copy(path);
        goto end; // only reached if xclip is missing
    }

    // ── Interactive mode ──────────────────────────────────────────────────────
    if (!xutil_create_window())     die("Failed to create overlay window");
    if (!xutil_create_gc())         die("Failed to create GC");
    if (!xutil_create_backbuffer()) die("Failed to create backbuffer");

    {
        int result = run_selection();
        if (result == SELECT_ERROR)  die("Failed to capture screen");
        if (result == SELECT_CANCEL) goto end;
    }

    // Clamp selection to screen bounds before cropping
    {
        int x = select_x(), y = select_y();
        int w = select_w(), h = select_h();
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > W) w = W - x;
        if (y + h > H) h = H - y;
        if (w < 1 || h < 1) goto end;
        img = XSubImage(img, x, y, w, h);
    }
    if (!img) die("XSubImage failed");

    XSync(disp, False);

    // Unmap overlay before any external tool draws
    if (win) { XUnmapWindow(disp, win); XSync(disp, False); }

    // Save to disk — all actions receive the file path
    {
        char path[4096];
        if (save_image_path(path, sizeof(path)) != 0)
            die("Failed to save screenshot");

        Action    act = select_action();
        Rect      r   = { select_x(), select_y(), select_w(), select_h() };

        switch (act) {
        case ACTION_SAVE:
            action_save(path);
            break;

        case ACTION_COPY:
            action_copy(path);    // execs — doesn't return on success
            break;

        case ACTION_ANNOTATE:
            action_annotate(path); // execs — doesn't return on success
            break;

        case ACTION_SCRIPT: {
            int idx = select_script_idx();
            if (idx >= 0 && idx < select_nscripts()) {
                scripts_run(&select_scripts()[idx], path, &r);
            }
            break;
        }

        case ACTION_NONE:
        default:
            // No action chosen (shouldn't happen) — just print the path
            action_save(path);
            break;
        }
    }

    xutil_cleanup();
    if (img) XDestroyImage(img);
    return 0;

end:
    xutil_cleanup();
    if (img) XDestroyImage(img);
    return 1;
}
