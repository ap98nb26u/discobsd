#ifndef _GBA_DEV_GPANEL_H_
#define _GBA_DEV_GPANEL_H_

#ifdef KERNEL

/*
 * GBA backend for the portable gpanel graphics device (/dev/tft0-2).
 * See sys/sys/gpanel.h for the ioctl ABI and NOTES.graphics for the design.
 */
struct uio;
int gpanel_open(dev_t dev, int flag, int mode);
int gpanel_close(dev_t dev, int flag, int mode);
int gpanel_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);

/*
 * True while a userland process holds a /dev/tft* panel open.  The text
 * console, its cursor blink, the idle screen-blank and the on-screen
 * keyboard draw nothing to VRAM while this is set, so the panel has a
 * single owner; gpanel_close() then repaints the console.
 */
int gba_gfx_active(void);

/*
 * Kernel-internal framebuffer primitives for the current panel surface
 * (Mode 3 240x160, or the Mode 5 160x128 back page).  Colours here are
 * already BGR555 (native GBA); the gpanel ioctl path converts RGB565 ->
 * BGR555 first.
 */
void gba_fb_pixel(int x, int y, unsigned short bgr555);
void gba_fb_fill(int x0, int y0, int x1, int y1, unsigned short bgr555);

#endif /* KERNEL */

#endif /* _GBA_DEV_GPANEL_H_ */
