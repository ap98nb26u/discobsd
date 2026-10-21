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
         * OMAGIC text is writable, so the whole image is treated as data;
         * the text length is set to 0 (rather than the segment dropped)
         * so that exec_estab() still gives text a valid base address
         * instead of NO_ADDR.
         */
        epp->hdr.aout.a_data += epp->hdr.aout.a_text;
        epp->hdr.aout.a_text = 0;
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
     * Read in text and data, retrying a failed read a few times: a
     * flaky storage device (seen with an SD card behind the GBA's
     * EverDrive) can fail a read now and then, and by this point
     * exec_estab() has already torn down the old image, so a failed
     * read would leave the process running on a half-loaded image.
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
         * SIGKILL, not SIGSEGV: SIGSEGV can be caught (/bin/sh does, in
         * bin/sh/fault.c), and delivering a caught signal to a process
         * whose image was just overwritten jumps to wherever the handler
         * used to be - into garbage, hanging the machine.  SIGKILL cannot
         * be caught, so the process simply dies.
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
