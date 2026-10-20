/*
 * tcp.c - a small server-side TCP for slipd.
 *
 * Passive opens only: services register a port with tcp_listen(), and a
 * SYN to it creates a connection.  Up to NTCB connections at a time.
 * Segments are accepted only in order (anything else is answered with an
 * ACK and dropped, and the peer resends).  Each connection keeps the data
 * it has sent until it is acknowledged, and on a timeout resends from the
 * oldest unacknowledged byte (go-back-N).  The clock is in whole seconds
 * (DiscoBSD's tv_usec does not advance), so the retransmit timeout starts
 * at 1 s and doubles; after MAXRETRY the connection is reset.
 */
#include <stdio.h>
#include <string.h>
#include "slipd.h"

#define NTCB		4
#define NLISTEN		8
#define TXBUF		2048		/* per-connection send buffer */
#define MYMSS		512		/* what we accept per segment */
#define MAXRETRY	8
#define RTO_MAX		16
#define TIMEWAIT	2		/* seconds in TIME_WAIT */

#define TH_FIN	0x01
#define TH_SYN	0x02
#define TH_RST	0x04
#define TH_PSH	0x08
#define TH_ACK	0x10

enum {
	CLOSED, SYN_RCVD, ESTABLISHED, CLOSE_WAIT, LAST_ACK,
	FIN_WAIT_1, FIN_WAIT_2, CLOSING, TIME_WAIT
};
static const char *stname[] = {
	"CLOSED", "SYN_RCVD", "ESTABLISHED", "CLOSE_WAIT", "LAST_ACK",
	"FIN_WAIT_1", "FIN_WAIT_2", "CLOSING", "TIME_WAIT"
};

typedef unsigned int seq_t;	/* 32 bits, wrapping (int is 32-bit here) */

struct tcb {
	int		state;
	unsigned char	raddr[4];
	unsigned	rport, lport;
	seq_t		iss;
	seq_t		snd_una;	/* oldest unacknowledged */
	seq_t		snd_nxt;	/* next to send */
	unsigned	snd_wnd;	/* peer's window */
	seq_t		rcv_nxt;	/* next expected */
	unsigned	rcv_adv;	/* window we last advertised */
	int		ack_pending;	/* data in, not yet acknowledged */
	unsigned	mss;		/* largest segment we send */
	unsigned char	tx[TXBUF];	/* data from snd_una on */
	int		txstart, txlen;
	int		fin_pending;	/* app closed: FIN after the data */
	int		fin_sent;	/* FIN is at snd_una + txlen */
	time_t		rexmt_at;	/* 0: no timer */
	int		rto, retries;
	time_t		tw_until;
	const struct service *svc;
	void		*app;
};

static struct tcb tcbs[NTCB];
static const struct service *listeners[NLISTEN];
static unsigned long isn_seed;

#define SEQ_LT(a, b)	((int)((seq_t)(a) - (seq_t)(b)) < 0)
#define SEQ_LEQ(a, b)	((int)((seq_t)(a) - (seq_t)(b)) <= 0)

static void
newstate(struct tcb *tp, int st)
{
	if (verbose)
		fprintf(stderr, "tcp %u<-%d.%d.%d.%d:%u %s -> %s\n", tp->lport,
		    tp->raddr[0], tp->raddr[1], tp->raddr[2], tp->raddr[3],
		    tp->rport, stname[tp->state], stname[st]);
	tp->state = st;
}

void
tcp_listen(const struct service *svc)
{
	int i;

	for (i = 0; i < NLISTEN; i++)
		if (!listeners[i]) {
			listeners[i] = svc;
			return;
		}
}

void *tcp_appdata(struct tcb *tp) { return tp->app; }
void tcp_setappdata(struct tcb *tp, void *d) { tp->app = d; }

/* bytes the app has queued that are not yet sent */
int
tcp_unsent(struct tcb *tp)
{
	return tp->txlen - (int)(tp->snd_nxt - tp->snd_una) +
	    (tp->fin_sent && tp->snd_nxt != tp->snd_una ? 1 : 0);
}

static int
inflight(struct tcb *tp)
{
	return (int)(seq_t)(tp->snd_nxt - tp->snd_una);
}

/*
 * Our receive window: what the service can take, but never more than one
 * segment.  The kernel's receive ring holds only 64 bytes; a segment that
 * arrives while we are still busy with the one before it overruns that
 * ring and loses a byte, so the whole segment is dropped and resent.  One
 * segment at a time keeps the line quiet while we work.
 */
static unsigned
rcv_window(struct tcb *tp)
{
	int w = tp->svc && tp->svc->room ? tp->svc->room(tp) : MYMSS;

	if (w < 0)
		w = 0;
	if (w > MYMSS)
		w = MYMSS;
	return w;
}

/*
 * Build and send one segment: seq, flags, and len bytes of data from the
 * send buffer at offset off (bytes after snd_una).  A SYN carries our MSS.
 */
static void
send_seg(struct tcb *tp, seq_t seq, int flags, int off, int len)
{
	static unsigned char pkt[MTU];
	unsigned char *th = pkt + 20;
	int thlen = (flags & TH_SYN) ? 24 : 20, i, n;
	unsigned long s;

	memset(pkt, 0, 20 + thlen);
	pkt[0] = 0x45;
	put16(pkt + 4, (unsigned)(isn_seed++ & 0xFFFF));	/* IP id */
	pkt[8] = 64;
	pkt[9] = 6;
	memcpy(pkt + 12, myaddr, 4);
	memcpy(pkt + 16, tp->raddr, 4);

	put16(th + 0, tp->lport);
	put16(th + 2, tp->rport);
	put32(th + 4, seq);
	put32(th + 8, tp->rcv_nxt);
	th[12] = (thlen / 4) << 4;
	th[13] = flags;
	tp->rcv_adv = rcv_window(tp);
	put16(th + 14, tp->rcv_adv);
	tp->ack_pending = 0;		/* this one carries the ACK */
	if (flags & TH_SYN) {
		th[20] = 2;		/* MSS option */
		th[21] = 4;
		put16(th + 22, MYMSS);
	}
	for (i = 0; i < len; i++)
		th[thlen + i] = tp->tx[(tp->txstart + off + i) % TXBUF];

	n = 20 + thlen + len;
	put16(pkt + 2, n);
	/* checksum over the pseudo-header and the segment */
	s = sum16(pkt + 12, 8, 0);
	s += 6 + thlen + len;
	s = sum16(th, thlen + len, s);
	put16(th + 16, fold(s));
	ip_send(pkt, n);
}

/* a reply RST to a segment that belongs to no connection */
static void
send_reset(const unsigned char *ip, const unsigned char *th, int dlen)
{
	struct tcb t;
	int flags = th[13];

	memset(&t, 0, sizeof(t));
	memcpy(t.raddr, ip + 12, 4);
	t.rport = get16(th);
	t.lport = get16(th + 2);
	t.state = CLOSED;
	if (flags & TH_ACK) {
		send_seg(&t, (seq_t)get32(th + 8), TH_RST, 0, 0);
	} else {
		t.rcv_nxt = (seq_t)get32(th + 4) + dlen + (flags & TH_SYN ? 1 : 0) +
		    (flags & TH_FIN ? 1 : 0);
		send_seg(&t, 0, TH_RST | TH_ACK, 0, 0);
	}
}

static void
start_timer(struct tcb *tp)
{
	if (!tp->rexmt_at)
		tp->rexmt_at = now() + tp->rto;
}

/* send whatever the window allows, then a FIN if the app has closed */
static void
tcp_output(struct tcb *tp)
{
	int unsent, win, len;

	if (tp->state == SYN_RCVD || tp->state == CLOSED || tp->state == TIME_WAIT)
		return;
	for (;;) {
		unsent = tp->txlen - inflight(tp);
		if (tp->fin_sent && inflight(tp) > tp->txlen)
			unsent = 0;		/* the FIN is out */
		win = (int)tp->snd_wnd - inflight(tp);
		if (unsent <= 0 || win <= 0)
			break;
		len = unsent;
		if (len > (int)tp->mss)
			len = tp->mss;
		if (len > win)
			len = win;
		if (ip_room() < 40 + len + 2 * 40)
			break;			/* line busy; keep room for ACKs */
		send_seg(tp, tp->snd_nxt, TH_ACK | TH_PSH, inflight(tp), len);
		tp->snd_nxt += len;
		start_timer(tp);
	}
	if (tp->fin_pending && !tp->fin_sent && inflight(tp) == tp->txlen) {
		send_seg(tp, tp->snd_nxt, TH_FIN | TH_ACK, 0, 0);
		tp->snd_nxt++;
		tp->fin_sent = 1;
		if (tp->state == ESTABLISHED)
			newstate(tp, FIN_WAIT_1);
		else if (tp->state == CLOSE_WAIT)
			newstate(tp, LAST_ACK);
		start_timer(tp);
	}
}

static void
send_ack(struct tcb *tp)
{
	send_seg(tp, tp->snd_nxt, TH_ACK, 0, 0);
}

static void
drop(struct tcb *tp)
{
	const struct service *svc = tp->svc;

	if (tp->state != CLOSED) {
		newstate(tp, CLOSED);
		if (svc && svc->closed)
			svc->closed(tp);
	}
	memset(tp, 0, sizeof(*tp));
}

int
tcp_write(struct tcb *tp, const unsigned char *b, int n)
{
	int i, room = TXBUF - tp->txlen;

	if (tp->fin_pending || (tp->state != ESTABLISHED && tp->state != CLOSE_WAIT))
		return 0;
	if (n > room)
		n = room;
	for (i = 0; i < n; i++)
		tp->tx[(tp->txstart + tp->txlen + i) % TXBUF] = b[i];
	tp->txlen += n;
	return n;		/* tcp_poll() sends it, in full-sized segments */
}

int
tcp_room(struct tcb *tp)
{
	return TXBUF - tp->txlen;
}

void
tcp_close(struct tcb *tp)
{
	if (tp->fin_pending)
		return;
	tp->fin_pending = 1;
	tcp_output(tp);
}

void
tcp_abort(struct tcb *tp)
{
	send_seg(tp, tp->snd_nxt, TH_RST | TH_ACK, 0, 0);
	drop(tp);
}

/* the peer acknowledged up to ack */
static void
ack_input(struct tcb *tp, seq_t ack)
{
	int acked;

	if (!SEQ_LT(tp->snd_una, ack) || SEQ_LT(tp->snd_nxt, ack))
		return;			/* nothing new, or acks unsent data */
	acked = (int)(ack - tp->snd_una);
	if (tp->fin_sent && ack == tp->snd_nxt) {
		acked--;		/* the FIN's sequence number */
		tp->fin_pending = 2;	/* our FIN is acknowledged */
	}
	if (acked > tp->txlen)
		acked = tp->txlen;
	tp->txstart = (tp->txstart + acked) % TXBUF;
	tp->txlen -= acked;
	tp->snd_una = ack;
	tp->retries = 0;
	tp->rto = 1;
	tp->rexmt_at = 0;
	if (inflight(tp))
		start_timer(tp);
}

void
tcp_input(unsigned char *ip, int hlen, int n)
{
	unsigned char *th = ip + hlen;
	int tlen = n - hlen, thlen, dlen, flags, i, took;
	seq_t seq, ack;
	unsigned long s;
	unsigned sport, dport;
	struct tcb *tp, *freetp;
	const unsigned char *data;

	if (tlen < 20)
		return;
	s = sum16(ip + 12, 8, 0) + 6 + tlen;
	if (fold(sum16(th, tlen, s)) != 0)
		return;			/* bad checksum */
	sport = get16(th);
	dport = get16(th + 2);
	seq = (seq_t)get32(th + 4);
	ack = (seq_t)get32(th + 8);
	thlen = (th[12] >> 4) * 4;
	flags = th[13];
	if (thlen < 20 || thlen > tlen)
		return;
	data = th + thlen;
	dlen = tlen - thlen;

	freetp = 0;
	for (tp = 0, i = 0; i < NTCB; i++) {
		struct tcb *t = &tcbs[i];

		if (t->state == CLOSED) {
			if (!freetp)
				freetp = t;
			continue;
		}
		if (t->lport == dport && t->rport == sport &&
		    memcmp(t->raddr, ip + 12, 4) == 0) {
			tp = t;
			break;
		}
	}

	if (!tp) {
		const struct service *svc = 0;

		if (flags & TH_RST)
			return;
		for (i = 0; i < NLISTEN; i++)
			if (listeners[i] && listeners[i]->port == dport)
				svc = listeners[i];
		if (!(flags & TH_SYN) || (flags & TH_ACK) || !svc || !freetp) {
			send_reset(ip, th, dlen);
			return;
		}
		/* passive open */
		tp = freetp;
		memset(tp, 0, sizeof(*tp));
		memcpy(tp->raddr, ip + 12, 4);
		tp->rport = sport;
		tp->lport = dport;
		tp->svc = svc;
		tp->rcv_nxt = seq + 1;
		tp->iss = (seq_t)now() * 250000U + (seq_t)(isn_seed += 64000);
		tp->snd_una = tp->iss;
		tp->snd_nxt = tp->iss + 1;
		tp->snd_wnd = get16(th + 14);
		tp->mss = 536;
		for (i = 20; i + 1 < thlen; ) {	/* options: take the MSS */
			int kind = th[i], olen;

			if (kind == 0)
				break;
			if (kind == 1) {
				i++;
				continue;
			}
			olen = th[i + 1];
			if (olen < 2)
				break;
			if (kind == 2 && olen == 4 && get16(th + i + 2) < tp->mss)
				tp->mss = get16(th + i + 2);
			i += olen;
		}
		if (tp->mss > MYMSS)
			tp->mss = MYMSS;
		tp->rto = 1;
		tp->state = CLOSED;
		newstate(tp, SYN_RCVD);
		send_seg(tp, tp->iss, TH_SYN | TH_ACK, 0, 0);
		start_timer(tp);
		return;
	}

	if (flags & TH_RST) {
		if (SEQ_LEQ(tp->rcv_nxt, seq) &&
		    SEQ_LT(seq, tp->rcv_nxt + rcv_window(tp) + 1))
			drop(tp);
		return;
	}
	if (flags & TH_SYN) {
		/* a retransmitted SYN: answer again; anything else gets an ACK */
		if (tp->state == SYN_RCVD && seq + 1 == tp->rcv_nxt)
			send_seg(tp, tp->iss, TH_SYN | TH_ACK, 0, 0);
		else
			send_ack(tp);
		return;
	}
	if (!(flags & TH_ACK))
		return;

	if (tp->state == SYN_RCVD) {
		if (ack != tp->iss + 1) {
			send_reset(ip, th, dlen);
			return;
		}
		tp->snd_una = ack;
		tp->rexmt_at = 0;
		tp->retries = 0;
		newstate(tp, ESTABLISHED);
		if (tp->svc->open)
			tp->svc->open(tp);
		if (tp->state == CLOSED)
			return;
	}

	ack_input(tp, ack);
	tp->snd_wnd = get16(th + 14);

	if (tp->fin_pending == 2) {		/* our FIN was acknowledged */
		if (tp->state == FIN_WAIT_1)
			newstate(tp, FIN_WAIT_2);
		else if (tp->state == CLOSING) {
			newstate(tp, TIME_WAIT);
			tp->tw_until = now() + TIMEWAIT;
		} else if (tp->state == LAST_ACK) {
			drop(tp);
			return;
		}
	}

	/* data, in order only */
	if (dlen > 0 && (tp->state == ESTABLISHED ||
	    tp->state == FIN_WAIT_1 || tp->state == FIN_WAIT_2)) {
		if (SEQ_LT(seq, tp->rcv_nxt)) {		/* partly old */
			int old = (int)(tp->rcv_nxt - seq);

			if (old >= dlen) {
				send_ack(tp);
				dlen = 0;
			} else {
				data += old;
				dlen -= old;
				seq = tp->rcv_nxt;
			}
		}
		if (dlen > 0) {
			if (seq != tp->rcv_nxt) {
				send_ack(tp);		/* out of order: drop */
				return;
			}
			took = tp->svc->input ? tp->svc->input(tp, data, dlen) : dlen;
			if (tp->state == CLOSED)
				return;
			if (took < 0)
				took = 0;
			tp->rcv_nxt += took;
			if (took < dlen)
				flags &= ~TH_FIN;	/* FIN only after all data */
			/*
			 * Acknowledge from tcp_poll(), riding on whatever the
			 * service sends back (echo's copy).  The peer then
			 * sends its next segment only once ours is out, so
			 * nothing arrives while we send.
			 */
			tp->ack_pending = 1;
		}
	}

	if ((flags & TH_FIN) && seq + dlen == tp->rcv_nxt) {
		tp->rcv_nxt++;
		send_ack(tp);
		switch (tp->state) {
		case ESTABLISHED:
			newstate(tp, CLOSE_WAIT);
			if (tp->svc->eof)
				tp->svc->eof(tp);
			else
				tcp_close(tp);
			break;
		case FIN_WAIT_1:
			newstate(tp, tp->fin_pending == 2 ? TIME_WAIT : CLOSING);
			tp->tw_until = now() + TIMEWAIT;
			break;
		case FIN_WAIT_2:
			newstate(tp, TIME_WAIT);
			tp->tw_until = now() + TIMEWAIT;
			break;
		}
		return;
	}
	if (tp->state != CLOSED)
		tcp_output(tp);
}

/* once a second or so: retransmits, TIME_WAIT, zero-window probes */
void
tcp_timer(void)
{
	time_t t = now();
	int i;

	for (i = 0; i < NTCB; i++) {
		struct tcb *tp = &tcbs[i];

		if (tp->state == CLOSED)
			continue;
		if (tp->state == TIME_WAIT) {
			if (t >= tp->tw_until)
				drop(tp);
			continue;
		}
		if (tp->rexmt_at && t >= tp->rexmt_at) {
			tp->rexmt_at = 0;
			if (++tp->retries > MAXRETRY) {
				if (verbose)
					fprintf(stderr, "tcp %u: timed out\n", tp->lport);
				tcp_abort(tp);
				continue;
			}
			tp->rto *= 2;
			if (tp->rto > RTO_MAX)
				tp->rto = RTO_MAX;
			if (tp->state == SYN_RCVD) {
				send_seg(tp, tp->iss, TH_SYN | TH_ACK, 0, 0);
				start_timer(tp);
				continue;
			}
			/* go back to the oldest unacknowledged byte */
			tp->snd_nxt = tp->snd_una;
			if (tp->fin_sent) {
				tp->fin_sent = 0;
				if (tp->state == FIN_WAIT_1)
					tp->state = ESTABLISHED;
				else if (tp->state == LAST_ACK || tp->state == CLOSING)
					tp->state = CLOSE_WAIT;
			}
			if (tp->snd_wnd == 0 && tp->txlen > 0) {
				/* zero window: probe with one byte */
				send_seg(tp, tp->snd_nxt, TH_ACK, 0, 1);
				tp->snd_nxt++;
				start_timer(tp);
			} else
				tcp_output(tp);
			if (!tp->rexmt_at && inflight(tp))
				start_timer(tp);
		} else if (!tp->rexmt_at && tp->snd_wnd == 0 && tp->txlen > 0)
			start_timer(tp);		/* arm the persist probe */
	}
}

/*
 * Each main-loop pass: let services push data (e.g. from a pty), send it,
 * and tell the peer when our window has opened up again (after the
 * service freed room), or it may wait for its own persist timer.
 */
void
tcp_poll(void)
{
	int i;

	for (i = 0; i < NTCB; i++) {
		struct tcb *tp = &tcbs[i];

		if ((tp->state == ESTABLISHED || tp->state == CLOSE_WAIT) &&
		    tp->svc->poll)
			tp->svc->poll(tp);
		if (tp->state == CLOSED || tp->state == SYN_RCVD ||
		    tp->state == TIME_WAIT)
			continue;
		tcp_output(tp);
		if (tp->ack_pending ||
		    rcv_window(tp) >= tp->rcv_adv + MYMSS / 2 ||
		    (tp->rcv_adv == 0 && rcv_window(tp) > 0))
			send_ack(tp);		/* delayed ACK or window update */
	}
}
