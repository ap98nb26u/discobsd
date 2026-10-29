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

#ifdef AGBPRINT
/*
 * Nonzero under mGBA (detected in uartinit). mGBA appends its own newline to
 * each AGBPrint flush, VBA does not, so agb_puts uses this to decide whether
 * to add a trailing '\n'. Detection only - logging is all AGBPrint.
 */
static int mgba_present;
#endif

void uartinit(int unit) {
    /*
     * Two optional output sinks mirror the on-screen (gtxt) console, each
     * selected by a kernel config option (see the Config files):
     *   AGBPRINT        - mirror the console to AGBPrint, the standard GBA
     *                     debug-print that mGBA and VisualBoyAdvance capture
     *                     (see agb_puts). Its flush is a real svc, so it is
     *                     set ONLY in the emulator-only GBALOG config, never
     *                     in the plain GBA/GBAED kernels that run on real
     *                     hardware.
     *   SERIAL_CONSOLE  - real UART over the link cable (gsio), for
     *                     interactive debugging; the GBAED build enables
     *                     it, the cable-free GBA demo build does not (it
     *                     would otherwise spin on CTS with no peer).
     * Only the console unit is set up here (the GBA has a single UART;
     * uartprobe() would init any other unit).
     */
    if (unit == CONS_MINOR) {
        gtxt_init(GTXT_WHITE, GTXT_BLACK); // console: white on black
#ifdef AGBPRINT
        /*
         * Emulator console log via AGBPrint (GBALOG config). Both mGBA and
         * VBA 1.8.0 capture it (flush = svc 0xFA, see agb_puts), one copy per
         * line. Set the buffer bank to 0x1fd (buffer 0x09FD0000 = ROM offset
         * 0x1FD0000, which the emulator reads); the 0x20/0x00 writes are the
         * AGBPrint protect handshake.
         */
        AGB_PRINT_PROTECT = 0x20;
        AGB_PRINT_CTX[0] = 0;       /* request */
        AGB_PRINT_CTX[1] = 0x01fd;  /* bank -> buffer at 0x09FD0000 */
        AGB_PRINT_CTX[2] = 0;       /* get */
        AGB_PRINT_CTX[3] = 0;       /* put */
        AGB_PRINT_PROTECT = 0;

        /* Detect mGBA (vs VBA) for agb_puts's newline handling (see above). */
        MGBA_REG_DEBUG_ENABLE = 0xC0DE;
        mgba_present = (MGBA_REG_DEBUG_ENABLE == 0x1DEA);
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

/* Entry points, under the names conf.c expects. */
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
     * Opening the console with no process group makes it the
     * controlling tty (u.u_ttyp/u.u_ttyd, as ttyopen() does) and puts
     * the opener at the head of a new foreground group, tp->t_pgrp.
     * The controlling tty is what ps(1) shows in its TTY column, what
     * a bare `ps` selects on, and what /dev/tty and vhangup() act on;
     * t_pgrp is what ^C/^\ signal through gsignal().
     *
     * Unlike ttyopen(), t_pgrp is always set to the opener rather than
     * kept when non-zero. uartclose() is never reached while a
     * daemon started from /etc/rc (update) still holds the console,
     * so the tty would otherwise stay in /etc/rc's group: getty would
     * join it, and getty's vhangup() would SIGHUP update, as would a
     * ^C typed at any later shell.
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

    /*
     * First open: give the tty the usual terminal modes (echo, CR->NL,
     * erase/kill/intr characters), as other ports' uartopen() does.
     * getty sets its own modes over these in multi-user mode, but a
     * single-user shell (init's single(), e.g. after /etc/rc fails)
     * reads the console as it is - without CRMOD a typed Enter never
     * ends the line, and without ECHO nothing typed shows.
     */
    if ((tp->t_state & TS_ISOPEN) == 0) {
        ttychars(tp);
        tp->t_flags = ECHO | XTABS | CRMOD | CRTBS | CRTERA | CTLECH | CRTKIL;
        tp->t_state |= TS_ISOPEN;
    }
    tp->t_state |= TS_CARR_ON;

    tp->t_dev = dev;
    if (pp->p_pid != 1 && pp->p_pgrp == 0) {
        u.u_ttyp = tp;
        /*
         * cnopen() hands us the uart's own device, but processes open
         * this tty as /dev/console: record that one, so ps names it
         * "co" like ttyname(0) does (a bare `ps` compares the two) and
         * vhangup() revokes the /dev/console opens.
         */
        u.u_ttyd = (CONS_MAJOR == UART_MAJOR && minor(dev) == CONS_MINOR) ?
            makedev(0, 0) : dev;
        tp->t_pgrp = pp->p_pid;
        pp->p_pgrp = pp->p_pid;
    }

    return 0;
}
int uartclose(dev_t dev, int flag, int mode) { return 0; }
int uartread(dev_t dev, struct uio *uio, int flag)
{
    /*
     * An ordinary tty read: ttread() takes from t_canq/t_rawq, which
     * uart_poll_input() fills (via console_input_poll(), from idle() and
     * the Timer0 interrupt - see machdep.c), and sleeps on t_rawq until
     * ttyinput()'s ttwakeup() wakes it.
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
     * Hand sgtty/termios ioctls to ttioctl(): isatty(), ttyname(),
     * login's secure-tty check, getty and stty(1) all depend on them.
     */
    register struct tty *tp = &uartttys[minor(dev)];
    int error;

    /*
     * The on-screen (LCD) console has a fixed 40x15 geometry (6x8 font,
     * 240/6 = 40 cols; 15 text rows above the 5-row soft keyboard), so pin
     * its window size on every ioctl - in particular so TIOCGWINSZ always
     * reports 40x15 to full-screen apps (vi). login(1) deliberately clears
     * the window size to 0x0 on local login (login.c, "if (!hflag)"), and
     * nothing on a hardwired console re-establishes it; re-asserting it
     * here, just before ttioctl() reads or writes it, keeps it authoritative.
     */
    if (minor(dev) == CONS_MINOR) {
        tp->t_winsize.ws_col = 40;
        tp->t_winsize.ws_row = 15;
    }

    error = ttioctl(tp, cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    return (error);
}

/*
 * Feed any received bytes through the real tty line discipline, so
 * echo/erase/kill/cooked-line assembly (ttyinput()) run for console
 * input.
 *
 * UART mode has no data-arrived interrupt, so the receive FIFO is
 * polled: this runs from console_input_poll() (arch/gba/gba/machdep.c),
 * called by idle() and by the Timer0 interrupt. Draining t_outq by hand
 * afterward stands in for a proper ttstart()/d_start callback (conf.c
 * wires the uart's d_stop slot to nullstop, so nothing else ever drains
 * ttyecho()'s queued bytes) - fine for this port's needs since
 * uartputc() itself never blocks waiting on anything but the far end's
 * own byte-at-a-time CTS/busy flags.
 */
void
uart_poll_input(void)
{
    struct tty *tp = &uartttys[CONS_MINOR];
    int c;
    int n;

#ifdef SERIAL_CONSOLE
    /*
     * Empty the 4-byte receive FIFO into the ring (gba_sio_uart.c), then
     * hand up to 64 bytes per call to the tty; the rest wait in the ring
     * for the next poll.
     */
    {
        extern void gsio_rx_service(void);
        extern int gsio_rx_pop(void);
        int rc;

        if (gsio_owned)
            sio_poll();		/* /dev/sio has the port: wake its reader */
        else {
        gsio_rx_service();	/* empty the hardware FIFO into the ring */
        for (n = 0; n < 64 && (rc = gsio_rx_pop()) >= 0; n++) {
            /* A serial byte is console activity: wake the screen-saver. */
            console_activity();
            ttyinput(rc, tp);
        }
        }
    }
#endif

    for (n = 0; n < 256 && (c = getc(&tp->t_outq)) >= 0; n++)
        uartputc(CONS_MINOR, (char)c);
}
int uartselect(dev_t dev, int rw)
{
    /*
     * Read readiness comes from the queue ttread() takes from
     * (ttyselect()/ttnread()), so in cooked mode the console is ready
     * only once a full line is in. Writes never block (uartwrite() goes
     * straight to the wire), so FWRITE is always ready, as seltrue().
     */
    struct tty *tp = &uartttys[minor(dev)];

    if (rw == FREAD)
        return ttyselect(tp, rw);
    return 1;
}

/* Raw character input, as named in conf.c. */
char uartgetc(dev_t dev) {
#ifdef SERIAL_CONSOLE
    return (char)gsio_getc();
#else
    return (char)0;
#endif
}

#ifdef AGBPRINT
/*
 * Emit one completed console line to AGBPrint, which mGBA and VisualBoyAdvance
 * capture. The reader pulls bytes over get..put, so pack two chars per 16-bit
 * cartridge word (little-endian: buffer[2k] low, [2k+1] high); a trailing
 * newline ends the line. protect=0x20 lets the emulator capture the
 * buffer/context writes; the flush is a software interrupt, comment 0xFA. Only
 * built into the GBALOG config: this is a REAL svc and the port has no real svc
 * handler (user syscalls use a simulated SWI), so on hardware / a non-AGBPrint
 * emulator it would trap to the BIOS - hence never in the plain GBA/GBAED
 * kernels that run on real non-EverDrive carts.
 */
static void agb_puts(const char *s) {
    volatile unsigned short *buf = AGB_PRINT_BUFFER;
    unsigned short half = 0;
    int i = 0;

    AGB_PRINT_PROTECT = 0x20;
    /* Pack the line, two bytes per 16-bit cartridge word. */
    while (s[i] && i < 509) {
        unsigned char c = (unsigned char)s[i];
        if (i & 1) {
            half |= (unsigned short)c << 8;
            buf[i >> 1] = half;
        } else {
            half = c;
        }
        i++;
    }
    /*
     * VBA has no per-flush newline and relies on one in the buffer to break
     * lines, but mGBA adds its own, so an extra '\n' here double-spaces the
     * mGBA log. Append the newline only when NOT running under mGBA.
     */
    if (!mgba_present) {
        if (i & 1)
            buf[i >> 1] = half | ((unsigned short)'\n' << 8);
        else
            half = '\n';
        i++;
    }
    if (i & 1)
        buf[i >> 1] = half;                 /* trailing odd byte (high byte 0) */
    AGB_PRINT_CTX[1] = 0x01fd;               /* bank: buffer 0x09FD0000 -> g_rom 0x1FD0000 */
    AGB_PRINT_CTX[2] = 0;                     /* get */
    AGB_PRINT_CTX[3] = (unsigned short)i;     /* put = byte count */
    asm volatile("svc #0xfa" ::: "memory");  /* flush (mGBA/VBA intercept 0xFA) */
    AGB_PRINT_PROTECT = 0;                    /* lock */
}
#endif /* AGBPRINT */

void uartputc(dev_t dev, char c) {
    gtxt_putc(c);
#ifdef SERIAL_CONSOLE
    if (!gsio_owned)		/* not while /dev/sio has the port */
        gsio_putc((unsigned char)c);
#endif

#ifdef AGBPRINT
    {
        static char line[256];
        static int i = 0;
        if (c && c != '\n' && i < 255) {
            line[i++] = c;
        } else {
            line[i] = '\0';
            agb_puts(line);
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
 * LCD console; the serial mirror and AGBPrint log are plain text.
 */
void uartputc_kmsg(dev_t dev, char c) {
    gtxt_msgcolor(1);
    uartputc(dev, c);
    gtxt_msgcolor(0);
}

void uartputs(dev_t dev, const char *s) {
#ifdef AGBPRINT
    agb_puts(s);
#else
    (void)s;
#endif
}

/* The driver structure ioconf.c refers to. */
struct driver uartdriver = {
    "uart",
    uartprobe
};

struct tty uartttys[NUART];
