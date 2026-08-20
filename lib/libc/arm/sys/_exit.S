/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include "SYS.h"

#ifndef MACHINE_GBA

ENTRY(_exit)
	svc	#SYS_exit
END(_exit)

#else /* GBA */

ENTRY(_exit)
	CALL_SIMSYS(SYS_exit)
END(_exit)

#endif
