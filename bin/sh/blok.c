/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

/*
 * storage allocator
 * (circular first fit strategy)
 */

#define BUSY 01
#define busy(x) (Rcheat((x)->word) & BUSY)

unsigned int    brkincr = BRKINCR;
struct blk *blokp;                      /*current search pointer*/
struct blk *bloktop;            /* top of arena (last blok) */

char            *brkbegin;
char            *setbrk();

/* DBG: raw write()-based hex dump, bypassing stdio entirely (this port's
 * bin/sh has no stdio dependency and this needs to work regardless of
 * buffering). */
static void
dbg_hex(label, v)
	char *label;
	unsigned int v;
{
	char buf[64];
	char *p = buf;
	char *s;
	int i;

	for (s = label; *s; s++)
		*p++ = *s;
	*p++ = '0';
	*p++ = 'x';
	for (i = 28; i >= 0; i -= 4) {
		int d = (v >> i) & 0xf;
		*p++ = d < 10 ? '0' + d : 'a' + d - 10;
	}
	*p++ = '\n';
	write(2, buf, p - buf);
}

char *
alloc(nbytes)
	unsigned int nbytes;
{
	register unsigned int rbytes = round(nbytes+BYTESPERWORD, BYTESPERWORD);

	for (;;)
	{
		int     c = 0;
		register struct blk *p = blokp;
		register struct blk *q;

		do
		{
			if (!busy(p))
			{
				/*
				 * A block's word field only means anything - free
				 * link vs terminator - within the arena addblok()
				 * actually manages, i.e. [brkbegin, bloktop]. Any
				 * block reachable via a corrupted chain can end up
				 * pointing past bloktop into memory nothing has ever
				 * initialized (reads as whatever the hardware gives
				 * back for untouched EWRAM/open bus, observed as
				 * 0xe55ec002 chased from a NULL word field on this
				 * port) - walking into that as if it were a real
				 * link corrupts further blocks and can loop forever
				 * (confirmed via a hardware watchpoint + this file's
				 * own diagnostic prints). Bail out to addblok()
				 * instead of trusting q/p once either strays outside
				 * the real arena - worst case this allocates more
				 * arena than strictly needed, which is far better
				 * than corrupting or spinning forever.
				 */
				while (!busy(q = p->word))
				{
					if (q < (struct blk *)brkbegin || q > bloktop) {
						dbg_hex("DBG alloc OOB q bailout q=", (unsigned)q);
						dbg_hex("DBG alloc OOB q bailout bloktop=", (unsigned)bloktop);
						goto grow;
					}
					p->word = q->word;
				}
				if ((char *)q - (char *)p >= rbytes)
				{
					blokp = (struct blk *)((char *)p + rbytes);
					if (q > blokp)
						blokp->word = p->word;
					p->word = (struct blk *)(Rcheat(blokp) | BUSY);
					return((char *)(p + 1));
				}
			}
			q = p;
			p = (struct blk *)(Rcheat(p->word) & ~BUSY);
			if (p < (struct blk *)brkbegin || p > bloktop) {
				dbg_hex("DBG alloc OOB p bailout p=", (unsigned)p);
				dbg_hex("DBG alloc OOB p bailout bloktop=", (unsigned)bloktop);
				goto grow;
			}
		} while (p > q || (c++) == 0);
	grow:
		addblok(rbytes);
	}
}

void
addblok(reqd)
	unsigned int reqd;
{
	dbg_hex("DBG addblok entry reqd=", reqd);
	dbg_hex("DBG addblok entry bloktop=", (unsigned)bloktop);
	dbg_hex("DBG addblok entry stakbas=", (unsigned)stakbas);
	dbg_hex("DBG addblok entry staktop=", (unsigned)staktop);

	if (stakbot == NIL)
	{
                extern int end;
		brkbegin = setbrk(BRKINCR * 5);
		bloktop = (struct blk *) &end;
		dbg_hex("DBG addblok firsttime brkbegin=", (unsigned)brkbegin);
		dbg_hex("DBG addblok firsttime bloktop=", (unsigned)bloktop);
	}

	if (stakbas != staktop)
	{
		register char *rndstak;
		register struct blk *blokstak;
		register struct blk *newtop;

		pushstak(0);
		rndstak = (char *)round(staktop, BYTESPERWORD);
		blokstak = (struct blk *)(stakbas) - 1;
		blokstak->word = stakbsy;
		stakbsy = blokstak;
		dbg_hex("DBG addblok stak-absorb rndstak=", (unsigned)rndstak);

		/*
		 * Same "finalize before publish" fix as below: this new top
		 * block's own word field used to be left whatever it already
		 * held (leftover stak/string bytes just below it, since
		 * rndstak sits right after the absorbed string buffer) -
		 * uninitialized as far as the block arena is concerned. The
		 * BUSY bit on the *old* top only stops alloc()'s inner
		 * coalescing walk from following its word pointer; the outer
		 * do-while in alloc() advances past a busy block regardless
		 * (p = p->word & ~BUSY unconditionally), so a later alloc()
		 * call can land directly on this new top and, if its word
		 * field isn't a proper busy-marked terminator, start the same
		 * chase-a-bad-pointer loop this file's other addblok() path
		 * had (see the comment below - same watchpoint-confirmed bug,
		 * same fix).
		 */
		newtop = (struct blk *)(rndstak);
		newtop->word = (struct blk *)(brkbegin + 1);
		bloktop->word = (struct blk *)(Rcheat(rndstak) | BUSY);
		bloktop = newtop;
		dbg_hex("DBG addblok stak-absorb new bloktop=", (unsigned)bloktop);
		dbg_hex("DBG addblok stak-absorb newtop->word=", (unsigned)newtop->word);
	}
	reqd += brkincr;
	reqd &= ~(brkincr - 1);
	{
		/*
		 * Build and terminate the new top block *before* linking it
		 * in below - previously this linked the old top forward to
		 * the new one first, then set the new top's own terminator,
		 * leaving a brief window where the new top was reachable by
		 * alloc()'s coalescing walk (see the while loop above) but
		 * still held its pre-addblok() word value (0, from brk()'s
		 * zero-fill of the just-grown region). Not busy, so the walk
		 * chased it as an ordinary free link, chasing NULL from
		 * there - observed as alloc() looping forever, corrupting
		 * bloktop with whatever garbage that read back (confirmed
		 * with a hardware watchpoint on the underlying port: the
		 * watched word toggled from that legitimate 0 to garbage
		 * with alloc()'s own loop as the writer, mid-coalescing-walk,
		 * squarely inside this function's caller-visible window).
		 */
		register struct blk *newtop = (struct blk *)(Rcheat(bloktop) + reqd);

		dbg_hex("DBG addblok reqd-grow reqd=", reqd);
		dbg_hex("DBG addblok reqd-grow old bloktop=", (unsigned)bloktop);
		dbg_hex("DBG addblok reqd-grow newtop=", (unsigned)newtop);
		newtop->word = (struct blk *)(brkbegin + 1);
		blokp = bloktop;
		bloktop->word = newtop;
		bloktop = newtop;
		dbg_hex("DBG addblok reqd-grow newtop->word=", (unsigned)newtop->word);
	}
	{
		register char *stakadr = (char *)(bloktop + 2);

		if (stakbot != staktop)
			staktop = movstr(stakbot, stakadr);
		else
			staktop = stakadr;

		stakbas = stakbot = stakadr;
	}
}

void
free(ap)
	struct blk *ap;
{
	register struct blk *p;

	if ((p = ap) && p < bloktop)
	{
#ifdef DEBUG
		chkbptr(p);
#endif
		--p;
		p->word = (struct blk *)(Rcheat(p->word) & ~BUSY);
	}
}


#ifdef DEBUG

void
chkbptr(ptr)
	struct blk *ptr;
{
	int	exf = 0;
	register struct blk *p = (struct blk *)brkbegin;
	register struct blk *q;
	int	us = 0, un = 0;

	for (;;)
	{
		q = (struct blk *)(Rcheat(p->word) & ~BUSY);

		if (p+1 == ptr)
			exf++;

		if (q < (struct blk *)brkbegin || q > bloktop)
			abort(3);

		if (p == bloktop)
			break;

		if (busy(p))
			us += q - p;
		else
			un += q - p;

		if (p >= q)
			abort(4);

		p = q;
	}
	if (exf == 0)
		abort(1);
}

void
chkmem()
{
	register struct blk *p = (struct blk *)brkbegin;
	register struct blk *q;
	int	us = 0, un = 0;

	for (;;)
	{
		q = (struct blk *)(Rcheat(p->word) & ~BUSY);

		if (q < (struct blk *)brkbegin || q > bloktop)
			abort(3);

		if (p == bloktop)
			break;

		if (busy(p))
			us += q - p;
		else
			un += q - p;

		if (p >= q)
			abort(4);

		p = q;
	}

	prs("un/used/avail ");
	prn(un);
	blank();
	prn(us);
	blank();
	prn((char *)bloktop - brkbegin - (un + us));
	newline();

}
#endif
