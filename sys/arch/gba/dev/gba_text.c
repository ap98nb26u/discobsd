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
#include <gba/dev/gba_sound.h>

extern const unsigned char gtxt_font[2048];

#define GTXT_SCREEN_W   240
#define GTXT_SCREEN_H   160
#define GTXT_CHAR_W     8
#define GTXT_CHAR_H     8
#define GTXT_COLS       (GTXT_SCREEN_W / GTXT_CHAR_W)
#define GTXT_ROW_PIXELS (GTXT_SCREEN_W * GTXT_CHAR_H)

static unsigned short gtxt_fg;
static unsigned short gtxt_bg;
static unsigned short gtxt_fg_normal;	/* console fg to restore after a msg */
static int gtxt_curx;		/* column 0..GTXT_COLS (COLS = deferred wrap) */
static int gtxt_cury;		/* row of the current line (absolute cell) */
static int gtxt_cursor_on;	/* is the block cursor currently lit? */

/*
 * Pixel height reserved at the bottom of the screen for an overlay (the
 * on-screen keyboard, swkbd.c). The scrolling console is confined to
 * the rows ABOVE this, so console output never scrolls the overlay away.
 * 0 = no overlay, console uses the whole screen as before.
 */
static int gtxt_reserved;

#define GTXT_CONS_H     (GTXT_SCREEN_H - gtxt_reserved)

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
    volatile unsigned short *end = VRAM + (GTXT_SCREEN_W * (GTXT_CONS_H - GTXT_CHAR_H));
    volatile unsigned short *tail = VRAM + (GTXT_SCREEN_W * GTXT_CONS_H);

    for (; p < end; p++)
        *p = p[GTXT_ROW_PIXELS];
    for (; p < tail; p++)
        *p = gtxt_bg;
}

/*
 * Scroll the WHOLE screen (overlay region included) up one character
 * row. Used only when the keyboard is shown and the input line would
 * otherwise be left underneath it - see gtxt_reserve_bottom().
 */
static void
gtxt_shift_full(void)
{
    volatile unsigned short *p = VRAM;
    volatile unsigned short *end = VRAM + (GTXT_SCREEN_W * (GTXT_SCREEN_H - GTXT_CHAR_H));
    volatile unsigned short *tail = VRAM + (GTXT_SCREEN_W * GTXT_SCREEN_H);

    for (; p < end; p++)
        *p = p[GTXT_ROW_PIXELS];
    for (; p < tail; p++)
        *p = gtxt_bg;
}

/* Lowest character row the console may write to (just above any overlay). */
#define GTXT_BOTTOM	((GTXT_CONS_H / GTXT_CHAR_H) - 1)

/* Advance to the next line, scrolling the console region when at the bottom. */
static void
gtxt_linefeed(void)
{
    if (gtxt_cury < GTXT_BOTTOM)
        gtxt_cury++;
    else
        gtxt_shift();
}

/*
 * Draw one character at text-cell (col,row) from the top-left, in the
 * given fg/bg (inverted for a highlighted key). Public so swkbd.c can
 * paint its keyboard without scrolling the console. row/col are in 8x8
 * cells; no bounds checking beyond the caller's.
 */
void
gtxt_draw_cell(char c, int col, int row, unsigned short fg, unsigned short bg)
{
    volatile unsigned short *p = VRAM + (row * GTXT_CHAR_H * GTXT_SCREEN_W)
        + (col * GTXT_CHAR_W);
    const unsigned char *glyph = &gtxt_font[(unsigned char)c * GTXT_CHAR_H];
    int r, col2;
    unsigned char bits;

    for (r = 0; r < GTXT_CHAR_H; r++) {
        bits = glyph[r];
        for (col2 = 0; col2 < GTXT_CHAR_W; col2++) {
            p[col2] = (bits & 0x80) ? fg : bg;
            bits <<= 1;
        }
        p += GTXT_SCREEN_W;
    }
}

/*
 * Reserve `pixels` at the bottom of the screen for an overlay, confining
 * the scrolling console above it. Called by swkbd.c when it shows/hides
 * (0 restores full-screen console).
 *
 * When SHRINKING the console (overlay appearing) the current input line
 * may end up below the new region - under the keyboard. Scroll the whole
 * screen up until it sits on the new bottom row, so it stays visible
 * above the overlay and the cursor tracks with it. When GROWING it again
 * (overlay hidden) nothing scrolls: the input line stays exactly where
 * it was rather than jumping back down to the screen's last row.
 */
void
gtxt_reserve_bottom(int pixels)
{
    if (pixels < 0)
        pixels = 0;
    if (pixels > GTXT_SCREEN_H - GTXT_CHAR_H)
        pixels = GTXT_SCREEN_H - GTXT_CHAR_H;
    gtxt_cursor(0);
    gtxt_reserved = pixels;
    while (gtxt_cury > GTXT_BOTTOM) {
        gtxt_shift_full();
        gtxt_cury--;
    }
}

int
gtxt_cols(void)
{
    return GTXT_COLS;
}

int
gtxt_rows(void)
{
    return GTXT_SCREEN_H / GTXT_CHAR_H;
}

/*
 * Show (on != 0) or hide the console's block cursor - a solid filled
 * square in the current cell at the bottom console row. Idempotent, so
 * idle()'s once-a-pass blink call is free unless the state changes; the
 * heavy VRAM fill runs only on an actual on<->off transition. Lit with
 * the foreground colour, erased back to the background (the input
 * position is always blank, so nothing under it is lost).
 */
void
gtxt_cursor(int on)
{
    volatile unsigned short *p;
    unsigned short color;
    int row, col, cx;

    /*
     * While a glyph typed in the last column waits for its deferred
     * wrap, gtxt_curx == GTXT_COLS and there is no free cell to mark.
     * Drawing the block over that glyph and later erasing it to the
     * background wiped the character (it vanished as the line scrolled).
     * Show no cursor until the next character resolves the wrap.
     */
    cx = gtxt_curx < GTXT_COLS ? gtxt_curx : GTXT_COLS - 1;
    if (gtxt_curx >= GTXT_COLS)
        on = 0;
    if (on == gtxt_cursor_on)
        return;
    gtxt_cursor_on = on;

    color = on ? gtxt_fg : gtxt_bg;
    p = VRAM + gtxt_cury * GTXT_CHAR_H * GTXT_SCREEN_W + cx * GTXT_CHAR_W;
    for (row = 0; row < GTXT_CHAR_H; row++) {
        for (col = 0; col < GTXT_CHAR_W; col++)
            p[col] = color;
        p += GTXT_SCREEN_W;
    }
}

void
gtxt_cls(void)
{
    volatile unsigned short *p = VRAM;
    volatile unsigned short *end = VRAM + (GTXT_SCREEN_W * GTXT_SCREEN_H);

    for (; p < end; p++)
        *p = gtxt_bg;
    gtxt_curx = 0;
    gtxt_cury = GTXT_BOTTOM;	/* start at the bottom, like a terminal */
    gtxt_cursor_on = 0;		/* cleared along with the rest of VRAM */
}

void
gtxt_init(unsigned short fg, unsigned short bg)
{
    REG_DISPCNT = 3 | (1 << 10);   /* Mode 3, enable BG2 */
    gtxt_fg = fg;
    gtxt_fg_normal = fg;
    gtxt_bg = bg;
    gtxt_cls();
}

/*
 * Kernel messages (the printf path) are drawn in green to set them apart
 * from ordinary console/program output, matching the raw-framebuffer
 * console convention (roadmap #8). uartputc_kmsg() (mgba_uart.c) - which
 * only the kernel's cnputc() reaches - brackets each character it emits
 * with gtxt_msgcolor(1)/gtxt_msgcolor(0); everything else stays the normal
 * console colour.
 */
void
gtxt_msgcolor(int on)
{
    gtxt_fg = on ? GTXT_GREEN : gtxt_fg_normal;
}

/*
 * Screen blanking (the console screen-saver, roadmap #6). "on" turns the
 * LCD image off by disabling BG2: the display then shows nothing but the
 * backdrop colour (palette entry 0), which we force to black. VRAM is left
 * untouched, so the text reappears intact the instant BG2 is re-enabled.
 *
 * This is a plain register write - deliberately NOT a BIOS power-down SWI
 * (Halt/Stop). The BIOS reset/Halt SWIs behave inconsistently under mGBA
 * (the undocumented HardReset SWI used by cpu_reboot() hangs the emulator
 * inside the BIOS), so blanking stays SWI-free to work identically on real
 * hardware and in the emulator. The original GBA's backlight is not
 * software-controllable, so this darkens the image and stops BG/VRAM
 * fetches rather than cutting the backlight; it prevents a static image
 * from sitting on the panel and is the console-blanking behaviour other
 * systems expose (blank on idle, restore on activity).
 */
static int gtxt_blanked;

void
gtxt_blank(int on)
{
    if (on == gtxt_blanked)
        return;
    gtxt_blanked = on;
    if (on) {
        *(volatile unsigned short *)0x5000000 = 0x0000; /* backdrop -> black */
        REG_DISPCNT = 3;              /* Mode 3, BG2 OFF -> backdrop only */
    } else {
        REG_DISPCNT = 3 | (1 << 10);  /* Mode 3, BG2 ON  -> text visible */
    }
}

int
gtxt_is_blanked(void)
{
    return gtxt_blanked;
}

void
gtxt_putc(char c)
{
    /*
     * Console output is activity: wake the screen if it was blanked and
     * restart the idle-blank countdown, so kernel/program output is never
     * printed onto a dark screen (see console_activity() in machdep.c).
     */
    extern void console_activity(void);
    console_activity();

    /*
     * Rub out the block cursor (if lit) before drawing anything, so it
     * neither gets scrolled up as a stray block nor is left behind at
     * the old column. The idle() blink relights it afterward.
     */
    gtxt_cursor(0);

    if (c == '\a') {
        /*
         * BEL is audible-only on a real terminal: sound the console beep
         * (roadmap #4) and draw no glyph. The cooked tty rings the bell
         * (CTRL-G) on input errors and for each keystroke once the input
         * line hits TTYHOG (255 bytes); without swallowing it here those
         * printed as glyphs and marched the cursor off the screen.
         */
        gba_beep();
        return;
    }
    if (c == 0x7f) {
        /*
         * DEL is a non-printing rubout - draw nothing. (It should not
         * normally reach here now that Backspace sends ^H, but guard.)
         */
        return;
    }
    if (c == '\r') {
        gtxt_curx = 0;
        return;
    }
    if (c == '\n') {
        gtxt_curx = 0;
        gtxt_linefeed();
        return;
    }
    if (c == '\b') {
        /*
         * The cooked tty erases a character by sending "\b \b" (back,
         * overwrite with a space, back again); handle '\b' so that
         * shows on the LCD instead of printing a glyph. At the left
         * edge, step up to the end of the previous line so an erase
         * can cross a line the input had wrapped onto (the tty's own
         * erase logic bounds how far back it ever sends).
         */
        if (gtxt_curx > 0)
            gtxt_curx--;
        else if (gtxt_cury > 0) {
            gtxt_curx = GTXT_COLS - 1;
            gtxt_cury--;
        }
        return;
    }

    /*
     * Deferred wrap: a glyph drawn in the last column leaves the column
     * at GTXT_COLS rather than immediately moving to the next line, so
     * the following "\b \b" erase can step straight back onto it instead
     * of first spuriously scrolling. The wrap happens here, when the
     * next printable character actually needs the new line.
     */
    if (gtxt_curx >= GTXT_COLS) {
        gtxt_curx = 0;
        gtxt_linefeed();
    }
    gtxt_blit((unsigned char)c, gtxt_curx * GTXT_CHAR_W,
              gtxt_cury * GTXT_CHAR_H);
    gtxt_curx++;
}
