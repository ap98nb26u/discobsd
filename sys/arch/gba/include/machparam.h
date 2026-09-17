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
#ifdef GBAED
/*
 * GBAED runs its swap on the real SD card, so the EWRAM swap-staging region
 * the mrams build needs is free to become user RAM: USERRAM fills all of
 * EWRAM after the u/u0 area (see kern_gbaed.ldscript). -DGBAED is set only in
 * the GBAED kernel build (sys/arch/gba/compile/GBAED/Makefile PARAM); the
 * plain GBA build and all userland keep the 64K value.
 */
#define MAXMEM		(250*1024)
#else
#define MAXMEM		(64*1024)
#endif

/*
 * System parameter formulae.
 */
#ifndef NBUF
#define NBUF            3                      /* number of i/o buffers */
#endif
#ifndef MAXUSERS
#define MAXUSERS        1                       /* number of user logins */
#endif
/*
 * Kernel table sizes. Deep shell pipelines are bounded by these: each
 * `|` is a pipe() = 1 inode + 2 file-table entries + (per stage) 1 proc.
 * Measured on real GBAED hardware (project_gba_oom_root_cause): a
 * cat-pipeline ran fine to 9 stages but "file: table full" / "cannot make
 * pipe" at 10 (9 pipes = 18 file entries + ~6 baseline = the old NFILE 24).
 * NFILE is the first wall (2 entries per pipe), then NINODE (1 per pipe),
 * then NPROC (1 per stage). Raised to roughly double the usable depth
 * (~18-stage pipelines): NFILE 40 -> ~18 pipes, NINODE 32 and NPROC 28
 * keep pace. Cost is IWRAM .bss (struct file 24B, inode 108B, proc ~92B;
 * NNAMECACHE and SMAPSIZ scale off these) and must stay under the SYSVEC
 * vector at IWRAM+0x6400 - the kern.ldscript ASSERT enforces it. At these
 * values ~2K of the ~3.9K IWRAM slack below SYSVEC is used, leaving margin;
 * pushing much higher needs the SYSVEC-relocation reclamation first
 * (project_gba_config_completeness workstream #4). Shared by both the GBA
 * (mrams) and GBAED builds - rebuild both when changing these.
 */
#ifndef NPROC
#define NPROC           28                      /* number of processes */
#endif
#ifndef NINODE
#define NINODE          32
#endif
#ifndef NFILE
#define NFILE           40
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
#ifdef GBAED
#define USER_DATA_SIZE          (250 * 1024)    /* GBAED: match kern_gbaed.ldscript USERRAM/MAXMEM. */
#else
#define USER_DATA_SIZE          (64 * 1024)     /* GBA/mrams: match kern_gba.ldscript USERRAM/MAXMEM. */
#endif
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
/*
 * Initial user stack size. This port has no MMU and no stack-fault growth (the
 * whole 64K USERRAM window is directly accessible, so a deep user stack never
 * faults), which once let p_ssize stay frozen at this value while a process's
 * stack grew past it - swapout()/swapin() (vm_swap.c) save only p_ssize bytes
 * from the top, so a deep stack was truncated across a swap and e.g. a shell
 * building a >=6-stage pipeline silently produced 0 bytes (see
 * project_gba_deep_pipeline_data_loss). That is now fixed by growing p_ssize
 * (and p_saddr) from the live sp on every kernel entry, in gba/syscall.c,
 * exactly as the pic32 port does in its exception.c - so this value is just the
 * starting size, matching pic32.
 */
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
 * the SYSVEC region reserved in gba/conf/kern.ldscript AND the literal
 * in the native-toolchain runtime (usr.bin/as/arm-dev/rt/clib.s);
 * changing it forces a full userland rebuild (every binary bakes this
 * address into its syscall stubs).
 *
 * Placed high in IWRAM, just under the BIOS-convention IRQ stack region
 * (0x03007F00..0x03007FA0), so the kernel's .data/.bss can occupy IWRAM
 * all the way up to here instead of being capped at the old 0x03006400 -
 * reclaiming ~6.5K of IWRAM for kernel tables (NFILE/NINODE/NPROC etc.).
 * The SYS/USR kernel stack is in EWRAM (locore0.S), so only the small
 * IRQ stack sits above this point.
 */
#define SYSCALL_VECTOR_ADDR	0x03007E00

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
