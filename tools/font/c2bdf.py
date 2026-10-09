#!/usr/bin/env python3
"""
c2bdf.py - dump the GBA console font (sys/arch/gba/dev/gba_font.c) to a BDF
file, so it can be opened in a bitmap font editor (Fony, gbdfed, ...) as a
starting point for redesigning it narrower.

The font is 256 glyphs x 8 bytes; each byte is one 8-pixel row, bit 7 = the
leftmost pixel. Emitted as a fixed 8x8 BDF (cell 8 wide, 8 tall). Redraw the
glyphs 6 wide (5px glyph + 1px gap) and feed the result back through bdf2c.py.

Usage: c2bdf.py gba_font.c > font8x8.bdf
"""
import sys, re

def main():
    src = open(sys.argv[1]).read()
    bytes_ = [int(h, 16) for h in re.findall(r'0x([0-9A-Fa-f]{2})', src)]
    if len(bytes_) != 2048:
        sys.exit("expected 2048 font bytes, got %d" % len(bytes_))

    W = H = 8
    out = sys.stdout.write
    out("STARTFONT 2.1\n")
    out('FONT -gba-console-medium-r-normal--8-80-75-75-c-80-iso10646-1\n')
    out("SIZE 8 75 75\n")
    out("FONTBOUNDINGBOX %d %d 0 0\n" % (W, H))
    out("STARTPROPERTIES 2\n")
    out("FONT_ASCENT 8\nFONT_DESCENT 0\n")
    out("ENDPROPERTIES\n")
    out("CHARS 256\n")
    for c in range(256):
        rows = bytes_[c*8:c*8+8]
        out("STARTCHAR char%d\n" % c)
        out("ENCODING %d\n" % c)
        out("SWIDTH 1000 0\n")
        out("DWIDTH %d 0\n" % W)
        out("BBX %d %d 0 0\n" % (W, H))
        out("BITMAP\n")
        for r in rows:              # one hex byte per 8-pixel row, MSB-left
            out("%02X\n" % r)
        out("ENDCHAR\n")
    out("ENDFONT\n")

if __name__ == "__main__":
    main()
