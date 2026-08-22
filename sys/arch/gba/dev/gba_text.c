/*
 * Minimal on-screen text console for the GBA, using video Mode 3
 * (240x160 16bpp bitmap) and an 8x8 pixel bitmap font (gba_font.c).
 * It began from Adrian O'Grady's fivemouse.com GBA text demo
 * (http://www.fivemouse.com/gba/), but none of that code remains.
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

/*
 * Fill or copy a run of pixels 32 bits (two pixels) at a time; the
 * runs used here start on 4-byte boundaries and are an even number
 * of pixels long.
 */
static void
gtxt_fill32(volatile unsigned short *p, unsigned short c, int npix)
{
    volatile unsigned long *w = (volatile unsigned long *)p;
    unsigned long v = (unsigned long)c << 16 | c;

    for (npix /= 2; npix > 0; npix--)
        *w++ = v;
}

static void
gtxt_copy32(volatile unsigned short *dst, volatile unsigned short *src,
    int npix)
{
    volatile unsigned long *d = (volatile unsigned long *)dst;
    volatile unsigned long *s = (volatile unsigned long *)src;

    for (npix /= 2; npix > 0; npix--)
        *d++ = *s++;
}

static void
gtxt_blit(unsigned char c, int x, int y)
{
    volatile unsigned short *p = VRAM + (y * GTXT_SCREEN_W) + x;
    const unsigned char *glyph = &gtxt_font[c * GTXT_CHAR_H];
    int row, col;
    unsigned char bits;

    for (row = 0; row < GTXT_CHAR_H; row++) {
        bits = glyph[row];
        for (col = 0; col < GTXT_CHAR_W; col++)
            p[col] = (bits & (0x80 >> col)) ? gtxt_fg : gtxt_bg;
        p += GTXT_SCREEN_W;
    }
}

static void
gtxt_shift(void)
{
    volatile unsigned short *p = VRAM;
    volatile unsigned short *end = VRAM + (GTXT_SCREEN_W * (GTXT_SCREEN_H - GTXT_CHAR_H));
    volatile unsigned short *tail = VRAM + (GTXT_SCREEN_W * GTXT_SCREEN_H);

    gtxt_copy32(p, p + GTXT_ROW_PIXELS, end - p);
    gtxt_fill32(end, gtxt_bg, tail - end);
}

void
gtxt_cls(void)
{
    volatile unsigned short *p = VRAM;
    volatile unsigned short *end = VRAM + (GTXT_SCREEN_W * GTXT_SCREEN_H);

    gtxt_fill32(p, gtxt_bg, end - p);
    gtxt_curx = 0;
}

void
gtxt_init(unsigned short fg, unsigned short bg)
{
    REG_DISPCNT = 0x0403;   /* Mode 3, enable BG2 */
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
