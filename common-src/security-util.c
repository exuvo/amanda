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
 * $Id: security-util.c,v 1.25 2006/07/22 12:04:47 martinea Exp $
 *
 * sec-security.c - security and transport over sec or a sec-like command.
 *
 * XXX still need to check for initial keyword on connect so we can skip
 * over shell garbage and other stuff that sec might want to spew out.
 */

#include "amanda.h"
#include "amutil.h"
#include "event.h"
#include "packet.h"
#include "shm-ring.h"
#include "security.h"
#include "security-util.h"
#include "stream.h"
#include "sockaddr-util.h"

/*
 * This is a queue of open connections
 */
GSList *connq = NULL;
static int newhandle = 1;
static event_id_t newevent = 1;

/*
 * Local functions
 */
static void sec_tcp_conn_put(struct tcp_conn *rc);
static void recvpkt_callback(void *, void *, ssize_t);
static void stream_read_callback(void *);
static void stream_read_sync_callback(void *);
static void stream_read_to_shm_ring_callback(void *);

static void sec_tcp_conn_read_cancel(struct tcp_conn *);
static void sec_tcp_conn_read_callback(void *);

static void tcpm_send_token_helper(struct tcp_conn *rc, int handle,
			           const void *buf, size_t len,
				   struct iovec **iov, int *nb_iov,
				   char **envbuf, ssize_t *encsize);
static void tcpm_send_token_callback(void *cookie);

/*
 * Authenticate a stream
 * Nothing needed for sec.  The connection is authenticated by secd
 * on startup.
 */
int
sec_stream_auth(
    void *	s)
{
    (void)s;	/* Quiet unused parameter warning */
    return (0);
}

/*
 * Returns the stream id for this stream.  This is just the local
 * port.
 */
int
sec_stream_id(
    void *	s)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    return (rs->handle);
}

/*
 * Setup to handle new incoming connections
 */
void
sec_accept(
    const security_driver_t *driver,
    char       *(*conf_fn)(char *, void *),
    int		in,
    int		out,
    void	(*fn)(security_handle_t *, pkt_t *),
    void       *datap)
{
    struct tcp_conn *rc;

    rc = sec_tcp_conn_get("localhost", "localhost",0); /* no hostname yet */
    rc->read = in;
    rc->write = out;
    rc->accept_fn = fn;
    rc->driver = driver;
    rc->conf_fn = conf_fn;
    rc->datap = datap;
    sec_tcp_conn_read(rc);
}

/*
 * frees a handle allocated by the above
 */
void
sec_close(
    void *	inst)
{
    struct sec_handle *rh = inst;

    assert(rh != NULL);

    auth_debug(1, _("sec: closing handle to %s\n"), rh->hostname);

    if (rh->rs != NULL) {
	/* This may be null if we get here on an error */
	stream_recvpkt_cancel(rh);
	security_stream_close(&rh->rs->secstr);
    }
    /* keep us from getting here again */
    rh->sech.driver = NULL;
    amfree(rh->dle_hostname);
    amfree(rh->hostname);
    amfree(rh);
}

/*
 * Called when a sec connection is finished connecting and is ready
 * to be authenticated.
 */
void
sec_connect_callback(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    g_mutex_lock(security_mutex);
    event_release(rh->rs->rc->ev_write);
    rh->rs->rc->ev_write = NULL;
    if (rh->ev_timeout) {
	event_release(rh->ev_timeout);
	rh->ev_timeout = NULL;
    }
    g_mutex_unlock(security_mutex);

    (*rh->fn.connect)(rh->arg, &rh->sech, S_OK);
}

/*
 * Called if a connection times out before completion.
 */
void
sec_connect_timeout(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    rh->rs->rc->reader_callbacks = g_slist_remove(rh->rs->rc->reader_callbacks,
						  &rh->rs->r_callback);
    event_release(rh->ev_timeout);
    rh->ev_timeout = NULL;

    (*rh->fn.connect)(rh->arg, &rh->sech, S_TIMEOUT);
}



/*
 * Transmit a packet.
 */
ssize_t
stream_sendpkt(
    void *	cookie,
    pkt_t *	pkt)
{
    char *buf;
    struct sec_handle *rh = cookie;
    size_t len;
    char *s;

    assert(rh != NULL);
    assert(pkt != NULL);

    auth_debug(1, _("sec: stream_sendpkt: enter\n"));

    if (rh->rc->prefix_packet)
	s = rh->rc->prefix_packet(rh, pkt);
    else
	s  = g_strdup("");
    len = strlen(pkt->body) + strlen(s) + 2;
    buf = g_malloc(len);
    buf[0] = (char)pkt->type;
    strncpy(&buf[1], s, len - 1);
    strncpy(&buf[1 + strlen(s)], pkt->body, (len - strlen(s) - 1));
    amfree(s);

    auth_debug(1,
     _("sec: stream_sendpkt: %s (%d) pkt_t (len %zu) contains:\n\n\"%s\"\n\n"),
      pkt_type2str(pkt->type), pkt->type, strlen(pkt->body), pkt->body);

    if (security_stream_write(&rh->rs->secstr, buf, len) < 0) {
	security_seterror(&rh->sech, "%s", security_stream_geterror(&rh->rs->secstr));
	amfree(buf);
	return (-1);
    }
    amfree(buf);
    return (0);
}

/*
 * Set up to receive a packet asyncronously, and call back when
 * it has been read.
 */
void
stream_recvpkt(
    void *	cookie,
    void	(*fn)(void *, pkt_t *, security_status_t),
    void *	arg,
    int		timeout)
{
    struct sec_handle *rh = cookie;

    assert(rh != NULL);

    auth_debug(1, _("sec: recvpkt registered for %s\n"), rh->hostname);

    /*
     * Reset any pending timeout on this handle
     */
    if (rh->ev_timeout != NULL)
	event_release(rh->ev_timeout);

    /*
     * Negative timeouts mean no timeout
     */
    if (timeout < 0) {
	rh->ev_timeout = NULL;
    } else {
	rh->ev_timeout = event_create((event_id_t)timeout, EV_TIME,
		stream_recvpkt_timeout, rh);
	event_activate(rh->ev_timeout);
    }
    rh->fn.recvpkt = fn;
    rh->arg = arg;
    security_stream_read(&rh->rs->secstr, recvpkt_callback, rh);
}

/*
 * This is called when a handle times out before receiving a packet.
 */
void
stream_recvpkt_timeout(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    assert(rh != NULL);

    auth_debug(1, _("sec: recvpkt timeout for %s\n"), rh->hostname);

    stream_recvpkt_cancel(rh);
    (*rh->fn.recvpkt)(rh->arg, NULL, S_TIMEOUT);
}

/*
 * Remove a async receive request from the queue
 */
void
stream_recvpkt_cancel(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    auth_debug(1, _("sec: cancelling recvpkt for %s\n"), rh->hostname);

    assert(rh != NULL);

    security_stream_read_cancel(&rh->rs->secstr);
    if (rh->ev_timeout != NULL) {
	event_release(rh->ev_timeout);
	rh->ev_timeout = NULL;
    }
}

/*
 * Write a chunk of data to a stream.  Blocks until completion.
 */
GMutex *stream_write_mutex = NULL;
int
tcpm_stream_write(
    void *	s,
    const void *buf,
    size_t	size)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);
    assert(rs->rc != NULL);

    if (!stream_write_mutex) {
	stream_write_mutex = g_mutex_new();
    }
    g_mutex_lock(stream_write_mutex);
    auth_debug(6, _("sec: stream_write: writing %zu bytes to %s:%d %d\n"),
		   size, rs->rc->hostname, rs->handle,
		   rs->rc->write);

    if (tcpm_send_token(rs->rc, rs->handle, &rs->rc->errmsg,
			     buf, size)) {
	security_stream_seterror(&rs->secstr, "%s", rs->rc->errmsg);
	g_mutex_unlock(stream_write_mutex);
	return (-1);
    }
    g_mutex_unlock(stream_write_mutex);
    return (0);
}

/*
 * Write a chunk of data to a stream.
 */
int
tcpm_stream_write_async(
    void *	s,
    void *	buf,
    size_t	size,
    void        (*fn)(void *, ssize_t, void *, ssize_t),
    void *      arg)
{
    struct sec_stream *rs = s;
    int stack_size;

    assert(rs != NULL);
    assert(rs->rc != NULL);

    auth_debug(6, _("sec: stream_write_async: writing %zu bytes to %s:%d %d\n"),
		   size, rs->rc->hostname, rs->handle,
		   rs->rc->write);

    stack_size = tcpm_send_token_async(rs, buf, size, fn, arg);

    return (stack_size);
}

/*
 * Submit a request to read some data.  Calls back with the given
 * function and arg when completed.
 */
void
tcpm_stream_read(
    void *	s,
    void	(*fn)(void *, void *, ssize_t),
    void *	arg)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    /*
     * Only one read request can be active per stream.
     */
    rs->r_callback.handle = rs->handle;
    rs->r_callback.s = rs;
    rs->r_callback.callback = stream_read_callback;
    if (!rs->ev_read_callback) {
	rs->ev_read_callback = TRUE;
	rs->rc->reader_callbacks = g_slist_prepend(rs->rc->reader_callbacks,
						   &rs->r_callback);
	sec_tcp_conn_read(rs->rc);
    }
    rs->fn = fn;
    rs->arg = arg;
}

/* buffer for tcpm_stream_read_sync function */
static ssize_t  sync_pktlen;
static void    *sync_pkt;

/*
 * Read a chunk of data from a stream.  Blocks until completion.
 */
static void
for_event_release(
    void *      cookie)
{
    struct sec_stream *rs = cookie;

   event_release(rs->ev_read_sync);
}

ssize_t
tcpm_stream_read_sync(
    void *	s,
    void **	buf)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    /*
     * Only one read request can be active per stream.
     */
    if (rs->ev_read_callback) {
	return -1;
    }
    sync_pktlen = 0;
    sync_pkt = NULL;
    if (rs->closed_by_network) {
	security_stream_seterror(&rs->secstr, "Failed to read from handle %d because server already closed it", rs->handle);
	return -1;
    }
    rs->r_callback.handle = rs->handle;
    rs->r_callback.s = rs;
    rs->r_callback.callback = stream_read_sync_callback;
    if (!rs->ev_read_callback) {
	rs->ev_read_callback = TRUE;
	rs->rc->reader_callbacks = g_slist_prepend(rs->rc->reader_callbacks,
						   &rs->r_callback);
	sec_tcp_conn_read(rs->rc);
    }

    g_mutex_lock(security_mutex);
    rs->event_id = newevent++;
    rs->ev_read_sync = event_create(rs->event_id, EV_WAIT, for_event_release, rs);
    event_activate(rs->ev_read_sync);
    g_mutex_unlock(security_mutex);
    event_wait(rs->ev_read_sync);
    rs->ev_read_sync = NULL;
    /* Can't use rs or rc, they can be freed */
    *buf = sync_pkt;
    return (sync_pktlen);
}

void
tcpm_stream_read_to_shm_ring(
    void *s,
    void (*fn)(void *, void *, ssize_t),
    shm_ring_t *shm_ring,
    void *arg)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    /*
     * Only one read request can be active per stream.
     */
    rs->r_callback.handle = rs->handle;
    rs->r_callback.s = rs;
    rs->r_callback.callback = stream_read_to_shm_ring_callback;
    if (!rs->ev_read_callback) {
	rs->ev_read_callback = TRUE;
	rs->rc->reader_callbacks = g_slist_prepend(rs->rc->reader_callbacks,
						   &rs->r_callback);
	sec_tcp_conn_read(rs->rc);

    }
    rs->fn = fn;
    rs->arg = arg;
    rs->shm_ring = shm_ring;
    rs->ring_init = FALSE;
}

/*
 * Cancel a previous stream read request.  It's ok if we didn't have a read
 * scheduled.
 */
void
tcpm_stream_read_cancel(
    void *	s)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    if (rs->ev_read_callback) {
	rs->rc->reader_callbacks = g_slist_remove(rs->rc->reader_callbacks,
						  &rs->r_callback);
	rs->ev_read_callback = FALSE;
	sec_tcp_conn_read_cancel(rs->rc);
    }
}

/*
 * Pause all read requests on the connection.
 */
void
tcpm_stream_pause(
    void *	s)
{
    struct sec_stream *rs = s;
    struct tcp_conn *rc = rs->rc;

    assert(rs != NULL);

    if (rc->ev_read_refcnt == 0) {
	return;
    }
    if (!rc->ev_read) {
	return;
    }
    event_release(rc->ev_read);
    rc->ev_read = NULL;
    rc->paused = TRUE;
}

/*
 * Resume all read requests on the connection.
 */
void
tcpm_stream_resume(
    void *	s)
{
    struct sec_stream *rs = s;
    struct tcp_conn *rc = rs->rc;

    assert(rs != NULL);
    if (!rc->paused) {
	return;
    }
    rc->paused = FALSE;

    if (rc->ev_read_refcnt == 0) {
	return;
    }
    if (rc->ev_read) {
	return;
    }
    rc->ev_read = event_create((event_id_t)rc->read, EV_READFD,
		sec_tcp_conn_read_callback, rc);
    event_activate(rc->ev_read);
}

/*
 * Transmits a chunk of data over a rsh_handle, adding
 * the necessary headers to allow the remote end to decode it.
 */
static void
tcpm_send_token_helper(
    struct tcp_conn *rc,
    int		     handle,
    const void      *buf,
    size_t	     len,
    struct iovec   **iov,
    int             *nb_iov,
    char           **encbuf,
    ssize_t         *encsize)
{
    guint32		*nethandle = g_malloc(sizeof(guint32));
    guint32		*netlength = g_malloc(sizeof(guint32));
    time_t		logtime;

    assert(sizeof(*netlength) == 4);

    logtime = time(NULL);
    if (logtime > rc->logstamp + 10) {
	g_debug("tcpm_send_token: data is still flowing");
        rc->logstamp = logtime;
    }

    auth_debug(6, "tcpm_send_token: write %zd bytes to handle %d\n",
	       len, handle);
    /*
     * Format is:
     *   32 bit length (network byte order)
     *   32 bit handle (network byte order)
     *   data
     */
    *netlength = htonl(len);
    (*iov)[0].iov_base = (void *)netlength;
    (*iov)[0].iov_len = sizeof(*netlength);

    *nethandle = htonl((guint32)handle);
    (*iov)[1].iov_base = (void *)nethandle;
    (*iov)[1].iov_len = sizeof(*nethandle);

    *encbuf = (char *)buf;
    *encsize = len;

    if(len == 0) {
	(*iov)[2].iov_base = NULL;
	(*iov)[2].iov_len = 0;
	*nb_iov = 2;
    }
    else {
	if (rc->driver->data_encrypt == NULL) {
	    (*iov)[2].iov_base = (void *)buf;
	    (*iov)[2].iov_len = len;
	} else {
	    /* (the extra (void *) cast is to quiet type-punning warnings) */
	    rc->driver->data_encrypt(rc, (void *)buf, len, (void **)(void *)encbuf, encsize);
	    (*iov)[2].iov_base = (void *)*encbuf;
	    (*iov)[2].iov_len = *encsize;
	    *netlength = htonl(*encsize);
	}
        *nb_iov = 3;
    }

    if (debug_auth >= 3) {
	crc_t crc;
	crc32_init(&crc);
	crc32_add((uint8_t *)buf, len, &crc);
	g_debug("packet send CRC: %d %08x:%llu", handle, crc32_finish(&crc), (long long)crc.size);
    }
}

ssize_t
tcpm_send_token(
    struct tcp_conn *rc,
    int		handle,
    char **	errmsg,
    const void *buf,
    size_t	len)
{
    struct iovec  iov[3];
    struct iovec  iov_copy[3];
    struct iovec  *iovx = iov;
    int           nb_iov = 3;
    char         *encbuf;
    ssize_t       encsize;
    int           rval;
    int           save_errno;

    tcpm_send_token_helper(rc, handle, buf, len, &iovx, &nb_iov, &encbuf, &encsize);
    /* copy iov because data_write modify it */
    memcpy(iov_copy, iov, 3*sizeof(struct iovec));
    rval = rc->driver->data_write(rc, iov_copy, nb_iov);
    save_errno = errno;
    g_free(iov[0].iov_base);
    g_free(iov[1].iov_base);
    if (len != 0 && rc->driver->data_encrypt != NULL && buf != encbuf) {
	amfree(encbuf);
    }

    if (rval < 0) {
	if (errmsg) {
            g_free(*errmsg);
            *errmsg = g_strdup_printf(_("write error to: %s"),
                                      strerror(save_errno));
        }
        return (-1);
    }
    return (0);
}

ssize_t
tcpm_send_token_async(
    struct sec_stream *rs,
    void *	buf,
    size_t	len,
    void        (*fn)(void *, ssize_t, void *, ssize_t),
    void *      arg)
{
    struct iovec  iov[3];
    struct iovec  *iovx = iov;
    int           nb_iov = 3;
    char         *encbuf;
    ssize_t       encsize;
    async_write_data *awd;

    int	handle = rs->handle;

    tcpm_send_token_helper(rs->rc, handle, buf, len, &iovx, &nb_iov, &encbuf, &encsize);

    awd = g_new0(struct async_write_data, 1);
    memcpy(awd->iov, iov, 3*sizeof(struct iovec));
    awd->nb_iov = nb_iov;
    memcpy(awd->copy_iov, iov, 3*sizeof(struct iovec));
    awd->copy_nb_iov = nb_iov;
    awd->buf = encbuf;
    awd->written = 0;
    awd->fn = fn;
    awd->arg = arg;
    if (encbuf != buf)
	amfree(buf);
    rs->rc->async_write_data_list = g_list_append(rs->rc->async_write_data_list, awd);
    rs->rc->async_write_data_size += 8 + len;

    if(!rs->rc->ev_write) {
	rs->rc->ev_write = event_create(
			(event_id_t)(rs->rc->write),
			EV_WRITEFD, tcpm_send_token_callback, rs);
	event_activate(rs->rc->ev_write);
    }
    return (rs->rc->async_write_data_size);
}

static void
tcpm_send_token_callback(
    void *      cookie)
{
    struct sec_stream *rs = cookie;
    gboolean done = FALSE;
    async_write_data *awd = NULL;

    if (rs->rc->async_write_data_list) {
	int rval;
	int save_errno;
	awd = (async_write_data *)rs->rc->async_write_data_list->data;
	rval = rs->rc->driver->data_write_non_blocking(rs->rc, awd->copy_iov, awd->copy_nb_iov);
	save_errno = errno;
	if (rval < 0) {
	    security_stream_seterror(&rs->secstr, "write error to: %s", strerror(save_errno));
	    if (awd->fn) {
		(*awd->fn)(awd->arg, rs->rc->async_write_data_size, NULL, -1);
	    }
            return;
	}

	awd->written += rval;
	rs->rc->async_write_data_size -= rval;
	/* if completely written */
	if (awd->copy_iov[0].iov_len == 0 &&
	    awd->copy_iov[1].iov_len == 0 &&
	    (awd->nb_iov < 2 || awd->copy_iov[2].iov_len == 0)) {
	    if (awd->fn) {
		(*awd->fn)(awd->arg, rs->rc->async_write_data_size, awd->buf, awd->written);
	    }
	    g_free(awd->iov[0].iov_base);
	    g_free(awd->iov[1].iov_base);
	    rs->rc->async_write_data_list = g_list_remove(rs->rc->async_write_data_list,
						      awd);
	    done = TRUE;
	}
    }

    /* unschedule us */
    if (!rs->rc->async_write_data_list) {
	event_release(rs->rc->ev_write);
	rs->rc->ev_write = NULL;
    }

    if (done && !awd->buf) { /* closing */

	if (rs->handle < 10000 || rs->closed_by_network == 1) {
	    security_stream_read_cancel(&rs->secstr);
	    rs->closed_by_network = 1;
	    sec_tcp_conn_put(rs->rc);
	}
	rs->closed_by_me = 1;
	if (rs->closed_by_network) {
	    amfree(((security_stream_t *)rs)->error);
	}

    }
    if (done) {
	g_free(awd);
    }
    return;
}

/*
 *  return -2 for incomplete packet
 *  return -1 on error
 *  return  0 on EOF:   *handle = H_EOF  && *size = 0    if socket closed
 *  return  0 on EOF:   *handle = handle && *size = 0    if stream closed
 *  return size     :   *handle = handle && *size = size for data read
 */

ssize_t
tcpm_recv_token(
    struct tcp_conn    *rc,
    int *	handle,
    char **	errmsg,
    char **	buf,
    ssize_t *	size)
{
    ssize_t            rval;
    struct sec_stream *rs = NULL;
    GSList            *reader_callbacks;

    assert(sizeof(rc->netint) == 8);
    if (rc->size_header_read < (ssize_t)sizeof(rc->netint)) {
	rval = rc->driver->data_read(rc,
				((char *)&rc->netint) + rc->size_header_read,
				sizeof(rc->netint) - rc->size_header_read, 0);
	if (rval == -1) {
	    if (errmsg) {
		g_free(*errmsg);
		*errmsg = g_strdup_printf(_("Xrecv error: %s"),
                                          strerror(errno));
            }
	    auth_debug(1, _("tcpm_recv_token: A return(-1)\n"));
	    return(-1);
	} else if (rval == 0) {
	    *size = 0;
	    *handle = H_EOF;
	    g_free(*errmsg);
	    *errmsg = g_strdup("SOCKET_EOF");
	    auth_debug(1, "tcpm_recv_token: A return(0)\n");
	    return(0);
	} else if (rval < (ssize_t)sizeof(rc->netint) - rc->size_header_read) {
	    rc->size_header_read += rval;
	    return(-2);
	}
	rc->size_header_read += rval;
        amfree(rc->buffer);
	*size = (ssize_t)ntohl(rc->netint[0]);
	*handle = (int)ntohl(rc->netint[1]);
        rc->buffer = NULL;
	rc->size_buffer_read = 0;

	/* amanda protocol packet can be above NETWORK_BLOCK_BYTES */
	if (*size > 128*NETWORK_BLOCK_BYTES || *size < 0) {
	    if (isprint((int)(*size        ) & 0xFF) &&
		isprint((int)(*size   >> 8 ) & 0xFF) &&
		isprint((int)(*size   >> 16) & 0xFF) &&
		isprint((int)(*size   >> 24) & 0xFF) &&
		isprint((*handle      ) & 0xFF) &&
		isprint((*handle >> 8 ) & 0xFF) &&
		isprint((*handle >> 16) & 0xFF) &&
		isprint((*handle >> 24) & 0xFF)) {
		char s[201];
		char *s1;
		int i;
		s[0] = ((int)(*size)  >> 24) & 0xFF;
		s[1] = ((int)(*size)  >> 16) & 0xFF;
		s[2] = ((int)(*size)  >>  8) & 0xFF;
		s[3] = ((int)(*size)       ) & 0xFF;
		s[4] = (*handle >> 24) & 0xFF;
		s[5] = (*handle >> 16) & 0xFF;
		s[6] = (*handle >> 8 ) & 0xFF;
		s[7] = (*handle      ) & 0xFF;
		i = 8; s[i] = ' ';
		while(i<100 && isprint((int)s[i]) && s[i] != '\n') {
		    rval = rc->driver->data_read(rc, &s[i], 1, 0);
		    switch (rval) {
		    case -1: s[i] = '\0'; break;
		    case  0: s[i] = '\0'; break;
		    default:
			 dbprintf(_("read: %c\n"), s[i]); i++; s[i]=' ';
			 break;
		    }
		}
		s[i] = '\0';
		s1 = quote_string(s);
		g_free(*errmsg);
		*errmsg = g_strdup_printf(_("tcpm_recv_token: invalid size: %s"),
                                          s1);
		dbprintf(_("tcpm_recv_token: invalid size %s\n"), s1);
		amfree(s1);
	    } else {
		g_free(*errmsg);
		*errmsg = g_strdup("tcpm_recv_token: invalid size");
		dbprintf("tcpm_recv_token: invalid size %zd\n", *size);
	    }
	    *size = -1;
	    return -1;
	}

	if (*size == 0) {
	    auth_debug(1, "tcpm_recv_token: read EOF from %d\n", *handle);
	    g_free(*errmsg);
	    *errmsg = g_strdup("EOF");
	    rc->size_header_read = 0;
	    rs = NULL;
	    for (reader_callbacks = rc->reader_callbacks; reader_callbacks != NULL;
		 reader_callbacks = reader_callbacks->next) {
		reader_callback *r_callback = (reader_callback *)reader_callbacks->data;
		if (r_callback->s->handle == rc->handle) {
		    rs = r_callback->s;
		    break;
		}
	    }
	    if (rs && rs->shm_ring) {
		rs->shm_ring->mc->eof_flag = TRUE;
		sem_post(rs->shm_ring->sem_read);
		sem_post(rs->shm_ring->sem_read);
	    }
	    return 0;
	}
    }

    *size = (ssize_t)ntohl(rc->netint[0]);
    *handle = (int)ntohl(rc->netint[1]);

    if (!rs) {
	for (reader_callbacks = rc->reader_callbacks; reader_callbacks != NULL;
	     reader_callbacks = reader_callbacks->next) {
	    reader_callback *r_callback = (reader_callback *)reader_callbacks->data;
	    if (r_callback->s->handle == rc->handle) {
		rs = r_callback->s;
		break;
	    }
	}
    }
    if (!rs || !rs->shm_ring) {
	if (!rc->buffer)
	    rc->buffer = g_malloc((size_t)*size);
	rval = rc->driver->data_read(rc, rc->buffer + rc->size_buffer_read,
				     (size_t)*size - rc->size_buffer_read, 0);
    } else {
	size_t to_read;
	uint64_t write_offset;
	uint64_t readx;
	uint64_t written;
	uint64_t shm_ring_size;
	void *decbuf;
	ssize_t decsize;
	char *buf = NULL;
	size_t size_read = 0;

	if (!rs->ring_init) {
	    shm_ring_producer_set_size(rs->shm_ring, NETWORK_BLOCK_BYTES*8, NETWORK_BLOCK_BYTES);
	    rs->ring_init = TRUE;
	}
	to_read = (size_t)*size - rc->size_buffer_read;
	write_offset = rs->shm_ring->mc->write_offset;
	written = rs->shm_ring->mc->written;
	shm_ring_size = rs->shm_ring->mc->ring_size;

	if (rc->driver->data_decrypt) {

	    // read to a buffer
	    if (!rc->buffer)
		rc->buffer = g_malloc((size_t)*size);
	    rval = rc->driver->data_read(rc, rc->buffer + rc->size_buffer_read,
				(size_t)*size - rc->size_buffer_read, 0);
	    if (rval < 0) {
		g_free(*errmsg);
		*errmsg = g_strdup_printf("recv error: data_read failed");
		auth_debug(1, _(" recv error: D return(-1)\n"));
		return -1;
	    }
	    if (rval < (ssize_t)*size - rc->size_buffer_read) {
		rc->size_buffer_read += rval;
		return (-2);
	    }

	    // decrypt to another buffer
	    buf = rc->buffer;
	    rc->buffer = NULL;
	    rc->driver->data_decrypt(rc, buf, *size, &decbuf, &decsize);
	    if (buf != (char *)decbuf) {
		amfree(buf);
		buf = (char *)decbuf;
	    }
	    *size = decsize;
	    to_read = *size;
	} else {
	    to_read = *size - rc->size_buffer_read;
	}

	rval = 1;
	while (to_read > 0 && rval > 0) {
	    size_t avail;
	    size_t usable;

	    /* wait for enough space in the ring to read the packet */
	    while (!rs->shm_ring->mc->cancelled) {
	        readx = rs->shm_ring->mc->readx;
		avail = shm_ring_size - (written - readx);
	        if (avail >= to_read)
		    break;
	        if (avail >= shm_ring_size/4)
		    break;
	        if (shm_ring_sem_wait(rs->shm_ring, rs->shm_ring->sem_write) != 0) {
		    g_free(*errmsg);
		    *errmsg = g_strdup_printf("recv error: sem_wait(sem_write) failed");
		    return -1;
		}
	    }
	    if (rs->shm_ring->mc->cancelled) {
		g_free(*errmsg);
		*errmsg = g_strdup_printf("recv error: shm_ring is cancelled");
		auth_debug(1, _("tcpm_recv_token: D return(-1)\n"));
		return -1;
	    }
	    usable = to_read;
	    if (usable > avail) {
		usable = avail;
	    }

	    if (rc->driver->data_decrypt) {
		// copy from the decrypted buffer
		if (write_offset + usable <= shm_ring_size) {
		    memcpy(rs->shm_ring->data + write_offset , buf, usable);
		} else {
		    memcpy(rs->shm_ring->data + write_offset, buf, shm_ring_size - write_offset);
		    memcpy(rs->shm_ring->data, buf + shm_ring_size - write_offset, usable -(shm_ring_size - write_offset));
		}
		rval = usable;
	    } else {
		if (usable > shm_ring_size - write_offset)
		    usable = shm_ring_size - write_offset;
		rval = rc->driver->data_read(rc, rs->shm_ring->data + write_offset,
					     usable, 0);
	    }
	    if (rval > 0) {
		size_read += rval;
		if (rs->shm_ring->mc->written == 0 && rs->shm_ring->mc->need_sem_ready) {
		    sem_post(rs->shm_ring->sem_ready);
		    if (shm_ring_sem_wait(rs->shm_ring, rs->shm_ring->sem_start) != 0) {
			g_free(*errmsg);
			*errmsg = g_strdup_printf("recv error: sem_wait(sem_start) failed");
			amfree(buf);
			return -1;
		    }
		}
		write_offset += rval;
		if (write_offset >= shm_ring_size) {
		    write_offset -= shm_ring_size;
		}
		rs->shm_ring->mc->write_offset = write_offset;
		rs->shm_ring->mc->written += rval;
		rs->shm_ring->data_avail += rval;
		if (rs->shm_ring->data_avail >= rs->shm_ring->mc->consumer_block_size) {
		    sem_post(rs->shm_ring->sem_read);
		    rs->shm_ring->data_avail -= rs->shm_ring->mc->consumer_block_size;
		}
		rc->size_buffer_read += rval;
		to_read -= rval;
		if (rc->size_buffer_read < (ssize_t)*size && to_read == 0) {
		    amfree(buf);
		    return -2;
		}
	    }
	}


	if (rval < 0) {
	    g_free(*errmsg);
	    *errmsg = g_strdup_printf(_("Yrecv error: %s"), strerror(errno));
	    g_debug("tcpm_recv_token: cancelling shm-ring because rval < 0");
	    rs->shm_ring->mc->cancelled = TRUE;
	    rs->shm_ring->mc->eof_flag = TRUE;
	    sem_post(rs->shm_ring->sem_read);
	    sem_post(rs->shm_ring->sem_read);
	    auth_debug(1, _("tcpm_recv_token: C return(-1)\n"));
	    amfree(buf);
	    return (-1);
	} else if (rval == 0) {
	    g_free(*errmsg);
	    *errmsg = g_strdup("SOCKET_EOF");
	    *size = 0;
	    *handle = H_EOF;
	    rs->shm_ring->mc->eof_flag = TRUE;
	    sem_post(rs->shm_ring->sem_read);
	    sem_post(rs->shm_ring->sem_read);
	    auth_debug(1, "tcpm_recv_token: C return(0)\n");
	    amfree(buf);
	    return (0);
	} else {
	    *size = size_read;
	    rc->size_header_read = 0;
	    rc->size_buffer_read = 0;
	    amfree(buf);
	    return (*size);
	}
    }

    if (rval == -1) {
	g_free(*errmsg);
	*errmsg = g_strdup_printf(_("Zrecv error: %s"), strerror(errno));
	auth_debug(1, _("tcpm_recv_token: B return(-1)\n"));
	return (-1);
    } else if (rval == 0) {
	*size = 0;
	*handle = H_EOF;
	g_free(*errmsg);
	*errmsg = g_strdup("SOCKET_EOF");
	auth_debug(1, "tcpm_recv_token: B return(0)\n");
	return (0);
    } else if (rval < (ssize_t)*size - rc->size_buffer_read) {
	rc->size_buffer_read += rval;
	return (-2);
    }
    rc->size_buffer_read += rval;
    amfree(*buf);
    *buf = rc->buffer;
    rc->size_header_read = 0;
    rc->size_buffer_read = 0;
    rc->buffer = NULL;

    auth_debug(6, _("tcpm_recv_token: read %zd bytes from %d\n"), *size, *handle);

    if (*size > 0 && rc->driver->data_decrypt != NULL) {
	void *decbuf;
	ssize_t decsize;
	rc->driver->data_decrypt(rc, *buf, *size, &decbuf, &decsize);
	if (*buf != (char *)decbuf) {
	    amfree(*buf);
	    *buf = (char *)decbuf;
	}
	*size = decsize;
    }

    if (debug_auth >= 3) {
	crc_t crc;
	crc32_init(&crc);
	crc32_add((uint8_t *)*buf, *size, &crc);
	g_debug("packet receive CRC: %d %08x:%llu", *handle, crc32_finish(&crc), (long long)crc.size);
    }
    return((*size));
}

void
tcpm_close_connection(
    void *h,
    char *hostname)
{
    struct sec_handle *rh = h;

    auth_debug(1, _("tcpm_close_connection: closing connection to %s : %s\n"), rh->hostname, hostname);

    if (rh && rh->rc && rh->rc->read >= 0) {
	rh->rc->toclose = 1;
	sec_tcp_conn_put(rh->rc);
    }
}



/*
 * Accept an incoming connection on a stream_server socket
 * Nothing needed for tcpma.
 */
int
tcpma_stream_accept(
    void *	s)
{
    (void)s;	/* Quiet unused parameter warning */

    return (0);
}

/*
 * Return a connected stream.  For sec, this means setup a stream
 * with the supplied handle.
 */
void *
tcpma_stream_client(
    void *	h,
    int		id)
{
    struct sec_handle *rh = h;
    struct sec_stream *rs;

    assert(rh != NULL);

    if (id <= 0) {
	security_seterror(&rh->sech,
	    _("%d: invalid security stream id"), id);
	return (NULL);
    }

    rs = g_new0(struct sec_stream, 1);
    security_streaminit(&rs->secstr, rh->sech.driver);
    rs->handle = id;
    rs->ev_read_callback = FALSE;
    rs->closed_by_me = 0;
    rs->closed_by_network = 0;
    if (rh->rc) {
	rs->rc = rh->rc;
	rh->rc->refcnt++;
    }
    else {
	rs->rc = sec_tcp_conn_get(rh->dle_hostname, rh->hostname, 0);
	rs->rc->driver = rh->sech.driver;
	rh->rc = rs->rc;
    }

    auth_debug(1, _("sec: stream_client: connected to stream %d\n"), id);

    return (rs);
}

/*
 * Create the server end of a stream.  For sec, this means setup a stream
 * object and allocate a new handle for it.
 */
void *
tcpma_stream_server(
    void *	h)
{
    struct sec_handle *rh = h;
    struct sec_stream *rs;

    assert(rh != NULL);

    rs = g_new0(struct sec_stream, 1);
    security_streaminit(&rs->secstr, rh->sech.driver);
    rs->closed_by_me = 0;
    rs->closed_by_network = 0;
    if (rh->rc) {
	rs->rc = rh->rc;
	rs->rc->refcnt++;
    }
    else {
	rs->rc = sec_tcp_conn_get(rh->dle_hostname, rh->hostname, 0);
	rs->rc->driver = rh->sech.driver;
	rh->rc = rs->rc;
    }
    /*
     * Stream should already be setup!
     */
    if (rs->rc->read < 0) {
	sec_tcp_conn_put(rs->rc);
	amfree(rs->secstr.error);
	security_seterror(&rh->sech, _("lost connection to %s"), rh->hostname);
	return (NULL);
    }
    assert(g_str_equal(rh->hostname, rs->rc->hostname));
    /*
     * so as not to conflict with the amanda server's handle numbers,
     * we start at 500000 and work down
     */
    g_mutex_lock(security_mutex);
    rs->handle = 500000 - newhandle++;
    g_mutex_unlock(security_mutex);
    rs->ev_read_callback = FALSE;
    auth_debug(1, _("sec: stream_server: created stream %d\n"), rs->handle);
    return (rs);
}

/*
 * Close and unallocate resources for a stream.
 */
void
tcpma_stream_close(
    void *	s)
{
    struct sec_stream *rs = s;
    char buf = 0;

    assert(rs != NULL);

    auth_debug(1, _("sec: tcpma_stream_close: closing stream %d\n"), rs->handle);

    if (rs->rc->write != -1)
	tcpm_stream_write(rs, &buf, 0);
    if (rs->handle < 10000 || rs->closed_by_network == 1) {
	security_stream_read_cancel(&rs->secstr);
	rs->closed_by_network = 1;
	sec_tcp_conn_put(rs->rc);
    }
    rs->closed_by_me = 1;
    if (rs->closed_by_network) {
	amfree(((security_stream_t *)rs)->error);
    }
}

/*
 * Close and unallocate resources for a stream.
 */
void
tcpma_stream_close_async(
    void *	s,
    void        (*fn)(void *, ssize_t, void *, ssize_t),
    void *      arg)
{
    struct sec_stream *rs = s;
    //char buf = 0;

    assert(rs != NULL);

    auth_debug(1, _("sec: tcpma_stream_close_async: closing stream %d\n"), rs->handle);

    if (rs->rc->write != -1)
	tcpm_stream_write_async(rs, NULL, 0, fn, arg);
}



/*
 * Locate an existing connection to the given host, or create a new,
 * unconnected entry if none exists.  The caller is expected to check
 * for the lack of a connection (rc->read == -1) and set one up.
 */
struct tcp_conn *
sec_tcp_conn_get(
    const char *dle_hostname,
    const char *hostname,
    int		want_new)
{
    GSList *iter;
    struct tcp_conn *rc = NULL;

    auth_debug(1, _("sec_tcp_conn_get: %s %s\n"), dle_hostname, hostname);

    g_mutex_lock(security_mutex);
    if (want_new == 0) {
	for (iter = connq; iter != NULL; iter = iter->next) {
	    rc = (struct tcp_conn *)iter->data;
	    if (!rc->errmsg &&
		strcasecmp(hostname, rc->hostname) == 0 &&
		((!dle_hostname && !rc->dle_hostname) ||
		 (dle_hostname && rc->dle_hostname && strcasecmp(dle_hostname, rc->dle_hostname) == 0))) {
		break;
	    }
	}

	if (iter != NULL) {
	    rc->refcnt++;
	    auth_debug(1,
		      _("sec_tcp_conn_get: exists, refcnt to %s is now %d\n"),
		       rc->hostname, rc->refcnt);
	    g_mutex_unlock(security_mutex);
	    return (rc);
	}
    }
    g_mutex_unlock(security_mutex);

    auth_debug(1, _("sec_tcp_conn_get: creating new handle\n"));
    /*
     * We can't be creating a new handle if we are the client
     */
    rc = g_new0(struct tcp_conn, 1);
    rc->read = rc->write = -1;
    rc->driver = NULL;
    rc->pid = -1;
    rc->ev_read = NULL;
    rc->toclose = 0;
    rc->donotclose = 0;
    strncpy(rc->hostname, hostname, sizeof(rc->hostname) - 1);
    rc->hostname[sizeof(rc->hostname) - 1] = '\0';
    if (dle_hostname) {
	rc->dle_hostname = g_strdup(dle_hostname);
    }
    rc->errmsg = NULL;
    rc->refcnt = 1;
    rc->handle = -1;
    rc->pkt = NULL;
    rc->accept_fn = NULL;
    rc->recv_security_ok = NULL;
    rc->prefix_packet = NULL;
    rc->auth = 0;
    rc->conf_fn = NULL;
    rc->datap = NULL;
    g_mutex_lock(security_mutex);
    rc->event_id = newevent++;
    connq = g_slist_append(connq, rc);
    g_mutex_unlock(security_mutex);
    return (rc);
}

/*
 * Delete a reference to a connection, and close it if it is the last
 * reference.
 */
static void
sec_tcp_conn_put(
    struct tcp_conn *rc)
{
    amwait_t status;

    assert(rc->refcnt > 0);
    --rc->refcnt;
    auth_debug(1, _("sec_tcp_conn_put: decrementing refcnt for %s to %d\n"),
		   rc->hostname, rc->refcnt);
    if (rc->refcnt > 0) {
	return;
    }
    auth_debug(1, _("sec_tcp_conn_put: closing connection to %s\n"), rc->hostname);
    if (rc->read != -1)
	aclose(rc->read);
    if (rc->write != -1)
	aclose(rc->write);
    if (rc->pid != -1 && !rc->child_watch) {
	int pid;
	int count = 50;

	pid = waitpid(rc->pid, &status, WNOHANG);
	while (pid == 0 && count > 0) {
	    struct timespec tdelay;
	    tdelay.tv_sec = 0;
	    tdelay.tv_nsec = 50000000;
	    nanosleep(&tdelay, NULL);
	    pid = waitpid(rc->pid, &status, WNOHANG);
	    count--;
	}
	if (pid == 0) {
	    g_debug("sending SIGTERM to pid: %ld", (long)rc->pid);
	    if (kill(rc->pid, SIGTERM) == -1 && errno != ESRCH) {
		g_debug("Can't kill pid %ld: %s", (long)rc->pid, strerror(errno));
	    }
	    pid = waitpid(rc->pid, &status, WNOHANG);
	    count = 50;
	    while (pid == 0 && count > 0) {
		struct timespec tdelay;
		tdelay.tv_sec = 0;
		tdelay.tv_nsec = 50000000;
		nanosleep(&tdelay, NULL);
		pid = waitpid(rc->pid, &status, WNOHANG);
		count--;
	    }
	}
	rc->pid = -1;
    }
    if (rc->ev_read != NULL)
	event_release(rc->ev_read);
    if (rc->errmsg != NULL)
	amfree(rc->errmsg);
    g_mutex_lock(security_mutex);
    connq = g_slist_remove(connq, rc);
    g_mutex_unlock(security_mutex);
    amfree(rc->pkt);
    if(!rc->donotclose) {
	/* amfree(rc) */
	/* a memory leak occurs, but freeing it lead to memory
	 * corruption because it can still be used.
	 * We need to find a good place to free 'rc'.
	 */
    }
}

/*
 * Turn on read events for a conn.  Or, increase a ev_read_refcnt if we are
 * already receiving read events.
 */
void
sec_tcp_conn_read(
    struct tcp_conn *	rc)
{
    assert (rc != NULL);

    if (rc->ev_read != NULL) {
	rc->ev_read_refcnt++;
	auth_debug(1,
	      _("sec: conn_read: incremented ev_read_refcnt to %d for %s\n"),
	       rc->ev_read_refcnt, rc->hostname);
	return;
    }
    auth_debug(1, _("sec: conn_read registering event handler for %s\n"),
		   rc->hostname);
    rc->ev_read = event_create((event_id_t)rc->read, EV_READFD,
		sec_tcp_conn_read_callback, rc);
    event_activate(rc->ev_read);
    rc->ev_read_refcnt = 1;
}

static void
sec_tcp_conn_read_cancel(
    struct tcp_conn *	rc)
{

    --rc->ev_read_refcnt;
    auth_debug(1,
       _("sec: conn_read_cancel: decremented ev_read_refcnt to %d for %s\n"),
	rc->ev_read_refcnt, rc->hostname);
    if (rc->ev_read_refcnt > 0) {
	return;
    }
    auth_debug(1,
                _("sec: conn_read_cancel: releasing event handler for %s\n"),
	         rc->hostname);
    event_release(rc->ev_read);
    rc->ev_read = NULL;
}

/*
 * This is called when a handle is woken up because data read off of the
 * net is for it.
 */
static void
recvpkt_callback(
    void *	cookie,
    void *	buf,
    ssize_t	bufsize)
{
    pkt_t pkt;
    struct sec_handle *rh = cookie;

    assert(rh != NULL);

    auth_debug(1, _("sec: recvpkt_callback: %zd\n"), bufsize);
    /*
     * We need to cancel the recvpkt request before calling
     * the callback because the callback may reschedule us.
     */
    stream_recvpkt_cancel(rh);

    switch (bufsize) {
    case 0:
	security_seterror(&rh->sech,
	    _("EOF on read from %s"), rh->hostname);
	(*rh->fn.recvpkt)(rh->arg, NULL, S_ERROR);
	return;
    case -1:
	security_seterror(&rh->sech, "%s", security_stream_geterror(&rh->rs->secstr));
	(*rh->fn.recvpkt)(rh->arg, NULL, S_ERROR);
	return;
    default:
	break;
    }

    parse_pkt(&pkt, buf, (size_t)bufsize);
    auth_debug(1,
	  _("sec: received %s packet (%d) from %s, contains:\n\n\"%s\"\n\n"),
	   pkt_type2str(pkt.type), pkt.type,
	   rh->hostname, pkt.body);
    if (rh->rc->recv_security_ok && (rh->rc->recv_security_ok)(rh, &pkt, rh->rc->need_priv_port) < 0)
	(*rh->fn.recvpkt)(rh->arg, NULL, S_ERROR);
    else
	(*rh->fn.recvpkt)(rh->arg, &pkt, S_OK);
    amfree(pkt.body);
}

/*
 * Callback for tcpm_stream_read_sync
 */
static void
stream_read_sync_callback(
    void *	s)
{
    struct sec_stream *rs = s;
    assert(rs != NULL);

    auth_debug(6, _("sec: stream_read_sync_callback: handle %d\n"), rs->handle);

    /*
     * Make sure this was for us.  If it was, then blow away the handle
     * so it doesn't get claimed twice.  Otherwise, leave it alone.
     *
     * If the handle is EOF, pass that up to our callback.
     */
    if (rs->rc->handle == rs->handle) {
        auth_debug(6, _("stream_read_sync_callback: stream_read_sync_callback: it was for us\n"));
        rs->rc->handle = H_TAKEN;
    } else if (rs->rc->handle != H_EOF) {
        auth_debug(6, _("stream_read_sync_callback: stream_read_sync_callback: not for us\n"));
        return;
    }

    /*
     * Remove the event first, and then call the callback.
     * We remove it first because we don't want to get in their
     * way if they reschedule it.
     */
    tcpm_stream_read_cancel(rs);

    sync_pktlen = rs->rc->pktlen;
    sync_pkt = malloc(sync_pktlen);
    memcpy(sync_pkt, rs->rc->pkt, sync_pktlen);

    if (rs->rc->pktlen <= 0) {
	auth_debug(6, _("sec: stream_read_sync_callback: %s\n"), rs->rc->errmsg);
	security_stream_seterror(&rs->secstr, "%s", rs->rc->errmsg);
	if(rs->closed_by_me == 1 && rs->closed_by_network == 0) {
	    sec_tcp_conn_put(rs->rc);
	}
	rs->closed_by_network = 1;
	return;
    }
    auth_debug(6,
	    _("sec: stream_read_sync_callback: read %zd bytes from %s:%d\n"),
	    rs->rc->pktlen, rs->rc->hostname, rs->handle);
    event_wakeup(rs->event_id);
}

/*
 * Callback for tcpm_stream_read
 */
static void
stream_read_callback(
    void *	arg)
{
    struct sec_stream *rs = arg;
    time_t             logtime;

    assert(rs != NULL);
    assert(rs->rc != NULL);

    logtime = time(NULL);
    if (logtime > rs->rc->logstamp + 10) {
	g_debug("stream_read_callback: data is still flowing");
	rs->rc->logstamp = logtime;
    }
    auth_debug(6, _("sec: stream_read_callback: handle %d\n"), rs->handle);

    /*
     * Make sure this was for us.  If it was, then blow away the handle
     * so it doesn't get claimed twice.  Otherwise, leave it alone.
     *
     * If the handle is EOF, pass that up to our callback.
     */
    if (rs->rc->handle == rs->handle) {
	auth_debug(6, _("sec: stream_read_callback: it was for us\n"));
	rs->rc->handle = H_TAKEN;
    } else if (rs->rc->handle != H_EOF) {
	auth_debug(6, _("sec: stream_read_callback: not for us\n"));
	return;
    }

    /*
     * Remove the event first, and then call the callback.
     * We remove it first because we don't want to get in their
     * way if they reschedule it.
     */

    if (rs->rc->pktlen <= 0) {
	auth_debug(5, _("Xsec: stream_read_callback: %s\n"), rs->rc->errmsg);
	tcpm_stream_read_cancel(rs);
	security_stream_seterror(&rs->secstr, "%s", rs->rc->errmsg);
	if(rs->closed_by_me == 1 && rs->closed_by_network == 0) {
	    sec_tcp_conn_put(rs->rc);
	}
	rs->closed_by_network = 1;
	(*rs->fn)(rs->arg, NULL, rs->rc->pktlen);
	return;
    }
    auth_debug(6, _("sec: stream_read_callback: read %zd bytes from %s:%d\n"),
		   rs->rc->pktlen, rs->rc->hostname, rs->handle);
    (*rs->fn)(rs->arg, rs->rc->pkt, rs->rc->pktlen);
    auth_debug(6, _("sec: after callback stream_read_callback\n"));
}

/*
 * Callback for tcpm_stream_read_to_shm_ring
 */
static void
stream_read_to_shm_ring_callback(
    void *	arg)
{
    struct sec_stream *rs = arg;
    time_t             logtime;

    assert(rs != NULL);
    assert(rs->rc != NULL);

    logtime = time(NULL);
    if (logtime > rs->rc->logstamp + 10) {
	g_debug("stream_read_to_shm_ring_callback: data is still flowing");
	rs->rc->logstamp = logtime;
    }
    auth_debug(6, _("sec: stream_read_to_shm_ring_callback: handle %d\n"), rs->handle);

    /*
     * Make sure this was for us.  If it was, then blow away the handle
     * so it doesn't get claimed twice.  Otherwise, leave it alone.
     *
     * If the handle is EOF, pass that up to our callback.
     */
    if (rs->rc->handle == rs->handle) {
	auth_debug(6, _("sec: stream_read_to_shm_ring_callback: it was for us\n"));
	rs->rc->handle = H_TAKEN;
    } else if (rs->rc->handle != H_EOF) {
	auth_debug(6, _("sec: stream_read_to_shm_ring_callback: not for us\n"));
	return;
    }

    /*
     * Remove the event first, and then call the callback.
     * We remove it first because we don't want to get in their
     * way if they reschedule it.
     */

    if (rs->rc->pktlen <= 0) {
	auth_debug(5, _("sec: stream_read_to_shm_ring_callback: %s\n"), rs->rc->errmsg);
	tcpm_stream_read_cancel(rs);
	security_stream_seterror(&rs->secstr, "%s", rs->rc->errmsg);
	if(rs->closed_by_me == 1 && rs->closed_by_network == 0) {
	    sec_tcp_conn_put(rs->rc);
	}
	rs->closed_by_network = 1;
	(*rs->fn)(rs->arg, NULL, rs->rc->pktlen);
	return;
    }
    auth_debug(6, _("sec: stream_read_to_shm_ring_callback: read %zd bytes from %s:%d\n"),
		   rs->rc->pktlen, rs->rc->hostname, rs->handle);
    (*rs->fn)(rs->arg, rs->rc->pkt, rs->rc->pktlen);
    auth_debug(6, _("sec: after callback stream_read_to_shm_ring_callback\n"));
}

/*
 * The callback for the netfd for the event handler
 * Determines if this packet is for this security handle,
 * and does the real callback if so.
 */
static void
sec_tcp_conn_read_callback(
    void *	cookie)
{
    struct tcp_conn *	rc = cookie;
    struct sec_handle *	rh;
    pkt_t		pkt;
    ssize_t		rval;
    GSList		*reader_callbacks;

    assert(cookie != NULL);

    auth_debug(6, _("sec: conn_read_callback %d %d\n"), (int)rc->event_id, rc->read);

    /* Read the data off the wire.  If we get errors, shut down. */
    rval = tcpm_recv_token(rc, &rc->handle, &rc->errmsg, &rc->pkt,
				&rc->pktlen);
    auth_debug(6, _("sec: conn_read_callback: tcpm_recv_token returned %zd\n"),
		   rval);

    if (rval == -2) {
	return;
    }

    if (rval < 0 || rc->handle == H_EOF) {
	rc->pktlen = rval;
	rc->handle = H_EOF;
	for (reader_callbacks = rc->reader_callbacks; reader_callbacks != NULL;
	     reader_callbacks = reader_callbacks->next) {
	    reader_callback *r_callback = (reader_callback *)reader_callbacks->data;
	    r_callback->callback(r_callback->s);
	}
	/* Must close all events */
	/* delete our 'accept' reference */
	if (rc->accept_fn != NULL) {
	    (*rc->accept_fn)(NULL, NULL);
	    if(rc->refcnt != 1) {
		dbprintf(_("STRANGE, rc->refcnt should be 1, it is %d\n"),
			  rc->refcnt);
	    }
	    rc->accept_fn = NULL;
	    while (rc->refcnt > 0) {
		sec_tcp_conn_put(rc);
	    }
	}
	return;
    }

    if(rval == 0) {
	rc->pktlen = 0;
	for (reader_callbacks = rc->reader_callbacks; reader_callbacks != NULL;
	     reader_callbacks = reader_callbacks->next) {
	    reader_callback *r_callback = (reader_callback *)reader_callbacks->data;
	    if (r_callback->s->handle == rc->handle) {
		r_callback->callback(r_callback->s);
		break;
	    }
	}
	if (rc->handle != H_TAKEN) {
	    g_debug("ignoring close stream %d", rc->handle);
	}
	return;
    }

    /* If there are events waiting on this handle, we're done */
    rc->donotclose = 1;
    for (reader_callbacks = rc->reader_callbacks; reader_callbacks != NULL;
	 reader_callbacks = reader_callbacks->next) {
	reader_callback *r_callback = (reader_callback *)reader_callbacks->data;
	if (r_callback->s->handle == rc->handle) {
	    r_callback->callback(r_callback->s);
	    break;
	}
    }
    rc->donotclose = 0;
    if (rc->handle == H_TAKEN) {
	if(rc->refcnt == 0) {
	    // do not free, it stillcan be used
	    //amfree(rc);
	}
	return;
    }
    if (rc->pktlen == 0) {
	if(rc->refcnt == 0) {
	    // do not free, it stillcan be used
	    //amfree(rc);
	}
	return;
    }

    assert(rc->refcnt > 0);

    /* If there is no accept fn registered, then drop the packet */
    if (rc->accept_fn == NULL) {
	g_warning(
	  _("sec: conn_read_callback: %zd bytes for handle %d went unclaimed!"),
	  rc->pktlen, rc->handle);
	if (rc->pktlen<100) {
	    char *badbuf = g_malloc(rc->pktlen+1);
	    memcpy(badbuf, rc->pkt, rc->pktlen);
	    badbuf[rc->pktlen] = '\0';
	    g_debug("packet: %s", badbuf);
	    g_free(badbuf);
	}
	return;
    }

    rh = g_new0(struct sec_handle, 1);
    security_handleinit(&rh->sech, rc->driver);
    rh->hostname = g_strdup(rc->hostname);
    rh->ev_timeout = NULL;
    rh->rc = rc;
    rh->peer = rc->peer;
    rh->rs = tcpma_stream_client(rh, rc->handle);

    auth_debug(1, _("sec: new connection %d\n"),rc->handle);
    pkt.body = NULL;
    parse_pkt(&pkt, rc->pkt, (size_t)rc->pktlen);
    auth_debug(1, _("sec: calling accept_fn\n"));
    if (rh->rc->recv_security_ok && (rh->rc->recv_security_ok)(rh, &pkt, rh->rc->need_priv_port) < 0)
	(*rc->accept_fn)(&rh->sech, NULL);
    else
	(*rc->accept_fn)(&rh->sech, &pkt);
    amfree(pkt.body);
}



ssize_t
generic_data_write(
    void         *cookie,
    struct iovec *iov,
    int           iovcnt)
{
    struct tcp_conn *rc = cookie;
    return full_writev(rc->write, iov, iovcnt);
}

ssize_t
generic_data_write_non_blocking(
    void         *cookie,
    struct iovec *iov,
    int           iovcnt)
{
    struct tcp_conn *rc = cookie;
    ssize_t r;
    ssize_t n;
    ssize_t delta;

    int flags = fcntl(rc->write, F_GETFL, 0);
    flags = fcntl(rc->write, F_SETFL, flags|O_NONBLOCK);

    while(iovcnt>0 && iov->iov_len == 0) {
	iov++;
	iovcnt--;
    }

    r = writev(rc->write, iov, iovcnt);
    n = r;

    for (; n > 0; iovcnt--, iov++) {
	delta = ((size_t)n < (size_t)iov->iov_len) ? n : (ssize_t)iov->iov_len;
	n -= delta;
	iov->iov_len -= delta;
	iov->iov_base = (char *)iov->iov_base + delta;
	if (iov->iov_len > 0)
	    break;
    }
    return r;
}

ssize_t
generic_data_read(
    void   *cookie,
    void   *vbuf,
    size_t  sizebuf,
    int     timeout G_GNUC_UNUSED)
{
    struct tcp_conn *rc = cookie;
    return read(rc->read, (char *)vbuf, sizebuf);
}


void
parse_pkt(
    pkt_t *	pkt,
    const void *buf,
    size_t	bufsize)
{
    const unsigned char *bufp = buf;

    auth_debug(1, _("sec: parse_pkt: parsing buffer of %zu bytes\n"), bufsize);

    pkt->type = (pktype_t)*bufp++;
    bufsize--;

    pkt->packet_size = bufsize+1;
    pkt->body = g_malloc(pkt->packet_size);
    if (bufsize == 0) {
	pkt->body[0] = '\0';
    } else {
	memcpy(pkt->body, bufp, bufsize);
	pkt->body[pkt->packet_size - 1] = '\0';
    }
    pkt->size = strlen(pkt->body);

    auth_debug(1, _("sec: parse_pkt: %s (%d): \"%s\"\n"), pkt_type2str(pkt->type),
		   pkt->type, pkt->body);
}
