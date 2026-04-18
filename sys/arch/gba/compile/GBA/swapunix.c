#include <sys/param.h>
#include <sys/conf.h>

dev_t	rootdev = makedev(0, 1);	/* sd0a */
dev_t	dumpdev = makedev(0, 2);	/* sd0b */
dev_t	swapdev = makedev(0, 2);	/* sd0b */
