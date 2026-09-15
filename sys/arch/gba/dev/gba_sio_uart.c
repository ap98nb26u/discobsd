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

#define REG_BASE        0x4000000

#define REG_SIOCNT      (*(volatile unsigned short *)(REG_BASE + 0x128))
#define REG_SIODATA8    (*(volatile unsigned short *)(REG_BASE + 0x12a))
#define REG_RCNT        (*(volatile unsigned short *)(REG_BASE + 0x134))

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
#define SIO_FIFO_ENABLE 0x0100	/* SIOCNT d08: enable the 4-byte send/recv FIFOs */
#define SIO_IRQ_ENABLE  0x4000	/* SIOCNT d14: request the SIO interrupt */

/*
 * Interrupt-driven receive, with the UART's 4-byte hardware FIFO.
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
 * With the FIFO on, the interrupt fires when the receive FIFO becomes FULL
 * (4 bytes), not per byte, so a short burst may never raise it - the input
 * poll therefore also drains the FIFO, with the SIO interrupt masked so the
 * two drainers cannot race.
 */
#define GSIO_RXBUF	64			/* must stay a power of two */
static volatile unsigned char gsio_rxbuf[GSIO_RXBUF];
static volatile unsigned char gsio_rxhead, gsio_rxtail;

/*
 * Move every byte now in the hardware RX FIFO into the ring. The caller must
 * ensure the SIO interrupt cannot run concurrently: gsio_rx_isr() is already
 * in IRQ context, and gsio_rx_service() masks the SIO IRQ around its call.
 */
static void
gsio_fifo_drain(void)
{
    while ((REG_SIOCNT & SIO_RECV_DATA) == 0) {	/* d05==0: a byte is waiting */
        unsigned char b = (unsigned char)REG_SIODATA8;
        unsigned char nh = (unsigned char)((gsio_rxhead + 1) & (GSIO_RXBUF - 1));

        if (nh != gsio_rxtail) {		/* silently drop on ring overflow */
            gsio_rxbuf[gsio_rxhead] = b;
            gsio_rxhead = nh;
        }
    }
}

/*
 * SIO interrupt service, called from the kernel IRQ dispatch
 * (gba_do_schedule(), machdep.c) when IRQ_SERIAL is pending. It fires on a
 * full receive FIFO, an emptied send FIFO, or an error; just move whatever
 * has been received into the ring (nothing if it was a send/error IRQ).
 */
void
gsio_rx_isr(void)
{
    gsio_fifo_drain();
}

/*
 * Drain the FIFO from the tty input poll (uart_poll_input()). Masks the SIO
 * interrupt for the duration so gsio_rx_isr() cannot run at the same instant
 * and race on the FIFO/ring; a byte arriving meanwhile simply waits in the
 * FIFO and is taken on the next pass.
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

void
gsio_init(unsigned int baud)
{
    unsigned short rate;

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
    static int gsio_dead;	/* no serial peer: send nothing, LCD only */
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
