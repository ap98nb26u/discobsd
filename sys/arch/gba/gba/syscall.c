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

// GBAの割り込みマスタ有効化レジスタ (16bit)
#define REG_IME_ADDR 0x04000208

void my_custom_swi_handler(uint32_t swi_number, uint32_t *regs) {
    // ここに独自のSWI処理を記述（IRQは禁止されています）
    if (swi_number == 0x01) {
        // 例: カスタム処理
    }
}

static void debug_print_return_target(unsigned v)
{
    printf("DBG: syscall return, jumping to %x\n", v);
}

static void debug_print_sp_lr(unsigned sp, unsigned lr)
{
    printf("DBG: about to bx, sp=%x lr=%x\n", sp, lr);
}

/*
 * Scratch cell for stashing the jump target across the debug call
 * below, in memory rather than a register - AAPCS says r4-r11 must
 * survive a well-formed call, but this sidesteps having to trust that
 * across every function in the printf/uartputc chain.
 */
static volatile unsigned debug_target_scratch;

/*
 * Set by syscall_handler() only when execve() just replaced the calling
 * process's memory image - the one case where tf_ip/tf_sp (r12) legitimately
 * holds a new stack pointer (written by exec_setupstack() in exec_subr.c)
 * instead of CALL_SIMSYS's leftover trampoline jump-target scratch value.
 * simulate_swi_via_inline_data() below checks this before deciding whether
 * to switch the live sp to r12 on return.
 */
static volatile int gba_exec_switched_stack;

__attribute__((target("arm"), noinline))
void simulate_swi_via_inline_data(void) {
    __asm__ volatile (
        /* -------------------------------------------------------------
         1. struct trapframe一杯分(16ワード=64バイト)を確保し、
            r0〜r12、および呼び出し元の戻り先LRを先頭14ワードへ退避
         ------------------------------------------------------------- */
        // struct trapframeはtf_r0..tf_r11(12)+tf_ip(=r12)+tf_lr+tf_pc+tf_psrの
        // 16ワード。ここで14ワード分しか確保しないと、後段のsyscall_handler()が
        // 読み書きするtf_pc/tf_psr(offset56/60)が未確保領域になる。今回のように
        // frameがスタック上端付近(IWRAM終端0x03008000近く)にあるとIWRAMの物理範囲
        // を超えて書き込むことになり、実機とmGBAで挙動が分かれてハングしていた。
        //
        // レジスタ退避は他の何よりも先に行う。以前はこの前にREG_IME無効化の
        // コードがあり、そこでr1/r2を汎用レジスタとして使っていたため、
        // icode側がr1に積んだ第2引数(execvのargvポインタ等)がここで退避される
        // 前に0x04000200(REG_IME計算途中の値)へ上書きされてしまっていた。
        // fnameが正しくargpだけ壊れていたのはr0を触らずr1/r2だけ使っていたため。
        "sub sp, sp, #64\n\t"
        "stmia sp, {r0-r12, lr}\n\t"  // sp+0..sp+53 (14ワード、書き戻しなし)

        /* -------------------------------------------------------------
         2. 割り込みを禁止 (REG_IME = 0)
         ------------------------------------------------------------- */
        "mov r1, #0x04000000\n\t"
        "add r1, #0x200\n\t"
        "mov r2, #0\n\t"
        "strh r2, [r1, #0x08]\n\t"    // REG_IME (0x04000208) に 0 を書き込み (16bit)

        /* -------------------------------------------------------------
         3. 呼び出し元の状態（CPSR）を取得し、戻り先（LR_svc）を計算
         ------------------------------------------------------------- */
        "mrs r0, cpsr\n\t"            // 現在のCPSR（呼び出し元のモード情報含む）を取得
        "ldr r1, [sp, #52]\n\t"       // スタックから退避したLRを取得

        /* -------------------------------------------------------------
         4. 呼び出し元の命令状態（ARM/Thumb）を判定してSWI番号を取得
         ------------------------------------------------------------- */
        "tst r0, #0x20\n\t"           // Tビット（Thumbフラグ）をチェック
        "ldrneh r2, [r1]\n\t"         // 【Thumb】2バイトとしてSWI番号を読み込む
        "addne r1, r1, #2\n\t"        // 【Thumb】復帰先を2バイト進める
        "ldreq r2, [r1]\n\t"          // 【ARM】4バイトとしてSWI番号を読み込む
        "addeq r1, r1, #4\n\t"        // 【ARM】復帰先を4バイト進める

        "str r1, [sp, #52]\n\t"       // 修正済みの復帰先アドレスをスタックに書き戻す
        "str r1, [sp, #56]\n\t"       // tf_pc = 修正済みの復帰先アドレス
        "str r0, [sp, #60]\n\t"       // tf_psr = 呼び出し元のCPSR

        /* -------------------------------------------------------------
         5. C言語のハンドラ関数を呼び出し
         ------------------------------------------------------------- */
        // GBA移植では特権分離(SVCモード)を実装していない(USERMODE()は常に0)ため、
        // ここでモードを切り替えると sp/lr がバンクされた別レジスタに切り替わり、
        // 直後の「mov r1, sp」が今積んだr0-r12ではなく未初期化のsp_svc(BIOS既定値、
        // IWRAM上のu/u0領域のすぐ近く)を指してしまい、フレームポインタが完全に
        // 壊れる。呼び出し元と同じモード(SYS)のまま進める。
        "mov r0, r2\n\t"              // 第1引数: SWI番号
        "mov r1, sp\n\t"              // 第2引数: 退避されたレジスタ配列へのポインタ
        "bl syscall_handler\n\t"

        /* -------------------------------------------------------------
         6. レジスタと復帰先アドレスの復元
         ------------------------------------------------------------- */
        // bl syscall_handler 自体がlrを上書きするため、復帰先アドレスはスタックから
        // 読み直す。ここで必ずtf_pc(offset56)から読むこと - tf_lr(offset52)は
        // 呼び出し前にこちらが計算した「icode内の次の命令」のスナップショットの
        // ままだが、execve()成功時はexec_subr.c内のexec_setupstack()が
        // u.u_frame->tf_pcを新しいエントリポイントへ書き換える(syscall_handler
        // 末尾で frame->tf_pc |= 1 によりThumbビットも設定済み)。tf_lrから読むと
        // execveが成功してもicode自身に戻ってしまい、新しいプロセスへ絶対に
        // 遷移できない。
        "ldr r0, [sp, #56]\n\t"       // デバッグ用: sp/r0-r12はまだ壊さずtf_pcを覗く
        "bl debug_print_return_target\n\t"

        "ldmia sp!, {r0-r12}\n\t"     // R0〜R12を復元 (52バイト、sp+52=tf_lr位置)
        "add sp, sp, #4\n\t"          // tf_lrをスキップ (sp+56=tf_pc位置)
        "ldmia sp!, {lr}\n\t"         // tf_pc(更新されている場合あり)をlrへ復元
        "add sp, sp, #4\n\t"          // tf_psr分を破棄

        // tf_ip(=r12)は通常のレジスタだが、exec_setupstack()(exec_subr.c)は
        // 新しいプロセスの実際のユーザースタックポインタをtf_sp(=tf_ipの別名、
        // frame.hで#defineされている)へ書き込む。execve()成功直後に限り、
        // カーネル(=呼び出し元)のスタックから新しいユーザースタックへ実際に
        // 切り替える必要がある(execve成功後も画面に何も出ない原因だった)。
        //
        // 通常のシステムコール(CALL_SIMSYS経由)では、r12はこのトランポリン
        // 自身のジャンプ先アドレスを一時的に保持するスクラッチ値でしかなく、
        // 実際のスタックポインタではない。ここで無条件にmov sp, r12すると、
        // 直前のldmiaで既に正しく復元されているsp(呼び出し元が元々使っていた
        // 実スタック)をそのゴミ値で上書きしてしまい、execve以外の全ての
        // システムコール復帰後にスタックが破壊されていた
        // (fstat等の直後にJumped to invalid addressで落ちていた原因)。
        "ldr r5, =gba_exec_switched_stack\n\t"
        "ldr r5, [r5]\n\t"
        "cmp r5, #0\n\t"
        "beq 1f\n\t"
        "mov sp, r12\n\t"
        "1:\n\t"

        // 以前ここにsp/lr確認用のデバッグ出力があったが、r0-r12を正しく復元した
        // *あと*にr0/r1/r3/r5を退避せず上書きしたまま戻していなかった。r0は
        // システムコールの戻り値そのもの(fstat等の成功/失敗)なので、呼び出し元に
        // 渡る直前にspの値で上書きされ、戻り値を見て分岐するコードが誤動作していた
        // (fstat復帰後、呼び出し元が壊れた戻り値で誤った分岐をして暴走していた)。

        "mov r1, #0x04000000\n\t"
        "add r1, #0x200\n\t"
        "mov r2, #1\n\t"
        "strh r2, [r1, #0x08]\n\t"   // REG_IME = 1 (割り込みマスタ許可)

        "bx lr\n\t"                   // 呼び出し元へ復帰（モードは変えていないので
                                       // movs不要。interworkingのためbxを使用）
    );
}

int sys_write(const char *s)
{
    printf("%s", s);
    //for (int i=0; *(s+i)!=0; i++)
    //    cnputc(*(s+i));
    return 0;
}

int sys_read(char *buf, int size)
{
    printf("sys_read called\n");
    //MGBA_REG_DEBUG_FLAGS = 0x100|MGBA_LOG_INFO;
    return 0;
}

#if 0
int arch_syscall(int num)
{
    switch (num) {
    case 0:
        return sys_write();
    case 1:
        return sys_read();
    default:
        return -1;
    }
}
#endif

/*
 * SVC_Handler(frame)
 *	struct trapframe *frame;
 *
 * Exception handler entry point for system calls (via 'svc' instruction).
 * The real work is done in PendSV_Handler at the lowest exception priority.
 */
void
SVC_Handler(void)
{
#if 0
	/* Set a PendSV exception to immediately tail-chain into. */
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

	__DSB();
	__ISB();

	/* PendSV has lowest priority, so need to allow it to fire. */
	(void)spl0();
#endif // 0
}

/*
 * PendSV_Handler(frame)
 *	struct trapframe *frame;
 *
 * System call handler (via SVC_Handler pending a PendSV exception).
 * Save the processor state in a trap frame and pass it to syscall().
 * Restore processor state from returned trap frame on return from syscall().
 */
void
PendSV_Handler(void)
{
#if 0
__asm volatile (
"	.syntax	unified		\n\t"
"	.thumb			\n\t"

"	cpsid	i		\n\t"	/* Disable interrupts. */

	/*
	 * ARMv6-M hardware already pushed r0-r3, ip, lr, pc, psr on PSP,
	 * and then switched to MSP and is currently in Handler Mode.
	 */
"	mov	r0, r8		\n\t"	/* Bring high register v5 to low. */
"	mov	r1, r9		\n\t"	/* Bring high register v6 to low. */
"	mov	r2, r10		\n\t"	/* Bring high register v7 to low. */
"	mov	r3, r11		\n\t"	/* Bring high register v8 to low. */
"	push	{r0-r3}		\n\t"	/* Push v5-v8 registers onto MSP. */
"	push	{r4-r7}		\n\t"	/* Push v1-v4 registers onto MSP. */

"	mrs	r1, PSP		\n\t"	/* Get pointer to trap frame. */
"	mov	r2, r1		\n\t"	/* Pointer to use for top half. */
"	adds	r2, #(4 * 4)	\n\t"	/* Index to top half of trap frame. */
"	ldmfd	r2!, {r4-r7}	\n\t"	/* Copy frame top half from PSP. */
"	mov	r4, r1		\n\t"	/* Set trap frame sp as PSP. */
"	push	{r4-r7}		\n\t"	/* Push frame top half onto MSP. */
"	ldmfd	r1!, {r4-r7}	\n\t"	/* Copy frame low half from PSP. */
"	push	{r4-r7}		\n\t"	/* Push frame low half onto MSP. */

"	mrs	r0, MSP		\n\t"	/* MSP trap frame is syscall() arg. */
"	bl	syscall		\n\t"	/* Call syscall() with MSP as arg. */

"	pop	{r0-r7}		\n\t"	/* Pop off trap frame from MSP. */
"	msr	PSP, r4		\n\t"	/* Set PSP as trap frame sp. */
"	stmia	r4!, {r0-r3}	\n\t"	/* Copy trap frame low half to PSP. */
"	mrs	r1, PSP		\n\t"	/* Get PSP again as trap frame sp. */
"	stmia	r4!, {r1,r5-r7}	\n\t"	/* Copy trap frame top half to PSP. */

"	pop	{r4-r7}		\n\t"	/* Pop from MSP into v1-v4 regs. */
"	pop	{r0-r3}		\n\t"	/* Pop from MSP for v5-v8 regs. */
"	mov	r11, r3		\n\t"	/* Move low register to high v8. */
"	mov	r10, r2		\n\t"	/* Move low register to high v7. */
"	mov	r9, r1		\n\t"	/* Move low register to high v6. */
"	mov	r8, r0		\n\t"	/* Move low register to high v5. */

	/*
	 * On return, ARMv6-M hardware sets PSP as stack pointer,
	 * pops from PSP to registers r0-r3, ip, lr, pc, psr,
	 * and then switches back to Thread Mode (exception completed).
	 */
"	ldr	r1, =0xFFFFFFFD	\n\t"	/* EXC_RETURN Thread Mode, PSP */
"	mov	lr, r1		\n\t"	/* Return to Thread Mode. */
);
#endif // 0
}

void
syscall_handler(int sys_num, struct trapframe *frame)
{
	time_t syst;
	int code;

	//volatile struct trapframe *f = frame;

	printf("DBG: syscall_handler entry, sys_num=%d frame=%x\n",
	    sys_num, (unsigned)frame);

	syst = u.u_ru.ru_stime;

	/*
	 * Unlike STM32/pic32, GBA has no MMU and no separate always-in-IWRAM
	 * kernel stack: syscall_handler() runs on whatever stack the calling
	 * process is currently using. Once a process has exec'd, that's its
	 * own EWRAM stack (see the sp switch in simulate_swi_via_inline_data
	 * below __attribute__((target("arm")))), which is numerically always
	 * less than any IWRAM address - so comparing frame against &u+sizeof(u)
	 * (an IWRAM address) here would always look like an underflow for any
	 * real userland syscall. The actual per-process stack bounds check
	 * further down (against tf_sp/p_daddr/p_dsize) is the correct one for
	 * this architecture.
	 */

#ifdef UCB_METER
	cnt.v_trap++;
	cnt.v_syscall++;
#endif

	/* Enable interrupts. */
	(void)arm_intr_enable();

	u.u_error = 0;
	u.u_frame = frame;
	u.u_code = u.u_frame->tf_pc - INSN_SZ;	/* Syscall for sig handler. */
	gba_exec_switched_stack = 0;

	//led_control(LED_KERNEL, 1);

	/*
	 * Stack overflow/growth tracking (disabled on GBA): on stm32/pic32
	 * this reads tf_sp from a *real* hardware-saved SP (Cortex-M's PSP,
	 * captured by PendSV_Handler; MIPS's $sp, captured by the exception
	 * vector before switching stacks) - a legitimate live value there.
	 * On GBA, syscalls are dispatched through simulate_swi_via_inline_data,
	 * a hand-rolled function-call trampoline with no hardware exception
	 * entry. Userland's CALL_SIMSYS macro (lib/libc/arm/sys/SYS.h) uses
	 * r12 purely as scratch to hold the trampoline's own address before
	 * jumping, so tf_sp/tf_ip here is garbage for any syscall except the
	 * one boot-time execve() dispatched from icode (which happens to set
	 * r12 to a real value beforehand - see locore.S). This check was
	 * carried over from pic32/stm32 without accounting for that, and
	 * was corrupting p_saddr/p_ssize with the trampoline's own address.
	 *
	 * Known gap from disabling this: p_ssize/p_saddr stay fixed at
	 * whatever exec_setupstack() set (SSIZE, 2048 bytes) instead of
	 * tracking real growth, so vm_swap.c's swapout() would only save
	 * that initial region - if a process's stack ever grows past SSIZE
	 * and then gets swapped, the deeper part won't survive a swap back
	 * in. Not yet a problem since swap isn't exercised on this port yet;
	 * revisit if/when it is (would need CALL_SIMSYS to hand off the real
	 * SP some other way, since r12 is otherwise spoken for here).
	 */

	code = sys_num; /* Syscall number decoded from the inline .word by
			 * simulate_swi_via_inline_data(), not from a register. */

	//const struct sysent *callp = &sysent[0];
	const struct sysent *callp;

	if (code < nsysent)
		//callp += code;
		callp = sysent + code;

	if (callp->sy_narg) {
		/* In AAPCS, first four args are from trapframe regs r0-r3. */
		u.u_arg[0] = u.u_frame->tf_r0;	/* $a1 */
		u.u_arg[1] = u.u_frame->tf_r1;	/* $a2 */
		u.u_arg[2] = u.u_frame->tf_r2;	/* $a3 */
		u.u_arg[3] = u.u_frame->tf_r3;	/* $a4 */

		/* for ARM7TDMI */
		int stkalign = 0;

		/* Remaining args are from the stack, after the trapframe. */
		if (callp->sy_narg > 4) {
			u_int addr = (u.u_frame->tf_sp + 32 + stkalign) & ~3;
			if (!baduaddr((caddr_t)addr))
				u.u_arg[4] = *(u_int *)addr;
		}
		if (callp->sy_narg > 5) {
			u_int addr = (u.u_frame->tf_sp + 36 + stkalign) & ~3;
			if (!baduaddr((caddr_t)addr))
				u.u_arg[5] = *(u_int *)addr;
		}
	}

	u.u_rval = 0;

	printf("DBG: before setjmp qsave, callp=%x sy_call=%x\n",
	    (unsigned)callp, (unsigned)callp->sy_call);

	if (setjmp(&u.u_qsave) == 0) {
		printf("DBG: setjmp qsave=0, calling sy_call\n");
		(*callp->sy_call)();		/* Make syscall. */

		/*
		 * execve(11)が成功した直後の処理
		 * execveから戻る際、新しいプログラム(init)のLRが
		 * カーネル内の古いアドレスを指していると、最初の
		 * 関数から戻るときに暴走するため初期化する。
		 */
		if (code == 11 && u.u_error == 0) {
			u.u_frame->tf_lr = 0;
			gba_exec_switched_stack = 1;
		}
	}

	switch (u.u_error) {
	case 0:
		u.u_frame->tf_psr &= ~PSR_C;	/* Clear carry bit. */
		u.u_frame->tf_r0 = u.u_rval;	/* $a1 - result. */
		break;
	case ERESTART:
		u.u_frame->tf_pc -= INSN_SZ;	/* Return to svc syscall. */
		break;
	case EJUSTRETURN:			/* Return from sig handler. */
		break;
	default:
		u.u_frame->tf_psr |= PSR_C;	/* Set carry bit. */
		u.u_frame->tf_r0 = u.u_error;	/* $a1 - result. */
		break;
	}
out:
	/*
	 * Only force the Thumb bit for the just-succeeded execve() case:
	 * tf_pc there is the new process's entry point, and crt0 is always
	 * compiled Thumb. Every ordinary syscall wrapper on this port (the
	 * GBA branch of SYS.h's ENTRY macro, and icode in locore.S) is
	 * written in ARM (.arm), so the trampoline's computed return address
	 * always points back into ARM code. Forcing the Thumb bit there
	 * unconditionally made the CPU decode those ARM instruction bytes as
	 * Thumb after every ordinary syscall returned - harmless-looking for
	 * a few instructions by chance, but eventually wandering off into
	 * whatever those bytes happened to decode to (observed: execution
	 * ending up inside a .rodata table after sigaction() returned).
	 */
	if (gba_exec_switched_stack)
		frame->tf_pc |= 1;
	frame->tf_psr |= 0x80;
	userret(u.u_frame->tf_pc, syst);

	//led_control(LED_KERNEL, 0);
}
