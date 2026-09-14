/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <sys/systm.h>

void
brk()
{
    struct a {
        int naddr;
    };
    register int newsize, d;

    /* set newsize to new data size */
    newsize = ((struct a*)u.u_arg)->naddr - u.u_procp->p_daddr;
    if (newsize < 0)
        newsize = 0;
    if (u.u_tsize + newsize + u.u_ssize > MAXMEM) {
        u.u_error = ENOMEM;
        return;
    }
    /*
     * Also refuse to grow past what swap can hold. The test above
     * bounds the resident image (physical user RAM); this one bounds
     * the *swapped* image, since the swapper must be able to write the
     * whole process (data + stack + u.) out to swap. If it cannot, the
     * old behavior was to panic("out of swap space") from swapout() the
     * instant this process was picked for eviction, taking the whole
     * system down. Rejecting the growth here turns "grew bigger than
     * swap" into an ordinary ENOMEM the process sees from sbrk()/malloc()
     * (e.g. a native compiler on a large source can fail gracefully),
     * long before any swap-out is attempted. Sizes are converted to
     * DEV_BSIZE swap blocks to compare against nswap; this mirrors the
     * three-part allocation swapout()/malloc3() performs.
     */
    if (btod (newsize) + btod (u.u_ssize) + btod (USIZE) > nswap) {
        u.u_error = ENOMEM;
        return;
    }

    u.u_procp->p_dsize = newsize;

    /* set d to (new - old) */
    d = newsize - u.u_dsize;
//printf ("brk: new size %u bytes, incremented by %d\n", newsize, d);
    if (d > 0)
        bzero ((void*) (u.u_procp->p_daddr + u.u_dsize), d);
    u.u_dsize = newsize;
    u.u_rval = u.u_procp->p_daddr + u.u_dsize;
}
