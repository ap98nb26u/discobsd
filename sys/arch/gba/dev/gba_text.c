/*
 * Minimal on-screen text console for the GBA, using video Mode 3
 * (240x160 16bpp bitmap) and an 8x8 pixel bitmap font (gba_font.c).
 * Ported from Adrian O'Grady's fivemouse.com GBA text demo/library
 * (http://www.fivemouse.com/gba/).
 *
 * Behaves like a scrolling terminal: characters are drawn on the
 * bottom text row; the whole screen is shifted up one character row
 * on '\n' or when the row fills up. Intended as a real-hardware
 * substitute for the mGBA-only debug log console (mgba_uart.c), since
 * real GBA hardware has nowhere else visible to show kernel output
 * until UART is wired up.
 */
#include <sys/param.h>

#include <machine/gba.h>
#include <gba/dev/gba_text.h>

extern const unsigned char gtxt_font[2048];

#define GTXT_SCREEN_W   240
#define GTXT_SCREEN_H   160
#define GTXT_CHAR_W     8
#define GTXT_CHAR_H     8
#define GTXT_COLS       (GTXT_SCREEN_W / GTXT_CHAR_W)
#define GTXT_ROW_PIXELS (GTXT_SCREEN_W * GTXT_CHAR_H)

static unsigned short gtxt_fg;
static unsigned short gtxt_bg;
static int gtxt_curx;

static void
gtxt_blit(unsigned char c, int x, int y)
{
    volatile unsigned short *p = VRAM + (y * GTXT_SCREEN_W) + x;
    const unsigned char *glyph = &gtxt_font[c * GTXT_CHAR_H];
    int row, col;
    unsigned char bits;

    for (row = 0; row < GTXT_CHAR_H; row++) {
        bits = glyph[row];
        for (col = 0; col < GTXT_CHAR_W; col++) {
            p[col] = (bits & 0x80) ? gtxt_fg : gtxt_bg;
            bits <<= 1;
        }
        p += GTXT_SCREEN_W;
    }
}

static void
gtxt_shift(void)
{
    volatile unsigned short *p = VRAM;
    volatile unsigned short *end = VRAM + (GTXT_SCREEN_W * (GTXT_SCREEN_H - GTXT_CHAR_H));
    volatile unsigned short *tail = VRAM + (GTXT_SCREEN_W * GTXT_SCREEN_H);

    for (; p < end; p++)
        *p = p[GTXT_ROW_PIXELS];
    for (; p < tail; p++)
        *p = gtxt_bg;
}

void
gtxt_cls(void)
{
    volatile unsigned short *p = VRAM;
    volatile unsigned short *end = VRAM + (GTXT_SCREEN_W * GTXT_SCREEN_H);

    for (; p < end; p++)
        *p = gtxt_bg;
    gtxt_curx = 0;
}

void
gtxt_init(unsigned short fg, unsigned short bg)
{
    REG_DISPCNT = 3 | (1 << 10);   /* Mode 3, enable BG2 */
    gtxt_fg = fg;
    gtxt_bg = bg;
    gtxt_cls();
}

void
gtxt_putc(char c)
{
    if (c == '\n') {
        gtxt_shift();
        gtxt_curx = 0;
        return;
    }
    if (c == '\r')
        return;

    gtxt_blit((unsigned char)c, gtxt_curx * GTXT_CHAR_W,
              GTXT_SCREEN_H - GTXT_CHAR_H);
    gtxt_curx++;
    if (gtxt_curx == GTXT_COLS) {
        gtxt_shift();
        gtxt_curx = 0;
    }
}
