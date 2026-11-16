// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
// Preloaded into Uzume's programs: now and then it pushes this process's idle anonymous memory into zram (MADV_PAGEOUT), so that big buffers
// that nothing is touching cost almost no RAM. The pages come back by themselves when something reads them.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif
static void sweep(size_t min_bytes) {
    FILE* f = fopen("/proc/self/maps", "r"); if (!f) return; char l[512];
    while (fgets(l, sizeof l, f)) {
        unsigned long a, b, off, ino; char perm[8], dev[16], path[256] = "";
        int n = sscanf(l, "%lx-%lx %7s %lx %15s %lu %255s", &a, &b, perm, &off, dev, &ino, path);
        if (n < 6 || perm[0] != 'r' || perm[1] != 'w' || perm[3] == 's') continue;
        if (n == 7 && path[0] == '/') continue;                       // file-backed memory is left alone
        if (n == 7 && strstr(path, "[stack")) continue;
        if (b - a < min_bytes) continue;
        madvise((void*)a, b - a, MADV_PAGEOUT);
    }
    fclose(f);
}
// The sweeps start when the file named by UZUME_PAGEOUT_GO exists (the launcher creates it once everything has started: a sweep while sclang
// is still compiling its class library makes it crawl). Without that variable they start after UZUME_PAGEOUT_DELAY seconds (default 20).
static void* loop(void* arg) {
    (void)arg;
    const char* go = getenv("UZUME_PAGEOUT_GO");
    int delay = getenv("UZUME_PAGEOUT_DELAY") ? atoi(getenv("UZUME_PAGEOUT_DELAY")) : 20;
    int every = getenv("UZUME_PAGEOUT_EVERY") ? atoi(getenv("UZUME_PAGEOUT_EVERY")) : 10;
    if (go) { while (access(go, F_OK) != 0) sleep(1); sleep(3); } else sleep(delay);
    for (;;) { sweep(1 << 20); sleep(every); }
    return 0;
}
__attribute__((constructor)) static void init(void) { pthread_t t; pthread_create(&t, 0, loop, 0); pthread_detach(t); }
