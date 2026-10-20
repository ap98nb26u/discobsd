/*
 * slipd - a small IP host on the GBA's link-cable UART, over SLIP.
 *
 *	slipd [-v] [-a addr] [-b baud] [-q ms] [-t seconds] [-l logfile] [-d dev]
 *
 * Opens /dev/sio (which takes the UART off the serial console while it is
 * open) and answers as the IP address addr (default 192.168.7.2) on a
 * point-to-point SLIP link.  It speaks IPv4, ICMP echo (ping) and TCP,
 * with the echo, discard and chargen services (svc.c).  The peer is a Linux TUN
 * interface (tools/gbanet/slip_tun.py), which needs no SLIP driver.
 *
 * -b sets the line rate (9600, 38400, 57600 or 115200; default 115200,
 * the serial console's own rate) - the peer must match.  The GBA UART has
 * no receive interrupt and a 4-byte FIFO (see gba_sio_uart.c); at 115200
 * the peer must pad each packet with about 40 SLIP END bytes, as
 * slip_tun.py does, to cover slipd waking up.  -q sets how long the line
 * must stay quiet before a read of a burst ends (default 10 ms, to ride over the USB serial
 * adapter's pauses).  -v logs each packet to stderr
 * (the LCD console).  -l keeps every rejected frame (reason, length and
 * bytes, in hex) and writes them to logfile on exit, for finding where
 * bytes go missing on the line.  -t exits after that
 * many seconds without traffic, which gives the serial console back -
 * handy while testing over the same cable.
 *
 * The whole stack lives in this one process: no kernel networking.
 * TCP (server side) is in tcp.c and the services it offers in svc.c.
 *
 * -d uses another device in place of /dev/sio - e.g. a pty, to run slipd
 * on a Linux host against tools/gbanet/slip_tun.py --pty for testing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include "slipd.h"

#define SLIP_END	0xC0
#define SLIP_ESC	0xDB
#define SLIP_ESC_END	0xDC
#define SLIP_ESC_ESC	0xDD

/* /dev/sio's set-rate ioctl (sys/arch/gba/dev/gba_sio_uart.h) */
#define SIOSETBAUD	_IOW('G', 1, int)
#define SIOSETQUIET	_IOW('G', 2, int)
#define SIOGETSTAT	_IOR('G', 3, unsigned long[2])
#define IPPROTO_ICMP_	1

static int sio = -1;
int verbose;
unsigned char myaddr[4] = { 192, 168, 7, 2 };

static unsigned char rxpkt[MTU];	/* packet being received */
static int rxlen, rxesc, rxdrop;

static unsigned long stat_in, stat_out, stat_bad;

static char *badlog;			/* -l: rejected frames, written at exit */
static int badlen;
#define BADLOG	16384

static void
bad(const char *why, const unsigned char *p, int n)
{
	int i;

	stat_bad++;
	if (!badlog || badlen > BADLOG - 3 * n - 64)
		return;
	badlen += sprintf(badlog + badlen, "%s len %d:", why, n);
	for (i = 0; i < n; i++)
		badlen += sprintf(badlog + badlen, " %02x", p[i]);
	badlog[badlen++] = '\n';
}

/* ---- utilities -------------------------------------------------------- */

unsigned
get16(const unsigned char *p)
{
	return (p[0] << 8) | p[1];
}

void
put16(unsigned char *p, unsigned v)
{
	p[0] = v >> 8;
	p[1] = v;
}

unsigned long
get32(const unsigned char *p)
{
	return ((unsigned long)get16(p) << 16) | get16(p + 2);
}

void
put32(unsigned char *p, unsigned long v)
{
	put16(p, (unsigned)(v >> 16) & 0xFFFF);
	put16(p + 2, (unsigned)v & 0xFFFF);
}

time_t
now(void)
{
	return time((time_t *)0);
}

/* one's-complement sum of n bytes, not yet folded or inverted */
unsigned long
sum16(const unsigned char *p, int n, unsigned long s)
{
	while (n > 1) {
		s += get16(p);
		p += 2;
		n -= 2;
	}
	if (n)
		s += p[0] << 8;
	return s;
}

unsigned
fold(unsigned long s)
{
	while (s >> 16)
		s = (s & 0xFFFF) + (s >> 16);
	return ~s & 0xFFFF;
}

static void
logpkt(const char *dir, const unsigned char *p, int n)
{
	if (!verbose)
		return;
	if (n >= 20)
		fprintf(stderr, "%s %d.%d.%d.%d > %d.%d.%d.%d proto %d len %d\n",
		    dir, p[12], p[13], p[14], p[15], p[16], p[17], p[18], p[19],
		    p[9], n);
	else
		fprintf(stderr, "%s short packet, %d bytes\n", dir, n);
}

/* ---- SLIP ------------------------------------------------------------- */

/*
 * Output queue.  A write to /dev/sio can come back short - the kernel
 * stops sending when its small receive ring is close to full, so that we
 * read first (see siowrite() in gba_sio_uart.c) - so encoded frames wait
 * here and the main loop writes them out between reads.
 */
#define TXQ	4096
static unsigned char txq[TXQ];
static int txhead, txlen;

/* free space in the output queue, in packet bytes (worst-case SLIP size) */
int
ip_room(void)
{
	int n = (TXQ - txlen - 2) / 2;

	return n < 0 ? 0 : n;
}

static void
txput(int c)
{
	txq[(txhead + txlen++) % TXQ] = c;
}

static void
slip_send(const unsigned char *p, int n)
{
	int i;

	if (ip_room() < n) {
		if (verbose)
			fprintf(stderr, "slipd: output queue full, dropped\n");
		return;		/* TCP sends it again; a ping is lost */
	}
	txput(SLIP_END);
	for (i = 0; i < n; i++) {
		if (p[i] == SLIP_END) {
			txput(SLIP_ESC);
			txput(SLIP_ESC_END);
		} else if (p[i] == SLIP_ESC) {
			txput(SLIP_ESC);
			txput(SLIP_ESC_ESC);
		} else
			txput(p[i]);
	}
	txput(SLIP_END);
	logpkt("out", p, n);
	stat_out++;
}

/* write out what the line takes now; -1 on error */
static int
tx_flush(void)
{
	int n, k;

	while (txlen > 0) {
		n = TXQ - txhead;
		if (n > txlen)
			n = txlen;
		k = write(sio, txq + txhead, n);
		if (k < 0) {
			perror("slipd: write");
			return -1;
		}
		txhead = (txhead + k) % TXQ;
		txlen -= k;
		if (k < n)
			break;		/* the receive side needs a read first */
	}
	return 0;
}

/* ---- IP / ICMP -------------------------------------------------------- */

/* send an IP packet whose header (20 bytes, no options) is filled in
   except for the checksum */
void
ip_send(unsigned char *p, int n)
{
	p[10] = p[11] = 0;
	put16(p + 10, fold(sum16(p, 20, 0)));
	slip_send(p, n);
}

static void
icmp_input(unsigned char *p, int hlen, int n)
{
	unsigned char *icmp = p + hlen;
	int ilen = n - hlen;
	unsigned char tmp[4];

	if (ilen < 8 || fold(sum16(icmp, ilen, 0)) != 0) {
		bad("icmp", p, n);
		return;
	}
	if (icmp[0] != 8)		/* only echo request */
		return;

	/* echo reply: same packet with addresses swapped, type 0 */
	icmp[0] = 0;
	icmp[2] = icmp[3] = 0;
	put16(icmp + 2, fold(sum16(icmp, ilen, 0)));
	if (hlen != 20) {		/* drop IP options from the reply */
		memmove(p + 20, icmp, ilen);
		hlen = 20;
		n = 20 + ilen;
	}
	p[0] = 0x45;
	put16(p + 2, n);
	p[6] = p[7] = 0;		/* no fragmentation flags */
	p[8] = 64;			/* TTL */
	memcpy(tmp, p + 12, 4);
	memcpy(p + 12, p + 16, 4);
	memcpy(p + 16, tmp, 4);
	ip_send(p, n);
}

static void
ip_input(unsigned char *p, int n)
{
	int hlen, tlen;

	logpkt("in ", p, n);
	stat_in++;
	if (n < 20 || (p[0] >> 4) != 4) {
		bad("hdr", p, n);
		return;
	}
	hlen = (p[0] & 15) * 4;
	tlen = get16(p + 2);
	if (hlen < 20 || tlen < hlen || tlen > n ||
	    fold(sum16(p, hlen, 0)) != 0) {
		bad("ip", p, n);
		return;
	}
	n = tlen;			/* ignore link padding */
	if (memcmp(p + 16, myaddr, 4) != 0)
		return;			/* not for us */
	if ((get16(p + 6) & 0x3FFF) != 0)
		return;			/* a fragment: not supported */

	switch (p[9]) {
	case IPPROTO_ICMP_:
		icmp_input(p, hlen, n);
		break;
	case 6:				/* TCP */
		tcp_input(p, hlen, n);
		break;
	default:
		break;
	}
}

/* feed received bytes through the SLIP decoder */
static void
slip_input(const unsigned char *b, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		int c = b[i];

		if (c == SLIP_END) {
			if (rxlen > 0 && !rxdrop)
				ip_input(rxpkt, rxlen);
			rxlen = rxesc = rxdrop = 0;
			continue;
		}
		if (rxesc) {
			rxesc = 0;
			if (c == SLIP_ESC_END)
				c = SLIP_END;
			else if (c == SLIP_ESC_ESC)
				c = SLIP_ESC;
		} else if (c == SLIP_ESC) {
			rxesc = 1;
			continue;
		}
		if (rxlen < MTU)
			rxpkt[rxlen++] = c;
		else
			rxdrop = 1;	/* oversized: discard the frame */
	}
}

/* ---- main ------------------------------------------------------------- */

static int
parse_addr(const char *s, unsigned char *a)
{
	int i, v;

	for (i = 0; i < 4; i++) {
		if (*s < '0' || *s > '9')
			return -1;
		for (v = 0; *s >= '0' && *s <= '9'; s++)
			v = v * 10 + (*s - '0');
		if (v > 255 || (i < 3 && *s++ != '.'))
			return -1;
		a[i] = v;
	}
	return *s ? -1 : 0;
}

static void
usage(void)
{
	fprintf(stderr, "usage: slipd [-v] [-a addr] [-b baud] [-q ms] [-t seconds] [-l logfile] [-d dev]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	static unsigned char buf[MTU + 100];	/* a whole packet per read */
	int idle = 0, baud = 115200, quiet = 10, i, n;
	unsigned long kst[2];
	char *logfile = NULL, *dev = "/dev/sio";
	time_t lasttick = 0, lastrx;
	int is_sio;
	struct timeval tv;
	fd_set rf;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v"))
			verbose = 1;
		else if (!strcmp(argv[i], "-a") && i + 1 < argc) {
			if (parse_addr(argv[++i], myaddr) < 0)
				usage();
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc)
			idle = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-b") && i + 1 < argc)
			baud = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-q") && i + 1 < argc)
			quiet = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-d") && i + 1 < argc)
			dev = argv[++i];
		else if (!strcmp(argv[i], "-l") && i + 1 < argc) {
			logfile = argv[++i];
			badlog = malloc(BADLOG);
		}
		else
			usage();
	}

	sio = open(dev, O_RDWR);
	if (sio < 0) {
		perror(dev);
		return 1;
	}
	is_sio = !strcmp(dev, "/dev/sio");
	if (is_sio && baud && ioctl(sio, SIOSETBAUD, &baud) < 0) {
		perror("slipd: baud rate");
		return 1;
	}
	if (is_sio && quiet && ioctl(sio, SIOSETQUIET, &quiet) < 0) {
		perror("slipd: quiet time");
		return 1;
	}
	fprintf(stderr, "slipd: %d.%d.%d.%d on %s\n",
	    myaddr[0], myaddr[1], myaddr[2], myaddr[3], dev);
	svc_init();
	lastrx = now();

	for (;;) {
		FD_ZERO(&rf);
		FD_SET(sio, &rf);
		tv.tv_sec = txlen ? 0 : 1;	/* TCP timers run once a second */
		tv.tv_usec = 0;
		n = select(sio + 1, &rf, NULL, NULL, &tv);
		if (n < 0) {
			perror("slipd: select");
			break;
		}
		if (n > 0) {
			n = read(sio, buf, sizeof(buf));
			if (n < 0) {
				perror("slipd: read");
				break;
			}
			if (n == 0)
				break;		/* (a pty's other end went away) */
			slip_input(buf, n);
			lastrx = now();
		}
		if (now() != lasttick) {
			lasttick = now();
			tcp_timer();
		}
		tcp_poll();
		if (tx_flush() < 0)
			break;
		if (idle && now() - lastrx >= idle)
			break;			/* -t: quiet for that long */
	}
	kst[0] = kst[1] = 0;
	if (is_sio)
		ioctl(sio, SIOGETSTAT, kst);
	fprintf(stderr, "slipd: exit (in %lu out %lu bad %lu; line %lu bytes, %lu errors)\n",
	    stat_in, stat_out, stat_bad, kst[0], kst[1]);
	if (logfile) {
		FILE *f = fopen(logfile, "w");

		if (f) {
			fprintf(f, "in %lu out %lu bad %lu; line %lu bytes, %lu errors\n",
			    stat_in, stat_out, stat_bad, kst[0], kst[1]);
			fwrite(badlog, 1, badlen, f);
			fclose(f);
		}
	}
	close(sio);
	return 0;
}
