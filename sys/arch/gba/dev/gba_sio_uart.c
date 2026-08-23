/*
 * Real hardware UART over the GBA's Serial I/O (link cable) port.
 * It began from Adrian O'Grady's fivemouse.com GBA UART demo
 * (http://www.fivemouse.com/gba/), but none of that code remains.
 *
 * The SIO port in UART mode uses hardware CTS/RTS flow control on
 * the link cable's SD/SC lines: gsio_putc() blocks until the far end
 * asserts CTS, gsio_getc() blocks until a full byte has arrived. There
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
     * Enter UART mode with send and receive still off, so nothing
     * stale goes out, then switch them on.
     */
    REG_RCNT = 0;
    REG_SIOCNT = rate | SIO_CTS | SIO_LENGTH_8 | SIO_USE_UART;
    REG_SIOCNT = rate | SIO_CTS | SIO_LENGTH_8 | SIO_SEND_ENABLE |
                 SIO_RECV_ENABLE | SIO_USE_UART;
}

unsigned char
gsio_getc(void)
{
    /* SIOCNT bit 5 is set while the receive buffer is empty. */
    while (REG_SIOCNT & 0x0020)
        ;

    return (unsigned char)REG_SIODATA8;
}

void
gsio_putc(unsigned char c)
{
    /* With SIO_CTS set the hardware itself waits for the peer's CTS. */
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
