#ifndef _GBA_DEV_AUDIO_H_
#define _GBA_DEV_AUDIO_H_

#include <sys/ioctl.h>

/*
 * GBA DirectSound PCM playback - /dev/audio.
 *
 * Writes are raw 8-bit SIGNED mono PCM samples. The driver streams them to
 * DirectSound channel A: hardware Timer1 clocks the sample rate, DMA1 feeds
 * the 8-bit FIFO, and a thick software ring (refilled from the Timer0
 * interrupt) decouples playback from the writer so an SD read stalling the
 * CPU does not immediately starve the FIFO. Default rate 8192 Hz.
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
void audio_suspend(void);	/* pause sample clock across an SD transfer (ed.c) */
void audio_resume(void);	/* resume it afterwards */

int audioopen(dev_t dev, int flag, int mode);
int audioclose(dev_t dev, int flag, int mode);
int audiowrite(dev_t dev, struct uio *uio, int flag);
int audioioctl(dev_t dev, u_int cmd, caddr_t data, int flag);
#endif /* KERNEL */

#endif /* _GBA_DEV_AUDIO_H_ */
