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

#endif /* KERNEL */

#endif /* _GBA_DEV_TEXT_H_ */
