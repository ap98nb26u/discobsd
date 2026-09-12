#ifndef _GBA_DEV_TEXT_H_
#define _GBA_DEV_TEXT_H_

#ifdef KERNEL

/*
 * Console/keyboard colours (BGR555, bit15 unused). The text console and the
 * on-screen keyboard are white on black; kernel messages (the printf path,
 * via gtxt_msgcolor()) are shown in green, matching the raw-framebuffer /
 * phosphor-console convention other Unixes use for kernel-to-screen output
 * (roadmap #8).
 */
#define GTXT_BLACK	0x0000		/* R=0  G=0  B=0  */
#define GTXT_GREEN	0x03E0		/* R=0  G=31 B=0  (full green) */
#define GTXT_WHITE	0x7FFF		/* R=31 G=31 B=31 */

/*
 * Minimal scrolling text console on video Mode 3 (240x160 16bpp bitmap).
 * Ported from Adrian O'Grady's fivemouse.com GBA text demo/library.
 */
void gtxt_init(unsigned short fg, unsigned short bg);
void gtxt_cls(void);
void gtxt_putc(char c);

/* Overlay support for the on-screen keyboard (swkbd.c). */
void gtxt_draw_cell(char c, int col, int row, unsigned short fg,
	unsigned short bg);
void gtxt_reserve_bottom(int pixels);
int gtxt_cols(void);
int gtxt_rows(void);
void gtxt_cursor(int on);

/* Screen blanking (console screen-saver); see gtxt_blank() in gba_text.c. */
void gtxt_blank(int on);
int gtxt_is_blanked(void);

/*
 * Render subsequent glyphs in the kernel-message colour (green) while on,
 * back to the console's normal colour while off. Used only by the kernel
 * printf path (uartputc_kmsg) to tint kernel-to-screen output.
 */
void gtxt_msgcolor(int on);

#endif /* KERNEL */

#endif /* _GBA_DEV_TEXT_H_ */
