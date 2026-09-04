TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS          += -Wall -O0

virus:          virus.c
		${CC} ${CFLAGS} ${LDFLAGS} -o virus.elf virus.c ${LIBS} -ltermcap
		$(SIZE) virus.elf
		$(ELF2AOUT) virus.elf $@

install:        virus
		${INSTALL} virus ${DESTDIR}/usr/bin/vi

clean:
		rm -f *.o virus.elf virus

# --- auto-added: force relink when crt0.o/libc.a change ---
virus: $(TOPSRC)/lib/crt0.o $(TOPSRC)/lib/libc.a
