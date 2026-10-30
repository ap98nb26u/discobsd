/*
 * sigrestart - three checks of the syscall return path and signals.
 *
 * 1. A read() that a caught signal interrupts is restarted: SIGALRM
 *    arrives while read() waits on an empty pipe, the handler writes one
 *    byte into the pipe, and the restarted read() returns it.
 * 2. A syscall number past the end of the table gets SIGSYS.
 * 3. A signal handler runs on an 8-byte aligned stack: a double formatted
 *    inside the handler comes out right (va_arg(double) needs it).
 *
 *   sigrestart
 *
 * Expected output:
 *   restart: ok (read=1 c=x)
 *   bad syscall: ok (SIGSYS)
 *   handler double: ok (1.500000)
 */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

static int pfd[2];
static volatile double dval = 1.5;
static char dbuf[32];

static void
onalrm(int sig)
{
	write(pfd[1], "x", 1);
	sprintf(dbuf, "%f", dval);	/* stdio output is left to main() */
}

/* A syscall with number 250, called the way libc calls any syscall. */
__attribute__((target("arm"), naked)) static int
badsys(void)
{
	__asm__ volatile (
	    "push {lr}\n\t"
	    "ldr r12, =0x03007E00\n\t"
	    "ldr r12, [r12]\n\t"
	    "mov lr, pc\n\t"
	    "bx r12\n\t"
	    ".word 250\n\t"
	    "pop {lr}\n\t"
	    "bx lr\n\t"
	    ".pool\n\t");
}

int
main(void)
{
	char c = '?';
	int n, status, pid;

	pipe(pfd);
	signal(SIGALRM, onalrm);
	alarm(1);
	errno = 0;
	n = read(pfd[0], &c, 1);
	if (n == 1 && c == 'x')
		printf("restart: ok (read=%d c=%c)\n", n, c);
	else
		printf("restart: FAILED (read=%d errno=%d c=%c)\n", n, errno, c);

	fflush(stdout);
	if ((pid = fork()) == 0) {
		badsys();
		_exit(0);
	}
	waitpid(pid, &status, 0);
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSYS)
		printf("bad syscall: ok (SIGSYS)\n");
	else
		printf("bad syscall: FAILED (status=%d)\n", status);

	if (strcmp(dbuf, "1.500000") == 0)
		printf("handler double: ok (%s)\n", dbuf);
	else
		printf("handler double: FAILED (%s)\n", dbuf);
	return 0;
}
