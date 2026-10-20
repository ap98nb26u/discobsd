/*
 * Real hardware UART over the GBA's Serial I/O (link cable) port,
 * ported from Adrian O'Grady's fivemouse.com GBA UART demo/library
 * (http://www.fivemouse.com/gba/).
 *
 * The SIO port in UART mode uses hardware CTS/RTS flow control on
 * the link cable's SD/SC lines: sndChar() blocks until the far end
 * asserts CTS, rcvChar() blocks until a full byte has arrived. There
 * is no timeout, matching the reference library -- if nothing is
 * connected to the cable, console output blocks here forever.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/systm.h>
#include <sys/inode.h>
#include <sys/errno.h>
#include <gba/dev/gba_sio_uart.h>

#define REG_BASE        0x4000000

#define REG_SIOCNT      (*(volatile unsigned short *)(REG_BASE + 0x128))
#define REG_SIODATA8    (*(volatile unsigned short *)(REG_BASE + 0x12a))
#define REG_RCNT        (*(volatile unsigned short *)(REG_BASE + 0x134))
#define REG_TM0CNT_L_   (*(volatile unsigned short *)(REG_BASE + 0x100))
#define REG_IME_        (*(volatile unsigned short *)(REG_BASE + 0x208))
#define REG_TM0CNT_H_   (*(volatile unsigned short *)(REG_BASE + 0x102))

#define SIO_USE_UART    0x3000

#define SIO_BAUD_9600   0x0000
#define SIO_BAUD_38400  0x0001
#define SIO_BAUD_57600  0x0002
#define SIO_BAUD_115200 0x0003

#define SIO_CTS         0x0004
#define SIO_SEND_DATA   0x0010 /* 0=send register empty, 1=still full/busy */

/*
 * Bounds for gsio_putc()'s waits. CTS is a busy-wait only hit at the
 * very start if there IS a peer; if there is none (cable-less GBA), one
 * timeout of this length is paid once and serial is then dropped, so it
 * can be generous. The send-complete wait always finishes on its own
 * (internal-clock UART) in ~1500 cycles; bound it only against a wedge.
 */
#define GSIO_CTS_LIMIT   1000000
#define GSIO_SEND_LIMIT  100000
#define SIO_LENGTH_8    0x0080
#define SIO_SEND_ENABLE 0x0400
#define SIO_RECV_ENABLE 0x0800

#define SIO_RECV_DATA   0x0020	/* SIOCNT d05 recv-data flag: 0 = a byte waiting */
#define SIO_ERROR       0x0040	/* SIOCNT d06: overrun/framing (clears on read) */
#define SIO_FIFO_ENABLE 0x0100	/* SIOCNT d08: enable the 4-byte send/recv FIFOs */
#define SIO_IRQ_ENABLE  0x4000	/* SIOCNT d14: request the SIO interrupt */

/*
 * Receive, with the UART's 4-byte hardware FIFO.
 *
 * The GBA UART has a 4-entry receive FIFO (AGB Programming Manual 13.3,
 * SIOCNT d08), previously left disabled. With it off the SIO holds only ONE
 * received byte in REG_SIODATA8, so a fast multi-byte burst - an arrow key's
 * ESC[C, three bytes in ~260us at 115200 - overran that single register far
 * quicker than any poll could drain it, dropping the middle byte and leaving
 * the trailing letter to print literally (while the software keyboard, which
 * injects its three bytes at once, always worked). Enabling the FIFO buffers
 * a whole such burst; the bytes are moved into this ring and handed to the
 * tty by the input poll. The hardware drives the SD/RTS line and fills the
 * FIFO on its own once receive-enable is set - no manual arming (and, in UART
 * mode, RCNT is not the SD line: it just selects serial mode, RCNT[15:14]=0).
 *
 * There is no "data arrived" interrupt.  In UART mode the SIO interrupt
 * fires when one of SIOCNT bits 4-6 BECOMES SET (GBATEK): the send buffer
 * becoming full, the receive buffer becoming EMPTY, or an error.  So the
 * FIFO is only ever emptied by polling (the tty input poll, and /dev/sio's
 * fast timer poll, burst reads and syscall hooks); if four bytes arrive
 * before the next poll, the fifth overruns and is lost, and the error
 * interrupt that follows merely drains the four.  At 115200 that is 350 us,
 * and even an interrupts-off polling loop was measured losing bytes there;
 * at 38400 (1 ms of FIFO) SLIP runs cleanly.  Typing is slow enough either
 * way; a fast paste into the console can drop characters.
 *
 * When the ring is full, bytes are left in the FIFO instead of dropped.  The
 * receiver deasserts SD (RTS) while its FIFO is full (GBATEK, SIO UART mode),
 * so a peer that honours CTS - the FT232R cable with hardware handshaking -
 * pauses until the ring drains (the byte already on the wire may still be
 * lost: RTS only drops once the FIFO is full).
 */
#define GSIO_RXBUF	64			/* must stay a power of two */
static volatile unsigned char gsio_rxbuf[GSIO_RXBUF];
static volatile unsigned char gsio_rxhead, gsio_rxtail;

unsigned long gsio_nrx, gsio_nerr;	/* bytes received, receive errors */
static int gsio_dead;			/* no serial peer: send nothing, LCD only */

#define TM2NOW()	(*(volatile unsigned short *)(REG_BASE + 0x108))

/*
 * Move every byte now in the hardware RX FIFO into the ring. The caller must
 * ensure the SIO interrupt cannot run concurrently: gsio_rx_isr() is already
 * in IRQ context, and gsio_rx_service() masks the SIO IRQ around its call.
 */
static void
gsio_fifo_drain(void)
{
    unsigned short cnt;

    while (((cnt = REG_SIOCNT) & SIO_RECV_DATA) == 0) {	/* a byte is waiting */
        unsigned char nh = (unsigned char)((gsio_rxhead + 1) & (GSIO_RXBUF - 1));

        if (cnt & SIO_ERROR)
            gsio_nerr++;		/* reading SIOCNT cleared it */
        if (nh == gsio_rxtail)
            break;			/* ring full: leave it in the FIFO (see above) */
        gsio_rxbuf[gsio_rxhead] = (unsigned char)REG_SIODATA8;
        gsio_rxhead = nh;
        gsio_nrx++;
        gsio_dead = 0;			/* a peer is there after all */
    }
}

/*
 * SIO interrupt service, called from the kernel IRQ dispatch
 * (gba_do_schedule(), machdep.c) when IRQ_SERIAL is pending: the send
 * buffer filled, the receive buffer emptied, or an error (an overrun).
 * Take whatever has been received into the ring.
 */
void
gsio_rx_isr(void)
{
    gsio_fifo_drain();
}

/*
 * Drain the FIFO from outside the SIO interrupt: the tty input poll
 * (uart_poll_input()), /dev/sio, the fast Timer0 poll and the syscall
 * hooks. Masks the SIO interrupt for the duration so gsio_rx_isr() cannot
 * run at the same instant and race on the FIFO/ring; a byte arriving
 * meanwhile simply waits in the FIFO and is taken on the next pass.
 */
void
gsio_rx_service(void)
{
    REG_SIOCNT = REG_SIOCNT & (unsigned short)~SIO_IRQ_ENABLE;
    gsio_fifo_drain();
    REG_SIOCNT = REG_SIOCNT | SIO_IRQ_ENABLE;
}

/* Pop one received byte for the tty input poll, or -1 if the ring is empty. */
int
gsio_rx_pop(void)
{
    int b;

    if (gsio_rxtail == gsio_rxhead)
        return -1;
    b = gsio_rxbuf[gsio_rxtail];
    gsio_rxtail = (unsigned char)((gsio_rxtail + 1) & (GSIO_RXBUF - 1));
    return b;
}

static unsigned int gsio_baud;		/* current rate, for /dev/sio */

void
gsio_init(unsigned int baud)
{
    unsigned short rate;

    gsio_baud = baud;

    switch (baud) {
    case 38400:
        rate = SIO_BAUD_38400;
        break;
    case 57600:
        rate = SIO_BAUD_57600;
        break;
    case 115200:
        rate = SIO_BAUD_115200;
        break;
    default:
        rate = SIO_BAUD_9600;
        break;
    }

    /*
     * Stick a character in the data register first, to stop the GBA
     * transmitting a stray character as soon as it enters UART mode.
     */
    REG_SIODATA8 = 'A';

    REG_RCNT = 0;		/* RCNT[15:14]=0 -> serial (UART) mode */
    REG_SIOCNT = 0;
    /*
     * Enter UART mode with the FIFO disabled first - this initializes the
     * FIFO sequencer (AGB Programming Manual 13.3) - then turn the 4-byte
     * FIFO and the receive interrupt on.
     */
    REG_SIOCNT = rate | SIO_CTS | SIO_LENGTH_8 | SIO_SEND_ENABLE |
                 SIO_RECV_ENABLE | SIO_USE_UART;
    REG_SIOCNT = REG_SIOCNT | SIO_FIFO_ENABLE | SIO_IRQ_ENABLE;
}

/*
 * Non-blocking peek: is a fully-received byte currently sitting in
 * REG_SIODATA8? Pure read of the status bit gsio_getc()'s own wait
 * loop blocks on below - no side effects, so it's safe to call from
 * select() without disturbing a pending byte the way calling
 * gsio_getc() itself (which re-arms the flag first) would.
 */
int
gsio_avail(void)
{
    return (REG_SIOCNT & 0x0020) == 0;
}

unsigned char
gsio_getc(void)
{
    /* Mark the receive-data flag empty. */
    REG_SIOCNT = REG_SIOCNT | 0x0020;
    /* Using CTS: drive SD low to show we're ready to receive. */
    REG_RCNT = REG_RCNT & (0x0020 ^ 0xFFFF);

    /* Wait for a full byte (the recv-data flag clears to 0). */
    while (REG_SIOCNT & 0x0020)
        ;

    return (unsigned char)REG_SIODATA8;
}

/*
 * Bounded sibling of gsio_getc(), for callers that must not risk an
 * unbounded block - specifically uart_poll_input() (mgba_uart.c),
 * which calls this automatically from idle() based on gsio_avail()'s
 * signal, not in response to a process explicitly choosing to block
 * on read(). gsio_avail() is a cheap status-bit peek with its own
 * documented small race window against a genuinely-arriving byte;
 * more importantly, an emulator (mGBA) with nothing wired to the
 * emulated link cable may not model the same idle-line register
 * state real hardware does, so treating "avail" as a hard guarantee
 * here and calling the original unbounded gsio_getc() would hang the
 * *entire scheduler* forever the first time idle() ever runs (idle()
 * is swtch()'s core wait loop - nothing else can ever run again).
 * Deliberately much shorter than the ~50000-iteration scale used
 * elsewhere in this codebase for SD/ED command/response round-trips
 * (a genuinely slow operation) - gsio_avail() having just reported
 * "ready" means a byte should already be sitting there or arriving
 * within microseconds, not needing anywhere near that much patience.
 * Called from idle(), which runs in a *tight loop* whenever nothing
 * is runnable - a long bound here, hit repeatedly (e.g. real GBAED
 * hardware, immediately after fsck's heavy SD-bus activity: reported
 * hanging exactly there right when this feature first shipped, right
 * where `sh` would otherwise idle briefly waiting on fsck's exit
 * status), would burn massive cumulative time across many idle()
 * calls even though no single call ever technically hangs forever.
 * Returns -1 (never a valid unsigned char, since it's the widened
 * int -1 = 0xFFFFFFFF) on timeout instead of REG_SIODATA8's contents.
 */
int
gsio_getc_bounded(void)
{
    volatile unsigned long i;

    REG_SIOCNT = REG_SIOCNT | 0x0020;
    REG_RCNT = REG_RCNT & (0x0020 ^ 0xFFFF);

    for (i = 0; i < 1000; i++) {
        if ((REG_SIOCNT & 0x0020) == 0)
            return (unsigned char)REG_SIODATA8;
    }
    return -1;
}

void
gsio_putc(unsigned char c)
{
    unsigned int i;

    /*
     * A cable-less GBA has nothing to raise CTS, so the original
     * unbounded "wait for CTS" hung the very first boot printf and the
     * machine never started without a serial cable attached. Bound the
     * wait; if CTS never comes, mark the port dead and skip serial for
     * the rest of the run - the LCD text console keeps working, so a
     * bare GBA boots fine. With a cable, CTS is asserted at once and
     * this costs nothing.
     */
    if (gsio_dead)
        return;
    for (i = 0; REG_RCNT & 0x0010; i++)
        if (i > GSIO_CTS_LIMIT) {
            gsio_dead = 1;
            return;
        }

    /*
     * Wait for the previous byte to finish transmitting. The reference
     * library never checked this (SIO_SEND_DATA was defined but unused),
     * which corrupts back-to-back writes with no intervening delay --
     * e.g. "\r\n" landing as two 0x00 bytes. Bounded against a wedge.
     */
    for (i = 0; REG_SIOCNT & SIO_SEND_DATA; i++)
        if (i > GSIO_SEND_LIMIT)
            break;
    REG_SIODATA8 = c;
}

#ifdef SERIAL_CONSOLE
/*
 * /dev/sio - the link-cable UART as a raw byte stream, for SLIP.
 *
 * Normally the SIO is part of the console: console output is mirrored to it
 * (uartputc) and received bytes are fed to the console tty (uart_poll_input).
 * While /dev/sio is open (one opener at a time) it belongs to that process
 * instead: neither path touches it, so kernel messages cannot corrupt the
 * byte stream, and the console carries on on the LCD and software keyboard.
 * Received bytes go through the same interrupt-driven ring as before; read()
 * takes them, sleeping (or EWOULDBLOCK with O_NONBLOCK) when there are none,
 * and select() reports when there are.  write() sends with CTS honoured and
 * keeps the receive FIFO drained between bytes, since a syscall runs with
 * interrupts off.  The peer should use hardware handshaking (see the ring
 * comment above).
 */
int gsio_owned;				/* 1: /dev/sio has the port */

/*
 * Fast FIFO polling.  The SIO interrupt fires when the send buffer becomes
 * full, the receive buffer becomes EMPTY, or on an error (GBATEK, SIOCNT
 * bit 14) - never when a byte arrives.  So nothing empties the 4-byte
 * receive FIFO in time on its own: the fifth byte overruns (the error IRQ
 * then drains the four), losing every fifth byte of a burst.  While
 * /dev/sio is open, Timer0 is sped up from HZ to 4096 Hz (prescaler /64,
 * 244 us - under the 348 us four bytes take at 115200) and its interrupt
 * drains the FIFO each time; gba_do_schedule() (machdep.c) still runs the
 * 100 Hz clock work off Timer3.  The SIO_IRQ_ENABLE error interrupt stays
 * as a backstop.  A syscall still runs with interrupts off, so a long one
 * can cost a byte; the peer pads each SLIP packet's start for the common
 * case (the reader waking up).
 */
int gsio_fastpoll_on;

static void
gsio_fastpoll(int on)
{
    REG_TM0CNT_H_ = 0;
    if (on) {
        REG_TM0CNT_L_ = 65536 - 64;		/* 16.78 MHz / 64 / 64 = 4096 Hz */
        REG_TM0CNT_H_ = 0x00C1;			/* start, IRQ, /64 */
    } else {
        REG_TM0CNT_L_ = 65536 - (16777216 / 1024 / HZ);
        REG_TM0CNT_H_ = 0x00C3;			/* start, IRQ, /1024 */
    }
    gsio_fastpoll_on = on;
}
static struct proc *gsio_rsel;		/* a select()ing reader */
static int gsio_rcoll;

static int
gsio_ring_count(void)
{
    return (gsio_rxhead - gsio_rxtail) & (GSIO_RXBUF - 1);
}

/* uart_poll_input() calls this instead of feeding the console tty */
void
sio_poll(void)
{
    gsio_rx_service();
    if (gsio_ring_count()) {
        wakeup((caddr_t)gsio_rxbuf);
        if (gsio_rsel) {
            selwakeup(gsio_rsel, (long)gsio_rcoll);
            gsio_rsel = 0;
            gsio_rcoll = 0;
        }
    }
}

int
sioopen(dev_t dev, int flag, int mode)
{
    if (gsio_owned)
        return EBUSY;
    gsio_owned = 1;
    gsio_rxtail = gsio_rxhead;		/* drop console input still queued */
    gsio_nrx = gsio_nerr = 0;
    gsio_dead = 0;
    gsio_fastpoll(1);
    return 0;
}

int
sioclose(dev_t dev, int flag, int mode)
{
    gsio_fastpoll(0);
    if (gsio_baud != UART_BAUD)
        gsio_init(UART_BAUD);		/* back to the console's rate */
    gsio_owned = 0;
    gsio_rsel = 0;
    return 0;
}

/*
 * Burst reads.  The receiver deasserts RTS only once its 4-byte FIFO is
 * full, and the FT232R has usually started the next byte by then, so that
 * byte is lost unless the FIFO is emptied within one byte time (87 us at
 * 115200) - and a syscall runs with interrupts off.  So once data is
 * flowing, read() keeps the FIFO empty itself: it polls it directly until
 * the line has been quiet for about three byte times, and at least 2 ms
 * (timed with the free-running Timer2, 61 us per tick), taking a whole
 * SLIP packet in one call instead of a few bytes per syscall with gaps
 * between.  The gap between waking the reader and its read() starting
 * can still cost a byte; the peer pads each packet with extra SLIP END
 * bytes so that the loss lands harmlessly in the padding.
 */
static unsigned gsio_quiet_ms = 2;	/* SIOSETQUIET */

static unsigned
gsio_quiet_ticks(void)
{
    unsigned bytetime = 10000000 / (gsio_baud ? gsio_baud : 9600);  /* us/10 */
    unsigned t = (3 * bytetime + 609) / 610;			/* 61 us ticks */
    unsigned m = (gsio_quiet_ms * 10000 + 609) / 610;

    /*
     * At least ~2 ms: a USB serial adapter (FT232R) sends in 1 ms USB
     * frames, so a burst can pause for up to a frame mid-packet; ending
     * the read there would put a syscall's interrupts-off gap back in
     * the middle of the packet.  Tunable with SIOSETQUIET (default 2).
     */
    return t < m ? m : t;
}

int
sioread(dev_t dev, struct uio *uio, int flag)
{
    char buf[256];
    int n, error;
    unsigned short last, quiet, ime;

    for (;;) {
        gsio_rx_service();
        if (gsio_rxtail != gsio_rxhead)
            break;
        if (flag & IO_NDELAY)
            return EWOULDBLOCK;
        sleep((caddr_t)gsio_rxbuf, TTIPRI);	/* sio_poll() wakes us */
    }

    n = 0;
    while (n < (int)sizeof(buf) && n < uio->uio_resid &&
           gsio_rxtail != gsio_rxhead) {
        buf[n++] = gsio_rxbuf[gsio_rxtail];
        gsio_rxtail = (unsigned char)((gsio_rxtail + 1) & (GSIO_RXBUF - 1));
    }
    /*
     * Interrupts off for the burst: an interrupt here (the 100 Hz clock
     * work in the Timer0 handler takes about a millisecond) stalls the
     * polling long enough for the FIFO to overrun.  Diagnostics showed
     * the burst loop finding overruns after 0.4-1.2 ms gaps.
     */
    ime = REG_IME_;
    REG_IME_ = 0;
    quiet = gsio_quiet_ticks();
    last = TM2NOW();
    while (n < uio->uio_resid) {
        if (n == (int)sizeof(buf)) {
            /* hand over a full buffer; the FIFO covers the short pause */
            error = uiomove(buf, n, uio);
            if (error) {
                REG_IME_ = ime;
                return error;
            }
            n = 0;
            last = TM2NOW();
            continue;
        }
        unsigned short cnt = REG_SIOCNT;

        if ((cnt & SIO_RECV_DATA) == 0) {		/* a byte in the FIFO */
            if (cnt & SIO_ERROR)
                gsio_nerr++;
            buf[n++] = (char)REG_SIODATA8;
            gsio_nrx++;
            last = TM2NOW();
        } else if ((unsigned short)(TM2NOW() - last) > quiet)
            break;					/* line quiet: done */
    }
    REG_IME_ = ime;
    error = uiomove(buf, n, uio);
    gsio_rx_service();			/* anything that came in meanwhile */
    return error;
}

int
siowrite(dev_t dev, struct uio *uio, int flag)
{
    char buf[32];
    int n, i, error;

    while (uio->uio_resid > 0) {
        n = uio->uio_resid;
        if (n > (int)sizeof(buf))
            n = sizeof(buf);
        error = uiomove(buf, n, uio);
        if (error)
            return error;
        for (i = 0; i < n; i++) {
            gsio_putc((unsigned char)buf[i]);
            gsio_rx_service();		/* keep receiving while we send */
        }
    }
    return 0;
}

int
sioioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    if (cmd == FIONBIO || cmd == FIOASYNC)
        return 0;
    if (cmd == SIOSETQUIET) {		/* burst-read end, in ms */
        int ms = *(int *)addr;

        if (ms < 1 || ms > 100)
            return EINVAL;
        gsio_quiet_ms = ms;
        return 0;
    }
    if (cmd == SIOGETSTAT) {		/* {bytes received, errors} */
        ((unsigned long *)addr)[0] = gsio_nrx;
        ((unsigned long *)addr)[1] = gsio_nerr;
        return 0;
    }
    if (cmd == SIOSETBAUD) {		/* 9600, 38400, 57600 or 115200 */
        int baud = *(int *)addr;

        if (baud != 9600 && baud != 38400 && baud != 57600 && baud != 115200)
            return EINVAL;
        gsio_init(baud);
        gsio_rxtail = gsio_rxhead;
        return 0;
    }
    return ENOTTY;
}

int
sioselect(dev_t dev, int rw)
{
    int s;

    if (rw != FREAD)
        return 1;
    gsio_rx_service();
    if (gsio_ring_count())
        return 1;
    s = splhigh();
    if (gsio_rsel && gsio_rsel->p_wchan == (caddr_t)&selwait)
        gsio_rcoll = 1;
    else
        gsio_rsel = u.u_procp;
    splx(s);
    return 0;
}
#endif /* SERIAL_CONSOLE */
