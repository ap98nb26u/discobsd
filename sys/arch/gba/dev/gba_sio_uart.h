#ifndef _GBA_DEV_SIO_UART_H_
#define _GBA_DEV_SIO_UART_H_

#ifdef KERNEL

/*
 * Real hardware UART over the GBA's Serial I/O (link cable) port in
 * UART mode. Ported from Adrian O'Grady's fivemouse.com GBA UART
 * demo/library (http://www.fivemouse.com/gba/).
 *
 * Only the 4 fixed baud rates the SIO hardware itself supports are
 * valid: 9600, 38400, 57600, 115200. Any other value falls back to
 * 9600.
 */
void            gsio_init(unsigned int baud);
unsigned char   gsio_getc(void);
int             gsio_getc_bounded(void);
void            gsio_putc(unsigned char c);
int             gsio_avail(void);

/* /dev/sio: the SIO as a raw byte stream (SERIAL_CONSOLE builds) */
struct uio;
/* ioctl: set the line rate while /dev/sio is open (restored on close) */
#define SIOSETBAUD      _IOW('G', 1, int)
/* ioctl: burst-read end after this many quiet ms (1..100, default 2) */
#define SIOSETQUIET     _IOW('G', 2, int)
/* ioctl: unsigned long [2] = { bytes received, receive errors } */
#define SIOGETSTAT      _IOR('G', 3, unsigned long[2])
extern int      gsio_owned;
void            sio_poll(void);
void            gsio_rx_service(void);
int             sioopen(dev_t dev, int flag, int mode);
int             sioclose(dev_t dev, int flag, int mode);
int             sioread(dev_t dev, struct uio *uio, int flag);
int             siowrite(dev_t dev, struct uio *uio, int flag);
int             sioioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
int             sioselect(dev_t dev, int rw);

#endif /* KERNEL */

#endif /* _GBA_DEV_SIO_UART_H_ */
