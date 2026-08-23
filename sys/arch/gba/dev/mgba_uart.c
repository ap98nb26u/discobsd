#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/config.h>

#include <gba/dev/mgbalog.h>
#include <gba/dev/gba_text.h>
#include <gba/dev/gba_sio_uart.h>

void uartinit(int);

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
int uartread(dev_t dev, struct uio *uio, int flag) { return 0; }
int uartwrite(dev_t dev, struct uio *uio, int flag) { return 0; }
int uartioctl(dev_t dev, u_int cmd, caddr_t addr, int flag) { return -1; }
int uartselect(dev_t dev, int rw) { return 0; }

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
