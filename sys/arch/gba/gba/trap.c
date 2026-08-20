#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/tty.h>
#include <sys/systm.h>

#include <gba/dev/gba_syscall.h>

int syscall_entry(uint32_t *pc)
{
    printf("TRAP: ");

    uint32_t instr = *pc;

    printf("0x%08X, 0x%02X\n", pc, instr);

    int num = (instr >> 8) & 0xFF;

    return arch_syscall(num);
}
