/*
 * Copyright (c) 2022 Christopher Hettrick <chris@structfoo.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define UBASE __user_data_start	/* User space base addr (see kern_gba.ldscript). */
#define USIZE 3072		/* From user.h */

	.syntax	unified
	.thumb

/*
 * setjmp(env)
 *	label_t *env;
 *
 * setjmp(env) will save the process' current register context
 * and return a zero.
 */
	.text
	.align	2
	.arm
	.globl	setjmp
	.type	setjmp, %function
setjmp:
	mov	ip, sp
	stmea	r0!, {r4-r10,fp,ip,lr}	/* Save regs in env arg. */
	movs	r0, #0			/* Returns zero. */
	bx	lr
	.size	setjmp, . - setjmp

/*
 * longjmp(u, env)
 * resume(u, env)
 *	memaddr	u;
 *	label_t	*env;
 *
 * longjmp(u, env) (and resume) will generate a "return(1)" from the last
 * call to setjmp(env) by mapping in the user structure pointed to by u,
 * restoring the context saved by setjmp in env and returning a one.
 * Note that registers are recovered statically from the env buffer rather
 * than dynamically from the stack ...
 *
 * This longjmp differs from the longjmp found in the standard library and
 * the VAX 4.3 kernel - it's actually closer to the resume routine of the
 * 4.3 kernel and, indeed, even used to be called resume in the 2.9 kernel.
 * We've given it both names to promote some degree of compatibility between
 * the 4.3 and 2.10 C kernel source ...
 */
	.extern u0
	.extern u

	.text
	.align	2
	.arm
	.globl	longjmp
	.type	longjmp, %function
longjmp:
	.globl	resume
	.type	resume, %function
resume:
	@ u0 and u live in EWRAM (U0AREA/UAREA in kern_gba*.ldscript).
	/* Disable interrupts. */
	mrs r3, cpsr
	orr r3, r3, #0x80
	msr cpsr_c, r3

	@ u=a1=r0, env=a2=r1
	mov	r12, a2			/* Save env (second arg). */
	ldr	r1, =u0			/* Pointer to u0. area start addr. */
	ldr	r2, =u			/* Pointer to u. area start addr. */
	cmp	a1, r2			/* If u (first arg) is not u. area, */
	bne	1f			/*   then remap u. area. */
	/*
	 * No remap needed - this is the "process 0 resuming itself" case
	 * (u.u_procp->p_addr already equals u, so a1==r2 here). Label 2:
	 * below is ARM code in this same function, so a plain branch keeps
	 * ARM state.
	 */
	b	2f
	.pool				/* Insert literal pool for labels. */
1:
	ldr	r3, [r2, #0]		/* Pointer to u.u_procp structure. */
	str	r1, [r3, #60]		/* Save &u0 in u.u_procp->p_addr. */

	/* Exchange contents of u. and u0. */
	adds	r0, r2, #0		/* Pointer to u. area start addr. */

	.rept	USIZE / 3 / 4		/* 4 * 3 * 256 = 3072 = USIZE */
	ldmia	r0, {r2-r4}		/* No '!': stmia's own writeback */
	ldmia	r1, {r5-r7}		/*   advances r0/r1 by 12 each. */
	stmia	r1!, {r2-r4}
	stmia	r0!, {r5-r7}
	.endr

	ldr	r2, =u			/* Pointer to u. area start addr. */

	ldr	r3, [r2, #0]		/* Pointer to u.u_procp structure. */
	str	r2, [r3, #60]		/* Save &u in u.u_procp->p_addr. */
2:
	mov	a2, r12			/* Restore env (second arg). */
	ldmia	a2!, {r4-r10,fp,ip,lr}	/* Restore regs from env arg. */
	mov	sp, ip

	/* Enable interrupts. */
	mrs r3, cpsr
	bic r3, r3, #0x80
	msr cpsr_c, r3
	nop

	movs	a1, #1			/* Transfer back to setjmp() return */
	bx	lr			/*   with a return value of one. */
	.pool				/* Literal pool for local labels. */
	.size	longjmp, . - longjmp

/*
 * icode() is copied out to process 1 to exec /sbin/init.
 * If the exec fails, process 1 exits.
 */
	.text
	.align	2
	.arm
	.globl	icode
	.type	icode, %object
icode:
	.type	icode1, %function
icode1:
	movs	r0, #(sbininit - icode)	/* Offset for /sbin/init path. */
	movs	r1, #(argv - icode)	/* Offset for *argv[] array. */
	ldr	r2, =UBASE		/* User space base address. */
	adds	r0, r2			/* execv() *name argument. */
	adds	r1, r2			/* execv() *argv[] argument. */

	/*
	 * icode never uses a stack, so r12 is whatever garbage the last
	 * function call (locore0.S's debug printf) happened to leave
	 * behind - unspecified per AAPCS, and observed to differ between
	 * builds/platforms. syscall_handler() reads r12 back as tf_sp
	 * (the process's stack pointer) for its stack bounds check, so
	 * give it the same deterministic value init_main.c already uses
	 * for p_saddr instead of leaving it undefined.
	 */
	ldr	r12, =(__user_data_end - 1024)

	.globl	simulate_swi_via_inline_data
	@ SYS_execv
	ldr	r3, =simulate_swi_via_inline_data
	mov	lr, pc			/* lr = addr of the .word below, */
	bx	r3			/*   matching CALL_SIMSYS's convention. */
	.word	11
	@ SYS_exit
	ldr	r3, =simulate_swi_via_inline_data
	mov	lr, pc
	bx	r3
	.word	1

forever:				/* Catch runaway code. */
	b	forever
	.pool				/* Put literal pool inside icode(). */

	.align	2
	.type	sbininit, %object
sbininit:
	.ascii	"/sbin/init\0"

	.globl	initflags
	.type	initflags, %object
initflags:
	.ascii	"-\0\0\0\0\0\0\0\0\0\0"	/* Space for options. */

	.align	2
	.type	argv, %object
argv:
	.word	sbininit+6-icode+UBASE	/* Address of "init\0". */
	.word	initflags-icode+UBASE	/* Init options. */
	.word	0

	.globl	icodeend
	.type	icodeend, %object
icodeend:
	.size	icode, . - icode

/*
 * gba_preempt_tramp - preempt user code from an interrupt.
 *
 * gba_intr_handler (locore0.S) returns here instead of to the interrupted
 * user code when gba_irq_preempt() (machdep.c) wants that code in the
 * kernel: a process switch is due (runrun) or a signal is pending. This
 * port only does either on the way out of a syscall (userret), so the
 * process is made to call one, much as signal delivery makes it call its
 * handler: every register is saved on its own stack, the syscall runs
 * userret, and the saved state is then put back exactly - including r12
 * and lr, which an ordinary syscall return does not preserve, the flags,
 * and Thumb state.
 *
 * Entered in ARM state, SYS mode, with sp pointing at the interrupted pc
 * and CPSR and every other register as the interrupted code left it.
 */
	.text
	.align	2
	.arm
	.globl	gba_preempt_tramp
	.type	gba_preempt_tramp, %function
gba_preempt_tramp:
	stmfd	sp!, {r0-r12, lr}	/* sp -> r0..r12, lr, pc, CPSR */
	bl	simulate_swi_via_inline_data
	.word	20			/* SYS_getpid: any harmless syscall */
	.globl	gba_preempt_tramp_ret
gba_preempt_tramp_ret:
	/*
	 * Back from the kernel, perhaps much later or via a signal handler
	 * and sigreturn. Restore sp and lr (banked: SYS mode's own), then
	 * the rest from SVC mode, whose exception return (movs pc, lr)
	 * loads pc and the whole CPSR - Thumb bit included - at once, with
	 * no register left over to branch through. IRQs stay masked from
	 * the moment the saved state is above sp: an interrupt runs on the
	 * interrupted stack and would overwrite it.
	 */
	msr	cpsr_c, #0x9f		/* SYS, IRQ and FIQ off */
	mov	r0, sp
	ldr	lr, [r0, #52]
	add	sp, r0, #64
	msr	cpsr_c, #0xd3		/* SVC, IRQ and FIQ off */
	ldr	r1, [r0, #60]
	msr	spsr_cxsf, r1
	ldr	lr, [r0, #56]
	ldmia	r0, {r0-r12}
	movs	pc, lr
	.size	gba_preempt_tramp, . - gba_preempt_tramp
