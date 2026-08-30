/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

char 	*sbrk();

char *
setbrk(incr)
int	incr;
{
	register char *a = sbrk(incr);

	/*
	 * sbrk() used to always return the old break here even when the
	 * underlying brk() call failed (ENOMEM), so this stamped brkend
	 * with a boundary the kernel never actually granted - callers
	 * checking "setbrk(...) == -1" (fault.c, stak.c) never saw the
	 * failure, and later code trusted a brkend past the real, usable
	 * break. Only update brkend when the extension actually happened.
	 */
	if (a != (char *)-1)
		brkend = a + incr;
	return(a);
}
