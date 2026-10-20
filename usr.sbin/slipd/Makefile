#
# slipd - SLIP IP host on the GBA link-cable UART (/dev/sio)
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -O
SRCS		= slipd.c
OBJS		= slipd.o

all:		slipd

slipd:		${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o slipd.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S slipd.elf > slipd.dis
		${SIZE} slipd.elf
		${ELF2AOUT} slipd.elf $@ && rm slipd.elf

clean:
		rm -f *.o *~ slipd slipd.elf slipd.dis tags

install:	all
		${INSTALL} slipd ${DESTDIR}/usr/sbin/slipd

# --- auto-added: force relink when crt0.o/libc.a change ---
slipd: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
