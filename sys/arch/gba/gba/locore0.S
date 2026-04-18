.section .text

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
    @ IRQ mode
    msr cpsr_c, #0xD2
    ldr sp, =0x03007000 @ fit to BIOS

    @ SYS mode for C
    msr cpsr_c, #0xDF
    ldr sp, =0x0303F000

    @ register user intr handler
    ldr r0, =0x03007FFC
    ldr r1, =gba_intr_stub
    @orr r1, r1, #1 @ Thumb bit on
    str r1, [r0]

    @ --- clear BSS ---
    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    mov r2, #0
bss_loop:
    cmp r0, r1
    strlt r2, [r0], #4
    blt bss_loop

    @ change to ARM mode
    mrs r0, cpsr
    bic r0, r0, #0x80   @ IRQ enable
    msr cpsr_cxsf, r0

.global main
    ldr r0, =main
    bx r0

    b .

.ltorg @ put out data fo reteral pool

.align 4
.arm
.global gba_intr_stub
gba_intr_stub:
    stmfd sp!, {r0-r3, lr}

    @ldr r0, =0x04000202 @ REG_IF
    @ldr r1, [r0]
    @strh r1, [r0]

    ldr r0, =gba_intr_handler
    orr r0, r0, #1
    mov lr, pc
    bx r0

    ldmfd sp!, {r0-r3, lr}
    bx lr

.ltorg @ put out data for stub

    @ end
