/*
 * Returns 1 iff file is a tty
 */
#include <sgtty.h>
#include <stdio.h>
#include <errno.h>

int
isatty(f)
        int f;
{
	struct sgttyb ttyb;
	int rc;

	rc = ioctl(f, TIOCGETP, &ttyb);
	if (rc < 0) {
		fprintf(stderr, "DBG: isatty(%d) TIOCGETP rc=%d errno=%d\n",
		    f, rc, errno);
		return(0);
	}
	return(1);
}
