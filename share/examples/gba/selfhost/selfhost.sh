#!/bin/sh
#
# selfhost.sh - compile the Smaller C compiler (smlrc) on the GBA with its
# own toolchain, then check that the result reproduces itself.
#
#   stage 1: the installed smlrc compiles smlrc.c       -> $T/smlrc1
#   stage 2: smlrc1 compiles smlrc.c again              -> $T/smlrc2
#   smlrc1 and smlrc2 must be identical (a fixed point).
#
# Needs a writable /tmp with about 1.5 MB free, so GBAED only (the mrams
# ROM root is read-only).  Expect it to take a long time.  The stages run
# cpp, the compiler, as and ld by hand (rather than through cc) so that
# stage 2 can use the freshly built compiler.  smlrc.c and cgarm.c are
# copies of usr.bin/smlrc; smlrc.c #includes cgarm.c from its directory.
#
D=/usr/share/examples/gba/selfhost
T=${TMPDIR:-/tmp}
DEFS="-D__SMALLER_C__ -D__arm__ -D__SMALLER_C_32__ -D__SMALLER_C_SCHAR__
 -DARM -D_RETROBSD -DNO_ANNOTATIONS -DNO_PREPROCESSOR -DNO_PPACK
 -DNO_EXTRA_WARNS -DSTATIC -DSYNTAX_STACK_MAX=3200"

# build COMPILER OUTPUT: compile smlrc.c with COMPILER into OUTPUT
build() {
	echo "  cpp"
	/usr/bin/cpp $DEFS $D/smlrc.c $T/sh.i || exit 1
	echo "  smlrc ($1)"
	$1 $T/sh.i $T/sh.s || exit 1
	rm -f $T/sh.i
	echo "  as"
	/usr/bin/as -o $T/sh.o $T/sh.s || exit 1
	rm -f $T/sh.s
	echo "  ld"
	/usr/bin/ld -o $2 /usr/lib/crt0.o $T/sh.o -lc || exit 1
	rm -f $T/sh.o
}

date
echo "stage 1: /usr/libexec/smlrc -> $T/smlrc1"
build /usr/libexec/smlrc $T/smlrc1
date
echo "stage 2: $T/smlrc1 -> $T/smlrc2"
build $T/smlrc1 $T/smlrc2
date
if cmp $T/smlrc1 $T/smlrc2; then
	echo "selfhost: OK - smlrc1 and smlrc2 are identical"
else
	echo "selfhost: FAILED - smlrc1 and smlrc2 differ"
	exit 1
fi
