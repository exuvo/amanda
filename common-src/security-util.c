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

void
sec_close_connection_none(
    void *h G_GNUC_UNUSED,
    char *hostname G_GNUC_UNUSED)
{
    return;
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
 * Create the server end of a stream.  For bsdudp, this means setup a tcp
 * socket for receiving a connection.
 */
void *
tcp1_stream_server(
    void *	h)
{
    struct sec_stream *rs = NULL;
    struct sec_handle *rh = h;

    assert(rh != NULL);

    rs = g_new0(struct sec_stream, 1);
    security_streaminit(&rs->secstr, rh->sech.driver);
    rs->closed_by_me = 0;
    rs->closed_by_network = 0;
    if (rh->rc) {
	rs->rc = rh->rc;
	g_mutex_lock(security_mutex);
	rs->handle = 500000 - newhandle++;
	g_mutex_unlock(security_mutex);
	rs->rc->refcnt++;
	rs->socket = 0;		/* the socket is already opened */
    }
    else {
	rh->rc = sec_tcp_conn_get(rh->dle_hostname, rh->hostname, 1);
	rh->rc->driver = rh->sech.driver;
	rs->rc = rh->rc;
	rs->socket = stream_server(SU_GET_FAMILY(&rh->udp->peer), &rs->port,
				   STREAM_BUFSIZE, STREAM_BUFSIZE, 0);
	if (rs->socket < 0) {
	    security_seterror(&rh->sech,
			    _("can't create server stream: %s"), strerror(errno));
	    amfree(rs->secstr.error);
	    return (NULL);
	}
	rh->rc->read = rs->socket;
	rh->rc->write = rs->socket;
	rs->handle = (int)rs->port;
    }
    rs->fd = -1;
    rs->ev_read_callback = FALSE;
    return (rs);
}

/*
 * Accepts a new connection on unconnected streams.  Assumes it is ok to
 * block on accept()
 */
int
tcp1_stream_accept(
    void *	s)
{
    struct sec_stream *bs = s;

    assert(bs != NULL);
    assert(bs->socket != -1);
    assert(bs->fd < 0);

    if (bs->socket > 0) {
	bs->fd = stream_accept(bs->socket, 30, STREAM_BUFSIZE, STREAM_BUFSIZE);
	if (bs->fd < 0) {
	    security_stream_seterror(&bs->secstr,
				     _("can't accept new stream connection: %s"),
				     strerror(errno));
	    return (-1);
	}
	bs->rc->read = bs->fd;
	bs->rc->write = bs->fd;
    }
    return (0);
}

/*
 * Return a connected stream
 */
void *
tcp1_stream_client(
    void *	h,
    int		id)
{
    struct sec_stream *rs = NULL;
    struct sec_handle *rh = h;
    char *msg = NULL;

    assert(rh != NULL);

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
	rh->rc = sec_tcp_conn_get(rh->dle_hostname, rh->hostname, 1);
	rh->rc->driver = rh->sech.driver;
	rs->rc = rh->rc;
	rh->rc->read = stream_client(NULL, rh->hostname, (in_port_t)id,
			STREAM_BUFSIZE, STREAM_BUFSIZE, &rs->port, 0, &msg);
	if (msg) {
	    security_seterror(&rh->sech,
			      "can't connect stream to %s port %d: %s",
			      rh->hostname, id, msg);
	    g_free(msg);
	    return NULL;
	}
	if (rh->rc->read < 0) {
	    security_seterror(&rh->sech,
			      _("can't connect stream to %s port %d: %s"),
			       rh->hostname, id, strerror(errno));
	    amfree(rs->secstr.error);
	    return (NULL);
        }
	rh->rc->write = rh->rc->read;
    }
    rs->socket = -1;	/* we're a client */
    rh->rs = rs;
    return (rs);
}

int
tcp_stream_write(
    void *	s,
    const void *buf,
    size_t	size)
{
    struct sec_stream *rs = s;
    time_t             logtime;

    assert(rs != NULL);

    logtime = time(NULL);
    if (rs->rc && logtime > rs->rc->logstamp + 10) {
	g_debug("tcp_stream_write: data is still flowing");
	rs->rc->logstamp = logtime;
    }

    if (full_write(rs->fd, buf, size) < size) {
        security_stream_seterror(&rs->secstr,
            _("write error on stream %d: %s"), rs->port, strerror(errno));
        return (-1);
    }
    return (0);
}

/*
 * Write a chunk of data to a stream.  Blocks until completion.
 */
int
tcp_stream_write_async(
    void *	s,
    void *	buf,
    size_t	size,
    void        (*fn)(void *, ssize_t, void *, ssize_t),
    void *      arg)
{
    int r = tcp_stream_write(s, buf, size);
    (*fn)(arg, 0, buf, r);
    return 0;
}

char *
bsd_prefix_packet(
    void *	h,
    pkt_t *	pkt)
{
    struct sec_handle *rh = h;
    struct passwd *pwd;
    char *buf;

    if (pkt->type != P_REQ)
	return g_strdup("");

    if ((pwd = getpwuid(geteuid())) == NULL) {
	security_seterror(&rh->sech,
			  _("can't get login name for my uid %ld"),
			  (long)geteuid());
	return g_strdup("");
    }
    buf = g_malloc(16+strlen(pwd->pw_name));
    strncpy(buf, "SECURITY USER ", (16 + strlen(pwd->pw_name)));
    strncpy(&buf[14], pwd->pw_name, (16 + strlen(pwd->pw_name) - 14));
    buf[14 + strlen(pwd->pw_name)] = '\n';
    buf[15 + strlen(pwd->pw_name)] = '\0';

    return (buf);
}


/*
 * Check the security of a received packet.  Returns negative on security
 * violation, or returns 0 if ok.  Removes the security info from the pkt_t.
 */
int
bsd_recv_security_ok(
    struct sec_handle *	rh,
    pkt_t *		pkt,
    int                 need_priv_port)
{
    char *tok, *security, *body, *result;
    char *service = NULL, *serviceX, *serviceY;
    char *security_line;
    char *s;
    size_t len;
    in_port_t port;

    /*
     * Now, find the SECURITY line in the body, and parse it out
     * into an argv.
     */
    if (strncmp_const(pkt->body, "SECURITY ") == 0) {
	security = pkt->body;
	len = 0;
	while(*security != '\n' && len < pkt->size) {
	    security++;
	    len++;
	}
	if(*security == '\n') {
	    body = security+1;
	    *security = '\0';
	    security_line = g_strdup(pkt->body);
	    security = pkt->body + strlen("SECURITY ");
	} else {
	    body = pkt->body;
	    security_line = NULL;
	    security = NULL;
	}
    } else {
	body = pkt->body;
	security_line = NULL;
	security = NULL;
    }

    /*
     * Now, find the SERVICE line in the body, and parse it out
     * into an argv.
     */
    s = body;
    if (strncmp_const_skip_no_var(s, "SERVICE ", s) == 0) {
	serviceX = g_strdup(s);
	serviceY = strtok(serviceX, "\n");
	if (serviceY)
	    service  = g_strdup(serviceY);
	amfree(serviceX);
    }

    /*
     * We need to do different things depending on which type of packet
     * this is.
     */
    switch (pkt->type) {
    case P_REQ:
	/*
	 * Request packets must come from a reserved port
	 */
	port = SU_GET_PORT(&rh->peer);
	if (need_priv_port && port >= IPPORT_RESERVED) {
	    security_seterror(&rh->sech,
		_("host %s: port %u not secure"), rh->hostname,
		(unsigned int)port);
	    amfree(service);
	    amfree(security_line);
	    return (-1);
	}

	if (!service) {
	    security_seterror(&rh->sech,
			      _("packet as no SERVICE line"));
	    amfree(security_line);
	    return (-1);
	}

	/*
	 * Request packets contain a remote username.  We need to check
	 * that we allow it in.
	 *
	 * They will look like:
	 *	SECURITY USER [username]
	 */

	/* there must be some security info */
	if (security == NULL) {
	    security_seterror(&rh->sech,
		_("no bsd SECURITY for P_REQ"));
	    amfree(service);
	    amfree(security_line);
	    return (-1);
	}

	/* second word must be USER */
	if ((tok = strtok(security, " ")) == NULL) {
	    security_seterror(&rh->sech,
		_("SECURITY line: %s"), security_line);
	    amfree(service);
	    amfree(security_line);
	    return (-1);	/* default errmsg */
	}
	if (!g_str_equal(tok, "USER")) {
	    security_seterror(&rh->sech,
		_("REQ SECURITY line parse error, expecting USER, got %s"), tok);
	    amfree(service);
	    amfree(security_line);
	    return (-1);
	}

	/* the third word is the username */
	if ((tok = strtok(NULL, "")) == NULL) {
	    security_seterror(&rh->sech,
		_("SECURITY line: %s"), security_line);
	    amfree(service);
	    amfree(security_line);
	    return (-1);	/* default errmsg */
	}
	if ((result = check_user(rh, tok, service)) != NULL) {
	    security_seterror(&rh->sech, "%s", result);
	    amfree(service);
	    amfree(result);
	    amfree(security_line);
	    return (-1);
	}

	/* we're good to go */
	break;
    default:
	break;
    }
    amfree(service);
    amfree(security_line);

    /*
     * If there is security info at the front of the packet, we need to
     * shift the rest of the data up and nuke it.
     */
    if (body != pkt->body)
	memmove(pkt->body, body, strlen(body) + 1);
    return (0);
}

/*
 * Transmit a packet.  Add security information first.
 */
ssize_t
udpbsd_sendpkt(
    void *	cookie,
    pkt_t *	pkt)
{
    struct sec_handle *rh = cookie;
    struct passwd *pwd;

    assert(rh != NULL);
    assert(pkt != NULL);

    auth_debug(1, _("udpbsd_sendpkt: enter\n"));
    /*
     * Initialize this datagram, and add the header
     */
    dgram_zero(&rh->udp->dgram);
    dgram_cat(&rh->udp->dgram, "%s", pkthdr2str(rh, pkt));

    /*
     * Add the security info.  This depends on which kind of packet we're
     * sending.
     */
    switch (pkt->type) {
    case P_REQ:
	/*
	 * Requests get sent with our username in the body
	 */
	if ((pwd = getpwuid(geteuid())) == NULL) {
	    security_seterror(&rh->sech,
		_("can't get login name for my uid %ld"), (long)getuid());
	    return (-1);
	}
	dgram_cat(&rh->udp->dgram, _("SECURITY USER %s\n"), pwd->pw_name);
	break;

    default:
	break;
    }

    /*
     * Add the body, and send it
     */
    dgram_cat(&rh->udp->dgram, "%s", pkt->body);

    auth_debug(1,
     _("sec: udpbsd_sendpkt: %s (%d) pkt_t (len %zu) contains:\n\n\"%s\"\n\n"),
      pkt_type2str(pkt->type), pkt->type, strlen(pkt->body), pkt->body);

    if (dgram_send_addr(&rh->peer, &rh->udp->dgram) != 0) {
	security_seterror(&rh->sech,
	    _("send %s to %s failed: %s"), pkt_type2str(pkt->type),
	    rh->hostname, strerror(errno));
	return (-1);
    }
    return (0);
}

void
udp_close(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    if (rh->proto_handle == NULL) {
	return;
    }

    auth_debug(1, _("udp: close handle '%s'\n"), rh->proto_handle);

    udp_recvpkt_cancel(rh);
    if (rh->next) {
	rh->next->prev = rh->prev;
    }
    else {
	rh->udp->bh_last = rh->prev;
    }
    if (rh->prev) {
	rh->prev->next = rh->next;
    }
    else {
	rh->udp->bh_first = rh->next;
    }

    amfree(rh->proto_handle);
    amfree(rh->hostname);
    amfree(rh);
}

/*
 * Set up to receive a packet asynchronously, and call back when it has
 * been read.
 */
void
udp_recvpkt(
    void *	cookie,
    void	(*fn)(void *, pkt_t *, security_status_t),
    void *	arg,
    int		timeout)
{
    struct sec_handle *rh = cookie;

    auth_debug(1, _("udp_recvpkt(cookie=%p, fn=%p, arg=%p, timeout=%u)\n"),
		   cookie, fn, arg, timeout);
    assert(rh != NULL);
    assert(fn != NULL);


    /*
     * Subsequent recvpkt calls override previous ones
     */
    if (rh->ev_read == NULL) {
	udp_addref(rh->udp, &udp_netfd_read_callback);
	rh->ev_read = event_create(rh->event_id, EV_WAIT,
	    udp_recvpkt_callback, rh);
	event_activate(rh->ev_read);
    }
    if (rh->ev_timeout != NULL)
	event_release(rh->ev_timeout);
    if (timeout < 0) {
	rh->ev_timeout = NULL;
    } else {
	rh->ev_timeout = event_create((event_id_t)timeout, EV_TIME,
					udp_recvpkt_timeout, rh);
	event_activate(rh->ev_timeout);
    }
    rh->fn.recvpkt = fn;
    rh->arg = arg;
}

/*
 * Remove a async receive request on this handle from the queue.
 * If it is the last one to be removed, then remove the event
 * handler for our network fd
 */
void
udp_recvpkt_cancel(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    assert(rh != NULL);

    if (rh->ev_read != NULL) {
	udp_delref(rh->udp);
	event_release(rh->ev_read);
	rh->ev_read = NULL;
    }

    if (rh->ev_timeout != NULL) {
	event_release(rh->ev_timeout);
	rh->ev_timeout = NULL;
    }
}

/*
 * This is called when a handle is woken up because data read off of the
 * net is for it.
 */
void
udp_recvpkt_callback(
    void *	cookie)
{
    struct sec_handle *rh = cookie;
    void (*fn)(void *, pkt_t *, security_status_t);
    void *arg;

    auth_debug(1, _("udp: receive handle '%s' netfd '%s'\n"),
		   rh->proto_handle, rh->udp->handle);
    assert(rh != NULL);

    /* if it doesn't correspond to this handle, something is wrong */
    assert(g_str_equal(rh->proto_handle, rh->udp->handle));

    /* if it didn't come from the same host/port, forget it */
    if (cmp_sockaddr(&rh->peer, &rh->udp->peer, 0) != 0) {
	amfree(rh->udp->handle);
	dbprintf(_("not from same host\n"));
	dump_sockaddr(&rh->peer);
	dump_sockaddr(&rh->udp->peer);
	return;
    }

    /*
     * We need to cancel the recvpkt request before calling the callback
     * because the callback may reschedule us.
     */
    fn = rh->fn.recvpkt;
    arg = rh->arg;
    udp_recvpkt_cancel(rh);

    /*
     * Check the security of the packet.  If it is bad, then pass NULL
     * to the packet handling function instead of a packet.
     */
    if (rh->udp->recv_security_ok &&
	rh->udp->recv_security_ok(rh, &rh->udp->pkt, rh->udp->need_priv_port) < 0) {
	(*fn)(arg, NULL, S_ERROR);
    } else {
	(*fn)(arg, &rh->udp->pkt, S_OK);
    }
}

/*
 * This is called when a handle times out before receiving a packet.
 */
void
udp_recvpkt_timeout(
    void *	cookie)
{
    struct sec_handle *rh = cookie;
    void (*fn)(void *, pkt_t *, security_status_t);
    void *arg;

    assert(rh != NULL);

    assert(rh->ev_timeout != NULL);
    fn = rh->fn.recvpkt;
    arg = rh->arg;
    udp_recvpkt_cancel(rh);
    (*fn)(arg, NULL, S_TIMEOUT);
}

/*
 * Given a hostname and a port, setup a udp_handle
 */
int
udp_inithandle(
    udp_handle_t *	udp,
    struct sec_handle *	rh,
    char *              hostname,
    sockaddr_union *addr,
    in_port_t		port,
    char *		handle,
    int			sequence)
{
    /*
     * Save the hostname and port info
     */
    auth_debug(1, _("udp_inithandle port %u handle %s sequence %d\n"),
		   (unsigned int)ntohs(port), handle, sequence);
    assert(addr != NULL);

    rh->hostname = g_strdup(hostname);
    copy_sockaddr(&rh->peer, addr);
    SU_SET_PORT(&rh->peer, port);


    rh->prev = udp->bh_last;
    if (udp->bh_last) {
	rh->prev->next = rh;
    }
    if (!udp->bh_first) {
	udp->bh_first = rh;
    }
    rh->next = NULL;
    udp->bh_last = rh;

    rh->sequence = sequence;
    g_mutex_lock(security_mutex);
    rh->event_id = newevent++;
    g_mutex_unlock(security_mutex);
    amfree(rh->proto_handle);
    rh->proto_handle = g_strdup(handle);
    rh->fn.connect = NULL;
    rh->arg = NULL;
    rh->ev_read = NULL;
    rh->ev_timeout = NULL;

    auth_debug(1, _("udp: adding handle '%s'\n"), rh->proto_handle);

    return(0);
}


/*
 * Callback for received packets.  This is the function bsd_recvpkt
 * registers with the event handler.  It is called when the event handler
 * realizes that data is waiting to be read on the network socket.
 */
void
udp_netfd_read_callback(
    void *	cookie)
{
    struct udp_handle *udp = cookie;
    struct sec_handle *rh;
    int a;
    char hostname[NI_MAXHOST];
    in_port_t port;
    char *errmsg = NULL;
    int result;

    auth_debug(1, _("udp_netfd_read_callback(cookie=%p)\n"), cookie);
    assert(udp != NULL);
    
#ifndef TEST							/* { */
    /*
     * Receive the packet.
     */
    dgram_zero(&udp->dgram);
    if (dgram_recv(&udp->dgram, 0, &udp->peer) < 0)
	return;
#endif /* !TEST */						/* } */

    /*
     * Parse the packet.
     */
    if (str2pkthdr(udp) < 0)
	return;

    /*
     * If there are events waiting on this handle, we're done
     */
    rh = udp->bh_first;
    while(rh != NULL && (!g_str_equal(rh->proto_handle, udp->handle) ||
			 rh->sequence != udp->sequence ||
			 cmp_sockaddr(&rh->peer, &udp->peer, 0) != 0)) {
	rh = rh->next;
    }
    if (rh && event_wakeup(rh->event_id) > 0)
	return;

    /*
     * If we didn't find a handle, then check for a new incoming packet.
     * If no accept handler was setup, then just return.
     */
    if (udp->accept_fn == NULL) {
	g_debug(_("Receive packet from unknown source"));
	dump_sockaddr(&udp->peer);
	//dump_sockaddr(&rh->peer);
	return;
    }

    rh = g_new0(struct sec_handle, 1);
    rh->proto_handle=NULL;
    rh->udp = udp;
    rh->rc = NULL;
    security_handleinit(&rh->sech, udp->driver);

    result = getnameinfo((struct sockaddr *)&udp->peer, SS_LEN(&udp->peer),
			 hostname, sizeof(hostname), NULL, 0, 0);
    if (result != 0) {
	dbprintf("getnameinfo failed: %s\n",
		  gai_strerror(result));
	security_seterror(&rh->sech, "getnameinfo failed: %s",
			  gai_strerror(result));
	return;
    }
    if (check_name_give_sockaddr(hostname,
				 (struct sockaddr *)&udp->peer, &errmsg) < 0) {
	security_seterror(&rh->sech, "%s",errmsg);
	amfree(errmsg);
	amfree(rh);
	return;
    }

    port = SU_GET_PORT(&udp->peer);
    a = udp_inithandle(udp, rh,
		   hostname,
		   &udp->peer,
		   port,
		   udp->handle,
		   udp->sequence);
    if (a < 0) {
	auth_debug(1, _("bsd: closeX handle '%s'\n"), rh->proto_handle);

	amfree(rh);
	return;
    }
    /*
     * Check the security of the packet.  If it is bad, then pass NULL
     * to the accept function instead of a packet.
     */
    if (rh->udp->recv_security_ok(rh, &udp->pkt, rh->udp->need_priv_port) < 0)
	(*udp->accept_fn)(&rh->sech, NULL);
    else
	(*udp->accept_fn)(&rh->sech, &udp->pkt);
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

/*
 * Convert a packet header into a string
 */
const char *
pkthdr2str(
    const struct sec_handle *	rh,
    const pkt_t *		pkt)
{
    static char retbuf[256];

    assert(rh != NULL);
    assert(pkt != NULL);

    g_snprintf(retbuf, sizeof(retbuf), _("Amanda %d.%d %s HANDLE %s SEQ %d\n"),
	VERSION_MAJOR, VERSION_MINOR, pkt_type2str(pkt->type),
	rh->proto_handle, rh->sequence);

    auth_debug(1, _("bsd: pkthdr2str handle '%s'\n"), rh->proto_handle);

    /* check for truncation.  If only we had asprintf()... */
    assert(retbuf[strlen(retbuf) - 1] == '\n');

    return (retbuf);
}

/*
 * Parses out the header line in 'str' into the pkt and handle
 * Returns negative on parse error.
 */
int
str2pkthdr(
    udp_handle_t *	udp)
{
    char *str;
    const char *tok;
    pkt_t *pkt;

    pkt = &udp->pkt;

    assert(udp->dgram.cur != NULL);
    str = g_strdup(udp->dgram.cur);

    /* "Amanda %d.%d <ACK,NAK,...> HANDLE %s SEQ %d\n" */

    /* Read in "Amanda" */
    if ((tok = strtok(str, " ")) == NULL || !g_str_equal(tok, "Amanda"))
	goto parse_error;

    /* nothing is done with the major/minor numbers currently */
    if ((tok = strtok(NULL, " ")) == NULL || strchr(tok, '.') == NULL)
	goto parse_error;

    /* Read in the packet type */
    if ((tok = strtok(NULL, " ")) == NULL)
	goto parse_error;
    amfree(pkt->body);
    pkt_init_empty(pkt, pkt_str2type(tok));
    if (pkt->type == (pktype_t)-1)    
	goto parse_error;

    /* Read in "HANDLE" */
    if ((tok = strtok(NULL, " ")) == NULL || !g_str_equal(tok, "HANDLE"))
	goto parse_error;

    /* parse the handle */
    if ((tok = strtok(NULL, " ")) == NULL)
	goto parse_error;
    amfree(udp->handle);
    udp->handle = g_strdup(tok);

    /* Read in "SEQ" */
    if ((tok = strtok(NULL, " ")) == NULL || !g_str_equal(tok, "SEQ"))   
	goto parse_error;

    /* parse the sequence number */   
    if ((tok = strtok(NULL, "\n")) == NULL)
	goto parse_error;
    udp->sequence = atoi(tok);

    /* Save the body, if any */       
    if ((tok = strtok(NULL, "")) != NULL)
	pkt_cat(pkt, "%s", tok);

    amfree(str);
    return (0);

parse_error:
#if 0 /* XXX we have no way of passing this back up */
    security_seterror(&rh->sech,
	"parse error in packet header : '%s'", origstr);
#endif
    amfree(str);
    return (-1);
}

char *
check_user(
    struct sec_handle *	rh,
    const char *	remoteuser,
    const char *	service)
{
    struct passwd *pwd;
    char *r;
    char *result = NULL;
    char *localuser = NULL;

    /* lookup our local user name */
    if ((pwd = getpwnam(CLIENT_LOGIN)) == NULL) {
	return g_strdup_printf(_("getpwnam(%s) failed."), CLIENT_LOGIN);
    }

    /*
     * Make a copy of the user name in case getpw* is called by
     * any of the lower level routines.
     */
    localuser = g_strdup(pwd->pw_name);

#ifndef USE_AMANDAHOSTS
    r = check_user_ruserok(rh->hostname, pwd, remoteuser);
#else
    r = check_user_amandahosts(rh->hostname, &rh->peer, pwd, remoteuser, service);
#endif
    if (r != NULL) {
	result = g_strdup_printf(
		_("user %s from %s is not allowed to execute the service %s: %s"),
		remoteuser, rh->hostname, service, r);
	amfree(r);
    }
    amfree(localuser);
    return result;
}

#ifndef USE_AMANDAHOSTS
/*
 * See if a remote user is allowed in.  This version uses ruserok()
 * and friends.
 *
 * Returns NULL on success, or error message on error.
 */
char *
check_user_ruserok(
    const char *	host,
    struct passwd *	pwd,
    const char *	remoteuser)
{
    int saved_stderr;
    int fd[2];
    FILE *fError;
    amwait_t exitcode;
    pid_t ruserok_pid;
    pid_t pid;
    char *es;
    char *result;
    int ok;
    uid_t myuid = getuid();
    int devnull;

    /*
     * note that some versions of ruserok (eg SunOS 3.2) look in
     * "./.rhosts" rather than "~CLIENT_LOGIN/.rhosts", so we have to
     * chdir ourselves.  Sigh.
     *
     * And, believe it or not, some ruserok()'s try an initgroup just
     * for the hell of it.  Since we probably aren't root at this point
     * it'll fail, and initgroup "helpfully" will blatt "Setgroups: Not owner"
     * into our stderr output even though the initgroup failure is not a
     * problem and is expected.  Thanks a lot.  Not.
     */
    if (pipe(fd) != 0) {
	return g_strconcat(_("pipe() fails: "), strerror(errno), NULL);
    }
    if ((ruserok_pid = fork()) < 0) {
	return g_strconcat(_("fork() fails: "), strerror(errno), NULL);
    } else if (ruserok_pid == 0) {
	int ec;

	close(fd[0]);
	fError = fdopen(fd[1], "w");
	if (!fError) {
	    error(_("Can't fdopen: %s"), strerror(errno));
	    /*NOTREACHED*/
	}
	/* pamper braindead ruserok's */
	if (chdir(pwd->pw_dir) != 0) {
	    g_fprintf(fError, _("chdir(%s) failed: %s"),
		    pwd->pw_dir, strerror(errno));
	    fclose(fError);
	    exit(1);
	}

	if (debug_auth >= 9) {
	    char *dir = g_strdup(pwd->pw_dir);

	    auth_debug(9, _("bsd: calling ruserok(%s, %d, %s, %s)\n"), host,
			   ((myuid == 0) ? 1 : 0), remoteuser, pwd->pw_name);
	    if (myuid == 0) {
		auth_debug(9, _("bsd: because you are running as root, "));
		auth_debug(9, _("/etc/hosts.equiv will not be used\n"));
	    } else {
		show_stat_info("/etc/hosts.equiv", NULL);
	    }
	    show_stat_info(dir, "/.rhosts");
	    amfree(dir);
	}

	saved_stderr = dup(2);
	if (saved_stderr < 0) {
	    g_debug("Can't dup 2: %s", strerror(errno));
	    ec = 1;
	    exit(ec);
	}
	close(2);
	if ((devnull = open("/dev/null", O_RDWR)) == -1) {
            g_debug(_("Could not open /dev/null: %s"), strerror(errno));
	    ec = 1;
	} else {
	    int devnull2 = -1;
	    if (devnull != 2) {
		devnull2 = dup2(devnull, 2);
	    }
	    ok = ruserok(host, myuid == 0, remoteuser, CLIENT_LOGIN);
	    if (ok < 0) {
	        ec = 1;
	    } else {
	        ec = 0;
	    }
	    close(devnull);
	    if (devnull2 != -1)
		close(devnull2);
	}
	(void)dup2(saved_stderr,2);
	close(saved_stderr);
	exit(ec);
    }
    close(fd[1]);
    fError = fdopen(fd[0], "r");
    if (!fError) {
	error(_("Can't fdopen: %s"), strerror(errno));
	/*NOTREACHED*/
    }

    result = NULL;
    while ((es = pgets(fError)) != NULL) {
	if (*es == 0) {
	    amfree(es);
	    continue;
	}
	if (result == NULL) {
	    result = g_strdup("");
	} else {
	    strappend(result, ": ");
	}
	strappend(result, es);
	amfree(es);
    }
    fclose(fError);

    pid = wait(&exitcode);
    while (pid != ruserok_pid) {
	if ((pid == (pid_t) -1) && (errno != EINTR)) {
	    amfree(result);
	    return g_strdup_printf(_("ruserok wait failed: %s"), strerror(errno));
	}
	pid = wait(&exitcode);
    }
    if (!WIFEXITED(exitcode) || WEXITSTATUS(exitcode) != 0) {
	amfree(result);
	result = str_exit_status("ruserok child", exitcode);
    } else {
	amfree(result);
    }

    return result;
}
#else
/*
 * Check to see if a user is allowed in.  This version uses .amandahosts
 * Returns an error message on failure, or NULL on success.
 */
char *
check_user_amandahosts(
    const char *	host,
    sockaddr_union *addr,
    struct passwd *	pwd,
    const char *	remoteuser,
    const char *	service)
{
    char *line = NULL;
    char *filehost;
    const char *fileuser;
    char *ptmp = NULL;
    char *result = NULL;
    FILE *fp = NULL;
    int found;
    struct stat sbuf;
    int hostmatch;
    int usermatch;
    char *aservice = NULL;
#ifdef WORKING_IPV6
    char ipstr[INET6_ADDRSTRLEN];
#else
    char ipstr[INET_ADDRSTRLEN];
#endif

    auth_debug(1, _("check_user_amandahosts(host=%s, pwd=%p, "
		   "remoteuser=%s, service=%s)\n"),
		   host, pwd, remoteuser, service);

    ptmp = g_strconcat(pwd->pw_dir, "/.amandahosts", NULL);
    if (debug_auth >= 9) {
	show_stat_info(ptmp, "");;
    }
    if ((fp = fopen(ptmp, "r")) == NULL) {
	result = g_strdup_printf(_("cannot open %s: %s"), ptmp, strerror(errno));
	amfree(ptmp);
	return result;
    }

    /*
     * Make sure the file is owned by the Amanda user and does not
     * have any group/other access allowed.
     */
    if (fstat(fileno(fp), &sbuf) != 0) {
	result = g_strdup_printf(_("cannot fstat %s: %s"), ptmp, strerror(errno));
	goto common_exit;
    }
    if (sbuf.st_uid != pwd->pw_uid) {
	result = g_strdup_printf(_("%s: owned by id %ld, should be %ld"),
			ptmp, (long)sbuf.st_uid, (long)pwd->pw_uid);
	goto common_exit;
    }
    if ((sbuf.st_mode & 077) != 0) {
	result = g_strdup_printf(_("%s: incorrect permissions; file must be accessible only by its owner (chmod 600 %s)"), ptmp, ptmp);
	goto common_exit;
    }

    /*
     * Now, scan the file for the host/user/service.
     */
    found = 0;
    while ((line = pgets(fp)) != NULL) {
	if (*line == 0) {
	    amfree(line);
	    continue;
	}

	auth_debug(9, _("bsd: processing line: <%s>\n"), line);
	/* get the host out of the file */
	if ((filehost = strtok(line, " \t")) == NULL) {
	    amfree(line);
	    continue;
	}

	/* get the username.  If no user specified, then use the local user */
	if ((fileuser = strtok(NULL, " \t")) == NULL) {
	    fileuser = pwd->pw_name;
	}

	hostmatch = (strcasecmp(filehost, host) == 0);
	/*  ok if addr=127.0.0.1 and
	 *  either localhost or localhost.domain is in .amandahost */
	if (!hostmatch  &&
	    (strcasecmp(filehost, "localhost")== 0 ||
	     strcasecmp(filehost, "localhost.localdomain")== 0)) {
#ifdef WORKING_IPV6
	    if (SU_GET_FAMILY(addr) == (sa_family_t)AF_INET6)
		inet_ntop(AF_INET6, &addr->sin6.sin6_addr,
			  ipstr, sizeof(ipstr));
	    else
#endif
		inet_ntop(AF_INET, &addr->sin.sin_addr,
			  ipstr, sizeof(ipstr));
	    if (g_str_equal(ipstr, "127.0.0.1") ||
		g_str_equal(ipstr, "::1"))
		hostmatch = 1;
	}
	usermatch = (strcasecmp(fileuser, remoteuser) == 0);
	auth_debug(9, _("bsd: comparing \"%s\" with\n"), filehost);
	auth_debug(9, _("bsd:           \"%s\" (%s)\n"), host,
		       hostmatch ? _("match") : _("no match"));
	auth_debug(9, _("bsd:       and \"%s\" with\n"), fileuser);
	auth_debug(9, _("bsd:           \"%s\" (%s)\n"), remoteuser,
		       usermatch ? _("match") : _("no match"));
	/* compare */
	if (!hostmatch || !usermatch) {
	    amfree(line);
	    continue;
	}

        if (!service) {
	    /* success */
	    amfree(line);
	    found = 1;
	    break;
	}

	/* get the services.  If no service specified, then use
	 * noop/selfcheck/sendsize/sendbackup
         */
	aservice = strtok(NULL, " \t,");
	if (!aservice) {
	    if (g_str_equal(service, "noop") ||
	       g_str_equal(service, "selfcheck") ||
	       g_str_equal(service, "sendsize") ||
	       g_str_equal(service, "sendbackup")) {
		/* success */
		found = 1;
		amfree(line);
		break;
	    }
	    else {
		amfree(line);
		continue;
	    }
	}

	do {
	    if (g_str_equal(aservice, service)) {
		found = 1;
		break;
	    }
	    if (g_str_equal(aservice, "amdump") && 
	       (g_str_equal(service, "noop") ||
		g_str_equal(service, "selfcheck") ||
		g_str_equal(service, "sendsize") ||
		g_str_equal(service, "sendbackup"))) {
		found = 1;
		break;
	    }
	} while((aservice = strtok(NULL, " \t,")) != NULL);

	if (aservice && g_str_equal(aservice, service)) {
	    /* success */
	    found = 1;
	    amfree(line);
	    break;
	}
	amfree(line);
    }
    if (! found) {
	if (g_str_equal(service, "amindexd") ||
	    g_str_equal(service, "amidxtaped")) {
	    result = g_strdup_printf(_("Please add the line \"%s %s amindexd amidxtaped\" to %s on the server"), host, remoteuser, ptmp);
	} else if (g_str_equal(service, "amdump") ||
		   g_str_equal(service, "noop") ||
		   g_str_equal(service, "selfcheck") ||
		   g_str_equal(service, "sendsize") ||
		   g_str_equal(service, "sendbackup")) {
	    result = g_strdup_printf(_("Please add the line \"%s %s amdump\" to %s on the client"), host, remoteuser, ptmp);
	} else {
	    result = g_strdup_printf(_("%s: invalid service %s"), ptmp, service);
	}
    }

common_exit:

    afclose(fp);
    amfree(ptmp);

    return result;
}
#endif

/* return 1 on success, 0 on failure */
int
check_security(
    sockaddr_union *addr,
    char *		str,
    unsigned long	cksum,
    char **		errstr,
    char               *service G_GNUC_UNUSED)
{
    char *		remotehost = NULL, *remoteuser = NULL;
    char *		bad_bsd = NULL;
    struct passwd *	pwptr;
    uid_t		myuid;
    char *		s;
    char *		fp;
    int			ch;
    char		hostname[NI_MAXHOST];
    in_port_t		port;
    int			result;

    (void)cksum;	/* Quiet unused parameter warning */

    auth_debug(1,
	       _("check_security(addr=%p, str='%s', cksum=%lu, errstr=%p\n"),
		addr, str, cksum, errstr);
    dump_sockaddr(addr);

    *errstr = NULL;

    /* what host is making the request? */
    if ((result = getnameinfo((struct sockaddr *)addr, SS_LEN(addr),
			      hostname, NI_MAXHOST, NULL, 0, 0)) != 0) {
	dbprintf(_("getnameinfo failed: %s\n"),
		  gai_strerror(result));
	*errstr = g_strjoin(NULL, "[", "addr ", str_sockaddr(addr), ": ",
			    "getnameinfo failed: ", gai_strerror(result),
			    "]", NULL);
	return 0;
    }
    remotehost = g_strdup(hostname);
    if( check_name_give_sockaddr(hostname,
				 (struct sockaddr *)addr, errstr) < 0) {
	amfree(remotehost);
	return 0;
    }


    /* next, make sure the remote port is a "reserved" one */
    port = SU_GET_PORT(addr);
    if (port >= IPPORT_RESERVED) {
	*errstr = g_strdup_printf(_("[host %s: port %u not secure]"),
			remotehost, (unsigned int)port);
	amfree(remotehost);
	return 0;
    }

    /* extract the remote user name from the message */

    s = str;
    ch = *s++;

    bad_bsd = g_strdup_printf(_("[host %s: bad bsd security line]"), remotehost);

    if (strncmp_const_skip(s - 1, "USER ", s, ch) != 0) {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	amfree(remotehost);
	return 0;
    }

    skip_whitespace(s, ch);
    if (ch == '\0') {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	amfree(remotehost);
	return 0;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    remoteuser = g_strdup(fp);
    s[-1] = (char)ch;
    amfree(bad_bsd);

    /* lookup our local user name */

    myuid = getuid();
    if ((pwptr = getpwuid(myuid)) == NULL)
        error(_("error [getpwuid(%d) fails]"), (int)myuid);

    auth_debug(1, _("bsd security: remote host %s user %s local user %s\n"),
		   remotehost, remoteuser, pwptr->pw_name);

#ifndef USE_AMANDAHOSTS
    s = check_user_ruserok(remotehost, pwptr, remoteuser);
#else
    s = check_user_amandahosts(remotehost, addr, pwptr, remoteuser, service);
#endif

    if (s != NULL) {
	*errstr = g_strdup_printf(_("[access as %s not allowed from %s@%s: %s]"),
			    pwptr->pw_name, remoteuser, remotehost, s);
    }
    amfree(s);
    amfree(remotehost);
    amfree(remoteuser);
    return *errstr == NULL;
}

/*
 * Like read(), but waits until the entire buffer has been filled.
 */
ssize_t
net_read(
    int		fd,
    void *	vbuf,
    size_t	origsize,
    int		timeout)
{
    char *buf = vbuf;	/* ptr arith */
    ssize_t nread;
    size_t size = origsize;

    auth_debug(1, _("net_read: begin %zu\n"), origsize);

    while (size > 0) {
	auth_debug(1, _("net_read: while %p %zu\n"), buf, size);
	nread = net_read_fillbuf(fd, timeout, buf, size);
	if (nread < 0) {
    	    auth_debug(1, _("db: net_read: end return(-1)\n"));
	    return (-1);
	}
	if (nread == 0) {
    	    auth_debug(1, _("net_read: end return(0)\n"));
	    return (0);
	}
	buf += nread;
	size -= nread;
    }
    auth_debug(1, _("net_read: end %zu\n"), origsize);
    return ((ssize_t)origsize);
}

/*
 * net_read likes to do a lot of little reads.  Buffer it.
 */
ssize_t
net_read_fillbuf(
    int		fd,
    int		timeout,
    void *	buf,
    size_t	size)
{
    SELECT_ARG_TYPE readfds;
    struct timeval tv;
    ssize_t nread;

    auth_debug(1, _("net_read_fillbuf: begin\n"));
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    switch (select(fd + 1, &readfds, NULL, NULL, &tv)) {
    case 0:
	auth_debug(1, "net_read_fillbuf: case 0: timeout");
	errno = ETIMEDOUT;
	/* FALLTHROUGH */
    case -1:
	auth_debug(1, _("net_read_fillbuf: case -1\n"));
	return (-1);
    case 1:
	auth_debug(1, _("net_read_fillbuf: case 1\n"));
	assert(FD_ISSET(fd, &readfds));
	break;
    default:
	auth_debug(1, _("net_read_fillbuf: case default\n"));
	assert(0);
	break;
    }
    nread = read(fd, buf, size);
    auth_debug(1, _("net_read_fillbuf: end %zd\n"), nread);
    if (nread < 0) {
	g_debug("err: %p %s", buf, strerror(errno));
	return (-1);
    }
    return (nread);
}


/*
 * Display stat() information about a file.
 */

void
show_stat_info(
    char *	a,
    char *	b)
{
    char *name = g_strjoin(NULL, a, b, NULL);
    struct stat sbuf;
    struct passwd *pwptr G_GNUC_UNUSED;
    struct passwd pw G_GNUC_UNUSED;
    char *owner;
    struct group *grptr G_GNUC_UNUSED;
    struct group gr G_GNUC_UNUSED;
    char *group;
    int buflen G_GNUC_UNUSED;
    char *buf G_GNUC_UNUSED;

    if (stat(name, &sbuf) != 0) {
	auth_debug(1, _("bsd: cannot stat %s: %s\n"), name, strerror(errno));
	amfree(name);
	return;
    }

#ifdef _SC_GETPW_R_SIZE_MAX
    buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buflen == -1)
	buflen = 1024;
#else
    buflen = 1024;
#endif
    buf = malloc(buflen);

#ifdef HAVE_GETPWUID_R
    if (getpwuid_r(sbuf.st_uid, &pw, buf, buflen, &pwptr) == 0 &&
	pwptr != NULL) {
	owner = g_strdup(pwptr->pw_name);
    } else
#endif
    {
	owner = g_malloc(NUM_STR_SIZE + 1);
	g_snprintf(owner, NUM_STR_SIZE, "%ld", (long)sbuf.st_uid);
    }
#ifdef HAVE_GETGRGID_R
    if (getgrgid_r(sbuf.st_gid, &gr, buf, buflen, &grptr) == 0 &&
	grptr != NULL) {
	group = g_strdup(grptr->gr_name);
    } else
#endif
    {
	group = g_malloc(NUM_STR_SIZE + 1);
	g_snprintf(group, NUM_STR_SIZE, "%ld", (long)sbuf.st_gid);
    }

    auth_debug(1, _("bsd: processing file: %s\n"), name);
    auth_debug(1, _("bsd:                  owner=%s group=%s mode=%03o\n"),
		   owner, group,
		   (int) (sbuf.st_mode & 0777));
    amfree(name);
    amfree(owner);
    amfree(group);
    amfree(buf);
}

int
check_name_give_sockaddr(
    const char *hostname,
    struct sockaddr *addr,
    char **errstr)
{
    int result;
    struct addrinfo *res = NULL, *res1;
    char *canonname;

    result = resolve_hostname(hostname, 0, &res, &canonname);
    if (result != 0) {
	dbprintf(_("check_name_give_sockaddr: resolve_hostname('%s'): %s\n"), hostname, gai_strerror(result));
        g_free(*errstr);
        *errstr = g_strdup_printf(_("check_name_give_sockaddr: resolve_hostname('%s'): %s"),
                                  hostname, gai_strerror(result));
	goto error;
    }
    if (canonname == NULL) {
	dbprintf(_("resolve_hostname('%s') did not return a canonical name\n"), hostname);
        g_free(*errstr);
        *errstr = g_strdup_printf(_("check_name_give_sockaddr: resolve_hostname('%s') did not return a canonical name"),
                                  hostname);
	goto error;
    }

    if (strncasecmp(hostname, canonname, strlen(hostname)) != 0) {
	dbprintf(_("%s doesn't resolve to itself, it resolves to %s\n"),
		       hostname, canonname);
        g_free(*errstr);
        *errstr = g_strdup_printf(_("%s doesn't resolve to itself, it resolves to %s"),
                                  hostname, canonname);
	goto error;
    }

    for(res1=res; res1 != NULL; res1 = res1->ai_next) {
	if (cmp_sockaddr((sockaddr_union *)res1->ai_addr, (sockaddr_union *)addr, 1) == 0) {
	    freeaddrinfo(res);
	    amfree(canonname);
	    return 0;
	}
    }

    g_debug("%s doesn't resolve to %s",
	    hostname, str_sockaddr((sockaddr_union *)addr));
			   g_free(*errstr);
			   *errstr = g_strdup_printf("%s doesn't resolve to %s",
                                                     hostname, str_sockaddr((sockaddr_union *)addr));
error:
    if (res) freeaddrinfo(res);
    amfree(canonname);
    return -1;
}

in_port_t
find_port_for_service(
    char *service,
    char *proto)
{
    in_port_t  port;
    char      *s;
    int        all_numeric = 1;

    for (s=service; *s != '\0'; s++) {
	if (!isdigit((int)*s)) {
	    all_numeric = 0;
	}
    }

    if (all_numeric == 1) {
	port = atoi(service);
    } else {
        struct servent *result;
#ifdef HAVE_FUNC_GETSERVBYNAME_R_6
        struct servent sp;
        char buf[2048];
	int r;
	r = getservbyname_r(service, proto, &sp, buf, 2048, &result);
	assert(r != ERANGE);
	if (r != 0) {
	    port = 0;
	} else {
	    port = (in_port_t)(ntohs((in_port_t)sp.s_port));
	}

#elif defined HAVE_FUNC_GETSERVBYNAME_R_5
        struct servent sp;
        char buf[2048];
	result = getservbyname_r(service, proto, &sp, buf, 2048);
	if (result == 0) {
	    assert(errno != ERANGE);
	    port = 0;
	} else {
	    port = (in_port_t)(ntohs((in_port_t)sp.s_port));
	}
#elif defined HAVE_FUNC_GETSERVBYNAME_R_4
        struct servent sp;
	struct servent_data servent_data;
	int r;
	memset(&servent_data, 0, sizeof(struct servent_data));
	r = getservbyname_r(service, proto, &sp, &servent_data);
	result = &sp;
	if (r != 0) {
	    port = 0;
	} else {
	    port = (in_port_t)(ntohs((in_port_t)result->s_port));
	}
#else
	if ((result = getservbyname(service, proto)) == NULL) {
	    port = 0;
	} else {
	    port = (in_port_t)(ntohs((in_port_t)result->s_port));
	}
#endif
    }

    return port;
}

char *
sec_get_authenticated_peer_name_hostname(
    security_handle_t *hdl)
{
    char *hostname = ((struct sec_handle *)hdl)->hostname;
    if (!hostname)
	hostname = "";
    return g_strdup(hostname);
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
