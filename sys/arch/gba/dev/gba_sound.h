#ifndef _GBA_DEV_SOUND_H_
#define _GBA_DEV_SOUND_H_

#ifdef KERNEL

/*
 * Minimal GBA sound (roadmap #4). Only the console beep so far; the PSG
 * square-wave channel drives it, no DMA. DirectSound PCM playback (from a
 * file on the root FS or the EverDrive FAT32 partition) is intended to
 * build on this later.
 */
void gba_sound_init(void);	/* enable the sound hardware once, at boot */
void gba_beep(void);		/* short console-bell tone (PSG channel 1) */

#endif /* KERNEL */

#endif /* _GBA_DEV_SOUND_H_ */
