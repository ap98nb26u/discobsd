/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)machdep.c	2.4 (2.11BSD) 1999/9/13
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/dir.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/fs.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/clist.h>
#include <sys/callout.h>
#include <sys/reboot.h>
#include <sys/msgbuf.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/config.h>
#include <sys/tty.h>

#include <machine/fault.h>

//#include <gba/dev/uart.h>

#include <gba/dev/mgbalog.h>

int cpu_khz = 16777;
int bus_khz = 16777;

char	machine[] = MACHINE;		/* from <machine/machparam.h> */
char	machine_arch[] = MACHINE_ARCH;	/* from <machine/machparam.h> */
char	cpu_model[64];

int	hz = HZ;
int	usechz = (1000000L + HZ - 1) / HZ;

#ifdef TIMEZONE
struct timezone		tz = { TIMEZONE, DST };
#else
struct timezone		tz = { 8 * 60, 1 };
#endif

int			nproc = NPROC;

struct namecache	namecache[NNAMECACHE];
char			bufdata[NBUF * MAXBSIZE];
struct inode		inode[NINODE];
struct callout		callout[NCALL];
struct mount		mount[NMOUNT];
struct buf		buf[NBUF], bfreelist[BQUEUES];
struct bufhd		bufhash[BUFHSZ];
struct cblock		cfree[NCLIST];
struct proc		proc[NPROC];
struct file		file[NFILE];

/*
 * Remove the ifdef/endif to run the kernel in unsecure mode even when in
 * a multiuser state.  Normally 'init' raises the security level to 1
 * upon transitioning to multiuser.  Setting the securelevel to -1 prevents
 * the secure level from being raised by init.
 */
#ifdef PERMANENTLY_INSECURE
int	securelevel = -1;
#else
int	securelevel = 0;
#endif

struct mapent	swapent[SMAPSIZ];
struct map	swapmap[1] = {
	{ swapent,
	  &swapent[SMAPSIZ],
	  "swapmap" },
};

int	waittime = -1;

static int
nodump(dev_t dev)
{
	printf("\ndumping to dev %o off %D: not implemented\n",
	    dumpdev, dumplo);

	return 0;
}

int (*dump)(dev_t) = nodump;

dev_t	pipedev;
daddr_t	dumplo = (daddr_t)1024;

#if 0
static void
SystemClock_Config(void)
{
	/* Enable HSE oscillator. */

	LL_RCC_HSE_Enable();
	while (LL_RCC_HSE_IsReady() != 1)
		;

	/* Set FLASH latency. */

	/* Enable PWR clock. */
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);

	LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);


	/* Main PLL configuration and activation. */

	LL_RCC_PLL_Enable();
	while (LL_RCC_PLL_IsReady() != 1)
		;

	/* SysClk activation on the main PLL. */
	LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
	LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
	while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
		;

	/* Set APB1 & APB2 prescaler. */

	/* Set SysTick to 1ms. */
	SysTick_Config(CPU_KHZ);

	/* Update CMSIS variable (or through SystemCoreClockUpdate()). */
	SystemCoreClock = CPU_KHZ * 1000;
}
#endif // 0

/*
 * Machine dependent startup code.
 */
void
startup(void)
{
	/*
	 * Early setup for console devices.
	 */
#if CONS_MAJOR == UART_MAJOR
	uartinit(CONS_MINOR);
#endif

	/*
	 * When User button is pressed - boot to single user mode.
	 */
	boothowto = 0;
}

static void
cpuidentify(void)
{
	printf("cpu: ARM7TDMI");
	printf(", %u MHz, bus %u MHz\n", CPU_KHZ/1000, BUS_KHZ/1000);

}

#define REG_BASE     0x04000000

#define REG_DISPCNT  (*(volatile uint16_t*)(REG_BASE + 0x0000))
#define REG_DISPSTAT (*(volatile uint16_t*)(REG_BASE + 0x0004))

#define REG_BGCNT    ((volatile uint16_t *)(REG_BASE + 0x0008))
#define REG_BG0CNT   REG_BGCNT[0]
#define REG_BG1CNT   REG_BGCNT[1]
#define REG_BG2CNT   REG_BGCNT[2]
#define REG_BG3CNT   REG_BGCNT[3]
#define REG_BG0HOFS  ((volatile uint16_t *)(REG_BASE + 0x0010))
#define REG_BG0VOFS  ((volatile uint16_t *)(REG_BASE + 0x0012))

#define MEM_PAL      0x05000000
#define BG_PALETTE   ((volatile uint16_t *)MEM_PAL)
#define OBJ_PALETTE  ((volatile uint16_t *)(MEM_PAL + 0x200))
#define REG_PALETTE  BG_PALETTE

#define VRAM         ((volatile uint16_t*)0x06000000)

#define REG_IME      (*(volatile uint16_t*)0x04000208)
#define REG_IE       (*(volatile uint16_t*)0x04000200)
#define REG_IF       (*(volatile uint16_t*)0x04000202)

#define IRQ_VBLANK  (1 << 0)
#define IRQ_HBLANK  (1 << 1)
#define IRQ_VCOUNT  (1 << 2)
#define IRQ_TIMER0  (1 << 3)
#define IRQ_KEYPAD  (1 << 12)

volatile uint16_t frame_count = 0;
void
gba_intr_handler(void)
{
	uint16_t flags = REG_IF & REG_IE;

	if (flags & IRQ_VBLANK) {
		frame_count++;
		if (frame_count & 1)
			VRAM[0] = 0x7C00; // red
		else
			VRAM[0] = 0x001F; // blue
	}

	REG_IF = flags; // cleaer only processed
}

static inline void irq_enable(void)
{
	REG_IME = 0;

	REG_IE = IRQ_VBLANK;
	REG_IF = 0xFFFF; // clear all flags

	REG_DISPSTAT |= (1 << 3); // enable VBLANK IRQ

	REG_IME = 1;
}

/*
 * Check whether the controller has been successfully initialized.
 */
static int
is_controller_alive(struct driver *driver, int unit)
{
	struct conf_ctlr *ctlr;

	/* No controller - that's OK. */
	if (driver == 0)
		return 1;

	for (ctlr = conf_ctlr_init; ctlr->ctlr_driver; ctlr++) {
		if (ctlr->ctlr_driver == driver &&
		    ctlr->ctlr_unit == unit && ctlr->ctlr_alive) {
			return 1;
		}
	}

	return 0;
}

extern void cpu_initclocks(void);
extern void gba_intr_stub(void);

/*
 * Configure all controllers and devices as specified
 * in the kernel configuration file.
 */
void
config(void)
{
	struct conf_ctlr *ctlr;
	struct conf_device *dev;

	cpuidentify();
	//cpu_initclocks();
	//printf("bofore irq enable\n");

	REG_DISPCNT = 0x0403;
	//BG_PALETTE[0] = 0x7FFF;
	//BG_PALETTE[1] = 0x001F;
	//BG_PALETTE[2] = 0x03E0;
	//BG_PALETTE[3] = 0x7C00;

	for (int i = 0; i < 240*160; i++) {
		VRAM[i] = 0x7C00;
	}
	irq_enable();
	printf("after irq enable\n");

	//while (1) {
	//	if (*(volatile uint16_t *)0x04000202 & 1) {
	//		*(volatile uint16_t *)0x05000000 = 0x001F;
	//	}
	//}
	/* Probe and initialize controllers first. */
	for (ctlr = conf_ctlr_init; ctlr->ctlr_driver; ctlr++) {
		if ((*ctlr->ctlr_driver->d_init)(ctlr)) {
			ctlr->ctlr_alive = 1;
		}
	}

	/* Probe and initialize devices. */
	for (dev = conf_device_init; dev->dev_driver; dev++) {
		if (is_controller_alive(dev->dev_cdriver, dev->dev_ctlr)) {
			if ((*dev->dev_driver->d_init)(dev)) {
				dev->dev_alive = 1;
			}
		}
	}
}

/*
 * Sit and wait for something to happen...
 */
void
idle(void)
{
#if 0
	/* Indicate that no process is running. */
	noproc = 1;

	/* Set SPL low so we can be interrupted. */
	int x = spl0();

	led_control(LED_KERNEL, 0);

	/* Wait for something to happen. */
	__DSB();
	__ISB();
	__WFI();

	/* Restore previous SPL. */
	splx(x);
#endif // 0
}

void
boot(dev_t dev, int howto)
{
	if ((howto & RB_NOSYNC) == 0 && waittime < 0 && bfreelist[0].b_forw) {
		struct fs *fp;
		struct buf *bp;
		int iter, nbusy;

		/*
		 * Force the root filesystem's superblock to be updated,
		 * so the date will be as current as possible after
		 * rebooting.
		 */
		fp = getfs(rootdev);
		if (fp)
			fp->fs_fmod = 1;
		waittime = 0;
		printf("syncing disks... ");
		(void)splnet();
		sync();
		for (iter = 0; iter < 20; iter++) {
			nbusy = 0;
			for (bp = &buf[NBUF]; --bp >= buf;)
				if (bp->b_flags & B_BUSY)
					nbusy++;
			if (nbusy == 0)
				break;
			printf("%d ", nbusy);
			mdelay(40L * iter);
		}
		printf("done\n");
	}
	(void)splhigh();
	if (!(howto & RB_HALT)) {
		if ((howto & RB_DUMP) && dumpdev != NODEV) {
			/*
			 * Take a dump of memory by calling (*dump)(),
			 * which must correspond to dumpdev.
			 * It should dump from dumplo blocks to the end
			 * of memory or to the end of the logical device.
			 */
			(*dump)(dumpdev);
		}
		/* Restart from dev, howto. */

		/* Reset microcontroller. */
		//NVIC_SystemReset();
		/* NOTREACHED */
	}
	printf("halted\n");

#ifdef HALTREBOOT
	printf("press any key to reboot...\n");
	cngetc();

	/* Reset microcontroller. */
	//NVIC_SystemReset();
	/* NOTREACHED */
#endif

	printf("reboot failed; spinning\n");
	for (;;) {
		//__DSB();
		//__ISB();
		//__WFI();
	}
	/* NOTREACHED */
}

/*
 * Millisecond delay routine.
 *
 * Uses SysTick, which must be configured to a 1ms timebase.
 * This is a busy-wait blocking delay, so be wise with use.
 */
void
mdelay(u_int msec)
{
	//LL_mDelay(msec);
}

/*
 * Control LEDs, installed on the board.
 */
void
led_control(int mask, int on)
{
}

/*
 * Increment user profiling counters.
 */
void
addupc(caddr_t pc, struct uprof *pbuf, int ticks)
{
	u_int indx;

	if (pc < (caddr_t)pbuf->pr_off)
		return;

	indx = pc - (caddr_t)pbuf->pr_off;
	indx = (indx * pbuf->pr_scale) >> 16;
	if (indx >= pbuf->pr_size)
		return;

	pbuf->pr_base[indx] += ticks;
}

/*
 * ffs -- vax ffs instruction
 */
int
ffs(u_long mask)
{
	int cnt;

	if (mask == 0)
		return 0;
	for (cnt = 1; !(mask & 1); cnt++)
		mask >>= 1;

	return cnt;
}

/*
 * Copy a null terminated string from one point to another.
 * Returns zero on success, ENOENT if maxlength exceeded.
 * If lencopied is non-zero, *lencopied gets the length of the copy
 * (including the null terminating byte).
 */
int
copystr(caddr_t src, caddr_t dest, u_int maxlength, u_int *lencopied)
{
	caddr_t dest0 = dest;
	int error = ENOENT;

	if (maxlength != 0) {
		while ((*dest++ = *src++) != '\0') {
			if (--maxlength == 0) {
				/* Failed. */
				goto done;
			}
		}
		/* Succeeded. */
		error = 0;
	}
done:
	if (lencopied != 0)
		*lencopied = dest - dest0;

	return error;
}

/*
 * Calculate the length of a string.
 */
size_t
strlen(const char *s)
{
	const char *s0 = s;

	while (*s++ != '\0')
		;

	return s - s0 - 1;
}

/*
 * Return 0 if a user address is valid.
 * There is only one memory region allowed for user: RAM.
 */
int
baduaddr(caddr_t addr)
{
	if (addr >= (caddr_t)__user_data_start &&
	    addr < (caddr_t)__user_data_end)
		return 0;

	return 1;
}

/*
 * Return 0 if a kernel address is valid.
 * There are two memory regions allowed for kernel: RAM and flash.
 */
int
badkaddr(caddr_t addr)
{
	if (addr >= (caddr_t)__kernel_data_start &&
	    addr < (caddr_t)__kernel_data_end)
		return 0;
	if (addr >= (caddr_t)__kernel_flash_start &&
	    addr < (caddr_t)__kernel_flash_end)
		return 0;

	return 1;
}

/*
 * Insert the specified element into a queue immediately after
 * the specified predecessor element.
 */
void
insque(void *element, void *predecessor)
{
	struct que {
		struct que *q_next;
		struct que *q_prev;
	};

	struct que *e = (struct que *)element;
	struct que *prev = (struct que *)predecessor;

	e->q_prev = prev;
	e->q_next = prev->q_next;
	prev->q_next->q_prev = e;
	prev->q_next = e;
}

/*
 * Remove the specified element from the queue.
 */
void
remque(void *element)
{
	struct que {
		struct que *q_next;
		struct que *q_prev;
	};

	struct que *e = (struct que *)element;

	e->q_prev->q_next = e->q_next;
	e->q_next->q_prev = e->q_prev;
}

/*
 * Compare strings.
 */
int
strncmp(const char *s1, const char *s2, size_t n)
{
	int ret, tmp;

	if (n == 0)
		return 0;

	do {
		ret = *s1++ - (tmp = *s2++);
	} while ((ret == 0) && (tmp != 0) && --n);

	return ret;
}

/* Nonzero if pointer is not aligned on a "sz" boundary. */
#define UNALIGNED(p, sz)	((u_int)(p) & ((sz) - 1))

/*
 * Copy data from the memory region pointed to by src0 to the memory
 * region pointed to by dst0.
 * If the regions overlap, the behavior is undefined.
 */
void
bcopy(const void *src0, void *dst0, size_t nbytes)
{
	u_char		*dst = dst0;
	const u_char	*src = src0;
	u_int		*aligned_dst;
	const u_int	*aligned_src;

	/* printf("bcopy (%08x, %08x, %d)\n", src0, dst0, nbytes); */
	/* If the size is small, or either SRC or DST is unaligned,
	 * then punt into the byte copy loop.  This should be rare. */
	if (nbytes >= 4 * sizeof(u_int) &&
	    !UNALIGNED(src, sizeof(u_int)) &&
	    !UNALIGNED(dst, sizeof(u_int))) {
		aligned_dst = (u_int *)dst;
		aligned_src = (const u_int *)src;

		/* Copy 4X unsigned words at a time if possible. */
		while (nbytes >= 4 * sizeof(u_int)) {
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			nbytes -= 4 * sizeof(u_int);
		}

		/* Copy one unsigned word at a time if possible. */
		while (nbytes >= sizeof(u_int)) {
			*aligned_dst++ = *aligned_src++;
			nbytes -= sizeof(u_int);
		}

		/* Pick up any residual with a byte copier. */
		dst = (u_char *)aligned_dst;
		src = (const u_char *)aligned_src;
	}

	while (nbytes--)
		*dst++ = *src++;
}

void *
memcpy(void *dst, const void *src, size_t nbytes)
{
	bcopy(src, dst, nbytes);

	return dst;
}

/*
 * Fill the array with zeroes.
 */
void
bzero(void *dst0, size_t nbytes)
{
	u_char *dst;
	u_int *aligned_dst;

	dst = (u_char *)dst0;
	while (UNALIGNED(dst, sizeof(u_int))) {
		*dst++ = 0;
		if (--nbytes == 0)
			return;
	}

	if (nbytes >= sizeof(u_int)) {
		/*
		 * If we get this far, we know that nbytes is large
		 * and dst is word-aligned.
		 */
		aligned_dst = (u_int *)dst;

		while (nbytes >= 4 * sizeof(u_int)) {
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			nbytes -= 4 * sizeof(u_int);
		}
		while (nbytes >= sizeof(u_int)) {
			*aligned_dst++ = 0;
			nbytes -= sizeof(u_int);
		}
		dst = (u_char *)aligned_dst;
	}

	/* Pick up the remainder with a bytewise loop. */
	while (nbytes--)
		*dst++ = 0;
}

/*
 * Compare not more than nbytes of data pointed to by m1 with
 * the data pointed to by m2.
 * Return an integer greater than, equal to or less than zero
 * according to whether the object pointed to by m1 is greater
 * than, equal to or less than the object pointed to by m2.
 */
int
bcmp(const void *m1, const void *m2, size_t nbytes)
{
	const u_char *s1 = (const u_char *)m1;
	const u_char *s2 = (const u_char *)m2;
	const u_int *aligned1, *aligned2;

	/*
	 * If the size is too small, or either pointer is unaligned,
	 * then we punt to the byte compare loop.
	 * Hopefully this will not turn up in inner loops.
	 */
	if (nbytes >= 4 * sizeof(u_int) &&
	    !UNALIGNED(s1, sizeof(u_int)) &&
	    !UNALIGNED(s2, sizeof(u_int))) {
		/* Otherwise, load and compare the blocks of memory one
		   word at a time. */
		aligned1 = (const u_int *)s1;
		aligned2 = (const u_int *)s2;
		while (nbytes >= sizeof(u_int)) {
			if (*aligned1 != *aligned2)
				break;
			aligned1++;
			aligned2++;
			nbytes -= sizeof(u_int);
		}

		/* Check remaining characters. */
		s1 = (const u_char *)aligned1;
		s2 = (const u_char *)aligned2;
	}
	while (nbytes--) {
		if (*s1 != *s2)
			return *s1 - *s2;
		s1++;
		s2++;
	}

	return 0;
}

int
copyout(caddr_t from, caddr_t to, u_int nbytes)
{
	/* printf("copyout(from=%p, to=%p, nbytes=%u)\n", from, to, nbytes); */
	if (baduaddr(to) || baduaddr(to + nbytes - 1))
		return EFAULT;
	bcopy(from, to, nbytes);

	return 0;
}

int
copyin(caddr_t from, caddr_t to, u_int nbytes)
{
	if (baduaddr(from) || baduaddr(from + nbytes - 1))
		return EFAULT;
	bcopy(from, to, nbytes);

	return 0;
}
