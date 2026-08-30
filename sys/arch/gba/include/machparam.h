/*
 * Machine dependent constants for GBA.
 *
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)machparam.h	1.4 (2.11BSD GTE) 1998/9/15
 */

#ifndef ENDIAN

#define MACHINE         "gba"
#define MACHINE_ARCH    "arm"

/*
 * Definitions for byte order,
 * according to byte significance from low address to high.
 */
#define LITTLE          1234            /* least-significant byte first (vax) */
#define BIG             4321            /* most-significant byte first */
#define PDP             3412            /* LSB first in word, MSW first in long (pdp) */
#define ENDIAN          LITTLE          /* byte order on gba */

/*
 * The time for a process to be blocked before being very swappable.
 * This is a number of seconds which the system takes as being a non-trivial
 * amount of real time.  You probably shouldn't change this;
 * it is used in subtle ways (fractions and multiples of it are, that is, like
 * half of a ``long time'', almost a long time, etc.)
 * It is related to human patience and other factors which don't really
 * change over time.
 */
#define MAXSLP          20

/*
 * Clock ticks per second. The HZ value must be an integer factor of 1000.
 * Cortex-M SysTick operates with a 1ms time base, hence 1000 for HZ.
 */
#ifndef HZ
#define HZ              100
#endif

/*
 * Maximum core (text+data+bss+stack) a single process may occupy - on
 * this port that's a hard physical limit, not just a policy knob: only
 * one process's core is ever resident at a time (see kern.ldscript's
 * USERRAM), so this MUST match USERRAM's LENGTH exactly. Previously a
 * stale 128K (unrelated to the actual 32K window of the time), which let
 * exec_estab()'s overflow check silently pass a too-large /bin/sh image
 * that then overran USERRAM into SWAP - see kern.ldscript for the full
 * story. Keep in sync with USER_DATA_SIZE below and with USERRAM by hand
 * if either ever moves again.
 */
#define MAXMEM		(64*1024)

/*
 * System parameter formulae.
 */
#ifndef NBUF
#define NBUF            3                      /* number of i/o buffers */
#endif
#ifndef MAXUSERS
#define MAXUSERS        1                       /* number of user logins */
#endif
#ifndef NPROC
#define NPROC           25                      /* number of processes */
#endif
#ifndef NINODE
#define NINODE          24
#endif
#ifndef NFILE
#define NFILE           24
#endif
#define NNAMECACHE      (NINODE * 11/10)
#define NCALL           (16 + 2 * MAXUSERS)
#define NCLIST          32                      /* number or CBSIZE blocks */
#ifndef SMAPSIZ
#define SMAPSIZ         NPROC                   /* size of swap allocation map */
#endif

/*
 * Disk blocks.
 */
#define DEV_BSIZE       1024            /* the same as MAXBSIZE */
#define DEV_BSHIFT      10              /* log2(DEV_BSIZE) */
#define DEV_BMASK       (DEV_BSIZE-1)

/* Bytes to disk blocks */
#define btod(x)         (((x) + DEV_BSIZE-1) >> DEV_BSHIFT)

/*
 * Needed by userland tools (ps, w) that read another process's memory
 * directly (via /dev/kmem) rather than through the kernel - they have no
 * way to see the kernel's own __user_data_start/__user_data_end linker
 * symbols (kern.ldscript), so the same USERRAM bounds are duplicated here
 * as plain constants. Previously stale (0x20000000/96K, matching neither
 * this port's actual USERRAM origin nor its 32K size - ps.c wouldn't even
 * compile with them commented out) - keep in sync with kern.ldscript's
 * USERRAM MEMORY block by hand if that ever moves again.
 */
#define USER_DATA_START         (0x02001800)
#define USER_DATA_SIZE          (64 * 1024)     /* must match USERRAM/MAXMEM. */
#define USER_DATA_END           (USER_DATA_START + USER_DATA_SIZE)

#define stacktop(siz)           (USER_DATA_END)
#define stackbas(siz)           (USER_DATA_END-(siz))

/*
 * User area: a user structure, followed by the kernel
 * stack.  The number for USIZE is determined empirically.
 *
 * Note that the SBASE and STOP constants are only used by the assembly code,
 * but are defined here to localize information about the user area's
 * layout (see pdp/genassym.c).  Note also that a networking stack is always
 * allocated even for non-networking systems.  This prevents problems with
 * applications having to be recompiled for networking versus non-networking
 * systems.
 */
#define USIZE           3072
#define SSIZE           2048            /* initial stack size (bytes) */

/*
 * GBA has no writable SWI vector (0x00000018 is BIOS ROM), so syscalls
 * are dispatched via a software-simulated SWI: userland calls a fixed,
 * well-known RAM address holding a pointer to the kernel's
 * simulate_swi_via_inline_data() trampoline (see gba/gba/syscall.c),
 * rather than linking against that kernel symbol directly (userland
 * binaries are linked separately from the kernel and never see it).
 * The kernel writes the pointer here once at boot, before running any
 * user process (see gba/gba/machdep.c: startup()). Address must match
 * the SYSVEC region reserved in gba/conf/kern.ldscript.
 */
#define SYSCALL_VECTOR_ADDR	0x03006400

/*
 * Collect kernel statistics by default.
 */
#if !defined(UCB_METER) && !defined(NO_UCB_METER)
#define UCB_METER
#endif

#ifdef KERNEL
#include <machine/intr.h>

/*
 * Macros to decode processor status word.
 */
#define USERMODE(psr)   0 /* ((psr & IPSR_ISR_Msk) == 0) */ /* No exceptions. */
/*
 * Ported from stm32 (Cortex-M's BASEPRI register, real hardware
 * priority-level masking) as a literal "0" stub, i.e. always false.
 * hardclock() (kern_clock.c) only calls softclock() - which actually
 * runs due callout-queue entries, including tsleep()'s timeout wakeup
 * via endtsleep() - when "needsoft && BASEPRI(ps)" is true. Since GBA
 * has no such hardware and this port's IRQ handling is a single,
 * non-nested level (see gba_intr_handler in locore0.S), there's no
 * priority conflict to guard against here: it's always safe to run
 * softclock(). Left at 0 (always false), tsleep()'s timeout could be
 * correctly registered and hardclock() would tick it down to zero, but
 * softclock() would never actually run to fire it - a real timeout
 * would sit expired forever instead of waking its sleeper.
 */
#define BASEPRI(psr)    1

#define noop()          asm volatile("nop")

/*
 * Wait for something to happen.
 */
void idle(void);

/*
 * Millisecond delay routine.
 */
void mdelay(unsigned msec);

/*
 * Setup system timer for `hz' timer interrupts per second.
 */
void clkstart(void);

/*
 * Control LEDs, installed on the board.
 */
#define LED_TTY         0x08
#define LED_SWAP        0x04
#define LED_DISK        0x02
#define LED_KERNEL      0x01
#define LED_ALL         (LED_TTY | LED_SWAP | LED_DISK | LED_KERNEL)

void led_control(int mask, int on);

/* void LL_GPIO_EnableClock(GPIO_TypeDef *GPIOx); */

#endif /* KERNEL */

#endif /* ENDIAN */
