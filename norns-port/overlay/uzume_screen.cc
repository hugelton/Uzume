/*
 * uzume_screen.cc  --  screen backends for running norns' matron without the SSD1322 OLED.
 *
 *   screen:ascii  dumps the 128x64 screen as text (headless tests)
 *   screen:fb     writes it, scaled 2.5x (nearest), to a 16 bpp linux framebuffer (Cardputer Zero: /dev/fb0, 320x170)
 *
 * Both are `screen_ops_t` backends registered next to `screen_sdl_ops` (see hardware/io.cc), so they are only
 * available in a build configured with `--desktop`. matron's `screen_update()` then calls `paint()` instead of
 * the SSD1322 SPI path. A backend must also post EVENT_SCREEN_REFRESH (60 Hz): upstream only the SSD1322 thread
 * does that, and the Lua `refresh()` hook (menu/script animation) depends on it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
 * Written after matron/src/hardware/screen/sdl.cc and ssd1322.cc, Copyright (c) monome and norns contributors (GPL-3.0-or-later).
 */
#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "event_types.h"
#include "events.h"
#include "hardware/io.h"
#include "hardware/screen/screens.h"

#define SRC_W 128
#define SRC_H 64

typedef struct {
    char *dev;    // fb: framebuffer path
    char *path;   // ascii: output file
    int append;   // ascii: append frames instead of overwriting
    int fd;
    uint8_t *map;
    size_t maplen;
    int w, h, stride_bytes, bpp;
    double scale;     // picture scale: 2 = crisp integer (256x128), 2.5 = fills the width (smoothed)
    int ox, oy;       // picture offset inside the fb (centred)
    int dst_w, dst_h; // scaled picture size inside the fb (320x160 on a 320x170 panel)
    uint64_t last_hash;
    unsigned long frames;
    pthread_t th;
} uz_priv_t;

static char *cfg_string(lua_State *l, const char *key) {
    char *out = NULL;
    lua_pushstring(l, key);
    lua_gettable(l, -2);
    if (lua_isstring(l, -1)) {
        out = strdup(lua_tostring(l, -1));
    }
    lua_pop(l, 1);
    return out;
}

static int cfg_bool(lua_State *l, const char *key) {
    int v = 0;
    lua_pushstring(l, key);
    lua_gettable(l, -2);
    if (lua_isboolean(l, -1)) {
        v = lua_toboolean(l, -1);
    }
    lua_pop(l, 1);
    return v;
}

static void *refresh_thread(void *arg) {
    (void)arg;
    struct timespec ts = {0, 16666666}; // 60 Hz, like ssd1322_thread_run
    while (1) {
        event_post(event_data_new(EVENT_SCREEN_REFRESH));
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    }
    return NULL;
}

static int uz_create_surface(matron_fb_t *fb) {
    fb->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SRC_W, SRC_H);
    if (cairo_surface_status(fb->surface) != CAIRO_STATUS_SUCCESS) {
        return -1;
    }
    fb->cairo = cairo_create(fb->surface);
    return 0;
}

static void uz_bind(matron_fb_t *fb, cairo_surface_t *surface) {
    cairo_set_operator(fb->cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(fb->cairo, surface, 0, 0);
}

static void uz_destroy(matron_io_t *io) {
    matron_fb_t *fb = (matron_fb_t *)io;
    uz_priv_t *p = (uz_priv_t *)io->data;
    pthread_cancel(p->th);
    if (fb->cairo) cairo_destroy(fb->cairo);
    if (fb->surface) cairo_surface_destroy(fb->surface);
    if (p->map) munmap(p->map, p->maplen);
    if (p->fd > 0) close(p->fd);
    free(p->dev);
    free(p->path);
}

//---------------- ascii

static int ascii_config(matron_io_t *io, lua_State *l) {
    uz_priv_t *p = (uz_priv_t *)io->data;
    memset(p, 0, sizeof(*p));
    p->path = cfg_string(l, "path");
    p->append = cfg_bool(l, "append");
    if (!p->path) p->path = strdup("/tmp/norns-screen.txt");
    return 0;
}

static int ascii_setup(matron_io_t *io) {
    matron_fb_t *fb = (matron_fb_t *)io;
    uz_priv_t *p = (uz_priv_t *)io->data;
    if (uz_create_surface(fb)) return -1;
    pthread_create(&p->th, NULL, refresh_thread, NULL);
    return 0;
}

static void ascii_paint(matron_fb_t *fb) {
    static const char ramp[] = " .,:;-~=+*#%&8@M"; // 16 levels, 0 = black
    uz_priv_t *p = (uz_priv_t *)fb->io.data;
    cairo_paint(fb->cairo);
    cairo_surface_flush(fb->surface);
    const uint8_t *src = cairo_image_surface_get_data(fb->surface);
    const int stride = cairo_image_surface_get_stride(fb->surface);

    char text[(SRC_W + 1) * (SRC_H / 2) + 1];
    char *o = text;
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < SRC_H; y += 2) { // two source rows -> one text row (character cells are ~1:2)
        for (int x = 0; x < SRC_W; x++) {
            // ARGB32 little endian: byte 1 = green; norns draws grey, so any channel is the level
            int a = src[y * stride + x * 4 + 1] >> 4;
            int b = src[(y + 1) * stride + x * 4 + 1] >> 4;
            int lv = (a + b + 1) / 2;
            *o++ = ramp[lv];
            h = (h ^ (uint64_t)lv) * 1099511628211ULL;
        }
        *o++ = '\n';
    }
    *o = 0;
    if (h == p->last_hash && p->frames) return; // unchanged
    p->last_hash = h;
    p->frames++;

    if (p->append) {
        FILE *f = fopen(p->path, "a");
        if (!f) return;
        fprintf(f, "---- frame %lu\n%s", p->frames, text);
        fclose(f);
    } else {
        char tmp[512];
        snprintf(tmp, sizeof tmp, "%s.tmp", p->path);
        FILE *f = fopen(tmp, "w");
        if (!f) return;
        fputs(text, f);
        fclose(f);
        rename(tmp, p->path);
    }
}

//---------------- fb

static int fb_config(matron_io_t *io, lua_State *l) {
    uz_priv_t *p = (uz_priv_t *)io->data;
    memset(p, 0, sizeof(*p));
    p->dev = cfg_string(l, "dev");
    if (!p->dev) p->dev = strdup("/dev/fb0");
    lua_pushstring(l, "scale");
    lua_gettable(l, -2);
    p->scale = lua_isnumber(l, -1) ? lua_tonumber(l, -1) : 2.0;
    lua_pop(l, 1);
    if (p->scale < 1.0 || p->scale > 2.5) p->scale = 2.0;
    return 0;
}

static int fb_setup(matron_io_t *io) {
    matron_fb_t *fb = (matron_fb_t *)io;
    uz_priv_t *p = (uz_priv_t *)io->data;
    if (uz_create_surface(fb)) return -1;

    p->fd = open(p->dev, O_RDWR);
    if (p->fd < 0) {
        fprintf(stderr, "ERROR (screen:fb) cannot open %s: %s\n", p->dev, strerror(errno));
        return -1;
    }
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    struct stat st;
    fstat(p->fd, &st);
    if (S_ISREG(st.st_mode)) {
        // test mode: a plain file standing in for the framebuffer (320x170 RGB565)
        p->w = 320; p->h = 170; p->bpp = 16; p->stride_bytes = 640;
    } else {
        if (ioctl(p->fd, FBIOGET_VSCREENINFO, &vi) || ioctl(p->fd, FBIOGET_FSCREENINFO, &fi)) {
            fprintf(stderr, "ERROR (screen:fb) FBIOGET_*SCREENINFO failed\n");
            return -1;
        }
        p->w = vi.xres; p->h = vi.yres; p->bpp = vi.bits_per_pixel; p->stride_bytes = fi.line_length;
    }
    if (p->bpp != 16) {
        fprintf(stderr, "ERROR (screen:fb) only 16 bpp supported, got %d\n", p->bpp);
        return -1;
    }
    p->maplen = (size_t)p->stride_bytes * p->h;
    if (S_ISREG(st.st_mode) && ftruncate(p->fd, p->maplen)) return -1;
    p->map = (uint8_t *)mmap(NULL, p->maplen, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
    if (p->map == MAP_FAILED) {
        p->map = NULL;
        fprintf(stderr, "ERROR (screen:fb) mmap failed\n");
        return -1;
    }
    // 128x64 -> x scale (2: 256x128, crisp; 2.5: 320x160, smoothed), centred; the rest stays black
    p->dst_w = (int)(SRC_W * p->scale + 0.5);
    p->dst_h = (int)(SRC_H * p->scale + 0.5);
    if (p->dst_w > p->w) p->dst_w = p->w;
    if (p->dst_h > p->h) p->dst_h = p->h;
    p->ox = (p->w - p->dst_w) / 2;
    p->oy = (p->h - p->dst_h) / 2;
    memset(p->map, 0, p->maplen);
    fprintf(stderr, "screen:fb %s %dx%d bpp%d stride %d, picture %dx%d\n", p->dev, p->w, p->h, p->bpp,
            p->stride_bytes, p->dst_w, p->dst_h);
    pthread_create(&p->th, NULL, refresh_thread, NULL);
    return 0;
}

static void fb_trace(const char *w) {
    if (!getenv("UZUME_TRACE")) return;
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    fprintf(stderr, "TRACE %.3f %s\n", t.tv_sec % 1000 + t.tv_nsec / 1e9, w);
}
// SYSTEM > TINT: the colour the 16 norns greys are drawn in (default white), set from Lua by `_norns.screen_tint(r,g,b)`.
static volatile int uz_tint[3] = {255, 255, 255};
static volatile unsigned uz_tint_ver = 1;
void uz_screen_tint(int r, int g, int b) {
    uz_tint[0] = r < 0 ? 0 : r > 255 ? 255 : r;
    uz_tint[1] = g < 0 ? 0 : g > 255 ? 255 : g;
    uz_tint[2] = b < 0 ? 0 : b > 255 ? 255 : b;
    uz_tint_ver++;
}

static void fb_paint(matron_fb_t *fb) {
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    uz_priv_t *p = (uz_priv_t *)fb->io.data;
    cairo_paint(fb->cairo);
    cairo_surface_flush(fb->surface);
    const uint8_t *src = cairo_image_surface_get_data(fb->surface);
    const int stride = cairo_image_surface_get_stride(fb->surface);

    // grey level (0..255, from the 16 norns levels) -> RGB565; one LUT, one nearest-neighbour map per axis
    static uint16_t lut[256];
    static unsigned lut_ver;
    if (lut_ver != uz_tint_ver) {
        lut_ver = uz_tint_ver;
        const int tr = uz_tint[0], tg = uz_tint[1], tb = uz_tint[2];
        for (int v = 0; v < 256; v++) {
            int r = tr * v / 255, g = tg * v / 255, b = tb * v / 255;
            lut[v] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    const int integer = (p->scale == (double)(int)p->scale);
    for (int y = 0; y < p->dst_h; y++) {
        uint16_t *drow = (uint16_t *)(p->map + (size_t)(y + p->oy) * p->stride_bytes) + p->ox;
        if (integer) {
            const uint8_t *srow = src + (y * SRC_H / p->dst_h) * stride;
            for (int x = 0; x < p->dst_w; x++) drow[x] = lut[srow[(x * SRC_W / p->dst_w) * 4 + 1]];
        } else { // bilinear on the grey channel
            double fy = (y + 0.5) * SRC_H / p->dst_h - 0.5;
            int y0 = (int)fy; if (fy < 0) { y0 = 0; fy = 0; }
            int y1 = y0 + 1 < SRC_H ? y0 + 1 : y0;
            double wy = fy - y0;
            const uint8_t *r0 = src + y0 * stride, *r1 = src + y1 * stride;
            for (int x = 0; x < p->dst_w; x++) {
                double fx = (x + 0.5) * SRC_W / p->dst_w - 0.5;
                int x0 = (int)fx; if (fx < 0) { x0 = 0; fx = 0; }
                int x1 = x0 + 1 < SRC_W ? x0 + 1 : x0;
                double wx = fx - x0;
                double v = (r0[x0 * 4 + 1] * (1 - wx) + r0[x1 * 4 + 1] * wx) * (1 - wy) +
                           (r1[x0 * 4 + 1] * (1 - wx) + r1[x1 * 4 + 1] * wx) * wy;
                drow[x] = lut[(int)(v + 0.5)];
            }
        }
    }
    if (getenv("UZUME_TRACE")) { struct timespec t1; clock_gettime(CLOCK_MONOTONIC, &t1);
        struct timespec r; clock_gettime(CLOCK_REALTIME, &r);
        fprintf(stderr, "TRACE %.3f paint took %.1f ms\n", r.tv_sec % 1000 + r.tv_nsec / 1e9, (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6); }
}

screen_ops_t screen_ascii_ops = {
    {"screen:ascii", IO_SCREEN, sizeof(uz_priv_t), ascii_config, ascii_setup, uz_destroy},
    (void (*)(struct _matron_fb *))ascii_paint,
    uz_bind,
};

screen_ops_t screen_fb_ops = {
    {"screen:fb", IO_SCREEN, sizeof(uz_priv_t), fb_config, fb_setup, uz_destroy},
    (void (*)(struct _matron_fb *))fb_paint,
    uz_bind,
};
