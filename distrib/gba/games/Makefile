# GBA-specific graphics games for /usr/games.
#
# These are gpanel (/dev/tft0) programs adapted to the GBA: fixed 240x160
# layout and REG_KEYINPUT button input (the pic32 originals in
# share/examples/gpanel assume larger panels and an analog joystick / tty).
# Cross-built to an a.out like any other userland program (BINDIR=/usr/games)
# and pulled into the GBA image by distrib/gba/mi.gba.  Invoked from the GBA
# FSIMG rule (sys/arch/gba/conf/Makefile.inc) during `make distribution`.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/games
LIBS=		-lgpanel -lc

PROG=		tetris

all:		${PROG}

tetris:		tetris.o $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a \
		$(TOPSRC)/lib/libgpanel.a
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf tetris.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

clean:
		rm -f *.o *.elf *.d ${PROG} tags *~

install:	all
		mkdir -p ${DESTDIR}${BINDIR}
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
