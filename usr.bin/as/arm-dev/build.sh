#!/bin/sh
# Build the ARM toolchain host binaries used by the validation harnesses.
# (These are the same sources the tree builds for MACHINE_ARCH=arm:
#  ../as_arm.c and ../../ld/ld_arm.c, plus smlrc with -DARM.)
set -e
gcc -O1 -w -o asarm ../as_arm.c
gcc -O1 -w -o ldarm ../../ld/ld_arm.c
gcc -O1 -w -DARM -D_RETROBSD -D__SMALLER_C_SCHAR__ -DNO_ANNOTATIONS \
    -DNO_PREPROCESSOR -DNO_PPACK -DNO_EXTRA_WARNS -DSTATIC \
    -DSYNTAX_STACK_MAX=3200 ../../smlrc/smlrc.c -o smlrc-arm
echo "built: asarm ldarm smlrc-arm"
