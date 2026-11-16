/*
 * uzume_screen.cc  --  screen backends for running norns' matron without the SSD1322 OLED.
 *
 *   screen:ascii  dumps the 128x64 screen as text (headless tests)
 *   screen:fb     writes it, scaled 2x or 2.5x, to the 16 bpp LCD (Cardputer Zero: 320x170). It draws through DRM
 *                 (/dev/dri/card*, a dumb buffer sent with DIRTYFB) when it can: the kernel's /dev/fb0 emulation only
 *                 sends the picture to the panel every 50 ms (about 19 Hz at best), DRM sends each changed frame
 *                 right away (about 50 Hz for the 256x128 picture over the panel's SPI bus). /dev/fb0 is the fallback.
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
#include <math.h>
#include <fcntl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
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
    // DRM (screen:fb only): `map` is then our own back buffer; the flush thread copies the changed rows into the
    // dumb buffer and sends them, so paint() never waits for the SPI transfer (about 20 ms for the picture)
    char *drm;        // config: "auto" (default), "off", or a /dev/dri/cardN path
    int drm_fd;       // > 0 when drawing through DRM
    uint32_t drm_fb;
    uint8_t *drm_map;
    size_t drm_len;
    int dirty_y0, dirty_y1; // rows waiting to be sent, y1 = 0: none
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t flush_th;
    int flush_started;
    uint8_t *mirror;  // UZUME_FB_MIRROR=1: DRM frames are also copied to /dev/fb0 (for screenshots read from it)
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
    if (p->drm_fd > 0) {
        if (p->flush_started) { pthread_cancel(p->flush_th); pthread_join(p->flush_th, NULL); }
        if (p->drm_map) munmap(p->drm_map, p->drm_len);
        if (p->mirror) munmap(p->mirror, 640 * 170);
        close(p->drm_fd); // the kernel puts its /dev/fb0 picture (the launcher's) back on the LCD
        free(p->map);
        p->map = NULL;
    }
    if (p->map) munmap(p->map, p->maplen);
    if (p->fd > 0) close(p->fd);
    free(p->drm);
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
    p->drm = cfg_string(l, "drm");
    if (!p->drm) p->drm = strdup("auto");
    return 0;
}

//---------------- fb through DRM

#define UZ_FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

static void *flush_thread(void *arg) {
    uz_priv_t *p = (uz_priv_t *)arg;
    for (;;) {
        pthread_mutex_lock(&p->lock);
        while (p->dirty_y1 == 0) pthread_cond_wait(&p->cond, &p->lock);
        int y0 = p->dirty_y0, y1 = p->dirty_y1;
        p->dirty_y1 = 0;
        for (int y = y0; y < y1; y++)
            memcpy(p->drm_map + (size_t)y * p->stride_bytes + p->ox * 2, p->map + (size_t)y * p->stride_bytes + p->ox * 2,
                   (size_t)p->dst_w * 2);
        if (p->mirror)
            for (int y = y0; y < y1; y++)
                memcpy(p->mirror + (size_t)y * 640 + p->ox * 2, p->map + (size_t)y * p->stride_bytes + p->ox * 2,
                       (size_t)p->dst_w * 2);
        pthread_mutex_unlock(&p->lock);
        struct drm_clip_rect clip = {(unsigned short)p->ox, (unsigned short)y0, (unsigned short)(p->ox + p->dst_w),
                                     (unsigned short)y1};
        struct drm_mode_fb_dirty_cmd dc = {};
        dc.fb_id = p->drm_fb; dc.num_clips = 1; dc.clips_ptr = (uintptr_t)&clip;
        ioctl(p->drm_fd, DRM_IOCTL_MODE_DIRTYFB, &dc); // sends the rows to the panel; returns when they are sent
    }
    return NULL;
}

// The first card with a connected SPI panel (or `path`). Returns the fd, or -1 (then /dev/fb0 is used).
static int drm_open(uz_priv_t *p, const char *path) {
    for (int i = 0; i < 8; i++) {
        char dev[32];
        if (strcmp(path, "auto")) { if (i) break; snprintf(dev, sizeof dev, "%s", path); }
        else snprintf(dev, sizeof dev, "/dev/dri/card%d", i);
        int fd = open(dev, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        uint32_t crtcs[8], conns[8];
        struct drm_mode_card_res res = {};
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) || !res.count_crtcs || !res.count_connectors ||
            res.count_crtcs > 8 || res.count_connectors > 8) { close(fd); continue; }
        res.crtc_id_ptr = (uintptr_t)crtcs; res.connector_id_ptr = (uintptr_t)conns;
        res.count_fbs = 0; res.count_encoders = 0;
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { close(fd); continue; }
        for (unsigned c = 0; c < res.count_connectors; c++) {
            struct drm_mode_modeinfo modes[8];
            struct drm_mode_get_connector gc = {};
            gc.connector_id = conns[c];
            if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &gc)) continue;
            int spi = gc.connector_type == DRM_MODE_CONNECTOR_SPI;
            if ((!spi && !strcmp(path, "auto")) || gc.connection != 1 || gc.count_modes < 1 || gc.count_modes > 8) continue;
            gc.modes_ptr = (uintptr_t)modes; gc.count_props = 0; gc.count_encoders = 0;
            if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &gc)) continue;
            const int W = modes[0].hdisplay, H = modes[0].vdisplay;
            struct drm_mode_create_dumb cd = {};
            cd.width = W; cd.height = H; cd.bpp = 16;
            if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) break;
            struct drm_mode_fb_cmd2 fb = {};
            fb.width = W; fb.height = H; fb.pixel_format = UZ_FOURCC('R', 'G', '1', '6');
            fb.handles[0] = cd.handle; fb.pitches[0] = cd.pitch;
            struct drm_mode_map_dumb md = {};
            md.handle = cd.handle;
            if (ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb) || ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) break;
            uint8_t *map = (uint8_t *)mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
            if (map == MAP_FAILED) break;
            memset(map, 0, cd.size);
            struct drm_mode_crtc cr = {};
            cr.crtc_id = crtcs[0]; cr.fb_id = fb.fb_id; cr.set_connectors_ptr = (uintptr_t)&conns[c];
            cr.count_connectors = 1; cr.mode = modes[0]; cr.mode_valid = 1;
            if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &cr)) { // e.g. another program holds the display
                fprintf(stderr, "screen:fb %s: cannot show a picture (%s), using /dev/fb0\n", dev, strerror(errno));
                munmap(map, cd.size);
                break;
            }
            p->w = W; p->h = H; p->bpp = 16; p->stride_bytes = cd.pitch;
            p->drm_fb = fb.fb_id; p->drm_map = map; p->drm_len = cd.size;
            fprintf(stderr, "screen:fb %s (DRM) %dx%d pitch %u\n", dev, W, H, cd.pitch);
            return fd;
        }
        close(fd);
    }
    return -1;
}

static int fb_setup(matron_io_t *io) {
    matron_fb_t *fb = (matron_fb_t *)io;
    uz_priv_t *p = (uz_priv_t *)io->data;
    if (uz_create_surface(fb)) return -1;

    struct stat dst;
    const int test_file = !stat(p->dev, &dst) && S_ISREG(dst.st_mode); // a file standing in for /dev/fb0: no DRM
    if (!test_file && strcmp(p->drm, "off") && (p->drm_fd = drm_open(p, p->drm)) > 0) {
        p->maplen = (size_t)p->stride_bytes * p->h;
        p->map = (uint8_t *)calloc(1, p->maplen);
        pthread_mutex_init(&p->lock, NULL);
        pthread_cond_init(&p->cond, NULL);
        const char *mi = getenv("UZUME_FB_MIRROR");
        if (mi && !strcmp(mi, "1") && p->w == 320 && p->h == 170 && (p->fd = open(p->dev, O_RDWR)) > 0) {
            p->mirror = (uint8_t *)mmap(NULL, 640 * 170, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
            if (p->mirror == MAP_FAILED) p->mirror = NULL;
        }
        goto placed;
    }
    p->drm_fd = 0;
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
    memset(p->map, 0, p->maplen);
placed:
    // 128x64 -> x scale (2: 256x128, crisp; 2.5: 320x160, smoothed), centred; the rest stays black
    p->dst_w = (int)(SRC_W * p->scale + 0.5);
    p->dst_h = (int)(SRC_H * p->scale + 0.5);
    if (p->dst_w > p->w) p->dst_w = p->w;
    if (p->dst_h > p->h) p->dst_h = p->h;
    p->ox = (p->w - p->dst_w) / 2;
    p->oy = (p->h - p->dst_h) / 2;
    fprintf(stderr, "screen:fb %s %dx%d bpp%d stride %d, picture %dx%d\n", p->dev, p->w, p->h, p->bpp,
            p->stride_bytes, p->dst_w, p->dst_h);
    if (p->drm_fd > 0) p->flush_started = !pthread_create(&p->flush_th, NULL, flush_thread, p);
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
// SYSTEM > SETTINGS > DISPLAY: GAMMA and CONTRAST, which drive the SSD1322's grey table on norns (uzume_ssd1322_stub.cc
// forwards them here). The OLED's light grows in proportion to the grey level; an LCD pixel value does not (sRGB,
// about value^2.2), so without correction the dim levels nearly vanish. The table below makes the light proportional to
// level^gamma, like the OLED: gamma 1.0 (norns' default) is linear light, higher values darken the low levels as on norns.
static volatile double uz_gamma = 1.0;
static volatile int uz_contrast = 127;
void uz_screen_gamma(double g) { uz_gamma = g < 0.1 ? 0.1 : g > 30.0 ? 30.0 : g; uz_tint_ver++; }
void uz_screen_contrast(int c) { uz_contrast = c < 0 ? 0 : c > 255 ? 255 : c; uz_tint_ver++; }
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
        const double gamma = uz_gamma, scale = uz_contrast / 127.0;
        for (int v = 0; v < 256; v++) {
            double light = pow(v / 255.0, gamma) * scale;      // the OLED's light for this grey
            if (light > 1.0) light = 1.0;
            const double k = pow(light, 1.0 / 2.2);             // as an LCD pixel value
            int r = (int)(tr * k + 0.5), g = (int)(tg * k + 0.5), b = (int)(tb * k + 0.5);
            lut[v] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    const int integer = (p->scale == (double)(int)p->scale);
    const int drm = p->drm_fd > 0;
    int d0 = p->h, d1 = 0;
    uint16_t scratch[1024];
    if (drm) pthread_mutex_lock(&p->lock);
    for (int y = 0; y < p->dst_h; y++) {
        uint16_t *out = (uint16_t *)(p->map + (size_t)(y + p->oy) * p->stride_bytes) + p->ox;
        uint16_t *drow = drm ? scratch : out;
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
        if (drm && memcmp(out, scratch, (size_t)p->dst_w * 2)) {
            memcpy(out, scratch, (size_t)p->dst_w * 2);
            if (y + p->oy < d0) d0 = y + p->oy;
            d1 = y + p->oy + 1;
        }
    }
    if (drm) {
        if (d1) {
            if (p->dirty_y1 == 0) { p->dirty_y0 = d0; p->dirty_y1 = d1; }
            else { if (d0 < p->dirty_y0) p->dirty_y0 = d0; if (d1 > p->dirty_y1) p->dirty_y1 = d1; }
            pthread_cond_signal(&p->cond);
        }
        pthread_mutex_unlock(&p->lock);
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
