/*
 * telnet.c - the telnet service (port 23) slipd offers: each connection
 * gets a pseudo-terminal with login(1) on the slave side, and slipd
 * copies between the TCP connection and the pty master.
 *
 * The protocol side is minimal: we offer WILL ECHO and WILL SGA (the
 * client goes to character-at-a-time mode and leaves echoing to the
 * pty) and ask for NAWS (the client's window size, passed on to the pty
 * so vi and the like fit), refuse every other option, skip other
 * subnegotiations, turn the
 * client's CR LF / CR NUL back into the CR a terminal sends, and double
 * 0xFF (IAC) in output.
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
#include <sys/wait.h>
#ifdef __linux__
#include <termios.h>
#else
#include <sgtty.h>
#endif
#include "slipd.h"

#define NSESS		2		/* the GBA kernel has 2 ptys */
#define INBUF		256		/* client -> pty, waiting to be written */

#define IAC		255
#define DONT		254
#define DO		253
#define WONT		252
#define WILL		251
#define SB		250
#define SE		240
#define OPT_ECHO	1
#define OPT_SGA		3
#define OPT_NAWS	31

enum { S_DATA, S_IAC, S_OPT, S_SB, S_SBIAC, S_CR };

struct sess {
	struct tcb	*tp;
	int		mfd;		/* pty master, -1: none */
	int		pid;		/* login, 0: gone */
	int		state, optcmd;
	unsigned char	sb[8];		/* subnegotiation: option, data */
	int		sblen;
	unsigned char	in[INBUF];
	int		inlen;
};

static struct sess sess[NSESS];
static volatile int child_died;

char *login_prog = "/usr/bin/login";	/* -L */

static void
onchld(int sig)
{
	child_died = 1;
	signal(SIGCHLD, onchld);
}

/*
 * A free pseudo-terminal: the master open, the slave's name in slave.
 */
static int
pty_open(char *slave, int len)
{
#ifdef __linux__
	int fd = posix_openpt(O_RDWR | O_NOCTTY);

	if (fd < 0 || grantpt(fd) < 0 || unlockpt(fd) < 0) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	snprintf(slave, len, "%s", ptsname(fd));
	return fd;
#else
	char m[16];
	int i, fd;

	for (i = 0; i < NSESS; i++) {
		sprintf(m, "/dev/ptyp%d", i);
		fd = open(m, O_RDWR);
		if (fd >= 0) {
			sprintf(slave, "/dev/ttyp%d", i);
			return fd;
		}
	}
	return -1;
#endif
}

/* in the child: make slave the controlling terminal and run login */
static void
run_login(const char *slave, const char *host)
{
	int fd;
#ifdef __linux__
	setsid();
#else
	struct sgttyb sg;

	fd = open("/dev/tty", O_RDWR);
	if (fd >= 0) {			/* leave slipd's terminal */
		ioctl(fd, TIOCNOTTY, 0);
		close(fd);
	}
	setpgrp(0, 0);			/* the next tty opened becomes ours */
#endif
	fd = open(slave, O_RDWR);
	if (fd < 0)
		_exit(1);
#ifdef __linux__
	ioctl(fd, TIOCSCTTY, 0);
#else
	/* a fresh pty starts with no modes at all */
	if (ioctl(fd, TIOCGETP, &sg) == 0) {
		sg.sg_flags = ECHO | CRMOD | XTABS | ANYP;
		ioctl(fd, TIOCSETP, &sg);
	}
#endif
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	for (fd = 3; fd < 32; fd++)
		close(fd);		/* /dev/sio, other masters */
	signal(SIGCHLD, SIG_DFL);
	if (strcmp(login_prog, "/usr/bin/login") == 0)
		execl(login_prog, "login", "-h", host, (char *)0);
	else
		execl(login_prog, login_prog, (char *)0);
	_exit(1);
}

static struct sess *
find(struct tcb *tp)
{
	int i;

	for (i = 0; i < NSESS; i++)
		if (sess[i].tp == tp)
			return &sess[i];
	return 0;
}

static void
send_opt(struct tcb *tp, int cmd, int opt)
{
	unsigned char b[3];

	b[0] = IAC;
	b[1] = cmd;
	b[2] = opt;
	tcp_write(tp, b, 3);
}

static void
telnet_open(struct tcb *tp)
{
	struct sess *s = find(0);
	char slave[32], host[16];
	unsigned char *a = tcp_peer(tp);
	int pid;

	if (!s || (s->mfd = pty_open(slave, sizeof(slave))) < 0) {
		static const char busy[] = "slipd: no free terminal\r\n";

		if (s)
			s->mfd = -1;
		tcp_write(tp, (const unsigned char *)busy, sizeof(busy) - 1);
		tcp_close(tp);
		return;
	}
	sprintf(host, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
	pid = fork();
	if (pid < 0) {
		close(s->mfd);
		s->mfd = -1;
		tcp_abort(tp);
		return;
	}
	if (pid == 0)
		run_login(slave, host);
	fcntl(s->mfd, F_SETFL, O_NONBLOCK);
	s->tp = tp;
	s->pid = pid;
	s->state = S_DATA;
	s->inlen = 0;
	if (verbose)
		fprintf(stderr, "telnet: %s on %s, pid %d\n", host, slave, pid);
	send_opt(tp, WILL, OPT_ECHO);
	send_opt(tp, WILL, OPT_SGA);
	send_opt(tp, DO, OPT_NAWS);
}

/* IAC SB ... IAC SE done: NAWS gives the window, 16-bit width, height */
static void
subneg(struct sess *s)
{
	struct winsize ws;

	if (s->sblen != 5 || s->sb[0] != OPT_NAWS)
		return;
	memset(&ws, 0, sizeof(ws));
	ws.ws_col = s->sb[1] << 8 | s->sb[2];
	ws.ws_row = s->sb[3] << 8 | s->sb[4];
	ioctl(s->mfd, TIOCSWINSZ, &ws);
}

/* the client asks for (DO/DONT) or offers (WILL/WONT) an option */
static void
option(struct sess *s, int cmd, int opt)
{
	if (opt == OPT_ECHO || opt == OPT_SGA) {
		if (cmd == DO || cmd == WILL)
			return;		/* agreed (ours), or harmless */
	}
	if (opt == OPT_NAWS && (cmd == WILL || cmd == WONT))
		return;			/* the answer to our DO */
	if (cmd == DO)
		send_opt(s->tp, WONT, opt);
	else if (cmd == WILL)
		send_opt(s->tp, DONT, opt);
}

/* client bytes: telnet commands out, data into s->in */
static int
telnet_input(struct tcb *tp, const unsigned char *b, int n)
{
	struct sess *s = find(tp);
	int i;

	if (!s)
		return n;
	for (i = 0; i < n && s->inlen < INBUF; i++) {
		int c = b[i];

		switch (s->state) {
		case S_CR:
			s->state = S_DATA;
			if (c == '\n' || c == 0)
				continue;	/* CR LF, CR NUL: just the CR */
			/* FALLTHROUGH */
		case S_DATA:
			if (c == IAC) {
				s->state = S_IAC;
				continue;
			}
			s->in[s->inlen++] = c;
			if (c == '\r')
				s->state = S_CR;
			break;
		case S_IAC:
			s->state = S_DATA;
			if (c == IAC)
				s->in[s->inlen++] = c;
			else if (c >= WILL && c <= DONT) {
				s->optcmd = c;
				s->state = S_OPT;
			} else if (c == SB) {
				s->sblen = 0;
				s->state = S_SB;
			}
			break;			/* NOP, AYT, ...: ignored */
		case S_OPT:
			option(s, s->optcmd, c);
			s->state = S_DATA;
			break;
		case S_SB:
			if (c == IAC)
				s->state = S_SBIAC;
			else if (s->sblen < (int)sizeof(s->sb))
				s->sb[s->sblen++] = c;
			break;
		case S_SBIAC:
			if (c == SE) {
				subneg(s);
				s->state = S_DATA;
			} else {		/* IAC IAC: a 255 in the data */
				if (c == IAC && s->sblen < (int)sizeof(s->sb))
					s->sb[s->sblen++] = c;
				s->state = S_SB;
			}
			break;
		}
	}
	return i;
}

static int
telnet_room(struct tcb *tp)
{
	struct sess *s = find(tp);

	/* no session (it ended): take and drop, never a zero window */
	return s ? INBUF - s->inlen : INBUF;
}

static void
end(struct sess *s)
{
	if (s->pid)
		kill(s->pid, SIGHUP);
	if (s->mfd >= 0)
		close(s->mfd);
	s->mfd = -1;
	s->tp = 0;
}

/* peer closed its side: hang up the terminal */
static void
telnet_eof(struct tcb *tp)
{
	struct sess *s = find(tp);

	if (s) {
		end(s);
		tcp_close(tp);
	}
}

static void
telnet_closed(struct tcb *tp)
{
	struct sess *s = find(tp);

	if (s)
		end(s);
}

/* reap logins that ended; their connections close once output is out */
static void
reap(void)
{
	int pid, i;

	if (!child_died)
		return;
	child_died = 0;
	while ((pid = wait3(0, WNOHANG, 0)) > 0)
		for (i = 0; i < NSESS; i++)
			if (sess[i].pid == pid)
				sess[i].pid = 0;
}

static void
telnet_poll(struct tcb *tp)
{
	struct sess *s = find(tp);
	unsigned char b[128], out[256];
	int n, k, i, o;

	reap();
	if (!s || s->mfd < 0)
		return;
	if (s->inlen > 0) {			/* client -> pty */
		k = write(s->mfd, s->in, s->inlen);
		if (k > 0) {
			memmove(s->in, s->in + k, s->inlen - k);
			s->inlen -= k;
		}
	}
	/* pty -> client: half the room, as each 0xFF doubles */
	n = tcp_room(tp) / 2;
	if (n > (int)sizeof(b))
		n = sizeof(b);
	if (n > 0 && (FD_ISSET(s->mfd, &svc_ready) || s->pid == 0)) {
		k = read(s->mfd, b, n);
		for (i = o = 0; i < k; i++) {
			if (b[i] == IAC)
				out[o++] = IAC;
			out[o++] = b[i];
		}
		if (o > 0)
			tcp_write(tp, out, o);
		else if (k == 0 && s->pid == 0)
			k = -1;			/* EOF after logout */
	} else
		k = 1;
	if (k < 0 && s->pid == 0) {		/* logged out, output drained */
		close(s->mfd);
		s->mfd = -1;
		s->tp = 0;
		tcp_close(tp);
	}
}

/* the pty masters to wake up for */
int
telnet_fds(fd_set *fs, int max)
{
	int i;

	for (i = 0; i < NSESS; i++)
		if (sess[i].tp && sess[i].mfd >= 0 && tcp_room(sess[i].tp) >= 2) {
			FD_SET(sess[i].mfd, fs);
			if (sess[i].mfd > max)
				max = sess[i].mfd;
		}
	return max;
}

static const struct service telnet_svc = {
	23, "telnet", telnet_open, telnet_input, telnet_room,
	telnet_eof, telnet_closed, telnet_poll
};

void
telnet_init(void)
{
	int i;

	for (i = 0; i < NSESS; i++)
		sess[i].mfd = -1;
	signal(SIGCHLD, onchld);
	tcp_listen(&telnet_svc);
}
