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
    @ at a safe point (the syscall return in syscall.c, gba_irq_allow()
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
    @ The BIOS IRQ dispatch saves r0-r3, r12 and lr_irq and calls the
    @ address stored here, in IRQ mode.  gba_intr_handler below is an asm
    @ wrapper rather than a bare C function because it must mask IME,
    @ switch to SYS mode, save the interrupted code's own lr, and possibly
    @ redirect the return into gba_preempt_tramp (locore.S).
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

    @ --- clear all of EWRAM ---
    @ This covers the u0/u process-context slots (U0AREA/UAREA) and the
    @ kernel .bss kept at the end of EWRAM on GBAED, neither of which is
    @ part of .bss.  On a cold boot EWRAM holds garbage, and on a reboot
    @ (cpu_reboot() jumps here) it holds the previous session; the MI
    @ boot path relies on u starting zeroed (e.g. u_uid/u_ruid/u_svuid/
    @ u_groups[0] are never set before init runs, so a nonzero uid made
    @ init exit at once), and stale fixed-address EWRAM made a rebooted
    @ init wild-jump.  Done here in assembly, before any stack is used,
    @ rather than in cpu_reboot(), which runs on a stack inside EWRAM.
    ldr r0, =0x02000000 @ EWRAM start
    ldr r1, =0x02040000 @ EWRAM end
    mov r3, #0
    mov r4, #0
    mov r5, #0
ewram_loop:
    cmp r0, r1
    stmltia r0!, {r2-r5}
    blt ewram_loop

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

    @ Keep, across the C calls below (r4-r11 are callee-saved), where
    @ the BIOS frame sits and the interrupted code's CPSR: both are IRQ-
    @ mode state that SYS mode cannot reach.
    mov r4, sp
    mrs r5, spsr

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

    @ Preemption. The kernel only switches processes and delivers
    @ signals on its way out of a syscall (userret), so user code that
    @ never makes one would never give up the CPU or see ^C. When
    @ gba_irq_preempt() says so, return into gba_preempt_tramp
    @ (locore.S) instead of the interrupted user code: it saves every
    @ register, makes a syscall, and then resumes the interrupted code.
    @ The interrupted pc and CPSR go on the interrupted code's own stack
    @ for it to find.
    ldr r0, [r4, #(14*4 + 20)]  @ the interrupted pc + 4 (BIOS frame lr)
    sub r0, r0, #4
    ldr r1, =gba_irq_preempt    @ C, in ROM: too far for bl
    mov lr, pc
    bx r1
    pop {lr}
    movs r6, r0
    beq 1f
    ldr r1, [r4, #(14*4 + 20)]
    sub r1, r1, #4
    stmfd sp!, {r1, r5}         @ [sp] = pc, [sp+4] = CPSR
    ldr r1, =gba_preempt_tramp + 4
    str r1, [r4, #(14*4 + 20)]  @ the BIOS returns with subs pc, lr, #4
1:

    @ return to IRQ mode
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0x12 @ IRQ
    msr cpsr_c, r0
    nop

    @ gba_preempt_tramp is ARM code: enter it in ARM state, whatever
    @ state the interrupted code was in.
    cmp r6, #0
    mrsne r0, spsr
    bicne r0, r0, #0x20 @ T
    msrne spsr_c, r0

    pop {r0-r3, r4-r11, r12, lr}

    @ interrupts on
    ldr r1, =0x04000208 @ REG_IME
    mov r2, #1
    strh r2, [r1]

    bx lr @ return through the BIOS

.ltorg @ put out data for handler
    @ end
