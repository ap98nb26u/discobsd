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
#define ED_CFG_RTC_ON       0x200

static uint16_t ed_cart_cfg;

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
