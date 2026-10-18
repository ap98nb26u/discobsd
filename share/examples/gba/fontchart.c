/*
 * Draw a font's character list (printable ASCII 32..127) on /dev/tft0 as a
 * 16-column grid, using the fixed 6x9 font.  Colours are RGB565.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/gpanel.h>

extern const struct gpanel_font_t font_6x9;

#define RGB(r,g,b)  ((r)<<11 | (g)<<5 | (b))
#define BLACK   0
#define WHITE   RGB(31,63,31)
#define CYAN    RGB(0,63,31)
#define YELLOW  RGB(31,63,0)

int main()
{
    int xsize = 0, ysize = 0, c, col, row, x, y;
    char title[40];

    if (gpanel_open("/dev/tft0") < 0) {
        printf("cannot open /dev/tft0\n");
        return 1;
    }
    gpanel_clear(BLACK, &xsize, &ysize);

    sprintf(title, "font %s  ASCII 32-127", font_6x9.name);
    gpanel_text(&font_6x9, YELLOW, BLACK, 4, 2, title);

    /* 16 columns x 6 rows = 96 glyphs (0x20..0x7f). */
    for (c = 32; c < 128; c++) {
        col = (c - 32) & 15;
        row = (c - 32) >> 4;
        x = 8 + col * 14;
        y = 20 + row * 20;
        /* code in cyan above the glyph in white */
        {
            char hex[4];
            sprintf(hex, "%02x", c);
            gpanel_text(&font_6x9, CYAN, BLACK, x, y, hex);
        }
        gpanel_char(&font_6x9, WHITE, BLACK, x + 2, y + 10, c);
    }

    for (;;)            /* block until a signal (A+B+START escape, or ^C) */
        pause();
    return 0;
}
