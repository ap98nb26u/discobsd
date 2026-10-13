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
#include <gba/dev/gba_audio.h>	/* audio_suspend/resume around PSRAM swap I/O */

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
 * GBAED PSRAM swap write-reliability diagnostics (see mrams_write). Counted
 * across all swap writes; mrams_wr_fails > 0 means a store never verified
 * even after retries (real corruption risk). Globals so they can be read
 * from a debugger / future sysctl.
 */
unsigned int mrams_wr_retries;
unsigned int mrams_wr_fails;

/* GBA REG_WAITCNT; bit 14 is the ROM prefetch-buffer enable. */
#define MR_REG_WAITCNT      (*(volatile unsigned short *)0x04000204)
#define MR_WAITCNT_PREFETCH 0x4000

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

    if (dst_addr >= (size_t)__swap_start && dst_addr < (size_t)__swap_end) {
        /*
         * GBAED swap lives in EverDrive PSRAM, written over the cartridge
         * bus. A 16-bit store can occasionally be dropped when the ROM
         * prefetch unit contends for that same bus while the kernel both
         * fetches its own code from ROM and stores into PSRAM; a lost store
         * would silently corrupt a swapped-out image (seen as "panic: setrun"
         * after a garbage u-area is swapped back in). So disable the prefetch
         * buffer for the copy and verify each halfword against the VALUE WE
         * JUST WROTE (latched in v), rewriting that one word until it sticks.
         *
         * Verifying against the latched v - not by re-reading src[i] - is
         * important: the swap source is live core that can change under a
         * long (IME-off) copy, so a src re-read can differ from what was
         * stored even though the store was correct (observed as a harmless
         * boot-time "got = want-1"). Comparing to v catches only real dropped
         * stores and is immune to that. ARM7TDMI has no cache, so the
         * read-back sees real PSRAM. On the plain GBA build swap is in EWRAM
         * (never the cart bus), so this is just a reliable store there.
         */
        volatile unsigned short *dst = (volatile unsigned short *)dst_addr;
        unsigned short *src = (unsigned short *)data;
        size_t n = bcount >> 1, i;
        unsigned short savewc = MR_REG_WAITCNT;

        MR_REG_WAITCNT = savewc & ~MR_WAITCNT_PREFETCH;
        for (i = 0; i < n; i++) {
            unsigned short v = src[i];
            int t;

            dst[i] = v;
            for (t = 0; dst[i] != v && t < 4; t++) {
                dst[i] = v;
                mrams_wr_retries++;     /* a store was dropped and redone */
            }
            if (dst[i] != v) {
                /* A cell that will not take the value even after retries:
                 * a genuine write fault, not the benign src-changed case.
                 * Surface it once; the counter keeps the running total. */
                if (mrams_wr_fails++ == 0)
                    printf("mrams: PSRAM swap store failed at %x\n",
                        (unsigned)(dst_addr + (i << 1)));
            }
        }
        MR_REG_WAITCNT = savewc;
    }

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
        break;
    case 1:
        /* Partition A: filesystem. */
        ret = MRAMS_TOTAL_KBYTES;
        break;
    case 2:
        /* Partition B: swap space. */
        ret = MRAMS_SWAP_KBYTES;
        break;
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
     * Bound the transfer against THIS partition's size, not always the
     * filesystem partition's. On GBAED the fs partition (__fs_start) is an
     * empty placeholder (fs is on the SD card) while the swap partition
     * (minor 2, __swap_start) is the real 8 MB PSRAM region - checking swap
     * against MRAMS_TOTAL_KBYTES there would truncate every swap transfer to
     * EOF. (On the plain GBA build fs is ~20 MB so the old check happened to
     * never truncate swap, masking this.)
     */
    daddr_t psize = (unit == 2) ? (daddr_t)MRAMS_SWAP_KBYTES
                                : (daddr_t)MRAMS_TOTAL_KBYTES;

    /*
     * Determine the size of the transfer, and make sure it is
     * within the boundaries of the partition.
     */
    if (bp->b_blkno + nblk > psize) {
        /* if exactly at end of partition, return an EOF */
        if (bp->b_blkno == psize) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
            return;
        }
        /* or truncate if part of it fits */
        nblk = psize - bp->b_blkno;
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
    /*
     * GBAED swap I/O is a long (up to ~250 KB) CPU copy over the cart bus
     * that runs with all interrupts disabled (splbio == IME off on this
     * Thumb build), so the Timer0 audio ISR cannot service the DirectSound
     * ring during it: a sound DMA left free-running would hit an unmanaged
     * ring wrap and screech. Idle the sample clock across the copy (same
     * trick ed.c uses around SD DMA) so playback cleanly pauses for the
     * copy instead, then resumes in place. No-op when nothing is playing.
     */
#ifdef GBAED
    audio_suspend();
#endif
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
#ifdef GBAED
    audio_resume();
#endif

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
