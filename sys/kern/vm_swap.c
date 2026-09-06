/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/systm.h>
#include <sys/vm.h>

/* Diagnostic only: checksum a memory range to check a swap round trip. */
static unsigned
dbg_cksum(size_t addr, size_t len)
{
    unsigned char *p = (unsigned char *)addr;
    unsigned sum = 0;
    size_t i;

    for (i = 0; i < len; i++)
        sum = (sum << 1 | sum >> 31) ^ p[i];
    return sum;
}

/*
 * Real (not diagnostic) integrity check for the data segment's swap
 * round trip. Confirmed on real GBAED hardware via dbg_cksum() above:
 * an intermittent (roughly 1-in-10, in one measured run) mismatch
 * between the checksum computed on swapout and the checksum of what
 * comes back on the very next swapin for the *same* process/blkno/
 * len - i.e. genuine data corruption somewhere in the SD/EverDrive
 * round trip, not a logic bug (ruled out: swap space bookkeeping
 * reuses blocks correctly, and the corruption rate didn't change
 * across a settle-delay increase or a clock-speed swap, so it isn't
 * simply an init-timing or signal-margin issue either - see the
 * comments in gba/dev/sd.c's card_init()/sd_setup()). No CRC exists
 * on the SD read path to detect this at the driver level (sd_crc16()
 * in gba/dev/sd.c is write-only), so this is a software-level
 * safety net: remember what swapout wrote, and if swapin reads back
 * something else, retry the read a few times before giving up -
 * cheap, since the failure is rare, and turns most occurrences into
 * an invisible extra disk read instead of a wild-jump crash.
 * Indexed by proc[] table slot (not pid, which isn't densely bounded)
 * so it can't go out of bounds regardless of pid numbering/wraparound.
 */
static unsigned dcksum_tab[NPROC];

/*
 * Same idea as dcksum_tab above, but for the stack region (p_saddr/
 * p_ssize) - added 2026-09-05 while chasing a real-hardware-only
 * text-segment corruption in /bin/sh (fault()'s own code found wrong
 * at the moment sendsig() jumped into it, right after a genuine
 * stack-growth SIGSEGV - see project_gba_sh_fault_sigreturn_hang.md).
 * The data-region check above never reported a mismatch in that same
 * session, which only proves the *data* region's swap round trip was
 * self-consistent - it says nothing about the stack region, which has
 * never been checked this way. This brackets whether the stack half
 * of the swap round trip is where a corruption is being introduced.
 */
static unsigned scksum_tab[NPROC];

#define SWAPIN_CKSUM_RETRIES 4

/*
 * Swap a process in.
 * Allocate data and possible text separately.  It would be better
 * to do largest first.  Text, data, and stack are allocated in
 * that order, as that is likely to be in order of size.
 * U area goes into u0 buffer.
 */
void
swapin (p)
    register struct proc *p;
{
    size_t daddr = (size_t)__user_data_start;
    size_t saddr = (size_t)__user_data_end - p->p_ssize;
    size_t uaddr = (size_t) &u0;

    if (p->p_dsize) {
        int slot = p - proc;
        unsigned want = dcksum_tab[slot];
        unsigned got;
        int retry;

        swap (p->p_daddr, daddr, p->p_dsize, B_READ);
        got = dbg_cksum(daddr, p->p_dsize);
        //printf("DBG: swapin pid=%d data blkno=%u len=%u cksum=%x\n",
        //    p->p_pid, (unsigned)p->p_daddr, (unsigned)p->p_dsize, got);

        for (retry = 0; got != want && retry < SWAPIN_CKSUM_RETRIES; retry++) {
            printf("DBG: swapin cksum MISMATCH pid=%d blkno=%u "
                "want=%x got=%x, retrying (%d)\n",
                p->p_pid, (unsigned)p->p_daddr, want, got, retry + 1);
            swap (p->p_daddr, daddr, p->p_dsize, B_READ);
            got = dbg_cksum(daddr, p->p_dsize);
        }
        if (got != want)
            printf("DBG: swapin cksum still bad after retries, pid=%d "
                "blkno=%u want=%x got=%x - giving up\n",
                p->p_pid, (unsigned)p->p_daddr, want, got);
        else if (retry)
            printf("DBG: swapin cksum recovered pid=%d after %d retr%s\n",
                p->p_pid, retry, retry == 1 ? "y" : "ies");

        mfree (swapmap, btod (p->p_dsize), p->p_daddr);
    }
    if (p->p_ssize) {
        int slot = p - proc;
        unsigned want = scksum_tab[slot];
        unsigned got;
        int retry;

        swap (p->p_saddr, saddr, p->p_ssize, B_READ);
        got = dbg_cksum(saddr, p->p_ssize);

        for (retry = 0; got != want && retry < SWAPIN_CKSUM_RETRIES; retry++) {
            printf("DBG: swapin STACK cksum MISMATCH pid=%d blkno=%u "
                "want=%x got=%x, retrying (%d)\n",
                p->p_pid, (unsigned)p->p_saddr, want, got, retry + 1);
            swap (p->p_saddr, saddr, p->p_ssize, B_READ);
            got = dbg_cksum(saddr, p->p_ssize);
        }
        if (got != want)
            printf("DBG: swapin STACK cksum still bad after retries, pid=%d "
                "blkno=%u want=%x got=%x - giving up\n",
                p->p_pid, (unsigned)p->p_saddr, want, got);
        else if (retry)
            printf("DBG: swapin STACK cksum recovered pid=%d after %d retr%s\n",
                p->p_pid, retry, retry == 1 ? "y" : "ies");

        mfree (swapmap, btod (p->p_ssize), p->p_saddr);
    }
    swap (p->p_addr, uaddr, USIZE, B_READ);
    mfree (swapmap, btod (USIZE), p->p_addr);

    p->p_daddr = daddr;
    p->p_saddr = saddr;
    p->p_addr = uaddr;
    if (p->p_stat == SRUN)
        setrq (p);
    p->p_flag |= SLOAD;
    p->p_time = 0;
#ifdef UCB_METER
    cnt.v_swpin++;
#endif
}

/*
 * Swap out process p.
 * odata and ostack are the old data size and the stack size
 * of the process, and are supplied during core expansion swaps.
 * The freecore flag causes its core to be freed -- it may be
 * off when called to create an image for a child process
 * in newproc.
 *
 * panic: out of swap space
 */
void
swapout (p, freecore, odata, ostack)
    register struct proc *p;
    int freecore;
    register u_int odata, ostack;
{
    size_t a[3];

    if (odata == (u_int) X_OLDSIZE)
        odata = p->p_dsize;
    if (ostack == (u_int) X_OLDSIZE)
        ostack = p->p_ssize;
    //printf("DBG: before malloc3\n");
    if (malloc3 (swapmap, btod (p->p_dsize), btod (p->p_ssize),
        btod (USIZE), a) == NULL)
        panic ("out of swap space");
    //printf("DBG: after malloc3\n");
    p->p_flag |= SLOCK;
    if (odata) {
        unsigned cksum = dbg_cksum(p->p_daddr, odata);

        //printf("DBG: before swap a[0] odata=%u pid=%d blkno=%u cksum=%x\n",
        //    odata, p->p_pid, (unsigned)a[0], cksum);
        dcksum_tab[p - proc] = cksum;
        swap (a[0], p->p_daddr, odata, B_WRITE);
        //printf("DBG: after swap a[0]\n");
    }
    if (ostack) {
        unsigned cksum = dbg_cksum(p->p_saddr, ostack);

        scksum_tab[p - proc] = cksum;
        //printf("DBG: before swap a[1] ostack=%u\n", ostack);
        swap (a[1], p->p_saddr, ostack, B_WRITE);
        //printf("DBG: after swap a[1]\n");
    }
    /*
     * Increment u_ru.ru_nswap for process being tossed out of core.
     * We can be called to swap out a process other than the current
     * process, so we have to map in the victim's u structure briefly.
     * Note, savekdsa6 *must* be a static, because we remove the stack
     * in the next instruction.  The splclock is to prevent the clock
     * from coming in and doing accounting for the wrong process, plus
     * we don't want to come through here twice.  Why are we doing
     * this, anyway?
     */
    {
        int s;

        s = splclock();
        u.u_ru.ru_nswap++;
        splx (s);
    }
    //printf("DBG: before swap a[2] (uarea)\n");
    swap (a[2], p->p_addr, USIZE, B_WRITE);
    //printf("DBG: after swap a[2]\n");
    p->p_daddr = a[0];
    p->p_saddr = a[1];
    p->p_addr = a[2];
    p->p_flag &= ~(SLOAD|SLOCK);
    p->p_time = 0;

#ifdef UCB_METER
    cnt.v_swpout++;
#endif
    if (runout) {
        runout = 0;
        wakeup ((caddr_t)&runout);
    }
}
