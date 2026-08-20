#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <machine/sched_test.h>

#define ARM_CODE   __attribute__((target("arm")))
#define THUMB_CODE __attribute__((target("thumb")))
#define IWRAM_CODE __attribute__((section(".iwram")))

#define REG_BASE     0x04000000
#define REG_IME      (*(volatile uint16_t *)(REG_BASE + 0x0208))
#define REG_IE       (*(volatile uint16_t *)(REG_BASE + 0x0200))
#define REG_IF       (*(volatile uint16_t *)(REG_BASE + 0x0202))

uint32_t stack1[256], stack2[256];

static struct lwp lwp1, lwp2;
struct lwp *lwp_current;

static struct lwp *pick_next(void)
{
    return (lwp_current == &lwp1) ? &lwp2 : &lwp1;
}

ARM_CODE extern void switch_context(struct lwp *old, struct lwp *new);
ARM_CODE extern void gba_switch_from_irq(struct lwp *old, struct lwp *new);

ARM_CODE void sched_test(void)
{
    struct lwp *old = lwp_current;
    struct lwp *next = pick_next();

    lwp_current = next;

    if (old != next) {
        lwp_current = next;
        switch_context(old, next);
    }
}

#if 0
ARM_CODE void gba_sched_pick(void)
{
    struct proc *p;

    curproc = nproc;
}

ARM_CODE void cpu_set_kstack(struct proc *p)
{
    uint32_t *sp = (uint32_t *)(p->p_addr + 3072);

    // (A)
    *(--sp) = (uint32_t)start_func; // pc
    *(--sp) = 0;    // r12
    *(--sp) = 0;    // r3
    *(--sp) = 0;    // r2
    *(--sp) = 0;    // r1
    *(--sp) = 0;    // r0
    *(--sp) = 0x1F; // spsr (SYS Mode/ARM, 0x3F if Thumb)

    // (B)
    *(--sp) = (uint32_t)lr_exit; // lr
    *(--sp) = 0;    // r11
    *(--sp) = 0;    // r10
    *(--sp) = 0;    // r9
    *(--sp) = 0;    // r8
    *(--sp) = 0;    // r7
    *(--sp) = 0;    // r6
    *(--sp) = 0;    // r5
    *(--sp) = 0;    // r4

    // label_t (u.u_rsav) sp (index 9) 
    struct user *up = (struct user *)p->addr;
    up->u_rsav.val[9] = (long)sp;
}
#endif

#if 0
ARM_CODE int gba_do_schedule(void)
{
#if 0
    sys_write("I");
#else
    REG_IF = (1<<3);
    struct lwp *old = lwp_current;
    struct lwp *next = pick_next();

    if (old != next) {
        lwp_current = next;
        return 1;
    }
    return 0;
#endif
}
#endif

static void setup_lwp(struct lwp *p, void (*entry)(void), uint32_t *stack_top)
{
    uint32_t *sp = stack_top;

    *(--sp) = ((uint32_t)entry); // lr
    for (int i = 11; i >= 4; i--)
        *(--sp) = 0;

    p->sp = sp;
}

void proc1(void)
{
    while (1) {
        sys_write("A");
        //for (volatile int i=0; i<100; i++);
        //sched_test();
    }
    sys_write("back\n");
}

void proc2(void)
{
    while (1) {
        sys_write("B");
        //for (volatile int i=0; i<100; i++);
        //sched_test();
    }
    sys_write("back\n");
}

void dump_sp(struct lwp *p)
{
    uint32_t *sp = p->sp;

    for (int i = 0; i < 10; i++) {
        printf("%08x ", sp[i]);
    }
    printf("\n");
}

void sched_test_init(void)
{
    setup_lwp(&lwp1, proc1, stack1+256-1);
    setup_lwp(&lwp2, proc2, stack2+256-1);

    dump_sp(&lwp1);
    dump_sp(&lwp2);

    lwp_current = &lwp1;
}
