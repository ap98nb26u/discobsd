/*
 * Function (non-macro) versions of the character-class tests, for callers
 * that reference them as functions - e.g. code that declares its own
 * prototypes or takes their address.  <ctype.h> normally supplies these as
 * macros over the _ctype_ table; the functions evaluate the same expression.
 */
#include <ctype.h>

#undef isalpha
#undef isalnum

int
isalpha(c)
	int c;
{
	return ((_ctype_ + 1)[c] & (_U | _L));
}

int
isalnum(c)
	int c;
{
	return ((_ctype_ + 1)[c] & (_U | _L | _N));
}
