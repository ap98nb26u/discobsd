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

#endif /* KERNEL */

#endif /* _GBA_DEV_ED_H_ */
