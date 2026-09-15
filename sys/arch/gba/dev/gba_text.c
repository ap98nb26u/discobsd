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

/*
 * VT100-subset terminal emulation state (roadmap #6/vi). Full-screen apps
 * (vi) drive the console with ANSI/VT100 escape sequences; gtxt_putc()
 * feeds them through this little state machine (gtxt_esc_feed/gtxt_csi)
 * instead of printing them as glyphs. Only what vi emits is handled:
 * cursor addressing (ESC[r;cH), cursor up (ESC[A), clear-to-EOL (ESC[K),
 * clear-to-EOS (ESC[J) and standout on/off (ESC[7m / ESC[0m).
 */
static int gtxt_estate;			/* 0=text, 1=saw ESC, 2=in CSI */
#define GTXT_MAXPARM	4
static int gtxt_parm[GTXT_MAXPARM];	/* CSI numeric parameters */
static int gtxt_nparm;			/* index of the current parameter */
static int gtxt_reverse;		/* SGR reverse video: swap fg/bg */
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
 * Show (on != 0) or hide the console's block cursor at the current cell.
 * Idempotent, so idle()'s once-a-pass blink call is free unless the state
 * changes; the VRAM work runs only on an actual on<->off transition.
 *
 * The cursor is drawn by XOR-inverting the 8x8 cell rather than filling it,
 * so it is NON-DESTRUCTIVE: over a blank cell (the shell's input position)
 * it shows as a solid block, over a glyph (a full-screen app's cursor sitting
 * on live text) it shows as that glyph in reverse video, and toggling it off
 * XORs the same cell again to restore exactly what was underneath. This lets
 * vi's cursor be visible on the LCD without erasing the character beneath it.
 */
void
gtxt_cursor(int on)
{
    volatile unsigned short *p;
    int row, col, cx, cy;

    /*
     * A glyph typed in the last column holds a deferred wrap: gtxt_curx
     * == GTXT_COLS and the logical cursor sits at the START of the next
     * line. Draw it there - a free cell - so it stays visible, e.g. when
     * the line-edit cursor lands exactly on the right margin (moving one
     * step left/right otherwise being needed to make it reappear). Only
     * when that next row is on-screen; at the very bottom the wrap will
     * scroll on the next output, so leave the cursor hidden there rather
     * than mark a row that is about to move.
     */
    cx = gtxt_curx;
    cy = gtxt_cury;
    if (gtxt_curx >= GTXT_COLS) {
        if (gtxt_cury < GTXT_BOTTOM) {
            cx = 0;
            cy = gtxt_cury + 1;
        } else {
            on = 0;
            cx = GTXT_COLS - 1;
        }
    }
    if (on == gtxt_cursor_on)
        return;
    gtxt_cursor_on = on;

    p = VRAM + cy * GTXT_CHAR_H * GTXT_SCREEN_W + cx * GTXT_CHAR_W;
    for (row = 0; row < GTXT_CHAR_H; row++) {
        for (col = 0; col < GTXT_CHAR_W; col++)
            p[col] ^= 0x7FFF;		/* invert the 15 colour bits */
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
    /*
     * Permanently reserve the bottom 5 rows for the on-screen keyboard so
     * the console is a fixed 30x15 (roadmap: vi/terminal support, "Option
     * 2"). This matches the window size reported over TIOCGWINSZ, so a
     * full-screen app addresses exactly the visible grid whether or not the
     * keyboard happens to be drawn (swkbd.c paints/blanks that band but no
     * longer grows the console back to 20 rows when hidden).
     */
    gtxt_reserved = 5 * GTXT_CHAR_H;
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

/* Fill text cells [c0..c1] on row `row` with the background colour. */
static void
gtxt_clear_span(int row, int c0, int c1)
{
    int c;

    for (c = c0; c <= c1; c++)
        gtxt_draw_cell(' ', c, row, gtxt_bg, gtxt_bg);
}

/* Execute a CSI sequence whose final byte is `f` (params in gtxt_parm). */
static void
gtxt_csi(unsigned char f)
{
    int n = gtxt_parm[0] ? gtxt_parm[0] : 1;	/* default count/pos = 1 */
    int row;

    gtxt_cursor(0);		/* rub the block cursor before moving/clearing */

    switch (f) {
    case 'H':			/* CUP: ESC[row;colH, 1-based (default 1;1) */
    case 'f':
        gtxt_cury = (gtxt_parm[0] ? gtxt_parm[0] : 1) - 1;
        gtxt_curx = (gtxt_parm[1] ? gtxt_parm[1] : 1) - 1;
        if (gtxt_cury < 0) gtxt_cury = 0;
        if (gtxt_cury > GTXT_BOTTOM) gtxt_cury = GTXT_BOTTOM;
        if (gtxt_curx < 0) gtxt_curx = 0;
        if (gtxt_curx > GTXT_COLS - 1) gtxt_curx = GTXT_COLS - 1;
        break;
    case 'A':			/* CUU: cursor up n */
        gtxt_cury -= n;
        if (gtxt_cury < 0) gtxt_cury = 0;
        break;
    case 'B':			/* CUD: cursor down n */
        gtxt_cury += n;
        if (gtxt_cury > GTXT_BOTTOM) gtxt_cury = GTXT_BOTTOM;
        break;
    case 'C':			/* CUF: cursor right n */
        gtxt_curx += n;
        if (gtxt_curx > GTXT_COLS - 1) gtxt_curx = GTXT_COLS - 1;
        break;
    case 'D':			/* CUB: cursor left n */
        gtxt_curx -= n;
        if (gtxt_curx < 0) gtxt_curx = 0;
        break;
    case 'K':			/* EL: erase in line (0=to EOL,1=to BOL,2=all) */
        if (gtxt_parm[0] == 1)
            gtxt_clear_span(gtxt_cury, 0, gtxt_curx);
        else if (gtxt_parm[0] == 2)
            gtxt_clear_span(gtxt_cury, 0, GTXT_COLS - 1);
        else
            gtxt_clear_span(gtxt_cury, gtxt_curx, GTXT_COLS - 1);
        break;
    case 'J':			/* ED: erase in display (0=to end,1=to start,2=all) */
        if (gtxt_parm[0] == 2) {
            for (row = 0; row <= GTXT_BOTTOM; row++)
                gtxt_clear_span(row, 0, GTXT_COLS - 1);
        } else if (gtxt_parm[0] == 1) {
            for (row = 0; row < gtxt_cury; row++)
                gtxt_clear_span(row, 0, GTXT_COLS - 1);
            gtxt_clear_span(gtxt_cury, 0, gtxt_curx);
        } else {
            gtxt_clear_span(gtxt_cury, gtxt_curx, GTXT_COLS - 1);
            for (row = gtxt_cury + 1; row <= GTXT_BOTTOM; row++)
                gtxt_clear_span(row, 0, GTXT_COLS - 1);
        }
        break;
    case 'm':			/* SGR: 7 = reverse on, 0/none = reset */
        gtxt_reverse = (gtxt_parm[0] == 7);
        break;
    default:
        break;			/* silently ignore anything else */
    }
}

/* Feed one byte to the escape-sequence state machine. */
static void
gtxt_esc_feed(unsigned char c)
{
    if (gtxt_estate == 1) {			/* just saw ESC */
        if (c == '[') {
            gtxt_estate = 2;
            gtxt_nparm = 0;
            gtxt_parm[0] = gtxt_parm[1] = gtxt_parm[2] = gtxt_parm[3] = 0;
        } else {
            gtxt_estate = 0;			/* unsupported ESC x: drop it */
        }
        return;
    }
    /* gtxt_estate == 2: collecting a CSI sequence */
    if (c >= '0' && c <= '9') {
        if (gtxt_nparm < GTXT_MAXPARM)
            gtxt_parm[gtxt_nparm] = gtxt_parm[gtxt_nparm] * 10 + (c - '0');
        return;
    }
    if (c == ';') {
        if (gtxt_nparm < GTXT_MAXPARM - 1)
            gtxt_nparm++;
        return;
    }
    if (c >= '@' && c <= '~')			/* a final byte: run it */
        gtxt_csi(c);
    gtxt_estate = 0;				/* sequence ends (or was bad) */
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
     * VT100-subset escape handling: once ESC (0x1b) starts a sequence,
     * route the following bytes into the state machine instead of drawing
     * them, until the sequence completes (see gtxt_esc_feed/gtxt_csi).
     */
    if (gtxt_estate) {
        gtxt_esc_feed((unsigned char)c);
        return;
    }
    if (c == 0x1b) {
        gtxt_estate = 1;
        return;
    }

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
    /* Draw with reverse video (SGR ESC[7m) applied when active. */
    gtxt_draw_cell(c, gtxt_curx, gtxt_cury,
        gtxt_reverse ? gtxt_bg : gtxt_fg,
        gtxt_reverse ? gtxt_fg : gtxt_bg);
    gtxt_curx++;
}
