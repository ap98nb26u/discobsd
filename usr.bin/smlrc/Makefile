TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# Target code generator: -DARM (cgarm.c) for arm (gba/stm32), -DMIPS
# (cgmips.c) for pic32/mips. Shell select works under BSD make and GNU make.
TARGETDEF!=	if [ x"${MACHINE_ARCH}" = x"arm" ]; then echo -DARM; else echo -DMIPS; fi
# extra codegen flag for building smlrc itself (mips16 on pic32; none on arm)
TARGETCG!=	if [ x"${MACHINE_ARCH}" = x"arm" ]; then echo ; else echo -mips16; fi

CFLAGS          = -Os -Wall ${TARGETDEF} -DNO_ANNOTATIONS -DNO_PREPROCESSOR \
                  -DNO_PPACK -D_RETROBSD -D__SMALLER_C_SCHAR__ \
                  -D__SMALLER_C__ -D__SMALLER_C_32__ -DSTATIC \
                  -DNO_EXTRA_WARNS -DSYNTAX_STACK_MAX=3200

LIBS		= -lc

ARCH            = ${MACHINE_ARCH}

OBJS            = smlrc.o lb.o

all:		smlrc

smlrc:          $(OBJS)
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@

install:        smlrc
		${INSTALL} smlrc ${DESTDIR}/usr/libexec/


clean:
		rm -f *.o smlrc smlrc.dis smlrc.elf
###
smlrc.o: smlrc.c fp.c cgmips.c cgarm.c
		${CC} ${CFLAGS} ${TARGETCG} smlrc.c -c -o $@

# --- auto-added: force relink when crt0.o/libc.a change ---
smlrc: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
