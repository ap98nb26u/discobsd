/*
 * mml - play Music Macro Language on the GBA's four PSG channels.
 *
 * Usage: mml [-o file] [-l loops] [file]
 *
 * The MML follows the dialect of mmlgb, Simon Larsen's MML compiler for
 * the Game Boy (https://github.com/SimonLarsen/mmlgb): each line starts
 * with the channels it is for, A and B (square), C (wave) and D (noise),
 * and lines for the same channel continue each other.  ; starts a
 * comment.
 *
 *	c d e f g a b	note, + or # sharp, - flat, then a length:
 *			1 whole, 4 quarter ... 64, dots allowed (c4.)
 *	r  w		rest; wait (hold the sound)
 *	^n  &n		tie: c4^8 lasts a quarter and an eighth
 *	&		between notes (c4&d4): change pitch without
 *			restarting the note (slur)
 *	o n  < >	octave 1..8 (default 4, o4a = 440 Hz); down, up
 *	l n		default length
 *	t n		tempo, quarter notes a minute (default 120); like
 *			every command, only for the line's channels, so
 *			set it for all of them: ABCD t120
 *	v n		volume 0..15 (channel C: 0..3 = off, 25, 50, 100%)
 *	@ve n		envelope: -7..-1 fade out (1 fastest), 1..7 fade
 *			in, 0 none (A, B, D)
 *	@wd n		duty 0..3 = 12.5, 25, 50, 75% (A, B)
 *	@sw t,s		sweep: time 0..7, shift -7..7 (A)
 *	@wave n		wave table n (C); define it with
 *			@wave n = { 32 numbers 0..15 }
 *	@ns n		noise counter 0 = 15 bits, 1 = 7 bits (D); the
 *			noise "pitch" is set by the note: the octave picks
 *			the divider, the note the shift, as in mmlgb
 *	y n		pan: -1 left, 0 both, 1 right
 *	[ ... ]n	repeat n times (default 2)
 *	L		loop back here when the channel ends
 *	@@n		insert macro n; define it with @@n = { ... }
 *
 * mml reads the whole file, turns each channel into a list of
 * operations, then runs the four lists side by side, writing the PSG
 * register writes with the waits between them to /dev/psg, which plays
 * them from the clock interrupt.  Running the lists, rather than
 * converting the song ahead, lets a channel loop back to its L for ever.
 *
 * With -o, the same stream goes to a file instead, to be played later
 * with `cat file > /dev/psg'; there an L section repeats only the number
 * of times -l says (default 0).  ^C stops the music.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>

/* /dev/psg's ioctls (sys/arch/gba/dev/gba_psg.h) */
#define PSGFLUSH	_IO('P', 1)
#define PSGDRAIN	_IO('P', 2)

/* /dev/psg stream words */
#define W_WAIT		0x8000		/* | ms, 1..32767 */
#define W_REG		0x4000		/* | register, then the value */

#define NCH		4
#define WHOLE		384		/* time units in a whole note */
#define MAXDEPTH	8		/* nesting of [ ] and macros */

enum {
	OP_NOTE,	/* a = note number (octave * 12 + pitch), b = units */
	OP_SLUR,	/* like NOTE, but change pitch without restarting */
	OP_REST,	/* b = units */
	OP_WAIT,	/* b = units: hold the sound */
	OP_TEMPO,	/* a = beats per minute */
	OP_VOL,		/* a = volume */
	OP_ENV,		/* a = envelope: bit 3 up, bits 0-2 step */
	OP_DUTY,	/* a = 0..3 */
	OP_WAVE,	/* a = wave table number */
	OP_NSTEP,	/* a = 0 (15-bit) or 1 (7-bit) noise counter */
	OP_PAN,		/* a = -1 left, 0 both, 1 right */
	OP_SWEEP,	/* a = SOUND1CNT_L value */
	OP_LOOP,	/* L: where the channel loops back to */
	OP_REP,		/* [ */
	OP_ENDREP	/* ] a = count */
};

struct op {
	unsigned char	code;
	short		a;
	long		b;
};

struct chan {
	struct op	*ops;
	int		nops, maxops;
};

static struct chan ch[NCH];

/* wave tables: 32 4-bit samples each */
#define NWAVE		16
static unsigned char wave[NWAVE][32];
static char wavedef[NWAVE];

/* macros: their text, parsed where used */
#define NMACRO		32
static char *macro[NMACRO];

static const char *fname = "-";
static int lineno = 1;

static void
fatal(const char *msg)
{
	fprintf(stderr, "mml: %s:%d: %s\n", fname, lineno, msg);
	exit(1);
}

static void
addop(int c, int code, int a, long b)
{
	struct chan *p = &ch[c];

	if (p->nops == p->maxops) {
		p->maxops = p->maxops ? p->maxops * 2 : 64;
		p->ops = realloc(p->ops, p->maxops * sizeof(struct op));
		if (p->ops == NULL)
			fatal("out of memory");
	}
	p->ops[p->nops].code = code;
	p->ops[p->nops].a = a;
	p->ops[p->nops].b = b;
	p->nops++;
}

/* ---------- parsing ---------- */

/*
 * Per-channel parse state: octave and default length belong to the
 * text, not the player, so they are resolved here.
 */
static int octave[NCH] = { 4, 4, 4, 4 };
static long deflen[NCH] = { WHOLE / 4, WHOLE / 4, WHOLE / 4, WHOLE / 4 };

static const char *cp;			/* parse position */

static void
skipblank(void)
{
	while (*cp == ' ' || *cp == '\t' || *cp == '\r')
		cp++;
}

static int
isdig(int c)
{
	return c >= '0' && c <= '9';
}

static int
number(int *v)
{
	int n = 0;

	skipblank();
	if (!isdig(*cp))
		return 0;
	while (isdig(*cp))
		n = n * 10 + (*cp++ - '0');
	*v = n;
	return 1;
}

static int
needint(const char *what)
{
	int n, neg = 0;

	skipblank();
	if (*cp == '-') {
		neg = 1;
		cp++;
	}
	if (!number(&n)) {
		static char buf[64];

		snprintf(buf, sizeof(buf), "expected a number after %s", what);
		fatal(buf);
	}
	return neg ? -n : n;
}

/*
 * A length as written: n (1 = whole, 4 = quarter, ...) or none, which
 * means the channel's default (l), then dots.  Resolved per channel,
 * since the default can differ between channels.
 */
struct len {
	long	units;		/* 0: the default */
	int	dots;
};

static int
getlen(struct len *l)
{
	int n, any = 0;

	l->units = 0;
	l->dots = 0;
	if (number(&n)) {
		if (n < 1 || n > WHOLE)
			fatal("bad note length");
		l->units = WHOLE / n;
		any = 1;
	}
	for (; *cp == '.'; cp++, any = 1)
		l->dots++;
	return any;
}

static long
units(struct len *l, int c)
{
	long u = l->units ? l->units : deflen[c], add;
	int d;

	for (add = u / 2, d = 0; d < l->dots; d++, add /= 2)
		u += add;
	return u;
}

static int slurnext;			/* the next note follows an & */
static int depth[NCH];			/* open [ per channel: may span lines */

static void parse(const char *, int);

/* Commands up to the end of the line (or of a macro's text). */
static void
commands(int mask)
{
	static const int pitch[7] = { 9, 11, 0, 2, 4, 5, 7 };	/* a..g */
	struct len l, l2[8];
	int c, n, v, k, nt;
	const char *save;
	char cmd;

	for (;;) {
		skipblank();
		if (*cp == '\0' || *cp == '\n' || *cp == ';')
			break;
		cmd = *cp++;
		if (cmd >= 'a' && cmd <= 'g') {
			n = pitch[cmd - 'a'];
			if (*cp == '+' || *cp == '#') {
				n++;
				cp++;
			} else if (*cp == '-') {
				n--;
				cp++;
			}
			getlen(&l);
			/* ties: ^n or &n add to the length */
			for (nt = 0; (*cp == '^' || *cp == '&') &&
			    (isdig(cp[1]) || cp[1] == '.'); nt++) {
				if (nt == 8)
					fatal("too many ties");
				cp++;
				getlen(&l2[nt]);
			}
			for (c = 0; c < NCH; c++) {
				long u;

				if (!(mask & 1 << c))
					continue;
				u = units(&l, c);
				for (k = 0; k < nt; k++)
					u += units(&l2[k], c);
				addop(c, slurnext ? OP_SLUR : OP_NOTE,
				    octave[c] * 12 + n, u);
			}
			slurnext = 0;
			skipblank();
			if (*cp == '&') {	/* & then a note: slur into it */
				save = cp++;
				skipblank();
				if (*cp >= 'a' && *cp <= 'g')
					slurnext = 1;
				else
					cp = save;
			}
			continue;
		}
		switch (cmd) {
		case 'r':
		case 'w':
		case '^':		/* a tie on its own: hold the sound */
		case '&':
			getlen(&l);
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c)
					addop(c, cmd == 'r' ? OP_REST : OP_WAIT, 0,
					    units(&l, c));
			break;
		case 'o':
			n = needint("o");
			if (n < 1 || n > 8)
				fatal("octave must be 1 to 8");
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c)
					octave[c] = n;
			break;
		case '<':
		case '>':
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c) {
					octave[c] += cmd == '>' ? 1 : -1;
					if (octave[c] < 1 || octave[c] > 8)
						fatal("octave out of range");
				}
			break;
		case 'l':
			if (!getlen(&l) || l.units == 0)
				fatal("expected a length after l");
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c)
					deflen[c] = units(&l, c);
			break;
		case 't':
			n = needint("t");
			if (n < 1 || n > 1000)
				fatal("tempo must be 1 to 1000");
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c)
					addop(c, OP_TEMPO, n, 0);
			break;
		case 'v':
			n = needint("v");
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c) {
					if (n < 0 || n > (c == 2 ? 3 : 15))
						fatal(c == 2 ?
						    "channel C volume is 0 to 3" :
						    "volume is 0 to 15");
					addop(c, OP_VOL, n, 0);
				}
			break;
		case 'y':
			n = needint("y");
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c)
					addop(c, OP_PAN, n < 0 ? -1 : n > 0, 0);
			break;
		case 'L':
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c)
					addop(c, OP_LOOP, 0, 0);
			break;
		case '[':
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c) {
					if (++depth[c] > MAXDEPTH)
						fatal("[ ] nested too deep");
					addop(c, OP_REP, 0, 0);
				}
			break;
		case ']':
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c && --depth[c] < 0)
					fatal("] without [");
			if (!number(&n))
				n = 2;
			if (n < 1 || n > 255)
				fatal("repeat count must be 1 to 255");
			for (c = 0; c < NCH; c++)
				if (mask & 1 << c)
					addop(c, OP_ENDREP, n, 0);
			break;
		case '@':
			if (*cp == '@') {			/* @@n: a macro */
				cp++;
				n = needint("@@");
				if (n < 0 || n >= NMACRO || macro[n] == NULL)
					fatal("macro not defined");
				save = cp;
				parse(macro[n], mask);
				cp = save;
			} else if (strncmp(cp, "wave", 4) == 0) {
				cp += 4;
				n = needint("@wave");
				if (n < 0 || n >= NWAVE || !wavedef[n])
					fatal("wave not defined");
				if (mask & ~4)
					fatal("@wave is for channel C only");
				addop(2, OP_WAVE, n, 0);
			} else if (strncmp(cp, "ve", 2) == 0) {
				cp += 2;
				n = needint("@ve");
				if (n < -7 || n > 7)
					fatal("@ve is -7 to 7");
				if (mask & 4)
					fatal("@ve is not for channel C");
				v = n > 0 ? 8 | n : -n;
				for (c = 0; c < NCH; c++)
					if (mask & 1 << c)
						addop(c, OP_ENV, v, 0);
			} else if (strncmp(cp, "wd", 2) == 0) {
				cp += 2;
				n = needint("@wd");
				if (n < 0 || n > 3)
					fatal("@wd is 0 to 3");
				if (mask & ~3)
					fatal("@wd is for channels A and B only");
				for (c = 0; c < 2; c++)
					if (mask & 1 << c)
						addop(c, OP_DUTY, n, 0);
			} else if (strncmp(cp, "ns", 2) == 0) {
				cp += 2;
				n = needint("@ns");
				if (n < 0 || n > 1)
					fatal("@ns is 0 or 1");
				if (mask & ~8)
					fatal("@ns is for channel D only");
				addop(3, OP_NSTEP, n, 0);
			} else if (strncmp(cp, "sw", 2) == 0) {
				cp += 2;
				n = needint("@sw");		/* time 0..7 */
				skipblank();
				if (*cp++ != ',')
					fatal("@sw needs time,shift");
				v = needint("@sw");		/* -7..7 */
				if (n < 0 || n > 7 || v < -7 || v > 7)
					fatal("@sw is 0..7,-7..7");
				if (mask & ~1)
					fatal("@sw is for channel A only");
				addop(0, OP_SWEEP, n << 4 | (v < 0 ? 8 | -v : v), 0);
			} else
				fatal("unknown or unsupported @ command");
			break;
		default:
			fatal("unknown command");
		}
	}
}

/* { ... } after "@wave n =" or "@@n =": returns the text inside. */
static char *
braces(void)
{
	const char *start;
	char *t;
	int n;

	skipblank();
	if (*cp++ != '=')
		fatal("expected =");
	skipblank();
	if (*cp++ != '{')
		fatal("expected {");
	for (start = cp; *cp != '}'; cp++) {
		if (*cp == '\0')
			fatal("missing }");
		if (*cp == '\n')
			lineno++;
	}
	n = cp - start;
	cp++;
	if ((t = malloc(n + 1)) == NULL)
		fatal("out of memory");
	memcpy(t, start, n);
	t[n] = '\0';
	return t;
}

/* @wave n = { 32 samples 0..15 } */
static void
wavedefine(void)
{
	const char *save;
	char *t;
	int n, i, v;

	n = needint("@wave");
	if (n < 0 || n >= NWAVE)
		fatal("wave number must be 0 to 15");
	t = braces();
	save = cp;
	cp = t;
	for (i = 0; i < 32; i++) {
		while (*cp == ' ' || *cp == '\t' || *cp == '\n' ||
		    *cp == '\r' || *cp == ',')
			cp++;
		if (!number(&v) || v > 15)
			fatal("a wave is 32 numbers 0 to 15");
		wave[n][i] = v;
	}
	cp = save;
	free(t);
	wavedef[n] = 1;
}

/*
 * Parse text: lines of channel commands ("AB c d e") and definitions
 * (@wave, @@).  Within a macro, mask gives the channels it is used on.
 */
static void
parse(const char *text, int mask)
{
	int m, n;

	cp = text;
	for (;;) {
		skipblank();
		if (*cp == '\0')
			break;
		if (*cp == '\n') {
			cp++;
			if (mask == 0)
				lineno++;
			continue;
		}
		if (*cp == ';') {			/* comment */
			while (*cp != '\0' && *cp != '\n')
				cp++;
			continue;
		}
		if (mask != 0) {			/* macro text */
			commands(mask);
			continue;
		}
		if (strncmp(cp, "@wave", 5) == 0) {
			cp += 5;
			wavedefine();
			continue;
		}
		if (strncmp(cp, "@@", 2) == 0) {
			cp += 2;
			n = needint("@@");
			if (n < 0 || n >= NMACRO)
				fatal("macro number must be 0 to 31");
			free(macro[n]);
			macro[n] = braces();
			continue;
		}
		for (m = 0; *cp >= 'A' && *cp <= 'D'; cp++)
			m |= 1 << (*cp - 'A');
		if (m == 0)
			fatal("a line starts with channel names A-D");
		commands(m);
	}
	if (mask == 0)
		for (m = 0; m < NCH; m++)
			if (depth[m] != 0)
				fatal("[ without ]");
}

/* ---------- playing ---------- */

static int outfd;
static unsigned short obuf[256];
static int on;
static unsigned long outms;		/* stream time written so far */

static void
flush(void)
{
	if (on > 0 && write(outfd, obuf, on * 2) != on * 2) {
		perror("mml: write");
		exit(1);
	}
	on = 0;
}

static void
word(unsigned w)
{
	if (on == (int)(sizeof(obuf) / 2))
		flush();
	obuf[on++] = w;
}

static void
reg(int r, unsigned v)
{
	word(W_REG | r);
	word(v & 0xffff);
}

/* Bring the stream up to time ms. */
static void
until(unsigned long ms)
{
	unsigned long d;

	while (outms < ms) {
		d = ms - outms;
		if (d > 32767)
			d = 32767;
		word(W_WAIT | d);
		outms += d;
	}
}

struct player {
	int		pc;
	unsigned long	us;		/* time of the next operation */
	unsigned long	rem;		/* tempo division remainder */
	int		tempo;
	int		vol, env, duty, nstep;
	int		looppc;		/* -1: no L */
	int		nloop;		/* times looped back */
	int		sp;
	int		rpc[MAXDEPTH], rcnt[MAXDEPTH];
	int		done;
};

static struct player pl[NCH];
static int loops = -1;			/* L repeats left; -1 for ever */
static int panmask = 0xff00;		/* SOUNDCNT_L channel enables */

/*
 * Square channels: f = 131072 / (2048 - x) Hz; wave: 65536 / (2048 - x).
 * C in octave 0, in millihertz.
 */
static const unsigned long c0mhz[12] = {
	16352, 17324, 18354, 19445, 20602, 21827,
	23125, 24500, 25957, 27500, 29135, 30868
};

static int
freqreg(int note, unsigned long base)
{
	unsigned long f;
	long x;

	if (note < 0)
		note = 0;
	f = c0mhz[note % 12] << (note / 12);
	x = 2048 - (long)((base + f / 2) / f);
	if (x < 0)
		x = 0;
	if (x > 2047)
		x = 2047;
	return x;
}

/*
 * Noise: the octave picks the clock divider r (o1 = 7 .. o8 = 0), the
 * note the shift s (c = 15 .. b = 4): as mmlgb lays it out.
 */
static int
noisereg(int note)
{
	int o = note / 12, r = 8 - o, s = 15 - note % 12;

	if (r < 0)
		r = 0;
	if (r > 7)
		r = 7;
	return s << 4 | r;
}

/* channel C's wave until a @wave: a triangle */
static const unsigned char triangle[32] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
};

static const int wavevol[4] = { 0, 3, 2, 1 };	/* v0..3 -> 0,25,50,100% */

static void
mute(int c)
{
	static const int envreg[NCH] = { 0x62, 0x68, 0, 0x78 };

	if (c == 2)
		reg(0x70, 0);			/* wave DAC off */
	else
		reg(envreg[c], 0);		/* DAC off */
}

/*
 * Load 32 samples into wave RAM, two to a byte, the first in the high
 * nibble.  The CPU reaches the bank not selected for playback, so
 * select bank 1, fill bank 0, then select bank 0 (DAC off till a note).
 */
static void
loadwave(const unsigned char *w)
{
	int i;

	reg(0x70, 0x40);
	for (i = 0; i < 16; i += 2)
		reg(0x90 + i, (w[2 * i] << 4 | w[2 * i + 1]) |
		    (w[2 * i + 2] << 4 | w[2 * i + 3]) << 8);
	reg(0x70, 0x00);
}

static void
noteon(int c, int note, int slur)
{
	struct player *p = &pl[c];
	int env = p->vol << 12 | (p->env & 8 ? 0x800 : 0) | (p->env & 7) << 8;

	switch (c) {
	case 0:
	case 1:
		if (!slur)
			reg(c == 0 ? 0x62 : 0x68, env | p->duty << 6);
		reg(c == 0 ? 0x64 : 0x6c,
		    (slur ? 0 : 0x8000) | freqreg(note, 131072000UL));
		break;
	case 2:
		if (!slur) {
			reg(0x70, 0x80);		/* DAC on, bank 0 */
			reg(0x72, wavevol[p->vol & 3] << 13);
		}
		reg(0x74, (slur ? 0 : 0x8000) | freqreg(note, 65536000UL));
		break;
	case 3:
		if (!slur)
			reg(0x78, env);
		reg(0x7c, (slur ? 0 : 0x8000) | p->nstep << 3 | noisereg(note));
		break;
	}
}

/* Microseconds for u units at the channel's tempo (quarter = WHOLE/4). */
static unsigned long
duration(struct player *p, long u)
{
	unsigned long us = 0, n;

	while (u > 0) {
		n = u > 3000 ? 3000 : u;	/* keep n * 625000 in 32 bits */
		u -= n;
		n = n * 625000UL + p->rem;	/* 60e6 / (WHOLE / 4) = 625000 */
		us += n / p->tempo;
		p->rem = n % p->tempo;
	}
	return us;
}

/* Run channel c until it takes time; 0 when it has ended. */
static int
step(int c)
{
	struct player *p = &pl[c];
	struct chan *q = &ch[c];
	struct op *o;

	until(p->us / 1000);
	for (;;) {
		if (p->pc >= q->nops) {
			/* back to L, as often as -l says (for ever: -1) */
			if (p->looppc < 0 || p->looppc >= q->nops ||
			    (loops >= 0 && p->nloop >= loops))
				break;
			p->nloop++;
			p->pc = p->looppc;
			p->sp = 0;
			continue;
		}
		o = &q->ops[p->pc++];
		switch (o->code) {
		case OP_NOTE:
		case OP_SLUR:
			noteon(c, o->a, o->code == OP_SLUR);
			p->us += duration(p, o->b);
			return 1;
		case OP_REST:
			mute(c);
			p->us += duration(p, o->b);
			return 1;
		case OP_WAIT:
			p->us += duration(p, o->b);
			return 1;
		case OP_TEMPO:
			p->tempo = o->a;
			p->rem = 0;
			break;
		case OP_VOL:
			p->vol = o->a;
			break;
		case OP_ENV:
			p->env = o->a;
			break;
		case OP_DUTY:
			p->duty = o->a;
			break;
		case OP_WAVE:
			loadwave(wave[o->a]);
			break;
		case OP_NSTEP:
			p->nstep = o->a;
			break;
		case OP_PAN:
			panmask &= ~(0x1100 << c);
			panmask |= (o->a <= 0 ? 0x1000 : 0) << c |
			    (o->a >= 0 ? 0x0100 : 0) << c;
			reg(0x80, panmask | 0x77);
			break;
		case OP_SWEEP:
			reg(0x60, o->a);
			break;
		case OP_LOOP:
			p->looppc = p->pc;
			break;
		case OP_REP:
			if (p->sp == MAXDEPTH)
				fatal("internal: repeat stack");
			p->rpc[p->sp] = p->pc;
			p->rcnt[p->sp++] = 0;
			break;
		case OP_ENDREP:
			if (++p->rcnt[p->sp - 1] < o->a)
				p->pc = p->rpc[p->sp - 1];
			else
				p->sp--;
			break;
		}
	}
	mute(c);
	return 0;
}

static void
play(void)
{
	int c, best;
	unsigned long base;

	for (c = 0; c < NCH; c++) {
		pl[c].tempo = 120;
		pl[c].vol = c == 2 ? 3 : 15;
		pl[c].duty = 2;
		pl[c].looppc = -1;
		pl[c].done = ch[c].nops == 0;
	}
	reg(0x80, panmask | 0x77);	/* all four channels, both sides */
	reg(0x60, 0);			/* no sweep */
	for (c = 0; c < NCH; c++)
		mute(c);
	loadwave(triangle);
	for (;;) {
		best = -1;
		for (c = 0; c < NCH; c++)
			if (!pl[c].done &&
			    (best < 0 || pl[c].us < pl[best].us))
				best = c;
		if (best < 0)
			break;
		if (!step(best))
			pl[best].done = 1;
		/* keep the times in range when looping for ever */
		if (pl[best].us > 2000000000UL) {
			base = pl[best].us;
			for (c = 0; c < NCH; c++)
				if (!pl[c].done && pl[c].us < base)
					base = pl[c].us;
			base -= base % 1000;
			for (c = 0; c < NCH; c++)
				pl[c].us -= pl[c].done ? 0 : base;
			outms -= base / 1000;
		}
	}
	flush();
}

/* ---------- main ---------- */

static void
stop(int sig)
{
	ioctl(outfd, PSGFLUSH, 0);
	_exit(1);
}

static void
usage(void)
{
	fprintf(stderr, "usage: mml [-o file] [-l loops] [file]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	char *out = NULL, *text;
	long size, n;
	int c, fd;

	while ((c = getopt(argc, argv, "o:l:")) != -1) {
		switch (c) {
		case 'o':
			out = optarg;
			break;
		case 'l':
			loops = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();
	if (argc == 1) {
		fname = argv[0];
		if ((fd = open(fname, O_RDONLY)) < 0) {
			perror(fname);
			return 1;
		}
	} else
		fd = 0;
	/* read it all */
	size = 4096;
	text = malloc(size);
	for (n = 0; text != NULL; ) {
		c = read(fd, text + n, size - n - 1);
		if (c <= 0)
			break;
		n += c;
		if (n == size - 1)
			text = realloc(text, size *= 2);
	}
	if (text == NULL) {
		fprintf(stderr, "mml: out of memory\n");
		return 1;
	}
	text[n] = '\0';
	parse(text, 0);

	if (out != NULL) {
		if (loops < 0)
			loops = 0;
		if ((outfd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
			perror(out);
			return 1;
		}
	} else {
		if ((outfd = open("/dev/psg", O_WRONLY)) < 0) {
			perror("/dev/psg");
			return 1;
		}
		signal(SIGINT, stop);
		signal(SIGTERM, stop);
	}
	play();
	if (out == NULL)
		ioctl(outfd, PSGDRAIN, 0);
	close(outfd);
	return 0;
}
