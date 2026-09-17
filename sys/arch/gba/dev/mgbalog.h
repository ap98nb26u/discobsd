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

/*
 * AGBPrint: the debug-print protocol VisualBoyAdvance captures with its
 * "--gdb"-independent AGBPrint support (Tools -> Log window, or run with
 * --agb-print). Unlike mGBA's flat 0x04FFFxxx registers, AGBPrint lives in
 * the high cartridge address space: a small context struct + a print buffer
 * that the emulator reads on flush. VBA validates the buffer bank (0xfd ->
 * 0x09FD0000), reads bytes get..put, prints them, and writes get:=put back.
 *
 * IMPORTANT: these are real cartridge-bus addresses. On the EverDrive
 * (GBAED build) that space overlaps the cartridge's own registers, so this
 * is only ever used on the plain GBA (mrams / emulator) build, and only when
 * NOT running under mGBA (which has its own MGBA_LOG path above) - see the
 * gating in mgba_uart.c. On real non-EverDrive flash carts the writes land
 * in inert ROM space and are harmless.
 */
#define AGB_PRINT_PROTECT (*(volatile unsigned short*)0x09FE2FFE)
#define AGB_PRINT_CTX     ((volatile unsigned short*)0x09FE20F8)  /* [0]=request [1]=bank [2]=get [3]=put */
#define AGB_PRINT_BUFFER  ((volatile unsigned short*)0x09FD0000)

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

//extern struct tty uartttys[NUART];

#endif /* KERNEL */

#endif /* !_MGBAUART_H */
