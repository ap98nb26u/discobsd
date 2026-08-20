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

#include <syscall.h>

#ifdef MACHINE_GBA
#include <machine/machparam.h>
#endif

#ifndef MACHINE_GBA

#define	ENTRY(x) \
	.text; \
	.align	2; \
	.thumb_func; \
	.globl	x; \
	.type	x, %function; \
x:

#define	END(x) \
	.size	x, . - x

#define	SYS(x) \
	ENTRY(x); \
	svc	#SYS_##x; \
	bcs	1f; \
	bx	lr; \
1:	ldr	r1, =errno; \
	str	r0, [r1]; \
	mov	r0, #0; \
	mvn	r0, r0; \
	bx	lr; \
	END(x)

#else /* GBA */

#define	ENTRY(x) \
	.text; \
	.align	2; \
	.globl	x; \
	.type	x, %function; \
	.arm; \
x:

#define	END(x) \
	.pool; \
	.thumb; \
	.size	x, . - x

#define CALL_SIMSYS(sys_no) \
	ldr	r12, =SYSCALL_VECTOR_ADDR; \
	ldr	r12, [r12]; \
	mov	lr, pc; \
	bx	r12; \
	.word	sys_no

#define SYS(x) \
	ENTRY(x); \
	CALL_SIMSYS(SYS_##x); \
	bcc	2f; \
	ldr	r1, =errno; \
        str	r0, [r1]; \
        mov	r0, #0; \
        mvn	r0, r0; \
2:	bx	lr; \
	END(x)

#endif
