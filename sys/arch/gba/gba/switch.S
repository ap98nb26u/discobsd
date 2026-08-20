.align 4
.arm
.global switch_context
switch_context:
    @ r0 = old, r1 = new

    stmfd sp!, {r4-r11, lr}
    str sp, [r0]

    ldr sp, [r1]
    ldmfd sp!, {r4-r11, pc}

    mov r0, lr
    bx r0

    @mrs r2, cpsr
    @bic r2, r2, #0x1F
    @orr r2, r2, #0x1F
    @msr cpsr_c, r2

    @bx lr

.global gba_switch_from_irq
@gba_switch_from_irq:
    @ r0 = old,  r1 = new

    @ save context of current task
    stmfd sp!, {r4-r11, lr}
    str sp, [r0]

    @ load sp of next task
    ldr sp, [r1]
    ldmfd sp!, {r4-r11, lr}

    bx lr
