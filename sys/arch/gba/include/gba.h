#ifndef _XXX_
#define _XXX_

typedef unsigned short uint16_t;

#define ARM_CODE	__attribute__((target("arm")))
#define THUMB_CODE	__attribute__((target("thumb")))

#define IWRAM_CODE	__attribute__((section(".iwram")))
#define EWRAM_CODE	__attribute__((section(".ewram")))

#define REG_BASE	0x04000000

#define REG_DISPCNT	(*(volatile uint16_t *)(REG_BASE+0x0000))
#define REG_DISPSTAT	(*(volatile uint16_t *)(REG_BASE+0x0004))
#define REG_VCOUNT	(*(volatile uint16_t *)(REG_BASE+0x0006))	/* 0..227 scanline */

#define REG_BGCNT	(*(volatile uint16_t *)(REG_BASE+0x0008))
#define REG_BG0CNT	REG_BGCNT[0]
#define REG_BG1CNT	REG_BGCNT[1]
#define REG_BG2CNT	REG_BGCNT[2]
#define REG_BG3CNT	REG_BGCNT[3]
#define REG_BG0HOFS	(*(volatile uint16_t *)(REG_BASE+0x0010))
#define REG_BG0VOFS	(*(volatile uint16_t *)(REG_BASE+0x0012))

#define REG_TM0CNT_L	(*(volatile uint16_t *)(REG_BASE+0x0100))
#define REG_TM0CNT_H	(*(volatile uint16_t *)(REG_BASE+0x0102))
#define REG_TM1CNT_L	(*(volatile uint16_t *)(REG_BASE+0x0104))
#define REG_TM1CNT_H	(*(volatile uint16_t *)(REG_BASE+0x0106))

/* Sound. PSG channel 1 (square with sweep) + the master controls; used by
 * gba_sound.c for the console beep. DirectSound (DMA) FIFOs are not here. */
#define REG_SOUND1CNT_L	(*(volatile uint16_t *)(REG_BASE+0x0060))	/* ch1 sweep */
#define REG_SOUND1CNT_H	(*(volatile uint16_t *)(REG_BASE+0x0062))	/* ch1 duty/len/env */
#define REG_SOUND1CNT_X	(*(volatile uint16_t *)(REG_BASE+0x0064))	/* ch1 freq/control */
#define REG_SOUNDCNT_L	(*(volatile uint16_t *)(REG_BASE+0x0080))	/* master vol/enables */
#define REG_SOUNDCNT_H	(*(volatile uint16_t *)(REG_BASE+0x0082))	/* PSG/DMA mix ratio */
#define REG_SOUNDCNT_X	(*(volatile uint16_t *)(REG_BASE+0x0084))	/* master sound enable */
#define REG_SOUNDBIAS	(*(volatile uint16_t *)(REG_BASE+0x0088))	/* output bias/rate */

/* Keypad input (active low: a 0 bit = pressed). See swkbd.c. */
#define REG_KEYINPUT	(*(volatile uint16_t *)(REG_BASE+0x0130))
#define KEY_A		0x0001
#define KEY_B		0x0002
#define KEY_SELECT	0x0004
#define KEY_START	0x0008
#define KEY_RIGHT	0x0010
#define KEY_LEFT	0x0020
#define KEY_UP		0x0040
#define KEY_DOWN	0x0080
#define KEY_R		0x0100
#define KEY_L		0x0200

#define REG_IE		(*(volatile uint16_t *)(REG_BASE+0x0200))
#define REG_IF		(*(volatile uint16_t *)(REG_BASE+0x0202))
#define REG_WAITCNT	(*(volatile uint16_t *)(REG_BASE+0x0204))	/* GamePak/SRAM waitstates */
#define REG_IME		(*(volatile uint16_t *)(REG_BASE+0x0208))

/* Cartridge backup SRAM: 32KB, 8-bit bus (byte access only). See sram.c. */
#define GBA_SRAM_BASE	0x0E000000
#define GBA_SRAM_SIZE	0x8000

#define MEM_PAL		0x05000000
#define BG_PALETTE	((volatile uint16_t *)MEM_PAL)
#define OBJ_PALETTE	((volatile uint16_t *)(MEM_PAL + 0x200))
#define REG_PALETTE  	BG_PALETTE

#define VRAM		((volatile uint16_t*)0x06000000)

#define IRQ_VBLANK	(1 << 0)
#define IRQ_HBLANK	(1 << 1)
#define IRQ_VCOUNT	(1 << 2)
#define IRQ_TIMER0	(1 << 3)
#define IRQ_KEYPAD	(1 << 12)



#endif /* _XXX_ */

