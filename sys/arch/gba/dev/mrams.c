/*
 * Disk driver for serial MRAM chips connected via SPI port.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/dk.h>
#include <sys/disk.h>
#include <sys/config.h>

#include <machine/debug.h>

#include <gba/dev/mrams.h>

#define MRAM_WREN       0x06
#define MRAM_WRDI       0x04
#define MRAM_RDSR       0x05
#define MRAM_WRSR       0x01
#define MRAM_READ       0x03
#define MRAM_WRITE      0x02
#define MRAM_SLEEP      0xB9
#define MRAM_WAKE       0xAB

#ifndef MRAMS_MHZ
#define MRAMS_MHZ       13
#endif

extern char __fs_start[];
extern char __fs_end[];
extern char __swap_start[];
extern char __swap_end[];

int mrams_dkindex;                      /* disk index for statistics */

/*
 * Size of RAM disk.
 */
#define MRAMS_TOTAL_KBYTES  ((unsigned int)((__fs_end - __fs_start)>>DEV_BSHIFT))
#define MRAMS_SWAP_KBYTES  ((unsigned int)((__swap_end - __swap_start)>>DEV_BSHIFT))

#define MRBSIZE         1024
#define MRBLOG2         10

unsigned int mr_read_block(unsigned int chip, unsigned int address, unsigned int length, char *data)
{
    register unsigned int cs = 0;

    return cs;
}

int mrams_read(size_t phys_addr, char *data, unsigned int bcount)
{
    // offset (block number) to byte address
    size_t src_addr = phys_addr;

    // copy ROM or EWRAM to RAM(data)
    bcopy((void *)src_addr, data, bcount);
    return 1;
}

unsigned int mr_write_block(unsigned int chip, unsigned int address, unsigned int length, char *data)
{
    register unsigned int cs = 0;

    return cs;
}

int mrams_write(size_t phys_addr, char *data, unsigned bcount)
{
    // offset (block number) to byte address
    size_t dst_addr = phys_addr;

    //volatile unsigned short dummy;

    if (dst_addr >= (size_t)__swap_start && dst_addr <= (size_t)__swap_end) {
        // copy only swap device.
        //bcopy((void *)data, (void *)dst_addr, bcount);
        volatile unsigned short *dst = (volatile unsigned short *)dst_addr;
        unsigned short *src = (unsigned short *)data;
        size_t i;
        for (i=0; i<bcount/2;i++) {
            dst[i] = src[i];
        }
        //dummy = dst[i-1]; (void)dummy;
    }

    //dummy = *(volatile unsigned short *)0x02000000; (void)dummy;
    return 1;
}

/*
 * Initialize hardware.
 */
static int mrams_init(void)
{
    return 1;
}

/*
 * Open the disk.
 */
int mrams_open(dev_t dev, int flag, int mode)
{
    return 0;
}

int mrams_close(dev_t dev, int flag, int mode)
{
    return 0;
}

/*
 * Return the size of the device in kbytes.
 */
daddr_t mrams_size(dev_t dev)
{
    daddr_t ret = 0;
    switch (minor(dev)) {
    case 0:
        /* Whole disk. */
        ret = MRAMS_TOTAL_KBYTES + MRAMS_SWAP_KBYTES;
    case 1:
        /* Partition A: filesystem. */
        ret = MRAMS_TOTAL_KBYTES;
    case 2:
        /* Partition B: swap space. */
        ret = MRAMS_SWAP_KBYTES;
    }
    return ret;
}

void mrams_strategy(struct buf *bp)
{
    int offset = bp->b_blkno;
    long nblk = btod(bp->b_bcount);
    int s;
    int unit = minor(bp->b_dev); // minor device number

    /*
     * Determine the size of the transfer, and make sure it is
     * within the boundaries of the partition.
     */
    if (bp->b_blkno + nblk > MRAMS_TOTAL_KBYTES) {
        /* if exactly at end of partition, return an EOF */
        if (bp->b_blkno == MRAMS_TOTAL_KBYTES) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
            return;
        }
        /* or truncate if part of it fits */
        nblk = MRAMS_TOTAL_KBYTES - bp->b_blkno;
        if (nblk <= 0) {
            bp->b_error = EINVAL;
            bp->b_flags |= B_ERROR;
            biodone(bp);
            return;
        }
        bp->b_bcount = nblk << DEV_BSHIFT;
    }

    s = splbio();
#ifdef UCB_METER
    if (mrams_dkindex >= 0) {
        dk_busy |= 1 << mrams_dkindex;
        dk_xfer[mrams_dkindex]++;
        dk_bytes[mrams_dkindex] += bp->b_bcount;
    }
#endif

    // Since each device minor number uses a different base address, 
    // calculate the offset.
    size_t phys_addr;
    if (unit == 2) {
        phys_addr = (offset<<DEV_BSHIFT) + (size_t)__swap_start;
    } else {
        phys_addr = (offset<<DEV_BSHIFT) + (size_t)__fs_start;
    }
    if (bp->b_flags & B_READ) {
        mrams_read(phys_addr, bp->b_addr, bp->b_bcount);
    } else {
        if (unit == 1) {
            bp->b_error = EROFS; // read-only file system
            bp->b_flags |= B_ERROR;
        } else {
            mrams_write(phys_addr, bp->b_addr, bp->b_bcount);
        }
    }

    biodone(bp);
#ifdef UCB_METER
    if (mrams_dkindex >= 0)
        dk_busy &= ~(1 << mrams_dkindex);
#endif
    splx(s);
}

int mrams_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    int error = 0;

    switch (cmd) {

    case DIOCGETMEDIASIZE:
        /* Get disk size in kbytes. */
        *(int*) addr = MRAMS_TOTAL_KBYTES;
        break;

    default:
        error = EINVAL;
        break;
    }
    return error;
}

/*
 * Test to see if device is present.
 * Return true if found and initialized ok.
 */
static int
mrams_probe(config)
    struct conf_device *config;
{

    /* Only one device unit is supported. */
    if (config->dev_unit != 0)
        return 0;

//    printf("mr0:\n");

    if (mrams_init() != 0)
        return 0;

    return 1;
}

struct driver mrdriver = {
    "mr", mrams_probe,
};
