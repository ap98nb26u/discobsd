#include <sys/param.h>
#include <sys/conf.h>

dev_t	rootdev = makedev(3, 1);	/* mr0a */
dev_t	dumpdev = makedev(3, 2);	/* mr0b */
dev_t	swapdev = makedev(3, 2);	/* mr0b */
