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

#define REG_BGCNT	(*(volatile uint16_t *)(REG_BASE+0x0008))
#define REG_BG0CNT	REG_BGCNT[0]
#define REG_BG1CNT	REG_BGCNT[1]
#define REG_BG2CNT	REG_BGCNT[2]
#define REG_BG3CNT	REG_BGCNT[3]
#define REG_BG0HOFS	(*(volatile uint16_t *)(REG_BASE+0x0010))
#define REG_BG0VOFS	(*(volatile uint16_t *)(REG_BASE+0x0012))

#define REG_TM0CNT_L	(*(volatile uint16_t *)(REG_BASE+0x0100))
#define REG_TM0CNT_H	(*(volatile uint16_t *)(REG_BASE+0x0102))

#define REG_IE		(*(volatile uint16_t *)(REG_BASE+0x0200))
#define REG_IF		(*(volatile uint16_t *)(REG_BASE+0x0202))
#define REG_IME		(*(volatile uint16_t *)(REG_BASE+0x0208))

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

