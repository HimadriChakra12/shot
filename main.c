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

// ── Usage ─────────────────────────────────────────────────────────────────────

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-h] [-f] [-w]\n"
        "\n"
        "  (no args)  Interactive region selection (drag to select)\n"
        "  -f         Fullscreen — capture the entire screen immediately\n"
        "  -w         Window — click a window to capture it\n"
        "  -h         Show this help message and exit\n"
        "\n"
        "Interactive pre-selection keybinds (before clicking):\n"
        "  f          Switch to fullscreen mode\n"
        "  w          Switch to window-pick mode\n"
        "  Escape     Cancel\n"
        "\n"
        "Post-selection keybinds (after region/window is chosen):\n"
        "  s          Save to disk  (%s)\n"
        "  y          Copy to clipboard\n"
        "  a          Open annotation tool\n"
        "  1-9        Run script from " OPTSCRIPTDIR "\n"
        "  Escape     Cancel\n",
        prog, OPTDIR);
}

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
    /* -1 = interactive, 0 = fullscreen, 1 = window */
    int headless_mode = -1;

    int opt;
    while ((opt = getopt(argc, argv, "hfw")) != -1) {
        switch (opt) {
        case 'h':
            usage(argv[0]);
            return 0;
        case 'f':
            headless_mode = 0;
            break;
        case 'w':
            headless_mode = 1;
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (!xutil_init())
        die("Failed to open display / get root window size");

    // ── Headless fullscreen mode: -f ──────────────────────────────────────────
    if (headless_mode == 0) {
        if (!screenshot()) die("Failed to capture screen");
        XSync(disp, False);

        char path[4096];
        if (save_image_path(path, sizeof(path)) != 0)
            die("Failed to save screenshot");

        action_copy(path);
        goto end; // only reached if xclip is missing
    }

    // ── Headless window-pick mode: -w ─────────────────────────────────────────
    if (headless_mode == 1) {
        Rect wr = {0, 0, 0, 0};
        if (!capture_window(&wr)) die("Window capture cancelled or failed");
        XSync(disp, False);

        char path[4096];
        if (save_image_path(path, sizeof(path)) != 0)
            die("Failed to save screenshot");

        action_copy(path);
        goto end;
    }

    // ── Interactive mode (no args) ────────────────────────────────────────────
    if (!xutil_create_window())     die("Failed to create overlay window");
    if (!xutil_create_gc())         die("Failed to create GC");
    if (!xutil_create_backbuffer()) die("Failed to create backbuffer");

    {
        int result = run_selection();
        if (result == SELECT_ERROR)  die("Failed to capture screen");
        if (result == SELECT_CANCEL) goto end;
    }

    // img is always a full W×H screenshot at this point (region, fullscreen,
    // and window modes all end with a full-screen img).  Crop to the rect.
    {
        int x = select_x(), y = select_y();
        int w = select_w(), h = select_h();
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > W) w = W - x;
        if (y + h > H) h = H - y;
        if (w < 1 || h < 1) goto end;
        img = XSubImage(img, x, y, w, h);
        if (!img) die("XSubImage failed");
    }

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
