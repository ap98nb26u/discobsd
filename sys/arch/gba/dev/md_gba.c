/*
 * GBA hooks for the memory disk, dev/md.c (options MD_MACHDEP).
 *
 * The plain GBA build roots on a filesystem image in cartridge ROM
 * (md0a) and swaps to EWRAM (md0b); GBAED swaps to the EverDrive's PSRAM,
 * which sits in the cartridge address space as well.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/md.h>

#include <gba/dev/gba_audio.h>

/* GBA REG_WAITCNT; bit 14 is the ROM prefetch-buffer enable. */
#define MD_REG_WAITCNT          (*(volatile unsigned short *)0x04000204)
#define MD_WAITCNT_PREFETCH     0x4000

/* The cartridge bus: ROM, or EverDrive PSRAM in its place. */
#define MD_CART_START           0x08000000
#define MD_CART_END             0x0E000000

/*
 * PSRAM write-reliability counters (see md_machdep_write).  md_wr_fails
 * above 0 means a store never verified even after retries - real
 * corruption risk.  Globals, for a debugger.
 */
unsigned int md_wr_retries;
unsigned int md_wr_fails;

/*
 * A swap transfer is a long CPU copy with interrupts off (splbio), so the
 * Timer0 audio ISR cannot service the DirectSound ring meanwhile: a sound
 * DMA left running would hit an unmanaged ring wrap and screech.  Idle the
 * sample clock across the copy (as ed.c does around SD DMA), so playback
 * pauses and resumes in place.  No-op when nothing is playing.  Only GBAED
 * has transfers long enough to matter (up to ~250 KB to PSRAM).
 */
void
md_machdep_begin(void)
{
#ifdef GBAED
    audio_suspend();
#endif
}

void
md_machdep_end(void)
{
#ifdef GBAED
    audio_resume();
#endif
}

void
md_machdep_write(caddr_t dst, caddr_t src, u_int len)
{
    volatile unsigned short *d;
    unsigned short *s, savewc;
    u_int n, i;

    if ((u_int)dst < MD_CART_START || (u_int)dst >= MD_CART_END) {
        bcopy(src, dst, len);           /* EWRAM: an ordinary copy */
        return;
    }

    /*
     * Writing to EverDrive PSRAM over the cartridge bus.  A 16-bit store
     * can occasionally be dropped when the ROM prefetch unit contends for
     * the same bus while the kernel fetches its own code from ROM and
     * stores into PSRAM; a lost store silently corrupts a swapped-out
     * image (seen as "panic: setrun" after a garbage u-area came back).
     * So turn the prefetch buffer off for the copy and verify each
     * halfword against the VALUE JUST WRITTEN (v), rewriting it until it
     * sticks.  Verifying against v, not by re-reading src, matters: the
     * source is live core that can change under a long copy, so a re-read
     * can differ although the store was right.  The ARM7TDMI has no cache,
     * so the read-back sees real PSRAM.
     */
    d = (volatile unsigned short *)dst;
    s = (unsigned short *)src;
    n = len >> 1;
    savewc = MD_REG_WAITCNT;
    MD_REG_WAITCNT = savewc & ~MD_WAITCNT_PREFETCH;
    for (i = 0; i < n; i++) {
        unsigned short v = s[i];
        int t;

        d[i] = v;
        for (t = 0; d[i] != v && t < 4; t++) {
            d[i] = v;
            md_wr_retries++;            /* a store was dropped and redone */
        }
        if (d[i] != v && md_wr_fails++ == 0)
            printf("md: PSRAM store failed at %x\n", (u_int)&d[i]);
    }
    MD_REG_WAITCNT = savewc;
}

void
md_machdep_swap(caddr_t *startp, caddr_t *endp)
{
    /* keep the linker script's swap area */
}
