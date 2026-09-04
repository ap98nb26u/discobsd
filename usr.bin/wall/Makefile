TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# Must run set-group-id tty.

BINDIR=		/usr/bin
BINMODE=	2751

PROG=		wall

OBJS=		${PROG}.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} *.dis tags *~

install:	all
		${INSTALL} -m ${BINMODE} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

# --- auto-added: force relink when crt0.o/libc.a change ---
${PROG}: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
