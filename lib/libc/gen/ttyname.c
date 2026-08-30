/*
 * ttyname(f): return "/dev/ttyXX" which the the name of the
 * tty belonging to file f.
 *  NULL if it is not a tty
 */
#include <sys/param.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

static	char	dev[]	= "/dev/";

char *
ttyname(f)
	int f;
{
	struct stat fsb;
	struct stat tsb;
	register struct direct *db;
	register DIR *df;
	static char rbuf[32];

	if (isatty(f)==0) {
		fprintf(stderr, "DBG: ttyname isatty(%d) failed\n", f);
		return 0;
	}
	if (fstat(f, &fsb) < 0) {
		fprintf(stderr, "DBG: ttyname fstat(%d) failed\n", f);
		return 0;
	}
	if ((fsb.st_mode&S_IFMT) != S_IFCHR) {
		fprintf(stderr, "DBG: ttyname fd=%d mode=%o not S_IFCHR\n", f, fsb.st_mode);
		return 0;
	}
	fprintf(stderr, "DBG: ttyname fd=%d st_ino=%u st_dev=%u\n", f,
	    (unsigned)fsb.st_ino, (unsigned)fsb.st_dev);
        df = opendir(dev);
	if (! df) {
		fprintf(stderr, "DBG: ttyname opendir(%s) failed\n", dev);
		return 0;
	}
	while ((db = readdir(df))) {
		fprintf(stderr, "DBG: ttyname scan '%s' d_ino=%u\n", db->d_name,
		    (unsigned)db->d_ino);
		if (db->d_ino != fsb.st_ino)
			continue;
		strcpy(rbuf, dev);
		strcat(rbuf, db->d_name);
		if (stat(rbuf, &tsb) < 0) {
			fprintf(stderr, "DBG: ttyname stat(%s) failed\n", rbuf);
			continue;
		}
		fprintf(stderr, "DBG: ttyname candidate %s tsb.dev=%u tsb.ino=%u\n",
		    rbuf, (unsigned)tsb.st_dev, (unsigned)tsb.st_ino);
		if (tsb.st_dev == fsb.st_dev && tsb.st_ino == fsb.st_ino) {
			closedir(df);
			return(rbuf);
		}
	}
	closedir(df);
	fprintf(stderr, "DBG: ttyname no match found for fd=%d\n", f);
	return 0;
}
