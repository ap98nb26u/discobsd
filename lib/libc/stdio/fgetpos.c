/*
 * Save the current file position (ANSI fgetpos).
 */
#include <stdio.h>

int
fgetpos(iop, pos)
	FILE *iop;
	long *pos;
{
	long o;

	if ((o = ftell(iop)) < 0)
		return (-1);
	*pos = o;
	return (0);
}
