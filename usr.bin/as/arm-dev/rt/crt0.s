@ crt0.s - ARM (a.out) C runtime startoff for DiscoBSD/GBA, native toolchain.
@ Entered by the kernel with r0=argc, r1=argv, r2=envp, sp=user stack.
@ _start is ARM and links at an EVEN address, so ISA-aware exec enters ARM.
	.arm
	.text
	.globl	_start
_start:
	ldr	r3, =environ
	str	r2, [r3]		@ environ = envp
	@ r0=argc, r1=argv, r2=envp are already the args for main(argc,argv,envp)
	bl	main
	bl	_exit			@ _exit(main's return); does not return
.Lhang:
	b	.Lhang
	.ltorg

	.data
	.globl	environ
environ:
	.word	0
	.globl	__progname
__progname:
	.word	0
