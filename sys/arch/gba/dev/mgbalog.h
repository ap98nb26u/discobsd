#ifndef _MGBAUART_H
#define _MGBAUART_H

#define NUART 1

#ifdef KERNEL

#define MGBA_REG_DEBUG_ENABLE  (*(volatile unsigned short*)0x04FFF780)
#define MGBA_REG_DEBUG_FLAGS   (*(volatile unsigned short*)0x04FFF700)
#define MGBA_REG_DEBUG_BUFFER  ((volatile char*)0x04FFF600)

#define MGBA_LOG_FATAL   0x00
#define MGBA_LOG_ERROR   0x01
#define MGBA_LOG_WARN    0x02
#define MGBA_LOG_INFO    0x03
#define MGBA_LOG_DEBUG   0x04

void            uartinit(int unit);
int             uartopen(dev_t dev, int flag, int mode);
int             uartclose(dev_t dev, int flag, int mode);
int             uartread(dev_t dev, struct uio *uio, int flag);
int             uartwrite(dev_t dev, struct uio *uio, int flag);
int             uartselect(dev_t dev, int rw);
int             uartioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
void            uartintr(dev_t dev);
void            uartstart(struct tty *tp);
void            uartputc(dev_t dev, char c);
char            uartgetc(dev_t dev);

extern struct tty uartttys[NUART];

#endif /* KERNEL */

#endif /* !_MGBAUART_H */
