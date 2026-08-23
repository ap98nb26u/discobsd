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

void
ed_sd_cmd_wr(uint8_t data)
{
    ed_reg_wr(ED_REG_SD_CMD, data);
    while (ed_reg_rd(ED_REG_STATUS) & ED_STAT_SD_BUSY)
        ;
}

uint8_t
ed_sd_cmd_rd(void)
{
    uint8_t dat = (uint8_t)ed_reg_rd(ED_REG_SD_CMD);
    while (ed_reg_rd(ED_REG_STATUS) & ED_STAT_SD_BUSY)
        ;
    return dat;
}

uint8_t
ed_sd_cmd_val(void)
{
    return (uint8_t)ed_reg_rd(ED_REG_SD_CMD + 2);
}

void
ed_sd_dat_wr(uint8_t data)
{
    ed_reg_wr(ED_REG_SD_DAT, 0xff00 | data);
    while (ed_reg_rd(ED_REG_STATUS) & ED_STAT_SD_BUSY)
        ;
}

uint8_t
ed_sd_dat_rd(void)
{
    uint8_t dat = (uint8_t)(ed_reg_rd(ED_REG_SD_DAT) >> 8);
    while (ed_reg_rd(ED_REG_STATUS) & ED_STAT_SD_BUSY)
        ;
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

    for (i = 0; i < 65000; i++) {
        ed_sd_mode(mode);
        ed_reg_rd(ED_REG_SD_DAT);
        for (;;) {
            resp = ed_reg_rd(ED_REG_STATUS);
            if ((resp & ED_STAT_SD_BUSY) == 0)
                break;
        }
        if ((resp & ED_STAT_SDC_TOUT) == 0)
            return 0;
        mode = ED_SD_MODE4 | ED_SD_WAIT_F0;
    }
    return 1;
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
    while (DMA_CTR & 0x8000)
        ;
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
    while (DMA_CTR & 0x8000)
        ;
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
        while (DMA_CTR & 0x8000)
            ;
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
        if (ed_sd_wait_f0() != 0)
            return 1;

        DMA_SRC = (uint32_t)(ED_REG_BASE + ED_REG_SD_DAT * 2);
        DMA_DST = (uint32_t)dst;
        DMA_LEN = 256;
        DMA_CTR = 0x8000;
        while (DMA_CTR & 0x8000)
            ;

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

    return (1);
}

struct driver eddriver = {
    "ed", ed_probe,
};
