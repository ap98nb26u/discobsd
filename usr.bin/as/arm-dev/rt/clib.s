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
	@ __aeabi_uidivmod: r0=N, r1=D -> r0=N/D, r1=N%D  (D=0 -> 0, 0)
	@ Shift-and-subtract: shift D left (with a quotient bit in r3) until it
	@ reaches N or its top bit, then walk it back down, subtracting where
	@ it fits.  At most 32+32 steps, whatever the quotient (the old loop
	@ subtracted D once per unit of quotient).  No flag-setting S suffix or
	@ tst: as_arm does not emit them, so the top bit is tested with cmp/blt.
	.globl	__aeabi_uidivmod
__aeabi_uidivmod:
	cmp	r1, #0
	beq	.Ludz
	mov	r2, #0			@ quotient
	mov	r3, #1			@ current quotient bit
.Lua:
	cmp	r1, r0
	bhs	.Lub			@ D >= N: aligned
	cmp	r1, #0
	blt	.Lub			@ D's top bit set: cannot shift further
	lsl	r1, r1, #1
	lsl	r3, r3, #1
	b	.Lua
.Lub:
	cmp	r0, r1
	subhs	r0, r0, r1		@ N -= D where it fits
	orrhs	r2, r2, r3		@ and record that quotient bit
	lsr	r1, r1, #1
	lsr	r3, r3, #1
	cmp	r3, #0
	bne	.Lub
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

	@ ---- hot string/memory/stdio routines, hand-written ----
	@ smlrc's own code (and the C versions these replace) is unoptimized;
	@ profiling the self-built smlrc showed strcmp/memmove/strchr/strlen
	@ and per-character stdio as a large share of its run time.  Only
	@ instructions as_arm emits are used: no S suffix, no tst, no
	@ conditional loads/stores, no register offsets.

	@ int strcmp(const char *a, const char *b): compares as unsigned char
	.globl	strcmp
strcmp:
.Lscl:
	ldrb	r2, [r0], #1
	ldrb	r3, [r1], #1
	cmp	r2, #0
	beq	.Lsce
	cmp	r2, r3
	beq	.Lscl
.Lsce:
	sub	r0, r2, r3
	bx	lr

	@ size_t strlen(const char *s)
	.globl	strlen
strlen:
	mov	r1, r0
.Lsll:
	ldrb	r2, [r1], #1
	cmp	r2, #0
	bne	.Lsll
	sub	r0, r1, r0
	sub	r0, r0, #1
	bx	lr

	@ char *strchr(const char *s, int c): c==0 finds the terminator
	.globl	strchr
strchr:
	and	r1, r1, #255
.Lchl:
	ldrb	r2, [r0], #1
	cmp	r2, r1
	beq	.Lchf
	cmp	r2, #0
	bne	.Lchl
	mov	r0, #0
	bx	lr
.Lchf:
	sub	r0, r0, #1
	bx	lr

	@ void *memmove(void *d, const void *s, size_t n); memcpy is the same.
	@ Copies a word at a time when d, s (and, going backwards, n) are
	@ word-aligned; otherwise a byte at a time.  Returns d (r0 kept).
	.globl	memcpy
memcpy:
	.globl	memmove
memmove:
	cmp	r0, r1
	bhi	.Lmvb			@ d > s: copy backwards (overlap-safe)
	mov	r3, r0
	orr	r12, r3, r1
	and	r12, r12, #3
	cmp	r12, #0
	bne	.Lmfb			@ unaligned: bytes
.Lmfw:
	cmp	r2, #4
	blo	.Lmfb
	ldr	r12, [r1], #4
	str	r12, [r3], #4
	sub	r2, r2, #4
	b	.Lmfw
.Lmfb:
	cmp	r2, #0
	beq	.Lmvd
	ldrb	r12, [r1], #1
	strb	r12, [r3], #1
	sub	r2, r2, #1
	b	.Lmfb
.Lmvb:
	add	r1, r1, r2
	add	r3, r0, r2
	orr	r12, r3, r1
	and	r12, r12, #3
	cmp	r12, #0
	bne	.Lmbb			@ unaligned ends: bytes
.Lmbw:
	cmp	r2, #4
	blo	.Lmbb
	ldr	r12, [r1, #-4]!
	str	r12, [r3, #-4]!
	sub	r2, r2, #4
	b	.Lmbw
.Lmbb:
	cmp	r2, #0
	beq	.Lmvd
	ldrb	r12, [r1, #-1]!
	strb	r12, [r3, #-1]!
	sub	r2, r2, #1
	b	.Lmbb
.Lmvd:
	bx	lr

	@ int fputc(int c, FILE *p) / int fgetc(FILE *p): the buffered fast
	@ path of the <stdio.h> putc/getc macros (struct _iobuf: _cnt at 0,
	@ _ptr at 4); when the buffer is full/empty, tail-call _flsbuf(c, p) /
	@ _filbuf(p) in stdio.c with the arguments still in r0/r1.
	.globl	fputc
fputc:
	ldr	r2, [r1]
	sub	r2, r2, #1
	str	r2, [r1]
	cmp	r2, #0
	blt	.Lfpc
	ldr	r3, [r1, #4]
	strb	r0, [r3], #1
	str	r3, [r1, #4]
	and	r0, r0, #255
	bx	lr
.Lfpc:
	b	_flsbuf

	.globl	fgetc
fgetc:
	ldr	r1, [r0]
	sub	r1, r1, #1
	str	r1, [r0]
	cmp	r1, #0
	blt	.Lfgc
	ldr	r2, [r0, #4]
	ldrb	r3, [r2], #1
	str	r2, [r0, #4]
	mov	r0, r3
	bx	lr
.Lfgc:
	b	_filbuf

	.data
	.globl	errno
errno:
	.word	0
	.globl	_curbrk
_curbrk:
	.word	_end
