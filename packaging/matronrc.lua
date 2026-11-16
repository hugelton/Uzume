-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
-- Cardputer Zero: LCD (/dev/fb0, 320x170) + the built-in keyboard standing in for K1-K3 and E1-E3
_boot.add_io('screen:fb', {dev = os.getenv('UZUME_FB') or '/dev/fb0', scale = tonumber(os.getenv('UZUME_SCALE') or '2')})
_boot.add_io('keys:cpz', {dev = os.getenv('UZUME_KBD') or '/dev/input/by-path/platform-3f804000.i2c-event'})
