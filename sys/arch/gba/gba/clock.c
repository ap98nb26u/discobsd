/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)clock.c	1.1 (2.10BSD Berkeley) 12/1/86
 */

#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>

#define REG_TM0D      (*(volatile unsigned short*)0x04000100)
#define REG_TM0CNT    (*(volatile unsigned short*)0x04000102)
#define REG_IE        (*(volatile unsigned short*)0x04000200)
#define REG_IME       (*(volatile unsigned short*)0x04000208)

void clkstart(void) {
}

void cpu_initclocks(void) {
    // Timer 0 の設定
    // HZ=10の設定 (16.78MHz / 1024 / 100 ≒  164)
    // 65536 - 164 = 65372
    REG_TM0D = 65536 - (16777216 / 1024 / HZ); 
    
    // 0x0040: 割り込み有効
    // 0x0003: 1024分周
    // 0x0080: 開始
    REG_TM0CNT = 0x00C3;

    // GBA全体の割り込み許可レジスタ
    REG_IE |= 0x0008;
    REG_IME = 1;
}

#if 0
void timer_interrupt_handler(void) {
    // 割り込み要因(REG_IF)の確認とクリア
    if (*(volatile unsigned short*)0x04000202 & 0x0008) { // Timer 0 bit
        *(volatile unsigned short*)0x04000202 = 0x0008; // クリア
        
        // カーネルの時計を更新
        hardclock((caddr_t)0, 0);
    }
}
#endif
