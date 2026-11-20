#ifndef _SECURITY_UTIL_H
#define _SECURITY_UTIL_H

/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */

/*
 * $Id: security-util.h,v 1.5 2006/07/01 00:10:38 paddy_s Exp $
 *
 */

#include "stream.h"
#include "conffile.h"
#include "shm-ring.h"
#include "security.h"
#include "event.h"

#define auth_debug(i, ...) do {		\
	if ((i) <= debug_auth) {	\
	    dbprintf(__VA_ARGS__);	\
	}				\
} while (0)


/*
 * Magic values for sec_conn->handle
 */
#define H_TAKEN -1		/* sec_conn->tok was already read */
#define H_EOF   -2		/* this connection has been shut down */

typedef struct async_write_data {
    struct iovec  iov[3];
    int           nb_iov;
    struct iovec  copy_iov[3];
    int           copy_nb_iov;
    void	 *buf;
    ssize_t	  written;
    void          (*fn)(void *, ssize_t, void *, ssize_t);
    void	 *arg;
} async_write_data;

struct sec_handle;
struct sec_stream;

typedef struct reader_callback {
    int          handle;
    struct sec_stream  *s;
    void       (*callback)(void *);
} reader_callback;

/*
 * This is a sec connection to a host.  We should only have
 * one connection per host.
 */
struct tcp_conn {
    const struct security_driver *driver;	/* MUST be first */
    int			read, write;		/* pipes to sec */
    pid_t		pid;			/* pid of sec process */
    char *		pkt;			/* last pkt read */
    ssize_t		pktlen;			/* len of above */
    event_handle_t *	ev_read;		/* read (EV_READFD) handle */
    event_handle_t *	ev_write;		/* write (EV_WRITEFD) handle */
    int			ev_read_refcnt;		/* number of readers */
    GList	       *async_write_data_list;  /* list of block to write */
    ssize_t		async_write_data_size;
    char		hostname[MAX_HOSTNAME_LENGTH+1];
						/* host we're talking to */
    char               *dle_hostname;
    char *		errmsg;			/* error passed up */
    int			refcnt;			/* number of handles using */
    int			handle;			/* last proto handle read */
    int			event_id;		/* event ID fired when token read */
    void		(*accept_fn)(security_handle_t *, pkt_t *);
    sockaddr_union	peer;
    GSList             *reader_callbacks;
    int			(*recv_security_ok)(struct sec_handle *, pkt_t *, int);
    int			need_priv_port;
    char *		(*prefix_packet)(void *, pkt_t *);
    int			toclose;
    int			donotclose;
    int			auth;
    char *              (*conf_fn)(char *, void *);
    void *              datap;
    time_t              logstamp;
    unsigned int	netint[2];
    char *              buffer;
    ssize_t             size_header_read;
    ssize_t             size_buffer_read;
    GSource            *child_watch;
    gboolean            paused;
};


struct sec_stream;

/*
 * This is the private handle data.
 */
struct sec_handle {
    security_handle_t	sech;		/* MUST be first */
    char *		hostname;	/* ptr to rc->hostname */
    char *		dle_hostname;	/* hostname from the dle */
    struct sec_stream *	rs;		/* virtual stream we xmit over */
    struct tcp_conn *	rc;		/* */
    union {
	void (*recvpkt)(void *, pkt_t *, security_status_t);
					/* func to call when packet recvd */
	void (*connect)(void *, security_handle_t *, security_status_t);
					/* func to call when connected */
    } fn;
    void *		arg;		/* argument to pass function */
    shm_ring_t *	shm_ring;
    event_handle_t *	ev_timeout;	/* timeout handle for recv */
    sockaddr_union	peer;
    int			sequence;
    event_id_t		event_id;
    char *		proto_handle;
    event_handle_t *	ev_read;
    struct sec_handle *	prev;
    struct sec_handle *	next;
    void		(*accept_fn)(security_handle_t *, pkt_t *);
    struct addrinfo    *res;
    struct addrinfo    *next_res;
    void                (*connect_callback)(void *, security_handle_t *, security_status_t);
    void               *connect_arg;
    char               *src_ip;
    int                 port;
};

/*
 * This is the internal security_stream data for sec.
 */
struct sec_stream {
    security_stream_t	secstr;		/* MUST be first */
    struct tcp_conn *	rc;		/* physical connection */
    int			handle;		/* protocol handle */
    gboolean		ev_read_callback;	/* read */
    event_handle_t *	ev_read;	/* read */
    event_handle_t *	ev_read_sync;	/* read */
    void		(*fn)(void *, void *, ssize_t);	/* read event fn */
    void *		arg;		/* arg for previous */
    int			fd;
    char		databuf[NETWORK_BLOCK_BYTES];
    ssize_t		len;
    int			socket;
    in_port_t		port;
    int			closed_by_me;
    int			closed_by_network;
    reader_callback     r_callback;
    shm_ring_t        *shm_ring;
    gboolean            ring_init;
    event_id_t          event_id;
    gboolean            paused;
};


int	sec_stream_auth(void *);
int	sec_stream_id(void *);
void	sec_accept(const security_driver_t *,
		   char *(*)(char *, void *),
		   int, int,
		   void (*)(security_handle_t *, pkt_t *),
		   void *);
void	sec_close(void *);
void	sec_connect_callback(void *);
void	sec_connect_timeout(void *);

ssize_t	stream_sendpkt(void *, pkt_t *);
void	stream_recvpkt(void *,
		        void (*)(void *, pkt_t *, security_status_t),
		        void *, int);
void	stream_recvpkt_timeout(void *);
void	stream_recvpkt_cancel(void *);

int	tcpm_stream_write(void *, const void *, size_t);
int	tcpm_stream_write_async(void *, void *, size_t, void (*)(void *, ssize_t, void *, ssize_t), void *);
void	tcpm_stream_read(void *, void (*)(void *, void *, ssize_t), void *);
ssize_t	tcpm_stream_read_sync(void *, void **);
void	tcpm_stream_read_to_shm_ring(void *, void (*)(void *, void *, ssize_t), struct shm_ring_t *, void *);
void	tcpm_stream_read_cancel(void *);
void	tcpm_stream_pause(void *);
void	tcpm_stream_resume(void *);
ssize_t	tcpm_send_token(struct tcp_conn *, int, char **, const void *, size_t);
ssize_t	tcpm_send_token_async(struct sec_stream *, void *, size_t, void (*)(void *, ssize_t, void *, ssize_t), void *);
ssize_t	tcpm_recv_token(struct tcp_conn *, int *, char **, char **, ssize_t *);
void	tcpm_close_connection(void *, char *);

int	tcpma_stream_accept(void *);
void *	tcpma_stream_client(void *, int);
void *	tcpma_stream_server(void *);
void	tcpma_stream_close(void *);
void    tcpma_stream_close_async(void *s, void (*fn)(void *, ssize_t, void *, ssize_t), void *arg);


struct tcp_conn *sec_tcp_conn_get(const char *, const char *, int);
void	sec_tcp_conn_read(struct tcp_conn *);
void	parse_pkt(pkt_t *, const void *, size_t);
char	*sec_get_authenticated_peer_name_gethostname(security_handle_t *);
ssize_t generic_data_write(void *, struct iovec *iov, int iovcnt);
ssize_t generic_data_write_non_blocking(void *, struct iovec *iov, int iovcnt);
ssize_t generic_data_read(void *, void *vbuf, size_t sizebuf, int timeout);

#endif /* _SECURITY_INFO_H */
