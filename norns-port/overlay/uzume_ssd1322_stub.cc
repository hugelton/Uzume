// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
/* No-op replacement for matron's SSD1322 OLED driver (SPI + libgpiod 1.x API, which Debian 13 no longer ships).
 * Uzume draws through screen:fb / screen:ascii instead (uzume_screen.cc). */
#include <stdint.h>
#include <stdbool.h>
#include <cairo.h>
#include "ssd1322.h"
extern "C" {
void ssd1322_init() {}
void ssd1322_deinit() {}
void ssd1322_refresh() {}
void ssd1322_update(cairo_surface_t *, bool) {}
void ssd1322_set_brightness(uint8_t) {}
void ssd1322_set_contrast(uint8_t) {}
void ssd1322_set_display_mode(ssd1322_display_mode_t) {}
void ssd1322_set_gamma(double) {}
void ssd1322_set_refresh_rate(uint8_t) {}
uint8_t *ssd1322_resize_buffer(size_t) { return NULL; }
}
