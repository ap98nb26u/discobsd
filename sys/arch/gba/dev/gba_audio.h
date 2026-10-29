#ifndef _GBA_DEV_AUDIO_H_
#define _GBA_DEV_AUDIO_H_

#include <sys/ioctl.h>

/*
 * GBA DirectSound PCM playback - /dev/audio.
 *
 * Writes are raw 8-bit SIGNED PCM samples: mono, or interleaved L,R for the
 * stereo minors. Hardware Timer1 clocks the sample rate; DMA1 (and DMA2 for
 * stereo) feed the DirectSound FIFOs from a thick IWRAM ring that write()
 * fills and audio_tick() (Timer0 interrupt) tracks and wraps, so an SD read
 * stalling the CPU does not immediately starve the FIFO. Default rate 8192 Hz.
 *
 * ioctls carry an int sample rate in Hz:
 *   AUDIO_SETRATE  set the playback sample rate (clamped, takes effect now)
 *   AUDIO_GETRATE  read the current sample rate
 */
#define AUDIO_SETRATE	_IOW('A', 1, int)
#define AUDIO_GETRATE	_IOR('A', 2, int)

#define AUDIO_RATE_MIN	2000
#define AUDIO_RATE_MAX	24000
#define AUDIO_RATE_DEF	8192

#ifdef KERNEL
void audio_init(void);		/* one-time hardware/state setup, at boot */
void audio_tick(void);		/* ring->FIFO bookkeeping, from Timer0 ISR */
void audio_suspend(void);	/* pause sample clock (SD DMA, swap copy) */
void audio_resume(void);	/* resume it afterwards */

int audioopen(dev_t dev, int flag, int mode);
int audioclose(dev_t dev, int flag, int mode);
int audiowrite(dev_t dev, struct uio *uio, int flag);
int audioioctl(dev_t dev, u_int cmd, caddr_t data, int flag);
#endif /* KERNEL */

#endif /* _GBA_DEV_AUDIO_H_ */
