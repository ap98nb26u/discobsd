#
# sixelshot - dump the GBA screen as a DEC sixel image
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -O
SRCS		= sixelshot.c
OBJS		= sixelshot.o

all:		sixelshot

sixelshot:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o sixelshot.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S sixelshot.elf > sixelshot.dis
		${SIZE} sixelshot.elf
		${ELF2AOUT} sixelshot.elf $@ && rm sixelshot.elf

clean:
		rm -f *.o *~ sixelshot sixelshot.elf sixelshot.dis tags

install:	all
		${INSTALL} sixelshot ${DESTDIR}/usr/bin/sixelshot

# --- auto-added: force relink when crt0.o/libc.a change ---
sixelshot: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
