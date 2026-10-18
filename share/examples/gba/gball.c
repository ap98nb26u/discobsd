/*
 * Interactive /dev/tft0 demo: a ball you steer with the D-pad, drawn with the
 * gpanel library.  Reads the GBA buttons DIRECTLY from REG_KEYINPUT (the GBA
 * has no memory protection, so a user program may read I/O registers) rather
 * than the tty - that is the natural input model for a full-screen graphics
 * program here, and it leaves the tty alone (no characters leak to the shell).
 * The A+B+START / A+B+R escape chords (handled in the kernel's swkbd_poll)
 * still quit it.  Draws are timed to vblank (busy-wait on VCOUNT) to limit
 * tearing on the single Mode-3 framebuffer.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/gpanel.h>

#define VCOUNT   (*(volatile unsigned short *)0x04000006)
#define KEYINPUT (*(volatile unsigned short *)0x04000130)
#define K_A 0x001
#define K_RIGHT 0x010
#define K_LEFT  0x020
#define K_UP    0x040
#define K_DOWN  0x080

#define RGB(r,g,b)  ((r)<<11 | (g)<<5 | (b))
#define BLACK 0
#define WHITE RGB(31,63,31)
static const int palette[] = {
    RGB(31,0,0), RGB(0,63,0), RGB(0,0,31), RGB(31,63,0), RGB(0,63,31), RGB(31,0,31)
};

int main()
{
    int xs = 0, ys = 0;
    int x = 120, y = 90, px = x, py = y, vx = 2, vy = 1, r = 7;
    int ci = 1, a_prev = 0, top = 18;

    if (gpanel_open("/dev/tft0") < 0) {
        printf("cannot open /dev/tft0\n");
        return 1;
    }
    gpanel_clear(BLACK, &xs, &ys);

    for (;;) {
        unsigned k;

        while (VCOUNT < 160)		/* wait for vblank before drawing */
            ;
        k = (unsigned)(~KEYINPUT) & 0x03FF;

        if (k & K_LEFT)  vx--;
        if (k & K_RIGHT) vx++;
        if (k & K_UP)    vy--;
        if (k & K_DOWN)  vy++;
        if ((k & K_A) && !a_prev)
            ci = (ci + 1) % (int)(sizeof palette / sizeof palette[0]);
        a_prev = (k & K_A) != 0;

        if (vx > 6) vx = 6; if (vx < -6) vx = -6;
        if (vy > 6) vy = 6; if (vy < -6) vy = -6;

        gpanel_fill(BLACK, px - r, py - r, px + r, py + r);	/* erase old */

        x += vx; y += vy;
        if (x < r)        { x = r;        vx = -vx; }
        if (x > xs-1 - r) { x = xs-1 - r; vx = -vx; }
        if (y < top + r)  { y = top + r;  vy = -vy; }
        if (y > ys-1 - r) { y = ys-1 - r; vy = -vy; }

        gpanel_fill(palette[ci], x - r, y - r, x + r, y + r);	/* draw new */
        px = x; py = y;

        while (VCOUNT >= 160)		/* let vblank pass before next wait */
            ;
    }
    return 0;
}
