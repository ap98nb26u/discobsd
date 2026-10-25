/*
 * bcopy() and bzero() for the GBA kernel, in ARM code in IWRAM.
 *
 * The C versions, built without optimization and fetched from ROM, made
 * every large copy slow - exec loading a program, swapping a process,
 * the buffer cache - and these run inside system calls with interrupts
 * off, long enough for soft keyboard taps to go unseen.  IWRAM is the
 * one place ARM code runs at full speed (32-bit bus, no wait states);
 * the loops move 32 bytes per ldm/stm pair.
 *
 * Same semantics as the C versions they replace: bcopy copies forward
 * (overlap is undefined) and both fall back to byte accesses for
 * unaligned ends, so neither is for VRAM, which takes no byte writes.
 */
	.section .iwram_text,"ax",%progbits
	.arm
	.align	2

/* void bcopy(const void *src, void *dst, size_t n) */
	.global	bcopy
	.type	bcopy, %function
bcopy:
	cmp	r2, #0
	bxeq	lr
	eor	r3, r0, r1
	tst	r3, #3
	bne	.Lcbyte			@ src/dst misaligned to each other
.Lcalign:				@ bytes until both are word aligned
	tst	r0, #3
	beq	.Lcwords
	ldrb	r3, [r0], #1
	strb	r3, [r1], #1
	subs	r2, r2, #1
	bne	.Lcalign
	bx	lr
.Lcwords:
	subs	r2, r2, #32
	blt	.Lcword
	stmfd	sp!, {r4-r10}
.Lcblock:				@ 32 bytes at a time
	ldmia	r0!, {r3-r10}
	stmia	r1!, {r3-r10}
	subs	r2, r2, #32
	bge	.Lcblock
	ldmfd	sp!, {r4-r10}
.Lcword:
	adds	r2, r2, #32 - 4		@ r2 = bytes left - 4
	blt	.Lctail
.Lcword1:				@ then a word at a time
	ldr	r3, [r0], #4
	str	r3, [r1], #4
	subs	r2, r2, #4
	bge	.Lcword1
.Lctail:
	adds	r2, r2, #4		@ 0..3 bytes left
	bxeq	lr
.Lcbyte:
	ldrb	r3, [r0], #1
	strb	r3, [r1], #1
	subs	r2, r2, #1
	bne	.Lcbyte
	bx	lr
	.size	bcopy, . - bcopy

/* void bzero(void *dst, size_t n) */
	.global	bzero
	.type	bzero, %function
bzero:
	cmp	r1, #0
	bxeq	lr
	mov	r2, #0
.Lzalign:				@ bytes until word aligned
	tst	r0, #3
	beq	.Lzwords
	strb	r2, [r0], #1
	subs	r1, r1, #1
	bne	.Lzalign
	bx	lr
.Lzwords:
	subs	r1, r1, #32
	blt	.Lzword
	stmfd	sp!, {r4-r9}
	mov	r3, #0
	mov	r4, #0
	mov	r5, #0
	mov	r6, #0
	mov	r7, #0
	mov	r8, #0
	mov	r9, #0
.Lzblock:				@ 32 bytes at a time
	stmia	r0!, {r2-r9}
	subs	r1, r1, #32
	bge	.Lzblock
	ldmfd	sp!, {r4-r9}
.Lzword:
	adds	r1, r1, #32 - 4		@ r1 = bytes left - 4
	blt	.Lztail
.Lzword1:
	str	r2, [r0], #4
	subs	r1, r1, #4
	bge	.Lzword1
.Lztail:
	adds	r1, r1, #4		@ 0..3 bytes left
	bxeq	lr
.Lzbyte:
	strb	r2, [r0], #1
	subs	r1, r1, #1
	bne	.Lzbyte
	bx	lr
	.size	bzero, . - bzero
