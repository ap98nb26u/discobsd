/*
 * md - memory disk.
 *
 * Serves two regions of the machine's address space, laid out by the
 * port's linker script, as one disk:
 *
 *      minor 0  md0    the root region (same as md0a)
 *      minor 1  md0a   root filesystem image  __fs_start .. __fs_end
 *      minor 2  md0b   swap area              __swap_start .. __swap_end
 *
 * The root region is normally a filesystem image linked into the kernel
 * (in ROM or flash), so a board without an SD card can still boot:
 * "config unix root on md0a swap on md0b".  It is read-only unless the
 * kernel is built with options MD_ROOT_RW (for a root that is really in
 * RAM).  The swap area is RAM.  Either region may be empty (e.g. GBAED:
 * root on SD, swap only).
 *
 * Transfers are plain copies.  A port that needs more - bus quirks, a
 * device to pause around the copy - builds with options MD_MACHDEP and
 * supplies md_machdep_begin(), md_machdep_end() and md_machdep_write()
 * (see <sys/md.h>).
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <sys/conf.h>
#include <sys/config.h>
#include <sys/md.h>

extern char __fs_start[], __fs_end[];
extern char __swap_start[], __swap_end[];

#define MD_ROOT_KB      ((daddr_t)((__fs_end - __fs_start) >> DEV_BSHIFT))
#define MD_SWAP_KB      ((daddr_t)((__swap_end - __swap_start) >> DEV_BSHIFT))

#ifndef MD_MACHDEP
#define md_machdep_begin()              /* nothing */
#define md_machdep_end()                /* nothing */
#define md_machdep_write(dst, src, len) bcopy((src), (dst), (len))
#endif

static int
md_isswap(dev_t dev)
{
    return minor(dev) == 2;
}

int
mdopen(dev_t dev, int flag, int mode)
{
    if (minor(dev) > 2)
        return ENXIO;
    return 0;
}

int
mdclose(dev_t dev, int flag, int mode)
{
    return 0;
}

/*
 * Size of a partition, in kbytes.
 */
daddr_t
mdsize(dev_t dev)
{
    switch (minor(dev)) {
    case 0:
    case 1:
        return MD_ROOT_KB;
    case 2:
        return MD_SWAP_KB;
    }
    return 0;
}

/*
 * True if dev is the read-only root region: kern.romroot (kern_sysctl.c)
 * tells /etc/rc to skip fsck and the root remount for such a root.
 */
int
md_rootrom(dev_t dev)
{
#ifdef MD_ROOT_RW
    return 0;
#else
    return major(dev) == MD_MAJOR && !md_isswap(dev);
#endif
}

void
mdstrategy(struct buf *bp)
{
    daddr_t psize = mdsize(bp->b_dev);
    long nblk = btod(bp->b_bcount);
    caddr_t base, addr;
    int s;

    /* Keep the transfer within this partition. */
    if (minor(bp->b_dev) > 2 || bp->b_blkno < 0) {
        bp->b_error = ENXIO;
        goto bad;
    }
    if (bp->b_blkno + nblk > psize) {
        if (bp->b_blkno == psize) {
            /* exactly at the end: EOF */
            bp->b_resid = bp->b_bcount;
            biodone(bp);
            return;
        }
        nblk = psize - bp->b_blkno;
        if (nblk <= 0) {
            bp->b_error = EINVAL;
            goto bad;
        }
        bp->b_bcount = nblk << DEV_BSHIFT;
    }

    base = md_isswap(bp->b_dev) ? __swap_start : __fs_start;
    addr = base + (bp->b_blkno << DEV_BSHIFT);

#ifndef MD_ROOT_RW
    if (!(bp->b_flags & B_READ) && !md_isswap(bp->b_dev)) {
        bp->b_error = EROFS;
        goto bad;
    }
#endif

    s = splbio();
    md_machdep_begin();
    if (bp->b_flags & B_READ)
        bcopy(addr, bp->b_addr, bp->b_bcount);
    else
        md_machdep_write(addr, bp->b_addr, bp->b_bcount);
    md_machdep_end();
    biodone(bp);
    splx(s);
    return;

bad:
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

int
mdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        /* Size of this partition, in kbytes. */
        *(int *)addr = mdsize(dev);
        return 0;
    }
    return EINVAL;
}

static int
mdprobe(struct conf_device *config)
{
    /* One unit, laid out by the linker script. */
    if (config->dev_unit != 0)
        return 0;
    printf("md0: root %u kbytes%s, swap %u kbytes\n",
        (unsigned)MD_ROOT_KB,
#ifdef MD_ROOT_RW
        "",
#else
        MD_ROOT_KB ? " read-only" : "",
#endif
        (unsigned)MD_SWAP_KB);
    return 1;
}

struct driver mddriver = {
    "md", mdprobe,
};
