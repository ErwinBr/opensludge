#!/usr/bin/env python3
# Make the images of the RenderTest project. We keep the images in the
# repository, and this script only to make them again after a change.
#
# Usage: make-images.py <folder>

import struct
import sys
import zlib


def write_png(path, width, height, pixel):
    """Write an RGBA PNG file. pixel(x, y) returns (r, g, b, a)."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(pixel(x, y))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def hole(x, y):
    # Diagonal stripes, with a transparent window in the middle, through
    # which the parallax layer shows, and a half transparent band.
    if 220 <= x < 420 and 140 <= y < 340:
        return (0, 0, 0, 0)
    stripe = ((x + y) // 24) % 3
    colour = [(200, 60, 40), (240, 200, 60), (60, 120, 200)][stripe]
    alpha = 128 if 380 <= y < 420 else 255
    return colour + (alpha,)


def sky(x, y):
    # A checkerboard that tiles, for a parallax layer that repeats.
    if ((x // 32) + (y // 32)) % 2:
        return (30, 30, 90, 255)
    return (90 + y // 4, 160, 220 - x // 4, 255)


def light(x, y):
    # From red on the left through white to blue on the right.
    if x < 320:
        t = x * 255 // 319
        return (255, t, t, 255)
    t = (639 - x) * 255 // 319
    return (t, t, 255, 255)


def big(x, y):
    # A grid with numbered-looking blocks, for scrolling and zooming.
    if x % 64 == 0 or y % 64 == 0:
        return (255, 255, 255, 255)
    return ((x // 64) * 12 % 256, (y // 64) * 16 % 256, 128, 255)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    write_png(folder + "/hole.png", 640, 480, hole)
    write_png(folder + "/sky.png", 256, 256, sky)
    write_png(folder + "/light.png", 640, 480, light)
    write_png(folder + "/big.png", 1280, 960, big)


if __name__ == "__main__":
    main()
