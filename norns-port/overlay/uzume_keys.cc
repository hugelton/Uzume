/*
 * uzume_keys.cc  --  keyboard backend for norns' matron on the Cardputer Zero (no knobs, no three buttons).
 *
 *   keys:cpz  reads a linux evdev keyboard and posts norns' own events:
 *             K1 K2 K3  <-  keys 1 2 3 (press and release, so holds work); Backspace = K2 (back), Enter = K3 (ok)
 *             E1 E2 E3  <-  a pair of keys each, turning by -1 / +1 (key repeat keeps it turning):
 *                           E1: Q W   or  Page Up / Page Down
 *                           E2: A S   or  Z C (the Zero's "<" ">")   or  Left / Right
 *                           E3: X F (the Zero's down / up)   or  Down / Up
 *             Shift held: the encoder moves 4 steps at a time.
 *             Esc held for 1 s: quit (Esc alone does nothing else: scripts use K2 for their own things).
 *
 * Config (Lua): _boot.add_io('keys:cpz', {dev = '/dev/input/by-path/...'})
 * The device is grabbed (EVIOCGRAB) so the launcher does not also react to the same keys; it is released on exit.
 * The keyboard sends no auto-repeat, so a held encoder key repeats here (after 180 ms, every 45 ms).
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
 * Written after matron/src/hardware/input/gpio.cc, Copyright (c) monome and norns contributors (GPL-3.0-or-later).
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#include "events.h"
#include "hardware/input.h"
#include "hardware/io.h"
#include "hardware/input/inputs.h"

typedef struct {
    int fd;
    char *dev;
} cpz_priv_t;

static int cpz_config(matron_io_t *io, lua_State *l) {
    cpz_priv_t *p = (cpz_priv_t *)io->data;
    p->dev = NULL;
    lua_pushstring(l, "dev");
    lua_gettable(l, -2);
    if (lua_isstring(l, -1)) {
        p->dev = strdup(lua_tostring(l, -1));
    } else {
        fprintf(stderr, "ERROR (keys:cpz) config option 'dev' should be a string\n");
        lua_settop(l, 0);
        return -1;
    }
    lua_pop(l, 1);
    return 0;
}

static int cpz_setup(matron_io_t *io) {
    cpz_priv_t *p = (cpz_priv_t *)io->data;
    p->fd = -1;
    for (int i = 0; i < 100 && p->fd < 0; i++) { // the device can appear a little after boot
        p->fd = open(p->dev, O_RDONLY);
        if (p->fd < 0) usleep(50000);
    }
    if (p->fd < 0) {
        fprintf(stderr, "ERROR (keys:cpz) device not available: %s\n", p->dev);
        return -1;
    }
    if (ioctl(p->fd, EVIOCGRAB, 1) != 0) fprintf(stderr, "WARN (keys:cpz) could not grab the keyboard\n");
    return input_setup(io);
}

static void cpz_destroy(matron_io_t *io) {
    cpz_priv_t *p = (cpz_priv_t *)io->data;
    free(p->dev);
    input_destroy(io);
}

static int trace_on(void) { static int t = -1; if (t < 0) t = getenv("UZUME_TRACE") != NULL; return t; }
static void trace(const char *what, const struct input_event *e) {
    if (!trace_on()) return;
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    double age = (t.tv_sec - e->time.tv_sec) * 1000.0 + (t.tv_nsec / 1e6 - e->time.tv_usec / 1e3);
    fprintf(stderr, "TRACE %.3f %s evdev->matron %.1f ms\n", t.tv_sec % 1000 + t.tv_nsec / 1e9, what, age);
}

static void post_key(int n, int val) {
    union event_data *ev = event_data_new(EVENT_KEY);
    ev->key.n = n;
    ev->key.val = val;
    event_post(ev);
}

static void post_enc(int n, int delta) {
    if (trace_on()) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); fprintf(stderr, "TRACE %.3f post_enc %d %d\n", t.tv_sec % 1000 + t.tv_nsec / 1e9, n, delta); }
    union event_data *ev = event_data_new(EVENT_ENC);
    ev->enc.n = n;
    ev->enc.delta = delta;
    event_post(ev);
}

static int enc_map(int code, int *n, int *d) {
    *n = 0; *d = 0;
    switch (code) {
    case KEY_Q: case KEY_PAGEUP:   *n = 1; *d = -1; break;
    case KEY_W: case KEY_PAGEDOWN: *n = 1; *d = 1; break;
    case KEY_A: case KEY_Z: case KEY_LEFT:   *n = 2; *d = -1; break; // Z = "<" on the Zero's pad
    case KEY_S: case KEY_C: case KEY_RIGHT:  *n = 2; *d = 1; break;  // C = ">"
    case KEY_X: case KEY_DOWN: case KEY_MINUS: *n = 3; *d = -1; break; // X = down; - / = as on norns' keyboard
    case KEY_F: case KEY_UP: case KEY_EQUAL:   *n = 3; *d = 1; break;  // F = up
    }
    return *n != 0;
}

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void *cpz_poll(void *data) {
    matron_input_t *input = (matron_input_t *)data;
    cpz_priv_t *p = (cpz_priv_t *)input->io.data;
    struct input_event evs[64];
    int shift = 0;
    struct timespec esc_down = {0, 0};
    int esc_held = 0;
    int held_code = 0;
    double next_rep = 0;

    while (1) {
        // wake up every 100 ms so a held Esc is noticed without further events
        struct timespec ts = {0, 100000000};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(p->fd, &fds);
        struct timeval tv = {0, 10000};
        (void)ts;
        int r = select(p->fd + 1, &fds, NULL, NULL, &tv);
        if (esc_held) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double held = (now.tv_sec - esc_down.tv_sec) + (now.tv_nsec - esc_down.tv_nsec) / 1e9;
            if (held >= 1.0) {
                esc_held = 0;
                event_post(event_data_new(EVENT_QUIT));
            }
        }
        if (held_code && now_s() >= next_rep) {
            int n, d;
            if (enc_map(held_code, &n, &d)) post_enc(n, d * (shift ? 4 : 1));
            next_rep = now_s() + 0.045;
        }
        if (r <= 0) continue;
        int rd = read(p->fd, evs, sizeof(evs));
        if (rd < (int)sizeof(struct input_event)) {
            if (rd < 0 && errno == EINTR) continue;
            fprintf(stderr, "ERROR (keys:cpz) read error\n");
            usleep(200000);
            continue;
        }
        for (unsigned i = 0; i < rd / sizeof(struct input_event); i++) {
            if (evs[i].type != EV_KEY) continue;
            trace("key", &evs[i]);
            int code = evs[i].code, val = evs[i].value; // 0 up, 1 down, 2 repeat
            if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) { shift = val ? 1 : 0; continue; }
            if (code == KEY_ESC) {
                if (val == 1) { clock_gettime(CLOCK_MONOTONIC, &esc_down); esc_held = 1; }
                if (val == 0) esc_held = 0;
                continue;
            }
            int k = 0;
            switch (code) { case KEY_1: k = 1; break; case KEY_2: k = 2; break; case KEY_3: k = 3; break;
                            case KEY_ENTER: k = 3; break;        // Enter = K3 (ok)
                            case KEY_BACKSPACE: k = 2; break; } // Backspace = K2 (back)
            if (k) { if (val != 2) post_key(k, val); continue; }
            if (val == 0) { if (code == held_code) held_code = 0; continue; }
            if (val == 2) continue; // (this keyboard sends none; repeat is done here)
            int n, d;
            if (!enc_map(code, &n, &d)) continue;
            held_code = code;
            next_rep = now_s() + 0.18;
            post_enc(n, d * (shift ? 4 : 1));
        }
    }
    return NULL;
}

input_ops_t key_cpz_ops = {
    {
        "keys:cpz",
        IO_INPUT,
        sizeof(cpz_priv_t),
        cpz_config,
        cpz_setup,
        cpz_destroy,
    },
    cpz_poll,
};
