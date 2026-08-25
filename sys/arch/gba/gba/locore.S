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

#define UBASE __user_data_start	/* User space base addr (see kern.ldscript). */
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
	@ u0area: 0x030067FC-0x030073FB
	@ uarea:  0x030073FC-0x03007FFB (0x03007FFC-0x03007FFF is the IRQ vector)
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
	ldr	r3, =(2f + 1)		/*   else no need to remap u. area. */
	bx	r3			/*   The +1 sets Thumb mode bit. */
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
	@.thumb_func
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
	@svc     #11			/* Make SYS_execv syscall. */
	ldr	r3, =simulate_swi_via_inline_data
	mov	lr, pc			/* lr = addr of the .word below, */
	bx	r3			/*   matching CALL_SIMSYS's convention. */
	.word	11
	@svc     #1			/* Make SYS_exit syscall. */
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

@	.text
@	.align 2
@	.arm
@	.global	syscall_gateway
@	.type	syscall_gateway, %function
@syscall_gateway:
@	@ save user SP to ip (r12) (tf_ip is defined tf_sp in C)
@	mov	ip, sp		@ save user sp to sp (r12)
@
@	ldr	r12, =u
@	add	r12, r12, #3072	@ kernel stack top
@	sub	r12, r12, #64	@ trapframe start
@
@	stmia	r12, {r0-r3}	@ first, save r0-r3 in safe
@	mov	sp, r12		@ now change sp
@
@	@ change SP to fixed position (u+USIZE) of kernel stack
@	@ldr	sp, =0x0203E000	@ 壊れない場所に置く
@
@	@sub	sp, sp, #64	@ prepare trapframe
@
@	stmia	sp, {r0-r3}	@ save r0-r3 to top of trapframe
@
@	str	ip, [sp, #16]	@ tf_sp / tf_ip (offset 16)
@	str	lr, [sp, #20]	@ tf_lr (offset 20)
@	str	lr, [sp, #24]	@ tf_pc (offset 24) (lr should be saved here)
@	mrs	r0, spsr
@	str	r0, [sp, #28]
@
@	@ second 8 regs
@	add	r0, sp, #32
@	stmia	r0, {r4-r11}
@	
@	@ call syscall_handler(sys_num, frame_ptr)
@	@ syscall number to arg1 set in r7 at icode
@	mov	r0, r7	@ sys_num
@	mov	r1, sp	@ pointer to trapframe
@
@	ldr	r3, =syscall_handler
@	mov	lr, pc
@	bx	r3
@
@	@ --- syscall_handler restore process ---
@
@	@ save tf_pc (24) and tf_ip (16)
@	ldr	r12, [sp, #24]	@ tf_pc 0x20000f9
@	ldr	r2, [sp, #16]	@ tf_sp user SP
@
@	@ restore first half register
@	add	r0, sp, #32	@ restore r0-r3
@	ldmia	r0, {r4-r11}	@ restore r0-r3
@
@	@ restore first half register
@	ldmia	sp, {r0-r3}	@ restore r0-r3
@
@	@ ----------
@	mov	sp, r2		@ set User SP
@
@	@ for debug
@	msr	cpsr_c, #0xDF
@	bx	r12		@ go to User + Thumb
