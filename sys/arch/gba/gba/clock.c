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

#include <machine/gba.h>

void clkstart(void) {
}

void cpu_initclocks(void) {
    // Timer 0 の設定
    REG_TM0CNT_L = 65536 - (16777216 / 1024 / HZ); 
    
    // 0x0040: 割り込み有効
    // 0x0003: 1024分周
    // 0x0080: 開始
    REG_TM0CNT_H = 0x00C3;

    // GBA全体の割り込み許可レジスタ
    REG_IE |= IRQ_TIMER0;
    //REG_IME = 1;
}

#if 0
void timer_interrupt_handler(void) {
    // 割り込み要因(REG_IF)の確認とクリア
    if (REG_IF & IRQ_TIMER0) { // Timer 0 bit
        REG_IF = IRQ_TIMER0; // クリア
        
        // カーネルの時計を更新
        hardclock((caddr_t)0, 0);
    }
}
#endif
