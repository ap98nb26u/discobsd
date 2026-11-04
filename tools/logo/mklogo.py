#!/usr/bin/env python3
"""Draw the DiscoBSD/gba boot logo and write it as a DEC sixel image.

Usage: mklogo.py out.six [preview.png]

The picture is made here from scratch: a mirror ball on the left and
"DiscoBSD" in the console's own 6x8 font (sys/arch/gba/dev/gba_font.c),
scaled up, with a line under it.  It is 240 pixels wide, the GBA screen's
width, and a multiple of 6 high, so it fills whole sixel bands.
Colours are kept to a small palette so each gets its own register.
Only the Python standard library is used.
"""
import math
import os
import re
import struct
import sys
import zlib

W, H = 240, 48
HERE = os.path.dirname(os.path.abspath(__file__))
FONT_C = os.path.join(HERE, '..', '..', 'sys', 'arch', 'gba', 'dev',
                      'gba_font.c')


def load_font():
    text = open(FONT_C).read()
    body = text[text.index('{') + 1:text.rindex('}')]
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    vals = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', body)]
    assert len(vals) == 2048
    return [vals[i * 8:i * 8 + 8] for i in range(256)]


def hsv(h, s, v):
    """h 0-360, s and v 0-1 -> (r, g, b) 0-255."""
    h = (h % 360) / 60.0
    i = int(h)
    f = h - i
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r, g, b = [(v, t, p), (q, v, p), (p, v, t),
               (p, q, v), (t, p, v), (v, p, q)][i % 6]
    return (int(r * 255 + .5), int(g * 255 + .5), int(b * 255 + .5))


def to555(c):
    """Round to what the GBA can show (5 bits a channel)."""
    return tuple((x * 31 + 127) // 255 * 255 // 31 for x in c)


def draw():
    img = [[None] * W for _ in range(H)]      # None = background (black)

    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H:
            img[y][x] = to555(c)

    # Mirror ball: a sphere of square facets, lit from the upper left.
    cx, cy, r = 23, 23, 20
    lx, ly, lz = -0.5, -0.6, 0.62
    n = math.sqrt(lx * lx + ly * ly + lz * lz)
    lx, ly, lz = lx / n, ly / n, lz / n
    for y in range(H):
        for x in range(W):
            dx, dy = (x + .5 - cx) / r, (y + .5 - cy) / r
            d2 = dx * dx + dy * dy
            if d2 > 1:
                continue
            dz = math.sqrt(1 - d2)
            lat = math.asin(-dy)               # facets follow the sphere
            lon = math.atan2(dx, dz)
            fi, fj = math.floor(lat / 0.26), math.floor(lon / 0.26)
            # each facet's own normal: its centre's
            clat, clon = (fi + .5) * 0.26, (fj + .5) * 0.26
            ny = -math.sin(clat)
            nx = math.cos(clat) * math.sin(clon)
            nz = math.cos(clat) * math.cos(clon)
            lum = max(0.0, nx * lx + ny * ly + nz * lz)
            seam = (abs(lat / 0.26 - round(lat / 0.26)) < 0.09 or
                    abs(lon / 0.26 - round(lon / 0.26)) < 0.09 / max(.2, math.cos(lat)))
            tint = ((fi * 7 + fj * 3) % 5) * 0.03   # facets differ a little
            v = 0.25 + 0.7 * lum + tint
            if seam:
                v *= 0.45
            v = round(min(1.0, v) * 12) / 12     # 12 shades keep it small
            put(x, y, hsv(215, 0.25 - 0.2 * round(lum * 4) / 4, v))
    # glints
    for gx, gy, gr in ((14, 12, 3), (31, 30, 2), (9, 28, 1)):
        for i in range(-gr, gr + 1):
            put(gx + i, gy, (255, 255, 255))
            put(gx, gy + i, (255, 255, 255))
    # hanging string
    for y in range(0, cy - r):
        put(cx, y, (120, 120, 140))

    # "DiscoBSD": the console font, 3x, a rainbow left to right, shadowed.
    font = load_font()
    text = "DiscoBSD"
    sx, sy, scale = 56, 4, 3
    tw = len(text) * 6 * scale

    def glyph_pixels():
        for k, ch in enumerate(text):
            rows = font[ord(ch)]
            for gy in range(8):
                for gx in range(6):
                    if rows[gy] & (0x80 >> gx):
                        yield k * 6 + gx, gy
    def rainbow(px):                           # magenta to red, 15-degree steps
        return round((300 - 300 * (px - sx) / tw) / 15) * 15
    pix = list(glyph_pixels())
    for gx, gy in pix:                         # shadow first
        for a in range(scale):
            for b in range(scale):
                put(sx + gx * scale + a + 2, sy + gy * scale + b + 2,
                    (60, 20, 90))
    for gx, gy in pix:
        for a in range(scale):
            for b in range(scale):
                px = sx + gx * scale + a
                put(px, sy + gy * scale + b, hsv(rainbow(px), 0.85, 1.0))

    # a rule under the name, and a line of small text below it
    small = "2.11BSD for the GBA"
    for k, ch in enumerate(small):
        rows = font[ord(ch)]
        for gy in range(8):
            for gx in range(6):
                if rows[gy] & (0x80 >> gx):
                    put(sx + 2 + k * 6 + gx, 36 + gy, (200, 200, 220))
    for x in range(sx, sx + tw):
        put(x, 31, hsv(rainbow(x), 0.85, 0.7))
    return img


def palette(img):
    cols = sorted({c for row in img for c in row if c is not None})
    assert len(cols) <= 255, len(cols)
    return cols


def sixel(img, cols):
    idx = {c: i + 1 for i, c in enumerate(cols)}
    out = ['\x1bP0;1;0q', '"1;1;%d;%d' % (W, H)]
    for i, c in enumerate(cols):
        out.append('#%d;2;%d;%d;%d' % ((i + 1,) + tuple(
            (v * 100 + 127) // 255 for v in c)))
    for band in range(0, H, 6):
        first = True
        for ci in range(1, len(cols) + 1):
            line = []
            used = False
            for x in range(W):
                v = 0
                for b in range(6):
                    y = band + b
                    if y < H and img[y][x] is not None and idx[img[y][x]] == ci:
                        v |= 1 << b
                line.append(v)
                used |= v != 0
            if not used:
                continue
            while line and line[-1] == 0:
                line.pop()
            s = []
            i = 0
            while i < len(line):
                j = i
                while j < len(line) and line[j] == line[i]:
                    j += 1
                ch = chr(63 + line[i])
                n = j - i
                s.append('!%d%s' % (n, ch) if n > 3 else ch * n)
                i = j
            out.append(('' if first else '$') + '#%d' % ci + ''.join(s))
            first = False
        out.append('-')
    out.append('\x1b\\')
    return ''.join(out)


def png(path, img):
    raw = b''.join(b'\0' + b''.join(bytes(c if c else (0, 0, 0)) for c in row)
                   for row in img)

    def chunk(t, d):
        return (struct.pack('>I', len(d)) + t + d +
                struct.pack('>I', zlib.crc32(t + d) & 0xffffffff))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def main():
    img = draw()
    cols = palette(img)
    data = sixel(img, cols)
    with open(sys.argv[1], 'w', newline='') as f:
        f.write(data)
        f.write('\n')
    if len(sys.argv) > 2:
        png(sys.argv[2], img)
    print('%d colours, %d bytes' % (len(cols), len(data)), file=sys.stderr)


if __name__ == '__main__':
    main()
