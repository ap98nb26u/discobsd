BOARD?=		GBA
KERNDIR=	${TOPSRC}/sys/arch/gba/compile/${BOARD}
KERN=		unix

CONFSCRIPT!=if [ x"${BOARD}" = x"GBA" ] ; then \
		echo "board/gba.cfg" ; \
	else \
		echo "does/not/exist" ; \
	fi

help:
	@echo "Targets are:" 1>&2
	@echo "    ocd, gdb-ocd" 1>&2
	@echo "" 1>&2
	@echo "=== Debugging elf file in Circuit Board ===" 1>&2
	@echo "Run 'make ocd' in one terminal" 1>&2
	@echo "Run 'make gdb-ocd' in another terminal" 1>&2
	@echo "Enter 'c' in gdb to run the elf file" 1>&2
	@echo "Enter 'mon reset halt' in gdb to reset the cpu" 1>&2
	@echo "Enter 'load' in gdb to upload the elf file" 1>&2

ocd:
	sudo openocd -f "${CONFSCRIPT}"

gdb-ocd:
	arm-none-eabi-gdb -q \
		-ex 'cd ${KERNDIR}' \
		-ex 'target extended-remote :3333' \
		-ex 'mon reset halt' \
		-ex 'load' \
		${KERNDIR}/${KERN}

.PHONY: help ocd gdb-ocd
