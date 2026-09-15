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
 *   D-pad       keyboard shown: move the cursor over the keys;
 *               keyboard hidden: send VT100 arrow-key escape sequences
 *               (ESC[A/B/C/D) so vi and other full-screen apps get real
 *               cursor-key input
 *   A           send the highlighted key
 *   B           send Enter (a shortcut for the En key)
 *   L           toggle shift (upper case / the alternate symbols); sticky
 *   R           toggle control; one-shot - the next key sends its ^char
 *               (e.g. c -> ^C, d -> ^D). The Sh and Ct keys on the
 *               bottom row do the same and light up while active.
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
/* kind values: an ordinary character, a sticky modifier toggle, or Del. */
#define K_CHAR	0
#define K_SHIFT	1
#define K_CTRL	2
#define K_DEL	3	/* forward delete: sends ESC[3~ (see swkbd_press) */

struct swkey {
	char		n;	/* character sent / drawn, unshifted */
	char		s;	/* character sent / drawn, shifted */
	const char     *label;	/* 2 chars for a special key, else 0 */
	unsigned char	kind;	/* K_CHAR / K_SHIFT / K_CTRL (0 by default) */
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
	{'\t','\t',"Tb"},{' ',' ',"Sp"},{'\b','\b',"Bs"},{0,0,"De",K_DEL},
	{'\r','\r',"En"},{0x1b,0x1b,"Es"},
	{0,0,"Sh",K_SHIFT},{0,0,"Ct",K_CTRL},
};

static const struct swkey *const rows[] = { row0, row1, row2, row3, row4 };
static const unsigned char rowlen[] = {
	sizeof(row0)/sizeof(row0[0]), sizeof(row1)/sizeof(row1[0]),
	sizeof(row2)/sizeof(row2[0]), sizeof(row3)/sizeof(row3[0]),
	sizeof(row4)/sizeof(row4[0]),
};
#define NROWS		(sizeof(rows)/sizeof(rows[0]))

#define KB_FG		GTXT_WHITE	/* keyboard: white on black */
#define KB_BG		GTXT_BLACK

static int	swkbd_shown;
static int	swkbd_shift;		/* sticky: upper case / alternate symbol */
static int	swkbd_ctrl;		/* one-shot: next key sends its ^char */
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
	unsigned short fg, bg;
	char c0, c1;

	/* A modifier key stays lit while its mode is active, so its state
	 * is visible even when the cursor is elsewhere. */
	if ((k->kind == K_SHIFT && swkbd_shift) ||
	    (k->kind == K_CTRL && swkbd_ctrl))
		hilite = 1;
	fg = hilite ? KB_BG : KB_FG;
	bg = hilite ? KB_FG : KB_BG;

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
		swkbd_draw();
	} else {
		/*
		 * Blank the keyboard band, but keep the rows reserved: the
		 * console stays a fixed 30x15 (pinned in gtxt_init) so its size
		 * always matches the TIOCGWINSZ the kernel reports, which a
		 * full-screen app (vi) relies on. The bottom band is simply left
		 * blank while the keyboard is hidden.
		 */
		int r, c, srow0 = gtxt_rows() - NROWS;
		for (r = 0; r < (int)NROWS; r++)
			for (c = 0; c < gtxt_cols(); c++)
				gtxt_draw_cell(' ', c, srow0 + r, KB_FG, KB_BG);
	}
}

/* Send one character to the console tty, as if typed on the serial line. */
static void
swkbd_send(char c)
{
	ttyinput(c, &uartttys[CONS_MINOR]);
}

/*
 * Send a VT100 cursor-key escape sequence: ESC [ <final>, where final is
 * 'A'=up, 'B'=down, 'C'=right, 'D'=left. Used when the keyboard is hidden
 * so the D-pad acts as arrow keys (vi's readit() maps ESC[A..D to its
 * cursor keys). All three bytes are queued together, so the tty delivers
 * them as one sequence exactly as a real terminal's arrow key would.
 */
static void
swkbd_send_esc(char final)
{
	swkbd_send(0x1b);
	swkbd_send('[');
	swkbd_send(final);
}

static void
swkbd_press(void)
{
	const struct swkey *k = &rows[swkbd_cy][swkbd_cx];
	char c;

	if (k->kind == K_SHIFT) {
		swkbd_shift = !swkbd_shift;
		swkbd_draw();
		return;
	}
	if (k->kind == K_CTRL) {
		swkbd_ctrl = !swkbd_ctrl;
		swkbd_draw();
		return;
	}
	if (k->kind == K_DEL) {
		/* Forward delete: the standard Del sequence, which the tty line
		 * editor turns into "delete the character at the cursor". */
		swkbd_send(0x1b);
		swkbd_send('[');
		swkbd_send('3');
		swkbd_send('~');
		return;
	}

	c = swkbd_shift ? k->s : k->n;
	if (swkbd_ctrl) {
		c = (char)(c & 0x1f);	/* e.g. c->^C (0x03), d->^D (0x04) */
		swkbd_ctrl = 0;		/* one-shot: applies to this key only */
		swkbd_draw();
	}
	swkbd_send(c);
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
 * Auto-repeat: hold a movement key or A for SWKBD_REPEAT_DELAY and it
 * then fires every SWKBD_REPEAT_RATE. SELECT, shift and Enter never
 * repeat.
 *
 * Timed off myticks (the ~100Hz Timer0 count), NOT the number of frames
 * this function actually processes. Both matter now that swkbd_poll() is
 * driven from the Timer0 interrupt as well as idle(): while a command's
 * output scrolls, each write() runs with interrupts masked (IME=0) so
 * the interrupt - and thus this poll's once-per-frame sampling - is
 * starved, and a frame-counted repeat would take many real seconds to
 * reach its delay (looked like "repeat doesn't work while scrolling").
 * myticks advances in real time regardless of how sparsely we sample, so
 * a held key repeats at the right wall-clock cadence even mid-scroll.
 */
/*
 * Auto-repeat timing is runtime-tunable via the machdep sysctls
 * kbd_repeat_delay / kbd_repeat_rate (milliseconds), and persisted per build
 * (cart SRAM on mrams, /etc/rc.local on the SD-root GBAED build; see sram.c). The ms values are the canonical setting; swkbd_set_repeat()
 * clamps them and derives the Timer1 (16384Hz) tick counts actually compared
 * against below. Defaults ~0.4s delay / ~0.1s interval.
 */
#define SWKBD_DEF_DELAY_MS	400
#define SWKBD_DEF_RATE_MS	100
#define SWKBD_MIN_DELAY_MS	50
#define SWKBD_MAX_DELAY_MS	2000
#define SWKBD_MIN_RATE_MS	20
#define SWKBD_MAX_RATE_MS	1000

int swkbd_repeat_delay_ms = SWKBD_DEF_DELAY_MS;	/* sysctl/SRAM-visible (ms) */
int swkbd_repeat_rate_ms  = SWKBD_DEF_RATE_MS;
static int swkbd_delay_ticks = (SWKBD_DEF_DELAY_MS * 16384) / 1000;
static int swkbd_rate_ticks  = (SWKBD_DEF_RATE_MS  * 16384) / 1000;

void
swkbd_set_repeat(int delay_ms, int rate_ms)
{
	if (delay_ms < SWKBD_MIN_DELAY_MS)	delay_ms = SWKBD_MIN_DELAY_MS;
	if (delay_ms > SWKBD_MAX_DELAY_MS)	delay_ms = SWKBD_MAX_DELAY_MS;
	if (rate_ms  < SWKBD_MIN_RATE_MS)	rate_ms  = SWKBD_MIN_RATE_MS;
	if (rate_ms  > SWKBD_MAX_RATE_MS)	rate_ms  = SWKBD_MAX_RATE_MS;
	swkbd_repeat_delay_ms = delay_ms;
	swkbd_repeat_rate_ms  = rate_ms;
	swkbd_delay_ticks = (int)(((long)delay_ms * 16384) / 1000);
	swkbd_rate_ticks  = (int)(((long)rate_ms  * 16384) / 1000);
}

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
	static uint16_t held_since;	/* Timer1 value when this held run began */
	static uint16_t last_rep;	/* Timer1 value at the last repeat fire */
	static int repeating;		/* past the initial delay for held_prev */
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
	 * Any button held is console activity: keep the screen awake (and
	 * wake it if the screen-saver had blanked it). Waking is a side
	 * effect - the press is still processed normally below.
	 */
	if (down) {
		extern void console_activity(void);
		console_activity();
	}

	/*
	 * Auto-repeat: while exactly the same repeatable key(s) stay held,
	 * measure elapsed time with the free-running Timer1 (REG_TM1CNT_L,
	 * 16384Hz, IME-independent - see irq_enable() in machdep.c). Wait
	 * SWKBD_REPEAT_DELAY for the first repeat, then fire every
	 * SWKBD_REPEAT_RATE. Using real hardware time rather than a count of
	 * processed frames keeps the cadence correct even while a command's
	 * scrolling output starves this poll (the interrupt-driven clocks
	 * stall under IME=0, but Timer1 does not). All deltas are sub-second,
	 * far under Timer1's 4s 16-bit wrap, so 16-bit unsigned math is fine;
	 * the delay is checked only until `repeating` latches, so an
	 * indefinitely-held key never trips the wrap.
	 */
	rep = 0;
	{
		uint16_t r = (uint16_t)(down & SWKBD_REPEAT_KEYS);
		uint16_t t = REG_TM1CNT_L;

		if (r != 0 && r == held_prev) {
			if (! repeating) {
				if ((uint16_t)(t - held_since) >= swkbd_delay_ticks) {
					repeating = 1;
					rep = r;
					last_rep = t;
				}
			} else if ((uint16_t)(t - last_rep) >= swkbd_rate_ticks) {
				rep = r;
				last_rep = t;
			}
		} else {
			held_since = t;
			repeating = 0;
		}
		held_prev = r;
	}
	act = (uint16_t)(fresh | rep);		/* fresh presses + repeats */

	if (fresh & KEY_SELECT) {
		swkbd_show(!swkbd_shown);
		return;
	}
	if (!swkbd_shown) {
		/*
		 * Keyboard hidden: the D-pad sends cursor-key escape sequences
		 * (arrow keys) instead of moving the on-screen key selection, so
		 * vi and other full-screen apps get real arrow input. Uses `act`
		 * (fresh presses + auto-repeat) so a held direction repeats,
		 * exactly as the selection move does when the keyboard is shown.
		 *
		 * Holding R (the Ctrl shoulder) turns Left/Right into Home/End
		 * (ESC[H / ESC[F), extending cursor movement to the whole-line
		 * jumps the tty line editor understands - Ctrl+arrow, in effect.
		 */
		int he = (down & KEY_R);
		if (act & KEY_UP)
			swkbd_send_esc('A');
		if (act & KEY_DOWN)
			swkbd_send_esc('B');
		if (act & KEY_RIGHT)
			swkbd_send_esc(he ? 'F' : 'C');		/* End : right */
		if (act & KEY_LEFT)
			swkbd_send_esc(he ? 'H' : 'D');		/* Home : left */
		return;
	}

	if (fresh & KEY_L) {			/* L shoulder: shift */
		swkbd_shift = !swkbd_shift;
		swkbd_draw();			/* relabel every key */
	}
	if (fresh & KEY_R) {			/* R shoulder: control */
		swkbd_ctrl = !swkbd_ctrl;
		swkbd_draw();
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
