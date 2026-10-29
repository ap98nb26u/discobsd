/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)machdep.c	2.4 (2.11BSD) 1999/9/13
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/dir.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/fs.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/clist.h>
#include <sys/callout.h>
#include <sys/reboot.h>
#include <sys/msgbuf.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/config.h>
#include <sys/tty.h>
#include <sys/stdint.h>

#include <machine/gba.h>

#include <gba/dev/mgbalog.h>
#include <gba/dev/gba_sio_uart.h>
#include <gba/dev/gba_audio.h>
#include <machine/gba_syscall.h>

int cpu_khz = 16777;
int bus_khz = 16777;

char	machine[] = MACHINE;		/* from <machine/machparam.h> */
char	machine_arch[] = MACHINE_ARCH;	/* from <machine/machparam.h> */
char	cpu_model[64];

int	hz = HZ;
int	usechz = (1000000L + HZ - 1) / HZ;

/*
 * Upper bound on how many missed hardclock ticks the Timer0 ISR will
 * replay in a single interrupt (see the catch-up code in
 * gba_do_schedule()). ~5 s at HZ=100; a longer freeze's remainder is
 * caught up over the following interrupts.
 */
#define CLOCK_MAXCATCHUP	512

/*
 * Clock-drift correction (software). Timer0 is clocked /1024 with an
 * integer reload, so it actually overflows at 16777216/1024/163 =
 * 16384/163 = 100.515 Hz - +0.515% over the nominal HZ=100 the software
 * clock assumes (163 is the truncation of the ideal 163.84). The clock
 * therefore gains ~7 min/day. Skipping one hardclock() tick every
 * CLOCK_DRIFT_SKIP ticks makes it advance at the real rate instead:
 * keeping 194/195 of the ticks gives 100.515 * 194/195 = 99.99986 Hz,
 * ~0.01 s/day residual (the RTC corrects the rest, plus crystal
 * tolerance, at boot). This is done in software, NOT with a finer /64
 * prescaler: /64 did cut the quantization error but hung real GBAED
 * hardware for reasons never pinned down, so the timer config is kept
 * byte-identical to the known-stable /1024 one. CLOCK_DRIFT_SKIP is tied
 * to HZ=100 and the /1024 reload (163); recompute it if either changes.
 */
#define CLOCK_DRIFT_SKIP	195

#ifdef TIMEZONE
struct timezone		tz = { TIMEZONE, DST };
#else
struct timezone		tz = { 8 * 60, 1 };
#endif

int			nproc = NPROC;

struct namecache	namecache[NNAMECACHE];
char			bufdata[NBUF * MAXBSIZE];
struct inode		inode[NINODE];
struct callout		callout[NCALL];
struct mount		mount[NMOUNT];
struct buf		buf[NBUF], bfreelist[BQUEUES];
struct bufhd		bufhash[BUFHSZ];
struct cblock		cfree[NCLIST];
struct proc		proc[NPROC];
struct file		file[NFILE];

extern struct user u;

/*
 * Remove the ifdef/endif to run the kernel in unsecure mode even when in
 * a multiuser state.  Normally 'init' raises the security level to 1
 * upon transitioning to multiuser.  Setting the securelevel to -1 prevents
 * the secure level from being raised by init.
 */
#ifdef PERMANENTLY_INSECURE
int	securelevel = -1;
#else
int	securelevel = 0;
#endif

struct mapent	swapent[SMAPSIZ];
struct map	swapmap[1] = {
	{ swapent,
	  &swapent[SMAPSIZ],
	  "swapmap" },
};

int	waittime = -1;

static int
nodump(dev_t dev)
{
	printf("\ndumping to dev %o off %D: not implemented\n",
	    dumpdev, dumplo);

	return 0;
}

int (*dump)(dev_t) = nodump;

dev_t	pipedev;
daddr_t	dumplo = (daddr_t)1024;

void gba_do_schedule(void);
int gba_irq_preempt(unsigned pc);

volatile uint32_t myticks = 1;

#ifdef HANGDUMP
volatile unsigned irq_last_pc;		/* interrupted pc + 4, see locore0.S */

/*
 * options HANGDUMP: hold L+R+SELECT to have the Timer0 interrupt print
 * where it interrupted (pc; look it up with addr2line on unix.elf), the
 * running process and what every process sleeps on - for a hang where
 * nothing else responds.  Silence means the kernel is stuck with
 * interrupts off.  It prints once per press, to the LCD (and the serial
 * console unless /dev/sio has it), from the interrupt; nothing stops.
 * Side effects: the text lands in whatever the console shows, the same
 * keys also work the software keyboard (SELECT shows/hides it, L/R
 * toggle shift/ctrl), and /dev/sio input or PCM may hiccup while it
 * prints with interrupts off.
 */
static void
gba_hang_dump(void)
{
    static int shown;
    extern void gtxt_blank(int);
    unsigned k = ~REG_KEYINPUT & 0x3FF;
    struct proc *p = u.u_procp;

    if (k != 0x304) {			/* L, R, SELECT */
        shown = 0;
        return;
    }
    if (shown++)
        return;
    gtxt_blank(0);
    printf("HANG pc=%x noproc=%d pid=%d stat=%d wchan=%x pri=%d\n",
        irq_last_pc - 4, noproc, p ? p->p_pid : -1,
        p ? p->p_stat : -1, p ? (unsigned)p->p_wchan : 0,
        p ? p->p_pri : -1);
    for (p = allproc; p; p = p->p_nxt)
        printf(" %d:%d/%x/%x", p->p_pid, p->p_stat, (unsigned)p->p_wchan,
            p->p_flag);
    printf("\n");
}
#endif /* HANGDUMP */

/*
 * Console input polling, shared by idle() and the Timer0 interrupt.
 *
 * Historically the console (software keyboard + UART) was sampled ONLY
 * from idle() - fine while a process is blocked waiting for input, but a
 * CPU-bound or output-scrolling command keeps the scheduler out of
 * idle(), so nothing sampled input and ^C could not interrupt it. Also
 * calling this from the 100Hz Timer0 interrupt (gba_do_schedule below)
 * keeps input - including the software keyboard's own draw/state machine
 * - serviced while a command runs, so the keyboard stays usable during
 * scrolling output and a ^C lands as a posted SIGINT the command takes
 * at its next syscall return (userret); output-heavy commands syscall
 * constantly, so it is prompt. (A loop that never syscalls is sent
 * through one by the interrupt return path instead - see
 * gba_irq_preempt().)
 *
 * console_poll_busy guards re-entrancy: idle() runs this with interrupts
 * enabled, so a Timer0 tick can land in the middle of it and re-enter -
 * the flag makes the interrupt's call a no-op then, so ttyinput()/swkbd
 * state is never driven by two nested callers. A syscall itself runs
 * with IME=0, so the interrupt can never preempt one mid-tty-operation;
 * the only overlap to defend against is idle vs. interrupt.
 */
volatile int console_poll_busy;
volatile int console_poll_ready;	/* set once idle() has run; see below */

/*
 * Console screen blanking (roadmap #6), the LCD console's screen-saver.
 *
 * console_blank_secs is the idle timeout in seconds: after that many
 * seconds with no console activity the on-screen display is blanked
 * (gtxt_blank(1), which disables BG2 so only a black backdrop shows -- see
 * gba_text.c); any activity restores it. 0 disables blanking entirely.
 * The compile-time default comes from the SCREEN_BLANK_SECS config option
 * (0 if unset); it is also reachable at runtime via the machdep.console_blank
 * sysctl (gba/sysctl.c) so it can be tuned or switched off on a live system.
 *
 * console_last_active holds time.tv_sec of the most recent activity.
 * "Activity" is any console output (gtxt_putc) or input (a software-keyboard
 * button or a serial byte); each calls console_activity() below, which both
 * wakes the screen and restarts the countdown. The blank itself is applied
 * from idle() (see below), the one place guaranteed to run only when nothing
 * else is - i.e. exactly when the console is truly idle.
 */
#ifndef SCREEN_BLANK_SECS
#define SCREEN_BLANK_SECS 0
#endif
int console_blank_secs = SCREEN_BLANK_SECS;
static time_t console_last_active;

void
console_activity(void)
{
    extern void gtxt_blank(int);

    console_last_active = time.tv_sec;
    gtxt_blank(0);
}

void
console_input_poll(void)
{
    extern void swkbd_poll(void);
    extern void uart_poll_input(void);

    /*
     * Only poll from the interrupt once the normal scheduler loop is
     * live. console_poll_ready is set the first time idle() runs, which
     * cannot happen until proc0/proc1 exist and the boot has reached the
     * point of blocking a process - i.e. exactly the state idle()'s own
     * polling already assumes. Before that, a Timer0 tick calling in here
     * would run swkbd_poll()/ttyinput() in interrupt context far earlier
     * in boot than they were ever exercised, on a stack that may not be
     * ready; skip it. (idle() sets the flag just before it calls here, so
     * idle()'s own call is never gated out.)
     */
    if (! console_poll_ready)
        return;
    if (console_poll_busy)
        return;
    console_poll_busy = 1;
    /*
     * swkbd BEFORE uart on purpose - swkbd injects via ttyinput(), whose
     * echo lands in the tty output queue that uart_poll_input() drains;
     * same ordering reason as the original idle() poll (see below).
     */
    swkbd_poll();
    uart_poll_input();
    console_poll_busy = 0;
}

#ifdef SERIAL_CONSOLE
extern int gsio_fastpoll_on;            /* gba_sio_uart.c: /dev/sio open */
#endif

IWRAM_CODE THUMB_CODE void gba_do_schedule(void)
{
    uint16_t flag = REG_IF;
    uint16_t ack = 0;

#ifdef SERIAL_CONSOLE
    if (flag & IRQ_SERIAL) {    // SIO: overrun/error, send full, receive empty
        extern void gsio_rx_isr(void);
        ack |= IRQ_SERIAL;
        gsio_rx_isr();          /* drain what the FIFO holds (no rx IRQ) */
    }
#endif
    if (flag & IRQ_TIMER0) { // TIMER0
        ack |= IRQ_TIMER0;
#ifdef SERIAL_CONSOLE
        /*
         * While /dev/sio is open, Timer0 runs at ~4 kHz (gsio_fastpoll())
         * just to keep the SIO receive FIFO drained: UART mode has no
         * "data arrived" interrupt, and a full FIFO loses the next byte.
         * The 100 Hz work below runs only when Timer3 - the free-running
         * 100 Hz tick counter - has moved, so the clock, audio and console
         * polling keep their normal rate.
         */
        if (gsio_fastpoll_on) {
            static uint16_t tm3_seen;
            extern void gsio_rx_isr(void);

            gsio_rx_isr();
            if (REG_TM3CNT_L == tm3_seen)
                goto timer0_done;
            tm3_seen = REG_TM3CNT_L;
        }
#endif
        myticks++;
        /*
         * Advance the kernel clock: this IS the Timer0 interrupt, so
         * hardclock() - which advances lbolt and processes the
         * timeout/callout queue - must be called here directly.
         *
         * Catch-up: a GBA timer interrupt has no pending count, so
         * every Timer0 overflow that happens while IME=0 (the whole
         * of every syscall) collapses into the one interrupt
         * serviced when IME is re-enabled - one hardclock() for
         * however many ticks really elapsed. That made the software
         * clock run slow in proportion to system load (`sleep`,
         * `time`, itimers all drifted long). Timer3 counts one per
         * intended tick in real hardware time regardless of the IME
         * mask (a count-up cascade off Timer2, which wraps at
         * Timer0's 100 Hz rate - see cpu_initclocks()), so its delta
         * since the last service is exactly how many ticks we owe.
         * Call hardclock() that many times; the callout wheel and
         * schedcpu() re-arm on their own periods, so each timeout
         * fires at its correct caught-up tick rather than N times.
         * Bounded so a one-off long freeze can't spin the ISR
         * unreasonably (a residual above the cap is picked up on the
         * following interrupts).
         */
        {
            static uint16_t last_tm3;
            static uint16_t drift;      /* drift corrector, see CLOCK_DRIFT_SKIP */
            uint16_t elapsed = (uint16_t)(REG_TM3CNT_L - last_tm3);

            if (elapsed == 0)           /* we did take an interrupt */
                elapsed = 1;
            if (elapsed > CLOCK_MAXCATCHUP)
                elapsed = CLOCK_MAXCATCHUP;
            last_tm3 += elapsed;
            do {
                /*
                 * Drop one tick every CLOCK_DRIFT_SKIP to cancel the
                 * /1024 timer's +0.515% fast rate (see the define). The
                 * skipped tick's clock advance and callout decrement are
                 * simply deferred to the next interrupt - within the
                 * normal +/-1 tick timeout granularity.
                 */
                if (++drift >= CLOCK_DRIFT_SKIP)
                    drift = 0;          /* skip: do not advance the clock */
                else
                    hardclock((caddr_t)0, 0);
            } while (--elapsed);
        }
#ifdef SERIAL_CONSOLE
        if (gsio_fastpoll_on) {
            extern void gsio_rx_isr(void);
            gsio_rx_isr();      /* keep the SIO FIFO drained (see above) */
        }
#endif

        /*
         * Keep DirectSound PCM fed: advance the play position and wrap the
         * sound DMA once per interrupt. Cheap no-op when nothing is playing.
         */
        audio_tick();
#ifdef HANGDUMP
        gba_hang_dump();
#endif
#ifdef SERIAL_CONSOLE
        /*
         * While /dev/sio is open, leave console polling to idle(), where
         * interrupts are on: swkbd_poll() and the tty work can take longer
         * than the SIO FIFO lasts with interrupts off.  (^C and the
         * software keyboard then only respond while the system idles.)
         */
        if (gsio_fastpoll_on)
            goto timer0_done;
#endif

        /*
         * Sample console input here too, not just in idle(), so ^C and
         * the software keyboard keep working while a command is running
         * (the scheduler is not in idle() then). Guarded against idle()
         * re-entrancy by console_poll_busy. NB: this runs in interrupt
         * context on the interrupted code's stack (the user process's
         * stack when a command was executing); swkbd_poll()/ttyinput()
         * are heavier than hardclock(), so if a deep-stack crash ever
         * appears right here, this call is the first thing to drop.
         */
        console_input_poll();
    }
#ifdef SERIAL_CONSOLE
timer0_done:
#endif
    /* Acknowledge the sources we handled; if somehow none matched, clear
     * whatever was pending so the interrupt can't latch up. */
    REG_IF = ack ? ack : flag;
}

/*
 * Called by gba_intr_handler (locore0.S) after every interrupt, with the
 * interrupted pc. Nonzero sends the interrupted code through
 * gba_preempt_tramp (locore.S) and a syscall, so that userret() switches
 * processes or delivers a signal: without this, a program that makes no
 * syscalls would keep the CPU and ignore ^C. Only user code is preempted;
 * the kernel runs with interrupts off except in idle(). The signal test
 * is CURSIG()'s cheap part: a false positive costs one syscall.
 */
int
gba_irq_preempt(unsigned pc)
{
    extern char __user_data_start[], __user_data_end[];
    struct proc *p;

    if (pc < (unsigned)__user_data_start || pc >= (unsigned)__user_data_end)
        return 0;
    if (runrun)
        return 1;
    p = u.u_procp;
    return p->p_sig != 0 &&
        ((p->p_flag & P_TRACED) || (p->p_sig & ~p->p_sigmask) != 0);
}

void irq_enable(void)
{
	REG_IME = 0;

	REG_TM0CNT_H = 0;
	//REG_TM0CNT_L = 65263;//60Hz 65536 - (16777216 / 1024 / HZ);
	REG_TM0CNT_L = 65372;//100Hz 65536 - (16777216 / 1024 / HZ);

	// 0x0040: interrupt enable
	// 0x0003: prescaler 1/1024
	// 0x0080: start
	REG_TM0CNT_H = 0x00C3;

	/*
	 * Only Timer0 (IRQ_TIMER0 = bit3) drives anything - it runs
	 * hardclock(). VBLANK (bit0) was previously enabled too (DISPSTAT
	 * bit3 + REG_IE bit0) but its handler only ever set vblank_flag,
	 * which nothing read; on-screen input and the cursor blink are
	 * polled via REG_VCOUNT/lbolt, not the VBLANK IRQ. Leaving it out
	 * so the only interrupt that fires is the one that has real work.
	 *
	 * IRQ_SERIAL (bit7) is added on the serial-console build. In UART mode
	 * it fires on an error (an overrun), a full send buffer or an EMPTY
	 * receive buffer - never on a byte arriving - and gsio_rx_isr()
	 * (gba_sio_uart.c) drains the FIFO; receive is otherwise polled - see
	 * there.
	 */
#ifdef SERIAL_CONSOLE
	REG_IE = IRQ_TIMER0 | IRQ_SERIAL;
#else
	REG_IE = IRQ_TIMER0;
#endif

	REG_IME = 1;
}


/*
 * Machine dependent startup code.
 */
void
startup(void)
{

	/*
	 * Enable the GBA ROM prefetch buffer (WAITCNT bit 14). The cartridge
	 * bus powers up at its slowest setting (WAITCNT=0x0000: WS0 4/2
	 * cycles, prefetch off - confirmed on real GBAED hardware), so every
	 * ROM instruction fetch pays the full waitstate. Prefetch speeds up
	 * sequential opcode fetch from ROM with no effect on the timing of
	 * data accesses (LDR/STR to the EverDrive FPGA registers and SD DMA),
	 * so it does not disturb the reliability-critical cart/SD transfer
	 * timing that the EverDrive OS itself is careful about. The WS0
	 * waitstate bits are left at their slowest (0) for that same reason;
	 * sram.c later ORs in the SRAM-wait bits, preserving bit 14.
	 */
	REG_WAITCNT = 0x4000;

	/*
	 * Overclock EWRAM from its default 2 waitstates to 1, via the undocumented internal memory
	 * control register at 0x04000800 (bits 24-27 = EWRAM wait: 0xD = 2
	 * waits (power-on default 0x0D000020), 0xE = 1 wait, 0xF = lockup).
	 * Unlike the ROM prefetch above, this speeds up EWRAM - where every
	 * userland process actually executes (the kernel runs from ROM, but
	 * user code is swapped into EWRAM), plus kernel stacks/buffers in
	 * EWRAM and SD DMA landing there. It is a long-established, widely
	 * used GBA overclock (safe on retail hardware; 0xF would lock up).
	 * Must be set in software each boot; re-applied here on warm reboot
	 * too since startup() runs again.
	 */
	*(volatile uint32_t *)0x04000800 = 0x0E000020;

	/*
	 * Publish the syscall trampoline at the fixed address userland's
	 * SYS.h dereferences (userland is linked separately from the
	 * kernel and cannot reference simulate_swi_via_inline_data as a
	 * symbol). Must run before any user process is started.
	 */
	*(void (**)(void))SYSCALL_VECTOR_ADDR = simulate_swi_via_inline_data;

	/*
	 * Early setup for console devices.
	 */
#if CONS_MAJOR == UART_MAJOR
	uartinit(CONS_MINOR);
#endif

	/*
	 * Enable the sound hardware so the console beep (BEL) works; see
	 * gba_sound.c. Harmless on both mGBA and real hardware.
	 */
	{
		extern void gba_sound_init(void);
		gba_sound_init();
	}

	/* DirectSound PCM device (/dev/audio); see gba_audio.c. */
	audio_init();

	/*
	 * Load persistent boot parameters from cartridge SRAM, falling back to
	 * the compiled defaults if the block is blank/invalid. Meaningful only
	 * on the ROM-root build (a no-op elsewhere): the SD-root GBAED
	 * build cannot persist SRAM and reapplies settings from /etc/rc.local
	 * instead. See sram.c.
	 */
	{
		extern void sram_params_init(void);
		sram_params_init();
	}

	/*
	 * Always a normal (multi-user) boot: there is no boot prompt or button
	 * to ask for single-user mode.  init falls back to it when /etc/rc fails.
	 */
	boothowto = 0;
}

static void
cpuidentify(void)
{
	physmem = (256 + 32) * 1024; /* EWRAM + IWRAM */
}

/*
 * Check whether the controller has been successfully initialized.
 */
static int
is_controller_alive(struct driver *driver, int unit)
{
	struct conf_ctlr *ctlr;

	/* No controller - that's OK. */
	if (driver == 0)
		return 1;

	for (ctlr = conf_ctlr_init; ctlr->ctlr_driver; ctlr++) {
		if (ctlr->ctlr_driver == driver &&
		    ctlr->ctlr_unit == unit && ctlr->ctlr_alive) {
			return 1;
		}
	}

	return 0;
}

void
sleep_ticks(uint32_t t)
{
	uint32_t target = myticks + t;
	while (myticks < target);
}

extern void cpu_initclocks(void);

/*
 * Configure all controllers and devices as specified
 * in the kernel configuration file.
 */

/*
 * Set by the RTC driver's probe (gba/dev/rtc.c) when a real-time clock
 * is found. Left NULL on builds/hardware without one (e.g. the GBA and
 * GBALOG configs, which do not configure rtc0) so inittodr() below can fall
 * back cleanly. A function pointer rather than a direct call avoids an
 * unresolved reference to the RTC driver when it isn't configured in.
 */
int (*md_rtc_gettime)(time_t *) = 0;

/*
 * Set by the RTC driver's probe alongside md_rtc_gettime, for writing
 * the clock back. NULL when there is no RTC (resettodr() then does
 * nothing).
 */
int (*md_rtc_settime)(time_t) = 0;

/*
 * Initialize the system clock from the real-time clock, if any,
 * falling back to the supplied base time (the root filesystem's
 * last-write timestamp) when there is no RTC or its reading looks
 * implausible. Called once from init_main.c after the root filesystem
 * is mounted.
 */
void
inittodr(time_t base)
{
	time_t rt;

	if (md_rtc_gettime != 0 && (*md_rtc_gettime)(&rt) == 0) {
		/*
		 * The RTC read is decoded as if it were UTC; if the clock
		 * actually keeps local time, rtc_offset (kern.rtc_offset,
		 * minutes west of UTC) shifts it back to true UTC - e.g.
		 * -540 for a JST clock. rtc_offset is 0 by default (RTC =
		 * UTC), so this is a no-op unless configured.
		 */
		rt += (time_t)rtc_offset * 60;

		/*
		 * Trust the clock unless it is wildly before the filesystem's
		 * last-write time (more than a year), which means it is unset
		 * or wrong - then fall back to the filesystem timestamp. A
		 * clock a little behind the fs (e.g. the image was written on
		 * a PC moments ago) is still fine to use.
		 */
		if (rt >= base - 365L * 24 * 60 * 60) {
			time.tv_sec = rt;
			return;
		}
		printf("inittodr: RTC time unreasonable, using fs time\n");
	}
	time.tv_sec = base;
}

/*
 * Write the current system clock back to the real-time clock, if any.
 * Called from setthetime() (kern_time.c) whenever userland sets the
 * time (e.g. date(1)), so the setting survives a reboot. The kernel
 * clock is UTC; undo the rtc_offset applied in inittodr() so the RTC
 * keeps the same local wall-clock time it is read as. No-op when there
 * is no RTC.
 */
void
resettodr(void)
{
	if (md_rtc_settime != 0)
		(void)(*md_rtc_settime)(time.tv_sec - (time_t)rtc_offset * 60);
}

void
config(void)
{
	struct conf_ctlr *ctlr;
	struct conf_device *dev;

	cpuidentify();
	cpu_initclocks();

	/* Probe and initialize controllers first. */
	for (ctlr = conf_ctlr_init; ctlr->ctlr_driver; ctlr++) {
		if ((*ctlr->ctlr_driver->d_init)(ctlr)) {
			ctlr->ctlr_alive = 1;
		}
	}

	/* Probe and initialize devices. */
	for (dev = conf_device_init; dev->dev_driver; dev++) {
		if (is_controller_alive(dev->dev_cdriver, dev->dev_ctlr)) {
			if ((*dev->dev_driver->d_init)(dev)) {
				dev->dev_alive = 1;
			}
		}
	}
}

/*
 * Toggle the GBA's global interrupt master enable around swtch()'s idle
 * wait. The kernel runs with REG_IME=0 (the syscall trampoline in
 * gba/syscall.c clears it on entry): spl*() are no-ops on this port (see
 * <machine/intr.h>), so masking IME is the kernel's only protection from
 * its interrupt handler. That alone would deadlock a sleeper waiting on a
 * timeout (select()'s tsleep()+swtch()): if TIMER0 can never fire while
 * idling, hardclock()/softclock() can never run to expire the timeout.
 * Bracketing exactly the idle() call with these lets interrupts in only
 * while nothing else runs, and masks them again before swtch() resumes
 * a process.
 */
void
gba_irq_allow(void)
{
	REG_IME = 1;
}

void
gba_irq_block(void)
{
	REG_IME = 0;
}

/*
 * Sit and wait for something to happen...
 */
void
idle(void)
{
	/*
	 * Let TIMER0 fire while we idle so a sleeper blocked with a real
	 * timeout (e.g. select()) can eventually be woken by hardclock()/
	 * softclock(); re-masked before returning (below), before swtch()
	 * resumes any process, since the kernel runs with IME=0.
	 * gba_irq_allow/block just toggle REG_IME (see gba_irq_allow()
	 * above for the full rationale).
	 */
	gba_irq_allow();

	/* Indicate that no process is running. */
	noproc = 1;

	/* Set SPL low so we can be interrupted. */
	int x = spl0();

	led_control(LED_KERNEL, 0);

	/*
	 * No RX interrupt exists on this port's UART (see the big comment on
	 * uart_poll_input() in arch/gba/dev/mgba_uart.c for the full story) -
	 * poll for waiting input here instead, since this is the one place in
	 * the scheduler loop guaranteed to run with interrupts enabled, and
	 * it's called in a tight loop by swtch() precisely when nothing is
	 * runnable (e.g. a process blocked in ttread()'s sleep() waiting for
	 * console input).
	 */
	{
		/*
		 * On-screen software keyboard: turn GBA button presses
		 * into console input for a UART-less machine. Polled
		 * BEFORE uart_poll_input() on purpose - swkbd injects via
		 * ttyinput(), whose echo lands in the tty output queue,
		 * and uart_poll_input() below drains that queue. Draining
		 * it in the same idle() pass flushes the echoed newline of
		 * an Enter keypress before swtch() runs the just-woken
		 * shell (whose command output goes straight out through
		 * uartputc()); polled the other way round, the newline
		 * echo arrived a pass too late and printed after the
		 * command's output ("date" and its result ran together).
		 */
		/*
		 * Mark the scheduler loop live so the Timer0 interrupt may
		 * also poll input (see console_input_poll()); set before the
		 * call so idle()'s own poll is never gated out.
		 */
		console_poll_ready = 1;

		extern void console_input_poll(void);
		console_input_poll();

		/*
		 * Screen blanking (roadmap #6): once the console has been idle
		 * (no output or input) for console_blank_secs seconds, blank the
		 * LCD; console_activity() unblanks it on the next output/input.
		 * Checked here in idle() because a blank only makes sense when
		 * nothing is running - and time.tv_sec advances only about once a
		 * second, so a per-idle-pass compare is cheap. 0 disables it.
		 */
		extern void gtxt_blank(int);
		extern int gtxt_is_blanked(void);
		extern int gba_gfx_active(void);
		if (console_blank_secs > 0 && !gtxt_is_blanked() &&
		    !gba_gfx_active() &&
		    (time.tv_sec - console_last_active) >= console_blank_secs)
			gtxt_blank(1);

		/*
		 * Blink the console's block cursor while idle - i.e. only
		 * when nothing is runnable, which at the shell means we are
		 * waiting for the user to type. A big solid square, 0.5s on
		 * / 0.5s off. Paced off lbolt (the hardclock tick within the
		 * current second, 0..hz-1) - NOT time.tv_usec, which this
		 * port's hardclock never advances (it only bumps tv_sec when
		 * lbolt wraps), so a tv_usec test never blinked. gtxt_cursor()
		 * is idempotent, so this is a bare comparison every pass and
		 * only touches VRAM on the twice-a-second flip; any console
		 * output erases the cursor first (gtxt_putc), so it never
		 * blocks or smears output.
		 */
		extern void gtxt_cursor(int on);
		if (!gtxt_is_blanked() && !gba_gfx_active())
			gtxt_cursor(lbolt < hz / 2);
	}

	/* Restore previous SPL. */
	splx(x);

	gba_irq_block();
}

void
cpu_reboot(void)
{
	/*
	 * Reboot WITHOUT any BIOS SWI, so it behaves identically on real
	 * hardware and in the emulators. The undocumented HardReset (SWI 0x26)
	 * rebooted real hardware but hangs mGBA (no HLE for it); the documented
	 * SoftReset (0x00) leaves stale I/O and, even paired with RegisterRamReset
	 * (0x01), left the next boot's init wild-jumping.
	 *
	 * Instead: mask interrupts and quiet the peripherals whose leftover state
	 * would otherwise feed the next boot before it re-inits (display, DMA,
	 * timers, sound), then warm-jump to the cartridge ROM entry (0x08000000).
	 * reset: in locore0.S re-initialises .data/.bss/.fast_text, the stack and
	 * the IRQ vector exactly as on a cold boot.
	 */
	int i;

	REG_IME = 0;			/* master interrupt disable */
	REG_IE = 0;
	REG_IF = 0xFFFF;		/* acknowledge anything pending */
	for (i = 0; i < 4; i++)		/* stop all four DMA channels */
		*(volatile uint16_t *)(REG_BASE + 0x00BA + i * 12) = 0;
	REG_TM0CNT_H = 0;		/* stop the timers */
	REG_TM1CNT_H = 0;
	REG_TM2CNT_H = 0;
	REG_TM3CNT_H = 0;
	REG_SOUNDCNT_X = 0;		/* master sound off */
	REG_DISPCNT = 0x0080;		/* forced blank - clear stale video output */

	/*
	 * Wipe EWRAM below our own stack so no stale RAM from the previous
	 * session survives into the next boot: the u/u0 process-context area and
	 * other fixed-address EWRAM are not covered by reset:'s .bss clear, and
	 * stale contents there made the rebooted init wild-jump. This runs on
	 * the kernel stack in UAREA, which the loop clears too, so nothing may
	 * use the stack after it; the jump below does not return.
	 */
	{
		volatile uint32_t *p = (volatile uint32_t *)0x02000000;
		int n = (0x0203F000 - 0x02000000) / 4;
		for (i = 0; i < n; i++)
			p[i] = 0;
	}

	((void (*)(void))0x08000000)();	/* warm boot (ARM ROM entry); never returns */
}

void
boot(dev_t dev, int howto)
{

	if ((howto & RB_NOSYNC) == 0 && waittime < 0 && bfreelist[0].b_forw) {
		struct fs *fp;
		struct buf *bp;
		int iter, nbusy;

		/*
		 * Force the root filesystem's superblock to be updated,
		 * so the date will be as current as possible after
		 * rebooting.
		 */
		fp = getfs(rootdev);
		if (fp)
			fp->fs_fmod = 1;
		waittime = 0;
		printf("syncing disks... ");
		(void)splnet();
		/*
		 * Let the SD driver (sd.c) wait far longer than it safely
		 * can during ordinary runtime for each write's card-ready
		 * check to actually clear, rather than the tight bound
		 * needed there to avoid hanging on a genuinely unresponsive
		 * card mid-session - see the comment on sd_shutdown_flush's
		 * declaration in sd.c. Real-hardware corruption ("CANNOT
		 * READ: BLK n" / "UNEXPECTED INCONSISTENCY" on the very next
		 * boot after a clean-looking "syncing disks... done") was
		 * observed following heavy sustained write activity right
		 * before a sync;sync;reboot - consistent with the SD card's
		 * real internal write-completion time occasionally
		 * outlasting that tight runtime bound.
		 */
#ifdef SD_ENABLED
		extern int sd_shutdown_flush;
		sd_shutdown_flush = 1;
#endif
		sync();
		for (iter = 0; iter < 20; iter++) {
			nbusy = 0;
			for (bp = &buf[NBUF]; --bp >= buf;)
				if (bp->b_flags & B_BUSY)
					nbusy++;
			if (nbusy == 0)
				break;
			printf("%d ", nbusy);
			mdelay(40L * iter);
		}
#ifdef SD_ENABLED
		sd_shutdown_flush = 0;
#endif
		printf("done\n");
	}
	(void)splhigh();
	if (!(howto & RB_HALT)) {
		if ((howto & RB_DUMP) && dumpdev != NODEV) {
			/*
			 * Take a dump of memory by calling (*dump)(),
			 * which must correspond to dumpdev.
			 * It should dump from dumplo blocks to the end
			 * of memory or to the end of the logical device.
			 */
			(*dump)(dumpdev);
		}
		/* Restart from dev, howto: see cpu_reboot(). */
		cpu_reboot();
		/* NOTREACHED */
	}
	printf("halted\n");

#ifdef HALTREBOOT
	printf("press any key to reboot...\n");
	/*
	 * Wait for any key, then reboot. cngetc() is serial-only on this port and
	 * the software keyboard is dead once the scheduler has stopped, so poll
	 * the raw keypad directly - a button press works the same on real
	 * hardware (GBA/GBAED) and in the emulators. REG_KEYINPUT bits are
	 * active-low (0 = pressed); wait until any of the ten keys is down.
	 *
	 * On a build with a serial console (GBAED), also accept a byte on the
	 * serial link so a host at the other end of the cable can reboot without
	 * touching the GBA - gsio_avail() is a side-effect-free peek at the
	 * receive-data flag, so polling it here does not disturb anything.
	 */
	for (;;) {
		if ((REG_KEYINPUT & 0x03ff) != 0x03ff)
			break;
#ifdef SERIAL_CONSOLE
		if (gsio_avail())
			break;
#endif
	}

	cpu_reboot();
	/* NOTREACHED */
#endif

	printf("reboot failed; spinning\n");
	for (;;)
		;
	/* NOTREACHED */
}

/*
 * Millisecond delay routine: not implemented on the GBA (an empty stub -
 * the shutdown sync loop calls it).  Code that needs a real delay
 * busy-waits on its own (see sd.c), or times with Timer2/Timer3.
 */
void
mdelay(u_int msec)
{
}

/*
 * Control LEDs, installed on the board.
 */
void
led_control(int mask, int on)
{
}

/*
 * Increment user profiling counters.
 */
void
addupc(caddr_t pc, struct uprof *pbuf, int ticks)
{
	u_int indx;

	if (pc < (caddr_t)pbuf->pr_off)
		return;

	indx = pc - (caddr_t)pbuf->pr_off;
	indx = (indx * pbuf->pr_scale) >> 16;
	if (indx >= pbuf->pr_size)
		return;

	pbuf->pr_base[indx] += ticks;
}

/*
 * ffs -- vax ffs instruction
 */
int
ffs(u_long mask)
{
	int cnt;

	if (mask == 0)
		return 0;
	for (cnt = 1; !(mask & 1); cnt++)
		mask >>= 1;

	return cnt;
}

/*
 * Copy a null terminated string from one point to another.
 * Returns zero on success, ENOENT if maxlength exceeded.
 * If lencopied is non-zero, *lencopied gets the length of the copy
 * (including the null terminating byte).
 */
int
copystr(caddr_t src, caddr_t dest, u_int maxlength, u_int *lencopied)
{
	caddr_t dest0 = dest;
	int error = ENOENT;

	if (maxlength != 0) {
		while ((*dest++ = *src++) != '\0') {
			if (--maxlength == 0) {
				/* Failed. */
				goto done;
			}
		}
		/* Succeeded. */
		error = 0;
	}
done:
	if (lencopied != 0)
		*lencopied = dest - dest0;

	return error;
}

/*
 * Calculate the length of a string.
 */
size_t
strlen(const char *s)
{
	const char *s0 = s;

	while (*s++ != '\0')
		;

	return s - s0 - 1;
}

/*
 * Return 0 if a user address is valid.
 * There is only one memory region allowed for user: RAM.
 */
int
baduaddr(caddr_t addr)
{
	if (addr >= (caddr_t)__user_data_start &&
	    addr < (caddr_t)__user_data_end)
		return 0;

	return 1;
}

/*
 * Return 0 if a kernel address is valid.
 * There are three memory regions allowed for kernel: EWRAM (U0AREA/
 * UAREA only - see __kernel_data_start/end in kern_gba.ldscript), IWRAM,
 * and flash/ROM.
 *
 * IWRAM holds the kernel's .data/.bss (proc[], nproc and the other
 * globals ps(1) reads via /dev/kmem), so it must be accepted here:
 * mmrw() (dev/mem.c) rejects an address that is both badkaddr() and
 * baduaddr().
 */
int
badkaddr(caddr_t addr)
{
	extern char __iwram_start[], __iwram_end[];

	if (addr >= (caddr_t)__kernel_data_start &&
	    addr < (caddr_t)__kernel_data_end)
		return 0;
	if (addr >= (caddr_t)__iwram_start &&
	    addr < (caddr_t)__iwram_end)
		return 0;
	if (addr >= (caddr_t)__kernel_flash_start &&
	    addr < (caddr_t)__kernel_flash_end)
		return 0;

	return 1;
}

/*
 * Insert the specified element into a queue immediately after
 * the specified predecessor element.
 */
void
insque(void *element, void *predecessor)
{
	struct que {
		struct que *q_next;
		struct que *q_prev;
	};

	struct que *e = (struct que *)element;
	struct que *prev = (struct que *)predecessor;

	e->q_prev = prev;
	e->q_next = prev->q_next;
	prev->q_next->q_prev = e;
	prev->q_next = e;
}

/*
 * Remove the specified element from the queue.
 */
void
remque(void *element)
{
	struct que {
		struct que *q_next;
		struct que *q_prev;
	};

	struct que *e = (struct que *)element;

	e->q_prev->q_next = e->q_next;
	e->q_next->q_prev = e->q_prev;
}

/*
 * Compare strings.
 */
int
strncmp(const char *s1, const char *s2, size_t n)
{
	int ret, tmp;

	if (n == 0)
		return 0;

	do {
		ret = *s1++ - (tmp = *s2++);
	} while ((ret == 0) && (tmp != 0) && --n);

	return ret;
}

/* Nonzero if pointer is not aligned on a "sz" boundary. */
#define UNALIGNED(p, sz)	((u_int)(p) & ((sz) - 1))

/* bcopy() and bzero() are in fastcopy.S. */

void *
memcpy(void *dst, const void *src, size_t nbytes)
{
	bcopy(src, dst, nbytes);

	return dst;
}


/*
 * Compare not more than nbytes of data pointed to by m1 with
 * the data pointed to by m2.
 * Return an integer greater than, equal to or less than zero
 * according to whether the object pointed to by m1 is greater
 * than, equal to or less than the object pointed to by m2.
 */
int
bcmp(const void *m1, const void *m2, size_t nbytes)
{
	const u_char *s1 = (const u_char *)m1;
	const u_char *s2 = (const u_char *)m2;
	const u_int *aligned1, *aligned2;

	/*
	 * If the size is too small, or either pointer is unaligned,
	 * then we punt to the byte compare loop.
	 * Hopefully this will not turn up in inner loops.
	 */
	if (nbytes >= 4 * sizeof(u_int) &&
	    !UNALIGNED(s1, sizeof(u_int)) &&
	    !UNALIGNED(s2, sizeof(u_int))) {
		/* Otherwise, load and compare the blocks of memory one
		   word at a time. */
		aligned1 = (const u_int *)s1;
		aligned2 = (const u_int *)s2;
		while (nbytes >= sizeof(u_int)) {
			if (*aligned1 != *aligned2)
				break;
			aligned1++;
			aligned2++;
			nbytes -= sizeof(u_int);
		}

		/* Check remaining characters. */
		s1 = (const u_char *)aligned1;
		s2 = (const u_char *)aligned2;
	}
	while (nbytes--) {
		if (*s1 != *s2)
			return *s1 - *s2;
		s1++;
		s2++;
	}

	return 0;
}

int
copyout(caddr_t from, caddr_t to, u_int nbytes)
{
	if (baduaddr(to) || baduaddr(to + nbytes - 1))
		return EFAULT;
	bcopy(from, to, nbytes);

	return 0;
}

int
copyin(caddr_t from, caddr_t to, u_int nbytes)
{
	if (baduaddr(from) || baduaddr(from + nbytes - 1))
		return EFAULT;
	bcopy(from, to, nbytes);

	return 0;
}
