#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/config.h>
#include <sys/fcntl.h>
#include <sys/errno.h>

#include <gba/dev/mgbalog.h>
#include <gba/dev/gba_text.h>
#include <gba/dev/gba_sio_uart.h>

void uartinit(int);
void uartputc(dev_t dev, char c);
void uartputc_kmsg(dev_t dev, char c);	/* r_write slot: kernel msgs (green) */
char uartgetc(dev_t dev);
void uart_poll_input(void);
void console_activity(void);		/* machdep.c: console screen-saver */
extern struct tty uartttys[];

/*
 * Set nonzero when running under the mGBA emulator (detected at boot in
 * uartinit()). Gates the mGBA debug-log register writes in uartputc()/
 * uartputs() so the SAME GBA kernel drives the emulator log under mGBA but
 * stays silent on real hardware.
 */
int mgba_present;

void uartinit(int unit) {
    /*
     * Two optional output sinks mirror the on-screen (gtxt) console,
     * each selected by a kernel config option (see the GBA/GBAED Config
     * files):
     *   MGBA_LOG        - compile in the mGBA emulator debug-log mirror.
     *                     Which is actually USED is decided at runtime:
     *                     uartinit() probes for mGBA (below) and only then
     *                     is the log written, so a MGBA_LOG kernel is
     *                     correct on real hardware too (silent there). The
     *                     GBAED build, which only ever runs on hardware,
     *                     leaves it out entirely.
     *   SERIAL_CONSOLE  - real UART over the link cable (gsio), for
     *                     interactive debugging; the GBAED build enables
     *                     it, the cable-free GBA demo build does not (it
     *                     would otherwise spin on CTS with no peer).
     * Only the console unit gets set up here -- uartinit() is also
     * called once for the non-console UART during device attach.
     */
    if (unit == CONS_MINOR) {
        gtxt_init(GTXT_WHITE, GTXT_BLACK); // console: white on black
#ifdef MGBA_LOG
        /*
         * Detect mGBA at runtime rather than assuming it. Writing 0xC0DE to
         * the debug-enable register and reading back 0x1DEA identifies the
         * emulator (the write also arms mGBA's logging). On real hardware
         * that address is open bus, so the readback is not 0x1DEA,
         * mgba_present stays 0, and every later debug-register write is
         * skipped. Announce a hit so a hardware-vs-emulator demo of the one
         * GBA kernel shows the difference in the boot log. printf() is live
         * by now: startup() calls us before it prints the version banner.
         */
        MGBA_REG_DEBUG_ENABLE = 0xC0DE;
        mgba_present = (MGBA_REG_DEBUG_ENABLE == 0x1DEA);
        if (mgba_present)
            printf("mGBA detected.\n");
#endif
#ifdef SERIAL_CONSOLE
        gsio_init(UART_BAUD);
#endif
    }
}
    
static int
uartprobe(struct conf_device *config) {
    int unit = config->dev_unit - 1;
    int is_console = (CONS_MAJOR == UART_MAJOR &&
                      CONS_MINOR == unit);
    if (unit < 0 || unit >= NUART)
        return 0;

    if (! is_console)
        uartinit(unit);

    return 1;
}

/* conf.o が期待するシンボル名に変更 */
int uartopen(dev_t dev, int flag, int mode)
{
    /*
     * This is a hardwired point-to-point link-cable connection, not a
     * modem line - there is no real carrier-detect signal, so (as is
     * standard for a local/directly-connected tty) just declare
     * carrier permanently present. Without this, ttyselect()'s FREAD
     * case treats "carrier off" as an alternate always-ready
     * condition, which - since nothing ever sets TS_CARR_ON otherwise
     * on this port - would make select() report the console readable
     * unconditionally regardless of whether anything was actually
     * typed.
     *
     * Deliberately does NOT call the generic ttyopen() - that also
     * establishes the controlling-tty association (u.u_ttyp/u.u_ttyd),
     * which getty's own pre-existing vhangup() call (its normal
     * per-session setup, unrelated to this session's changes) then had
     * something real to act on for the first time and sent SIGHUP into -
     * confirmed on real GBAED hardware: login reached the "login:"
     * prompt but a SIGHUP arrived and killed/reset whatever was reading
     * it, right where ttyopen() was tried. vhangup() is a no-op while
     * u.u_ttyp == NULL (sys_inode.c), so leaving u.u_ttyp unset keeps it
     * harmless. This console's whole process chain (getty -> login ->
     * shell, all one continuous boot lineage, not genuinely separate
     * dial-in sessions) doesn't need real controlling-tty semantics.
     *
     * But it DOES need the other half ttyopen() would set: the tty's
     * foreground process group, tp->t_pgrp. ttyinput()'s ^C/^\ handling
     * signals it via gsignal(t_pgrp, SIGINT), and gsignal() is a no-op
     * while t_pgrp == 0 (kern_sig.c) - so without this, ^C did nothing
     * at all, even at an idle prompt. Set t_pgrp (and put the opener in
     * that group) here, WITHOUT touching u.u_ttyp, so ^C works while
     * vhangup() stays a no-op.
     *
     * Skip init (pid 1): it holds /dev/console open for its own logging,
     * and putting init into the signalled group would let a stray ^C
     * deliver SIGINT to init (panic risk). init keeps p_pgrp == 0; the
     * getty->login->shell lineage forks from init afterwards and claims
     * its own group here when it opens the console, and the shell's
     * child commands inherit it (fork copies p_pgrp) - so ^C reaches the
     * running command and the shell, but never init.
     */
    struct tty *tp = &uartttys[minor(dev)];
    struct proc *pp = u.u_procp;

    tp->t_state |= TS_CARR_ON;

    if (pp->p_pid != 1 && pp->p_pgrp == 0) {
        tp->t_pgrp = pp->p_pid;
        pp->p_pgrp = pp->p_pid;
    }

    return 0;
}
int uartclose(dev_t dev, int flag, int mode) { return 0; }
int uartread(dev_t dev, struct uio *uio, int flag)
{
    /*
     * Used to read one already-arrived raw byte per call directly off
     * the wire via gsio_getc(), completely bypassing the tty line
     * discipline - so ttyinput() (echo, erase/kill, cooked-line
     * assembly) never ran for anything a process actually read,
     * regardless of the uart_poll_input() fix in idle() (that fed
     * ttyinput() from the *idle* loop, but every byte a blocked
     * reader consumed here first never reached it). Delegating to the
     * generic ttread() instead makes this an ordinary tty device: it
     * pulls from t_canq/t_rawq (already fed by uart_poll_input()) and
     * sleeps on &tp->t_rawq via the standard TTIPRI sleep when empty,
     * which ttyinput()'s ttwakeup() (called from idle()'s polling)
     * wakes back up once more input arrives - see machdep.c's idle().
     */
    struct tty *tp = &uartttys[minor(dev)];

    return ttread(tp, uio, flag);
}
int uartwrite(dev_t dev, struct uio *uio, int flag)
{
    /*
     * Deliberately still bypasses ttwrite()/ttstart()/t_outq for
     * *outgoing* data - conf.c wires this device's stop slot to
     * nullstop (no real d_start callback exists), so routing normal
     * write(2) traffic through the output queue would need one to be
     * written just to drain it again. uart_poll_input() (see its own
     * comment) already hand-drains t_outq for the one thing that
     * actually queues into it - ttyinput()'s echo - so this simpler
     * direct-to-wire path is kept for everything else: push bytes
     * straight out through uartputc(), the same raw path the kernel's
     * own printf() uses.
     */
    extern void console_input_poll(void);
    char buf[16];
    int n, i, error;
    register struct proc *pp = u.u_procp;

    while (uio->uio_resid > 0) {
        /*
         * Keep ^C responsive while a command is streaming output. This
         * write runs with interrupts masked - the syscall trampoline holds
         * REG_IME=0 for the whole syscall (gba/syscall.c) - so the
         * Timer0-driven console_input_poll() that normally samples the
         * keyboard and serial line cannot fire until the write returns.
         * A long "cat" would therefore run to the end before an interrupt
         * key was even noticed. Sample the console here instead, each
         * chunk: swkbd and serial alike reach ttyinput(), whose intr-char
         * handling raises SIGINT on the foreground process group.
         */
        console_input_poll();

        /*
         * ...and act on it. If an unblocked interrupt or quit signal is now
         * pending for us, stop and return so the syscall's exit path
         * delivers it - otherwise the signal would only take effect once
         * the entire output had drained, which is what made ^C feel dead
         * mid-scroll.
         */
        if (pp->p_sig & ~pp->p_sigmask & (sigmask(SIGINT) | sigmask(SIGQUIT)))
            return EINTR;

        n = uio->uio_resid;
        if (n > sizeof(buf))
            n = sizeof(buf);
        error = uiomove(buf, n, uio);
        if (error)
            return error;
        for (i = 0; i < n; i++)
            uartputc(dev, buf[i]);
    }
    return 0;
}
int uartioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    /*
     * Was an unconditional `return -1;` stub - every sgtty ioctl
     * (TIOCGETP/TIOCSETP/TIOCGETC/...) failed for this console no
     * matter what, which silently broke isatty() (used by ttyname(),
     * in turn used by login's rootterm() /etc/ttys "secure" check -
     * confirmed root cause of "root login refused on this terminal"
     * on real GBAED hardware) and every stty(1)/getty/login attempt
     * to configure tty modes (ECHO included) on this console, and
     * surfaced elsewhere as e.g. stty's "TIOCMGET: Unknown error: 0".
     * (uartread() now goes through the real tty line discipline too -
     * see its own comment and uart_poll_input() below.)
     */
    register struct tty *tp = &uartttys[minor(dev)];
    int error;

    /*
     * The on-screen (LCD) console has a fixed 30x15 geometry, so pin its
     * window size on every ioctl - in particular so TIOCGWINSZ always
     * reports 30x15 to full-screen apps (vi). login(1) deliberately clears
     * the window size to 0x0 on local login (login.c, "if (!hflag)"), and
     * nothing on a hardwired console re-establishes it; re-asserting it
     * here, just before ttioctl() reads or writes it, keeps it authoritative.
     */
    if (minor(dev) == CONS_MINOR) {
        tp->t_winsize.ws_col = 30;
        tp->t_winsize.ws_row = 15;
    }

    error = ttioctl(tp, cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    return (error);
}

/*
 * Feed any bytes currently waiting on the wire through the real tty
 * line discipline, so echo/erase/kill/cooked-line-assembly (ttyinput())
 * actually run for console input - previously uartread() bypassed all
 * of that (see its own comment) and just handed back raw bytes, so
 * ttyinput() was never called at all: no ECHO despite the tty flags
 * saying ECHO is on (confirmed via a since-removed diagnostic that
 * never fired), and every user-visible symptom that goes with it
 * (silent typing at the shell prompt, per user report on real GBAED
 * hardware once fsck/login finally started working this session).
 *
 * No RX interrupt exists on this port (gsio_getc() is a plain
 * busy-wait primitive - see gba_sio_uart.c), so there is nothing to
 * feed ttyinput() asynchronously as bytes arrive. Called instead from
 * idle() (arch/gba/gba/machdep.c), which - per the comment on
 * gba_irq_allow() there - is exactly the one place in the scheduler
 * loop guaranteed to run with interrupts enabled and no live scratch
 * registers to protect, and gets called in a tight loop by swtch()
 * whenever nothing is runnable (i.e. exactly the "some process is
 * blocked in ttread()'s sleep() waiting for input" case this exists
 * for). Draining t_outq by hand afterward stands in for a proper
 * ttstart()/d_start callback (conf.c wires uartioctl's stop slot to
 * nullstop, so nothing else ever drains ttyecho()'s queued bytes) -
 * fine for this port's needs since uartputc() itself never blocks
 * waiting on anything but the far end's own byte-at-a-time CTS/busy
 * flags.
 */
void
uart_poll_input(void)
{
    struct tty *tp = &uartttys[CONS_MINOR];
    int c;
    int n;

    /*
     * Bounded defensively (a human can't type anywhere near this many
     * characters between two idle() calls, which fire in a tight loop
     * whenever the system is idle) - this whole port's history this
     * session has one hardware-noise scare too many to leave a raw
     * hardware-status-driven while() loop unbounded here.
     *
     * Uses gsio_getc_bounded(), not gsio_getc(), on purpose: this is
     * called automatically from idle() based on gsio_avail()'s signal
     * alone, not because a process explicitly chose to block on
     * read() - if gsio_avail() ever reports a byte ready that never
     * actually completes (confirmed on real mGBA: boot hung solid
     * right after the "swap size" banner, exactly where idle() first
     * runs, with nothing typed yet - an emulator with no serial peer
     * wired up plausibly doesn't model idle-line register state the
     * same way real hardware does), gsio_getc()'s own unbounded wait
     * would hang idle() - and with it swtch()'s entire wait loop,
     * i.e. the whole scheduler - forever.
     */
#ifdef SERIAL_CONSOLE
    /*
     * Drain the interrupt-driven receive ring (gba_sio_uart.c). The bytes
     * were captured off the single-byte SIO register the instant they
     * arrived by gsio_rx_isr(); here we just hand them to the tty at
     * idle()/Timer0 pace. Bounded at the ring size - it can hold no more.
     */
    {
        extern void gsio_rx_service(void);
        extern int gsio_rx_pop(void);
        int rc;

        gsio_rx_service();	/* empty the hardware FIFO into the ring */
        for (n = 0; n < 64 && (rc = gsio_rx_pop()) >= 0; n++) {
            /* A serial byte is console activity: wake the screen-saver. */
            console_activity();
            ttyinput(rc, tp);
        }
    }
#endif

    for (n = 0; n < 256 && (c = getc(&tp->t_outq)) >= 0; n++)
        uartputc(CONS_MINOR, (char)c);
}
int uartselect(dev_t dev, int rw)
{
    /*
     * Was a permanent "never ready" stub, so any select()/poll() on the
     * console blocked forever regardless of actual input (confirmed:
     * typing at the serial terminal had no effect). Writes are always
     * immediately possible (uartwrite() never blocks), so only the
     * read direction needs a real check.
     *
     * Was later reading raw wire state via gsio_avail() directly -
     * that stopped matching reality once uartread() started pulling
     * from the tty's cooked queue (t_canq) instead of the wire: a
     * single typed byte makes gsio_avail() true immediately, but in
     * cooked mode ttread() won't actually have anything to return
     * until a full line is buffered, so select() would wrongly report
     * ready long before a read() would succeed. ttyselect() checks
     * the queue ttread() itself pulls from (via ttnread()), which
     * matches. FWRITE still mirrors seltrue()'s always-ready behavior.
     */
    struct tty *tp = &uartttys[minor(dev)];

    if (rw == FREAD)
        return ttyselect(tp, rw);
    return 1;
}

/* r_read / r_write に相当する関数名 (conf.cの定義に合わせる) */
char uartgetc(dev_t dev) {
#ifdef SERIAL_CONSOLE
    return (char)gsio_getc();
#else
    return (char)0;
#endif
}

void uartputc(dev_t dev, char c) {
    gtxt_putc(c);
#ifdef SERIAL_CONSOLE
    gsio_putc((unsigned char)c);
#endif

#ifdef MGBA_LOG
    if (mgba_present) {
        static int i = 0;
        if ((c && i < 255) && c != '\n') {
            MGBA_REG_DEBUG_BUFFER[i] = c;                // Buffer
            i++;
        } else {
            MGBA_REG_DEBUG_BUFFER[i] = '\0';                // Buffer
            MGBA_REG_DEBUG_FLAGS = 0x100|MGBA_LOG_INFO;  // Send Info Log
            i = 0;
        }
    }
#endif
}

/*
 * Console-put for KERNEL messages only. cnputc() (sys/dev/cons.c) reaches
 * the console through the cdevsw r_write slot, and that slot is wired to
 * THIS function (see conf.c); ordinary program/tty output calls uartputc()
 * directly and never comes through here. So bracketing the character in the
 * kernel-message colour tints exactly the kernel's own printf output green
 * on the LCD, leaving login/shell/program output and the keyboard in the
 * normal console colour (roadmap #8). The green applies only on the gtxt
 * LCD console; the serial mirror and mGBA log are plain text.
 */
void uartputc_kmsg(dev_t dev, char c) {
    gtxt_msgcolor(1);
    uartputc(dev, c);
    gtxt_msgcolor(0);
}

void uartputs(dev_t dev, const char *s) {
#ifdef MGBA_LOG
    if (mgba_present) {
        int i = 0;
        while (s[i] && i < 255) {
            MGBA_REG_DEBUG_BUFFER[i] = s[i];                // Buffer
            i++;
        }
        MGBA_REG_DEBUG_BUFFER[i] = '\0';                // Buffer
        MGBA_REG_DEBUG_FLAGS = 0x100|MGBA_LOG_INFO;  // Send Info Log
    }
#else
    (void)s;
#endif
}

/* ioconf.c が期待するドライバ構造体 */
struct driver uartdriver = {
    "uart",
    uartprobe
};

struct tty uartttys[NUART];
