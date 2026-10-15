#ifndef _GBA_DEV_GPANEL_H_
#define _GBA_DEV_GPANEL_H_

#ifdef KERNEL

/*
 * GBA backend for the portable gpanel graphics device (/dev/tft0).
 * See sys/sys/gpanel.h for the ioctl ABI and NOTES.graphics for the design.
 */
struct uio;
int gpanel_open(dev_t dev, int flag, int mode);
int gpanel_close(dev_t dev, int flag, int mode);
int gpanel_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);

/*
 * True while a userland process holds /dev/tft0 open.  The console cursor
 * blink, the idle screen-blank and the on-screen keyboard (all of which
 * paint the shared Mode 3 framebuffer from idle()) must not draw while this
 * is set, or they scribble over the graphics.  This interim guard is retired
 * by the Phase 4 convergence (NOTES.graphics 5.1), which gives the frame
 * buffer a single owner.
 */
int gba_gfx_active(void);

/*
 * Kernel-internal framebuffer primitives (Mode 3, 240x160, BGR555).  Exposed
 * so the text console / swkbd can later render through this one owner instead
 * of poking VRAM directly (NOTES.graphics 5.1).  Colours here are already
 * BGR555 (native GBA); the gpanel ioctl path converts RGB565 -> BGR555 first.
 */
void gba_fb_pixel(int x, int y, unsigned short bgr555);
void gba_fb_fill(int x0, int y0, int x1, int y1, unsigned short bgr555);

#endif /* KERNEL */

#endif /* _GBA_DEV_GPANEL_H_ */
