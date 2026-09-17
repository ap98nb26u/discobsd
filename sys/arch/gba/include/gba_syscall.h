
#ifndef _GBA_SYSCALL_H_
#define _GBA_SYSCALL_H_

int sys_write(const char *s);

/* Software-simulated SWI trampoline (see machparam.h SYSCALL_VECTOR_ADDR). */
void simulate_swi_via_inline_data(void);

#endif /* _GBA_SYSCALL_H_ */
