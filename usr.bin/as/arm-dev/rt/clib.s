@ clib.s - minimal ARM (a.out) libc for DiscoBSD/GBA, native toolchain.
@ Syscalls use this port's simulated-SWI trampoline at 0x03007E00:
@   push{lr}; ldr r12,=vec; ldr r12,[r12]; mov lr,pc; bx r12; .word SYS_n; pop{lr}
@ carry set on error (trampoline convention).
	.arm
	.text

	.globl	_exit
_exit:
	push	{lr}
	ldr	r12, =0x03007E00
	ldr	r12, [r12]
	mov	lr, pc
	bx	r12
	.word	1			@ SYS_exit
	b	_exit			@ not reached
	.ltorg

	.globl	exit
exit:
	b	_exit

	.globl	write
write:
	push	{lr}
	ldr	r12, =0x03007E00
	ldr	r12, [r12]
	mov	lr, pc
	bx	r12
	.word	4			@ SYS_write
	pop	{lr}
	bcs	.Lwrite_err
	bx	lr
.Lwrite_err:
	ldr	r1, =errno
	str	r0, [r1]
	mvn	r0, #0
	bx	lr
	.ltorg

	.globl	read
read:
	push	{lr}
	ldr	r12, =0x03007E00
	ldr	r12, [r12]
	mov	lr, pc
	bx	r12
	.word	3			@ SYS_read
	pop	{lr}
	bcs	.Lread_err
	bx	lr
.Lread_err:
	ldr	r1, =errno
	str	r0, [r1]
	mvn	r0, #0
	bx	lr
	.ltorg

	.globl	open
open:
	push	{lr}
	ldr	r12, =0x03007E00
	ldr	r12, [r12]
	mov	lr, pc
	bx	r12
	.word	5			@ SYS_open
	pop	{lr}
	bcs	.Lopen_err
	bx	lr
.Lopen_err:
	ldr	r1, =errno
	str	r0, [r1]
	mvn	r0, #0
	bx	lr
	.ltorg

	.globl	close
close:
	push	{lr}
	ldr	r12, =0x03007E00
	ldr	r12, [r12]
	mov	lr, pc
	bx	r12
	.word	6			@ SYS_close
	pop	{lr}
	bcs	.Lclose_err
	bx	lr
.Lclose_err:
	ldr	r1, =errno
	str	r0, [r1]
	mvn	r0, #0
	bx	lr
	.ltorg

	.globl	lseek
lseek:
	push	{lr}
	ldr	r12, =0x03007E00
	ldr	r12, [r12]
	mov	lr, pc
	bx	r12
	.word	19			@ SYS_lseek
	pop	{lr}
	bcs	.Llseek_err
	bx	lr
.Llseek_err:
	ldr	r1, =errno
	str	r0, [r1]
	mvn	r0, #0
	bx	lr
	.ltorg

	@ _brk(addr): SYS_sbrk. r0=new break -> 0 ok / -1 err.
	.globl	_brk
_brk:
	push	{lr}
	ldr	r12, =0x03007E00
	ldr	r12, [r12]
	mov	lr, pc
	bx	r12
	.word	69			@ SYS_sbrk
	pop	{lr}
	bcs	.Lbrk_err
	bx	lr
.Lbrk_err:
	ldr	r1, =errno
	str	r0, [r1]
	mvn	r0, #0
	bx	lr
	.ltorg

	@ void *sbrk(int incr): grow the break; return old break or (void*)-1.
	.globl	sbrk
sbrk:
	push	{r4, r5, lr}
	ldr	r3, =_curbrk
	ldr	r4, [r3]		@ r4 = oldbrk
	cmp	r0, #0
	beq	.Lsbrk_ret		@ incr==0 -> return oldbrk
	add	r5, r4, r0		@ r5 = newbrk
	mov	r0, r5
	bl	_brk			@ r0 = 0 / -1
	cmn	r0, #1			@ r0 == -1 ?
	beq	.Lsbrk_err
	ldr	r3, =_curbrk
	str	r5, [r3]		@ _curbrk = newbrk
.Lsbrk_ret:
	mov	r0, r4			@ return oldbrk
	pop	{r4, r5, lr}
	bx	lr
.Lsbrk_err:
	mvn	r0, #0			@ (void*)-1
	pop	{r4, r5, lr}
	bx	lr
	.ltorg

	@ ---- EABI integer division (repeated subtraction; correct, slow) ----
	@ __aeabi_uidivmod: r0=N, r1=D -> r0=N/D, r1=N%D
	.globl	__aeabi_uidivmod
__aeabi_uidivmod:
	cmp	r1, #0
	beq	.Ludz
	mov	r2, #0			@ quotient
.Lus:
	cmp	r0, r1
	blo	.Lud			@ N < D -> done
	sub	r0, r0, r1
	add	r2, r2, #1
	b	.Lus
.Lud:
	mov	r1, r0			@ remainder
	mov	r0, r2			@ quotient
	bx	lr
.Ludz:
	mov	r0, #0
	mov	r1, #0
	bx	lr

	.globl	__aeabi_uidiv
__aeabi_uidiv:
	b	__aeabi_uidivmod	@ returns quotient in r0

	@ __aeabi_idivmod: signed. r0=N, r1=D -> r0=N/D, r1=N%D (rem has sign of N)
	.globl	__aeabi_idivmod
__aeabi_idivmod:
	push	{r4, lr}
	mov	r4, #0			@ bit0=negate quotient, bit1=negate remainder
	cmp	r0, #0
	bge	.Lip1
	rsb	r0, r0, #0
	eor	r4, r4, #3		@ N<0: quotient flips, remainder(sign of N) negative
.Lip1:
	cmp	r1, #0
	bge	.Lip2
	rsb	r1, r1, #0
	eor	r4, r4, #1		@ D<0: quotient flips
.Lip2:
	bl	__aeabi_uidivmod	@ r0=|Q|, r1=|R|
	and	r2, r4, #1
	cmp	r2, #0
	rsbne	r0, r0, #0		@ Q = -Q
	and	r2, r4, #2
	cmp	r2, #0
	rsbne	r1, r1, #0		@ R = -R
	pop	{r4, lr}
	bx	lr

	.globl	__aeabi_idiv
__aeabi_idiv:
	b	__aeabi_idivmod		@ returns quotient in r0

	.data
	.globl	errno
errno:
	.word	0
	.globl	_curbrk
_curbrk:
	.word	_end
