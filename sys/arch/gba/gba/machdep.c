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
#include <machine/gba_syscall.h>

int cpu_khz = 16777;
int bus_khz = 16777;

char	machine[] = MACHINE;		/* from <machine/machparam.h> */
char	machine_arch[] = MACHINE_ARCH;	/* from <machine/machparam.h> */
char	cpu_model[64];

int	hz = HZ;
int	usechz = (1000000L + HZ - 1) / HZ;

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
volatile int need_resched;

volatile uint16_t frame_count = 0;
volatile uint32_t myticks = 1;
volatile int timer0_flag;

/*
 * DBG: lr_irq, stashed by gba_intr_handler (locore0.S) before it
 * switches out of IRQ mode and that banked register becomes
 * unreachable. Used below to see where the interrupted code actually
 * was - chasing a wild-jump crash/reboot on real GBAED hardware that
 * happens between syscalls (confirmed: the bounds check in
 * syscall_handler(), gba/syscall.c, never fires before it), so a
 * check only at syscall return can't catch it; this fires on every
 * timer/vblank tick instead, while the corruption is presumably still
 * fresh. -4 is the standard ARM IRQ return-address adjustment (lr_irq
 * = address of the interrupted instruction + 4, both ARM and Thumb).
 */
volatile unsigned irq_last_lr;

static void check_irq_pc(void)
{
    extern char __swap_start[], __swap_end[];
    unsigned pc = irq_last_lr - 4;

    if (pc >= (unsigned)__swap_start && pc < (unsigned)__swap_end) {
        printf("DBG: IRQPC in SWAP! irq_last_lr=%x pc=%x\n",
            irq_last_lr, pc);
    }
}

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
 * constantly, so it is prompt. (A pure compute loop that never syscalls
 * still won't see it until it does - the interrupt return path does not
 * run userret - but that is rare and out of scope here.)
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

IWRAM_CODE THUMB_CODE void gba_do_schedule(void)
{
    uint16_t flag = REG_IF;
    uint16_t ack = 0;

    check_irq_pc();
#ifdef SERIAL_CONSOLE
    if (flag & IRQ_SERIAL) {    // serial byte received (or send done)
        extern void gsio_rx_isr(void);
        ack |= IRQ_SERIAL;
        gsio_rx_isr();          /* capture the byte the instant it lands */
    }
#endif
    if (flag & IRQ_TIMER0) { // TIMER0
        ack |= IRQ_TIMER0;
        myticks++;
        timer0_flag = 1;
        need_resched = 1;
        /*
         * Advance the kernel clock: this IS the Timer0 interrupt, so
         * hardclock() - which advances lbolt and processes the
         * timeout/callout queue - must be called here directly.
         * (Earlier it was missing entirely: TIMER0's interrupt fired,
         * but nothing called hardclock(), so a tsleep() with a real
         * timeout - e.g. select()'s ts argument - computed a valid
         * wakeup tick and then waited forever, since nothing ever
         * checked whether it had expired.)
         */
        hardclock((caddr_t)0, 0);

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
    /* Acknowledge the sources we handled; if somehow none matched, clear
     * whatever was pending so the interrupt can't latch up. */
    REG_IF = ack ? ack : flag;
}

void irq_enable(void)
{
	REG_IME = 0;

	REG_TM0CNT_H = 0;
	//REG_TM0CNT_L = 65263;//60Hz 65536 - (16777216 / 1024 / HZ);
	REG_TM0CNT_L = 65372;//100Hz 65536 - (16777216 / 1024 / HZ);

	// 0x0040: 割り込み有効
	// 0x0003: 1024分周
	// 0x0080: 開始
	REG_TM0CNT_H = 0x00C3;

	/*
	 * Only Timer0 (IRQ_TIMER0 = bit3) drives anything - it runs
	 * hardclock(). VBLANK (bit0) was previously enabled too (DISPSTAT
	 * bit3 + REG_IE bit0) but its handler only ever set vblank_flag,
	 * which nothing read; on-screen input and the cursor blink are
	 * polled via REG_VCOUNT/lbolt, not the VBLANK IRQ. Leaving it out
	 * so the only interrupt that fires is the one that has real work.
	 *
	 * IRQ_SERIAL (bit7) is added on the serial-console build so a byte
	 * arriving on the link cable is captured immediately by gsio_rx_isr()
	 * (gba_sio_uart.c) instead of being polled for and lost - see there.
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

	/*
	 * Load persistent boot parameters from cartridge SRAM, falling back to
	 * the compiled defaults if the block is blank/invalid. Meaningful only
	 * on the mrams/ROM-root build (a no-op elsewhere): the SD-root GBAED
	 * build cannot persist SRAM and reapplies settings from /etc/rc.local
	 * instead. See sram.c.
	 */
	{
		extern void sram_params_init(void);
		sram_params_init();
	}

	/*
	 * When User button is pressed - boot to single user mode.
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
 * is found. Left NULL on builds/hardware without one (e.g. the mGBA
 * config, which does not compile rtc.c) so inittodr() below can fall
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
 * wait. The syscall trampoline (gba/syscall.c) keeps REG_IME=0 for the
 * entire duration of syscall_handler(), because it uses r0-r2 as live
 * scratch across several raw instructions with no protection against a
 * reentrant interrupt (confirmed via GDB: enabling REG_IME mid-trampoline
 * let an interrupt clobber r0-r2 with garbage, crashing the very next
 * syscall). But that leaves a real deadlock: select()'s tsleep()+swtch()
 * can genuinely block waiting on a timeout, and swtch()'s idle() loop is
 * ordinary compiler-generated C (a normal function call boundary, so no
 * live scratch registers to protect) - if TIMER0 can never fire while
 * idling, hardclock()/softclock() can never run to expire that timeout,
 * so the sleeper can never be woken. Bracketing exactly the idle() call
 * with these keeps interrupts enabled only while there is truly nothing
 * else running, then re-masks them before resuming whatever process was
 * found, restoring the invariant the syscall trampoline depends on.
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
	 * resumes any process, to restore the invariant the syscall
	 * trampoline (gba/syscall.c) depends on. This bracketing used to
	 * live in the MI swtch() (kern/kern_synch.c); moved here to keep
	 * that file machine-independent. gba_irq_allow/block just toggle
	 * REG_IME (see gba_irq_allow() above for the full rationale).
	 */
	gba_irq_allow();

	/* Indicate that no process is running. */
	noproc = 1;

	/* Set SPL low so we can be interrupted. */
	int x = spl0();

	led_control(LED_KERNEL, 0);

	/*
	 * No RX interrupt exists on this port's UART (see the big comment
	 * on uart_poll_input() in arch/gba/dev/mgba_uart.c for the full
	 * story) - poll for waiting input here instead, since this is the
	 * one place in the scheduler loop guaranteed to run with
	 * interrupts enabled and no live scratch registers to protect,
	 * and it's called in a tight loop by swtch() precisely when
	 * nothing is runnable (e.g. a process blocked in ttread()'s
	 * sleep() waiting for console input).
	 */
	{
		/*
		 * Ruled out as the cause of an early-boot crash (identical
		 * DBG trail and crash address with this disabled) - see the
		 * comment on uart_poll_input() in mgba_uart.c for what this
		 * does and why it's here.
		 */
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
		if (console_blank_secs > 0 && !gtxt_is_blanked() &&
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
		if (!gtxt_is_blanked())
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
	 * Was a raw jump to _start - a "warm jump" that leaves IWRAM,
	 * CPU mode/register state, and peripheral registers exactly as
	 * the crashed/previous session left them, only reinitialized by
	 * whatever this port's own boot code happens to touch. The
	 * mojibake glyph seen at the very start of every reboot's console
	 * output (before "DiscoBSD..." banner) is consistent with stale
	 * leftover state feeding into the console driver before it
	 * re-inits.
	 *
	 * Tried the BIOS's documented SoftReset (SWI 0x00) first: it
	 * clears the reserved IWRAM area and CPU-mode stack pointers, but
	 * -not- I/O register space (0x04000000+) - confirmed still leaving
	 * an occasional early hang after a panic-triggered reboot,
	 * consistent with some I/O register (REG_IME's stale value was
	 * one candidate; explicitly zeroing it at the top of locore0.S's
	 * reset: didn't fully fix it either, so something else in that
	 * space is apparently also still stale) surviving into the next
	 * boot in a state this port's init code doesn't expect. Now using
	 * the undocumented HardReset (SWI 0x26) instead - per GBATEK
	 * (problemkaputt.de/gbatek-bios-reset-functions.htm), it performs
	 * a much more thorough reset closer to actual power-cycling.
	 * Undocumented and known to vary across real hardware revisions,
	 * but SoftReset's narrower reset demonstrably isn't sufficient
	 * here, so trading that documented-but-insufficient guarantee for
	 * this broader one. r0 selects the post-reset boot target; 0 is
	 * the conventional "same as normal cold boot" value.
	 */
	__asm__ volatile (
	    "mov r0, #0\n\t"
	    "swi 0x26"
	    ::: "r0"
	);
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
		/* Restart from dev, howto. */

		/*
		 * Reset microcontroller. Was left as a commented-out
		 * NVIC_SystemReset() (an STM32 call, meaningless on GBA)
		 * with no GBA-appropriate replacement, so a real reboot(8)
		 * request (RB_HALT not set) fell all the way through to
		 * the halt-and-wait-for-a-keypress path below exactly like
		 * halt(8) does - reboot(8) never actually rebooted
		 * unattended, silently waiting for a keypress instead.
		 */
		cpu_reboot();
		/* NOTREACHED */
	}
	printf("halted\n");

#ifdef HALTREBOOT
	printf("press any key to reboot...\n");
	cngetc();

	/* Reset microcontroller. */
	cpu_reboot();
	/* NOTREACHED */
#endif

	printf("reboot failed; spinning\n");
	for (;;)
		;
	/* NOTREACHED */
}

/*
 * Millisecond delay routine.
 *
 * Uses SysTick, which must be configured to a 1ms timebase.
 * This is a busy-wait blocking delay, so be wise with use.
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
 * UAREA only - see __kernel_data_start/end in kern.ldscript), IWRAM,
 * and flash/ROM.
 *
 * __kernel_data_start/end only ever covered that narrow EWRAM slice,
 * not IWRAM - where this port's actual kernel .data/.bss (every
 * ordinary kernel global, including proc[]/nproc that ps(1) reads via
 * /dev/kmem) lives (see kern.ldscript's IWRAM block and the .data/.bss
 * VMAs in the linked image). Confirmed via ps failing "/dev/kmem: Bad
 * address" on every attempt: mmrw() (dev/mem.c) rejects any address
 * that's *both* badkaddr() and baduaddr(), and an IWRAM address is
 * neither in this narrow EWRAM range nor in USERRAM, so every kernel
 * global outside that one small EWRAM slice was unreadable through
 * /dev/kmem.
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

/*
 * Copy data from the memory region pointed to by src0 to the memory
 * region pointed to by dst0.
 * If the regions overlap, the behavior is undefined.
 */
void
bcopy(const void *src0, void *dst0, size_t nbytes)
{
	u_char		*dst = dst0;
	const u_char	*src = src0;
	u_int		*aligned_dst;
	const u_int	*aligned_src;

	/* printf("bcopy (%08x, %08x, %d)\n", src0, dst0, nbytes); */
	/* If the size is small, or either SRC or DST is unaligned,
	 * then punt into the byte copy loop.  This should be rare. */
	if (nbytes >= 4 * sizeof(u_int) &&
	    !UNALIGNED(src, sizeof(u_int)) &&
	    !UNALIGNED(dst, sizeof(u_int))) {
		aligned_dst = (u_int *)dst;
		aligned_src = (const u_int *)src;

		/* Copy 4X unsigned words at a time if possible. */
		while (nbytes >= 4 * sizeof(u_int)) {
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			nbytes -= 4 * sizeof(u_int);
		}

		/* Copy one unsigned word at a time if possible. */
		while (nbytes >= sizeof(u_int)) {
			*aligned_dst++ = *aligned_src++;
			nbytes -= sizeof(u_int);
		}

		/* Pick up any residual with a byte copier. */
		dst = (u_char *)aligned_dst;
		src = (const u_char *)aligned_src;
	}

	while (nbytes--)
		*dst++ = *src++;
}

void *
memcpy(void *dst, const void *src, size_t nbytes)
{
	bcopy(src, dst, nbytes);

	return dst;
}

/*
 * Fill the array with zeroes.
 */
void
bzero(void *dst0, size_t nbytes)
{
	u_char *dst;
	u_int *aligned_dst;

	dst = (u_char *)dst0;
	while (UNALIGNED(dst, sizeof(u_int))) {
		*dst++ = 0;
		if (--nbytes == 0)
			return;
	}

	if (nbytes >= sizeof(u_int)) {
		/*
		 * If we get this far, we know that nbytes is large
		 * and dst is word-aligned.
		 */
		aligned_dst = (u_int *)dst;

		while (nbytes >= 4 * sizeof(u_int)) {
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			nbytes -= 4 * sizeof(u_int);
		}
		while (nbytes >= sizeof(u_int)) {
			*aligned_dst++ = 0;
			nbytes -= sizeof(u_int);
		}
		dst = (u_char *)aligned_dst;
	}

	/* Pick up the remainder with a bytewise loop. */
	while (nbytes--)
		*dst++ = 0;
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
	/* printf("copyout(from=%p, to=%p, nbytes=%u)\n", from, to, nbytes); */
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
