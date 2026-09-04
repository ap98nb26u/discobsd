TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		egrep

OBJS=		${PROG}.o

YFLAGS=

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} *.dis tags *~ y.tab.[ch]

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

# --- auto-added: force relink when crt0.o/libc.a change ---
${PROG}: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
