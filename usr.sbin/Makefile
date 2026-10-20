#
# Public domain - 1996/10/24 - sms
#
#	@(#)Makefile	1.2 (2.11BSD) 1997/6/27
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

# GBA-only: slipd needs the link-cable UART's /dev/sio.
MDSUBDIR!=if [ x"${MACHINE}" = x"gba" ] ; then \
		echo "slipd" ; \
	else \
		echo "" ; \
	fi

SUBDIR=	chroot cron disktool iostat mkpasswd pstat talloc \
	update vipw ${MDSUBDIR}

# TODO: devupdate

all:	${SUBDIR}

${SUBDIR}: FRC
	cd $@; ${MAKE} ${MFLAGS} all

FRC:

install: FRC
	-for i in ${SUBDIR}; do \
		(cd $$i; ${MAKE} ${MFLAGS} DESTDIR=${DESTDIR} install); done

clean:
	/bin/rm -f a.out core *.s *.o *.dis *.elf *~ errs
	for i in ${SUBDIR}; do (cd $$i; ${MAKE} ${MFLAGS} clean); done
