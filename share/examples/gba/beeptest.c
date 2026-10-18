/*
 * beeptest - PSG sound diagnostic.  Plays N short tones on PSG channel 1
 * and prints each tone's number as it starts, so you can tell which ones
 * were inaudible.  It was used to find that, on real hardware, the first
 * two PSG triggers after the boot-time sound enable make no sound, which
 * the kernel now absorbs (gba_sound_prime() in sys/arch/gba/dev/gba_sound.c).
 * Every tone should be heard.
 *
 *   beeptest [count [tone_ms [gap_ms [freq_hz]]]]
 *   defaults:  10       100      400      1047
 *
 * Same two rules as tetris's beep(): per-channel SOUND1CNT_* only, and the
 * length-stop flag is always set (tone_ms is capped at 250).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SOUND1CNT_L (*(volatile unsigned short *)0x04000060)
#define SOUND1CNT_H (*(volatile unsigned short *)0x04000062)
#define SOUND1CNT_X (*(volatile unsigned short *)0x04000064)

int
main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 10;
	int ms = argc > 2 ? atoi(argv[2]) : 100;
	int gap = argc > 3 ? atoi(argv[3]) : 400;
	int frq = argc > 4 ? atoi(argv[4]) : 1047;
	int f, len, i;

	if (ms > 250)
		ms = 250;
	if (ms < 4)
		ms = 4;
	if (frq < 65)
		frq = 65;
	f = 2048 - 131072 / frq;
	if (f < 0)
		f = 0;
	if (f > 0x7ff)
		f = 0x7ff;
	len = 64 - (ms * 256) / 1000;
	if (len < 0)
		len = 0;
	if (len > 63)
		len = 63;

	printf("beeptest: %d x %dms, gap %dms, %dHz\n", n, ms, gap, frq);
	for (i = 1; i <= n; i++) {
		printf("%d ", i);
		fflush(stdout);
		SOUND1CNT_L = 0x0000;
		SOUND1CNT_H = 0xF000 | 0x0080 | (len & 0x3F);
		SOUND1CNT_X = 0x8000 | 0x4000 | (f & 0x07FF);
		usleep((ms + gap) * 1000);
	}
	printf("\n");
	return 0;
}
