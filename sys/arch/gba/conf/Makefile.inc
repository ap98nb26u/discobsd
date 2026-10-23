BOARD?=		GBA
KERNDIR=	${TOPSRC}/sys/arch/gba/compile/${BOARD}
KERN=		unix
ROM=		${KERNDIR}/${KERN}.bin

#
# Root filesystem image (per-port override of the generic MBR full-disk
# image in the top-level Makefile). Setting PORT_CUSTOM_FSIMAGE switches
# off that generic ${FSIMG}: / installfs rule so the ones below are used.
#
# The GBA image is a BARE root filesystem (no MBR/partition table). The
# plain GBA (ROM-root/mGBA) kernel embeds it in ROM (sys/arch/gba/dev/rootfs.img);
# on real EverDrive hardware the same image is written onto the DiscoBSD
# root partition (sd0b, MBR #2), leaving the EverDrive's own FAT32 boot
# partition (#1) intact. There is deliberately no full-disk image: swap is
# a separate region (EWRAM for the ROM-root build, MBR #3 for GBAED) and the build has
# no FAT32 tooling to populate the EverDrive boot partition, so a sector-0
# image would not be directly usable anyway.
#
PORT_CUSTOM_FSIMAGE=	1
FS_MBYTES=	20
U_MBYTES=	1
SWAP_MBYTES=	1
FSIMG=		${TOPSRC}/distrib/gba/rootfs.img

# The plain GBA kernels link a root image of their own (md0a, options
# MD_ROOTIMAGE): `make fs' copies ${MDROOT_SRC} into place - with
# distrib/gba/fstab.md as /etc/fstab - and relinks them (top-level
# Makefile, MDROOT_KERNELS).  It holds the same files as ${FSIMG}, which
# GBAED reads from the SD card, in a smaller filesystem: run from an
# EverDrive, those kernels can write their root (options MD_ROOT_RW), but
# only within the first 16MB of the cartridge space (see md_gba.c); the
# image starts after the kernel, and swap then takes the rest.
MDROOT_KERNELS=	GBA GBALOG
MDROOT_MBYTES=	14
MDROOT_SRC=	${TOPSRC}/distrib/gba/mdroot.img

# `make all' also builds the filesystem image (and so relinks the
# ROM-root kernels around it).
POST_ALL=	$(MAKE) fs

# Build the bare root FS from the installed tree (single partition, no MBR,
# no separate /home).  The top-level mdroot step then links it into the
# ROM-root kernels (MDROOT_KERNELS above).  The GBAED kernel reads the SD at
# runtime and needs no relink - it just consumes this same rootfs.img,
# written to the card's DiscoBSD root partition.
#
# GBA uses its OWN base manifest (distrib/gba/mi.gba), not the shared
# distrib/base/mi. The shared mi lists the full userland (games, man pages,
# the MIPS-targeted toolchain, all headers), which neither fits this port's
# small inode-limited image nor makes sense on Arm; mi.gba is the curated
# common core (devices + /bin + the common /usr/bin utilities + /etc +
# /var/log). base/mi stays machine-independent for pic32/stm32.
#
# The image ships the sd0b /etc/fstab (etc/fstab.gba, for GBAED which roots
# on sd0b and runs a boot fsck). The plain GBA kernel instead roots on the
# ROM memory disk md0a, so its EMBEDDED copy gets /etc/fstab swapped to
# distrib/gba/fstab.md (by the top-level mdroot step); the SD image ($@)
# keeps sd0b.
${FSIMG}:	distrib/gba/md.gba distrib/gba/mi.gba
		rm -f $@ distrib/gba/_manifest
		cat distrib/gba/mi.gba distrib/gba/md.gba > distrib/gba/_manifest
		# Native ARM a.out C runtime (crt0.o + minimal libc.a) for the
		# on-device toolchain, installed into the image's /usr/lib. ld_arm
		# links DiscoBSD a.out objects, so the cross-built ELF libc is
		# unusable on target; this runtime is produced by host-run copies of
		# this port's own tools (as_arm + smlrc) from the arm-dev/rt sources.
		# The toolchain binaries (cc/cpp/as/ld/ar/smlrc) come from the normal
		# usr.bin build.
		( cd ${TOPSRC}/usr.bin/as/arm-dev && sh build.sh )
		$(MAKE) -C ${TOPSRC}/usr.bin/as/arm-dev/rt
		mkdir -p ${DESTDIR}/usr/lib
		cp ${TOPSRC}/usr.bin/as/arm-dev/rt/crt0.o ${DESTDIR}/usr/lib/crt0.o
		cp ${TOPSRC}/usr.bin/as/arm-dev/rt/libc.a ${DESTDIR}/usr/lib/libc.a
		# GBA examples -> /usr/share/examples/gba: gpanel demos and test
		# programs (tetris, ...), cross-built as a.out next to their
		# sources, plus hello.c/ctest.c for the on-device cc.  The native
		# cc cannot build gpanel programs yet, so the binaries are shipped.
		$(MAKE) -C ${TOPSRC}/share/examples/gba DESTDIR=${DESTDIR} install
		$(FSUTIL) --new --size=`expr $(FS_MBYTES) \* 1024` --manifest=distrib/gba/_manifest $@ ${DESTDIR}

# The ROM kernels' root: the same manifest and installed tree as ${FSIMG}.
${MDROOT_SRC}:	${FSIMG}
		rm -f $@
		$(FSUTIL) --new --size=`expr $(MDROOT_MBYTES) \* 1024` --manifest=distrib/gba/_manifest $@ ${DESTDIR}

# rootfs.img is a bare root filesystem, not a full-disk image, so it must
# NOT be dd'd to sector 0 (that would destroy the EverDrive FAT32 boot
# partition). Write it onto the DiscoBSD root partition (MBR #2 / sd0b).
installfs:
		@echo "installfs is not supported for gba: rootfs.img is a bare"
		@echo "root filesystem, not a full-disk image. Write it onto the"
		@echo "DiscoBSD root partition (EverDrive MBR #2 / sd0b), NOT sector 0,"
		@echo "or the EverDrive FAT32 boot partition is destroyed."
		@exit 1

.PHONY:		installfs

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
