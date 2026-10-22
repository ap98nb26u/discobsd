/*
 * GBA DirectSound PCM playback driver - /dev/audio.
 *
 * Plays raw 8-bit SIGNED PCM, mono or stereo.  Mono (/dev/audio[16]) feeds
 * DirectSound A to both speakers from one ring.  Stereo (/dev/audiost[16])
 * takes interleaved L,R,L,R... samples, deinterleaves them into a left and a
 * right ring (each half the mono length, so the same total RAM), and plays
 * them through DirectSound A (left) via DMA1 and DirectSound B (right) via
 * DMA2, both clocked by the same Timer1.  NB the GBA's built-in speaker is
 * mono; stereo separation is only heard on headphones / an emulator.
 *
 * Rates: /dev/audio=8192, /dev/audio16=16384, /dev/audio4=4096 (mono) and
 * /dev/audiost[16] (stereo); AUDIO_SETRATE overrides.  On the plain GBA
 * (ROM-root build, data in ROM) playback is clean apart from the wrap click below.
 * On GBAED it is USABLE BUT NOT PRACTICAL: the data streams off the SD while
 * playing, and the SD stalls (plus the halved-ring wraps) add audible drop-
 * outs, beat/growl and a startup echo - 4096 Hz halves the data rate and so
 * smooths it a little at the cost of a 2 kHz-dull tone, but neither rate is
 * clean.  Getting any PCM out while also reading it from the SD is the point
 * of the GBAED support, not high fidelity.
 *
 * The hard parts of GBA PCM are all about who keeps the sound FIFO fed,
 * because the sound DMA has no "wrap" and no
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
 * sized as large as the kernel's IWRAM .bss headroom allows (1.125 s mono),
 * which costs no swap.  Stereo halves the per-channel ring to keep the same
 * total RAM, so it wraps twice as often and is audibly clicker than mono -
 * stereo is the lower-fi mode, worth it only for the L/R separation (which,
 * again, only a headphone jack or an emulator can reproduce; the console
 * speaker is mono).  Two things were tried to wrap less often and did NOT pan
 * out, recorded here so they are not re-attempted blindly:
 *   - A 32 KB EWRAM ring (short clips wrap-free): it permanently shrank the
 *     ROM-root build's EWRAM swap area enough to OOM 4-stage pipelines, and the kernel stack
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

/*
 * DMA channels 1 and 2 (sound FIFO A and B).  SAD/DAD are 32-bit, CNT 16+16.
 * DMA1 feeds FIFO A (left in stereo / both in mono), DMA2 feeds FIFO B (right
 * in stereo).  DMA2 is otherwise unused here (the SD driver uses DMA3).
 */
#define REG_DMA1SAD	(*(volatile uint32_t *)(REG_BASE+0x00BC))
#define REG_DMA1DAD	(*(volatile uint32_t *)(REG_BASE+0x00C0))
#define REG_DMA1CNT_L	(*(volatile uint16_t *)(REG_BASE+0x00C4))
#define REG_DMA1CNT_H	(*(volatile uint16_t *)(REG_BASE+0x00C6))
#define REG_DMA2SAD	(*(volatile uint32_t *)(REG_BASE+0x00C8))
#define REG_DMA2DAD	(*(volatile uint32_t *)(REG_BASE+0x00CC))
#define REG_DMA2CNT_L	(*(volatile uint16_t *)(REG_BASE+0x00D0))
#define REG_DMA2CNT_H	(*(volatile uint16_t *)(REG_BASE+0x00D2))
#define FIFO_A		(REG_BASE+0x00A0)
#define FIFO_B		(REG_BASE+0x00A4)

/*
 * DMA control for a sound FIFO channel:
 *   0x8000 enable | 0x3000 start-timing = special (sound FIFO)
 * | 0x0400 32-bit units | 0x0200 repeat.
 * Destination control is forced to the FIFO in this mode; source increments.
 */
#define DMA1_SOUND	0xB600

/*
 * SOUNDCNT_H routings (all on Timer1).  Bits: 0x0002 PSG mix 100%; 0x0004/8
 * DSound A/B volume 100%; 0x0100/0200 A enable R/L; 0x1000/2000 B enable R/L;
 * 0x0400/4000 A/B timer-select = Timer1; 0x0800/8000 A/B FIFO reset.
 *   SNDH_MONO:   A to both L+R (mono).
 *   SNDH_STEREO: A to L only, B to R only.
 */
#define SNDH_MONO	(0x0002 | 0x0004 | 0x0100 | 0x0200 | 0x0400)
#define SNDH_STEREO	(0x0002 | 0x0004 | 0x0200 | 0x0400 \
			         | 0x0008 | 0x1000 | 0x4000)
#define SNDH_FIFO_RESET_A	0x0800
#define SNDH_FIFO_RESET_B	0x8000

/*
 * Ring storage.  ASAMPLES must be a multiple of 16 (the DMA moves 32-bit
 * units = 4 samples, and the wrap math rounds to the 16-sample FIFO refill).
 * MONO uses the whole buffer as one 1.125 s ring (sized to the GBAED build's
 * ~2.5 KB IWRAM .bss headroom).  STEREO splits the SAME buffer in half - a
 * left ring in [0,HALF) and a right ring in [HALF,2*HALF) - so two channels
 * cost the same total RAM as mono, at half the per-channel length.  AGUARD is
 * a mirror of each ring's first AGUARD samples (the producer mirrors writes
 * that land there) covering the DMA's over-read past the ring end before
 * audio_tick's Timer0-driven wrap rewind is serviced.
 */
#define ASAMPLES	9216		/* mono: 1.125 s at 8192 Hz; mult of 16 */
#define AGUARD		1024		/* mono guard, 125 ms */
#define HALF		((ASAMPLES + AGUARD) / 2)	/* 5120 */
#define ASAMPLES_ST	4608		/* stereo per-channel ring; mult of 16 */
#define AGUARD_ST	(HALF - ASAMPLES_ST)		/* 512 */

/*
 * Producer safety reserve.  The sound DMA reads AHEAD of the software play
 * position a_play (by the up-to-32-sample FIFO occupancy, plus any slack
 * between a_play's Timer3-derived estimate and the hardware's true output
 * point).  If the ring is allowed to fill completely, the producer's write
 * front a_wr coincides with a_play, leaving only that ~32-sample FIFO margin
 * between the producer and the samples the DMA is about to read: the producer
 * then overwrites a frame AS the DMA reads it.  Within a constant tone that is
 * inaudible (same data either lap), but at a segment boundary - and, in stereo,
 * because a frame is written as two separate stores (left ring then right ring)
 * - the DMA reads a torn frame: left from the new lap, right from the old (or
 * vice versa), i.e. channel bleed plus a crackle.  The first wrap's
 * audio_dma_rewind() happens to restore a clean margin, which is why the defect
 * is heard only on the first ring lap.  Reserving ARESERVE samples (never
 * filling the last of the ring) keeps a_wr a safe distance behind the DMA read
 * front at all times, on the first lap as well.  Costs ARESERVE samples of
 * buffering (negligible: 256 of 4608/9216).
 */
#define ARESERVE	256		/* mult of 16; >= FIFO occupancy + slack */

static signed char abuf[ASAMPLES + AGUARD];
#define RBASE		(&abuf[HALF])	/* stereo right-channel ring base */

static int	audio_rate = AUDIO_RATE_DEF;
static int	audio_open;		/* single-opener guard */
static int	audio_stereo;		/* 2 DMA channels, deinterleaved */
static unsigned	a_rsize;		/* ring length (per channel) */
static unsigned	a_gsize;		/* guard length (per channel) */

/* Ring state.  Shared with audio_tick() (interrupt); guard with splhigh().
 * Indices and counts are PER CHANNEL (mono = the one channel). */
static volatile int audio_playing;	/* DMA/Timer1 running */
static volatile unsigned a_wr;		/* producer index into [0..a_rsize) */
static volatile unsigned a_play;	/* play index  into [0..a_rsize) */
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
	audio_stereo = 0;
	audio_playing = 0;
	a_rsize = ASAMPLES;
	a_gsize = AGUARD;
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
 * Suspend/resume the sample clock around an EverDrive SD data transfer (called
 * from ed.c).  Stopping Timer1 stops the FIFO draining, so the sound DMA stops
 * requesting and goes idle WITHOUT being disabled - its source position is
 * preserved, so playback resumes seamlessly.  This keeps the sound DMA off the
 * bus while DMA3 drives the SD: on real EverDrive hardware a sound DMA running
 * concurrently with an SD transfer intermittently wedges the transfer (the cat
 * process then hangs in the SD driver's IME=0 busy-wait, uninterruptible by
 * ^C). The emulator does not reproduce it. No-op when nothing is playing, so
 * ordinary SD I/O is unaffected.
 */
void
audio_suspend(void)
{
	if (audio_playing)
		REG_TM1CNT_H = 0;		/* stop clock; DMA1 stays enabled but idle */
}

void
audio_resume(void)
{
	if (audio_playing)
		audio_set_timer();		/* restart the sample clock */
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
	REG_DMA1CNT_H = 0;			/* make sure the channel(s) are off */
	REG_DMA2CNT_H = 0;

	if (audio_stereo) {
		REG_SOUNDCNT_H = SNDH_STEREO | SNDH_FIFO_RESET_A | SNDH_FIFO_RESET_B;
		REG_DMA1SAD = (uint32_t)&abuf[off];	/* left */
		REG_DMA1DAD = (uint32_t)FIFO_A;
		REG_DMA1CNT_L = 0;
		REG_DMA2SAD = (uint32_t)&RBASE[off];	/* right */
		REG_DMA2DAD = (uint32_t)FIFO_B;
		REG_DMA2CNT_L = 0;
		REG_DMA1CNT_H = DMA1_SOUND;
		REG_DMA2CNT_H = DMA1_SOUND;		/* both prime now */
	} else {
		REG_SOUNDCNT_H = SNDH_MONO | SNDH_FIFO_RESET_A;
		REG_DMA1SAD = (uint32_t)&abuf[off];
		REG_DMA1DAD = (uint32_t)FIFO_A;
		REG_DMA1CNT_L = 0;
		REG_DMA1CNT_H = DMA1_SOUND;		/* enable: primes the FIFO now */
	}

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
	REG_DMA1CNT_H = 0;			/* disable sound DMA(s) */
	REG_DMA2CNT_H = 0;
	REG_SOUNDCNT_H = SNDH_FIFO_RESET_A | SNDH_FIFO_RESET_B;	/* clear FIFOs */
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

	if (off >= a_rsize)			/* keep the source inside the ring */
		off -= a_rsize;

	REG_DMA1CNT_H = 0;
	REG_DMA1SAD = (uint32_t)&abuf[off];
	REG_DMA1CNT_H = DMA1_SOUND;
	if (audio_stereo) {			/* the right channel wraps in step */
		REG_DMA2CNT_H = 0;
		REG_DMA2SAD = (uint32_t)&RBASE[off];
		REG_DMA2CNT_H = DMA1_SOUND;
	}
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
	if (a_play >= a_rsize) {			/* one wrap: repoint the DMA(s) */
		a_play -= a_rsize;
		audio_dma_rewind();
	}
	wakeup((caddr_t)&a_used);		/* room freed for the writer */
}

int
audioopen(dev_t dev, int flag, int mode)
{
	/*
	 * The minor selects rate and channel count without needing a tool:
	 *   0 /dev/audio      mono   8192 Hz
	 *   1 /dev/audio16    mono  16384 Hz
	 *   2 /dev/audiost    stereo 8192 Hz   (interleaved L,R,L,R...)
	 *   3 /dev/audiost16  stereo 16384 Hz
	 *   4 /dev/audio4     mono   4096 Hz
	 *   5 /dev/audiost4   stereo 4096 Hz
	 * All rates divide the 16.78 MHz timer clock exactly (reloads 0xF000 /
	 * 0xF800 / 0xFC00); 16384 is twice the 8192 default and half the GBA's
	 * native 32768 Hz output, unlike a round 16000 the timer cannot hit.
	 * 4096 (Nyquist 2 kHz) is dull but halves the data rate, so on GBAED it
	 * streams from SD with half the reads and half the ring wraps - smoother,
	 * worth it for voice/effects.  AUDIO_SETRATE still overrides the rate;
	 * stereo halves the per-channel ring so two channels cost the mono RAM.
	 */
	switch (minor(dev)) {
	case 0: audio_rate = AUDIO_RATE_DEF; audio_stereo = 0; break;
	case 1: audio_rate = 16384;          audio_stereo = 0; break;
	case 2: audio_rate = AUDIO_RATE_DEF; audio_stereo = 1; break;
	case 3: audio_rate = 16384;          audio_stereo = 1; break;
	case 4: audio_rate = 4096;           audio_stereo = 0; break;
	case 5: audio_rate = 4096;           audio_stereo = 1; break;
	default: return ENXIO;
	}
	if (audio_open)
		return EBUSY;
	audio_open = 1;
	a_rsize = audio_stereo ? ASAMPLES_ST : ASAMPLES;
	a_gsize = audio_stereo ? AGUARD_ST : AGUARD;
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

/*
 * Mirror the part of a just-written run [wr, wr+n) that lands in the ring's
 * first a_gsize samples into the guard copy at base[a_rsize..], so the DMA's
 * over-read past the ring end (before audio_tick's wrap rewind lands) reads
 * this lap's real data rather than garbage.
 */
static void
audio_mirror(signed char *base, unsigned wr, unsigned n)
{
	if (wr < a_gsize) {
		unsigned m = a_gsize - wr;
		if (m > n)
			m = n;
		bcopy((caddr_t)&base[wr], (caddr_t)&base[a_rsize + wr], m);
	}
}

int
audiowrite(dev_t dev, struct uio *uio, int flag)
{
	int s, error = 0;
	unsigned free, n;
	signed char tmp[128];			/* deinterleave staging (stereo) */
	/*
	 * How full to fill the ring before playback starts, = the steady-state
	 * lead the producer keeps ahead of the play point.  Stereo's ring is half
	 * mono's, so start it fuller (3/4) to keep enough slack that a wrap never
	 * repoints the DMA onto not-yet-refilled samples (a ~0.5 s stale echo).
	 */
	unsigned prebuf = audio_stereo ? (a_rsize - a_rsize / 4) : (a_rsize / 2);
	/*
	 * Usable capacity, one ARESERVE short of the physical ring, so the
	 * producer never writes the frame the DMA is about to read (see
	 * ARESERVE).  prebuf stays below this (3/4 and 1/2 of the ring).
	 */
	unsigned cap = a_rsize - ARESERVE;

	for (;;) {
		if (audio_stereo) {
			if (uio->uio_resid < 2) {	/* need a whole L,R pair */
				if (uio->uio_resid == 1)
					(void)uiomove((caddr_t)tmp, 1, uio);
				break;
			}
		} else if (uio->uio_resid == 0)
			break;

		s = splhigh();
		while (a_used >= cap) {			/* ring full: wait for drain */
			if (!audio_playing && a_used > 0)
				audio_hw_start();
			sleep((caddr_t)&a_used, PPIPE);
		}
		free = cap - a_used;
		splx(s);

		/* n = per-channel samples to add this pass, bounded by the free
		 * space, the contiguous run to the ring end, and the input. */
		n = free;
		if (n > a_rsize - a_wr)
			n = a_rsize - a_wr;

		if (audio_stereo) {
			unsigned pairs = uio->uio_resid / 2, done;

			if (n > pairs)
				n = pairs;
			for (done = 0; done < n; ) {	/* deinterleave L,R -> 2 rings */
				unsigned c = n - done, i;

				if (c > sizeof(tmp) / 2)
					c = sizeof(tmp) / 2;
				error = uiomove((caddr_t)tmp, c * 2, uio);
				if (error)
					goto out;
				for (i = 0; i < c; i++) {
					abuf[a_wr + done + i] = tmp[2 * i];
					RBASE[a_wr + done + i] = tmp[2 * i + 1];
				}
				done += c;
			}
			audio_mirror(abuf, a_wr, n);
			audio_mirror(RBASE, a_wr, n);
		} else {
			if (n > uio->uio_resid)
				n = uio->uio_resid;
			error = uiomove((caddr_t)&abuf[a_wr], n, uio);
			if (error)
				break;
			audio_mirror(abuf, a_wr, n);
		}

		s = splhigh();
		a_wr += n;
		if (a_wr >= a_rsize)
			a_wr -= a_rsize;
		a_used += n;
		if (!audio_playing && a_used >= prebuf)
			audio_hw_start();
		splx(s);
	}
out:
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
