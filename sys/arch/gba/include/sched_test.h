#pragma once
#include <sys/stdint.h>

#include <machine/gba_syscall.h>

struct lwp {
    uint32_t *sp;
};

extern struct lwp *lwp_current;

void sched_test(void);
void sched_test_init(void);
