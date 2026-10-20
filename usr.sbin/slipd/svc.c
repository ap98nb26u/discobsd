/*
 * svc.c - the TCP services slipd offers.
 *
 *	echo (7)	sends back what it receives
 *	discard (9)	takes and drops everything (receive throughput)
 *	chargen (19)	sends a rotating character pattern (send throughput)
 *	telnet (23)	a login on a pseudo-terminal (telnet.c)
 */
#include <string.h>
#include "slipd.h"

/* echo: whatever fits in the send buffer is taken; the rest waits */
static int
echo_input(struct tcb *tp, const unsigned char *b, int n)
{
	return tcp_write(tp, b, n);
}

static int
echo_room(struct tcb *tp)
{
	return tcp_room(tp);
}

static const struct service echo_svc = {
	7, "echo", 0, echo_input, echo_room, 0, 0, 0
};

static int
discard_input(struct tcb *tp, const unsigned char *b, int n)
{
	return n;
}

static const struct service discard_svc = {
	9, "discard", 0, discard_input, 0, 0, 0, 0
};

/* chargen: RFC 864's 72-column lines, rotating through printable ASCII */
static void
chargen_poll(struct tcb *tp)
{
	unsigned char line[74];
	long pos = (long)tcp_appdata(tp);
	int i;

	while (tcp_room(tp) >= (int)sizeof(line)) {
		for (i = 0; i < 72; i++)
			line[i] = ' ' + (pos + i) % 95;
		line[72] = '\r';
		line[73] = '\n';
		if (tcp_write(tp, line, sizeof(line)) == 0)
			break;
		pos = (pos + 1) % 95;
	}
	tcp_setappdata(tp, (void *)pos);
}

static const struct service chargen_svc = {
	19, "chargen", 0, discard_input, 0, 0, 0, chargen_poll
};

void
svc_init(void)
{
	tcp_listen(&echo_svc);
	tcp_listen(&discard_svc);
	tcp_listen(&chargen_svc);
	telnet_init();
}
