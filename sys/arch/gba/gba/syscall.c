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


/*
 * Holds tf_psr (the CPSR syscall_handler computed, with PSR_C set/cleared
 * to reflect success/failure) across the r0-r12 restore below, so it can
 * be applied to the real CPSR right before the final bx lr - see the long
 * comment at the store/restore sites in simulate_swi_via_inline_data().
 */
static volatile unsigned saved_tf_psr;

/*
 * Why the trapframe now lives on a dedicated kernel stack (UAREA, u/u_end)
 * instead of directly on top of whatever sp the caller had: it used to be
 * built on the calling process's OWN user-mode stack, and syscall_handler()
 * ran on it unmodified - meaning every syscall's C call chain, including
 * execve()'s exec_clear()/exec_setupstack(), executed on the SAME memory
 * that exec_clear() then bzero()s out as the new image's stack (both old
 * and new stacks are always pinned to the same fixed top address,
 * __user_data_end/u_end - see kern.ldscript). Once a new image's stack
 * needed clearing more bytes than the current call depth had *not yet*
 * used, the bzero loop marched forward and overwrote its own live locals
 * (including epp/exec_params itself) out from under itself - observed as
 * /bin/sh's exec hanging inside exec_clear()'s bzero(stack) with a
 * corrupted, effectively-infinite byte count, while /sbin/init's much
 * shallower boot-time exec happened not to reach far enough to clobber
 * anything load-bearing. Fixed by relocating the entire trapframe onto
 * the kernel stack for the whole syscall body (the same physical stack
 * every other port's syscall entry, and this port's own boot-time
 * main()/icode jump in locore0.S, already uses).
 *
 * The caller's real sp - needed to restore it verbatim on a normal
 * return - is stashed directly in the trapframe's own tf_ip (r12) slot
 * rather than a separate global, and deliberately NOT restored to its
 * original r12 value: a first attempt used a "saved_user_sp" global
 * instead, which broke fork()'s child - the child doesn't resume through
 * this trampoline's normal call/return at all, but via a setjmp/longjmp
 * captured deep inside newproc() (see kern_fork.c) that gets replayed
 * whenever the scheduler eventually swaps it in, arbitrarily later and
 * interleaved with any number of *unrelated* processes' own syscalls in
 * between - each of which stomped the single shared global with its own
 * caller's sp before the fork()ed child ever got a chance to read it
 * back, observed as both mGBA and real GBAED hardware hanging completely
 * silent (no more hardclock ticks - IME never got re-enabled) right
 * after "newproc resumed via longjmp (child, pid=N)". The trapframe
 * itself doesn't have this problem: it lives on the kernel stack, which
 * IS part of the per-process "u" struct the swtch()/resume() u/u0
 * ping-pong already swaps wholesale on every context switch (see the
 * long comment on U0AREA/UAREA in kern.ldscript) - so stashing the value
 * there instead makes it correctly follow whichever process it belongs
 * to, no matter how much unrelated activity happens before that process
 * next resumes. tf_ip's original incoming r12 doesn't need preserving:
 * per the existing gba_exec_switched_stack comment below, CALL_SIMSYS
 * (lib/libc/arm/sys/SYS.h) only ever puts its own disposable jump-target
 * scratch there, never anything a caller reads back.
 */

/*
 * Set by syscall_handler() only when execve() just replaced the calling
 * process's memory image - the one case where tf_ip/tf_sp (r12) legitimately
 * holds a new stack pointer (written by exec_setupstack() in exec_subr.c)
 * instead of CALL_SIMSYS's leftover trampoline jump-target scratch value.
 * simulate_swi_via_inline_data() below checks this before deciding whether
 * to switch the live sp to r12 on return.
 */
static volatile int gba_exec_switched_stack;

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
         1.5. トラップフレームをカーネルスタック(UAREA, u_end手前)へ
              退避する。このあとsyscall_handler()とその呼び出し先
              (execve()のexec_clear()/exec_setupstack()等)は全て
              *ここに切り替えた後の* spの上で動く。呼び出し元自身の
              ユーザースタック上にフレームを置いたままにしておくと、
              execve()が新イメージのスタック領域(常に同じ固定終端
              __user_data_end/u_endに置かれる)をbzero()でクリアする
              際、まだ実行中のこの呼び出しチェーン自身(このフレーム
              やepp/exec_paramsのローカル変数を含む)を踏み潰してし
              まう - 実際に/bin/shのexecでexec_clear()のbzero(stack)
              が巨大化けした長さでハングする形で再現した(このファイル
              冒頭のコメント参照)。
         ------------------------------------------------------------- */
        "mov r0, sp\n\t"               // r0 = コピー元(呼び出し元スタック上の現フレーム)
        "add r5, r0, #64\n\t"          // r5 = 呼び出し元の本来のsp(sub #64より前の値)。
                                        // コピーループでr0-r4を使い切るのでr5に温存する。

        "ldr r1, =u_end\n\t"
        "sub r1, r1, #64\n\t"          // r1 = コピー先(カーネルスタック上のフレーム位置)
        "mov r2, r1\n\t"               // r2 = コピー先の先頭(切替後のsp値として温存)
        "mov r3, #16\n\t"              // 64バイト = 16ワード
        "9:\n\t"
        "ldr r4, [r0], #4\n\t"
        "str r4, [r1], #4\n\t"
        "subs r3, r3, #1\n\t"
        "bne 9b\n\t"

        // 呼び出し元の本来のsp(r5)を、コピー後のフレームのtf_ip(=r12)
        // スロット(オフセット48)へ書き込む。通常のシステムコールでは
        // r12は使い捨てのスクラッチ値でしかないので、ここで温存用途に
        // 転用しても安全(下のgba_exec_switched_stackの説明コメント参照)。
        // グローバル変数(saved_user_sp)ではなくフレーム自身に持たせる
        // ことで、fork()の子プロセスがずっと後でlongjmp経由で復帰した
        // ときも正しい値が付いてくる - フレームはカーネルスタック上に
        // あり、u/u0の切り替えでプロセスごとに正しくスワップされる
        // ため(詳細はファイル冒頭のコメント参照)。
        "str r5, [r2, #48]\n\t"

        "mov sp, r2\n\t"               // ここからカーネルスタック上のフレームを使う

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
        //
        // 注意: REG_IME=0はここではまだ解除しない。以前ここで
        // syscall_handler呼び出し直前にREG_IME=1へ戻す変更を試したが、
        // このnaked関数はr0-r2をSWI番号/復帰先などの生きたスクラッチとして
        // 複数命令にまたがって保持しており、通常のC呼び出し規約(呼び出しの
        // たびにr0-r3は破棄されて構わない)を前提にしたISR(gba_do_schedule等)
        // がここで割り込むとr0-r2が破壊され、ゴミのSWI番号でsyscall_handlerが
        // 呼ばれて即クラッシュした(GDBでr0=0x4000000等になっているのを確認)。
        // select()のブロック対策としての割り込み再許可は、生きたレジスタ状態を
        // 抱えていない安全な場所(swtch()がidle()を呼ぶ直前、通常のC関数呼び出し
        // 境界)で行う(kern_synch.c参照)。
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

        // tf_psr(呼び出し元へ返すべきCPSR、syscall_handler側でPSR_Cを成功/失敗に
        // 応じて設定・クリア済み)をここで退避する。r0はこの直後のldmiaでどうせ
        // 上書きされるので自由に使える。呼び出し元(SYS.hのSYS()マクロが生成する
        // 各ラッパー)は復帰後に「bcc 2f」でキャリーフラグを見て成功/失敗を判定する
        // が、以前はtf_psrを一切CPUの実CPSRへ反映しておらず、少し下のexec切替判定
        // の「cmp r5, #0」がr5=0(execve以外は常にそう)のたびにキャリーを1(セット)
        // にしてしまっていた。その結果、あらゆる通常システムコールが成功していても
        // 呼び出し元からは「エラー」にしか見えず、戻り値がすべて-1にすり替わって
        // いた(fstat以外の全ての戻り値を使う呼び出し元が誤動作していた実際の原因)。
        "ldr r0, [sp, #60]\n\t"
        "ldr r1, =saved_tf_psr\n\t"
        "str r0, [r1]\n\t"

        // REG_IME再許可とCPSRフラグ復元は、以前は下のldmia(r0-r12の本復元)
        // の*あと*に置かれていて、r1/r2をスクラッチとして使い潰したまま
        // 一切元に戻していなかった(r5だけは別の一時変数経由で復元する
        // 特別扱いがあったが、r1/r2にはその対応がなかった)。通常のシステム
        // コールは呼び出し元がr0(戻り値)以外の生存を仮定しないため気付かれずに
        // 済んでいたが、execve()成功時だけは別 - exec_setupstack()
        // (exec_subr.c)がr0/r1/r2にargc/argv/envpを積んで新プロセスの
        // エントリポイント(crt0の_start)へ渡す契約になっており、この
        // 破壊によりargv(r1)が本来のargpではなく「REG_IME再許可コードが
        // 計算した0x04000200(I/Oレジスタアドレス)」にすり替わっていた
        // (initのmain()でargv[1]が実際にNULLになる形で顕在化・実機で確認)。
        // ここでは逆に、r0-r12がまだ本復元されていない今のうちに
        // r1/r2/r5を自由なスクラッチとして使い切ってしまうことで、
        // 下のldmia以降は本当に一切のレジスタを壊さないようにする。
        "mov r1, #0x04000000\n\t"
        "add r1, #0x200\n\t"
        "mov r2, #1\n\t"
        "strh r2, [r1, #0x08]\n\t"   // REG_IME = 1 (割り込みマスタ許可)

        // 呼び出し元のSYS()マクロ生成ラッパーは復帰後「bcc 2f」でキャリーフラグを
        // チェックして成功/失敗を判定する。ここまでに来る間の複数のcmp/tst命令が
        // 実CPSRのフラグを勝手に書き換えているため、bx lrの直前でsyscall_handlerが
        // 計算したtf_psr(のフラグ部分)を明示的に復元しないと、その判定は常に無関係な
        // 値を見ることになる。cpsr_fはフラグビットのみ書き換え、モード/割り込み
        // マスクなどの制御ビットには触れない。
        "ldr r5, =saved_tf_psr\n\t"
        "ldr r5, [r5]\n\t"
        "msr cpsr_f, r5\n\t"

        // ユーザーランドへ復帰する。ここではspはまだフレーム先頭を指す
        // (tf_r0=0 .. tf_ip=48, tf_lr=52, tf_pc=56, tf_psr=60)。
        //
        // tf_lrは「破棄」してはならず、ユーザーのlrへ復元する。通常の
        // システムコールではARMラッパー(SYS.h)が復帰後に自前で pop {lr}
        // するため、以前ここでtf_lrを捨ててlr=tf_pcのまま返しても実害が
        // なかった。しかしsendsig()(sig_machdep.c)は tf_lr = u.u_sigtramp
        // を明示的に設定し、シグナルハンドラが sigtramp -> sigreturn へ
        // 戻るようにしている。tf_lrを捨てるとハンドラは「lr=自分自身の
        // アドレス」で開始してしまい sigtramp へ戻れず、捕捉シグナル
        // (SIGINT/^C・SIGALRM 等)の復帰が一度も成立せずゴミへ暴走して
        // いた。tf_lrをlrへ復元し、tf_pcへのinterworking分岐は呼び出し
        // 規約上破壊可能なr12を分岐先に使って行う(ユーザーの本来のspは
        // tf_ip=48にあり、上の1.5節で既にユーザーr12を上書き済みなので
        // r12は自由に使える)。ldmiaは書き戻しなしにして、最後にtf_ipから
        // spを直接ロードする。
        "ldr r12, [sp, #56]\n\t"      // r12 = tf_pc(分岐先)
        "ldr lr,  [sp, #52]\n\t"      // lr  = tf_lr(ユーザーlr。シグナル時はsigtramp)
        "ldmia sp, {r0-r11}\n\t"      // r0-r11 = tf_r0..tf_r11(書き戻しなし)
        "ldr sp,  [sp, #48]\n\t"      // sp  = tf_ip = 復帰先スタックポインタ
        "bx r12\n\t"                  // 復帰。tf_pcのbit0でARM/Thumb切替(Thumbハンドラ対応)
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

	//printf("DBG: syscall_handler entry, sys_num=%d frame=%x\n",
	//    sys_num, (unsigned)frame);

	syst = u.u_ru.ru_stime;

	/*
	 * GBA has no MMU and no hardware-assisted exception stack switch
	 * (unlike STM32/pic32's PSP/MSP or MIPS exception vector). Since
	 * this session, simulate_swi_via_inline_data() switches sp by hand
	 * before calling here (see the frame-relocation comment near the
	 * top of this file), so syscall_handler() does run on a genuine,
	 * dedicated kernel stack now (UAREA, in EWRAM) - not IWRAM though,
	 * so comparing frame against &u+sizeof(u) (an IWRAM address) would
	 * still always look like an underflow. The actual per-process stack
	 * bounds check further down (against tf_sp/p_daddr/p_dsize) is the
	 * correct one for this architecture regardless.
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

	/*
	 * Grow the user stack to cover the live stack pointer, mirroring the
	 * pic32 port's exception.c (which does exactly this on every kernel
	 * entry). GBA has no MMU and no stack-fault growth: the whole 64K
	 * USERRAM window is directly accessible, so a deep user stack never
	 * faults and, without this, p_ssize would stay frozen at the exec-time
	 * SSIZE. swapout()/swapin() (vm_swap.c) then save/restore only p_ssize
	 * bytes from the stack top, truncating a deeper stack across a swap - a
	 * shell recursing to build a >=6-stage pipeline lost its deepest stage's
	 * frame and silently produced 0 bytes (project_gba_deep_pipeline_data_loss).
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

	//led_control(LED_KERNEL, 1);

	/*
	 * Stack overflow/growth tracking (disabled on GBA): on stm32/pic32
	 * this reads tf_sp from a *real* hardware-saved SP (Cortex-M's PSP,
	 * captured by PendSV_Handler; MIPS's $sp, captured by the exception
	 * vector before switching stacks) - a legitimate live value there.
	 *
	 * On GBA, tf_ip (=tf_sp) is now reliably the caller's real entry sp
	 * for every syscall (simulate_swi_via_inline_data stashes it there
	 * before switching to the kernel stack - see the frame-relocation
	 * comment near the top of this file), not just for the boot-time
	 * execve() dispatched from icode as it used to be. This check is
	 * still left disabled rather than re-enabled outright: it was
	 * originally written for pic32/stm32's exception-frame layout and
	 * hasn't been re-verified against this port's p_daddr/p_dsize
	 * bookkeeping, so flip it on deliberately (with testing) rather
	 * than as a side effect of the tf_ip fix.
	 *
	 * Known gap from leaving this disabled: p_ssize/p_saddr stay fixed
	 * at whatever exec_setupstack() set (SSIZE, 2048 bytes) instead of
	 * tracking real growth, so vm_swap.c's swapout() would only save
	 * that initial region - if a process's stack ever grows past SSIZE
	 * and then gets swapped, the deeper part won't survive a swap back
	 * in. Not yet a problem since swap isn't exercised on this port yet.
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

		/*
		 * Remaining args (5th, 6th, ...) are on the caller's real
		 * stack, per AAPCS, right at the SP value the callee (the
		 * SYS()-generated wrapper in SYS.h) saw at its own entry.
		 *
		 * This used to be computed as "frame + 64 + 4": back when the
		 * trapframe was built directly on top of the caller's own
		 * stack (before the kernel-stack relocation added in this
		 * same session - see the big comment near the top of this
		 * file), `frame` sat exactly 64 bytes below whatever sp was
		 * when CALL_SIMSYS's "bx r12" jumped in, and CALL_SIMSYS
		 * itself opens with "push {lr}" (4 more bytes) before that -
		 * so frame + 64 + 4 landed exactly on the wrapper's own entry
		 * sp. Now that the trapframe lives on the kernel stack instead
		 * (a fixed address, u_end - 64, completely unrelated to any
		 * particular caller's stack position), that arithmetic reads
		 * garbage - observed as sysctl()'s optional 5th/6th args
		 * (e.g. getsecuritylevel()'s NULL/0) coming back as
		 * near-random pointers, tripping baduaddr() into either
		 * silently leaving u.u_arg[4]/[5] as 0 (lucky) or copying from
		 * a bogus address that happened to look valid (EINVAL from
		 * sysctl_int()'s copyin) - "init: cannot get kernel security
		 * level: Invalid argument" on every boot.
		 *
		 * The caller's real entry sp is exactly what frame->tf_ip
		 * holds now (see the frame-relocation comment above): CALL_SIMSYS's
		 * own "push {lr}" still needs accounting for, so +4 here plays
		 * the same role it always did.
		 */
		if (callp->sy_narg > 4) {
			u_int addr = (u_int)frame->tf_ip + 4;
			if (!baduaddr((caddr_t)addr))
				u.u_arg[4] = *(u_int *)addr;
			//if (sys_num == 23)
			//	printf("DBG: sysctl arg4 tf_ip=%x addr=%x bad=%d val=%x\n",
			//	    (unsigned)frame->tf_ip, addr,
			//	    baduaddr((caddr_t)addr), (unsigned)u.u_arg[4]);
		}
		if (callp->sy_narg > 5) {
			u_int addr = (u_int)frame->tf_ip + 4 + 4;
			if (!baduaddr((caddr_t)addr))
				u.u_arg[5] = *(u_int *)addr;
			//if (sys_num == 23)
			//	printf("DBG: sysctl arg5 addr=%x bad=%d val=%x\n",
			//	    addr, baduaddr((caddr_t)addr), (unsigned)u.u_arg[5]);
		}
	}

	u.u_rval = 0;

	//printf("DBG: before setjmp qsave, callp=%x sy_call=%x\n",
	//    (unsigned)callp, (unsigned)callp->sy_call);

	if (setjmp(&u.u_qsave) == 0) {
		//printf("DBG: setjmp qsave=0, calling sy_call\n");
		(*callp->sy_call)();		/* Make syscall. */

		/*
		 * execve(11)が成功した直後の処理
		 * execveから戻る際、新しいプログラム(init)のLRが
		 * カーネル内の古いアドレスを指していると、最初の
		 * 関数から戻るときに暴走するため初期化する。
		 *
		 * code==11はSYS_execv(icodeが起動時に使う手書きのSWI)のみで、
		 * /sbin/initが実際の子プロセスを起動する経路であるSYS_execve
		 * (code==59)がこれまで抜けていた。
		 */
		if ((code == 11 || code == 59) && u.u_error == 0) {
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
	 * Instruction-set-aware exec entry.
	 *
	 * On a successful execve(), tf_pc is the new program's entry point
	 * (a_entry, written by exec_setupstack) and ALREADY carries its
	 * instruction set in bit 0: a Thumb-built crt0 links _start at an odd
	 * address (init=0x0200195d, sh=0x020019f9, echo=0x020018d5, ...), an
	 * ARM-built one - as produced by this port's native as_arm/ld_arm
	 * toolchain - links it even. The "bx r12" that returns to userland
	 * (r12 = tf_pc, above) honours that bit, so a Thumb image enters Thumb
	 * state and an ARM image enters ARM state. We therefore let tf_pc's
	 * own bit 0 select the mode and do NOT touch it here.
	 *
	 * This used to force the Thumb bit unconditionally on exec ("crt0 is
	 * always Thumb"), which entered ARM images in Thumb state and mis-
	 * decoded their first instructions. It must likewise never be forced
	 * for ordinary syscalls, whose wrappers are ARM (.arm): forcing it
	 * once made the CPU decode those ARM return bytes as Thumb and wander
	 * off (observed landing in a .rodata table after sigaction returned).
	 * gba_exec_switched_stack is retained for its documented role above;
	 * the exec sp switch happens via tf_ip in the return trampoline.
	 */
	frame->tf_psr |= 0x80;

	/*
	 * DBG: chasing a wild jump (PC lands in the SWAP staging region,
	 * ORIGIN(SWAP)=0x02011800 in kern.ldscript, right past USERRAM's
	 * end) that happens several syscalls after a successful execve(),
	 * both on mGBA (during init's own startup) and on real GBAED
	 * hardware (during the login shell's startup, further along).
	 * Catch it *here* - the last point with a normal C stack frame and
	 * full register/memory state before control returns to userland
	 * via the naked trampoline's raw ldmia/bx lr - rather than after
	 * the wild bx has already happened and clobbered everything.
	 * Bounds are the user process's own declared text+data+bss+stack
	 * window; a legitimate resume address/sp can never be outside it
	 * on this MMU-less port.
	 */
	{
		extern char __user_data_start[], __user_data_end[];
		unsigned lo = (unsigned)__user_data_start;
		unsigned hi = (unsigned)__user_data_end;
		unsigned pc = frame->tf_pc & ~1;
		unsigned rsp = frame->tf_ip;

		if (pc < lo || pc >= hi || rsp < lo || rsp > hi) {
			printf("DBG: BOUNDS pid=%d sysnum=%d pc=%x sp=%x "
			    "r0=%x r1=%x r2=%x r3=%x lr=%x psr=%x\n",
			    u.u_procp->p_pid, sys_num,
			    (unsigned)frame->tf_pc, rsp,
			    (unsigned)frame->tf_r0, (unsigned)frame->tf_r1,
			    (unsigned)frame->tf_r2, (unsigned)frame->tf_r3,
			    (unsigned)frame->tf_lr, (unsigned)frame->tf_psr);
		}
	}

	userret(u.u_frame->tf_pc, syst);

	//led_control(LED_KERNEL, 0);
}
