/*
 * Copyright (c) 2022 Christopher Hettrick <chris@structfoo.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_MACHINE_INTR_H_
#define	_MACHINE_INTR_H_

#ifdef	KERNEL

/*
 * Interrupt priority levels on the GBA: there are none to manage.
 *
 * The GBA has one IRQ line, masked as a whole by REG_IME (see
 * gba_irq_allow()/gba_irq_block() in machdep.c); there is no priority
 * hardware like the Cortex-M BASEPRI/NVIC that the stm32 port's spl*()
 * drive.  This port keeps kernel code safe from its interrupt handlers
 * another way: every system call runs with IME=0 (the syscall
 * trampoline, gba/syscall.c), and interrupts are let in only while user
 * code runs and while idle() waits.  The single-level handler
 * (gba_intr_handler, locore0.S) never nests.  So the machine-independent
 * kernel's spl*() calls have nothing to raise or restore: they are
 * no-ops: each evaluates to 0, and splx() ignores its argument.
 */
static inline __attribute__((always_inline)) int
gba_spl(void)
{
	return 0;
}

static inline __attribute__((always_inline)) void
splx(int s)
{
	(void)s;
}

#define	splhigh()	gba_spl()
#define	splclock()	gba_spl()
#define	spltty()	gba_spl()
#define	splnet()	gba_spl()
#define	splbio()	gba_spl()
#define	splsoftclock()	gba_spl()
#define	spl0()		gba_spl()

#endif	/* KERNEL */

#endif	/* !_MACHINE_INTR_H_ */
