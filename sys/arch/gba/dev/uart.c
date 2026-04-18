/*
 * UART driver for STM32.
 *
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)cons.c	1.3 (2.11BSD GTE) 1997/4/25
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/config.h>

#include <gba/dev/uart.h>

#define CONCAT(x,y) x ## y
#define BBAUD(x) CONCAT(B,x)

#ifndef UART_BAUD
#define UART_BAUD 115200
#endif

/*
 * STM32 USART/UART port.
 */
struct uart_port {
    //GPIO_TypeDef        *port;
    char                 port_name;
    u_long               pin;
};

/*
 * STM32 USART/UART instance.
 */
struct uart_inst {
    //USART_TypeDef       *inst;
    struct               uart_port tx;
    struct               uart_port rx;
    u_int                apb_div;
    u_int                af;
};

/*
 * STM32 USART/UART.
 */
#if 0
static const struct uart_inst uart[NUART] = {
#define PIN0             LL_GPIO_PIN_0
#define PIN2             LL_GPIO_PIN_2
#define PIN3             LL_GPIO_PIN_3
#define PIN6             LL_GPIO_PIN_6
#define PIN7             LL_GPIO_PIN_7
#define PIN9             LL_GPIO_PIN_9
#define PIN10            LL_GPIO_PIN_10
#define PIN11            LL_GPIO_PIN_11
#define PIN14            LL_GPIO_PIN_14
#define AF7              LL_GPIO_AF_7
#define AF8              LL_GPIO_AF_8
#ifdef STM32F469xx
    { /* USART1 */ },
    { /* USART2 */ },
    { USART3, { GPIOB, 'B', PIN10 }, { GPIOB, 'B', PIN11 }, 2, AF7 },
    { /* UART4 */ },
    { /* UART5 */ },
    { USART6, { GPIOC, 'C', PIN6  }, { GPIOC, 'C', PIN7  }, 2, AF8 },
#endif
};
#endif // 0

struct tty uartttys[NUART];

#if 0 // XXX UART
static unsigned speed_bps [NSPEEDS] = {
    0,       50,      75,      150,     200,    300,     600,     1200,
    1800,    2400,    4800,    9600,    19200,  38400,   57600,   115200,
    230400,  460800,  500000,  576000,  921600, 1000000, 1152000, 1500000,
    2000000, 2500000, 3000000, 3500000, 4000000
};
#endif // XXX UART

void cnstart(struct tty *tp);

void
USART1_IRQHandler(void)
{
    //uartintr(makedev(UART_MAJOR, 0));   /* USART1 */
}

void
USART2_IRQHandler(void)
{
    //uartintr(makedev(UART_MAJOR, 1));   /* USART2 */
}

void
USART3_IRQHandler(void)
{
    //uartintr(makedev(UART_MAJOR, 2));   /* USART3 */
}

void
UART4_IRQHandler(void)
{
    //uartintr(makedev(UART_MAJOR, 3));   /* UART4 */
}

void
UART5_IRQHandler(void)
{
    //uartintr(makedev(UART_MAJOR, 4));   /* UART5 */
}

void
USART6_IRQHandler(void)
{
    //uartintr(makedev(UART_MAJOR, 5));   /* USART6 */
}

/*
 * Setup USART/UART.
 */
void
uartinit(int unit)
{
#if 0
    register USART_TypeDef      *inst;
    register GPIO_TypeDef       *tx_port;
    register u_int               tx_pin;
    register GPIO_TypeDef       *rx_port;
    register u_int               rx_pin;
    register u_int               apb_div;
    register u_int               af;

    if (unit < 0 || unit >= NUART)
        return;

    inst    = uart[unit].inst;
    tx_port = uart[unit].tx.port;
    tx_pin  = uart[unit].tx.pin;
    rx_port = uart[unit].rx.port;
    rx_pin  = uart[unit].rx.pin;
    apb_div = uart[unit].apb_div;
    af      = uart[unit].af;

    /*
     * Configure and enable USART/UART NVIC interrupts.
     * Enable GPIO port peripheral clock and USART/UART peripheral clock.
     */
    switch (unit) {
    case 0:     /* USART1 */
#ifdef USART1
        arm_intr_set_priority(USART1_IRQn, IPL_TTY);
        arm_intr_enable_irq(USART1_IRQn);

#ifdef STM32F446xx
        /* USART1: APB2 90 MHz AF7: TX on PA.09, RX on PA.10 */
        LL_GPIO_EnableClock(GPIOA);
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
#endif

#endif /* USART1 */
        break;

    case 1:     /* USART2 */
#ifdef USART2
        arm_intr_set_priority(USART2_IRQn, IPL_TTY);
        arm_intr_enable_irq(USART2_IRQn);

#ifdef STM32F446xx
        /* USART2: APB1 45 MHz AF7: TX on PA.02, RX on PA.03 */
        LL_GPIO_EnableClock(GPIOA);
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
#endif

#endif /* USART2 */
        break;

    case 2:     /* USART3 */
#ifdef USART3
        arm_intr_set_priority(USART3_IRQn, IPL_TTY);
        arm_intr_enable_irq(USART3_IRQn);

#ifdef STM32F469xx
        /* USART3: AHB1/APB1, 45 MHz, AF7, TX on PB.10, RX on PB.11 */
        LL_GPIO_EnableClock(GPIOB);
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART3);
#endif

#endif /* USART3 */
        break;

    case 3:     /* UART4 */
#ifdef UART4
        arm_intr_set_priority(UART4_IRQn, IPL_TTY);
        arm_intr_enable_irq(UART4_IRQn);
#endif /* UART4 */
        break;

    case 4:     /* UART5 */
#ifdef UART5
        arm_intr_set_priority(UART5_IRQn, IPL_TTY);
        arm_intr_enable_irq(UART5_IRQn);
#endif /* UART5 */
        break;

    case 5:     /* USART6 */
#ifdef USART6
        arm_intr_set_priority(USART6_IRQn, IPL_TTY);
        arm_intr_enable_irq(USART6_IRQn);


#endif /* USART6 */
        break;

    default:
        break;
    }

#endif // 0
}

int
uartopen(dev_t dev, int flag, int mode)
{
#if 0
    register struct uart_inst *uip;
    register struct tty *tp;
    register int unit = minor(dev);

    if (unit < 0 || unit >= NUART)
        return (ENXIO);

    tp = &uartttys[unit];
    if (! tp->t_addr)
        return (ENXIO);

    uip = (struct uart_inst *)tp->t_addr;
    tp->t_oproc = uartstart;
    if ((tp->t_state & TS_ISOPEN) == 0) {
        if (tp->t_ispeed == 0) {
            tp->t_ispeed = BBAUD(UART_BAUD);
            tp->t_ospeed = BBAUD(UART_BAUD);
        }
        ttychars(tp);
        tp->t_state = TS_ISOPEN | TS_CARR_ON;
        tp->t_flags = ECHO | XTABS | CRMOD | CRTBS | CRTERA | CTLECH | CRTKIL;
    }
    if ((tp->t_state & TS_XCLUDE) && u.u_uid != 0)
        return (EBUSY);

    // XXX Clear USART state, then set up new state.
    LL_USART_Enable(uip->inst);
    LL_USART_EnableDirectionRx(uip->inst);
    LL_USART_EnableDirectionTx(uip->inst);
#if 0 // XXX
    reg->sta = 0;
    reg->brg = PIC32_BRG_BAUD (BUS_KHZ * 1000, speed_bps [tp->t_ospeed]);
    reg->mode = PIC32_UMODE_PDSEL_8NPAR |
                PIC32_UMODE_ON;
    reg->staset = PIC32_USTA_URXEN | PIC32_USTA_UTXEN;
#endif // XXX

    /* Enable receive interrupt. */
    LL_USART_EnableIT_RXNE(uip->inst);

    return ttyopen(dev, tp);
#endif // 0
    return 0;
}

/*ARGSUSED*/
int
uartclose(dev_t dev, int flag, int mode)
{
#if 0
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];

    if (! tp->t_addr)
        return ENODEV;

    ttywflush(tp);
    ttyclose(tp);
#endif // 0
    return(0);
}

/*ARGSUSED*/
int
uartread(dev_t dev, struct uio *uio, int flag)
{
#if 0
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];

    if (! tp->t_addr)
        return ENODEV;

    return ttread(tp, uio, flag);
#endif // 0
    return 0;
}

/*ARGSUSED*/
int
uartwrite(dev_t dev, struct uio *uio, int flag)
{
#if 0
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];

    if (! tp->t_addr)
        return ENODEV;

    return ttwrite(tp, uio, flag);
#endif // 0
    return 0;
}

int
uartselect(dev_t dev, int rw)
{
#if 0
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];

    if (! tp->t_addr)
        return ENODEV;

    return (ttyselect (tp, rw));
#endif // 0
    return 0;
}

/*ARGSUSED*/
int
uartioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
#if 0
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];
    register int error;

    if (! tp->t_addr)
        return ENODEV;

    error = ttioctl(tp, cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    return (error);
#endif // 0
    return 0;
}

void
uartintr(dev_t dev)
{
#if 0
    register int c;
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];
    register struct uart_inst *uip;

    if (! tp->t_addr)
        return;

    uip = (struct uart_inst *)tp->t_addr;

    /* Receive */
    while (LL_USART_IsActiveFlag_RXNE(uip->inst)) {
        c = LL_USART_ReceiveData8(uip->inst);
        ttyinput(c, tp);
    }

#if 0 // XXX
    /* XXX Receive Buffer Overrun */
    if (reg->sta & PIC32_USTA_OERR)
        reg->staclr = PIC32_USTA_OERR;
#endif // XXX

    /* RXNE flag was cleared by reading DR register */

    /* Transmit */
    if (LL_USART_IsActiveFlag_TXE(uip->inst)) {
        led_control(LED_TTY, 0);

        /* Disable transmit interrupt. */
        LL_USART_DisableIT_TXE(uip->inst);

        if (tp->t_state & TS_BUSY) {
            tp->t_state &= ~TS_BUSY;
            ttstart(tp);
        }
    }
#endif // 0
}

/*
 * Start (restart) transmission on the given line.
 */
void
uartstart(struct tty *tp)
{
#if 0
    register struct uart_inst *uip;
    register int c, s;

    if (! tp->t_addr)
        return;

    uip = (struct uart_inst *)tp->t_addr;

    /*
     * Must hold interrupts in following code to prevent
     * state of the tp from changing.
     */
    s = spltty();
    /*
     * If it is currently active, or delaying, no need to do anything.
     */
    if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
out:
        led_control(LED_TTY, 0);
        splx(s);
        return;
    }

    /*
     * Wake up any sleepers.
     */
    ttyowake(tp);

    /*
     * Now restart transmission unless the output queue is empty.
     */
    if (tp->t_outq.c_cc == 0)
        goto out;

    if (LL_USART_IsActiveFlag_TXE(uip->inst)) {
        c = getc(&tp->t_outq);
        LL_USART_TransmitData8(uip->inst, c & 0xff);
        tp->t_state |= TS_BUSY;
    }

    /* Enable transmit interrupt. */
    LL_USART_EnableIT_TXE(uip->inst);

    led_control(LED_TTY, 1);
    splx(s);
#endif // 0
}

void
uartputc(dev_t dev, char c)
{
#if 0
    int unit = minor(dev);
    struct tty *tp = &uartttys[unit];
    register const struct uart_inst *uip = &uart[unit];
    register int s, timo;

    s = spltty();
again:
    /*
     * Try waiting for the console tty to come ready,
     * otherwise give up after a reasonable time.
     */
    timo = 30000;
    while (!LL_USART_IsActiveFlag_TXE(uip->inst))
        if (--timo == 0)
            break;

    if (tp->t_state & TS_BUSY) {
        uartintr(dev);
        goto again;
    }
    led_control(LED_TTY, 1);
    LL_USART_ClearFlag_TC(uip->inst);
    LL_USART_TransmitData8(uip->inst, c);

    timo = 30000;
    while (!LL_USART_IsActiveFlag_TC(uip->inst))
        if (--timo == 0)
            break;

    led_control(LED_TTY, 0);
    splx(s);
#endif // 0
}

char
uartgetc(dev_t dev)
{
#if 0
    int unit = minor(dev);
    register const struct uart_inst *uip = &uart[unit];
    int s, c;

    s = spltty();
    for (;;) {
        /* Wait for key pressed. */
        if (LL_USART_IsActiveFlag_RXNE(uip->inst)) {
            c = LL_USART_ReceiveData8(uip->inst);
            break;
        }
    }

    /* RXNE flag was cleared by reading DR register */

    splx(s);
    return (unsigned char) c;
#endif // 0
    return 0;
}

/*
 * Test to see if device is present.
 * Return true if found and initialized ok.
 */
static int
uartprobe(struct conf_device *config)
{
#if 0
    int unit = config->dev_unit - 1;
    int is_console = (CONS_MAJOR == UART_MAJOR &&
                      CONS_MINOR == unit);

    if (unit < 0 || unit >= NUART)
        return 0;

    printf("uart%d: pins tx=P%c%d/rx=P%c%d, af=%d", unit+1,
        uart[unit].tx.port_name, ffs(uart[unit].tx.pin)-1,
        uart[unit].rx.port_name, ffs(uart[unit].rx.pin)-1,
        uart[unit].af);

    if (is_console)
        printf(", console");
    printf("\n");

    /* Initialize the device. */
    uartttys[unit].t_addr = (caddr_t) &uart[unit];
    if (! is_console)
        uartinit(unit);

#endif // 0
    return 1;
}

struct driver uartdriver = {
    "uart", uartprobe,
};
