BOARD?=		GBA
KERNDIR=	${TOPSRC}/sys/arch/gba/compile/${BOARD}
KERN=		unix
ROM=		${KERNDIR}/${KERN}.bin

# The GBA has no external JTAG/SWD debug port and its kernel is a ROM loaded
# into an emulator or flash cartridge, not flashed into an MCU - so it is
# debugged through an emulator's built-in GDB remote stub, not OpenOCD.
# Two emulators are supported:
#
#   mGBA              'mgba -g'          -> GDB server on :2345
#   VisualBoyAdvance  'VisualBoyAdvance --gdb=tcp' -> GDB server on :55555
#
# Two-terminal flow (like the old ocd/gdb-ocd pair), e.g. for mGBA:
#   terminal 1:  make mgba        # starts the emulator, halted, GDB server up
#   terminal 2:  make gdb-mgba    # attaches arm-none-eabi-gdb with kernel symbols
# and likewise 'make vba' + 'make gdb-vba'. Use BOARD=GBAED to debug the
# EverDrive kernel: 'make BOARD=GBAED mgba' / 'make BOARD=GBAED gdb-mgba'.

MGBA?=		mgba
VBA?=		VisualBoyAdvance
# Prefer the arm-none-eabi gdb if installed; otherwise fall back to the
# multiarch gdb (it reads the ARM architecture from the unix ELF). Override
# with 'make GDB=... gdb-mgba' if your gdb has another name.
GDB!=		if command -v arm-none-eabi-gdb >/dev/null 2>&1 ; then \
			echo arm-none-eabi-gdb ; \
		else \
			echo gdb-multiarch ; \
		fi

help:
	@echo "Targets are:" 1>&2
	@echo "    mgba, gdb-mgba   (mGBA, GDB stub on :2345)" 1>&2
	@echo "    vba,  gdb-vba    (VisualBoyAdvance, GDB stub on :55555)" 1>&2
	@echo "" 1>&2
	@echo "=== Debugging the kernel in an emulator's GDB stub ===" 1>&2
	@echo "Run 'make mgba' (or 'make vba') in one terminal to start the" 1>&2
	@echo "emulator halted with its GDB server listening." 1>&2
	@echo "Run 'make gdb-mgba' (or 'make gdb-vba') in another terminal to" 1>&2
	@echo "attach gdb with the kernel symbols loaded." 1>&2
	@echo "In gdb: 'c' to run, Ctrl-C to break, 'b func' to set breakpoints." 1>&2
	@echo "Use BOARD=GBAED to debug the EverDrive kernel." 1>&2

# --- mGBA (GDB stub on :2345) ---
mgba:
	${MGBA} -g ${ROM}

gdb-mgba:
	${GDB} -q \
		-ex 'target remote :2345' \
		${KERNDIR}/${KERN}

# --- VisualBoyAdvance (GDB stub on :55555) ---
vba:
	${VBA} --gdb=tcp ${ROM}

gdb-vba:
	${GDB} -q \
		-ex 'target remote localhost:55555' \
		${KERNDIR}/${KERN}

.PHONY: help mgba gdb-mgba vba gdb-vba
