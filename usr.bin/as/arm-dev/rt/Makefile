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
LDARM = ../ldarm
AR    = ../../../../tools/bin/ar
# smlrc-arm is built with NO_PREPROCESSOR; strip comments/#defines first.
CPP   = gcc -E -P -undef -nostdinc

all: crt0.o libc.a

crt0.o: crt0.s
	$(ASARM) -o $@ crt0.s
clib.o: clib.s
	$(ASARM) -o $@ clib.s
libc.o: libc.c
	$(SMLRC) libc.c libc.s
	$(ASARM) -o $@ libc.s
stdio.o: stdio.c
	$(CPP) stdio.c -o stdio.i
	$(SMLRC) stdio.i stdio.s
	$(ASARM) -o $@ stdio.s
libc.a: clib.o libc.o stdio.o
	rm -f $@
	$(AR) rc $@ clib.o libc.o stdio.o

# Check the native libc against the host's: build stdiotest.c both ways, run
# the native one in Unicorn (run_file.py), compare output and files written.
test: crt0.o libc.a
	$(CPP) stdiotest.c -o stdiotest.i
	$(SMLRC) stdiotest.i stdiotest.s
	$(ASARM) -o stdiotest.o stdiotest.s
	$(LDARM) -e _start -o stdiotest.out crt0.o stdiotest.o -L. -lc
	rm -rf t.native t.host && mkdir t.native t.host
	cd t.native && python3 ../run_file.py ../stdiotest.out > out
	gcc -w -DHOST -o t.host/stdiotest stdiotest.c
	cd t.host && ./stdiotest > out
	for f in out t1 t2 t3; do cmp t.native/$$f t.host/$$f || exit 1; done
	@echo "stdiotest: native libc matches the host libc"

clean:
	rm -f *.o libc.s stdio.i stdio.s libc.a stdiotest.i stdiotest.s stdiotest.out
	rm -rf t.native t.host
