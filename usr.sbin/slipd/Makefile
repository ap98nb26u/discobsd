#
# slipd - SLIP IP host on the GBA link-cable UART (/dev/sio)
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -O
SRCS		= slipd.c tcp.c svc.c telnet.c
OBJS		= slipd.o tcp.o svc.o telnet.o

all:		slipd

slipd:		${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o slipd.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S slipd.elf > slipd.dis
		${SIZE} slipd.elf
		${ELF2AOUT} slipd.elf $@ && rm slipd.elf

# Host build, for testing against tools/gbanet/slip_tun.py --pty:
#   make slipd-host && ./slipd-host -v -d /dev/pts/N
slipd-host:	${SRCS} slipd.h
		cc -O -Wall -D_GNU_SOURCE -o $@ ${SRCS}

clean:
		rm -f *.o *~ slipd slipd.elf slipd.dis slipd-host tags

install:	all
		${INSTALL} slipd ${DESTDIR}/usr/sbin/slipd

${OBJS}:	slipd.h

# --- auto-added: force relink when crt0.o/libc.a change ---
slipd: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
