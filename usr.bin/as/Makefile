#
# as      - Assembler
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall -Os
LDFLAGS         +=

# ARM targets (gba/stm32) use the native ARM assembler (as_arm.c); MIPS
# (pic32) keeps the Vakulenko MIPS assembler (as.c). Shell-based selection so
# it works under both BSD make and GNU make (like sys.mk). See arm-dev/ and
# sys/arch/gba/NOTES.toolchain.
ASOBJ!=		if [ x"${MACHINE_ARCH}" = x"arm" ]; then echo as_arm.o; else echo as.o; fi

all:            as

as:             ${ASOBJ}
		${CC} ${CFLAGS} ${LDFLAGS} -o as.elf ${ASOBJ} ${LIBS}
		${OBJDUMP} -S as.elf > as.dis
		${SIZE} as.elf
		${ELF2AOUT} as.elf $@ && rm as.elf

clean:
		rm -f *.o *.0 *.elf as tags *~ *.dis tests/*.dis tests/*.gcc-dis tests/*.o

install:        all
		${INSTALL} as ${DESTDIR}/usr/bin/

# --- auto-added: force relink when crt0.o/libc.a change ---
as: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
