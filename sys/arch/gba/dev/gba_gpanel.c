/*
 * GBA backend for the portable graphics-panel device, /dev/tft0.
 *
 * The GBA is already in video Mode 3 - a 240x160, 16bpp linear framebuffer
 * at VRAM (0x06000000), the same surface the text console (gba_text.c) draws
 * into.  So this driver is just the portable gpanel software rasterizers
 * (ported from sys/arch/pic32/dev/gpanel.c) over a trivial "write a pixel to
 * VRAM" backend: the existing /dev/tft0 ioctl ABI (sys/sys/gpanel.h), the
 * libgpanel userland library and the share/examples/gpanel programs run on
 * the GBA unchanged.
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

#define GFB_W	240
#define GFB_H	160

/* Portable raster core (below) reads these; keep them in sync with Mode 3. */
int gpanel_width  = GFB_W;
int gpanel_height = GFB_H;

/* Text-drawing cursor for GPANEL_CHAR/TEXT, as in the pic32 driver. */
static int _col, _row;

/* Set while a userland process holds /dev/tft0 (see gba_gpanel.h). */
static int gpanel_active;

int
gba_gfx_active(void)
{
	return gpanel_active;
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
	if ((unsigned)x >= GFB_W || (unsigned)y >= GFB_H)
		return;			/* clamp: a stray store hits OAM/other VRAM */
	VRAM[y * GFB_W + x] = bgr555;
}

void
gba_fb_fill(int x0, int y0, int x1, int y1, unsigned short bgr555)
{
	int t, x, y;

	if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= GFB_W) x1 = GFB_W - 1;
	if (y1 >= GFB_H) y1 = GFB_H - 1;
	for (y = y0; y <= y1; y++) {
		volatile unsigned short *p = VRAM + y * GFB_W + x0;
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
	if (minor(dev) != 0)
		return ENODEV;
	gpanel_active = 1;		/* console/keyboard stop painting VRAM */
	return 0;
}

int
gpanel_close(dev_t dev, int flag, int mode)
{
	/*
	 * Release the panel back to the console.  Clear to black first (the
	 * app's last frame is not the console's to keep, and there is no text
	 * backing store yet - NOTES.graphics 5.1), then drop the owner flag so
	 * console/keyboard drawing resumes; the next console output paints on
	 * the cleared screen.
	 */
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
	default:
		return EINVAL;
	}
	return 0;
}
