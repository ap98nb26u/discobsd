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
#define SIO_LENGTH_8    0x0080
#define SIO_SEND_ENABLE 0x0400
#define SIO_RECV_ENABLE 0x0800

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

    REG_RCNT = 0;
    REG_SIOCNT = 0;
    REG_SIOCNT = rate | SIO_CTS | SIO_LENGTH_8 | SIO_SEND_ENABLE |
                 SIO_RECV_ENABLE | SIO_USE_UART;
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
    /* Wait for the far end's CTS signal. */
    while (REG_RCNT & 0x0010)
        ;
    /*
     * Wait for the previous byte to finish transmitting. The
     * reference library never checked this (SIO_SEND_DATA was
     * defined but unused), which corrupts back-to-back writes with
     * no intervening delay -- e.g. "\r\n" landing as two 0x00 bytes.
     */
    while (REG_SIOCNT & SIO_SEND_DATA)
        ;
    REG_SIODATA8 = c;
}
