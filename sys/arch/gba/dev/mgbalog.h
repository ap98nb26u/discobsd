#ifndef _MGBAUART_H
#define _MGBAUART_H

#define NUART 1

#ifdef KERNEL

/*
 * AGBPrint: the standard GBA debug-print that mGBA and VisualBoyAdvance
 * capture (VBA 1.8.0 with --verbose=512 --agb-print). It lives in the high
 * cartridge address space: a small context struct + a print buffer, flushed
 * by a software interrupt (comment 0xFA). The emulator reads the buffer bank
 * (0x1fd -> 0x09FD0000) over get..put, prints the bytes, then writes get:=put
 * back.
 *
 * IMPORTANT: these are real cartridge-bus addresses and the flush is a real
 * svc (this port dispatches user syscalls through a simulated SWI and has no
 * real svc handler). So AGBPrint is used ONLY in the emulator-only GBALOG
 * build (options AGBPRINT); the plain GBA and GBAED kernels contain none of
 * it and stay safe on real (non-EverDrive) hardware.
 */
/*
 * mGBA's debug-enable register - written/read ONLY to tell mGBA from VBA at
 * boot (writing 0xC0DE reads back 0x1DEA under mGBA, open bus elsewhere). Not
 * used for logging any more (that is all AGBPrint); it just decides whether
 * agb_puts appends a newline, since mGBA adds its own per flush and VBA does
 * not. AGBPRINT-only, so it is absent from the plain GBA/GBAED kernels.
 */
#define MGBA_REG_DEBUG_ENABLE (*(volatile unsigned short*)0x04FFF780)

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
