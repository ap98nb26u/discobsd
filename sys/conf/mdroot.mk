#
# Root filesystem image linked into the kernel, for the memory disk's
# md0a (dev/md.c).  A port's kernel Makefile template includes this after
# it sets SYSTEM_OBJ; it takes effect when the kernel config has
#
#	device	md0
#	options	"MD_ROOTIMAGE"
#
# The template sets MDROOT_OFORMAT and MDROOT_ARCH (objcopy's -O and -B
# for the port's ELF), and the port's linker script places the
# .fs_image section between __fs_start and __fs_end.
#
# The image is ${MDROOT_IMG}.  The top-level `make fs' writes the real
# root filesystem there and relinks the kernels named in the port's
# MDROOT_KERNELS (see the top-level Makefile); until then a 1K
# placeholder lets the kernel link.
#
ifneq (,$(findstring -DMD_ROOTIMAGE,$(PARAM)))
MDROOT_IMG?=	${_machdir}/dev/rootfs.img
SYSTEM_OBJ+=	mdroot.o

# Included before the template's load rules: keep these rules from
# becoming the default goal.
__MDROOT_GOAL:=	$(.DEFAULT_GOAL)

mdroot.o:	${MDROOT_IMG}
	${OBJCOPY} -I binary -O ${MDROOT_OFORMAT} -B ${MDROOT_ARCH} \
	    --rename-section .data=.fs_image $< $@

${MDROOT_IMG}:
	dd if=/dev/zero of=$@ bs=1024 count=1 2>/dev/null

.DEFAULT_GOAL:=	$(__MDROOT_GOAL)
endif
