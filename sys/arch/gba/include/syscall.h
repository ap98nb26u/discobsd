#ifndef _GBA_USER_SYSCALL_H_
#define _GBA_USER_SYSCALL_H_

#if 0
#define SYSCALL(n) \
    __asm__ volatile (".word 0xE7F000F0 | (" #n " << 8)")
#endif

#endif /* _GBA_USER_SYSCALL_H_ */
