#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

SRCS		= more.c
OBJS		= more.o

all:            more

more:        	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o more.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S more.elf > more.dis
		${SIZE} more.elf
		${ELF2AOUT} more.elf $@ && rm more.elf

clean:
		rm -f *.o *.elf ${MAN} more *.elf *.dis tags *~

install:        all more.help
		${INSTALL} more ${DESTDIR}/usr/bin/more
		@-rm -f ${DESTDIR}/usr/share/misc/more.help
		cp more.help ${DESTDIR}/usr/share/misc/more.help

# --- auto-added: force relink when crt0.o/libc.a change ---
more: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
