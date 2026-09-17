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

    @ Force REG_IME off before anything else. The GBA BIOS's SoftReset
    @ (SWI 0x00, used by cpu_reboot() in machdep.c) only clears a small
    @ IWRAM range and CPU-mode state - it never touches I/O register
    @ space (0x04000000+), so REG_IME survives a SoftReset-triggered
    @ reboot exactly as it was at the moment of reset. A true cold
    @ power-on always starts with REG_IME=0 (hardware POR default), so
    @ every existing assumption in this port about "interrupts start
    @ masked until the kernel deliberately unmasks them at a safe
    @ point" (see resume()/longjmp()'s CPSR I-bit dance in locore.S,
    @ and gba_irq_allow()/gba_irq_block() in machdep.c) implicitly
    @ relies on that same starting condition. If a SoftReset happens to
    @ land here with REG_IME left at 1 - e.g. a panic firing while a
    @ process was mid-resume or idle() was between gba_irq_allow() and
    @ gba_irq_block() - the very first CPSR I-bit clear (the first
    @ process resume, in resume()/longjmp()) would immediately let a
    @ real interrupt through at a point boot code never expected one,
    @ with none of this port's careful register-preservation machinery
    @ around it yet. Matches this session's observation: an early hang
    @ specifically following a SoftReset-triggered reboot, never seen
    @ after a genuine cold power-on.
    ldr r0, =0x04000208 @ REG_IME
    mov r1, #0
    strh r1, [r0]

    @ Generous settle delay before touching any cartridge hardware
    @ (EverDrive/SD probing starts soon after, in config()/card_init()
    @ - see gba/dev/sd.c's own settle delay for the fuller story).
    @ Confirmed needed here, not just in card_init(): switching
    @ cpu_reboot() (machdep.c) from SoftReset (SWI 0x00) to the
    @ undocumented HardReset (SWI 0x26) fixed the hang SoftReset left
    @ behind, but turned this session's previously-intermittent SD
    @ write timeout into a 100%-reproducible one, on every boot, at
    @ the exact same point - and, on later boots in the same test
    @ run, "sd0: cannot open ed0 port" starting to appear too.
    @ Reproduced across swapping in a completely different physical SD
    @ card (even a different capacity/type, 2GB SD to 16GB SDHC),
    @ which rules out a marginal card and points at HardReset's more
    @ aggressive reset needing more real settling time before the
    @ EverDrive's own FPGA/SD interface is actually ready to talk to -
    @ card_init()'s own settle delay runs too late to help
    @ ed_probe()'s port-open attempt, which happens first. A few
    @ hundred ms once at boot is cheap insurance either way.
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

    @ カーネルスタックを u に合わせる
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

    @ (二重)割り込み禁止
    ldr r1, =0x04000208 @ REG_IME
    mov r2, #0
    strh r2, [r1]

    @ save registers (full)
    push {r0-r3, r4-r11, r12, lr}

    @ DBG: stash lr_irq (still live in lr here, untouched by the push
    @ above) into a C-visible global before it's out of reach - IRQ
    @ mode's own lr is banked and unavailable once we switch to SYS
    @ mode two instructions down. gba_do_schedule() (machdep.c) uses
    @ this to bounds-check where the interrupted code actually was,
    @ chasing a wild-jump crash that a syscall-return-only check never
    @ catches - see the comment there.
    ldr r0, =irq_last_lr
    str lr, [r0]

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

    @ 割り込み許可
    ldr r1, =0x04000208 @ REG_IME
    mov r2, #1
    strh r2, [r1]

    bx lr @ BIOS経由で帰還

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
