#
# ek - E-Kermit 1.8, the Kermit Project's embedded Kermit
# (https://www.kermitproject.org/ek.html, Revised 3-Clause BSD; see
# COPYING), for file transfer over the GBA link-cable UART (/dev/sio):
# run `ek -r` (receive) or `ek -s file...` (send) on the GBA, then
# Tera Term's File > Transfer > Kermit > Send or Receive.
#
# cdefs.h, debug.h, platform.h and unix.h are as distributed.  Changed:
# unixio.c opens /dev/sio (or the -l device), reads with a timeout and
# polls instead of sleeping just after sending (a packet arriving while
# ek sleeps loses a byte to the 4-byte FIFO); main.c adds -l and ends
# the send list at the next option; kermit.c takes a NAK for the next
# packet as the ACK it waits for, NAKs with a type 1 block check while
# waiting for the S packet, and frees the slot of a too-short packet;
# kermit.h lets P_S_TIMO be set here.  Long packets are on; sliding
# windows are off, as Tera Term's Kermit does not do them.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -O -DNODEBUG -DNO_SSW -DP_PKTLEN=1024 -DP_S_TIMO=10 \
		   -DIBUFLEN=1024 -DOBUFLEN=1024
SRCS		= main.c kermit.c unixio.c
OBJS		= main.o kermit.o unixio.o

all:		ek

ek:		${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o ek.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S ek.elf > ek.dis
		${SIZE} ek.elf
		${ELF2AOUT} ek.elf $@ && rm ek.elf

clean:
		rm -f *.o *~ ek ek.elf ek.dis tags

install:	all
		${INSTALL} ek ${DESTDIR}/usr/bin/ek

${OBJS}:	cdefs.h debug.h kermit.h platform.h

# --- auto-added: force relink when crt0.o/libc.a change ---
ek: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
