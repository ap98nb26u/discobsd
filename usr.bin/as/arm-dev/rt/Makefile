# Native ARM a.out C runtime (crt0.o + libc.a) for DiscoBSD/GBA, built with this
# port's OWN toolchain (smlrc + as_arm + ar). Prototype/dev.
#
# First build the host tool binaries:   (cd .. && ./build.sh)
# which produces ../asarm ../ldarm ../smlrc-arm from the tree sources.
#
# Then:  make        -> crt0.o libc.a
# Link a program:    ../ldarm -e _start -o prog crt0.o prog.o -L. -lc

ASARM = ../asarm
SMLRC = ../smlrc-arm
AR    = ../../../../tools/bin/ar

all: crt0.o libc.a

crt0.o: crt0.s
	$(ASARM) -o $@ crt0.s
clib.o: clib.s
	$(ASARM) -o $@ clib.s
libc.o: libc.c
	$(SMLRC) libc.c libc.s
	$(ASARM) -o $@ libc.s
libc.a: clib.o libc.o
	rm -f $@
	$(AR) rc $@ clib.o libc.o

clean:
	rm -f *.o libc.s libc.a
