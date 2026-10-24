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
				 * A block's word field is a link only within the
				 * arena, [brkbegin, bloktop].  Should a chain ever
				 * point outside it, following it would corrupt
				 * memory or loop forever: grow the arena instead.
				 */
				while (!busy(q = p->word))
				{
					if (q < (struct blk *)brkbegin || q > bloktop)
						goto grow;
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
			if (p < (struct blk *)brkbegin || p > bloktop)
				goto grow;
		} while (p > q || (c++) == 0);
	grow:
		addblok(rbytes);
	}
}

void
addblok(reqd)
	unsigned int reqd;
{
	if (stakbot == NIL)
	{
                extern int end;
		brkbegin = setbrk(BRKINCR * 5);
		bloktop = (struct blk *) &end;
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

		/*
		 * Terminate the new top block before linking it in, as
		 * below: its word field still holds old stack bytes, and
		 * alloc()'s walk could otherwise follow them as a link.
		 */
		newtop = (struct blk *)(rndstak);
		newtop->word = (struct blk *)(brkbegin + 1);
		bloktop->word = (struct blk *)(Rcheat(rndstak) | BUSY);
		bloktop = newtop;
	}
	reqd += brkincr;
	reqd &= ~(brkincr - 1);
	{
		/*
		 * Terminate the new top block before linking it in: until
		 * then its word field is whatever the grown memory held
		 * (0), which alloc()'s walk would follow as a free link.
		 */
		register struct blk *newtop = (struct blk *)(Rcheat(bloktop) + reqd);

		newtop->word = (struct blk *)(brkbegin + 1);
		blokp = bloktop;
		bloktop->word = newtop;
		bloktop = newtop;
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
