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

void clkstart(void) {
}

void cpu_initclocks(void) {
    // Timer 0 の設定
    REG_TM0CNT_L = 65536 - (16777216 / 1024 / HZ); 
    
    // 0x0040: 割り込み有効
    // 0x0003: 1024分周
    // 0x0080: 開始
    REG_TM0CNT_H = 0x00C3;

    /*
     * Timer1: free-running 16-bit counter at 16.78MHz/1024 = 16384 Hz,
     * NO interrupt (0x0080 enable | 0x0003 /1024 prescaler; the 0x0040
     * IRQ bit is deliberately not set). Unlike Timer0's interrupt count
     * (myticks), this advances in real hardware time regardless of the
     * IME mask, so it keeps ticking while a command's output rendering
     * holds interrupts off (IME=0). The software keyboard's auto-repeat
     * reads REG_TM1CNT_L (swkbd.c) so a held key repeats at the right
     * wall-clock cadence even while scrolling output starves the
     * interrupt-driven clocks. Wraps every 4 s (65536/16384); auto-repeat
     * only measures sub-second deltas, so 16-bit wrap math suffices.
     *
     * NB: this lives here, not in irq_enable() (machdep.c), because that
     * function is not actually called on this port - cpu_initclocks() is
     * the live timer-setup path.
     */
    REG_TM1CNT_L = 0;
    REG_TM1CNT_H = 0x0083;

    /*
     * Timer2 + Timer3: an IME-independent hardware tick counter, so the
     * software clock cannot lose time under load.
     *
     * A GBA timer interrupt has no pending count: every Timer0 overflow
     * that happens while IME=0 (the whole of every syscall, and any
     * splhigh/splbio region) collapses into the single interrupt serviced
     * when IME is re-enabled, so hardclock() ran once for however many
     * ticks really elapsed and the clock drifted long in proportion to
     * load (`sleep`/`time`/itimers all overshot). To recover the lost
     * ticks the Timer0 ISR needs to know how many overflows really
     * happened; a free-running counter provides that.
     *
     * Timer2 is a second free-running timer clocked exactly like Timer0
     * (same /1024 prescaler and reload), so it overflows at the same HZ
     * rate in real hardware time, IRQ bit off. Timer3 is chained to it in
     * count-up mode (increments once per Timer2 overflow): GBA count-up
     * timers count the *preceding* timer's overflows, so Timer3 counts
     * Timer2 (not Timer0 - Timer1 is the swkbd auto-repeat counter and is
     * left alone). Timer3 therefore advances once per intended hardclock
     * tick regardless of the IME mask; gba_do_schedule() reads its delta
     * and replays exactly that many hardclock() calls. Timer3 wraps only
     * every 65536 ticks (~11 min), far longer than any masked window.
     *
     * 0x0083 = enable | /1024 prescaler (no IRQ); 0x0084 = enable |
     * count-up (prescaler ignored). Order matters: start Timer2 before the
     * Timer3 that cascades off it.
     */
    REG_TM2CNT_L = 65536 - (16777216 / 1024 / HZ);
    REG_TM2CNT_H = 0x0083;
    REG_TM3CNT_L = 0;
    REG_TM3CNT_H = 0x0084;

    // GBA全体の割り込み許可レジスタ
    REG_IE |= IRQ_TIMER0;
#ifdef SERIAL_CONSOLE
    REG_IE |= IRQ_SERIAL;	/* link-cable RX byte -> gsio_rx_isr() */
#endif
}
