#ifndef _GBA_DEV_PSG_H_
#define _GBA_DEV_PSG_H_

/*
 * /dev/psg: a timed stream of PSG register writes (see gba_psg.c for the
 * stream format).
 *   PSGFLUSH  drop what is queued and mute the four channels
 *   PSGDRAIN  wait until everything queued has played
 * mml(1) carries its own copy of these two numbers.
 */
#define PSGFLUSH	_IO('P', 1)
#define PSGDRAIN	_IO('P', 2)

#ifdef KERNEL
struct uio;
int	psgopen(dev_t dev, int flag, int mode);
int	psgclose(dev_t dev, int flag, int mode);
int	psgwrite(dev_t dev, struct uio *uio, int flag);
int	psgioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
void	psg_tick(void);		/* from the clock interrupt, at HZ */
int	psg_busy(void);		/* a stream is playing: the bell keeps quiet */
#endif /* KERNEL */

#endif /* _GBA_DEV_PSG_H_ */
