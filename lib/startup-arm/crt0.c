/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * C runtime startoff.  When an a.out is loaded by the kernel, the kernel
 * sets up the stack as follows:
 *
 *	--------------------------------- top of memory
 *	| (NULL)			| end of command-line args and env
 *	|-------------------------------|
 *	| 				|
 *	| environment strings		|
 *	| 				|
 *	|-------------------------------|
 *	| 				|
 *	| argument strings		|
 *	| 				|
 *	|-------------------------------|
 *	| envv[envc] (NULL)		| end of environment vector tag, a 0
 *	|-------------------------------|
 *	| envv[envc-1]			| pointer to last environment string
 *	|-------------------------------|
 *	| ...				|
 *	|-------------------------------|
 *	| envv[0]			| pointer to first environment string
 *	|-------------------------------|
 *	| argv[argc] (NULL)		| end of argument vector tag, a 0
 *	|-------------------------------|
 *	| argv[argc-1]			| pointer to last argument string
 *	|-------------------------------|
 *	| ...				|
 *	|-------------------------------|
 *	| argv[0]			| pointer to first argument string
 *	|-------------------------------|
 *	| &envv[0]			| pointer to array of env pointers
 *	|-------------------------------|
 *	| &argv[0]			| pointer to array of arg pointers
 *	|-------------------------------|
 * sp->	| argc				| number of command-line arguments
 *	---------------------------------
 *
 * Arguments are passed in registers $a1, $a2 and $a3.
 *
 * Crt0 simply moves the env to environ variable, calculates
 * the __progname and then calls main.
 */
extern int main (int, char **, char **);

char **environ;
const char *__progname = "";

void _start (int, char **, char **);

/* The entry function. */
void
_start (argc, argv, env)
	int argc;
	char **argv;
	char **env;
{
	/*
	 * Raw VRAM write, no libc/syscall dependency at all - if this
	 * doesn't show up, we never even reached this instruction (crt0
	 * entry itself is failing); if it shows but the printf() below
	 * never appears, the problem is specifically in stdio/write().
	 */
	{
		volatile unsigned short *vram = (volatile unsigned short *)0x06000000;
		int i;
		for (i = 0; i < 240*4; i++)
			vram[i] = 0x03E0; /* green, Mode 3 top rows */
	}

	printf("DBG: _start entered, argc=%d argv=%x env=%x\n",
	    argc, (unsigned)argv, (unsigned)env);
	environ = env;
	if (argc > 0 && argv[0] != 0) {
		const char *s;

		__progname = argv[0];
		for (s = __progname; *s != '\0'; s++)
			if (*s == '/')
				__progname = s + 1;
	}
	exit (main (argc, argv, environ));
}
