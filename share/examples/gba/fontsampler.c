/*
 * Show several gpanel fonts, one sample line each, on /dev/tft0.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/gpanel.h>

extern const struct gpanel_font_t font_lucidasans15;
extern const struct gpanel_font_t font_lucidasans9;
extern const struct gpanel_font_t font_verdana7;
extern const struct gpanel_font_t font_6x9;
extern const struct gpanel_font_t font_5x7;
extern const struct gpanel_font_t font_digits20;

#define RGB(r,g,b)  ((r)<<11 | (g)<<5 | (b))
#define BLACK   0
#define WHITE   RGB(31,63,31)
#define YELLOW  RGB(31,63,0)
#define CYAN    RGB(0,63,31)
#define GREEN   RGB(0,63,0)
#define MAGENTA RGB(31,0,31)

static int y;

static void line(const struct gpanel_font_t *f, int color, const char *s)
{
    gpanel_text(f, color, BLACK, 2, y, s);
    y += f->height + 2;
}

int main()
{
    int xsize = 0, ysize = 0;

    if (gpanel_open("/dev/tft0") < 0) {
        printf("cannot open /dev/tft0\n");
        return 1;
    }
    gpanel_clear(BLACK, &xsize, &ysize);

    y = 1;
    line(&font_6x9,         YELLOW,  "GBA gpanel fonts:");
    line(&font_lucidasans15, WHITE,  "Lucida15 Ag0");
    line(&font_lucidasans9,  CYAN,   "Lucida9 Abcdef 01234");
    line(&font_verdana7,     GREEN,  "Verdana7 The quick brown fox 0123");
    line(&font_6x9,          WHITE,  "6x9 ABCabc 0123456789");
    line(&font_5x7,          MAGENTA,"5x7 ABCDEabcde 0123456789");
    line(&font_digits20,     YELLOW, "0123456789");

    for (;;)        /* block until a signal (A+B+START escape, or ^C) */
        pause();
    return 0;
}
