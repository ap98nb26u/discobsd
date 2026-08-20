
#ifndef _GBA_SYSCALL_H_
#define _GBA_SYSCALL_H_

//int arch_syscall(int num);
int sys_write(const char *s);
int sys_read(char *buf, int size);

/* Software-simulated SWI trampoline (see machparam.h SYSCALL_VECTOR_ADDR). */
void simulate_swi_via_inline_data(void);

#endif /* _GBA_SYSCALL_H_ */
