PARAM = -DGBA
PARAM += -DARM7TDMI
PARAM += -DUART1_ENABLED
PARAM += -DUART2_ENABLED
PARAM += -DED_ENABLED
PARAM += -DSD_ENABLED
PARAM += -DNSD=1
PARAM += -DCONS_MINOR=0
PARAM += -DCONS_MAJOR=UART_MAJOR
PARAM += -DUART_BAUD=115200
PARAM += -DHALTREBOOT
PARAM += -DBUS_KHZ=16777
PARAM += -DCPU_KHZ=16777
LDSCRIPT = "kern.ldscript"
DEBUG = -g
#
# Makefile for DiscoBSD/gba
#
# DEBUG is set to -g by config if debugging is requested (config -g).
#

# sources are located via $S relative to the compilation directory
S=		../../../..

_mach?=		gba
_arch?=		arm
_machdir?=	$S/arch/${_mach}
_archdir?=	$S/arch/${_arch}
_confdir?=	${_machdir}/conf

include		${_confdir}/compiler.mk

INCLUDES=	-nostdinc -I. -I$S -I$S/arch -I${_archdir}/include
DEPFLAGS=	-MT $@ -MP -MD -MF .deps/$*.dep
DEFS=		${INCLUDES} ${PARAM} -DKERNEL ${DEPFLAGS}
CWARNFLAGS=	-Wall

CMACHCPU=	-mcpu=arm7tdmi -march=armv4t -little-endian -mfloat-abi=soft
CMACHFLAGS=	-mthumb -mabi=aapcs -mthumb-interwork
CMACHFLAGS+=	-ffreestanding -fno-builtin -fcommon
CMACHFLAGS+=	${CMACHCPU}

COPTS?=		#-O
CFLAGS=		${DEBUG} ${CWARNFLAGS} ${CMACHFLAGS} ${COPTS}
AFLAGS=		-x assembler-with-cpp ${CWARNFLAGS} ${CMACHFLAGS}
LDFLAGS=	-T ${_confdir}/${LDSCRIPT}
LDFLAGS+=	-nostdlib -Map=$@.map -z noexecstack
LDFLAGS+=	-static -marmelf
LDADD=		-L/usr/lib/gcc/arm-none-eabi/12.2.1/thumb/nofp -lgcc
FIXBIN=		/mnt/c/msys64/usr/local/bin/gbafix.exe

# compile rules: rules are named COMPILE_${SUFFIX}
# SUFFIX is the file suffix, capitalized (e.g. C for a .c file).

COMPILE_C=	${CC} ${CFLAGS} ${DEFS} -c $<
COMPILE_S=	${AS} ${AFLAGS} ${DEFS} -c $<

OBJS = exec_aout.o exec_conf.o exec_elf.o exec_script.o exec_subr.o \
	init_main.o init_sysent.o kern_clock.o kern_descrip.o \
	kern_exec.o kern_exit.o kern_fork.o kern_mman.o kern_proc.o \
	kern_prot.o kern_prot2.o kern_resource.o kern_sig.o kern_sig2.o \
	kern_subr.o kern_synch.o kern_sysctl.o kern_time.o subr_prf.o \
	subr_rmap.o sys_generic.o sys_inode.o sys_pipe.o sys_process.o \
	syscalls.o tty.o tty_subr.o tty_tty.o ufs_alloc.o ufs_bio.o \
	ufs_bmap.o ufs_dsort.o ufs_fio.o ufs_inode.o ufs_mount.o \
	ufs_namei.o ufs_subr.o ufs_syscalls.o ufs_syscalls2.o \
	vfs_vnops.o vm_sched.o vm_swap.o vm_swp.o cons.o mem.o swap.o \
	clock.o conf.o fault.o machdep.o mpu.o sig_machdep.o syscall.o \
	sysctl.o systick.o sched_test.o ed.o sd.o mgba_uart.o gba_text.o \
	gba_font.o gba_sio_uart.o 

CFILES = $S/kern/exec_aout.c $S/kern/exec_conf.c $S/kern/exec_elf.c \
	$S/kern/exec_script.c $S/kern/exec_subr.c $S/kern/init_main.c \
	$S/kern/init_sysent.c $S/kern/kern_clock.c \
	$S/kern/kern_descrip.c $S/kern/kern_exec.c $S/kern/kern_exit.c \
	$S/kern/kern_fork.c $S/kern/kern_mman.c $S/kern/kern_proc.c \
	$S/kern/kern_prot.c $S/kern/kern_prot2.c $S/kern/kern_resource.c \
	$S/kern/kern_sig.c $S/kern/kern_sig2.c $S/kern/kern_subr.c \
	$S/kern/kern_synch.c $S/kern/kern_sysctl.c $S/kern/kern_time.c \
	$S/kern/subr_prf.c $S/kern/subr_rmap.c $S/kern/sys_generic.c \
	$S/kern/sys_inode.c $S/kern/sys_pipe.c $S/kern/sys_process.c \
	$S/kern/syscalls.c $S/kern/tty.c $S/kern/tty_subr.c \
	$S/kern/tty_tty.c $S/kern/ufs_alloc.c $S/kern/ufs_bio.c \
	$S/kern/ufs_bmap.c $S/kern/ufs_dsort.c $S/kern/ufs_fio.c \
	$S/kern/ufs_inode.c $S/kern/ufs_mount.c $S/kern/ufs_namei.c \
	$S/kern/ufs_subr.c $S/kern/ufs_syscalls.c \
	$S/kern/ufs_syscalls2.c $S/kern/vfs_vnops.c $S/kern/vm_sched.c \
	$S/kern/vm_swap.c $S/kern/vm_swp.c $S/dev/cons.c $S/dev/mem.c \
	$S/dev/swap.c $S/arch/gba/gba/clock.c $S/arch/gba/gba/conf.c \
	$S/arch/gba/gba/fault.c $S/arch/gba/gba/machdep.c \
	$S/arch/gba/gba/mpu.c $S/arch/gba/gba/sig_machdep.c \
	$S/arch/gba/gba/syscall.c $S/arch/gba/gba/sysctl.c \
	$S/arch/gba/gba/systick.c $S/arch/gba/gba/sched_test.c \
	$S/arch/gba/dev/ed.c $S/arch/gba/dev/sd.c \
	$S/arch/gba/dev/mgba_uart.c $S/arch/gba/dev/gba_text.c \
	$S/arch/gba/dev/gba_font.c $S/arch/gba/dev/gba_sio_uart.c \
	swapunix.c 

# load lines for config "xxx" will be emitted as:
# xxx: ${SYSTEM_DEP} swapxxx.o
#	${SYSTEM_LD_HEAD}
#	${SYSTEM_LD} swapxxx.o
#	${SYSTEM_LD_TAIL}
SYSTEM_OBJ=	locore0.o locore.o switch.o ${OBJS} ioconf.o
SYSTEM_OBJ+=	sdcard.o
SYSTEM_DEP=	Makefile ioconf.c machine sys .deps ${SYSTEM_OBJ}
SYSTEM_LD_HEAD=	sh $S/conf/newvers.sh > vers.c; ${CC} ${CFLAGS} -c vers.c; rm -f $@
SYSTEM_LD=	-@echo ${LD} ${LDFLAGS} '$${SYSTEM_OBJ}' vers.o -o $@ ${LDADD}; \
		${LD} ${LDFLAGS} ${SYSTEM_OBJ} vers.o -o $@ ${LDADD}
SYSTEM_LD_TAIL=	${SIZE} $@; cp $@ $@.elf; \
		${OBJCOPY} -O ihex $@ $@.hex; \
		${OBJCOPY} -O binary $@ $@.bin; \
		${FIXBIN} $@.bin; \
		${OBJDUMP} -d -S $@ > $@.dis

unix: ${SYSTEM_DEP} swapunix.o
	${SYSTEM_LD_HEAD}
	${SYSTEM_LD} swapunix.o
	${SYSTEM_LD_TAIL}

swapunix.o: swapunix.c
	${COMPILE_C}

all: unix

clean:
	rm -rf .deps unix *.elf *.o *.map *.dis *.bin *.sav *.hex machine sys

clean-all: clean
	rm -f *.h ioconf.c swap*.c vers.c

reconfig ioconf.c: Config $S/../tools/bin/config
	$S/../tools/bin/config Config
	${MAKE} clean

machine:
	ln -s ${_machdir}/include $@

sys:
	ln -s $S/sys $@

.deps:
	mkdir .deps

locore.o: ${_machdir}/${_mach}/locore.S
	${COMPILE_S}

locore0.o: ${_machdir}/${_mach}/locore0.S
	${COMPILE_S}

switch.o: ${_machdir}/${_mach}/switch.S
	${COMPILE_S}

ioconf.o: ioconf.c
	${COMPILE_C}

SDCARD_IMG=	${_machdir}/dev/sdcard.img
sdcard.o: ${SDCARD_IMG}
	${OBJCOPY} -I binary -O elf32-littlearm -B ${_arch} \
	--rename-section .data=.fs_image \
	$< $@

exec_aout.o: $S/kern/exec_aout.c
	${COMPILE_C}

exec_conf.o: $S/kern/exec_conf.c
	${COMPILE_C}

exec_elf.o: $S/kern/exec_elf.c
	${COMPILE_C}

exec_script.o: $S/kern/exec_script.c
	${COMPILE_C}

exec_subr.o: $S/kern/exec_subr.c
	${COMPILE_C}

init_main.o: $S/kern/init_main.c
	${COMPILE_C}

init_sysent.o: $S/kern/init_sysent.c
	${COMPILE_C}

kern_clock.o: $S/kern/kern_clock.c
	${COMPILE_C}

kern_descrip.o: $S/kern/kern_descrip.c
	${COMPILE_C}

kern_exec.o: $S/kern/kern_exec.c
	${COMPILE_C}

kern_exit.o: $S/kern/kern_exit.c
	${COMPILE_C}

kern_fork.o: $S/kern/kern_fork.c
	${COMPILE_C}

kern_mman.o: $S/kern/kern_mman.c
	${COMPILE_C}

kern_proc.o: $S/kern/kern_proc.c
	${COMPILE_C}

kern_prot.o: $S/kern/kern_prot.c
	${COMPILE_C}

kern_prot2.o: $S/kern/kern_prot2.c
	${COMPILE_C}

kern_resource.o: $S/kern/kern_resource.c
	${COMPILE_C}

kern_sig.o: $S/kern/kern_sig.c
	${COMPILE_C}

kern_sig2.o: $S/kern/kern_sig2.c
	${COMPILE_C}

kern_subr.o: $S/kern/kern_subr.c
	${COMPILE_C}

kern_synch.o: $S/kern/kern_synch.c
	${COMPILE_C}

kern_sysctl.o: $S/kern/kern_sysctl.c
	${COMPILE_C}

kern_time.o: $S/kern/kern_time.c
	${COMPILE_C}

subr_prf.o: $S/kern/subr_prf.c
	${COMPILE_C}

subr_rmap.o: $S/kern/subr_rmap.c
	${COMPILE_C}

sys_generic.o: $S/kern/sys_generic.c
	${COMPILE_C}

sys_inode.o: $S/kern/sys_inode.c
	${COMPILE_C}

sys_pipe.o: $S/kern/sys_pipe.c
	${COMPILE_C}

sys_process.o: $S/kern/sys_process.c
	${COMPILE_C}

syscalls.o: $S/kern/syscalls.c
	${COMPILE_C}

tty.o: $S/kern/tty.c
	${COMPILE_C}

tty_subr.o: $S/kern/tty_subr.c
	${COMPILE_C}

tty_tty.o: $S/kern/tty_tty.c
	${COMPILE_C}

ufs_alloc.o: $S/kern/ufs_alloc.c
	${COMPILE_C}

ufs_bio.o: $S/kern/ufs_bio.c
	${COMPILE_C}

ufs_bmap.o: $S/kern/ufs_bmap.c
	${COMPILE_C}

ufs_dsort.o: $S/kern/ufs_dsort.c
	${COMPILE_C}

ufs_fio.o: $S/kern/ufs_fio.c
	${COMPILE_C}

ufs_inode.o: $S/kern/ufs_inode.c
	${COMPILE_C}

ufs_mount.o: $S/kern/ufs_mount.c
	${COMPILE_C}

ufs_namei.o: $S/kern/ufs_namei.c
	${COMPILE_C}

ufs_subr.o: $S/kern/ufs_subr.c
	${COMPILE_C}

ufs_syscalls.o: $S/kern/ufs_syscalls.c
	${COMPILE_C}

ufs_syscalls2.o: $S/kern/ufs_syscalls2.c
	${COMPILE_C}

vfs_vnops.o: $S/kern/vfs_vnops.c
	${COMPILE_C}

vm_sched.o: $S/kern/vm_sched.c
	${COMPILE_C}

vm_swap.o: $S/kern/vm_swap.c
	${COMPILE_C}

vm_swp.o: $S/kern/vm_swp.c
	${COMPILE_C}

cons.o: $S/dev/cons.c
	${COMPILE_C}

mem.o: $S/dev/mem.c
	${COMPILE_C}

swap.o: $S/dev/swap.c
	${COMPILE_C}

clock.o: $S/arch/gba/gba/clock.c
	${COMPILE_C}

conf.o: $S/arch/gba/gba/conf.c
	${COMPILE_C}

fault.o: $S/arch/gba/gba/fault.c
	${COMPILE_C}

machdep.o: $S/arch/gba/gba/machdep.c
	${COMPILE_C}

mpu.o: $S/arch/gba/gba/mpu.c
	${COMPILE_C}

sig_machdep.o: $S/arch/gba/gba/sig_machdep.c
	${COMPILE_C}

syscall.o: $S/arch/gba/gba/syscall.c
	${COMPILE_C}

sysctl.o: $S/arch/gba/gba/sysctl.c
	${COMPILE_C}

systick.o: $S/arch/gba/gba/systick.c
	${COMPILE_C}

sched_test.o: $S/arch/gba/gba/sched_test.c
	${COMPILE_C}

ed.o: $S/arch/gba/dev/ed.c
	${COMPILE_C}

sd.o: $S/arch/gba/dev/sd.c
	${COMPILE_C}

mgba_uart.o: $S/arch/gba/dev/mgba_uart.c
	${COMPILE_C}

gba_text.o: $S/arch/gba/dev/gba_text.c
	${COMPILE_C}

gba_font.o: $S/arch/gba/dev/gba_font.c
	${COMPILE_C}

gba_sio_uart.o: $S/arch/gba/dev/gba_sio_uart.c
	${COMPILE_C}


-include .deps/*.dep
