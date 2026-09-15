#
# Version 2.2 January 24, 1998
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall -Os
LDFLAGS		+=

# ARM targets (gba/stm32) use the native ARM linker (ld_arm.c); MIPS (pic32)
# keeps the 2BSD MIPS linker (ld.c). Shell-based selection so it works under
# both BSD make and GNU make (like sys.mk). See sys/arch/gba/NOTES.toolchain.
LDOBJ!=		if [ x"${MACHINE_ARCH}" = x"arm" ]; then echo ld_arm.o; else echo ld.o; fi

MAN		= ld.0
MANSRC		= ld.1

all:            ld $(MAN)

ld:             ${LDOBJ}
		${CC} ${CFLAGS} ${LDFLAGS} -o ld.elf ${LDOBJ} ${LIBS}
		${OBJDUMP} -S ld.elf > ld.dis
		${SIZE} ld.elf
		${ELF2AOUT} ld.elf $@ && rm ld.elf

$(MAN):         $(MANSRC)
		${MANROFF} ${MANSRC} > $@

clean:
		rm -f *.o *.0 *.elf ${MAN} ld *.elf *.dis a.out tags *~

install:        all
		${INSTALL} ld ${DESTDIR}/usr/bin/ld
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/

# --- auto-added: force relink when crt0.o/libc.a change ---
ld: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
