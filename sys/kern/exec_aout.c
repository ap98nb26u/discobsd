#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/namei.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/exec.h>
#include <sys/exec_aout.h>
#include <sys/dir.h>
#include <sys/uio.h>
#include <machine/debug.h>

/*
 * How many times to re-try a failed read of the a.out image before
 * giving up and killing the process - see the comment at the read
 * itself below. Matches the retry counts used for the same class of
 * intermittent SD failure in vm_swap.c / vm_swp.c.
 */
#define EXEC_READ_RETRIES 4

int exec_aout_check(struct exec_params *epp)
{
//unsigned char *ptr = (unsigned char *)&epp->hdr.aout;
//for (int i=0;i<32;i++) printf("%02x ", ptr[i]);
    int error;
    int i;

    DEBUG("\texec_aout_check(): start\n");

    if (epp->hdr_len < sizeof(struct exec)) {
        DEBUG("\texec_aout_check(): error: wrong header length\n");
        DEBUG("\texec_aout_check(): end\n");
        return ENOEXEC;
    }
    if (!(N_GETMID(epp->hdr.aout) == MID_ZERO &&
          N_GETFLAG(epp->hdr.aout) == 0)) {
        DEBUG("\texec_aout_check(): error: not an a.out\n");
        DEBUG("\texec_aout_check(): end\n");
        return ENOEXEC;
    }

    switch (N_GETMAGIC(epp->hdr.aout)) {
    case OMAGIC:
        /*
	 * Because OMAGIC's text area is writable, DiscBSD treats the entire 
	 * area as data.  However, completely deleting the text segment 
	 * leaves an invalid address (NO_ADDR) in exec_estab(), so the length 
	 * is explicitly set to 0 to align the address with the base address.
	 */
        epp->hdr.aout.a_data += epp->hdr.aout.a_text;
        epp->hdr.aout.a_text = 0;
#if 0
	epp->text.vaddr = (caddr_t)__user_data_start;
	epp->text.len = 0;
#endif
        break;
    default:
        printf("Bad a.out magic = %0o\n", N_GETMAGIC(epp->hdr.aout));
        return ENOEXEC;
    }

    /*
     * Save arglist
     */
    exec_save_args(epp);

    DEBUG("\texec_aout_check(): exec file header\n");
    /* magic number */
    DEBUG("\texec_aout_check(): a_midmag  = %#x\n", epp->hdr.aout.a_midmag);
    /* size of text segment */
    DEBUG("\texec_aout_check(): a_text    = %d\n",  epp->hdr.aout.a_text);
    /* size of initialized data */
    DEBUG("\texec_aout_check(): a_data    = %d\n",  epp->hdr.aout.a_data);
    /* size of uninitialized data */
    DEBUG("\texec_aout_check(): a_bss     = %d\n",  epp->hdr.aout.a_bss);
    /* size of text relocation info */
    DEBUG("\texec_aout_check(): a_reltext = %d\n",  epp->hdr.aout.a_reltext);
    /* size of data relocation info */
    DEBUG("\texec_aout_check(): a_reldata = %d\n",  epp->hdr.aout.a_reldata);
    /* size of symbol table */
    DEBUG("\texec_aout_check(): a_syms    = %d\n",  epp->hdr.aout.a_syms);
    /* entry point */
    DEBUG("\texec_aout_check(): a_entry   = %#x\n", epp->hdr.aout.a_entry);

    /*
     * Set up memory allocation
     */
    epp->text.vaddr = epp->heap.vaddr = NO_ADDR;
    epp->text.len = epp->heap.len = 0;

    epp->data.vaddr = (caddr_t)__user_data_start;
    epp->data.len = epp->hdr.aout.a_data;
    epp->bss.vaddr = epp->data.vaddr + epp->data.len;
    epp->bss.len = epp->hdr.aout.a_bss;
    epp->heap.vaddr = epp->bss.vaddr + epp->bss.len;
    epp->heap.len = 0;
    epp->stack.len = SSIZE + roundup(epp->argbc + epp->envbc, NBPW) + (epp->argc + epp->envc+4)*NBPW;
    epp->stack.vaddr = (caddr_t)__user_data_end - epp->stack.len;

    /*
     * Allocate core at this point, committed to the new image.
     * TODO: What to do for errors?
     */
    exec_estab(epp);

    /*
     * Read in text and data.
     *
     * Retry an intermittently failing read before giving up (2026-09-06).
     * This is the root cause of the long-chased real-hardware-only
     * "/bin/sh hangs jumping into its own fault()" bug - see
     * project_gba_sh_fault_sigreturn_hang.md. The SD/EverDrive read
     * path fails occasionally (the same failure class that made
     * vm_swp.c's swap() panic "hard err: swap" once sd.c started
     * reporting card_read()/card_write() failures properly), and when
     * it failed *here*, exec_estab() had already torn down the old
     * image, so the process was left running on a half-destroyed,
     * partially-overwritten memory image - which is exactly why the
     * bytes at /bin/sh's fault() address were found to be garbage at
     * the moment sendsig() tried to jump there, even though the
     * on-disk binary and every swap round trip verified clean.
     */
    for (i = 0; ; i++) {
        error = rdwri (UIO_READ, epp->ip,
                   (caddr_t)epp->data.vaddr, epp->hdr.aout.a_data,
                   sizeof(struct exec) + epp->hdr.aout.a_text, IO_UNIT, 0);
        if (! error)
            break;
        DEBUG("\texec_aout_check(): error: read image returned: %d\n", error);
        if (i >= EXEC_READ_RETRIES)
            break;
        printf("exec: image read error %d for pid %d, retrying (%d)\n",
            error, u.u_procp->p_pid, i + 1);
    }
    if (error) {
        /*
         * Error - all is lost: exec_estab() above already committed to
         * the new image, so the old one is gone and the new one is at
         * best partially read. There is nothing left to return to.
         *
         * This used to raise SIGSEGV, which is *catchable* - and
         * /bin/sh catches it (bin/sh/fault.c, its classic stack-growth
         * handler). So a failed exec sent a caught signal to a process
         * whose text had just been overwritten by the partial load,
         * making sendsig() faithfully compute a jump into a handler
         * address that no longer held any handler - a wild jump into
         * garbage, hanging the machine with no further output. That is
         * the crash this whole investigation chased. SIGKILL cannot be
         * caught, blocked or ignored, so the doomed process dies
         * cleanly instead of jumping into its own wreckage.
         */
        printf("exec: image read failed (%d) for pid %d, killing it\n",
            error, u.u_procp->p_pid);
        psignal (u.u_procp, SIGKILL);
        return error;
    }

    exec_clear(epp);
    exec_setupstack(epp->hdr.aout.a_entry, epp);

    DEBUG("\texec_aout_check(): end\n");

    return 0;
}
