BOARD?=		PINGUINO_MICRO
KERNDIR=	${TOPSRC}/sys/arch/pic32/compile/${BOARD}
KERN=		unix

installflash:
	sudo pic32prog ${KERNDIR}/${KERN}.hex

.PHONY: installflash

#
# Booting from flash instead of the SD card: a kernel built with the md
# memory disk and options MD_ROOTIMAGE (e.g. compile/MAX32_MDROOT, for the
# virtualmips emulator) links a small root filesystem, md0a, into flash;
# swap stays on the SD card.  The full SD image is far too big for that,
# so the root comes from its own small manifest, distrib/pic32/mi.mdroot -
# enough to boot multi-user to a login prompt.  Name the kernels to relink
# when building the image:
#
#   make MACHINE=pic32 MACHINE_ARCH=mips MDROOT_KERNELS=MAX32_MDROOT fs
#
MDROOT_SRC=	${TOPSRC}/distrib/pic32/mdroot.img
MDROOT_KBYTES?=	340

${MDROOT_SRC}:	distrib/pic32/mi.mdroot
	rm -f $@
	${TOPSRC}/tools/bin/fsutil --new --size=${MDROOT_KBYTES} --inodes=64 \
	    --manifest=distrib/pic32/mi.mdroot $@ ${DESTDIR}
