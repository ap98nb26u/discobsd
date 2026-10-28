# GBA examples: gpanel (/dev/tft0..2) demos and test programs, a port of
# share/examples/gpanel's tftetris, a PSG sound test (beeptest), a
# preemption test (infloop), plus small sources for the on-device C
# toolchain, and PCM sound samples for /dev/audio (see SOUNDS for their
# source and license).
#
# Like the rest of share/examples, the sources are installed in
# /usr/share/examples/gba.  But the GBA's native cc cannot build gpanel
# programs yet (it has no a.out libgpanel).  So this Makefile also
# cross-builds those programs with the tree toolchain and installs the
# a.out binaries next to their sources.  hello.c and ctest.c are left as
# source only: they are meant to be compiled on the device (`cc hello.c`).
# selfhost/ gets selfhost.sh plus copies of the compiler's own sources
# (usr.bin/smlrc/smlrc.c and cgarm.c), so smlrc can be rebuilt on the device.
# The GBA FSIMG rule (sys/arch/gba/conf/Makefile.inc) runs `make install`
# here, and distrib/gba/mi.gba lists what goes into the image.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

EXDIR=		/usr/share/examples/gba
LIBS=		-lgpanel -lc

PROG=		tetris gball gball5 gball5i imgdemo fontchart fontsampler \
		gfxtest gfxtest2 gfxtest3 gfxtest4 keytest beeptest infloop
SRCONLY=	hello.c ctest.c
SOUNDS=		SOUNDS down15th.s8 down15th-st.s8

FONTS=		5x7.o 6x9.o digits20.o lucidasans9.o lucidasans15.o verdana7.o

all:		${PROG}

DEPS=		$(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a $(TOPSRC)/lib/libgpanel.a

# Most programs need only libgpanel; the text demos also link fonts
# compiled from share/examples/gpanel/fonts.
tetris:	tetris.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf tetris.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

gball:	gball.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf gball.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

gball5:	gball5.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf gball5.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

gball5i:	gball5i.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf gball5i.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

imgdemo:	imgdemo.o 6x9.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf imgdemo.o 6x9.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

fontchart:	fontchart.o 6x9.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf fontchart.o 6x9.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

fontsampler:	fontsampler.o ${FONTS} ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf fontsampler.o ${FONTS} ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

gfxtest:	gfxtest.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf gfxtest.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

gfxtest2:	gfxtest2.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf gfxtest2.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

gfxtest3:	gfxtest3.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf gfxtest3.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

gfxtest4:	gfxtest4.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf gfxtest4.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

keytest:	keytest.o 6x9.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf keytest.o 6x9.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

beeptest:	beeptest.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf beeptest.o ${LIBS}
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

infloop:	infloop.o ${DEPS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf infloop.o -lc
		${ELF2AOUT} $@.elf $@ && rm -f $@.elf

%.o:		../gpanel/fonts/%.c
		${CC} ${CFLAGS} -c -o $@ $<

clean:
		rm -f *.o *.elf *.d ${PROG} tags *~

install:	all
		mkdir -p ${DESTDIR}${EXDIR}
		for p in ${PROG}; do \
			${INSTALL} $$p ${DESTDIR}${EXDIR}/$$p; \
			cp $$p.c ${DESTDIR}${EXDIR}/$$p.c; \
		done
		for s in ${SRCONLY} ${SOUNDS} Makefile; do \
			cp $$s ${DESTDIR}${EXDIR}/$$s; \
		done
		mkdir -p ${DESTDIR}${EXDIR}/selfhost
		${INSTALL} -m 755 selfhost/selfhost.sh ${DESTDIR}${EXDIR}/selfhost/selfhost.sh
		cp ${TOPSRC}/usr.bin/smlrc/smlrc.c ${TOPSRC}/usr.bin/smlrc/cgarm.c \
			${DESTDIR}${EXDIR}/selfhost/
