#ifndef _GBA_DEV_ED_H_
#define _GBA_DEV_ED_H_

#ifdef KERNEL

#include <sys/stdint.h>

/*
 * Low-level EverDrive GBA X5 cartridge register access, shared by
 * child devices (sd, rtc, ...) attached to the ed0 controller.
 */
uint16_t    ed_reg_rd(uint16_t reg);
void        ed_reg_wr(uint16_t reg, uint16_t data);
void        ed_lock_regs(void);
void        ed_unlock_regs(void);
void        ed_rtc_on(void);
void        ed_rtc_off(void);
uint16_t    ed_get_fpga_ver(void);

/*
 * SD card command/data port access (shared REG_SD_* registers).
 * Used by sd.c to implement the SD command protocol.
 */
#define ED_SD_MODE1     0
#define ED_SD_MODE2     2
#define ED_SD_MODE4     4
#define ED_SD_MODE8     6
#define ED_SD_SPD_LO    0
#define ED_SD_SPD_HI    1

void        ed_sd_mode(uint16_t mode);
void        ed_sd_speed(uint16_t speed);
void        ed_sd_cmd_wr(uint8_t data);
uint8_t     ed_sd_cmd_rd(void);
uint8_t     ed_sd_cmd_val(void);
void        ed_sd_dat_wr(uint8_t data);
uint8_t     ed_sd_dat_rd(void);
uint8_t     ed_sd_dma_rd(void *dst, int slen);
uint8_t     ed_sd_dma_wr(const void *src);
void        ed_sd_read_crc_ram(void *dst);

#endif /* KERNEL */

#endif /* _GBA_DEV_ED_H_ */
