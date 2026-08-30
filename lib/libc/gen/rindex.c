/*
 * Return the ptr in sp at which the character c last
 * appears; NULL if not found
 */

#define NULL 0

char *
rindex(sp, c)
	register const char *sp;
	register int c;
{
	register char *r;

	/*
	 * No MMU on this port - dereferencing a NULL sp below doesn't
	 * fault, it silently reads whatever sits at address 0 (BIOS ROM
	 * on GBA) and keeps walking through open-bus memory looking for
	 * a NUL byte that may never come. Confirmed via getty's `%t`
	 * (ttyname(0) can legitimately return NULL) feeding NULL in here
	 * and printing garbage indefinitely instead of erroring out.
	 */
	if (sp == NULL)
		return (NULL);

	r = NULL;
	do {
		if (*sp == (char)c)
			r = (char *)sp;
	} while (*sp++);
	return(r);
}
