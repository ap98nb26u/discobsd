/*
 * Double-buffered ball demo, INCREMENTAL version (Mode 5).  Same page-flipping
 * as gball5, but instead of clearing the whole back page each frame it erases
 * only this page's previous ball and draws the new one - ~450 px/frame instead
 * of ~20,500.  With two pages the back page is two frames stale, so the
 * previous ball position is tracked per page (bx[]/by[]).  Both pages are
 * primed once at startup so the incremental erase has a correct backdrop.
 *
 * Usage: gball5i [device]  (default /dev/tft1 = scaled; /dev/tft2 = 1:1)
 * Input is read straight from the GBA buttons; A+B+START / A+B+R quit (kernel).
 */
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/gpanel.h>

extern int _gpanel_fd;			/* libgpanel's open device fd */

#define KEYINPUT (*(volatile unsigned short *)0x04000130)
#define K_A 0x001
#define K_RIGHT 0x010
#define K_LEFT  0x020
#define K_UP    0x040
#define K_DOWN  0x080

#define RGB(r,g,b)  ((r)<<11 | (g)<<5 | (b))
#define BLACK 0
static const int palette[] = {
    RGB(31,0,0), RGB(0,63,0), RGB(0,0,31), RGB(31,63,0), RGB(0,63,31), RGB(31,0,31)
};

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/tft1";
    int xs = 0, ys = 0;
    int x = 80, y = 64, vx = 2, vy = 1, r = 7;
    int ci = 1, a_prev = 0;
    int page = 0, bx[2], by[2], i;

    if (gpanel_open(dev) < 0) {
        printf("cannot open %s\n", dev);
        return 1;
    }
    gpanel_clear(BLACK, &xs, &ys);		/* xs=160, ys=128 */

    /* Prime BOTH pages: black background + ball at the start position, so the
       per-page incremental erase below always has a correct backdrop. */
    for (i = 0; i < 2; i++) {
        gpanel_fill(BLACK, 0, 0, xs-1, ys-1);
        gpanel_fill(palette[ci], x - r, y - r, x + r, y + r);
        ioctl(_gpanel_fd, GPANEL_FLIP, 0);
    }
    bx[0] = bx[1] = x; by[0] = by[1] = y;

    for (;;) {
        unsigned k = (unsigned)(~KEYINPUT) & 0x03FF;

        if (k & K_LEFT)  vx--;
        if (k & K_RIGHT) vx++;
        if (k & K_UP)    vy--;
        if (k & K_DOWN)  vy++;
        if ((k & K_A) && !a_prev)
            ci = (ci + 1) % (int)(sizeof palette / sizeof palette[0]);
        a_prev = (k & K_A) != 0;
        if (vx > 6) vx = 6; if (vx < -6) vx = -6;
        if (vy > 6) vy = 6; if (vy < -6) vy = -6;

        /* Erase only this page's old ball (it was drawn two frames ago). */
        gpanel_fill(BLACK, bx[page]-r, by[page]-r, bx[page]+r, by[page]+r);

        x += vx; y += vy;
        if (x < r)        { x = r;        vx = -vx; }
        if (x > xs-1 - r) { x = xs-1 - r; vx = -vx; }
        if (y < r)        { y = r;        vy = -vy; }
        if (y > ys-1 - r) { y = ys-1 - r; vy = -vy; }

        gpanel_fill(palette[ci], x - r, y - r, x + r, y + r);
        bx[page] = x; by[page] = y;

        ioctl(_gpanel_fd, GPANEL_FLIP, 0);
        page ^= 1;			/* toggle in lockstep with the driver */
    }
    return 0;
}
