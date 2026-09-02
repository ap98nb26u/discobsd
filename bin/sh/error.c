/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

/* ========     error handling  ======== */

/*
 * DBG: dump s1's raw bytes as hex, so a "notid" failure shows exactly
 * what byte(s) sh actually saw instead of a possibly-unprintable/
 * mojibake character - added while chasing a "$?  is not an
 * identifier" boot failure right after fsck on real GBAED hardware,
 * to find out whether the byte in place of the expected '?' is
 * genuine binary garbage (pointing at a read/DMA corruption bug) or
 * something else. Remove once that's root-caused.
 */
static void
dbg_hexdump(char *s)
{
	static char hexdig[] = "0123456789abcdef";
	char hexbuf[3];

	prs("[DBG bytes:");
	hexbuf[2] = '\0';
	while (*s)
	{
		unsigned char c = (unsigned char)*s++;
		hexbuf[0] = hexdig[(c >> 4) & 0xf];
		hexbuf[1] = hexdig[c & 0xf];
		prs(" ");
		prs(hexbuf);
	}
	prs("]");
}

failed(s1, s2)
char    *s1, *s2;
{
	prp();
	prs_cntl(s1);
	if (s2)
	{
		prs(colon);
		prs(s2);
		if (s2 == notid)
			dbg_hexdump(s1);
	}
	newline();
	exitsh(ERROR);
}

error(s)
char    *s;
{
	failed(s, NIL);
}

exitsh(xno)
int     xno;
{
	/*
	 * Arrive here from `FATAL' errors
	 *  a) exit command,
	 *  b) default trap,
	 *  c) fault with no trap set.
	 *
	 * Action is to return to command level or exit.
	 */
	exitval = xno;
	flags |= eflag;
	if ((flags & (forked | errflg | ttyflg)) != ttyflg)
		done();
	else
	{
		clearup();
		restore(0);
		clear_buff();
		execbrk = breakcnt = funcnt = 0;
		longjmp(errshell, 1);
	}
}

void
done()
{
	register char   *t;

	if (t = trapcom[0])
	{
		trapcom[0] = NIL;
		execexp(t, 0);
		free(t);
	}
	else
		chktrap();

	rmtemp(NIL);
	rmfunctmp();

#ifdef ACCOUNT
	doacct();
#endif
	exit(exitval);
}

rmtemp(base)
struct ionod    *base;
{
	while (iotemp > base)
	{
		unlink(iotemp->ioname);
		free(iotemp->iolink);
		iotemp = iotemp->iolst;
	}
}

rmfunctmp()
{
	while (fiotemp)
	{
		unlink(fiotemp->ioname);
		fiotemp = fiotemp->iolst;
	}
}
