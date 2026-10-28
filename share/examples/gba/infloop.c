/*
 * infloop - spin forever without making a system call.  A check that the
 * kernel preempts user code from the timer interrupt: while it runs, other
 * processes keep running, and ^C or kill(1) stops it.
 *
 *   infloop &
 *   ps ax
 *   kill %1     (or run it in the foreground and press ^C)
 */
int
main(void)
{
    volatile int k = 1;

    while (1)
        k = -k;
    return 0;
}
