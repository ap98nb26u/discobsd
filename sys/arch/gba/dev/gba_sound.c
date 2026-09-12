/*
 * Minimal GBA sound: a console beep on PSG channel 1 (square wave with
 * sweep), roadmap #4. No DMA, no interrupt, no sample data - just a few
 * register writes that start a short self-terminating tone, so it is safe
 * to call from any context (including the tty output path in interrupt
 * context). DirectSound PCM playback via DMA1/2 - the eventual goal of
 * playing an audio file off the root FS or the EverDrive's FAT32 partition
 * - is a separate, larger piece meant to build on this.
 *
 * Register/bit layout per GBATEK (problemkaputt.de/gbatek.htm). The GBA
 * PSG channels only work while the master sound enable (SOUNDCNT_X bit7)
 * is set, and the per-channel registers reset while it is clear, so
 * gba_sound_init() must run before the first gba_beep().
 */
#include <sys/param.h>

#include <machine/gba.h>
#include <gba/dev/gba_sound.h>

/*
 * Beep pitch and duration.
 *   freq field f gives tone rate = 131072 / (2048 - f) Hz.
 *   f = 1885 -> ~804 Hz, a plain terminal-bell pitch.
 *   length field L gives (64 - L)/256 s once the timed flag is set;
 *   L = 32 -> 1/8 s.
 */
#define BEEP_FREQ	1885
#define BEEP_LENGTH	32

void
gba_sound_init(void)
{
	/*
	 * Master sound on, both PSG outputs at full master volume with
	 * channel 1 routed to the left and right mixers, PSG mix ratio
	 * 100%, and the default output bias/sample rate.
	 */
	REG_SOUNDCNT_X = 0x0080;	/* bit7: master sound enable */
	REG_SOUNDCNT_L = 0x1177;	/* L/R master vol 7; ch1 -> L(bit12)+R(bit8) */
	REG_SOUNDCNT_H = 0x0002;	/* PSG volume ratio = 100% */
	REG_SOUNDBIAS  = 0x0200;	/* default bias (32.768 kHz output) */
}

void
gba_beep(void)
{
	REG_SOUND1CNT_L = 0x0000;			/* no frequency sweep */
	/* initial envelope volume 15 (no decay), 50% duty, length field */
	REG_SOUND1CNT_H = 0xF000 | 0x0080 | (BEEP_LENGTH & 0x3F);
	/* restart (bit15) + stop-when-length-expires (bit14) + frequency */
	REG_SOUND1CNT_X = 0x8000 | 0x4000 | (BEEP_FREQ & 0x07FF);
}
