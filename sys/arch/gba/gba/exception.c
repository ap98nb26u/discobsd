void timer_interrupt_handler() {
    // 割り込み要因(REG_IF)の確認とクリア
    if (*(volatile unsigned short*)0x04000202 & 0x0008) { // Timer 0 bit
        *(volatile unsigned short*)0x04000202 = 0x0008; // クリア
        
        // カーネルの時計を更新
        hardclock((caddr_t)0, 0);
    }
}
