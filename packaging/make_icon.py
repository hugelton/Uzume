#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the Uzume launcher icons (dark tile, a rising moon over a wave line) as PNG files, no libraries needed."""
import math, struct, sys, zlib

def png(path, S):
    rows = []
    for y in range(S):
        row = bytearray([0])
        for x in range(S):
            u, v = x / (S - 1), y / (S - 1)
            r, g, b, a = 20, 24, 44, 255
            cx, cy = abs(u - 0.5), abs(v - 0.5)
            if cx > 0.36 and cy > 0.36 and math.hypot(cx - 0.36, cy - 0.36) > 0.14:
                a = 0
            d = math.hypot(u - 0.5, v - 0.40)                      # moon
            if d < 0.22:
                r, g, b = 232, 226, 196
                if math.hypot(u - 0.58, v - 0.34) < 0.19: r, g, b = 20, 24, 44   # crescent cut
            w = 0.74 + 0.04 * math.sin(u * 6.28 * 2)                # wave line
            if abs(v - w) < 0.018: r, g, b = 110, 150, 200
            row += bytes([r, g, b, a])
        rows.append(bytes(row))
    raw = b"".join(rows)
    def chunk(t, d): c = struct.pack(">I", len(d)) + t + d; return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", S, S, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

for name, size in (("uzume.png", 80), ("uzume_80.png", 80), ("uzume_100.png", 100)):
    png(sys.argv[1] + "/" + name, size)
