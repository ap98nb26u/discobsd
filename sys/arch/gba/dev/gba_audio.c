/*
 * GBA DirectSound PCM playback driver - /dev/audio.
 *
 * Plays raw 8-bit SIGNED mono PCM.  The hard parts of GBA PCM are all about
 * who keeps the sound FIFO fed, because the sound DMA has no "wrap" and no
 * completion interrupt (in FIFO mode the word count is forced to 4 and the
 * channel repeats forever, advancing its source), and this port has no
 * hardware timer free to count samples (Timer0=hardclock, Timer1=the sample
 * clock we need here, Timer2/Timer3=the clock catch-up counter).  So:
 *
 *   - Timer1 runs at the sample rate (one overflow = one sample consumed
 *     from FIFO A).  DirectSound A is selected onto Timer1 in SOUNDCNT_H.
 *   - DMA1 is a sound-FIFO channel (32-bit, repeat, start-on-FIFO) whose
 *     source address advances through one contiguous ring.  Because the ring
 *     is contiguous the DMA free-runs across the whole of it with no
 *     per-sample restart, so even a long IME=0 window (an SD transfer) cannot
 *     glitch playback as long as the ring still holds data ahead of the play
 *     point.
 *   - The DMA must be repointed back to the ring start when it reaches the
 *     end (its one wrap).  audio_tick(), called from the Timer0 interrupt,
 *     advances a software play position from elapsed real time (the same
 *     IME-independent Timer3 the clock catch-up uses, so it tracks the DMA's
 *     real consumption rate exactly) and does that one restart.
 *   - A write() blocks when the ring is full and is woken as playback drains
 *     it; playback auto-stops when the ring empties (end of stream).
 *
 * Re-enabling the sound DMA at each wrap is itself a tiny audio discontinuity
 * on this hardware/emulator, independent of the source address, so the only
 * lever on the resulting click is how OFTEN we wrap.  The ring is therefore
 * sized as large as the kernel's IWRAM .bss headroom allows (1.125 s), which
 * costs no swap.  Two things were tried to do better and did NOT pan out,
 * recorded here so they are not re-attempted blindly:
 *   - A 32 KB EWRAM ring (short clips wrap-free): it permanently shrank the
 *     mrams swap area enough to OOM 4-stage pipelines, and the kernel stack
 *     lives at the top of EWRAM so the buffer had to dodge it - reverted.
 *   - A VBlank-synced double buffer (restart on a fixed 60 Hz event instead
 *     of an arbitrary tick): restarting the DMA 60x/s makes the per-restart
 *     click a continuous ~60 Hz tremolo/hum - far WORSE, since the restart,
 *     not its phase, is the click.  Fewer restarts is the only win, and that
 *     means a bigger buffer, i.e. the swap trade-off above.
 *
 * Register/bit layout per GBATEK (problemkaputt.de/gbatek.htm).
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/stdint.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <sys/ioctl.h>

#include <machine/gba.h>
#include <gba/dev/gba_audio.h>

/* DMA channel 1 (sound FIFO A).  SAD/DAD are 32-bit, CNT split 16+16. */
#define REG_DMA1SAD	(*(volatile uint32_t *)(REG_BASE+0x00BC))
#define REG_DMA1DAD	(*(volatile uint32_t *)(REG_BASE+0x00C0))
#define REG_DMA1CNT_L	(*(volatile uint16_t *)(REG_BASE+0x00C4))
#define REG_DMA1CNT_H	(*(volatile uint16_t *)(REG_BASE+0x00C6))
#define FIFO_A		(REG_BASE+0x00A0)

/*
 * DMA1 control for a sound FIFO channel:
 *   0x8000 enable | 0x3000 start-timing = special (sound FIFO)
 * | 0x0400 32-bit units | 0x0200 repeat.
 * Destination control is forced to the FIFO in this mode; source increments.
 */
#define DMA1_SOUND	0xB600

/*
 * SOUNDCNT_H bits to route DirectSound A to the FIFO on Timer1:
 *   0x0004 DSound A volume 100% | 0x0100 enable R | 0x0200 enable L
 * | 0x0400 timer select = Timer1 | 0x0002 PSG mix 100% (keep the beep alive).
 * 0x0800 is the FIFO-A reset, pulsed only when (re)starting.
 */
#define SNDH_DSA	(0x0004 | 0x0100 | 0x0200 | 0x0400 | 0x0002)
#define SNDH_FIFO_RESET	0x0800

/*
 * The ring.  ASAMPLES must be a multiple of 16 (the DMA moves 32-bit units =
 * 4 samples, and the wrap math rounds to the 16-sample FIFO refill).  Sized
 * as large as the GBAED build's IWRAM .bss headroom allows (~2.5 KB spare):
 * 9216 samples = 1.125 s at 8192 Hz.  AGUARD is a mirror of the ring's first
 * AGUARD samples (the producer mirrors writes that land there) covering the
 * DMA's over-read past the ring end before audio_tick's Timer0-driven wrap
 * rewind is serviced (an IME=0 window can delay it); 1024 = 125 ms.
 */
#define ASAMPLES	9216		/* 1.125 s at 8192 Hz; multiple of 16 */
#define AGUARD		1024		/* 125 ms over-read mirror */
#define APREBUF		(ASAMPLES / 2)	/* fill this far before starting */

static signed char abuf[ASAMPLES + AGUARD];

static int	audio_rate = AUDIO_RATE_DEF;
static int	audio_open;		/* single-opener guard */

/* Ring state.  Shared with audio_tick() (interrupt); guard with splhigh(). */
static volatile int audio_playing;	/* DMA/Timer1 running */
static volatile unsigned a_wr;		/* producer index into abuf[0..ASAMPLES) */
static volatile unsigned a_play;	/* play index  into abuf[0..ASAMPLES) */
static volatile unsigned a_used;	/* samples written, not yet played */

/* Fractional play-position accumulator (see audio_tick). */
static unsigned a_acc;
static uint16_t a_last_tm3;

/* Timer0's reload delta = timer counts per intended hardclock tick, and the
 * timer input clock (CPU/1024).  samples-per-tick = rate*TICKS_CNT/TCLK. */
#define TCLK		(16777216 / 1024)		/* 16384 Hz */
#define TICKS_CNT	(16777216 / 1024 / HZ)		/* 163 at HZ=100 */

static void audio_hw_start(void);
static void audio_hw_stop(void);

void
audio_init(void)
{
	audio_rate = AUDIO_RATE_DEF;
	audio_open = 0;
	audio_playing = 0;
	a_wr = a_play = a_used = 0;
	a_acc = 0;
	/* SOUNDCNT_X master-enable is set by gba_sound_init() at boot. */
}

/* Program Timer1 to overflow once per sample at the current rate. */
static void
audio_set_timer(void)
{
	unsigned reload = 65536 - (16777216 / audio_rate);

	REG_TM1CNT_H = 0;			/* stop before reprogramming */
	REG_TM1CNT_L = reload;
	REG_TM1CNT_H = 0x0080;			/* enable, /1 prescaler, no IRQ */
}

/*
 * Start the hardware playing from the current a_play. Caller holds splhigh.
 *
 * Order matters to avoid a startup click: reset+enable the DSound A path,
 * then enable the DMA so it primes (fills) the FIFO from valid data, and
 * only THEN start Timer1 so the first sample clocked out of the FIFO is real
 * audio rather than an underrun of the just-reset (empty) FIFO.
 */
static void
audio_hw_start(void)
{
	unsigned off = a_play & ~3u;		/* DMA source must be 32-bit aligned */

	REG_TM1CNT_H = 0;			/* sample clock stopped while we set up */
	REG_DMA1CNT_H = 0;			/* make sure channel is off */
	REG_SOUNDCNT_H = SNDH_DSA | SNDH_FIFO_RESET;	/* enable DSA, clear FIFO A */

	REG_DMA1SAD = (uint32_t)&abuf[off];
	REG_DMA1DAD = (uint32_t)FIFO_A;
	REG_DMA1CNT_L = 0;			/* ignored in FIFO mode */
	REG_DMA1CNT_H = DMA1_SOUND;		/* enable: primes the FIFO now */

	a_acc = 0;
	a_last_tm3 = REG_TM3CNT_L;
	audio_playing = 1;

	audio_set_timer();			/* start consuming - last */
}

/*
 * Stop the DMA and the sample clock. Caller holds splhigh (or is the ISR).
 * Stop the sample clock FIRST so the FIFO cannot be clocked (and underrun
 * into a looping tone) during teardown, then disable the DMA and reset the
 * now-idle FIFO so no stale samples remain for the next start.
 */
static void
audio_hw_stop(void)
{
	REG_TM1CNT_H = 0;			/* stop the sample clock first */
	REG_DMA1CNT_H = 0;			/* disable sound DMA */
	REG_SOUNDCNT_H = SNDH_FIFO_RESET;	/* clear FIFO A, DSound A off */
	REG_SOUNDCNT_H = 0x0002;		/* back to PSG-only (beep) */
	audio_playing = 0;
}

/*
 * Reposition the free-running DMA source to the ring start when the play
 * point has wrapped.  Caller holds splhigh / is the ISR.  Keeps the FIFO
 * flowing (no reset) so the join is as clean as it can be; the guard copy
 * means the samples the DMA already over-read past the end were valid.
 *
 * The DMA reads AHEAD of the audio OUTPUT by however many samples sit in the
 * FIFO (it tops the 8-entry/32-sample FIFO up in 16-sample bursts, so ~16..32
 * in flight).  a_play tracks the OUTPUT position, so restarting at abuf[a_play]
 * would re-read and duplicate what the FIFO already holds; restart past it by
 * the phase-accurate FIFO occupancy abuf[(a_play & ~15) + 32] instead.  (A
 * residual click remains because re-enabling the DMA is itself a glitch - see
 * the file header - but this keeps it as small as a single wrap can be.)
 */
static void
audio_dma_rewind(void)
{
	unsigned off = (a_play & ~15u) + 32;	/* a_play + FIFO occupancy, 4-aligned */

	if (off >= ASAMPLES)			/* keep the source inside the ring */
		off -= ASAMPLES;

	REG_DMA1CNT_H = 0;
	REG_DMA1SAD = (uint32_t)&abuf[off];
	REG_DMA1CNT_H = DMA1_SOUND;
}

/*
 * Called from the Timer0 interrupt (gba_do_schedule) every tick.  Advances
 * the play position by however much real time has elapsed (Timer3 is the
 * IME-independent tick counter), wraps the DMA when needed, stops at end of
 * stream, and wakes a writer blocked on a full ring.
 */
void
audio_tick(void)
{
	uint16_t now;
	unsigned e, nsamp;

	if (!audio_playing)
		return;

	now = REG_TM3CNT_L;
	e = (uint16_t)(now - a_last_tm3);
	if (e == 0)
		e = 1;
	if (e > 64)				/* a huge gap already glitched audio */
		e = 64;
	a_last_tm3 = now;

	/* samples elapsed = e * rate * TICKS_CNT / TCLK, with carry. */
	a_acc += e * (unsigned)audio_rate * TICKS_CNT;
	nsamp = a_acc / TCLK;
	a_acc -= nsamp * TCLK;
	if (nsamp == 0)
		return;

	if (nsamp >= a_used) {			/* ran past the written data */
		a_play = a_wr;
		a_used = 0;
		audio_hw_stop();		/* end of stream: idle the hw */
		wakeup((caddr_t)&a_used);
		return;
	}

	a_used -= nsamp;
	a_play += nsamp;
	if (a_play >= ASAMPLES) {		/* one wrap: repoint the DMA */
		a_play -= ASAMPLES;
		audio_dma_rewind();
	}
	wakeup((caddr_t)&a_used);		/* room freed for the writer */
}

int
audioopen(dev_t dev, int flag, int mode)
{
	/*
	 * minor 0 = default 8192 Hz (/dev/audio), minor 1 = 16384 Hz
	 * (/dev/audio16): a convenience so a plain `cat file > dev` can pick a
	 * rate without a tool. AUDIO_SETRATE still overrides either one.  Both
	 * rates divide the 16.78 MHz timer clock exactly (reload 0xF800 / 0xFC00),
	 * and 16384 is exactly twice the default and half the GBA's native
	 * 32768 Hz output - unlike a round 16000, which the timer cannot hit.
	 */
	switch (minor(dev)) {
	case 0: audio_rate = AUDIO_RATE_DEF; break;
	case 1: audio_rate = 16384; break;
	default: return ENXIO;
	}
	if (audio_open)
		return EBUSY;
	audio_open = 1;
	a_wr = a_play = a_used = 0;
	a_acc = 0;
	audio_playing = 0;
	bzero((caddr_t)abuf, sizeof(abuf));	/* unfilled ring reads as silence */
	return 0;
}

int
audioclose(dev_t dev, int flag, int mode)
{
	int s;

	/* Let whatever is buffered finish playing before we return. */
	s = splhigh();
	while (audio_playing && a_used > 0)
		sleep((caddr_t)&a_used, PWAIT);
	audio_hw_stop();
	splx(s);

	audio_open = 0;
	return 0;
}

int
audiowrite(dev_t dev, struct uio *uio, int flag)
{
	int s, error = 0;
	unsigned free, n;

	while (uio->uio_resid > 0) {
		s = splhigh();
		while (a_used >= ASAMPLES) {		/* ring full: wait for drain */
			if (!audio_playing && a_used > 0)
				audio_hw_start();
			sleep((caddr_t)&a_used, PPIPE);
		}
		free = ASAMPLES - a_used;
		splx(s);

		/* Copy a contiguous run up to the ring end or the free space. */
		n = free;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ASAMPLES - a_wr)
			n = ASAMPLES - a_wr;

		error = uiomove((caddr_t)&abuf[a_wr], n, uio);
		if (error)
			break;

		/* Mirror the part of the write that lands in the ring's first
		 * AGUARD samples into the guard copy, so the DMA's over-read past
		 * the ring end (before audio_tick's wrap rewind lands) reads this
		 * lap's real data rather than garbage. */
		if (a_wr < AGUARD) {
			unsigned m = AGUARD - a_wr;
			if (m > n)
				m = n;
			bcopy((caddr_t)&abuf[a_wr],
			    (caddr_t)&abuf[ASAMPLES + a_wr], m);
		}

		s = splhigh();
		a_wr += n;
		if (a_wr >= ASAMPLES)
			a_wr -= ASAMPLES;
		a_used += n;
		if (!audio_playing && a_used >= APREBUF)
			audio_hw_start();
		splx(s);
	}

	/* Short clip that never reached the prebuffer threshold: start it. */
	s = splhigh();
	if (!audio_playing && a_used > 0)
		audio_hw_start();
	splx(s);

	return error;
}

int
audioioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
	int rate;

	switch (cmd) {
	case AUDIO_SETRATE:
		rate = *(int *)data;
		if (rate < AUDIO_RATE_MIN)
			rate = AUDIO_RATE_MIN;
		if (rate > AUDIO_RATE_MAX)
			rate = AUDIO_RATE_MAX;
		audio_rate = rate;
		if (audio_playing)		/* retune the running sample clock */
			audio_set_timer();
		return 0;

	case AUDIO_GETRATE:
		*(int *)data = audio_rate;
		return 0;

	default:
		return EINVAL;
	}
}
