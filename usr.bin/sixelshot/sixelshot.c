/*
 * sixelshot - dump the GBA screen as a DEC sixel image.
 *
 * Usage: sixelshot [-2|-3] [-o file]
 *
 * Reads the frame buffer straight from VRAM (there is no MMU, so a user
 * program can): video Mode 3 (240x160, the text console and /dev/tft0) or
 * Mode 5 (160x128, the page being shown, /dev/tft1 and tft2).  The image
 * is written as sixel, which mintty, xterm, RLogin and Windows Terminal
 * (1.22 and later) show; Tera Term does not.
 *
 * Output goes to -o file, or to standard output when that is not a
 * terminal, so `sixelshot > shot.six` makes a file to copy to the PC (with
 * ek) and `cat` there.  When standard output is a terminal, the image is
 * written to /dev/sio instead: printed on the console it would also be
 * drawn on the LCD, scrolling the screen being read.  Opening /dev/sio
 * takes the serial line off the console while the image is sent, so a
 * terminal on the PC end of the link cable shows it directly.
 *
 * -2 and -3 scale the image up (the GBA screen is small on a PC).
 *
 * Colours: if the screen uses at most 256 colours, each keeps its own
 * colour register; otherwise they are cut down to 3-3-2 bits (256).
 * Sixel draws six rows at a time, one pass per colour present in the
 * band, with runs of the same sixel compressed as "!count".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define REG_DISPCNT	(*(volatile unsigned short *)0x04000000)
#define VRAM		((volatile unsigned short *)0x06000000)
#define MODE5_PAGE1	0xA000		/* halfwords: 0x0600A000 */

#define MAXCOLORS	256

static volatile unsigned short *fb;	/* frame buffer being read */
static int fbw, fbh;			/* its size */
static int scale = 1;

static unsigned short pal[MAXCOLORS];	/* colours, sorted, if exact */
static int npal;
static int exact;			/* 1: pal[] holds every colour */

static FILE *out;

/* Mark which 15-bit colours appear; keep them if there are few enough. */
static void
scan_colors(void)
{
	static unsigned char seen[32768 / 8];
	int x, y, i, n;
	unsigned c;

	memset(seen, 0, sizeof(seen));
	n = 0;
	for (y = 0; y < fbh; y++)
		for (x = 0; x < fbw; x++) {
			c = fb[y * fbw + x] & 0x7fff;
			if (!(seen[c >> 3] & (1 << (c & 7)))) {
				seen[c >> 3] |= 1 << (c & 7);
				n++;
			}
		}
	if (n > MAXCOLORS) {
		exact = 0;
		npal = MAXCOLORS;
		return;
	}
	exact = 1;
	npal = 0;
	for (i = 0; i < 32768; i++)		/* ascending: pal[] is sorted */
		if (seen[i >> 3] & (1 << (i & 7)))
			pal[npal++] = i;
}

/* Colour register for a BGR555 pixel. */
static int
color_index(unsigned c)
{
	int lo, hi, mid;

	c &= 0x7fff;
	if (!exact)			/* rrrgggbb */
		return ((c >> 2) & 7) << 5 | ((c >> 7) & 7) << 2 |
		    ((c >> 13) & 3);
	lo = 0;
	hi = npal - 1;
	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (pal[mid] < c)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* Define the colour registers: "#n;2;r;g;b", r g b in percent. */
static void
put_palette(void)
{
	int i, r, g, b;

	for (i = 0; i < npal; i++) {
		if (exact) {
			r = pal[i] & 31;
			g = (pal[i] >> 5) & 31;
			b = (pal[i] >> 10) & 31;
			r = r * 100 / 31;
			g = g * 100 / 31;
			b = b * 100 / 31;
		} else {
			r = ((i >> 5) & 7) * 100 / 7;
			g = ((i >> 2) & 7) * 100 / 7;
			b = (i & 3) * 100 / 3;
		}
		fprintf(out, "#%d;2;%d;%d;%d", i, r, g, b);
	}
}

/* Emit one sixel character `n` times, as "!n" when that is shorter. */
static void
put_run(int ch, int n)
{
	if (n <= 0)
		return;
	if (n > 3)
		fprintf(out, "!%d%c", n, ch);
	else
		while (n-- > 0)
			putc(ch, out);
}

static void
put_image(void)
{
	static unsigned char idx[6][240 * 3];	/* one band, colour registers */
	static unsigned char used[MAXCOLORS];
	int w = fbw * scale, h = fbh * scale;
	int y0, r, x, c, bits, prev, run, rows, first;

	fprintf(out, "\033P0;1;0q\"1;1;%d;%d", w, h);
	put_palette();
	for (y0 = 0; y0 < h; y0 += 6) {
		rows = h - y0 < 6 ? h - y0 : 6;
		memset(used, 0, npal);
		for (r = 0; r < rows; r++) {
			volatile unsigned short *line =
			    fb + ((y0 + r) / scale) * fbw;
			for (x = 0; x < w; x++) {
				c = color_index(line[x / scale]);
				idx[r][x] = c;
				used[c] = 1;
			}
		}
		first = 1;
		for (c = 0; c < npal; c++) {
			if (!used[c])
				continue;
			if (!first)
				putc('$', out);	/* back to the band's start */
			first = 0;
			fprintf(out, "#%d", c);
			prev = -1;
			run = 0;
			for (x = 0; x < w; x++) {
				bits = 0;
				for (r = 0; r < rows; r++)
					if (idx[r][x] == c)
						bits |= 1 << r;
				if (bits + 63 == prev)
					run++;
				else {
					put_run(prev, run);
					prev = bits + 63;
					run = 1;
				}
			}
			if (prev != 63)		/* trailing blanks are free */
				put_run(prev, run);
		}
		putc('-', out);			/* next band */
	}
	fprintf(out, "\033\\");
}

static void
usage(void)
{
	fprintf(stderr, "usage: sixelshot [-2|-3] [-o file]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	char *file = 0;
	int c, mode, fd;

	while ((c = getopt(argc, argv, "23o:")) != -1) {
		switch (c) {
		case '2':
		case '3':
			scale = c - '0';
			break;
		case 'o':
			file = optarg;
			break;
		default:
			usage();
		}
	}

	mode = REG_DISPCNT & 7;
	if (mode == 3) {
		fb = VRAM;
		fbw = 240;
		fbh = 160;
	} else if (mode == 5) {
		fb = VRAM + ((REG_DISPCNT & 0x10) ? MODE5_PAGE1 : 0);
		fbw = 160;
		fbh = 128;
	} else {
		fprintf(stderr, "sixelshot: video mode %d not supported\n", mode);
		return 1;
	}

	if (file)
		out = fopen(file, "w");
	else if (!isatty(1))
		out = stdout;
	else {
		fd = open("/dev/sio", O_WRONLY);
		if (fd < 0) {
			perror("sixelshot: /dev/sio (redirect to a file)");
			return 1;
		}
		out = fdopen(fd, "w");
	}
	if (!out) {
		perror(file ? file : "sixelshot");
		return 1;
	}

	scan_colors();
	put_image();
	putc('\n', out);
	if (fclose(out) != 0) {
		perror("sixelshot");
		return 1;
	}
	return 0;
}
