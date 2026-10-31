#!/usr/bin/env python3
"""
pcf2bdf.py - convert an X11 PCF bitmap font (optionally gzipped) to BDF, so
it can be fed to bdf2c.py.

Only what bdf2c.py needs is kept: the font properties FONT_ASCENT,
FONT_DESCENT and COPYRIGHT, and per glyph its encoding, metrics and bitmap.
The console font tools/font/gba_font.bdf was made from the X.Org misc-misc
5x8 font this way:

    pcf2bdf.py 5x8-ISO8859-1.pcf.gz > gba_font.bdf

Usage: pcf2bdf.py font.pcf[.gz] > font.bdf
"""
import gzip, struct, sys

PROPERTIES, METRICS, BITMAPS, BDF_ENCODINGS = 1 << 0, 1 << 2, 1 << 3, 1 << 5
COMPRESSED_METRICS = 0x100


def fmt_order(fmt):
    return ">" if fmt & (1 << 2) else "<"     # PCF_BYTE_MASK: big endian


def main():
    path = sys.argv[1]
    data = (gzip.open if path.endswith(".gz") else open)(path, "rb").read()
    if data[:4] != b"\x01fcp":
        sys.exit("not a PCF file")
    ntables, = struct.unpack_from("<I", data, 4)
    tables = {}
    for i in range(ntables):
        typ, fmt, size, off = struct.unpack_from("<IIII", data, 8 + 16 * i)
        tables[typ] = off

    props = read_properties(data, tables[PROPERTIES])
    metrics = read_metrics(data, tables[METRICS])
    bitmaps = read_bitmaps(data, tables[BITMAPS], metrics)
    encodings = read_encodings(data, tables[BDF_ENCODINGS])

    out = sys.stdout.write
    out("STARTFONT 2.1\n")
    out("COMMENT Converted from %s by tools/font/pcf2bdf.py\n" % path.split("/")[-1])
    out("FONT %s\n" % props.get("FONT", "unknown"))
    out("SIZE 8 75 75\n")
    lsb = min(m[0] for m in metrics)
    out("FONTBOUNDINGBOX %d %d %d %d\n" % (
        max(m[1] for m in metrics) - lsb,
        max(m[3] for m in metrics) + max(m[4] for m in metrics),
        lsb, -max(m[4] for m in metrics)))
    # PCF keeps the font ascent/descent in its accelerator table, not as
    # properties; the glyph metrics give the same values.
    props.setdefault("FONT_ASCENT", max(m[3] for m in metrics))
    props.setdefault("FONT_DESCENT", max(m[4] for m in metrics))
    keep = [k for k in ("COPYRIGHT", "FONT_ASCENT", "FONT_DESCENT") if k in props]
    out("STARTPROPERTIES %d\n" % len(keep))
    for k in keep:
        v = props[k]
        out('%s "%s"\n' % (k, v) if isinstance(v, str) else "%s %d\n" % (k, v))
    out("ENDPROPERTIES\n")
    glyphs = sorted(encodings.items())
    out("CHARS %d\n" % len(glyphs))
    for enc, gi in glyphs:
        lsb, rsb, width, ascent, descent = metrics[gi]
        w, h = rsb - lsb, ascent + descent
        out("STARTCHAR %03d\nENCODING %d\n" % (enc, enc))
        out("SWIDTH %d 0\nDWIDTH %d 0\n" % (width * 1000 // 8, width))
        out("BBX %d %d %d %d\nBITMAP\n" % (w, h, lsb, -descent))
        nb = (w + 7) // 8
        for row in bitmaps[gi]:
            out("%0*X\n" % (2 * nb, row))
        out("ENDCHAR\n")
    out("ENDFONT\n")


def read_properties(data, off):
    fmt, = struct.unpack_from("<I", data, off)
    o = fmt_order(fmt)
    n, = struct.unpack_from(o + "I", data, off + 4)
    p = off + 8
    items = []
    for i in range(n):
        name, isstr, val = struct.unpack_from(o + "IbI", data, p)
        items.append((name, isstr, val))
        p += 9
    p += (4 - (n & 3)) & 3                    # pad to a 4-byte boundary
    strsize, = struct.unpack_from(o + "I", data, p)
    strings = data[p + 4:p + 4 + strsize]

    def s(at):
        return strings[at:strings.index(b"\0", at)].decode("latin-1")

    return {s(name): (s(val) if isstr else val) for name, isstr, val in items}


def read_metrics(data, off):
    fmt, = struct.unpack_from("<I", data, off)
    o = fmt_order(fmt)
    out = []
    if fmt & COMPRESSED_METRICS:
        n, = struct.unpack_from(o + "H", data, off + 4)
        for i in range(n):
            b = data[off + 6 + 5 * i:off + 11 + 5 * i]
            out.append(tuple(x - 0x80 for x in b))
    else:
        n, = struct.unpack_from(o + "I", data, off + 4)
        for i in range(n):
            out.append(struct.unpack_from(o + "hhhhh", data, off + 8 + 12 * i))
    return out


def read_bitmaps(data, off, metrics):
    fmt, = struct.unpack_from("<I", data, off)
    o = fmt_order(fmt)
    if not fmt & (1 << 3):                    # PCF_BIT_MASK: MSB-first bits
        sys.exit("LSB-first bit order not supported")
    pad = 1 << (fmt & 3)                      # bytes each row is padded to
    n, = struct.unpack_from(o + "I", data, off + 4)
    offsets = struct.unpack_from(o + "%dI" % n, data, off + 8)
    base = off + 8 + 4 * n + 16               # skip the four bitmap sizes
    out = []
    for gi, (lsb, rsb, width, ascent, descent) in enumerate(metrics):
        w, h = rsb - lsb, ascent + descent
        stride = ((w + 7) // 8 + pad - 1) // pad * pad
        p = base + offsets[gi]
        rows = []
        for r in range(h):
            row = data[p + r * stride:p + r * stride + (w + 7) // 8]
            rows.append(int.from_bytes(row, "big") if row else 0)
        out.append(rows)
    return out


def read_encodings(data, off):
    fmt, = struct.unpack_from("<I", data, off)
    o = fmt_order(fmt)
    min2, max2, min1, max1, default = struct.unpack_from(o + "5H", data, off + 4)
    ncols = max2 - min2 + 1
    count = ncols * (max1 - min1 + 1)
    idx = struct.unpack_from(o + "%dH" % count, data, off + 14)
    enc = {}
    for i, gi in enumerate(idx):
        if gi != 0xFFFF:
            enc[(min1 + i // ncols) * 256 + min2 + i % ncols] = gi
    return enc


if __name__ == "__main__":
    main()
