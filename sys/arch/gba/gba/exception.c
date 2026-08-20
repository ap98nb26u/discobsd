#if 0
static int tick = 0;

void timer_interrupt_handler() {
    if (timer0_flag == 1) {
        //hardclock((caddr_t)0, 0);
	if (tick % 100 == 0) {
		printf("@");
	i
	timer0_flag = 0;
    }
}
#endif
