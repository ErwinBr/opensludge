#!/usr/bin/env python3
"""Make the sprite banks of CompositeTest from the sprite sheets of a
character of parts (a man in a suit), which stay outside this repository:
CMake runs this when SLUDGE_EGO_SPRITES names their folder.

Usage: make-ego-banks.py <folder of Ego_*.png> <folder for the banks>

Each sheet is a grid of frames of one size, numbered left to right and top
to bottom from 0, as in the Godot scenes of the game (Sprite2D with
hframes and vframes). Each frame becomes a sprite of a bank (.duc, version
3: see source/Engine/spritebankdecode.h), trimmed to its pixels, with its
hotspot at the point that Godot puts at the node's origin: a centred sprite
is drawn with its centre at the node's offset, so the origin lies at the
centre of the frame less the offset. Empty frames stay, as sprites of one
transparent pixel, to keep the numbers.
"""

import os
import struct
import sys
import zlib

# Sheet, bank, frames across and down, and the Godot offset of the sprite, by
# rows of the sheet when they differ (NORTH's two arms share one sheet).
SHEETS = [
    ('Ego_Legs_SOUTH.png', 'legs_south.duc', 5, 5, [(0, -85)]),
    ('Ego_Torso_SOUTH.png', 'torso_south.duc', 8, 4, [(0, -74.7417)]),
    ('Ego_Head_SOUTH.png', 'head_south.duc', 11, 1, [(7.96677, -53.0853)]),
    ('Ego_Arm1_SOUTH.png', 'arm1_south.duc', 5, 5, [(34.028, 138)]),
    ('Ego_Arm2_SOUTH.png', 'arm2_south.duc', 7, 2, [(0, 125.059)]),
    ('Ego_Legs_EAST.png', 'legs_east.duc', 5, 6, [(-15.0827, -131.476)]),
    ('Ego_Torso_EAST.png', 'torso_east.duc', 10, 3, [(16.8997, -102.294)]),
    ('Ego_Head_EAST.png', 'head_east.duc', 6, 2, [(48.543, -50.3949)]),
    ('Ego_Arm1_EAST.png', 'arm1_east.duc', 6, 7, [(-9.48395, 112.938)]),
    ('Ego_Arm2_EAST.png', 'arm2_east.duc', 7, 7, [(82.0565, 112.708)]),
    ('Ego_Legs_NORTH.png', 'legs_north.duc', 13, 3, [(0, -100)]),
    ('Ego_Torso_NORTH.png', 'torso_north.duc', 5, 5, [(0, -45)]),
    ('Ego_Head_NORTH.png', 'head_north.duc', 3, 1, [(-7.25972, -59.7845)]),
    ('Ego_Arms_NORTH.png', 'arms_north.duc', 14, 2, [(-8, 138), (8, 138)]),
]


def read_png(path):
    """RGBA rows (bytearrays) of an 8-bit PNG: palette, RGB or RGBA."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('%s: not a PNG' % path)
    pos = 8
    idat = b''
    plte = trns = b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT':
            idat += body
        elif kind == b'PLTE':
            plte = body
        elif kind == b'tRNS':
            trns = body
        pos += 12 + n
    if depth != 8 or interlace or ctype not in (2, 3, 6):
        sys.exit('%s: only 8-bit, non-interlaced palette, RGB or RGBA images' % path)
    bpp = {2: 3, 3: 1, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows = []
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(line)
        prev = line
    if ctype == 3:
        pal = []
        for i in range(256):
            rgb = plte[i * 3:i * 3 + 3] if i * 3 + 3 <= len(plte) else b'\0\0\0'
            pal.append(bytes(rgb) + bytes((trns[i] if i < len(trns) else 255,)))
        rows = [bytearray(b''.join(pal[i] for i in line)) for line in rows]
    elif ctype == 2:
        rows = [bytearray(b''.join(bytes(line[i:i + 3]) + b'\xff' for i in range(0, len(line), 3))) for line in rows]
    return w, h, rows


def png_without_signature(width, height, rows):
    """The chunks of an RGBA PNG, as version 3 banks keep a sprite."""
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    return (chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def frame(rows, x0, y0, w, h):
    """The pixels of a frame, trimmed: (x, y, width, height, rows), or None
    when it is empty."""
    cut = [r[x0 * 4:(x0 + w) * 4] for r in rows[y0:y0 + h]]
    top = bottom = None
    left, right = w, 0
    for y, r in enumerate(cut):
        alpha = bytes(r[3::4])
        if alpha.strip(b'\0'):
            if top is None:
                top = y
            bottom = y + 1
            left = min(left, len(alpha) - len(alpha.lstrip(b'\0')))
            right = max(right, len(alpha.rstrip(b'\0')))
    if top is None:
        return None
    return left, top, right - left, bottom - top, [r[left * 4:right * 4] for r in cut[top:bottom]]


def make_bank(sheet, bank, across, down, offsets):
    w, h, rows = read_png(sheet)
    if w % across or h % down:
        sys.exit('%s: %dx%d is not a grid of %dx%d frames' % (sheet, w, h, across, down))
    fw, fh = w // across, h // down
    out = bytearray(struct.pack('>HBH', 0, 3, across * down))
    for i in range(across * down):
        col, row = i % across, i // across
        ox, oy = offsets[row] if len(offsets) > 1 else offsets[0]
        # Godot's origin within the frame.
        hx, hy = int(round(fw / 2.0 - ox)), int(round(fh / 2.0 - oy))
        f = frame(rows, col * fw, row * fh, fw, fh)
        if f is None:
            f = (0, 0, 1, 1, [bytearray(4)])
        x, y, sw, sh, pixels = f
        out += struct.pack('<hh', hx - x, hy - y)
        out += png_without_signature(sw, sh, pixels)
    with open(bank, 'wb') as fp:
        fp.write(out)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source, target = sys.argv[1], sys.argv[2]
    os.makedirs(target, exist_ok=True)
    for sheet, bank, across, down, offsets in SHEETS:
        make_bank(os.path.join(source, sheet), os.path.join(target, bank), across, down, offsets)


if __name__ == '__main__':
    main()
