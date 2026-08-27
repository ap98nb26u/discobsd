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
void            gsio_putc(unsigned char c);
int             gsio_avail(void);

#endif /* KERNEL */

#endif /* _GBA_DEV_SIO_UART_H_ */
