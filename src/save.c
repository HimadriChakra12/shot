#include "save.h"
#include "capture.h"
#include "../config.h"

#include <png.h>
#include <jpeglib.h>
#include <webp/encode.h>

#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>

// ── Internal helpers ──────────────────────────────────────────────────────────

/* Pack BGRA/BGRX scanlines → tightly-packed RGB rows (3 bytes/px). */
static unsigned char *make_rgb_buf(int *out_stride) {
    int w = img->width, h = img->height;
    int stride = w * 3;
    unsigned char *buf = malloc((size_t)h * stride);
    if (!buf) return NULL;

    for (int y = 0; y < h; y++) {
        unsigned char *src = (unsigned char *)img->data + y * img->bytes_per_line;
        unsigned char *dst = buf + y * stride;
        for (int x = 0; x < w; x++) {
            /* X11 stores pixels as BGRX (or BGRA) in 32-bit ints. */
            if (r < g && g < b) {
                /* RGB order */
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            } else {
                /* BGR order → swap R and B */
                dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
            }
            src += 4;
            dst += 3;
        }
    }
    *out_stride = stride;
    return buf;
}

static int encode_png(const char *fn) {
    FILE *fp = fopen(fn, "wb");
    if (!fp) { printf("\033[1;31mError:\033[0m Can't open %s\n", fn); return 1; }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return 1; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return 1; }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return 1;
    }

    int stride;
    unsigned char *buf = make_rgb_buf(&stride);
    if (!buf) { png_destroy_write_struct(&png, &info); fclose(fp); return 1; }

    png_init_io(png, fp);
    png_set_IHDR(png, info, img->width, img->height, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    for (int y = 0; y < img->height; y++)
        png_write_row(png, buf + y * stride);

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    free(buf);
    fclose(fp);
    debug("PNG written to %s", fn);
    return 0;
}

static int encode_jpeg(const char *fn) {
    FILE *fp = fopen(fn, "wb");
    if (!fp) { printf("\033[1;31mError:\033[0m Can't open %s\n", fn); return 1; }

    int stride;
    unsigned char *buf = make_rgb_buf(&stride);
    if (!buf) { fclose(fp); return 1; }

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr       jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    cinfo.image_width      = img->width;
    cinfo.image_height     = img->height;
    cinfo.input_components = 3;
    cinfo.in_color_space   = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, OPTQUALITY, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = buf + cinfo.next_scanline * stride;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    free(buf);
    fclose(fp);
    debug("JPEG written to %s", fn);
    return 0;
}

static int encode_webp(const char *fn) {
    FILE *fp = fopen(fn, "wb");
    if (!fp) { printf("\033[1;31mError:\033[0m Can't open %s\n", fn); return 1; }

    int stride;
    unsigned char *buf = make_rgb_buf(&stride);
    if (!buf) { fclose(fp); return 1; }

    unsigned char *fd = NULL;
    size_t fs = WebPEncodeRGB(buf, img->width, img->height, stride,
                              (float)OPTQUALITY, &fd);
    free(buf);

    int ret = 1;
    if (fs > 0) {
        size_t written = fwrite(fd, 1, fs, fp);
        if (written == fs) ret = 0;
        else printf("\033[1;31mError:\033[0m Wrote %zu/%zu bytes\n", written, fs);
    } else {
        printf("\033[1;31mError:\033[0m WebP encode failed\n");
    }
    if (fd) free(fd);
    fclose(fp);
    debug("WebP written to %s", fn);
    return ret;
}

static int build_path(char fn[PATH_MAX]) {
    fn[0] = '\0';
#ifdef OPTDIR
    if (OPTDIR[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            const struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
        if (!home) return 0;
        strncat(fn, home,       PATH_MAX - strlen(fn) - 1);
        strncat(fn, &OPTDIR[1], PATH_MAX - strlen(fn) - 1);
    } else {
        strncat(fn, OPTDIR, PATH_MAX - strlen(fn) - 1);
    }
#else
    strncat(fn, "/tmp/", PATH_MAX - strlen(fn) - 1);
#endif
    mkdir(fn, 0755);
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    snprintf(fn + strlen(fn), PATH_MAX - strlen(fn), OPTFORMAT, OPTFORMATARGS);
    return 1;
}

static int encode_and_write(const char *fn) {
    const char *fmt = OPTFORMAT_TYPE;
    if (strcmp(fmt, "jpeg") == 0 || strcmp(fmt, "jpg") == 0)
        return encode_jpeg(fn);
    if (strcmp(fmt, "webp") == 0)
        return encode_webp(fn);
    /* default: png */
    return encode_png(fn);
}

// ── Public API ────────────────────────────────────────────────────────────────

int save_image_path(char *out_path, size_t out_size) {
    char fn[PATH_MAX];
    if (!build_path(fn)) {
        printf("\033[1;31mError:\033[0m Couldn't resolve output path\n");
        return 1;
    }
    if (encode_and_write(fn) != 0) return 1;

    strncpy(out_path, fn, out_size - 1);
    out_path[out_size - 1] = '\0';
    return 0;
}

int save_image(void) {
    char fn[PATH_MAX];
    if (save_image_path(fn, sizeof(fn)) != 0) return 1;

    printf("%s\n", fn);
    char *args[] = { "xclip", "-selection", "clipboard", "-t", "image/png", fn, NULL };
    execvp(args[0], args);
    return 1;
}
