/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <unistd.h>

extern char _end[];
const char *_curbrk = _end;

void *
sbrk (incr)
	int incr;
{
	void *oldbrk = (void*) _curbrk;

	if (incr != 0) {
		/* calculate and pass break address */
		const void *addr = _curbrk + incr;
		if (_brk (addr) == -1) {
			/*
			 * _brk() failed (errno already set by the SYS()
			 * wrapper) - report failure instead of silently
			 * returning oldbrk as if the extension succeeded.
			 * A caller checking for sbrk()'s POSIX-mandated
			 * (void*)-1 failure return (e.g. bin/sh's setbrk(),
			 * which stamped "brkend = a + incr" on whatever this
			 * returned with no other failure signal available)
			 * used to always take the success path even when
			 * the process's actual break never moved, corrupting
			 * its own idea of how much memory was really usable.
			 */
			return (void *)-1;
		}
		/* add increment to curbrk */
		_curbrk = addr;
	}
	/* return old break address */
	return oldbrk;
}

void *
brk (addr)
	const void *addr;
{
	int ret;

	if (addr < (void*) _end)	/* break request too low? */
		addr = _end;		/* yes, knock the request up to _end */
	ret = _brk (addr);		/* ask for break */
	if (ret != -1)
		_curbrk = addr;		/* and remember it if it succeeded */
	return (void*) ret;
}
