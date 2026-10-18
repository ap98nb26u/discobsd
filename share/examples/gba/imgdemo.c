/*
 * GPANEL_IMAGE demo: build a 16-bit RGB565 gradient bitmap and blit it
 * centred on /dev/tft0, with a frame and a caption.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/gpanel.h>

#define IW 120
#define IH 90
static unsigned short img[IW * IH];

extern const struct gpanel_font_t font_6x9;

#define RGB(r,g,b)  ((r)<<11 | (g)<<5 | (b))

int main()
{
    int xsize = 0, ysize = 0, x, y, ox, oy;

    if (gpanel_open("/dev/tft0") < 0) {
        printf("cannot open /dev/tft0\n");
        return 1;
    }
    gpanel_clear(RGB(0,0,8), &xsize, &ysize);

    /* red ramps left->right, green top->bottom, blue on the diagonal */
    for (y = 0; y < IH; y++)
        for (x = 0; x < IW; x++)
            img[y*IW + x] = RGB(x*31/(IW-1), y*63/(IH-1), (x+y)*31/(IW+IH-2));

    ox = (xsize - IW) / 2;
    oy = (ysize - IH) / 2;
    gpanel_image(ox, oy, IW, IH, img);
    gpanel_rect(RGB(31,63,31), ox-1, oy-1, ox+IW, oy+IH);   /* white frame */
    gpanel_text(&font_6x9, RGB(31,63,31), RGB(0,0,8), 2, 2,
        "GPANEL_IMAGE 120x90 RGB565 gradient");

    for (;;)        /* block until a signal (A+B+START escape, or ^C) */
        pause();
    return 0;
}
