#!/usr/bin/env python3
"""Placeholder LiveArea pictures for the Vita package (packaging/vita/sce_sys, docs/dev/VITA.md).

The Vita wants 8-bit palette PNGs: icon0.png 128x128, livearea bg.png 840x500 and
startup.png 280x158. Pure Python (zlib only), so it runs wherever the build does:

    python3 packaging/vita/make_art.py
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SYS = os.path.join(HERE, "sce_sys")

# 0 night blue, 1 red, 2 white, 3 asphalt
PALETTE = [(16, 18, 28), (200, 30, 40), (235, 235, 235), (40, 44, 60)]


def stripes(x, y, w, h):
    """Two slanted racing stripes over a dark field, with a strip of road along the bottom."""
    t = (x + 0.6 * y) / (w + 0.6 * h)
    if 0.40 < t < 0.48:
        return 1
    if 0.51 < t < 0.55:
        return 2
    return 3 if y > h * 0.85 else 0


def write_png(path, w, h):
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter: none
        raw.extend(stripes(x, y, w, h) for x in range(w))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))  # 8-bit, palette
    png += chunk(b"PLTE", bytes(c for rgb in PALETTE for c in rgb))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


if __name__ == "__main__":
    write_png(os.path.join(SYS, "icon0.png"), 128, 128)
    write_png(os.path.join(SYS, "livearea", "contents", "bg.png"), 840, 500)
    write_png(os.path.join(SYS, "livearea", "contents", "startup.png"), 280, 158)
