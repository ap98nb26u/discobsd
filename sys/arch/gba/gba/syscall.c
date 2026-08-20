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

__attribute__((target("arm"), noinline))
void simulate_swi_via_inline_data(void) {
    __asm__ volatile (
        /* -------------------------------------------------------------
         1. 割り込みを最速で禁止 (REG_IME = 0)
         ------------------------------------------------------------- */
        "mov r1, #0x04000000\n\t"
        "add r1, #0x200\n\t"
        "mov r2, #0\n\t"
        "strh r2, [r1, #0x08]\n\t"    // REG_IME (0x04000208) に 0 を書き込み (16bit)

        /* -------------------------------------------------------------
         2. 汎用レジスタをスタックに退避
         ------------------------------------------------------------- */
        "stmdb sp!, {r0-r12}\n\t"     // R0〜R12、および呼び出し元の戻り先LRを退避
        
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
        
        /* -------------------------------------------------------------
         5. SVC（スーパーバイザ）モードへ強制移行
         ------------------------------------------------------------- */
        // ※ここではCPSRのIビット(割り込み禁止)も念のためセットしておきますが、
        // すでにREG_IMEが0なので、CPU全体で割り込みは完全に遮断されています。
        "msr cpsr_c, #0x93\n\t"       // SVCモード(0x13)へ移行 ＆ CPU側でもIRQ禁止
        
        /* -------------------------------------------------------------
         6. SVCモードのレジスタ（SPSR_svc, LR_svc）を設定
         ------------------------------------------------------------- */
        "msr spsr, r0\n\t"            // 元のCPSRをSPSRにセット
        "mov lr, r1\n\t"              // 修正済みの復帰先アドレスをLR_svcにセット
        
        /* -------------------------------------------------------------
         7. C言語のハンドラ関数を呼び出し
         ------------------------------------------------------------- */
        "mov r0, r2\n\t"              // 第1引数: SWI番号
        "mov r1, sp\n\t"              // 第2引数: 退避されたレジスタ配列へのポインタ
        //"bl my_custom_swi_handler\n\t"
        "bl syscall_handler\n\t"
        
        /* -------------------------------------------------------------
         8. レジスタの復元と「MOVS PC, LR」による復帰
         ------------------------------------------------------------- */
        // 最後に「movs pc, lr」が実行されると、SPSRがCPSRに書き戻されます。
        // 元のCPSR（割り込みが許可されていた状態）に戻ることで、自動的にCPU側の
        // 割り込みが許可されます。
        "ldmia sp!, {r0-r12}\n\t"     // R0〜R12を復元
        "add sp, sp, #4\n\t"          // スタックのLR領域をスキップ
        
        // 【注意点への対策】
        // この時点でCPUのCPSRはまだSVCモード（IRQ禁止）ですが、REG_IMEを有効に戻す
        // 必要があります。
        // 元々割り込みが許可されていた環境に戻すため、一足先にREG_IMEを1（有効）に
        // 戻します。
        "mov r1, #0x04000000\n\t"
        "add r1, #0x200\n\t"
        "mov r2, #1\n\t"
        "strh r2, [r1, #0x08]\n\t"   // REG_IME = 1 (割り込みマスタ許可)
        
        "movs pc, lr\n\t"             // 元のモード・状態・割り込み許可状態へ完全復帰
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
	int psig;
	time_t syst;
	int code;
	u_int sp;

	//volatile struct trapframe *f = frame;

	syst = u.u_ru.ru_stime;

	//if ((u_int)frame < (u_int)&u + sizeof(u)) {
	//	panic("stack overflow");
	//	/* NOTREACHED */
	//}

#ifdef UCB_METER
	cnt.v_trap++;
	cnt.v_syscall++;
#endif

	/* Enable interrupts. */
	(void)arm_intr_enable();

	u.u_error = 0;
	u.u_frame = frame;
	u.u_code = u.u_frame->tf_pc - INSN_SZ;	/* Syscall for sig handler. */

	//led_control(LED_KERNEL, 1);

	/* Check stack. */
	sp = u.u_frame->tf_sp;
	if (sp < u.u_procp->p_daddr + u.u_dsize) {
		/* Process has trashed its stack; give it an illegal
		 * instruction violation to halt it in its tracks. */
		psig = SIGSEGV;
		goto bad;
	}
	if (u.u_procp->p_ssize < (size_t)__user_data_end - sp) {
		/* Expand stack. */
		u.u_procp->p_ssize = (size_t)__user_data_end - sp;
		u.u_procp->p_saddr = sp;
		u.u_ssize = u.u_procp->p_ssize;
	}

	code = frame->tf_r7; // icode側でmov r7, #11としている

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

	if (setjmp(&u.u_qsave) == 0) {
		(*callp->sy_call)();		/* Make syscall. */

		/*
		 * execve(11)が成功した直後の処理
		 * execveから戻る際、新しいプログラム(init)のLRが
		 * カーネル内の古いアドレスを指していると、最初の
		 * 関数から戻るときに暴走するため初期化する。
		 */
		if (code == 11 && u.u_error == 0) {
			u.u_frame->tf_lr = 0;
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
	goto out;

bad:
	/* From this point and further the interrupts must be enabled. */
	psignal(u.u_procp, psig);

out:
	frame->tf_pc |= 1;
	frame->tf_psr |= 0x80;
	userret(u.u_frame->tf_pc, syst);

	//led_control(LED_KERNEL, 0);
}
