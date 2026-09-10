/*
 * EverDrive GBA X5 expansion-unit controller.
 *
 * The EverDrive is not a simple SD interface: it's a cartridge-resident
 * FPGA exposing SD card, RTC, flash/EEPROM save and ROM-bank switching,
 * all through one bank of memory-mapped 16-bit registers in the
 * cartridge address space. Child devices (sd, rtc, ...) attach to this
 * controller and share the register access and lock/unlock sequence
 * below, ported from EverDrive's own everdrive.c sample source.
 */
#include <sys/param.h>
#include <sys/config.h>
#include <sys/systm.h>

#include <gba/dev/ed.h>

#define ED_REG_BASE     0x09FC0000

#define ED_REG_CFG      0x00
#define ED_REG_STATUS   0x01
#define ED_REG_FPGA_VER 0x05
#define ED_REG_SD_CMD   0x08
#define ED_REG_SD_DAT   0x09
#define ED_REG_SD_CFG   0x0A
#define ED_REG_SD_RAM   0x0B
#define ED_REG_KEY      0x5A

#define ED_CFG_REGS_ON      1
#define ED_CFG_NROM_RAM     2
#define ED_CFG_ROM_WE_ON    4
#define ED_CFG_AUTO_WE      8
#define ED_CFG_RTC_ON       0x200

#define ED_STAT_SD_BUSY     1
#define ED_STAT_SDC_TOUT    2

#define ED_SD_WAIT_F0       8
#define ED_SD_STRT_F0       16
#define ED_SD_MODE_BITS     30
#define ED_SD_SPD_BITS      1

/* GBA DMA channel 3: general-purpose memory-to-memory transfer. */
#define DMA_SRC (*(volatile uint32_t *)0x040000D4)
#define DMA_DST (*(volatile uint32_t *)0x040000D8)
#define DMA_LEN (*(volatile uint16_t *)0x040000DC)
#define DMA_CTR (*(volatile uint16_t *)0x040000DE)

/*
 * Wait for channel 3's DMA_CTR busy bit to clear, bounded. Every
 * "while (DMA_CTR & 0x8000) ;" in this file used to be unbounded like
 * ed_sd_wait_f0()'s inner loop was (see the long comment there) - the
 * same class of real-hardware hang is possible here too, since three
 * of the four call sites DMA directly from the SD data register
 * (ED_REG_SD_DAT), so a stuck/slow SD response can stall the DMA
 * itself, not just plain memory-to-memory copies. Returns 0 on
 * success, 1 on timeout.
 */
static int
ed_dma_wait(void)
{
    volatile uint32_t i;

    for (i = 0; i < 50000; i++) {
        if ((DMA_CTR & 0x8000) == 0)
            return 0;
    }
    return 1;
}

static uint16_t ed_cart_cfg;
static uint16_t ed_sd_cfg;

uint16_t
ed_reg_rd(uint16_t reg)
{
    return *((volatile uint16_t *)(ED_REG_BASE + reg * 2));
}

void
ed_reg_wr(uint16_t reg, uint16_t data)
{
    *((volatile uint16_t *)(ED_REG_BASE + reg * 2)) = data;
}

void
ed_unlock_regs(void)
{
    ed_reg_wr(ED_REG_KEY, 0xA5);
    ed_cart_cfg |= (ED_CFG_REGS_ON | ED_CFG_ROM_WE_ON);
    ed_reg_wr(ED_REG_CFG, ed_cart_cfg);
}

void
ed_lock_regs(void)
{
    ed_cart_cfg &= ~(ED_CFG_REGS_ON | ED_CFG_ROM_WE_ON);
    ed_reg_wr(ED_REG_CFG, ed_cart_cfg);
}

void
ed_rtc_on(void)
{
    ed_cart_cfg |= ED_CFG_RTC_ON;
    ed_reg_wr(ED_REG_CFG, ed_cart_cfg);
}

void
ed_rtc_off(void)
{
    ed_cart_cfg &= ~ED_CFG_RTC_ON;
    ed_reg_wr(ED_REG_CFG, ed_cart_cfg);
}

uint16_t
ed_get_fpga_ver(void)
{
    return ed_reg_rd(ED_REG_FPGA_VER);
}

void
ed_sd_mode(uint16_t mode)
{
    ed_sd_cfg &= ~ED_SD_MODE_BITS;
    ed_sd_cfg |= mode & ED_SD_MODE_BITS;
    ed_reg_wr(ED_REG_SD_CFG, ed_sd_cfg);
}

void
ed_sd_speed(uint16_t speed)
{
    ed_sd_cfg &= ~ED_SD_SPD_BITS;
    ed_sd_cfg |= speed & ED_SD_SPD_BITS;
    ed_reg_wr(ED_REG_SD_CFG, ed_sd_cfg);
}

/*
 * Same fix as ed_sd_dat_wr()/ed_sd_dat_rd() below - these are the
 * command-line equivalents (sd_cmd()'s per-byte command/response
 * transfer in sd.c), found by re-auditing this file for any
 * remaining unbounded ED_STAT_SD_BUSY waits after the data-line pair
 * turned out not to be the only ones left.
 */
void
ed_sd_cmd_wr(uint8_t data)
{
    volatile uint32_t i;

    ed_reg_wr(ED_REG_SD_CMD, data);
    for (i = 0; i < 50000; i++) {
        if ((ed_reg_rd(ED_REG_STATUS) & ED_STAT_SD_BUSY) == 0)
            break;
    }
}

uint8_t
ed_sd_cmd_rd(void)
{
    uint8_t dat = (uint8_t)ed_reg_rd(ED_REG_SD_CMD);
    volatile uint32_t i;

    for (i = 0; i < 50000; i++) {
        if ((ed_reg_rd(ED_REG_STATUS) & ED_STAT_SD_BUSY) == 0)
            break;
    }
    return dat;
}

uint8_t
ed_sd_cmd_val(void)
{
    return (uint8_t)ed_reg_rd(ED_REG_SD_CMD + 2);
}

/*
 * These two are called once per byte during multi-block writes
 * (sd_write_sectors()'s CRC/token handshake in sd.c) - by far the
 * hottest and, until now, the only remaining unbounded
 * ED_STAT_SD_BUSY waits left in this file (see ed_sd_wait_f0()'s and
 * ed_dma_wait()'s comments for the earlier fixes in this same class
 * of bug). Confirmed by real-hardware testing: fsck -p's first
 * preen-mode repair write hung forever here, right where the earlier
 * fixes (ed_sd_wait_f0(), ed_dma_wait()) didn't reach - this is the
 * first code path in this whole port to exercise a real multi-byte
 * SD write handshake under load. Neither function's signature has a
 * failure return (ed_sd_dat_rd() returns the byte read, not a status
 * code, and threading an error path through every caller in
 * sd_write_sectors() is a bigger change than this needs) - just
 * bound the wait so a stuck card times out instead of freezing the
 * kernel forever; the caller ends up using possibly-stale status
 * bits on timeout, but that's a far smaller problem than a total
 * hang.
 */
void
ed_sd_dat_wr(uint8_t data)
{
    volatile uint32_t i;

    ed_reg_wr(ED_REG_SD_DAT, 0xff00 | data);
    for (i = 0; i < 50000; i++) {
        if ((ed_reg_rd(ED_REG_STATUS) & ED_STAT_SD_BUSY) == 0)
            break;
    }
}

/*
 * Set by ed_sd_dat_rd() on every call: nonzero if that call's inner
 * ED_STAT_SD_BUSY wait ran all the way to its 50000-iteration cap
 * without the FPGA/card ever releasing BUSY (a "wedge"), zero if BUSY
 * cleared normally. sd_write_sectors()'s per-block ready wait reads it
 * to tell a wedged card (bail at once, so a large outer bound can't
 * multiply the inner cap into a multi-hour hang) from one that is
 * merely slow to finish programming (keep polling) - see the comment
 * there.
 */
int ed_sd_dat_rd_wedged;

uint8_t
ed_sd_dat_rd(void)
{
    uint8_t dat = (uint8_t)(ed_reg_rd(ED_REG_SD_DAT) >> 8);
    volatile uint32_t i;

    for (i = 0; i < 50000; i++) {
        if ((ed_reg_rd(ED_REG_STATUS) & ED_STAT_SD_BUSY) == 0)
            break;
    }
    ed_sd_dat_rd_wedged = (i == 50000);
    return dat;
}

/*
 * Wait for the SD controller's data FIFO to be ready for a transfer.
 * Returns 0 on success, 1 on timeout.
 */
static uint8_t
ed_sd_wait_f0(void)
{
    uint8_t resp;
    uint16_t i;
    uint8_t mode = ED_SD_MODE4 | ED_SD_WAIT_F0 | ED_SD_STRT_F0;

    /*
     * This outer loop retries up to "i" times, each retry doing its
     * own bounded inner busy-wait below - the two bounds multiply.
     * At 65000 outer retries, a case where the inner wait genuinely
     * times out every single time (not the common case, but exactly
     * what happens when the card really isn't responding) turned
     * into a real-hardware hang of well over 6 hours before this
     * fix - not a "spurious timeout from too little patience" as
     * first assumed, but this exact multiplication making a bounded
     * inner loop practically unbounded in aggregate. Cut sharply;
     * this many attempts at "change mode and retry" was never really
     * the point of the outer loop (that's a small, fixed number of
     * legitimate retry strategies) - it was only ever this large
     * because the inner wait used to be unbounded, so overall
     * patience had to come from somewhere.
     */
    for (i = 0; i < 50; i++) {
        uint32_t busywait;

        ed_sd_mode(mode);
        ed_reg_rd(ED_REG_SD_DAT);
        /*
         * ED_STAT_SD_BUSY can get stuck set (observed on real GBAED
         * hardware under fsck's scattered read/write access pattern,
         * never hit by this port's earlier, mostly-sequential disk
         * I/O) - this used to be an unbounded "for (;;)", hanging the
         * whole kernel forever with interrupts masked. Bound it and
         * report a timeout like every other wait in this file does,
         * rather than spin forever. (A post-execve crash right after
         * this fix was first added turned out to be caused by
         * booting unix.bin directly as \GBASYS\GBAOS.gba on the
         * EverDrive rather than through its stock menu - unrelated
         * to this loop; confirmed by reverting this exact change and
         * seeing the same crash. Restored as originally written.)
         */
        for (busywait = 0; busywait < 50000; busywait++) {
            resp = ed_reg_rd(ED_REG_STATUS);
            if ((resp & ED_STAT_SD_BUSY) == 0)
                break;
        }
        /*
         * Distinct return codes (2026-09-06) so the read path can
         * report *why* a fresh CMD18 stream failed to deliver data -
         * the two conditions need different recovery:
         *   1 = ED_STAT_SD_BUSY never cleared (controller wedged busy)
         *   2 = ED_STAT_SDC_TOUT kept asserting for the whole outer
         *       loop (the card itself is signalling a read/command
         *       timeout - e.g. it never accepted the CMD18, whose R1
         *       response sd_cmd() deliberately does not check).
         */
        if (busywait == 50000)
            return 1;
        if ((resp & ED_STAT_SDC_TOUT) == 0)
            return 0;
        mode = ED_SD_MODE4 | ED_SD_WAIT_F0;
    }
    return 2;
}

uint8_t
ed_sd_dma_wr(const void *src)
{
    ed_reg_wr(ED_REG_SD_RAM, 0);
    ed_sd_mode(ED_SD_MODE4);
    DMA_SRC = (uint32_t)src;
    DMA_DST = (uint32_t)(ED_REG_BASE + ED_REG_SD_DAT * 2);
    DMA_LEN = 256;
    DMA_CTR = 0x8040;
    if (ed_dma_wait())
        return 1;
    return 0;
}

void
ed_sd_read_crc_ram(void *dst)
{
    ed_reg_wr(ED_REG_SD_RAM, 0);
    DMA_SRC = (uint32_t)(ED_REG_BASE + ED_REG_SD_RAM * 2);
    DMA_DST = (uint32_t)dst;
    DMA_LEN = 256;
    DMA_CTR = 0x8100;
    /*
     * No error return here (void, pre-existing signature - see
     * sd_crc16()'s caller in sd.c, which has no failure path of its
     * own to propagate into either). A timeout just leaves whatever
     * partial/stale data was already in dst rather than hanging
     * forever - a wrong CRC on the next write is a far smaller
     * problem than freezing the kernel.
     */
    (void)ed_dma_wait();
}

/*
 * DMA into cartridge ROM space (flash-backed SRAM window), needed
 * because a normal 16-bit DMA can't write ROM directly.
 */
static uint8_t
ed_sd_dma_to_rom(void *dst, int slen)
{
    uint16_t buf[256];

    while (slen) {
        if (ed_sd_wait_f0() != 0)
            return 1;

        ed_reg_wr(ED_REG_CFG, ed_cart_cfg | ED_CFG_AUTO_WE);
        DMA_SRC = (uint32_t)dst;
        DMA_DST = (uint32_t)buf;
        DMA_LEN = 256;
        DMA_CTR = 0x8000;
        if (ed_dma_wait()) {
            ed_reg_wr(ED_REG_CFG, ed_cart_cfg);
            return 1;
        }
        ed_reg_wr(ED_REG_CFG, ed_cart_cfg);

        slen--;
        dst = (char *)dst + 512;
    }
    return 0;
}

uint8_t
ed_sd_dma_rd(void *dst, int slen)
{
    if (((uint32_t)dst & 0xE000000) == 0x8000000)
        return ed_sd_dma_to_rom(dst, slen);

    while (slen) {
        /*
         * Distinct return codes so the caller can report *which*
         * stage failed (2026-09-06):
         *   1 = ED_STAT_SD_BUSY stuck (controller wedged),
         *   2 = card SDC_TOUT (card timed out / never accepted CMD18),
         *   3 = the DMA transfer itself timed out.
         * These mean very different things for recovery.
         */
        uint8_t wf = ed_sd_wait_f0();
        if (wf != 0)
            return wf;             /* 1 = busy stuck, 2 = card timeout */

        DMA_SRC = (uint32_t)(ED_REG_BASE + ED_REG_SD_DAT * 2);
        DMA_DST = (uint32_t)dst;
        DMA_LEN = 256;
        DMA_CTR = 0x8000;
        if (ed_dma_wait())
            return 3;

        slen--;
        dst = (char *)dst + 512;
    }
    return 0;
}

static int
ed_probe(struct conf_ctlr *ctlr)
{
    ed_reg_wr(ED_REG_KEY, 0xA5);
    ed_cart_cfg = ED_CFG_REGS_ON | ED_CFG_NROM_RAM | ED_CFG_ROM_WE_ON;
    ed_reg_wr(ED_REG_CFG, ed_cart_cfg);

    printf("ed%d: EverDrive GBA (FPGA v%u)\n",
        ctlr->ctlr_unit, ed_get_fpga_ver());

    return (1);
}

struct driver eddriver = {
    "ed", ed_probe,
};
