/*
 * /dev/tft0 smoke test: draw a few known shapes at known positions so the
 * framebuffer can be verified (by eye in mGBA, or by dumping VRAM).  Colours
 * are RGB565 (the gpanel convention); the GBA driver converts to BGR555.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/gpanel.h>

#define BLUE    0x001f
#define RED     0xf800
#define GREEN   0x07e0
#define WHITE   0xffff
#define YELLOW  0xffe0

int main()
{
    int xsize = 0, ysize = 0;

    if (gpanel_open("/dev/tft0") < 0) {
        printf("cannot open /dev/tft0\n");
        return 1;
    }
    gpanel_clear(BLUE, &xsize, &ysize);
    printf("tft0 %dx%d\n", xsize, ysize);

    gpanel_fill(RED,    10, 10,  60, 40);   /* filled red box      */
    gpanel_rect(GREEN,  70, 10, 120, 40);   /* green frame         */
    gpanel_line(WHITE,  10, 60, 230, 60);   /* white horizontal    */
    gpanel_circle(YELLOW, 180, 100, 30);    /* yellow circle       */

    /* Hold the picture on screen (Ctrl-C to exit). */
    for (;;)        /* block until a signal (A+B+START escape, or ^C) */
        pause();
    return 0;
}
