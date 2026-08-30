#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/config.h>
#include <sys/fcntl.h>
#include <sys/errno.h>

#include <gba/dev/mgbalog.h>
#include <gba/dev/gba_text.h>
#include <gba/dev/gba_sio_uart.h>

void uartinit(int);
void uartputc(dev_t dev, char c);
char uartgetc(dev_t dev);
extern struct tty uartttys[];

void uartinit(int unit) {
    *(volatile unsigned short*)0x04FFF780 = 0xC0DE; // mGBA Enable

    /*
     * mGBA's debug log is emulator-only, and the on-screen text
     * console is slow to work with over a rebuild/photo cycle; mirror
     * the console unit's output to real hardware UART too (and read
     * console input from it), for interactive debugging over a serial
     * cable. Only the console unit gets set up here -- uartinit() is
     * also called once for the non-console UART during device attach.
     */
    if (unit == CONS_MINOR) {
        gtxt_init(0xFFFF, 0x0000); // white on black
        gsio_init(UART_BAUD);
    }
}
    
static int
uartprobe(struct conf_device *config) {
    int unit = config->dev_unit - 1;
    int is_console = (CONS_MAJOR == UART_MAJOR &&
                      CONS_MINOR == unit);
    if (unit < 0 || unit >= NUART)
        return 0;

#if 0
    printf("uart%d:", unit+1);
    if (is_console)
        printf(", console");
    printf("\n");
#endif

    //uartttys[unit].t_addr = (caddr_t) &uart[unit];
    if (! is_console)
        uartinit(unit);

    return 1;
}

/* conf.o が期待するシンボル名に変更 */
int uartopen(dev_t dev, int flag, int mode) { return 0; }
int uartclose(dev_t dev, int flag, int mode) { return 0; }
int uartread(dev_t dev, struct uio *uio, int flag)
{
    /*
     * No tty line discipline yet (see uartwrite()'s comment below), so
     * read one already-arrived byte per call directly off the wire via
     * gsio_getc() - select()'s uartselect() is what actually confirms
     * a byte is waiting before a caller reads, so this doesn't block
     * indefinitely on an idle line.
     */
    char c;
    int error;

    if (uio->uio_resid <= 0)
        return 0;
    c = (char)uartgetc(dev);
    error = uiomove(&c, 1, uio);
    return error;
}
int uartwrite(dev_t dev, struct uio *uio, int flag)
{
    /*
     * No tty line discipline is wired up yet (uartopen()/uartioctl()
     * are still stubs), so this can't go through ttwrite()/ttstart()
     * like the other ports do. Just push bytes straight out through
     * uartputc() - the same raw path the kernel's own printf() uses -
     * so userland write(2) to the console is actually visible instead
     * of silently discarded (as it always was up to this point).
     */
    char buf[16];
    int n, i, error;

    while (uio->uio_resid > 0) {
        n = uio->uio_resid;
        if (n > sizeof(buf))
            n = sizeof(buf);
        error = uiomove(buf, n, uio);
        if (error)
            return error;
        for (i = 0; i < n; i++)
            uartputc(dev, buf[i]);
    }
    return 0;
}
int uartioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    /*
     * Was an unconditional `return -1;` stub - every sgtty ioctl
     * (TIOCGETP/TIOCSETP/TIOCGETC/...) failed for this console no
     * matter what, which silently broke isatty() (used by ttyname(),
     * in turn used by login's rootterm() /etc/ttys "secure" check -
     * confirmed root cause of "root login refused on this terminal"
     * on real GBAED hardware) and every stty(1)/getty/login attempt
     * to configure tty modes (ECHO included) on this console, and
     * surfaced elsewhere as e.g. stty's "TIOCMGET: Unknown error: 0".
     * uartread()/uartwrite() still bypass the tty line discipline
     * entirely (no ttyinput()/ttstart() wiring - see their own
     * comments), so this only fixes the *query/set flags* half of
     * the tty API, not cooked-mode echo/line-editing - but that's
     * exactly what isatty()/stty/tcgetattr-style callers need.
     */
    register struct tty *tp = &uartttys[minor(dev)];
    int error;

    error = ttioctl(tp, cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    return (error);
}
int uartselect(dev_t dev, int rw)
{
    /*
     * Was a permanent "never ready" stub, so any select()/poll() on the
     * console blocked forever regardless of actual input (confirmed:
     * typing at the serial terminal had no effect). Writes are always
     * immediately possible (uartwrite() never blocks), so only the
     * read direction needs a real check; FWRITE mirrors seltrue()'s
     * always-ready behavior for the other direction.
     */
    if (rw == FREAD) {
        int avail = gsio_avail();
        printf("DBG: uartselect FREAD avail=%d\n", avail);
        return avail;
    }
    return 1;
}

/* r_read / r_write に相当する関数名 (conf.cの定義に合わせる) */
char uartgetc(dev_t dev) {
    return (char)gsio_getc();
}

void uartputc(dev_t dev, char c) {
    gtxt_putc(c);
    gsio_putc((unsigned char)c);

    MGBA_REG_DEBUG_ENABLE = 0xC0DE; // mGBA Enable
#if 1 // buffering
    static int i = 0;
    if ((c && i < 255) && c != '\n') {
        MGBA_REG_DEBUG_BUFFER[i] = c;                // Buffer
        i++;
    } else {
        MGBA_REG_DEBUG_BUFFER[i] = '\0';                // Buffer
        MGBA_REG_DEBUG_FLAGS = 0x100|MGBA_LOG_INFO;  // Send Info Log
        i = 0;
    }
#else
    MGBA_REG_DEBUG_BUFFER[0] = c;
    MGBA_REG_DEBUG_BUFFER[1] = '\0';
    MGBA_REG_DEBUG_FLAGS = 0x100|MGBA_LOG_INFO;  // Send Info Log
#endif
}

void uartputs(dev_t dev, const char *s) {
    MGBA_REG_DEBUG_ENABLE = 0xC0DE; // mGBA Enable
    int i = 0;
    while (s[i] && i < 255) {
        MGBA_REG_DEBUG_BUFFER[i] = s[i];                // Buffer
        i++;
    }
    MGBA_REG_DEBUG_BUFFER[i] = '\0';                // Buffer
    MGBA_REG_DEBUG_FLAGS = 0x100|MGBA_LOG_INFO;  // Send Info Log
}

/* ioconf.c が期待するドライバ構造体 */
struct driver uartdriver = {
    "uart",
    uartprobe
};

struct tty uartttys[NUART];
