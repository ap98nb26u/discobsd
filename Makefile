# Copyright (c) 1986 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
# This makefile is designed to be run as:
#	make
#
# The `make' will compile everything, including a kernel, utilities
# and a root filesystem image.

TOPSRC!=	pwd

# Override the default port with:
# $ make MACHINE=pic32 MACHINE_ARCH=mips
#
MACHINE		?= stm32
MACHINE_ARCH	?= arm

DESTDIR?=	${TOPSRC}/distrib/obj/destdir.${MACHINE}
RELEASEDIR?=	${TOPSRC}/distrib/obj/releasedir

# Filesystem and swap sizes.
ifneq ($(MACHINE),gba)
FS_MBYTES       = 200
U_MBYTES        = 200
SWAP_MBYTES     = 2
else
FS_MBYTES       = 20
U_MBYTES        = 1
SWAP_MBYTES     = 1
endif

# Root filesystem image for ${MACHINE}.
ifneq ($(MACHINE),gba)
# Generic ports: a full disk image WITH an MBR partition table
# (root=#1, swap=#2, optional /home=#3), written to the SD card from
# sector 0.
FSIMG=		${TOPSRC}/distrib/${MACHINE}/sdcard.img
else
# GBA: a BARE root filesystem image (no MBR/partition table). The plain
# GBA (mrams/mGBA) kernel embeds it in ROM (sys/arch/gba/dev/rootfs.img);
# on real EverDrive hardware the same image is written onto the DiscoBSD
# root partition (sd0b, MBR #2), leaving the EverDrive's own FAT32 boot
# partition (#1) intact. There is deliberately no full-disk image for the
# GBA: swap is a separate region (EWRAM for mrams, MBR #3 for GBAED) and
# the build has no FAT32 tooling to populate the EverDrive boot partition,
# so a sector-0 image would not be directly usable anyway.
FSIMG=		${TOPSRC}/distrib/gba/rootfs.img
GBA_DEV_ROOTFS=	${TOPSRC}/sys/arch/gba/dev/rootfs.img
endif

# Set this to the device name for your SD card.  With this
# enabled you can use "make installfs" to copy the sdcard.img
# to the SD card.

#SDCARD          = /dev/sdb

#
# C library options: passed to libc makefile.
# See lib/libc/Makefile for explanation.
#
DEFS		=

FSUTIL=		${TOPSRC}/tools/bin/fsutil

-include Makefile.user

SUBDIR=		share lib bin sbin libexec usr.bin usr.sbin games

all:		build
ifeq ($(MACHINE),gba)
		$(MAKE) fs
endif

build:		symlinks tools
		$(MAKE) kernel
		$(MAKE) -C etc DESTDIR=${DESTDIR} distrib-dirs
		$(MAKE) -C include includes
		for dir in ${SUBDIR} ; do \
			${MAKE} -C $$dir ; done
		for dir in ${SUBDIR} ; do \
			${MAKE} -C $$dir DESTDIR=${DESTDIR} install ; done

distribution:	build
		${MAKE} -C etc DESTDIR=${DESTDIR} distribution
		$(MAKE) fs

tools:
		${MAKE} -C tools MACHINE=${MACHINE} install

kernel:		tools
		${MAKE} -C sys/arch/${MACHINE}/compile all

fs:		$(FSIMG)

ifneq ($(MACHINE),gba)
${FSIMG}:	distrib/${MACHINE}/md.${MACHINE} distrib/base/mi.home
		rm -f $@ distrib/$(MACHINE)/_manifest
		cat distrib/base/mi distrib/$(MACHINE)/md.$(MACHINE) > distrib/$(MACHINE)/_manifest
		$(FSUTIL) --repartition=fs=$(FS_MBYTES)M:swap=$(SWAP_MBYTES)M:fs=$(U_MBYTES)M $@
		${FSUTIL} --new --partition=1 --manifest=distrib/${MACHINE}/_manifest $@ ${DESTDIR}
# In case you need a separate /home partition,
# uncomment the following line.
		$(FSUTIL) --new --partition=3 --manifest=distrib/base/mi.home $@ distrib/home
else
# GBA: build the bare root FS from the installed tree (single partition, no
# MBR, no separate /home), copy it to where the mrams kernel embeds it, and
# relink the GBA kernel so its ROM carries the fresh filesystem. The GBAED
# kernel reads the SD at runtime and needs no relink - it just consumes this
# same rootfs.img, written to the card's DiscoBSD root partition.
${FSIMG}:	distrib/gba/md.gba distrib/base/mi
		rm -f $@ distrib/gba/_manifest
		cat distrib/base/mi distrib/gba/md.gba > distrib/gba/_manifest
		$(FSUTIL) --new --size=`expr $(FS_MBYTES) \* 1024` --manifest=distrib/gba/_manifest $@ ${DESTDIR}
		cp $@ $(GBA_DEV_ROOTFS)
		$(MAKE) -C sys/arch/gba/compile/GBA all
endif

release:
		${MAKE} -C etc MACHINE=${MACHINE} RELEASEDIR=${RELEASEDIR} release

clean:
		rm -f *~
		rm -f include/machine
		for dir in ${SUBDIR} ; do \
			$(MAKE) -C $$dir -k clean; done

cleantools:
		${MAKE} -C tools clean

cleankernel:
		${MAKE} -C sys/arch/${MACHINE}/compile -k clean

cleanfs:
		rm -f distrib/$(MACHINE)/_manifest
		rm -f $(FSIMG)
ifeq ($(MACHINE),gba)
		rm -f $(GBA_DEV_ROOTFS)
endif

cleanall:	cleantools clean cleankernel

symlinks:
		rm -f include/machine
		ln -s $(MACHINE) include/machine

installfs:
ifeq ($(MACHINE),gba)
		@echo "installfs is not supported for gba: rootfs.img is a bare"
		@echo "root filesystem, not a full-disk image. Write it onto the"
		@echo "DiscoBSD root partition (EverDrive MBR #2 / sd0b), NOT sector 0,"
		@echo "or the EverDrive FAT32 boot partition is destroyed."
		@exit 1
else
		@[ -n "${SDCARD}" ] || (echo "SDCARD not defined." && exit 1)
		@[ -f $(FSIMG) ] || $(MAKE) $(FSIMG)
		sudo dd bs=1M if=${FSIMG} of=${SDCARD}
endif

.PHONY:		all build distribution release tools kernel symlinks \
		${FSIMG} fs installfs \
		clean cleantools cleanfs cleanall

# Architecture-specific debugging and loading.
-include sys/arch/${MACHINE}/conf/Makefile.inc
