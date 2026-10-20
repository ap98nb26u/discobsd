/*
 * slipd - shared definitions for the SLIP/IP/TCP host (slipd.c, tcp.c,
 * svc.c).
 */
#include <time.h>

#define MTU		1500

extern int verbose;
extern unsigned char myaddr[4];

/* slipd.c */
unsigned	get16(const unsigned char *p);
void		put16(unsigned char *p, unsigned v);
unsigned long	get32(const unsigned char *p);
void		put32(unsigned char *p, unsigned long v);
unsigned long	sum16(const unsigned char *p, int n, unsigned long s);
unsigned	fold(unsigned long s);
void		ip_send(unsigned char *p, int n);
int		ip_room(void);
time_t		now(void);

/* tcp.c */
struct tcb;

/*
 * A TCP service: the callbacks a listening port's connections get.  Any
 * may be 0.  input() returns how many of the bytes it took (the rest is
 * not acknowledged, so the peer sends it again); room() gives how much
 * more it can take now (the advertised window).
 */
struct service {
	unsigned	port;
	const char	*name;
	void		(*open)(struct tcb *);
	int		(*input)(struct tcb *, const unsigned char *, int);
	int		(*room)(struct tcb *);
	void		(*eof)(struct tcb *);		/* peer sent FIN */
	void		(*closed)(struct tcb *);	/* connection gone */
	void		(*poll)(struct tcb *);		/* each main-loop pass */
};

void	tcp_listen(const struct service *svc);
void	tcp_input(unsigned char *p, int hlen, int n);
void	tcp_timer(void);
void	tcp_poll(void);
int	tcp_write(struct tcb *tp, const unsigned char *b, int n);
int	tcp_room(struct tcb *tp);
void	tcp_close(struct tcb *tp);
void	tcp_abort(struct tcb *tp);
void	*tcp_appdata(struct tcb *tp);
void	tcp_setappdata(struct tcb *tp, void *d);
int	tcp_unsent(struct tcb *tp);

unsigned char *tcp_peer(struct tcb *tp);

/* svc.c */
void	svc_init(void);

/* telnet.c */
#include <sys/types.h>
extern char	*login_prog;
extern fd_set	svc_ready;		/* slipd.c: what the last select() found */
void	telnet_init(void);
int	telnet_fds(fd_set *fs, int max);
