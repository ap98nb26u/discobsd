/*
 * Input diagnostic: show which GBA buttons the hardware reports as pressed,
 * live, on /dev/tft0.  Ignores SIGINT so the A+B+START escape chord does NOT
 * kill it - the point is to SEE whether A, B and START all register together
 * (if they do not, the chord can never fire; that is an input/keyboard issue,
 * not a kernel one).  Quit with the A+B+R chord (SIGKILL).  Reads REG_KEYINPUT
 * (0x04000130) directly; the GBA has no memory protection, so a user program
 * may read I/O registers.
 */
#include <stdio.h>
#include <signal.h>
#include <sys/gpanel.h>

extern const struct gpanel_font_t font_6x9;

#define RGB(r,g,b)  ((r)<<11 | (g)<<5 | (b))
#define BLACK   0
#define GREEN   RGB(0,63,0)
#define DIM     RGB(6,12,6)
#define WHITE   RGB(31,63,31)

/* key bit (REG_KEYINPUT: 0 = pressed) -> label, in bit order 0..9 */
static const char *lab[10] = {
    "A","B","SEL","STA","Rt","Lf","Up","Dn","R","L"
};

int main()
{
    volatile unsigned short *keyinput = (volatile unsigned short *)0x04000130;
    int xs = 0, ys = 0, i;

    signal(SIGINT, SIG_IGN);        /* survive the escape chord */
    if (gpanel_open("/dev/tft0") < 0) {
        printf("no tft0\n");
        return 1;
    }
    gpanel_clear(BLACK, &xs, &ys);
    gpanel_text(&font_6x9, WHITE, BLACK, 4, 2,
        "KEYINPUT test - press buttons");
    gpanel_text(&font_6x9, WHITE, BLACK, 4, 140,
        "A+B+STA all green = chord sent");
    gpanel_text(&font_6x9, WHITE, BLACK, 4, 150,
        "A+B+R to quit");

    for (;;) {
        unsigned pressed = (~*keyinput) & 0x03FF;   /* 1 = pressed */

        for (i = 0; i < 10; i++) {
            int on = (pressed >> i) & 1;
            int x = 6 + (i % 5) * 46;
            int y = 40 + (i / 5) * 40;
            gpanel_text(&font_6x9, on ? GREEN : DIM, BLACK, x, y, lab[i]);
        }
    }
    return 0;
}
