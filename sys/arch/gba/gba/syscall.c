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

#include <sys/param.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>

#include <machine/frame.h>
#include <machine/gba_syscall.h>
#include <gba/dev/mgbalog.h>

#include <sys/stdint.h>

/*
 * Holds tf_psr (the CPSR syscall_handler computed, with PSR_C set/cleared
 * to reflect success/failure) across the r0-r12 restore below, so it can
 * be applied to the real CPSR right before the final bx r12 - see the
 * comment at the store/restore sites in simulate_swi_via_inline_data().
 */
static volatile unsigned saved_tf_psr;

/*
 * The trapframe lives on the kernel stack (UAREA, just below u_end), not
 * on the caller's stack.  The old and the new image's stacks both end at
 * __user_data_end, and execve()'s exec_clear() zeroes the new one: run on
 * the user stack, it cleared its own live locals (/bin/sh's exec hung in
 * exec_clear()).
 *
 * The caller's sp is kept in the frame's tf_ip (r12) slot, not in a
 * global: a fork()ed child resumes much later, through a longjmp in
 * newproc(), after other processes' syscalls have overwritten any single
 * global (it hung right after resuming).  The frame is in the u area,
 * which swtch() switches with the process.  The caller's r12 itself need
 * not survive: the syscall sequence below only carries the jump target
 * in it.
 */

/*
 * Every user syscall is the same sequence (CALL_SIMSYS in
 * lib/libc/arm/sys/SYS.h, sigtramp in sigaction.S, and the native
 * toolchain's usr.bin/as/arm-dev/rt/clib.s):
 *
 *	ldr	r12, =SYSCALL_VECTOR_ADDR
 *	ldr	r12, [r12]
 *	mov	lr, pc
 *	bx	r12
 *	.word	SYS_n
 *
 * tf_pc points just past the .word, so restarting a syscall goes back
 * 20 bytes, to the first ldr.  INSN_SZ (one Thumb svc) is the stm32
 * value and is wrong here: going back 2 bytes ran the .word as an
 * instruction instead, and the syscall returned -1 with errno 0.
 */
#define	SYSCALL_SEQ_SZ	20

/*
 * The CONCEPT of a software-simulated SWI - userland calls the kernel through
 * a pointer at a fixed RAM address (SYSCALL_VECTOR_ADDR) because the real GBA
 * SWI vector at 0x08 is unwritable BIOS ROM - is inspired by the Prex Game Boy
 * Advance port's approach. The implementation below is this port's own (no Prex
 * code is used). See the BIOS/SWI section of sys/arch/gba/NOTES.hardware.
 */
/*
 * naked: without this, GCC emits its own "push {fp}" prologue for this
 * function (frame-pointer setup) - but the inline asm below returns by
 * jumping directly via "bx lr" at the very end, never falling through to
 * the compiler-generated epilogue that would "pop {fp}" to match. That
 * silently leaked 4 bytes of stack on every single syscall: the caller's
 * own pushed lr (CALL_SIMSYS's "push {lr}"/"pop {lr}" pairing, see SYS.h)
 * ended up 4 bytes below where the trampoline's return actually left sp,
 * so "pop {lr}" read whatever unrelated word sat above it (typically 0)
 * instead of the real return address - observed as fstat() returning via
 * "bx lr" with lr=0, jumping straight into the BIOS reset vector. naked
 * guarantees no prologue/epilogue exists to get bypassed like this.
 */
__attribute__((target("arm"), naked))
void simulate_swi_via_inline_data(void) {
    __asm__ volatile (
        /* -------------------------------------------------------------
         1. Reserve a whole struct trapframe (16 words, 64 bytes) and
            save r0-r12 and the caller's return lr in its first 14.
         ------------------------------------------------------------- */
        // struct trapframe is tf_r0..tf_r11 (12) + tf_ip (r12) + tf_lr +
        // tf_pc + tf_psr = 16 words.  Reserving only 14 left tf_pc/tf_psr
        // (offsets 56/60), which syscall_handler() reads and writes, outside
        // the frame - past the end of IWRAM when the frame sat near its top
        // (0x03008000), which hung real hardware and mGBA differently.
        //
        // Save the registers before anything else: code that turned
        // REG_IME off used to come first and used r1/r2 as scratch,
        // overwriting the second argument icode passes in r1 (execv's argv)
        // with 0x04000200 before it was saved.
        "sub sp, sp, #64\n\t"
        "stmia sp, {r0-r12, lr}\n\t"  // sp+0..sp+55 (14 words, no writeback)

        /* -------------------------------------------------------------
         1.5. Move the trapframe to the kernel stack (UAREA, just below
              u_end).  syscall_handler() and everything it calls (e.g.
              execve()'s exec_clear()/exec_setupstack()) then run on that
              stack.  Left on the caller's user stack, the frame and the
              call chain's locals would be cleared by execve()'s
              bzero() of the new image's stack, which always sits at the
              same fixed top (__user_data_end/u_end) - seen as /bin/sh's
              exec hanging in exec_clear() (see the comment at the top
              of this file).
         ------------------------------------------------------------- */
        "mov r0, sp\n\t"               // r0 = source: the frame on the caller's stack
        "add r5, r0, #64\n\t"          // r5 = the caller's own sp (before sub #64),
                                        // in r5 since the copy loop uses r0-r4.

        "ldr r1, =u_end\n\t"
        "sub r1, r1, #64\n\t"          // r1 = destination: the frame on the kernel stack
        "mov r2, r1\n\t"               // r2 = its start, the sp to switch to
        "mov r3, #16\n\t"              // 64 bytes = 16 words
        "9:\n\t"
        "ldr r4, [r0], #4\n\t"
        "str r4, [r1], #4\n\t"
        "subs r3, r3, #1\n\t"
        "bne 9b\n\t"

        // Keep the caller's sp (r5) in the copied frame's tf_ip (r12) slot,
        // offset 48.  r12 only carried the jump target, so the slot is
        // free.  In the frame rather than a global, the value follows its
        // process (see the comment at the top of this file).
        "str r5, [r2, #48]\n\t"

        "mov sp, r2\n\t"               // from here on, the frame on the kernel stack

        /* -------------------------------------------------------------
         2. Interrupts off (REG_IME = 0)
         ------------------------------------------------------------- */
        "mov r1, #0x04000000\n\t"
        "add r1, #0x200\n\t"
        "mov r2, #0\n\t"
        "strh r2, [r1, #0x08]\n\t"    // REG_IME (0x04000208) = 0 (16-bit)

        /* -------------------------------------------------------------
         3. Get the caller's CPSR and its return address
         ------------------------------------------------------------- */
        "mrs r0, cpsr\n\t"            // current CPSR (the caller's mode)
        "ldr r1, [sp, #52]\n\t"       // the saved lr

        /* -------------------------------------------------------------
         4. Read the SWI number inline, as ARM or Thumb per the caller
         ------------------------------------------------------------- */
        "tst r0, #0x20\n\t"           // T bit (Thumb state)
        "ldrneh r2, [r1]\n\t"         // Thumb: 2-byte SWI number
        "addne r1, r1, #2\n\t"        // Thumb: return past it
        "ldreq r2, [r1]\n\t"          // ARM: 4-byte SWI number
        "addeq r1, r1, #4\n\t"        // ARM: return past it

        "str r1, [sp, #52]\n\t"       // the adjusted return address, back in the frame
        "str r1, [sp, #56]\n\t"       // tf_pc = the adjusted return address
        "str r0, [sp, #60]\n\t"       // tf_psr = the caller's CPSR

        /* -------------------------------------------------------------
         5. Call the C handler
         ------------------------------------------------------------- */
        // This port has no privilege separation (user code runs in SYS
        // mode too): switching to SVC mode here would bank sp/lr, and
        // "mov r1, sp" below would get the uninitialized sp_svc instead of
        // the frame.
        // Stay in the caller's mode (SYS).
        //
        // Do not turn interrupts back on yet: this naked function keeps live
        // values in r0-r2 across several instructions, and an interrupt
        // handler (gba_do_schedule, ...) following the C convention may
        // clobber them - tried once, syscall_handler() got a garbage SWI
        // number and crashed.  Interrupts are re-enabled where no live
        // registers are held: in idle() (machdep.c), while no process runs.
        "mov r0, r2\n\t"              // 1st argument: the SWI number
        "mov r1, sp\n\t"              // 2nd argument: the saved registers
        "bl syscall_handler\n\t"

        /* -------------------------------------------------------------
         6. Restore the registers and return
         ------------------------------------------------------------- */
        // bl syscall_handler overwrote lr, so the return address comes from
        // the frame - from tf_pc (offset 56), never tf_lr (52): a successful
        // execve() sets tf_pc to the new entry point (exec_setupstack() in
        // exec_subr.c), and returning through tf_lr would go back into the
        // old image.

        // Keep tf_psr (the CPSR for the caller, PSR_C set or cleared by
        // syscall_handler() for failure or success); r0 is reloaded by the
        // ldmia below anyway.  The caller's SYS() wrapper tests the carry
        // ("bcc 2f") after return, and the cmp/tst instructions on the way
        // here change the real flags: without restoring them every syscall
        // looked like a failure and returned -1.
        "ldr r0, [sp, #60]\n\t"
        "ldr r1, =saved_tf_psr\n\t"
        "str r0, [r1]\n\t"

        // Turn interrupts back on and restore the flags here, while r1/r2/r5
        // are still free scratch, so nothing after the ldmia below changes a
        // register.  This used to come after the ldmia and clobber r1/r2 -
        // harmless for ordinary syscalls, but a successful execve() passes
        // argc/argv/envp in r0-r2 to the new program's crt0, and argv
        // arrived as 0x04000200 (init's argv[1] read as NULL on hardware).
        "mov r1, #0x04000000\n\t"
        "add r1, #0x200\n\t"
        // A Timer0 interrupt pending here fell due inside the syscall; it
        // is taken as soon as IME goes on, with a user pc. Note it so the
        // clock charges that tick to the system (gba_do_schedule()).
        "ldrh r2, [r1, #0x02]\n\t"   // REG_IF
        "and r2, r2, #8\n\t"         // IRQ_TIMER0
        "ldr r0, =gba_tick_in_kernel\n\t"
        "str r2, [r0]\n\t"
        "mov r2, #1\n\t"
        "strh r2, [r1, #0x08]\n\t"   // REG_IME = 1 (interrupts on)

        // Restore the flags syscall_handler() computed (cpsr_f writes only
        // the condition flags, not the mode or the interrupt masks).
        "ldr r5, =saved_tf_psr\n\t"
        "ldr r5, [r5]\n\t"
        "msr cpsr_f, r5\n\t"

        // Return to user space; sp still points at the frame
        // (tf_r0=0 .. tf_ip=48, tf_lr=52, tf_pc=56, tf_psr=60).
        //
        // Restore tf_lr into lr rather than dropping it: sendsig()
        // (sig_machdep.c) sets tf_lr = u.u_sigtramp so a signal handler
        // returns through sigtramp to sigreturn; dropping it made caught
        // signals (SIGINT, SIGALRM, ...) return into garbage.  Branch to
        // tf_pc through r12, which the calling convention lets us clobber
        // (the user's sp is in tf_ip, and r12 was given up in step 1.5);
        // load sp from tf_ip last.
        "ldr r12, [sp, #56]\n\t"      // r12 = tf_pc (branch target)
        "ldr lr,  [sp, #52]\n\t"      // lr  = tf_lr (the user's lr, or sigtramp)
        "ldmia sp, {r0-r11}\n\t"      // r0-r11 = tf_r0..tf_r11 (no writeback)
        "ldr sp,  [sp, #48]\n\t"      // sp  = tf_ip, the stack to return to
        "bx r12\n\t"                  // return; bit 0 of tf_pc selects ARM/Thumb
    );
}

#ifdef SERIAL_CONSOLE
/*
 * A syscall runs with interrupts off, so while /dev/sio is open (and its
 * fast FIFO polling is on) empty the SIO receive FIFO on the way in and
 * out of each one: the 4-byte FIFO overruns after ~350 us at 115200.
 * See gsio_fastpoll() in gba_sio_uart.c.
 */
extern int gsio_fastpoll_on;
extern void gsio_rx_service(void);
#define SIO_KEEPALIVE()	do { if (gsio_fastpoll_on) gsio_rx_service(); } while (0)
#else
#define SIO_KEEPALIVE()	do { } while (0)
#endif

void
syscall_handler(int sys_num, struct trapframe *frame)
{
	time_t syst;
	int code;

	SIO_KEEPALIVE();

	syst = u.u_ru.ru_stime;

#ifdef UCB_METER
	cnt.v_trap++;
	cnt.v_syscall++;
#endif

	u.u_error = 0;
	u.u_frame = frame;
	/* For a signal handler's code argument: where the syscall starts. */
	u.u_code = u.u_frame->tf_pc - SYSCALL_SEQ_SZ;

	/*
	 * Grow the user stack to cover the live stack pointer, mirroring the
	 * pic32 port's exception.c (which does exactly this on every kernel
	 * entry). GBA has no MMU and no stack-fault growth: the whole user
	 * window is directly accessible, so a deep user stack never
	 * faults and, without this, p_ssize would stay frozen at the exec-time
	 * SSIZE. swapout()/swapin() (vm_swap.c) then save/restore only p_ssize
	 * bytes from the stack top, truncating a deeper stack across a swap - a
	 * shell recursing to build a >=6-stage pipeline lost its deepest stage's
	 * frame and silently produced 0 bytes.
	 * This port only ever swaps a user process out from inside the kernel
	 * (tsleep, or the runrun reschedule at trap return, kern_sig.c), and the
	 * user sp is frozen for the duration of the trap, so refreshing p_ssize
	 * here captures the true depth before any swapout. Keeping p_saddr = sp
	 * is essential: swapout() writes p_ssize bytes starting at p_saddr, so a
	 * grown p_ssize left with a stale p_saddr would save the wrong region.
	 */
	{
		size_t sp = (size_t)u.u_frame->tf_sp;

		if (sp < u.u_procp->p_daddr + u.u_dsize) {
			/* Stack has run into the data segment: a real overflow. */
			psignal(u.u_procp, SIGSEGV);
		} else if (u.u_procp->p_ssize < USER_DATA_END - sp) {
			u.u_procp->p_ssize = USER_DATA_END - sp;
			u.u_procp->p_saddr = sp;
			u.u_ssize = u.u_procp->p_ssize;
		}
	}

	code = sys_num; /* Syscall number decoded from the inline .word by
			 * simulate_swi_via_inline_data(), not from a register. */

	/* Out of range: sysent[0], which is nosys(). */
	const struct sysent *callp = &sysent[0];

	if (code < nsysent)
		callp += code;

	if (callp->sy_narg) {
		/* In AAPCS, first four args are from trapframe regs r0-r3. */
		u.u_arg[0] = u.u_frame->tf_r0;	/* $a1 */
		u.u_arg[1] = u.u_frame->tf_r1;	/* $a2 */
		u.u_arg[2] = u.u_frame->tf_r2;	/* $a3 */
		u.u_arg[3] = u.u_frame->tf_r3;	/* $a4 */

		/*
		 * The 5th and 6th args are on the caller's stack, per AAPCS:
		 * at the sp the libc wrapper had on entry, which is tf_ip + 4
		 * (tf_ip is the sp after the wrapper's "push {lr}").  Computing
		 * this from the frame's address broke once the frame moved to
		 * the kernel stack ("init: cannot get kernel security level"
		 * on every boot: sysctl()'s 5th/6th args were garbage).
		 */
		if (callp->sy_narg > 4) {
			u_int addr = (u_int)frame->tf_ip + 4;
			if (!baduaddr((caddr_t)addr))
				u.u_arg[4] = *(u_int *)addr;
		}
		if (callp->sy_narg > 5) {
			u_int addr = (u_int)frame->tf_ip + 4 + 4;
			if (!baduaddr((caddr_t)addr))
				u.u_arg[5] = *(u_int *)addr;
		}
	}

	u.u_rval = 0;

	if (setjmp(&u.u_qsave) == 0) {
		(*callp->sy_call)();		/* Make syscall. */

		/*
		 * After a successful exec, replace the lr and psr that the MI
		 * exec_setupstack() set on its __thumb__ path: those are
		 * Cortex-M (stm32) values - lr 0xffffffff and the EPSR Thumb
		 * bit, which is a reserved bit in the ARM7TDMI CPSR. Here the
		 * entry state comes from bit 0 of tf_pc, and only the
		 * condition flags of tf_psr reach the CPU, so start the new
		 * program with lr 0 and clear flags. Both execv (11, icode's
		 * hand-made call at boot) and execve (59, how /sbin/init
		 * starts everything else).
		 */
		if ((code == 11 || code == 59) && u.u_error == 0) {
			u.u_frame->tf_lr = 0;
			u.u_frame->tf_psr = 0;
		}
	}

	switch (u.u_error) {
	case 0:
		u.u_frame->tf_psr &= ~PSR_C;	/* Clear carry bit. */
		u.u_frame->tf_r0 = u.u_rval;	/* $a1 - result. */
		break;
	case ERESTART:
		u.u_frame->tf_pc -= SYSCALL_SEQ_SZ;	/* Redo the syscall. */
		break;
	case EJUSTRETURN:			/* Return from sig handler. */
		break;
	default:
		u.u_frame->tf_psr |= PSR_C;	/* Set carry bit. */
		u.u_frame->tf_r0 = u.u_error;	/* $a1 - result. */
		break;
	}
	SIO_KEEPALIVE();
	/*
	 * The return to the user ("bx r12" on tf_pc) takes ARM or Thumb from
	 * bit 0 of tf_pc, so nothing here sets the Thumb bit.  After an
	 * execve(), tf_pc is the new program's entry point: odd for a
	 * gcc-built (Thumb) crt0, even for one from the native ARM
	 * toolchain.  Ordinary syscalls return to the libc wrappers, which
	 * are ARM.  Forcing the Thumb bit, as this once did, entered ARM
	 * programs and wrappers in Thumb state.
	 *
	 * Only the condition flags of tf_psr reach the CPSR (msr cpsr_f in
	 * simulate_swi_via_inline_data()).
	 */

#ifdef DIAGNOSTIC
	/*
	 * Without an MMU a corrupted return state goes unnoticed until the
	 * jump: check, while the trapframe is still at hand, that the
	 * process resumes inside its own memory (text+data+bss+stack).
	 */
	{
		extern char __user_data_start[], __user_data_end[];
		extern char gba_preempt_tramp_ret[];
		unsigned lo = (unsigned)__user_data_start;
		unsigned hi = (unsigned)__user_data_end;
		unsigned pc = frame->tf_pc & ~1;
		unsigned rsp = frame->tf_ip;

		/* gba_preempt_tramp (locore.S) resumes the user code itself. */
		if ((pc < lo || pc >= hi) &&
		    pc != (unsigned)gba_preempt_tramp_ret)
			goto bad;
		if (rsp < lo || rsp > hi) {
bad:
			printf("syscall %d: pid %d returns outside user memory:"
			    " pc=%x sp=%x r0=%x r1=%x r2=%x r3=%x lr=%x psr=%x\n",
			    sys_num, u.u_procp->p_pid,
			    (unsigned)frame->tf_pc, rsp,
			    (unsigned)frame->tf_r0, (unsigned)frame->tf_r1,
			    (unsigned)frame->tf_r2, (unsigned)frame->tf_r3,
			    (unsigned)frame->tf_lr, (unsigned)frame->tf_psr);
		}
	}
#endif

	userret(u.u_frame->tf_pc, syst);
}
