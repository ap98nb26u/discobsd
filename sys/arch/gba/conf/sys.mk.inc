#
# GBA userland build settings, read at the end of share/mk/sys.mk.
#
# gba and stm32 share MACHINE_ARCH=arm, so what differs is set here, by
# MACHINE: the GBA's ARM7TDMI (ARMv4T) instead of sys.mk's Cortex-M4,
# with -mthumb-interwork since its BIOS, the kernel's ARM-mode code and
# Thumb userland call one another.
#
CC!=	echo "${GCCPREFIX}-gcc -mcpu=arm7tdmi -mabi=aapcs -mlittle-endian -mthumb -mthumb-interwork -mfloat-abi=soft -nostdinc -I${TOPSRC}/include ${INCLUDES}"

# Userland sees -DGBA, as the kernel does (PARAM in the compile
# Makefiles), so a program can shrink its footprint for the GBA's small
# user window (e.g. usr.bin/tail's line buffer).
CFLAGS+=	-DGBA

# User programs link at the GBA's fixed user window (USERRAM), not at
# sys.mk's lib/elf32-arm.ld address (stm32's RAM): same flags, own script.
LDFLAGS=-N -nostartfiles -fno-dwarf2-cfi-asm \
	${LDWARN} \
	-T${TOPSRC}/sys/arch/gba/conf/elf32-gba.ld \
	${TOPSRC}/lib/crt0.o -L${TOPSRC}/lib
