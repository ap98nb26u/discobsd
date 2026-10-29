/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)clock.c	1.1 (2.10BSD Berkeley) 12/1/86
 */

#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>

#include <machine/gba.h>
#include <gba/dev/gba_sound.h>

void clkstart(void) {
    /* Callouts are set up by now; see gba_sound.c. */
    gba_sound_prime();
}

void cpu_initclocks(void) {
    // Timer 0: the HZ clock tick
    REG_TM0CNT_L = 65536 - (16777216 / 1024 / HZ); 
    
    // 0x0040: interrupt enable
    // 0x0003: prescaler 1/1024
    // 0x0080: start
    REG_TM0CNT_H = 0x00C3;

    /*
     * Timer1 is not configured here: gba_audio.c uses it as the PCM sample
     * clock. The GBA can clock the DirectSound FIFO only off Timer0 or
     * Timer1 (SOUNDCNT_H timer-select bit), and Timer0 is the hardclock.
     * The software keyboard times its auto-repeat off Timer3 (see swkbd.c
     * and the Timer2/Timer3 setup below).
     */

    /*
     * Timer2 + Timer3: an IME-independent hardware tick counter, so the
     * software clock cannot lose time under load.
     *
     * A GBA timer interrupt has no pending count: every Timer0 overflow
     * that happens while IME=0 (the whole of every syscall) collapses
     * into the single interrupt serviced
     * when IME is re-enabled, so hardclock() ran once for however many
     * ticks really elapsed and the clock drifted long in proportion to
     * load (`sleep`/`time`/itimers all overshot). To recover the lost
     * ticks the Timer0 ISR needs to know how many overflows really
     * happened; a hardware counter that keeps counting under IME=0
     * provides that.
     *
     * Timer2 is a second timer clocked exactly like Timer0 (same /1024
     * prescaler and reload, so it wraps every tick), so it overflows at the
     * same HZ rate in real hardware time, IRQ bit off. Timer3 is chained to
     * it in count-up mode (increments once per Timer2 overflow): GBA count-up
     * timers count the *preceding* timer's overflows, so Timer3 counts Timer2
     * (not Timer0 - Timer1 is the PCM sample clock and is left alone). Timer3
     * therefore advances once per intended hardclock tick regardless of the
     * IME mask; gba_do_schedule() reads its delta and replays exactly that
     * many hardclock() calls. Timer3 wraps only every 65536 ticks (~11 min),
     * far longer than any masked window.
     *
     * 0x0083 = enable | /1024 prescaler (no IRQ); 0x0084 = enable |
     * count-up (prescaler ignored). Order matters: start Timer2 before the
     * Timer3 that cascades off it.
     */
    REG_TM2CNT_L = 65536 - (16777216 / 1024 / HZ);
    REG_TM2CNT_H = 0x0083;
    REG_TM3CNT_L = 0;
    REG_TM3CNT_H = 0x0084;

    // Interrupt enable register
    REG_IE |= IRQ_TIMER0;
#ifdef SERIAL_CONSOLE
    REG_IE |= IRQ_SERIAL;	/* SIO error/empty -> gsio_rx_isr(); no rx IRQ */
#endif
}
