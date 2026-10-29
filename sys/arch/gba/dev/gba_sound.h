#ifndef _GBA_DEV_SOUND_H_
#define _GBA_DEV_SOUND_H_

#ifdef KERNEL

/*
 * Minimal GBA sound (roadmap #4): the console beep, driven by the PSG
 * square-wave channel, no DMA. DirectSound PCM playback (/dev/audio) is a
 * separate driver, gba_audio.h.
 */
void gba_sound_init(void);	/* enable the sound hardware once, at boot */
void gba_sound_prime(void);	/* absorb the post-enable dead triggers */
void gba_beep(void);		/* short console-bell tone (PSG channel 1) */

#endif /* KERNEL */

#endif /* _GBA_DEV_SOUND_H_ */
