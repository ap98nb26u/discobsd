/*
 * On-screen software keyboard for the GBA.
 *
 * Real GBA hardware (and mGBA) has no keyboard - only the 10 console
 * buttons. This driver draws a QWERTY keyboard along the bottom of the
 * screen and lets the D-pad move a cursor over it, turning button
 * presses into characters fed to the console tty exactly as if they had
 * arrived over the (often absent) serial line. Modelled on the Prex GBA
 * port's on-screen keyboard (3-clause BSD).
 *
 * Controls:
 *   SELECT      show / hide the keyboard (console output is confined to
 *               the rows above it while shown)
 *   D-pad       move the cursor over the keys
 *   A           send the highlighted key
 *   B           send Enter (a shortcut for the En key)
 *   L / R       toggle shift (upper case / the alternate symbols)
 *
 * swkbd_poll() is called from idle() (machdep.c), the same place the
 * serial console is polled, so both input paths coexist.
 */
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/config.h>

#include <machine/gba.h>
#include <gba/dev/gba_text.h>

#ifndef CONS_MINOR
#define CONS_MINOR	0
#endif

extern struct tty uartttys[];

/*
 * One key. For an ordinary key `label` is 0 and the glyph drawn is the
 * character itself (`n` normally, `s` when shifted), which is also what
 * is sent. For a special key `label` is a 2-char string drawn in the
 * key's two cells and `n` (== `s`) is the control character sent.
 */
struct swkey {
	char		n;	/* character sent / drawn, unshifted */
	char		s;	/* character sent / drawn, shifted */
	const char     *label;	/* 2 chars for a special key, else 0 */
};

/* Every key is drawn KEY_CELLS text cells wide, so key i sits at
 * screen column i*KEY_CELLS - trivial cursor math. */
#define KEY_CELLS	2

static const struct swkey row0[] = {
	{'`','~',0},{'1','!',0},{'2','@',0},{'3','#',0},{'4','$',0},
	{'5','%',0},{'6','^',0},{'7','&',0},{'8','*',0},{'9','(',0},
	{'0',')',0},{'-','_',0},{'=','+',0},
};
static const struct swkey row1[] = {
	{'q','Q',0},{'w','W',0},{'e','E',0},{'r','R',0},{'t','T',0},
	{'y','Y',0},{'u','U',0},{'i','I',0},{'o','O',0},{'p','P',0},
	{'[','{',0},{']','}',0},{'\\','|',0},
};
static const struct swkey row2[] = {
	{'a','A',0},{'s','S',0},{'d','D',0},{'f','F',0},{'g','G',0},
	{'h','H',0},{'j','J',0},{'k','K',0},{'l','L',0},{';',':',0},
	{'\'','"',0},
};
static const struct swkey row3[] = {
	{'z','Z',0},{'x','X',0},{'c','C',0},{'v','V',0},{'b','B',0},
	{'n','N',0},{'m','M',0},{',','<',0},{'.','>',0},{'/','?',0},
};
/*
 * Special keys emit what a real serial terminal sends, so the tty line
 * discipline treats them identically to serial input: Enter is CR (the
 * tty maps CR->NL via ICRNL and echoes the newline; sending a bare LF
 * runs the command but skips that echo, so output ran onto the command
 * line). Backspace is ^H (0x08), NOT DEL: the tty always treats ^H as
 * an erase (tty.c: CCEQ(CTRL('h'), c)), whatever t_erase is set to, so
 * it erases at the login prompt as well as the shell; DEL only erased
 * once .profile's "stty dec" had set erase=^?, and before that printed
 * as a literal 0x7f glyph on the LCD.
 */
static const struct swkey row4[] = {
	{'\t','\t',"Tb"},{' ',' ',"Sp"},{'\b','\b',"Bs"},
	{'\r','\r',"En"},{0x1b,0x1b,"Es"},
};

static const struct swkey *const rows[] = { row0, row1, row2, row3, row4 };
static const unsigned char rowlen[] = {
	sizeof(row0)/sizeof(row0[0]), sizeof(row1)/sizeof(row1[0]),
	sizeof(row2)/sizeof(row2[0]), sizeof(row3)/sizeof(row3[0]),
	sizeof(row4)/sizeof(row4[0]),
};
#define NROWS		(sizeof(rows)/sizeof(rows[0]))

#define KB_FG		0xFFFF		/* white  (BGR555) */
#define KB_BG		0x0000		/* black */

static int	swkbd_shown;
static int	swkbd_shift;
static int	swkbd_cx;		/* cursor column (key index in row) */
static int	swkbd_cy;		/* cursor row */
static uint16_t	swkbd_prev;		/* previously-pressed key bitmask */

/* Draw one key; highlighted (cursor) keys are drawn inverted. */
static void
swkbd_drawkey(int row, int col, int hilite)
{
	const struct swkey *k = &rows[row][col];
	int scol = col * KEY_CELLS;
	int srow = (gtxt_rows() - NROWS) + row;
	unsigned short fg = hilite ? KB_BG : KB_FG;
	unsigned short bg = hilite ? KB_FG : KB_BG;
	char c0, c1;

	if (k->label) {
		c0 = k->label[0];
		c1 = k->label[1];
	} else {
		c0 = swkbd_shift ? k->s : k->n;
		c1 = ' ';
	}
	gtxt_draw_cell(c0, scol, srow, fg, bg);
	gtxt_draw_cell(c1, scol + 1, srow, fg, bg);
}

static void
swkbd_draw(void)
{
	unsigned int r, c;

	for (r = 0; r < NROWS; r++)
		for (c = 0; c < rowlen[r]; c++)
			swkbd_drawkey(r, c, (int)r == swkbd_cy &&
			    (int)c == swkbd_cx);
}

static void
swkbd_show(int on)
{
	if (on == swkbd_shown)
		return;
	swkbd_shown = on;
	if (on) {
		gtxt_reserve_bottom(NROWS * 8);
		swkbd_draw();
	} else {
		/* Blank the keyboard band and give the rows back. */
		int r, c, srow0 = gtxt_rows() - NROWS;
		for (r = 0; r < (int)NROWS; r++)
			for (c = 0; c < gtxt_cols(); c++)
				gtxt_draw_cell(' ', c, srow0 + r, KB_FG, KB_BG);
		gtxt_reserve_bottom(0);
	}
}

/* Send one character to the console tty, as if typed on the serial line. */
static void
swkbd_send(char c)
{
	ttyinput(c, &uartttys[CONS_MINOR]);
}

static void
swkbd_press(void)
{
	const struct swkey *k = &rows[swkbd_cy][swkbd_cx];

	swkbd_send(swkbd_shift ? k->s : k->n);
}

static void
swkbd_move(int dx, int dy)
{
	int old_cx = swkbd_cx, old_cy = swkbd_cy;

	swkbd_cy += dy;
	if (swkbd_cy < 0)
		swkbd_cy = 0;
	if (swkbd_cy >= (int)NROWS)
		swkbd_cy = NROWS - 1;
	swkbd_cx += dx;
	if (swkbd_cx < 0)
		swkbd_cx = 0;
	if (swkbd_cx >= rowlen[swkbd_cy])
		swkbd_cx = rowlen[swkbd_cy] - 1;

	if (swkbd_cx != old_cx || swkbd_cy != old_cy) {
		swkbd_drawkey(old_cy, old_cx, 0);
		swkbd_drawkey(swkbd_cy, swkbd_cx, 1);
	}
}

/*
 * Auto-repeat (frames, at the ~60Hz sample rate below): hold a movement
 * key or A for SWKBD_REPEAT_DELAY (~1s) and it then fires every
 * SWKBD_REPEAT_RATE frames (~0.25s). SELECT, shift and Enter never
 * repeat.
 */
#define SWKBD_REPEAT_DELAY	60
#define SWKBD_REPEAT_RATE	15
#define SWKBD_REPEAT_KEYS	(KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A)

/*
 * Poll the keypad once. Called in a tight loop from idle(); acts on
 * fresh presses (edge-triggered) plus auto-repeats of a held key.
 */
void
swkbd_poll(void)
{
	static uint16_t last_vc;
	static uint16_t held_prev;	/* repeatable keys held last frame */
	static int hold_ctr;
	uint16_t vc, raw, down, fresh, rep, act;

	/*
	 * idle() calls this thousands of times a second - far faster than
	 * a button's few-ms contact bounce settles, so edge-detecting at
	 * that rate fires many times per physical press (bad chattering on
	 * real hardware). Sample the keypad at most once per video frame
	 * (~60Hz) instead, exactly as GBA games debounce input: VCOUNT
	 * sweeps 0..227 each frame, so a reading smaller than the last
	 * marks a new frame - proceed only then, and otherwise return.
	 */
	vc = REG_VCOUNT;
	if (vc >= last_vc) {
		last_vc = vc;
		return;
	}
	last_vc = vc;

	raw = REG_KEYINPUT;			/* 0 bit = pressed */
	down = (uint16_t)(~raw & 0x03FF);
	fresh = (uint16_t)(down & ~swkbd_prev);
	swkbd_prev = down;

	/*
	 * Auto-repeat: while exactly the same repeatable key(s) stay held,
	 * count frames; past the initial delay, fire once every RATE frames.
	 */
	rep = 0;
	{
		uint16_t r = (uint16_t)(down & SWKBD_REPEAT_KEYS);
		if (r != 0 && r == held_prev) {
			hold_ctr++;
			if (hold_ctr >= SWKBD_REPEAT_DELAY &&
			    (hold_ctr - SWKBD_REPEAT_DELAY) % SWKBD_REPEAT_RATE == 0)
				rep = r;
		} else
			hold_ctr = 0;
		held_prev = r;
	}
	act = (uint16_t)(fresh | rep);		/* fresh presses + repeats */

	if (fresh & KEY_SELECT) {
		swkbd_show(!swkbd_shown);
		return;
	}
	if (!swkbd_shown)
		return;

	if (fresh & (KEY_L | KEY_R)) {
		swkbd_shift = !swkbd_shift;
		swkbd_draw();			/* relabel every key */
	}
	if (act & KEY_UP)
		swkbd_move(0, -1);
	if (act & KEY_DOWN)
		swkbd_move(0, 1);
	if (act & KEY_LEFT)
		swkbd_move(-1, 0);
	if (act & KEY_RIGHT)
		swkbd_move(1, 0);
	if (act & KEY_A)
		swkbd_press();
	if (fresh & KEY_B)
		swkbd_send('\r');		/* Enter (see row4 comment) */
}
