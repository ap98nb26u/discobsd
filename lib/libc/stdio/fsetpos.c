/*
 * Restore a file position saved by fgetpos() (ANSI fsetpos).
 */
#include <stdio.h>

int
fsetpos(iop, pos)
	FILE *iop;
	long *pos;
{
	return (fseek(iop, *pos, 0));		/* 0 = SEEK_SET */
}
