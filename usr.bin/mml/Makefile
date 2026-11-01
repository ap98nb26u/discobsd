#
# mml - play Music Macro Language (mmlgb dialect) on the GBA's PSG
# channels through /dev/psg, or write the stream to a file (-o).
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -O
SRCS		= mml.c
OBJS		= mml.o

all:		mml

mml:		${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o mml.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S mml.elf > mml.dis
		${SIZE} mml.elf
		${ELF2AOUT} mml.elf $@ && rm mml.elf

clean:
		rm -f *.o *~ mml mml.elf mml.dis tags

install:	all
		${INSTALL} mml ${DESTDIR}/usr/bin/mml

# --- auto-added: force relink when crt0.o/libc.a change ---
mml: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
