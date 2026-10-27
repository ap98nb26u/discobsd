.section .text

    .arm
.global _start
_start:
    @ GBA cart header
    b reset
    .space 156

    @ 0x080000A0
    .ascii "DiscoBSD/GBA"
    .ascii "0000"
    .ascii "01"
    .byte  0x96
    .byte  0
    .byte  0
    .byte  0, 0, 0, 0, 0, 0, 0
    .byte  0
    .byte  0
    .space 2

    .align 4
    .arm
reset:
    @ arrive here in ARM mode, SYS mode, with IRQ and FIQ disabled
    ldr sp, =0x0203FFF8 @ do not use last 8 bytes

    @ Force REG_IME off before anything else.  A cold power-on starts
    @ with IME=0, but a reboot (cpu_reboot() in machdep.c) is a warm
    @ jump here, and nothing resets I/O space on the way.  The port
    @ relies on interrupts staying masked until the kernel unmasks them
    @ at a safe point (resume()/longjmp() in locore.S, gba_irq_allow()
    @ in machdep.c); an IME left at 1 would let an interrupt in at the
    @ first CPSR I-bit clear, before any of that is set up.
    ldr r0, =0x04000208 @ REG_IME
    mov r1, #0
    strh r1, [r0]

    @ Settle delay (a few hundred ms) before touching any cartridge
    @ hardware: the EverDrive's FPGA/SD interface needs time after a
    @ reset before ed_probe() opens its port, and card_init()'s own
    @ delay (sd.c) runs too late for that.  Without it, boots after a
    @ reset failed with SD write timeouts and "sd0: cannot open ed0
    @ port", on more than one SD card.
    ldr r0, =3000000
1:
    subs r0, r0, #1
    bne 1b

    @ register user intr handler
    @ The BIOS trampoline (add lr,pc,#0 / ldr pc,[r0,#-4]) does not save
    @ any registers before jumping to the handler installed here, so a
    @ plain compiled C function used directly would clobber r0-r3/r12 of
    @ whatever code was interrupted (harmless while the kernel carefully
    @ keeps IME=0 through its own register-juggling, but corrupts any
    @ real userland code the instant IME=1). gba_intr_handler below is
    @ an asm wrapper that saves/restores r0-r12,lr around the actual work
    @ (in gba_do_schedule) and is safe to call from arbitrary interrupted
    @ context - which is why the handler is this wrapper, not a bare C
    @ function.
    ldr r0, =0x03007FFC
    ldr r1, =gba_intr_handler
    str r1, [r0]

    @ copy .fast_text from ROM to IWRAM
    ldr r0, =_sifast @ source addr in ROM
    ldr r1, =__fast_start @ distination addr in IWRAM
    ldr r2, =__fast_end
fast_loop:
    cmp r1, r2
    bge fast_done
    ldr r3, [r0], #4
    str r3, [r1], #4
    b fast_loop
fast_done:

    @ --- copy .data from ROM to IWRAM ---
CopyDataInit:
.global _sidata
    ldr r0, =_sidata @ source addr in ROM
    ldr r1, =_sdata @ distination addr in IWRAM
    ldr r2, =_edata
data_loop:
    cmp r1, r2
    bge data_done
    ldr r3, [r0], #4
    str r3, [r1], #4
    b data_loop
data_done:

    @ --- clear BSS ---
FillZeroBss:
    ldr r0, =_sbss
    ldr r1, =_ebss
    mov r2, #0
bss_loop:
    cmp r0, r1
    strlt r2, [r0], #4
    blt bss_loop

    @ --- clear the kernel .bss kept in EWRAM (GBAED; empty on GBA) ---
    ldr r0, =__ewram_bss_start
    ldr r1, =__ewram_bss_end
ewram_bss_loop:
    cmp r0, r1
    strlt r2, [r0], #4
    blt ewram_bss_loop

    @ --- clear the u0/u process-context slots (U0AREA/UAREA, EWRAM) ---
    @ They are not part of .bss, and on a cold boot EWRAM holds garbage;
    @ the MI boot path relies on u starting zeroed (e.g. u_uid/u_ruid/
    @ u_svuid/u_groups[0] are never set before init runs, so a nonzero
    @ uid made init exit at once).  cpu_reboot() wipes them on a warm boot.
    ldr r0, =u0
    ldr r1, =u_end
uarea_loop:
    cmp r0, r1
    strlt r2, [r0], #4
    blt uarea_loop

    @ kernel stack: the top of u
    ldr r0, =u
    add sp, r0, #3072 @ USIZE

.global main @Thumb
    bl main
    @ main() only ever returns here for the newly-forked process 1
    @ (process 0 loops forever in sched()). It has just bcopy'd icode
    @ to __user_data_start; jump there to start running it.
    ldr r0, =__user_data_start
    bx r0

.ltorg @ put out data fo reteral pool

.section .iwram_text,"ax" @ put in .fast_text area
.align 4
.arm
.global gba_intr_handler
gba_intr_handler:
    ldr r0, =0x04000000
    ldr r1, =0x001F
    strh r1, [r0, #0x6D]

    @ interrupts off (again)
    ldr r1, =0x04000208 @ REG_IME
    mov r2, #0
    strh r2, [r1]

    @ save registers (full)
    push {r0-r3, r4-r11, r12, lr}

#ifdef HANGDUMP
    @ The interrupted code's own pc+4 is the lr the BIOS IRQ dispatch
    @ pushed ({r0-r3, r12, lr}, lr at +20) just above our 14 words; keep
    @ it for the L+R+SELECT dump (gba_hang_dump, machdep.c).
    ldr r1, [sp, #(14*4 + 20)]
    ldr r0, =irq_last_pc
    str r1, [r0]
#endif

    @ change to SYS mode
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0x1F @ SYS
    msr cpsr_c, r0
    nop

    @ SYS and USR modes share sp/lr (unlike IRQ, which is banked), so
    @ this lr is the *interrupted* code's own live lr - e.g. a kernel
    @ syscall trampoline mid-return with the userland entry address
    @ sitting in lr, about to bx to it. The push {..., lr} above (still
    @ in IRQ mode at that point) only saved lr_irq; without saving this
    @ one too, bl gba_do_schedule below clobbers the interrupted code's
    @ lr with its own return address, so control jumps to garbage the
    @ next time that code does "bx lr" after the interrupt returns.
    push {lr}

    @ call prep
.global gba_do_schedule
    bl gba_do_schedule

    pop {lr}

    @ return to IRQ mode
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0x12 @ IRQ
    msr cpsr_c, r0
    nop

    pop {r0-r3, r4-r11, r12, lr}

    @ interrupts on
    ldr r1, =0x04000208 @ REG_IME
    mov r2, #1
    strh r2, [r1]

    bx lr @ return through the BIOS

.align 2
.arm
.global savectx
savectx:
    @ r0 is label_t *env (usualy &u.u_rsav)
    @ val[0..7]: r4-11
    @ val[8]   : lr
    @ val[9]   : sp
    stmia r0!, {r4-r11, lr} @ save r4-r11, lr (9 regs)
    str sp, [r0] @ save sp as 10th reg

    mov r0, #0 @ return value 0
    bx lr @ return to callee (Thumb/ARM auto detect)

.ltorg @ put out data for handler
    @ end
