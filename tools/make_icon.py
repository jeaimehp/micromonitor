#!/usr/bin/env python3
"""Generate the menu bar template icon (black + alpha): a small screen with a graph line. Writes 18pt @1x and @2x."""
import struct
import sys
import zlib


def png(path, size, px):
    raw = b"".join(b"\x00" + bytes(px[y * size * 4:(y + 1) * size * 4]) for y in range(size))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def render(size):
    s = size / 18.0          # design grid is 18x18
    px = bytearray(size * size * 4)

    def dot(x, y, r):
        for yy in range(int(y - r), int(y + r) + 2):
            for xx in range(int(x - r), int(x + r) + 2):
                if 0 <= xx < size and 0 <= yy < size and (xx + .5 - x) ** 2 + (yy + .5 - y) ** 2 <= r * r:
                    px[(yy * size + xx) * 4 + 3] = 255

    def line(x0, y0, x1, y1, w):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * s * 2) + 1
        for i in range(n + 1):
            t = i / n
            dot((x0 + (x1 - x0) * t) * s, (y0 + (y1 - y0) * t) * s, w * s / 2)

    # screen outline + stand
    for a, b in [((1.5, 3), (16.5, 3)), ((16.5, 3), (16.5, 13)), ((16.5, 13), (1.5, 13)), ((1.5, 13), (1.5, 3))]:
        line(*a, *b, 1.4)
    line(9, 13, 9, 15.5, 1.4)
    line(6, 15.8, 12, 15.8, 1.4)
    # graph line inside the screen
    pts = [(3.5, 10.5), (6, 8.5), (8, 9.8), (11, 5.5), (13, 7.5), (14.5, 6.5)]
    for a, b in zip(pts, pts[1:]):
        line(*a, *b, 1.3)
    return px


out = sys.argv[1] if len(sys.argv) > 1 else "host/resources"
png(f"{out}/menubar.png", 18, render(18))
png(f"{out}/menubar@2x.png", 36, render(36))
print("wrote", out)
