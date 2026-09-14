/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <machine/debug.h>

#define MINFINITY   -32767      /* minus infinity */

int     maxslp = MAXSLP;
char    runin;                  /* scheduling flag */
char    runout;                 /* scheduling flag */

/*
 * Is p currently on the run queue? Used by the out-of-swap kill path
 * below to re-queue a victim idempotently: setrq() panics (under
 * DIAGNOSTIC) if handed a process that is already queued, and psignal()
 * may itself have queued the victim via setrun(), so we must not blindly
 * setrq() it a second time. Caller holds splhigh().
 */
static int
on_runq (p)
    register struct proc *p;
{
    register struct proc *q;

    for (q = qs; q != NULL; q = q->p_link)
        if (q == p)
            return 1;
    return 0;
}

/*
 * The main loop of the scheduling (swapping) process.
 * The basic idea is:
 *  see if anyone wants to be swapped in
 *  swap out processes until there is room
 *  swap him in
 *  repeat
 * The runout flag is set whenever someone is swapped out.  Sched sleeps on
 * it awaiting work.  Sched sleeps on runin whenever it cannot find enough
 * core (by swapping out or otherwise) to fit the selected swapped process.
 * It is awakened when the core situation changes and in any case once per
 * second.
 */
void
sched()
{
    register struct proc *rp;
    struct proc *swapped_out = 0, *in_core = 0;
    register int out_time, rptime;

    for (;;) {
        /* Perform swap-out/swap-in action. */
        spl0();
        if (in_core &&
            swapout (in_core, X_FREECORE, X_OLDSIZE, X_OLDSIZE) != 0) {
            /*
             * Out of swap space. This used to panic("out of swap
             * space") from swapout(), killing the whole system.
             * Instead, kill the process we were trying to evict.
             * It is still fully resident (swapout failed before
             * freeing its core and left p_addr/p_daddr/p_saddr
             * untouched), so it can run and die without needing any
             * swap; reaping it frees the core so the swapped-out
             * process can be brought in on a later pass. On this
             * port only one non-system process is resident at a
             * time, so in_core is exactly that process - during a
             * heavy job (e.g. a native compile that grew large) it
             * is the memory hog itself. Restore the state sched()
             * had already torn down for the swap (SLOAD cleared and,
             * if runnable, removed from the run queue) so the doomed
             * process is coherent, post the fatal signal, and back
             * off: its exit wakes us on runin (kern_exit.c). brk()
             * refuses growth that would not fit in swap, so this
             * path is the transient/multi-process safety net, not
             * the common case.
             */
            int s;

            s = splhigh();
            /*
             * The victim is still fully resident (swapout failed before
             * freeing its core, leaving p_addr/p_daddr/p_saddr intact),
             * so it can run and die without needing any swap; reaping it
             * frees the core for the swapped-out process on a later pass.
             * Make it resident+runnable and post the fatal signal.
             *
             * Ordering and idempotence matter here. sched() cleared this
             * process's SLOAD and (if it was SRUN) removed it from the run
             * queue before we attempted the swap-out, but that happened at
             * a lower spl and the swap-out ran with interrupts enabled, so
             * the victim's state may have changed underneath us (a wakeup
             * could have made a sleeper SRUN, etc.). Rather than assume a
             * particular prior state: (1) mark it loaded; (2) psignal(),
             * which safely makes a sleeping/stopped target runnable (and
             * may itself setrq() it via setrun()); (3) only then, if it is
             * SRUN but NOT already queued, add it - guarded by on_runq()
             * because both setrq() and setrun() panic on a double enqueue.
             * Announce only once per victim (SIGKILL already pending means
             * we have been here before, e.g. re-selected while dying).
             */
            if ((in_core->p_sig & sigmask(SIGKILL)) == 0)
                printf("out of swap: killing pid %d (%u kbytes)\n",
                    in_core->p_pid,
                    (in_core->p_dsize + in_core->p_ssize + USIZE) / 1024);
            in_core->p_flag |= SLOAD;       /* never actually left core */
            psignal (in_core, SIGKILL);
            if (in_core->p_stat == SRUN && !on_runq (in_core))
                setrq (in_core);
            in_core = 0;
            swapped_out = 0;                /* no room made; don't swap in */
            ++runin;
            sleep ((caddr_t) &runin, PSWP); /* let the victim run and die */
            splx (s);
            continue;
        }
        if (swapped_out) {
            //printf("DBG: sched swapin pid=%d\n", swapped_out->p_pid);
            swapin (swapped_out);
            //printf("DBG: sched after swapin\n");
        }
        splhigh();
        in_core = 0;
        swapped_out = 0;

        /* Find user to swap in; of users ready,
         * select one out longest. */
        out_time = -20000;
        for (rp = allproc; rp; rp = rp->p_nxt) {
            if (rp->p_stat != SRUN || (rp->p_flag & SLOAD))
                continue;
            rptime = rp->p_time - rp->p_nice * 8;

            /*
             * Always bring in parents ending a vfork,
             * to avoid deadlock
             */
            if (rptime > out_time || (rp->p_flag & SVFPRNT)) {
                swapped_out = rp;
                out_time = rptime;
                if (rp->p_flag & SVFPRNT)
                    break;
            }
        }

        /* If there is no one there, wait. */
        if (! swapped_out) {
            ++runout;
            sleep ((caddr_t) &runout, PSWP);
            continue;
        }

        /*
         * Look around for somebody to swap out.
         * There may be only one non-system loaded process.
         */
        for (rp = allproc; rp != NULL; rp = rp->p_nxt) {
            if (rp->p_stat != SZOMB &&
                (rp->p_flag & (SSYS | SLOAD)) == SLOAD) {
                in_core = rp;
                break;
            }
        }
        if (! in_core) {
            /* In-core memory is empty. */
            continue;
        }

        /*
         * Swap found user out if sleeping interruptibly, or if he has spent at
         * least 1 second in core and the swapped-out process has spent at
         * least 2 seconds out.  Otherwise wait a bit and try again.
         */
        if (! (in_core->p_flag & SLOCK) &&
            (in_core->p_stat == SSTOP ||
             (in_core->p_stat == SSLEEP && (in_core->p_flag & P_SINTR)) ||
             ((in_core->p_stat == SRUN || in_core->p_stat == SSLEEP) &&
              out_time >= 2 &&
              in_core->p_time + in_core->p_nice >= 1)))
        {
            /* Swap out in-core process. */
            in_core->p_flag &= ~SLOAD;
            if (in_core->p_stat == SRUN)
                remrq (in_core);
        } else {
            /* Nothing to swap in/out. */
            in_core = 0;
            swapped_out = 0;
            ++runin;
            sleep ((caddr_t) &runin, PSWP);
        }
    }
}

/*
 * Count up various things once a second
 */
void
vmmeter()
{
#ifdef UCB_METER
    register u_short *cp, *rp;
    register long *sp;

    ave(avefree, freemem, 5);
    ave(avefree30, freemem, 30);
    cp = &cnt.v_first;
    rp = &rate.v_first;
    sp = &sum.v_first;
    while (cp <= &cnt.v_last) {
        ave(*rp, *cp, 5);
        *sp += *cp;
        *cp = 0;
        rp++, cp++, sp++;
    }
#endif

    if (time.tv_sec % 5 == 0) {
        vmtotal();
#ifdef UCB_METER
        rate.v_swpin = cnt.v_swpin;
        sum.v_swpin += cnt.v_swpin;
        cnt.v_swpin = 0;
        rate.v_swpout = cnt.v_swpout;
        sum.v_swpout += cnt.v_swpout;
        cnt.v_swpout = 0;
#endif
    }
}

/*
 * Compute Tenex style load average.  This code is adapted from similar code
 * by Bill Joy on the Vax system.  The major change is that we avoid floating
 * point since not all pdp-11's have it.  This makes the code quite hard to
 * read - it was derived with some algebra.
 *
 * "floating point" numbers here are stored in a 16 bit short, with 8 bits on
 * each side of the decimal point.  Some partial products will have 16 bits to
 * the right.
 */
static void
loadav (avg, n)
    register short  *avg;
    register int    n;
{
    register int    i;
    static const long cexp[3] = {
        0353,   /* 256 * exp(-1/12)  */
        0373,   /* 256 * exp(-1/60)  */
        0376,   /* 256 * exp(-1/180) */
    };

    for (i = 0; i < 3; i++)
        avg[i] = (cexp[i] * (avg[i]-(n<<8)) + (((long)n)<<16)) >> 8;
}

void
vmtotal()
{
    register struct proc *p;
    register int nrun = 0;
#ifdef UCB_METER
    total.t_vmtxt = 0;
    total.t_avmtxt = 0;
    total.t_rmtxt = 0;
    total.t_armtxt = 0;
    total.t_vm = 0;
    total.t_avm = 0;
    total.t_rm = 0;
    total.t_arm = 0;
    total.t_rq = 0;
    total.t_dw = 0;
    total.t_sl = 0;
    total.t_sw = 0;
#endif
    for (p = allproc; p != NULL; p = p->p_nxt) {
        if (p->p_flag & SSYS)
            continue;
        if (p->p_stat) {
#ifdef UCB_METER
            if (p->p_stat != SZOMB) {
                total.t_vm += p->p_dsize + p->p_ssize + USIZE;
                if (p->p_flag & SLOAD)
                    total.t_rm += p->p_dsize + p->p_ssize
                        + USIZE;
            }
#endif
            switch (p->p_stat) {

            case SSLEEP:
            case SSTOP:
                if (!(p->p_flag & P_SINTR) && p->p_stat == SSLEEP)
                    nrun++;
#ifdef UCB_METER
                if (p->p_flag & SLOAD) {
                    if  (!(p->p_flag & P_SINTR))
                        total.t_dw++;
                    else if (p->p_slptime < maxslp)
                        total.t_sl++;
                } else if (p->p_slptime < maxslp)
                    total.t_sw++;
                if (p->p_slptime < maxslp)
                    goto active;
#endif
                break;

            case SRUN:
            case SIDL:
                nrun++;
#ifdef UCB_METER
                if (p->p_flag & SLOAD)
                    total.t_rq++;
                else
                    total.t_sw++;
active:
                total.t_avm += p->p_dsize + p->p_ssize + USIZE;
                if (p->p_flag & SLOAD)
                    total.t_arm += p->p_dsize + p->p_ssize
                        + USIZE;
#endif
                break;
            }
        }
    }
#ifdef UCB_METER
    total.t_vm += total.t_vmtxt;
    total.t_avm += total.t_avmtxt;
    total.t_rm += total.t_rmtxt;
    total.t_arm += total.t_armtxt;
    total.t_free = avefree;
#endif
    loadav (avenrun, nrun);
}
