#ifndef _GBA_DEV_TEXT_H_
#define _GBA_DEV_TEXT_H_

#ifdef KERNEL

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

#endif /* KERNEL */

#endif /* _GBA_DEV_TEXT_H_ */
