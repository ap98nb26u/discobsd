/*
 * GBA backend for the portable graphics-panel device: /dev/tft0 and /dev/tft1.
 *
 * /dev/tft0 (minor 0) is video Mode 3 - a 240x160, 16bpp linear framebuffer
 * at VRAM (0x06000000), the same surface the text console (gba_text.c) draws
 * into.  So this driver is just the portable gpanel software rasterizers
 * (ported from sys/arch/pic32/dev/gpanel.c) over a trivial "write a pixel to
 * VRAM" backend: the existing gpanel ioctl ABI (sys/sys/gpanel.h), the
 * libgpanel userland library and the share/examples/gpanel programs run on
 * the GBA unchanged.
 *
 * /dev/tft1 (minor 1) and /dev/tft2 (minor 2) are video Mode 5 - two 160x128
 * 16bpp pages.  The app draws the hidden back page and presents it with the
 * GBA-only GPANEL_FLIP ioctl, which swaps pages at vblank: tear-free,
 * flicker-free animation with no vblank wait of its own.  tft1 BG2-scales the
 * 160x128 image up to fill the 240x160 screen; tft2 draws it 1:1, centred with
 * a black border.  Scaling is free (PPU per-scanline), so the two differ only
 * in display size - exposing both lets the scaled/unscaled cost be compared.
 * Same ABI otherwise - gpanel_width/height just report 160x128.
 *
 * Colour: the portable layer and the example data use 16-bit RGB565 (the
 * pic32 TFT panels' native order).  The GBA framebuffer is 15-bit BGR555
 * (bit15 unused).  set_pixel converts RGB565 -> BGR555 once, so every
 * primitive, image and glyph shows the intended colour.
 *
 * Phase 1 (roadmap #9): drawing primitives + the single-owner guard
 * (gba_gfx_active) that stops the console cursor/keyboard/screen-blank from
 * painting over graphics.  See sys/arch/gba/NOTES.graphics for the full
 * design and the Phase 4 convergence that retires the guard.
 */
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/gpanel.h>

#include <machine/gba.h>
#include <gba/dev/gba_gpanel.h>

#define GFB_W	240		/* Mode 3 (/dev/tft0): full screen, single buffer */
#define GFB_H	160
#define M5_W	160		/* Mode 5 (/dev/tft1,tft2): double-buffered */
#define M5_H	128
#define M5_OFFX	((GFB_W - M5_W) / 2)	/* 40: centre the 160x128 image when 1:1 */
#define M5_OFFY	((GFB_H - M5_H) / 2)	/* 16 */
#define VRAM0	((volatile unsigned short *)0x06000000)	/* Mode3 FB / M5 page 0 */
#define VRAM1	((volatile unsigned short *)0x0600A000)	/* M5 page 1 */

/* BG2 rotation/scaling registers (not declared in gba.h). */
#define REG_BG2PA	(*(volatile short *)(REG_BASE + 0x0020))
#define REG_BG2PB	(*(volatile short *)(REG_BASE + 0x0022))
#define REG_BG2PC	(*(volatile short *)(REG_BASE + 0x0024))
#define REG_BG2PD	(*(volatile short *)(REG_BASE + 0x0026))
#define REG_BG2X	(*(volatile int   *)(REG_BASE + 0x0028))
#define REG_BG2Y	(*(volatile int   *)(REG_BASE + 0x002C))
#define DISPCNT_BG2	(1 << 10)	/* enable BG2 (the bitmap in modes 3/5) */
#define DISPCNT_FRAME1	(1 << 4)	/* Mode 5: display page 1 instead of page 0 */

/* Portable raster core (below) reads these; = the logical surface size. */
int gpanel_width  = GFB_W;
int gpanel_height = GFB_H;

/* Text-drawing cursor for GPANEL_CHAR/TEXT, as in the pic32 driver. */
static int _col, _row;

/* Set while a userland process holds the panel (see gba_gpanel.h). */
static int gpanel_active;

/*
 * Current draw surface.  Mode 3 => the one visible 240x160 framebuffer;
 * Mode 5 => the hidden 160x128 back page (the app presents it with GPANEL_FLIP).
 */
static volatile unsigned short *fb_base = VRAM0;
static int fb_w = GFB_W;
static int fb_h = GFB_H;
static int panel_mode5;		/* open panel is /dev/tft1 (Mode 5, double buffer) */
static int fb_front;		/* Mode 5: page index currently displayed (0/1) */

int
gba_gfx_active(void)
{
	return gpanel_active;
}

/*
 * Select Mode 3: the single 240x160 framebuffer the text console shares.
 * Identity BG2 transform (1:1), in case we are returning from Mode 5's scale.
 */
static void
panel_set_mode3(void)
{
	panel_mode5 = 0;
	fb_base = VRAM0;
	fb_w = gpanel_width = GFB_W;
	fb_h = gpanel_height = GFB_H;
	REG_BG2PA = 1 << 8;	/* identity: 1.0 in 8.8 fixed point */
	REG_BG2PB = 0;
	REG_BG2PC = 0;
	REG_BG2PD = 1 << 8;
	REG_BG2X = 0;
	REG_BG2Y = 0;
	REG_DISPCNT = 3 | DISPCNT_BG2;
}

/*
 * Select Mode 5: two 160x128 16bpp pages.  Display page 0, draw to page 1
 * (the back page).  The BG2 affine transform decides how the 160x128 bitmap
 * lands on the 240x160 screen:
 *   scaled   -> stretched to fill the whole screen (/dev/tft1);
 *   1:1      -> drawn pixel-exact, centred with a black border (/dev/tft2).
 * The scaling itself is free (the PPU does it per-scanline); it is exposed as
 * a separate device so the two can be compared directly.
 */
static void
panel_set_mode5(int scaled)
{
	panel_mode5 = 1;
	fb_front = 0;
	fb_base = VRAM1;		/* back page (page 0 is shown) */
	fb_w = gpanel_width = M5_W;
	fb_h = gpanel_height = M5_H;
	REG_BG2PB = 0;
	REG_BG2PC = 0;
	if (scaled) {
		/* screen(sx,sy) -> source(sx*160/240, sy*128/160), 8.8 fixed. */
		REG_BG2PA = (M5_W << 8) / GFB_W;	/* ~170 */
		REG_BG2PD = (M5_H << 8) / GFB_H;	/* ~204 */
		REG_BG2X = 0;
		REG_BG2Y = 0;
	} else {
		REG_BG2PA = 1 << 8;			/* 1:1 */
		REG_BG2PD = 1 << 8;
		REG_BG2X = -(M5_OFFX << 8);		/* shift image to screen (40,16) */
		REG_BG2Y = -(M5_OFFY << 8);
	}
	REG_DISPCNT = 5 | DISPCNT_BG2;		/* Mode 5, show page 0 */
}

/*
 * Present the back page (Mode 5 only): swap which page is displayed, at
 * vblank so the viewer never sees a half-swapped frame, then point drawing
 * at the newly hidden page.
 */
static void
panel_flip(void)
{
	if (!panel_mode5)
		return;
	while (REG_VCOUNT < 160)		/* wait for vblank to start */
		;
	fb_front ^= 1;
	REG_DISPCNT = 5 | DISPCNT_BG2 | (fb_front ? DISPCNT_FRAME1 : 0);
	fb_base = fb_front ? VRAM0 : VRAM1;	/* draw to the now-hidden page */
}

/*
 * Convert a 16-bit RGB565 colour (portable layer / example data) to the
 * GBA's 15-bit BGR555 framebuffer format.
 */
static unsigned short
rgb565_to_bgr555(int c)
{
	unsigned r = (c >> 11) & 0x1f;	/* 5 red bits   */
	unsigned g = (c >> 6)  & 0x1f;	/* top 5 of the 6 green bits */
	unsigned b =  c        & 0x1f;	/* 5 blue bits  */

	return (b << 10) | (g << 5) | r;
}

/*
 * Kernel-internal primitives (BGR555 in, no conversion) - the single point
 * that touches VRAM, so the console can later route through it.
 */
void
gba_fb_pixel(int x, int y, unsigned short bgr555)
{
	if ((unsigned)x >= (unsigned)fb_w || (unsigned)y >= (unsigned)fb_h)
		return;			/* clamp: a stray store hits OAM/other VRAM */
	fb_base[y * fb_w + x] = bgr555;
}

void
gba_fb_fill(int x0, int y0, int x1, int y1, unsigned short bgr555)
{
	int t, x, y;

	if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= fb_w) x1 = fb_w - 1;
	if (y1 >= fb_h) y1 = fb_h - 1;
	for (y = y0; y <= y1; y++) {
		volatile unsigned short *p = fb_base + y * fb_w + x0;
		for (x = x0; x <= x1; x++)
			*p++ = bgr555;
	}
}

/*
 * gpanel_hw backend ops (RGB565 colours from the ioctl layer).
 */
static void
gfb_set_pixel(int x, int y, int color)
{
	gba_fb_pixel(x, y, rgb565_to_bgr555(color));
}

static void
gfb_fill_rectangle(int x0, int y0, int x1, int y1, int color)
{
	gba_fb_fill(x0, y0, x1, y1, rgb565_to_bgr555(color));
}

static void
gfb_draw_image(int x, int y, int width, int height, const unsigned short *data)
{
	int h, w;

	for (h = 0; h < height; h++)
		for (w = 0; w < width; w++)
			gba_fb_pixel(x + w, y + h, rgb565_to_bgr555(*data++));
}

static void
gfb_draw_glyph(const struct gpanel_font_t *font, int color, int background,
	int x, int y, int width, const unsigned short *bits)
{
	unsigned short fg = rgb565_to_bgr555(color);
	unsigned short bg = (background >= 0) ? rgb565_to_bgr555(background) : 0;
	unsigned bitmask = 0;
	int h, w;

	for (h = 0; h < font->height; h++) {
		for (w = 0; w < width; w++) {
			if ((w & 15) == 0)
				bitmask = *bits++;
			else
				bitmask <<= 1;
			if (bitmask & 0x8000)
				gba_fb_pixel(x + w, y + h, fg);
			else if (background >= 0)
				gba_fb_pixel(x + w, y + h, bg);
		}
	}
}

/*
 * Portable software rasterizers (ported verbatim from pic32 gpanel.c, with
 * the hw.* indirection replaced by the gfb_* backend above).
 */
static void
gpanel_draw_line(int x0, int y0, int x1, int y1, int color)
{
	int dx, dy, stepx, stepy, fraction;

	if (x0 == x1 || y0 == y1) {
		gfb_fill_rectangle(x0, y0, x1, y1, color);
		return;
	}
	dy = y1 - y0;
	if (dy < 0) { dy = -dy; stepy = -1; } else stepy = 1;
	dx = x1 - x0;
	if (dx < 0) { dx = -dx; stepx = -1; } else stepx = 1;
	dy <<= 1;
	dx <<= 1;
	gfb_set_pixel(x0, y0, color);
	if (dx > dy) {
		fraction = dy - (dx >> 1);
		while (x0 != x1) {
			if (fraction >= 0) {
				y0 += stepy;
				fraction -= dx;
			}
			x0 += stepx;
			fraction += dy;
			gfb_set_pixel(x0, y0, color);
		}
	} else {
		fraction = dx - (dy >> 1);
		while (y0 != y1) {
			if (fraction >= 0) {
				x0 += stepx;
				fraction -= dy;
			}
			y0 += stepy;
			fraction += dx;
			gfb_set_pixel(x0, y0, color);
		}
	}
}

static void
gpanel_draw_frame(int x0, int y0, int x1, int y1, int color)
{
	gfb_fill_rectangle(x0, y0, x1, y0, color);
	gfb_fill_rectangle(x0, y1, x1, y1, color);
	gfb_fill_rectangle(x0, y0, x0, y1, color);
	gfb_fill_rectangle(x1, y0, x1, y1, color);
}

static void
gpanel_draw_circle(int x0, int y0, int radius, int color)
{
	int f = 1 - radius;
	int ddF_x = 0;
	int ddF_y = -2 * radius;
	int x = 0;
	int y = radius;

	gfb_set_pixel(x0, y0 + radius, color);
	gfb_set_pixel(x0, y0 - radius, color);
	gfb_set_pixel(x0 + radius, y0, color);
	gfb_set_pixel(x0 - radius, y0, color);
	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x + 1;
		gfb_set_pixel(x0 + x, y0 + y, color);
		gfb_set_pixel(x0 - x, y0 + y, color);
		gfb_set_pixel(x0 + x, y0 - y, color);
		gfb_set_pixel(x0 - x, y0 - y, color);
		gfb_set_pixel(x0 + y, y0 + x, color);
		gfb_set_pixel(x0 - y, y0 + x, color);
		gfb_set_pixel(x0 + y, y0 - x, color);
		gfb_set_pixel(x0 - y, y0 - x, color);
	}
}

static void
gpanel_draw_char(const struct gpanel_font_t *font, int color, int background,
	int sym)
{
	unsigned cindex, width;
	const unsigned short *bits;

	switch (sym) {
	case '\n':
		_row += font->height;
		_col = 0;
		if (_row > gpanel_height - (int)font->height)
			_row = 0;
		return;
	case '\r':
		_col = 0;
		return;
	case '\t':
		sym = ' ';
		break;
	}
	if (sym < font->firstchar || sym >= font->firstchar + font->size)
		sym = font->defaultchar;
	cindex = sym - font->firstchar;

	if (font->width)
		width = font->width[cindex];
	else
		width = font->maxwidth;
	if (font->offset)
		bits = font->bits + font->offset[cindex];
	else
		bits = font->bits + cindex * font->height;

	gfb_draw_glyph(font, color, background, _col, _row, width, bits);
	_col += width;
}

static void
gpanel_draw_text(const struct gpanel_font_t *font, int color, int background,
	int x, int y, const char *text)
{
	int sym;

	_col = x;
	_row = y;
	for (;;) {
		sym = *text++;
		if (!sym)
			break;
		gpanel_draw_char(font, color, background, sym);
	}
}

int
gpanel_open(dev_t dev, int flag, int mode)
{
	int m = minor(dev);

	if ((unsigned)m > 2)		/* 0=Mode3, 1=Mode5 scaled, 2=Mode5 1:1 */
		return ENODEV;
	if (gpanel_active)
		return EBUSY;		/* single owner of the panel at a time */
	switch (m) {
	case 1:
		panel_set_mode5(1);	/* /dev/tft1: 160x128 dbuf, scaled fullscreen */
		break;
	case 2:
		panel_set_mode5(0);	/* /dev/tft2: 160x128 dbuf, 1:1 centred */
		break;
	default:
		panel_set_mode3();	/* /dev/tft0: 240x160 single buffer */
		break;
	}
	gpanel_active = 1;		/* console/keyboard stop painting VRAM */
	return 0;
}

int
gpanel_close(dev_t dev, int flag, int mode)
{
	/*
	 * Release the panel back to the console.  Restore Mode 3 (undoing any
	 * Mode-5 scaling) and clear to black - the app's last frame is not the
	 * console's to keep, and there is no text backing store yet
	 * (NOTES.graphics 5.1) - then drop the owner flag so console/keyboard
	 * drawing resumes; the next console output paints on the cleared screen.
	 */
	panel_set_mode3();
	gba_fb_fill(0, 0, GFB_W - 1, GFB_H - 1, 0);
	gpanel_active = 0;
	return 0;
}

int
gpanel_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	switch (cmd) {
	case GPANEL_CLEAR: {
		struct gpanel_clear_t *p = (struct gpanel_clear_t *)addr;

		gfb_fill_rectangle(0, 0, gpanel_width - 1, gpanel_height - 1,
		    p->color);
		p->xsize = gpanel_width;
		p->ysize = gpanel_height;
		break;
	}
	case GPANEL_PIXEL: {
		struct gpanel_pixel_t *p = (struct gpanel_pixel_t *)addr;

		gfb_set_pixel(p->x, p->y, p->color);
		break;
	}
	case GPANEL_LINE: {
		struct gpanel_line_t *p = (struct gpanel_line_t *)addr;

		gpanel_draw_line(p->x0, p->y0, p->x1, p->y1, p->color);
		break;
	}
	case GPANEL_RECT: {
		struct gpanel_rect_t *p = (struct gpanel_rect_t *)addr;

		gpanel_draw_frame(p->x0, p->y0, p->x1, p->y1, p->color);
		break;
	}
	case GPANEL_FILL: {
		struct gpanel_rect_t *p = (struct gpanel_rect_t *)addr;

		gfb_fill_rectangle(p->x0, p->y0, p->x1, p->y1, p->color);
		break;
	}
	case GPANEL_CIRCLE: {
		struct gpanel_circle_t *p = (struct gpanel_circle_t *)addr;

		gpanel_draw_circle(p->x, p->y, p->radius, p->color);
		break;
	}
	case GPANEL_IMAGE: {
		struct gpanel_image_t *p = (struct gpanel_image_t *)addr;

		gfb_draw_image(p->x, p->y, p->width, p->height, p->image);
		break;
	}
	case GPANEL_CHAR: {
		struct gpanel_char_t *p = (struct gpanel_char_t *)addr;

		_col = p->x;
		_row = p->y;
		gpanel_draw_char(p->font, p->color, p->background, p->sym);
		break;
	}
	case GPANEL_TEXT: {
		struct gpanel_text_t *p = (struct gpanel_text_t *)addr;

		gpanel_draw_text(p->font, p->color, p->background,
		    p->x, p->y, p->text);
		break;
	}
	case GPANEL_FLIP:		/* GBA extension: present the back page (Mode 5) */
		panel_flip();
		break;
	default:
		return EINVAL;
	}
	return 0;
}
