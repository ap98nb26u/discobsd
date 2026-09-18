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
# plain GBA (mrams/mGBA) kernel embeds it in ROM (sys/arch/gba/dev/rootfs.img);
# on real EverDrive hardware the same image is written onto the DiscoBSD
# root partition (sd0b, MBR #2), leaving the EverDrive's own FAT32 boot
# partition (#1) intact. There is deliberately no full-disk image: swap is
# a separate region (EWRAM for mrams, MBR #3 for GBAED) and the build has
# no FAT32 tooling to populate the EverDrive boot partition, so a sector-0
# image would not be directly usable anyway.
#
PORT_CUSTOM_FSIMAGE=	1
FS_MBYTES=	20
U_MBYTES=	1
SWAP_MBYTES=	1
FSIMG=		${TOPSRC}/distrib/gba/rootfs.img
GBA_DEV_ROOTFS=	${TOPSRC}/sys/arch/gba/dev/rootfs.img

# `make all' also builds the filesystem image (and relinks the mrams ROM
# around it); `make cleanfs' also removes the ROM-embedded copy.
POST_ALL=	$(MAKE) fs
POST_CLEANFS=	rm -f $(GBA_DEV_ROOTFS)

# Build the bare root FS from the installed tree (single partition, no MBR,
# no separate /home), copy it to where the mrams kernel embeds it, and
# relink the GBA kernel so its ROM carries the fresh filesystem. The GBAED
# kernel reads the SD at runtime and needs no relink - it just consumes this
# same rootfs.img, written to the card's DiscoBSD root partition.
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
# ROM memory disk mr0a, so its EMBEDDED copy gets /etc/fstab swapped to
# distrib/gba/fstab.mrams; the SD image ($@) keeps sd0b.
${FSIMG}:	distrib/gba/md.gba distrib/gba/mi.gba
		rm -f $@ distrib/gba/_manifest
		cat distrib/gba/mi.gba distrib/gba/md.gba > distrib/gba/_manifest
		$(FSUTIL) --new --size=`expr $(FS_MBYTES) \* 1024` --manifest=distrib/gba/_manifest $@ ${DESTDIR}
		cp $@ $(GBA_DEV_ROOTFS)
		d=`mktemp -d`; mkdir -p $$d/etc; \
			cp ${TOPSRC}/distrib/gba/fstab.mrams $$d/etc/fstab; \
			( cd $$d && $(FSUTIL) --add $(GBA_DEV_ROOTFS) etc/fstab ); \
			rm -rf $$d
		$(MAKE) -C sys/arch/gba/compile/GBA all

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
