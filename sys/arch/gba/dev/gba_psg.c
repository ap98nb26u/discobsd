/*
 * /dev/psg - play a timed stream of writes to the four PSG sound
 * channels (the Game Boy compatible square, wave and noise channels).
 *
 * A program writes a stream of 16-bit little-endian words:
 *
 *	0x8000 | n		wait n milliseconds (1..32767)
 *	0x4000 | r, v		write v to the PSG register at 0x04000000 + r:
 *				r = 0x60..0x80 (channel registers and
 *				SOUNDCNT_L) or 0x90..0x9e (wave RAM), even
 *	0x5000 | m		PSG share of the mix (SOUNDCNT_H bits 0-1):
 *				0 = 25%, 1 = 50%, 2 = 100%
 *	0x0000			no operation
 *
 * Anything else is skipped.  The clock interrupt (psg_tick(), 100 Hz)
 * carries out the writes when their time comes, so the music keeps its
 * time while the writer is busy or swapped out; the 10 ms tick is the
 * timing grain, but waits add up exactly.  The stream is the format
 * mml(1) writes with -o, so `cat song.psg > /dev/psg' plays it.
 *
 * The queue holds 2048 words in spare VRAM: VRAM takes no byte writes,
 * which halfword entries avoid.  Mode 3 uses VRAM up to 0x06012C00 and
 * Mode 5 up to 0x06014000; the console backing store is at 0x06015000
 * and the serial receive ring at 0x06016000 (gba_text.c,
 * gba_sio_uart.c), so 0x06017000 to the end of VRAM is free.
 *
 * One opener at a time.  While the stream plays, all four channels are
 * routed to both speakers and the console bell stays quiet.  Close does
 * not wait: what is queued plays out from the clock interrupt, which
 * then silences the channels and puts the bell's setting back - so
 * `cat song.psg > /dev/psg' plays to the end.  A new open takes over
 * from a stream still playing out.  ioctl PSGFLUSH stops at once (mml(1)
 * does it on SIGINT), PSGDRAIN waits until everything has played.
 * DirectSound PCM (/dev/audio) is separate hardware: the GBA mixes the
 * two, so both can play at once.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/ioctl.h>
#include <sys/inode.h>

#include <machine/gba.h>
#include <gba/dev/gba_psg.h>

#define PSG_QSIZE	2048			/* words; a power of 2 */
#define PSG_QMASK	(PSG_QSIZE - 1)
#define psg_q		((volatile unsigned short *)0x06017000)

#define PSG_MS_PER_TICK	(1000 / HZ)
#define PSG_REG(r)	(*(volatile unsigned short *)(0x04000000 + (r)))
#define REG_TM3CNT_L_	(*(volatile unsigned short *)0x0400010C)

static unsigned short psg_head, psg_tail;	/* producer, consumer */
static short psg_open;			/* a process has it open */
static short psg_active;		/* a stream is playing (maybe after close) */
static int psg_wait;			/* ms until the next command */
static unsigned short psg_tm3;		/* Timer3 at the last tick */

#define psg_count()	((psg_head - psg_tail) & PSG_QMASK)

int
psg_busy(void)
{
	return psg_active;
}

/* A PSG register a stream may write: channel registers, SOUNDCNT_L, wave RAM. */
static int
psg_regok(unsigned r)
{
	if (r & 1)
		return 0;
	return (r >= 0x60 && r <= 0x80) || (r >= 0x90 && r <= 0x9e);
}

/* Mute the four channels: their DACs off (GBATEK: envelope bits 11-15 = 0). */
static void
psg_silence(void)
{
	REG_SOUND1CNT_L = 0;
	REG_SOUND1CNT_H = 0;
	PSG_REG(0x68) = 0;			/* SOUND2CNT_L */
	PSG_REG(0x70) = 0;			/* SOUND3CNT_L: wave DAC off */
	PSG_REG(0x78) = 0;			/* SOUND4CNT_L */
}

/* End of a stream: quiet, and the bell's routing and full PSG mix back. */
static void
psg_stop(void)
{
	psg_active = 0;
	psg_head = psg_tail = 0;
	psg_wait = 0;
	psg_silence();
	REG_SOUNDCNT_L = 0x1177;		/* ch1 only, as gba_sound_init() */
	REG_SOUNDCNT_H = (REG_SOUNDCNT_H & ~3) | 2;	/* PSG mix 100% */
}

/*
 * Called from the clock interrupt at HZ.  Timer3 counts the ticks that
 * went by while interrupts were off, so the queue catches up.
 */
void
psg_tick(void)
{
	unsigned short now, w;

	if (!psg_active)
		return;
	now = REG_TM3CNT_L_;
	psg_wait -= (unsigned short)(now - psg_tm3) * PSG_MS_PER_TICK;
	psg_tm3 = now;

	while (psg_wait <= 0 && psg_count() > 0) {
		w = psg_q[psg_tail];
		if (w & 0x8000) {
			psg_wait += w & 0x7fff;
			psg_tail = (psg_tail + 1) & PSG_QMASK;
		} else if ((w & 0xf000) == 0x4000) {
			if (psg_count() < 2)
				break;		/* the value is not written yet */
			if (psg_regok(w & 0xff))
				PSG_REG(w & 0xff) = psg_q[(psg_tail + 1) & PSG_QMASK];
			psg_tail = (psg_tail + 2) & PSG_QMASK;
		} else {
			if ((w & 0xf000) == 0x5000 && (w & 0xfff) <= 2)
				REG_SOUNDCNT_H = (REG_SOUNDCNT_H & ~3) | (w & 3);
			psg_tail = (psg_tail + 1) & PSG_QMASK;
		}
	}
	if (psg_count() == 0 && psg_wait < 0)
		psg_wait = 0;		/* ran dry: new data plays at once */
	if (psg_count() <= PSG_QSIZE / 2)
		wakeup((caddr_t)&psg_head);
	if (psg_count() == 0 && psg_wait <= 0) {
		wakeup((caddr_t)&psg_tail);
		if (!psg_open)
			psg_stop();	/* closed and played out */
	}
}

int
psgopen(dev_t dev, int flag, int mode)
{
	int s;

	if (psg_open)
		return EBUSY;
	s = splhigh();
	psg_head = psg_tail = 0;		/* take over from a closed stream */
	psg_wait = 0;
	psg_tm3 = REG_TM3CNT_L_;
	psg_silence();
	REG_SOUNDCNT_X = 0x0080;		/* master enable (set at boot) */
	REG_SOUNDCNT_L = 0xff77;		/* ch1-4 to L and R, volume 7 */
	psg_open = psg_active = 1;
	splx(s);
	return 0;
}

int
psgclose(dev_t dev, int flag, int mode)
{
	int s;

	s = splhigh();
	psg_open = 0;
	if (psg_count() == 0 && psg_wait <= 0)
		psg_stop();			/* else psg_tick() stops it */
	splx(s);
	return 0;
}

int
psgioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	int s, error = 0;

	s = splhigh();
	switch (cmd) {
	case PSGFLUSH:				/* drop the queue, mute */
		psg_head = psg_tail = 0;
		psg_wait = 0;
		psg_silence();
		wakeup((caddr_t)&psg_head);
		break;
	case PSGDRAIN:				/* wait until it has played */
		while (psg_count() > 0 || psg_wait > 0) {
			error = tsleep((caddr_t)&psg_tail, PCATCH | (PZERO + 1), 0);
			if (error)
				break;
		}
		break;
	case FIONBIO:
	case FIOASYNC:
		break;
	default:
		error = ENOTTY;
	}
	splx(s);
	return error;
}

int
psgwrite(dev_t dev, struct uio *uio, int flag)
{
	unsigned short buf[32];
	int n, i, error, s;

	if (uio->uio_resid & 1)
		return EINVAL;			/* whole words only */
	while (uio->uio_resid > 0) {
		s = splhigh();
		while (PSG_QMASK - psg_count() < (int)(sizeof(buf) / 2)) {
			if (flag & IO_NDELAY) {
				splx(s);
				return EWOULDBLOCK;
			}
			error = tsleep((caddr_t)&psg_head, PCATCH | (PZERO + 1), 0);
			if (error) {
				splx(s);
				return error;
			}
		}
		splx(s);
		n = uio->uio_resid;
		if (n > (int)sizeof(buf))
			n = sizeof(buf);
		error = uiomove((caddr_t)buf, n, uio);
		if (error)
			return error;
		for (i = 0; i < n / 2; i++)
			psg_q[(psg_head + i) & PSG_QMASK] = buf[i];
		s = splhigh();
		psg_head = (psg_head + n / 2) & PSG_QMASK;
		splx(s);
	}
	return 0;
}
