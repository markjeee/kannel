/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2009 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/* conn.c - implement Connection type
 *
 * This file implements the interface defined in conn.h.
 *
 * Richard Braakman
 *
 * SSL client implementation contributed by
 * Jarkko Kovala <jarkko.kovala@iki.fi>
 *
 * SSL server implementation contributed by
 * Stipe Tolj <stolj@wapme.de> for Wapme Systems AG
 */

/* TODO: unlocked_close() on error */
/* TODO: have I/O functions check if connection is open */
/* TODO: have conn_open_tcp do a non-blocking connect() */

#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

#include "gwlib/gwlib.h"

#ifdef HAVE_LIBSSL
#include <openssl/ssl.h>
#include <openssl/err.h>

static SSL_CTX *global_ssl_context = NULL;
static SSL_CTX *global_server_ssl_context = NULL;
#endif /* HAVE_LIBSSL */

typedef unsigned long (*CRYPTO_CALLBACK_PTR)(void);

/*
 * This used to be 4096.  It is now 0 so that callers don't have to
 * deal with the complexities of buffering (i.e. deciding when to
 * flush) unless they want to.
 * FIXME: Figure out how to combine buffering sensibly with use of
 * conn_register.
 */
#define DEFAULT_OUTPUT_BUFFERING 0
#define SSL_CONN_TIMEOUT         30

struct Connection
{
    /* We use separate locks for input and ouput fields, so that
     * read and write activities don't have to get in each other's
     * way.  If you need both, then acquire the outlock first. */
    Mutex *inlock;
    Mutex *outlock;
    volatile sig_atomic_t claimed;
#ifndef NO_GWASSERT
    long claiming_thread;
#endif

    /* fd value is read-only and is not locked */
    int fd;

    /* socket state */
    enum {yes,no} connected;

    /* Protected by outlock */
    Octstr *outbuf;
    long outbufpos;   /* start of unwritten data in outbuf */

    /* Try to buffer writes until there are this many octets to send.
     * Set it to 0 to get an unbuffered connection. */
    unsigned int output_buffering;

    /* Protected by inlock */
    Octstr *inbuf;
    long inbufpos;    /* start of unread data in inbuf */

    int read_eof;     /* we encountered eof on read */
    int io_error;   /* we encountered error on IO operation */

    /* Protected by both locks when updating, so you need only one
     * of the locks when reading. */
    FDSet *registered;
    conn_callback_t *callback;
    void *callback_data;
    conn_callback_data_destroyer_t *callback_data_destroyer;
    /* Protected by inlock */
    int listening_pollin;
    /* Protected by outlock */
    int listening_pollout;

#ifdef HAVE_LIBSSL
    SSL *ssl;
    X509 *peer_certificate;
#endif /* HAVE_LIBSSL */
};

static void unlocked_register_pollin(Connection *conn, int onoff);
static void unlocked_register_pollout(Connection *conn, int onoff);

/* There are a number of functions that play with POLLIN and POLLOUT flags.
 * The general rule is that we always want to poll for POLLIN except when
 * we have detected eof (which may be reported as eternal POLLIN), and
 * we want to poll for POLLOUT only if there's data waiting in the
 * output buffer.  If output buffering is set, we may not want to poll for
 * POLLOUT if there's not enough data waiting, which is why we have
 * unlocked_try_write. */

/* Macros to get more information for debugging purposes */
#define unlock_in(conn) unlock_in_real(conn, __FILE__, __LINE__, __func__)
#define unlock_out(conn) unlock_out_real(conn, __FILE__, __LINE__, __func__)

/* Lock a Connection's read direction, if the Connection is unclaimed */
static void inline lock_in(Connection *conn)
{
    gw_assert(conn != NULL);

    if (conn->claimed)
        gw_assert(gwthread_self() == conn->claiming_thread);
    else
        mutex_lock(conn->inlock);
}

/* Unlock a Connection's read direction, if the Connection is unclaimed */
static void inline unlock_in_real(Connection *conn, char *file, int line, const char *func)
{
    int ret;
    gw_assert(conn != NULL);

    if (!conn->claimed && (ret = mutex_unlock(conn->inlock)) != 0) {
        panic(0, "%s:%ld: %s: Mutex unlock failed. "
            "(Called from %s:%ld:%s.)",
            __FILE__, (long) __LINE__, __func__,
            file, (long) line, func);
    }
}

/* Lock a Connection's write direction, if the Connection is unclaimed */
static void inline lock_out(Connection *conn)
{
    gw_assert(conn != NULL);

    if (conn->claimed)
        gw_assert(gwthread_self() == conn->claiming_thread);
    else
        mutex_lock(conn->outlock);
}

/* Unlock a Connection's write direction, if the Connection is unclaimed */
static void inline unlock_out_real(Connection *conn, char *file, int line, const char *func)
{
    int ret;
    gw_assert(conn != NULL);

    if (!conn->claimed && (ret = mutex_unlock(conn->outlock)) != 0) {
        panic(0, "%s:%ld: %s: Mutex unlock failed. "
            "(Called from %s:%ld:%s.)",
            __FILE__, (long) __LINE__, __func__,
            file, (long) line, func);
    }
}

/* Return the number of bytes in the Connection's output buffer */
static long inline unlocked_outbuf_len(Connection *conn)
{
    return octstr_len(conn->outbuf) - conn->outbufpos;
}

/* Return the number of bytes in the Connection's input buffer */
static long inline unlocked_inbuf_len(Connection *conn)
{
    return octstr_len(conn->inbuf) - conn->inbufpos;
}

/* Send as much data as can be sent without blocking.  Return the number
 * of bytes written, or -1 in case of error. */
static long unlocked_write(Connection *conn)
{
    long ret = 0;

#ifdef HAVE_LIBSSL
    if (conn->ssl != NULL) {
        if (octstr_len(conn->outbuf) - conn->outbufpos > 0)
            ret = SSL_write(conn->ssl,
                        octstr_get_cstr(conn->outbuf) + conn->outbufpos,
                        octstr_len(conn->outbuf) - conn->outbufpos);

        if (ret < 0) {
            int SSL_error = SSL_get_error(conn->ssl, ret);

            if (SSL_error == SSL_ERROR_WANT_READ || SSL_error == SSL_ERROR_WANT_WRITE) {
                  ret = 0; /* no error */
            } else {
                error(errno, "SSL write failed: OpenSSL error %d: %s",
                      SSL_error, ERR_error_string(SSL_error, NULL));
                return -1;
            }
        }
    } else
#endif /* HAVE_LIBSSL */
        ret = octstr_write_data(conn->outbuf, conn->fd, conn->outbufpos);

    if (ret < 0) {
        conn->io_error = 1;
        return -1;
    }

    conn->outbufpos += ret;

    /* Heuristic: Discard the already-written data if it's more than
     * half of the total.  This should keep the buffer size small
     * without wasting too many cycles on moving data around. */
    if (conn->outbufpos > octstr_len(conn->outbuf) / 2) {
        octstr_delete(conn->outbuf, 0, conn->outbufpos);
        conn->outbufpos = 0;
    }

    if (conn->registered)
        unlocked_register_pollout(conn, unlocked_outbuf_len(conn) > 0);

    return ret;
}

/* Try to empty the output buffer without blocking.  Return 0 for success,
 * 1 if there is still data left in the buffer, and -1 for errors. */
static int unlocked_try_write(Connection *conn)
{
    long len;

    len = unlocked_outbuf_len(conn);
    if (len == 0)
        return 0;

    if (len < (long) conn->output_buffering)
        return 1;

    if (unlocked_write(conn) < 0)
        return -1;

    if (unlocked_outbuf_len(conn) > 0)
        return 1;

    return 0;
}

/* Read whatever data is currently available, up to an internal maximum. */
static void unlocked_read(Connection *conn)
{
    unsigned char buf[4096];
    long len;

    if (conn->inbufpos > 0) {
        octstr_delete(conn->inbuf, 0, conn->inbufpos);
        conn->inbufpos = 0;
    }

#ifdef HAVE_LIBSSL
    if (conn->ssl != NULL) {
        len = SSL_read(conn->ssl, buf, sizeof(buf));
    } else
#endif /* HAVE_LIBSSL */
        len = read(conn->fd, buf, sizeof(buf));

    if (len < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return;
#ifdef HAVE_LIBSSL
        if (conn->ssl) {
            int SSL_error = SSL_get_error(conn->ssl, len);
            if (SSL_error == SSL_ERROR_WANT_WRITE || SSL_error == SSL_ERROR_WANT_READ)
                return; /* no error */
            error(errno, "SSL read failed: OpenSSL error %d: %s",
                      SSL_error, ERR_error_string(SSL_error, NULL));
        }
        else
#endif /* HAVE_LIBSSL */
            error(errno, "Error reading from fd %d:", conn->fd);
        conn->io_error = 1;
        if (conn->registered)
            unlocked_register_pollin(conn, 0);
        return;
    } else if (len == 0) {
        conn->read_eof = 1;
        if (conn->registered)
            unlocked_register_pollin(conn, 0);
    } else {
        octstr_append_data(conn->inbuf, buf, len);
    }
}

/* Cut "length" octets from the input buffer and return them as an Octstr */
static Octstr *unlocked_get(Connection *conn, long length)
{
    Octstr *result = NULL;

    gw_assert(unlocked_inbuf_len(conn) >= length);
    result = octstr_copy(conn->inbuf, conn->inbufpos, length);
    conn->inbufpos += length;

    return result;
}

/* Tell the fdset whether we are interested in POLLIN events, but only
 * if the status changed.  (Calling fdset_listen can be expensive if
 * it requires synchronization with the polling thread.)
 * We must already have the inlock.
 */
static void unlocked_register_pollin(Connection *conn, int onoff)
{
    gw_assert(conn->registered);

    if (onoff == 1 && !conn->listening_pollin) {
        /* Turn it on */
        conn->listening_pollin = 1;
        fdset_listen(conn->registered, conn->fd, POLLIN, POLLIN);
    } else if (onoff == 0 && conn->listening_pollin) {
        /* Turn it off */
        conn->listening_pollin = 0;
        fdset_listen(conn->registered, conn->fd, POLLIN, 0);
    }
}

/* Tell the fdset whether we are interested in POLLOUT events, but only
 * if the status changed.  (Calling fdset_listen can be expensive if
 * it requires synchronization with the polling thread.)
 * We must already have the outlock.
 */
static void unlocked_register_pollout(Connection *conn, int onoff)
{
    gw_assert(conn->registered);

    if (onoff == 1 && !conn->listening_pollout) {
        /* Turn it on */
        conn->listening_pollout = 1;
        fdset_listen(conn->registered, conn->fd, POLLOUT, POLLOUT);
    } else if (onoff == 0 && conn->listening_pollout) {
        /* Turn it off */
        conn->listening_pollout = 0;
        fdset_listen(conn->registered, conn->fd, POLLOUT, 0);
    }
}

#ifdef HAVE_LIBSSL
static int conn_init_client_ssl(Connection *ret, Octstr *certkeyfile)
{
    ret->ssl = SSL_new(global_ssl_context);

    /*
     * The current thread's error queue must be empty before
     * the TLS/SSL I/O operation is attempted, or SSL_get_error()
     * will not work reliably.
     */
    ERR_clear_error();

    if (certkeyfile != NULL) {
        SSL_use_certificate_file(ret->ssl, octstr_get_cstr(certkeyfile),
                                 SSL_FILETYPE_PEM);
        SSL_use_PrivateKey_file(ret->ssl, octstr_get_cstr(certkeyfile),
                                SSL_FILETYPE_PEM);
        if (SSL_check_private_key(ret->ssl) != 1) {
            error(0, "conn_open_ssl: private key isn't consistent with the "
                     "certificate from file %s (or failed reading the file)",
                  octstr_get_cstr(certkeyfile));
            return -1;
        }
    }

    /* SSL_set_fd can fail, so check it */
    if (SSL_set_fd(ret->ssl, ret->fd) == 0) {
        /* SSL_set_fd failed, log error */
        error(errno, "SSL: OpenSSL: %.256s", ERR_error_string(ERR_get_error(), NULL));
        return -1;
    }

    /*
     * make sure the socket is non-blocking while we do SSL_connect
     */
    if (socket_set_blocking(ret->fd, 0) < 0) {
        return -1;
    }
    BIO_set_nbio(SSL_get_rbio(ret->ssl), 1);
    BIO_set_nbio(SSL_get_wbio(ret->ssl), 1);

    SSL_set_connect_state(ret->ssl);
    
    return 0;
}

Connection *conn_open_ssl_nb(Octstr *host, int port, Octstr *certkeyfile,
                          Octstr *our_host)
{
    Connection *ret;

    /* open the TCP connection */
    if (!(ret = conn_open_tcp_nb(host, port, our_host))) {
        return NULL;
    }
    
    if (conn_init_client_ssl(ret, certkeyfile) == -1) {
        conn_destroy(ret);
        return NULL;
    }
    
    return ret;
}

Connection *conn_open_ssl(Octstr *host, int port, Octstr *certkeyfile,
			  Octstr *our_host)
{
    Connection *ret;

    /* open the TCP connection */
    if (!(ret = conn_open_tcp(host, port, our_host))) {
        return NULL;
    }

    if (conn_init_client_ssl(ret, certkeyfile) == -1) {
        conn_destroy(ret);
        return NULL;
    }

    return ret;
}

#endif /* HAVE_LIBSSL */

Connection *conn_open_tcp(Octstr *host, int port, Octstr *our_host)
{
    return conn_open_tcp_with_port(host, port, 0, our_host);
}

Connection *conn_open_tcp_nb(Octstr *host, int port, Octstr *our_host)
{
  return conn_open_tcp_nb_with_port(host, port, 0, our_host);
}

Connection *conn_open_tcp_nb_with_port(Octstr *host, int port, int our_port,
				       Octstr *our_host)
{
  int sockfd;
  int done = -1;
  Connection *c;

  sockfd = tcpip_connect_nb_to_server_with_port(octstr_get_cstr(host), port,
						our_port, our_host == NULL ?
						NULL : octstr_get_cstr(our_host), &done);
  if (sockfd < 0)
    return NULL;
  c = conn_wrap_fd(sockfd, 0);
  if (done != 0) {
    c->connected = no;
  }
  return c;
}

int conn_is_connected(Connection *conn)
{
  return conn->connected == yes ? 0 : -1;
}

int conn_get_connect_result(Connection *conn)
{
  int err;
  socklen_t len;

  len = sizeof(err);
  if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
    return -1;
  }
  
  if (err) {
    return -1;
  }
  
  conn->connected = yes;
  return 0;
}

Connection *conn_open_tcp_with_port(Octstr *host, int port, int our_port,
		Octstr *our_host)
{
    int sockfd;

    sockfd = tcpip_connect_to_server_with_port(octstr_get_cstr(host), port,
					       our_port, our_host == NULL ?
					       NULL : octstr_get_cstr(our_host));
    if (sockfd < 0)
	return NULL;
    return conn_wrap_fd(sockfd, 0);
}


/*
 * XXX bad assumption here that conn_wrap_fd for SSL can only happens
 * for the server side!!!! FIXME !!!!
 */
Connection *conn_wrap_fd(int fd, int ssl)
{
    Connection *conn;

    if (socket_set_blocking(fd, 0) < 0)
        return NULL;

    conn = gw_malloc(sizeof(*conn));
    conn->inlock = mutex_create();
    conn->outlock = mutex_create();
    conn->claimed = 0;

    conn->outbuf = octstr_create("");
    conn->outbufpos = 0;
    conn->inbuf = octstr_create("");
    conn->inbufpos = 0;

    conn->fd = fd;
    conn->connected = yes;
    conn->read_eof = 0;
    conn->io_error = 0;
    conn->output_buffering = DEFAULT_OUTPUT_BUFFERING;

    conn->registered = NULL;
    conn->callback = NULL;
    conn->callback_data = NULL;
    conn->callback_data_destroyer = NULL;
    conn->listening_pollin = 0;
    conn->listening_pollout = 0;
#ifdef HAVE_LIBSSL
    /*
     * do all the SSL magic for this connection
     */
    if (ssl) {
        conn->ssl = SSL_new(global_server_ssl_context);
        conn->peer_certificate = NULL;

        /* SSL_set_fd can fail, so check it */
        if (SSL_set_fd(conn->ssl, conn->fd) == 0) {
            /* SSL_set_fd failed, log error and return NULL */
            error(errno, "SSL: OpenSSL: %.256s", ERR_error_string(ERR_get_error(), NULL));
            conn_destroy(conn);
            return NULL;
        }
        /* SSL_set_verify(conn->ssl, 0, NULL); */

        /* set read/write BIO layer to non-blocking mode */
        BIO_set_nbio(SSL_get_rbio(conn->ssl), 1);
        BIO_set_nbio(SSL_get_wbio(conn->ssl), 1);

        /* set accept state , SSL-Handshake will be handled transparent while SSL_[read|write] */
         SSL_set_accept_state(conn->ssl);
    } else {
        conn->ssl = NULL;
        conn->peer_certificate = NULL;
    }
#endif /* HAVE_LIBSSL */

    return conn;
}

void conn_destroy(Connection *conn)
{
    int ret;

    if (conn == NULL)
        return;

    /* No locking done here.  conn_destroy should not be called
     * if any thread might still be interested in the connection. */

    if (conn->registered) {
        fdset_unregister(conn->registered, conn->fd);
        /* call data destroyer if any */
        if (conn->callback_data != NULL && conn->callback_data_destroyer != NULL)
            conn->callback_data_destroyer(conn->callback_data);
    }

    if (conn->fd >= 0) {
        /* Try to flush any remaining data */
        unlocked_try_write(conn);

#ifdef HAVE_LIBSSL
        if (conn->ssl != NULL) {
            SSL_smart_shutdown(conn->ssl);
            SSL_free(conn->ssl);
            if (conn->peer_certificate != NULL)
                X509_free(conn->peer_certificate);
	}
#endif /* HAVE_LIBSSL */

        ret = close(conn->fd);
        if (ret < 0)
            error(errno, "conn_destroy: error on close");
        conn->fd = -1;
    }

    octstr_destroy(conn->outbuf);
    octstr_destroy(conn->inbuf);
    mutex_destroy(conn->inlock);
    mutex_destroy(conn->outlock);

    gw_free(conn);
}

void conn_claim(Connection *conn)
{
    gw_assert(conn != NULL);

    if (conn->claimed)
        panic(0, "Connection is being claimed twice!");
    conn->claimed = 1;
#ifndef NO_GWASSERT
    conn->claiming_thread = gwthread_self();
#endif
}

long conn_outbuf_len(Connection *conn)
{
    long len;

    lock_out(conn);
    len = unlocked_outbuf_len(conn);
    unlock_out(conn);

    return len;
}

long conn_inbuf_len(Connection *conn)
{
    long len;

    lock_in(conn);
    len = unlocked_inbuf_len(conn);
    unlock_in(conn);

    return len;
}

int conn_eof(Connection *conn)
{
    int eof;

    lock_in(conn);
    eof = conn->read_eof;
    unlock_in(conn);

    return eof;
}

int conn_error(Connection *conn)
{
    int err;

    lock_out(conn);
    lock_in(conn);
    err = conn->io_error;
    unlock_in(conn);
    unlock_out(conn);

    return err;
}

void conn_set_output_buffering(Connection *conn, unsigned int size)
{
    lock_out(conn);
    conn->output_buffering = size;
    /* If the buffer size is smaller, we may have to write immediately. */
    unlocked_try_write(conn);
    unlock_out(conn);
}

static void poll_callback(int fd, int revents, void *data)
{
    Connection *conn;
    int do_callback = 0;

    conn = data;

    if (conn == NULL) {
        error(0, "poll_callback called with NULL connection.");
        return;
    }

    if (conn->fd != fd) {
        error(0, "poll_callback called on wrong connection.");
        return;
    }

    /* Get result of nonblocking connect, before any reads and writes
     * we must check result (it must be handled in initial callback) */
    if (conn->connected == no) {
        if (conn->callback)
            conn->callback(conn, conn->callback_data);
        return;
    }

    /* If got POLLERR or POLHUP, then unregister the descriptor from the
     * fdset and set the error condition variable to let the upper layer
     * close and destroy the connection. */
    if (revents & (POLLERR|POLLHUP)) {
        lock_out(conn);
        lock_in(conn);
        if (conn->listening_pollin)
            unlocked_register_pollin(conn, 0);
        if (conn->listening_pollout)
            unlocked_register_pollout(conn, 0);
        conn->io_error = 1;
        unlock_in(conn);
        unlock_out(conn);
        do_callback = 1;
    }

    /* If unlocked_write manages to write all pending data, it will
     * tell the fdset to stop listening for POLLOUT. */
    if (revents & POLLOUT) {
        lock_out(conn);
        unlocked_write(conn);
        if (unlocked_outbuf_len(conn) == 0)
            do_callback = 1;
        unlock_out(conn);
    }

    /* We read only in unlocked_read in we received POLLIN, cause the 
     * descriptor is already broken and of no use anymore. */
    if (revents & POLLIN) {
        lock_in(conn);
        unlocked_read(conn);
        unlock_in(conn);
        do_callback = 1;
    }

    if (do_callback && conn->callback)
        conn->callback(conn, conn->callback_data);
}

int conn_register_real(Connection *conn, FDSet *fdset,
                  conn_callback_t callback, void *data, conn_callback_data_destroyer_t *data_destroyer)
{
    int events;
    int result = 0;

    gw_assert(conn != NULL);

    if (conn->fd < 0)
        return -1;

    /* We need both locks if we want to update the registration
     * information. */
    lock_out(conn);
    lock_in(conn);

    if (conn->registered == fdset) {
        /* Re-registering.  Change only the callback info. */
        conn->callback = callback;
        /* call data destroyer if new data supplied */
        if (conn->callback_data != NULL && conn->callback_data != data && conn->callback_data_destroyer != NULL)
            conn->callback_data_destroyer(conn->callback_data);
        conn->callback_data = data;
        conn->callback_data_destroyer = data_destroyer;
        result = 0;
    } else if (conn->registered) {
        /* Already registered to a different fdset. */
        result = -1;
    } else {
        events = 0;
	/* For nonconnected socket we must lesten both directions */
        if (conn->connected == yes) {
            if (conn->read_eof == 0 && conn->io_error == 0)
                events |= POLLIN;
            if (unlocked_outbuf_len(conn) > 0)
                events |= POLLOUT;
        } else {
          events |= POLLIN | POLLOUT;
        }

        conn->registered = fdset;
        conn->callback = callback;
        conn->callback_data = data;
        conn->callback_data_destroyer = data_destroyer;
        conn->listening_pollin = (events & POLLIN) != 0;
        conn->listening_pollout = (events & POLLOUT) != 0;
        fdset_register(fdset, conn->fd, events, poll_callback, conn);
        result = 0;
    }

    unlock_in(conn);
    unlock_out(conn);

    return result;
}

void conn_unregister(Connection *conn)
{
    FDSet *set = NULL;
    int fd = -1;
    void *data = NULL;
    conn_callback_data_destroyer_t *destroyer = NULL;
    
    gw_assert(conn != NULL);

    if (conn == NULL || conn->fd < 0)
        return;

    /* We need both locks to update the registration information */
    lock_out(conn);
    lock_in(conn);

    if (conn->registered) {
        set = conn->registered;
        fd = conn->fd;
        conn->registered = NULL;
        conn->callback = NULL;
        /*
         * remember and don't destroy data and data_destroyer because we
         * may be in callback right now. So destroy only after fdset_unregister
         * call which guarantee us we are not in callback anymore.
         */
        data = conn->callback_data;
        conn->callback_data = NULL;
        destroyer = conn->callback_data_destroyer;
        conn->callback_data_destroyer = NULL;
        conn->listening_pollin = 0;
        conn->listening_pollout = 0;
    }

    unlock_in(conn);
    unlock_out(conn);

    /* now unregister from FDSet */
    if (set != NULL)
        fdset_unregister(set, fd);

    /* ok we are not in callback anymore, destroy data if any */
    if (data != NULL && destroyer != NULL)
        destroyer(data);
}

int conn_wait(Connection *conn, double seconds)
{
    int events;
    int ret;
    int fd;

    lock_out(conn);

    /* Try to write any data that might still be waiting to be sent */
    ret = unlocked_write(conn);
    if (ret < 0) {
        unlock_out(conn);
        return -1;
    }
    if (ret > 0) {
        /* We did something useful.  No need to poll or wait now. */
        unlock_out(conn);
        return 0;
    }

    fd = conn->fd;

    /* Normally, we block until there is more data available.  But
     * if any data still needs to be sent, we block until we can
     * send it (or there is more data available).  We always block
     * for reading, unless we know there is no more data coming.
     * (Because in that case, poll will keep reporting POLLIN to
     * signal the end of the file).  If the caller explicitly wants
     * to wait even though there is no data to write and we're at
     * end of file, then poll for new data anyway because the caller
     * apparently doesn't trust eof. */
    events = 0;
    if (unlocked_outbuf_len(conn) > 0)
        events |= POLLOUT;
    /* Don't keep the connection locked while we wait */
    unlock_out(conn);

    /* We need the in lock to query read_eof */
    lock_in(conn);
    if ((conn->read_eof == 0 && conn->io_error == 0) || events == 0)
        events |= POLLIN;
    unlock_in(conn);

    ret = gwthread_pollfd(fd, events, seconds);
    if (ret < 0) {
        if (errno == EINTR)
            return 0;
        error(0, "conn_wait: poll failed on fd %d:", fd);
        return -1;
    }

    if (ret == 0)
        return 1;

    if (ret & POLLNVAL) {
        error(0, "conn_wait: fd %d not open.", fd);
        return -1;
    }

    if (ret & (POLLERR | POLLHUP)) {
        /* Call unlocked_read to report the specific error,
         * and handle the results of the error.  We can't be
         * certain that the error still exists, because we
         * released the lock for a while. */
        lock_in(conn);
        unlocked_read(conn);
        unlock_in(conn);
        return -1;
    }

    /* If POLLOUT is on, then we must have wanted
     * to write something. */
    if (ret & POLLOUT) {
        lock_out(conn);
        unlocked_write(conn);
        unlock_out(conn);
    }

    /* Since we normally select for reading, we must
     * try to read here.  Otherwise, if the caller loops
     * around conn_wait without making conn_read* calls
     * in between, we will keep polling this same data. */
    if (ret & POLLIN) {
        lock_in(conn);
        unlocked_read(conn);
        unlock_in(conn);
    }

    return 0;
}

int conn_flush(Connection *conn)
{
    int ret;
    int revents;
    int fd;

    lock_out(conn);
    ret = unlocked_write(conn);
    if (ret < 0) {
        unlock_out(conn);
        return -1;
    }

    while (unlocked_outbuf_len(conn) != 0) {
        fd = conn->fd;

        unlock_out(conn);
        revents = gwthread_pollfd(fd, POLLOUT, -1.0);

        /* Note: Make sure we have the "out" lock when
         * going through the loop again, because the 
         * loop condition needs it. */

        if (revents < 0) {
            if (errno == EINTR)
                return 1;
            error(0, "conn_flush: poll failed on fd %d:", fd);
            return -1;
        }

        if (revents == 0) {
            /* We were woken up */
            return 1;
        }

        if (revents & POLLNVAL) {
            error(0, "conn_flush: fd %d not open.", fd);
            return -1;
        }

        lock_out(conn);

        if (revents & (POLLOUT | POLLERR | POLLHUP)) {
            ret = unlocked_write(conn);
            if (ret < 0) {
                unlock_out(conn);
                return -1;
            }
        }
    }

    unlock_out(conn);

    return 0;
}

int conn_write(Connection *conn, Octstr *data)
{
    int ret;

    lock_out(conn);
    octstr_append(conn->outbuf, data);
    ret = unlocked_try_write(conn);
    unlock_out(conn);

    return ret;
}

int conn_write_data(Connection *conn, unsigned char *data, long length)
{
    int ret;

    lock_out(conn);
    octstr_append_data(conn->outbuf, data, length);
    ret = unlocked_try_write(conn);
    unlock_out(conn);

    return ret;
}

int conn_write_withlen(Connection *conn, Octstr *data)
{
    int ret;
    unsigned char lengthbuf[4];

    encode_network_long(lengthbuf, octstr_len(data));
    lock_out(conn);
    octstr_append_data(conn->outbuf, lengthbuf, 4);
    octstr_append(conn->outbuf, data);
    ret = unlocked_try_write(conn);
    unlock_out(conn);

    return ret;
}

Octstr *conn_read_everything(Connection *conn)
{
    Octstr *result = NULL;

    lock_in(conn);
    if (unlocked_inbuf_len(conn) == 0) {
        unlocked_read(conn);
        if (unlocked_inbuf_len(conn) == 0) {
            unlock_in(conn);
            return NULL;
        }
    }

    result = unlocked_get(conn, unlocked_inbuf_len(conn));
    gw_claim_area(result);
    unlock_in(conn);

    return result;
}

Octstr *conn_read_fixed(Connection *conn, long length)
{
    Octstr *result = NULL;

    if (length < 1)
        return NULL;

    /* See if the data is already available.  If not, try a read(),
     * then see if we have enough data after that.  If not, give up. */
    lock_in(conn);
    if (unlocked_inbuf_len(conn) < length) {
        unlocked_read(conn);
        if (unlocked_inbuf_len(conn) < length) {
            unlock_in(conn);
            return NULL;
        }
    }
    result = unlocked_get(conn, length);
    gw_claim_area(result);
    unlock_in(conn);

    return result;
}

Octstr *conn_read_line(Connection *conn)
{
    Octstr *result = NULL;
    long pos;

    lock_in(conn);
    /* 10 is the code for linefeed.  We don't rely on \n because that
     * might be a different value on some (strange) systems, and
     * we are reading from a network connection. */
    pos = octstr_search_char(conn->inbuf, 10, conn->inbufpos);
    if (pos < 0) {
        unlocked_read(conn);
        pos = octstr_search_char(conn->inbuf, 10, conn->inbufpos);
        if (pos < 0) {
            unlock_in(conn);
            return NULL;
        }
    }

    result = unlocked_get(conn, pos - conn->inbufpos);
    gw_claim_area(result);

    /* Skip the LF, which we left in the buffer */
    conn->inbufpos++;

    /* If the line was terminated with CR LF, we have to remove
     * the CR from the result. */
    if (octstr_len(result) > 0 &&
        octstr_get_char(result, octstr_len(result) - 1) == 13)
        octstr_delete(result, octstr_len(result) - 1, 1);

    unlock_in(conn);
    return result;
}

Octstr *conn_read_withlen(Connection *conn)
{
    Octstr *result = NULL;
    unsigned char lengthbuf[4];
    long length = 0; /* for compiler please */
    int try, retry;

    lock_in(conn);

    for (try = 1; try <= 2; try++) {
        if (try > 1)
            unlocked_read(conn);

        do {
            retry = 0;
            /* First get the length. */
            if (unlocked_inbuf_len(conn) < 4)
                continue;

            octstr_get_many_chars(lengthbuf, conn->inbuf, conn->inbufpos, 4);
            length = decode_network_long(lengthbuf);

            if (length < 0) {
                warning(0, "conn_read_withlen: got negative length, skipping");
                conn->inbufpos += 4;
                retry = 1;
             }
        } while(retry == 1);

        /* Then get the data. */
        if (unlocked_inbuf_len(conn) - 4 < length)
            continue;

        conn->inbufpos += 4;
        result = unlocked_get(conn, length);
        gw_claim_area(result);
        break;
    }

    unlock_in(conn);
    return result;
}

Octstr *conn_read_packet(Connection *conn, int startmark, int endmark)
{
    int startpos, endpos;
    Octstr *result = NULL;
    int try;

    lock_in(conn);

    for (try = 1; try <= 2; try++) {
        if (try > 1)
            unlocked_read(conn);

        /* Find startmark, and discard everything up to it */
        startpos = octstr_search_char(conn->inbuf, startmark, conn->inbufpos);
        if (startpos < 0) {
            conn->inbufpos = octstr_len(conn->inbuf);
            continue;
        } else {
            conn->inbufpos = startpos;
        }

        /* Find first endmark after startmark */
        endpos = octstr_search_char(conn->inbuf, endmark, conn->inbufpos);
        if (endpos < 0)
            continue;

        result = unlocked_get(conn, endpos - startpos + 1);
        gw_claim_area(result);
        break;
    }

    unlock_in(conn);
    return result;
}

#ifdef HAVE_LIBSSL
X509 *conn_get_peer_certificate(Connection *conn) 
{
    /* Don't know if it needed to be locked , but better safe as crash */
    lock_out(conn);
    lock_in(conn);
    if (conn->peer_certificate == NULL && conn->ssl != NULL)
        conn->peer_certificate = SSL_get_peer_certificate(conn->ssl);
    unlock_in(conn);
    unlock_out(conn);
    
    return conn->peer_certificate;
}

/*
 * XXX Alex decalred the RSA callback routine static and now we're getting 
 * warning messages for our automatic compilation tests. So we are commenting
 * the function out to avoid the warnings.
 *

static RSA *tmp_rsa_callback(SSL *ssl, int export, int key_len)
{
    static RSA *rsa = NULL;
    debug("gwlib.http", 0, "SSL: Generating new RSA key (export=%d, keylen=%d)", export, key_len);
    if (export) {
	   rsa = RSA_generate_key(key_len, RSA_F4, NULL, NULL);
    } else {
	   debug("gwlib.http", 0, "SSL: Export not set");
    }
    return rsa;
}
*/

static Mutex **ssl_static_locks = NULL;

/* the call-back function for the openssl crypto thread locking */
static void openssl_locking_function(int mode, int n, const char *file, int line)
{
    if (mode & CRYPTO_LOCK)
        mutex_lock(ssl_static_locks[n-1]);
    else
        mutex_unlock(ssl_static_locks[n-1]);
}

void openssl_init_locks(void)
{
    int c, maxlocks = CRYPTO_num_locks();

    gw_assert(ssl_static_locks == NULL);

    ssl_static_locks = gw_malloc(sizeof(Mutex *) * maxlocks);
    for (c = 0; c < maxlocks; c++)
        ssl_static_locks[c] = mutex_create();

    /* after the mutexes have been created, apply the call-back to it */
    CRYPTO_set_locking_callback(openssl_locking_function);
    CRYPTO_set_id_callback((CRYPTO_CALLBACK_PTR)gwthread_self);
}

void openssl_shutdown_locks(void)
{
    int c, maxlocks = CRYPTO_num_locks();

    gw_assert(ssl_static_locks != NULL);

    /* remove call-back from the locks */
    CRYPTO_set_locking_callback(NULL);

    for (c = 0; c < maxlocks; c++) 
        mutex_destroy(ssl_static_locks[c]);

    gw_free(ssl_static_locks);
    ssl_static_locks = NULL;
}

void conn_init_ssl(void)
{
    SSL_library_init();
    SSL_load_error_strings();
    global_ssl_context = SSL_CTX_new(SSLv23_client_method());
    SSL_CTX_set_mode(global_ssl_context, 
        SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
}

void server_ssl_init(void) 
{
    SSLeay_add_ssl_algorithms();
    SSL_load_error_strings();
    global_server_ssl_context = SSL_CTX_new(SSLv23_server_method());
    SSL_CTX_set_mode(global_server_ssl_context, 
        SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    if (!SSL_CTX_set_default_verify_paths(global_server_ssl_context)) {
	   panic(0, "can not set default path for server");
    }
}

void conn_shutdown_ssl(void)
{
    if (global_ssl_context)
        SSL_CTX_free(global_ssl_context);
    
    ERR_free_strings();
    EVP_cleanup();
}

void server_shutdown_ssl(void)
{
    if (global_server_ssl_context)
        SSL_CTX_free(global_server_ssl_context);

    ERR_free_strings();
    EVP_cleanup();
}

void use_global_client_certkey_file(Octstr *certkeyfile)
{ 
    SSL_CTX_use_certificate_file(global_ssl_context, 
                                 octstr_get_cstr(certkeyfile), 
                                 SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(global_ssl_context,
                                octstr_get_cstr(certkeyfile),
                                SSL_FILETYPE_PEM);
    if (SSL_CTX_check_private_key(global_ssl_context) != 1)
        panic(0, "reading global client certificate file `%s', the certificate "
	      "isn't consistent with the private key (or failed reading the file)", 
              octstr_get_cstr(certkeyfile));
    info(0, "Using global SSL certificate and key from file `%s'",
         octstr_get_cstr(certkeyfile));
}

void use_global_server_certkey_file(Octstr *certfile, Octstr *keyfile) 
{
    SSL_CTX_use_certificate_file(global_server_ssl_context, 
                                  octstr_get_cstr(certfile), 
                                  SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(global_server_ssl_context,
                                 octstr_get_cstr(keyfile),
                                 SSL_FILETYPE_PEM);
    if (SSL_CTX_check_private_key(global_server_ssl_context) != 1) {
        error(0, "SSL: %s", ERR_error_string(ERR_get_error(), NULL));
        panic(0, "reading global server certificate file %s, the certificate \
                  isn't consistent with the private key in file %s \
                  (or failed reading the file)", 
                  octstr_get_cstr(certfile), octstr_get_cstr(keyfile));
    }
    info(0, "Using global server SSL certificate from file `%s'", octstr_get_cstr(certfile));
    info(0, "Using global server SSL key from file `%s'", octstr_get_cstr(keyfile));
}

static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
    char    subject[256];
    char    issuer [256];
    char   *status;

    X509_NAME_oneline(X509_get_subject_name(ctx->current_cert), subject, sizeof(subject));
    X509_NAME_oneline(X509_get_issuer_name(ctx->current_cert), issuer, sizeof (issuer));

    status = preverify_ok ? "Accepting" : "Rejecting";
    
    info(0, "%s certificate for \"%s\" signed by \"%s\"", status, subject, issuer);
    
    return preverify_ok;
}

void use_global_trusted_ca_file(Octstr *ssl_trusted_ca_file)
{
    if (ssl_trusted_ca_file != NULL) {
	if (!SSL_CTX_load_verify_locations(global_ssl_context,
					   octstr_get_cstr(ssl_trusted_ca_file),
					   NULL)) {
	    panic(0, "Failed to load SSL CA file: %s", octstr_get_cstr(ssl_trusted_ca_file));
	} else {
	    info(0, "Using CA root certificates from file %s",
		 octstr_get_cstr(ssl_trusted_ca_file));
	    SSL_CTX_set_verify(global_ssl_context,
			       SSL_VERIFY_PEER,
			       verify_callback);
	}
	
    } else {
	SSL_CTX_set_verify(global_ssl_context,
			   SSL_VERIFY_NONE,
			   NULL);
    }
}

void conn_config_ssl (CfgGroup *grp)
{
    Octstr *ssl_client_certkey_file = NULL;
    Octstr *ssl_server_cert_file    = NULL;
    Octstr *ssl_server_key_file     = NULL;
    Octstr *ssl_trusted_ca_file     = NULL;

    /*
     * check if SSL is desired for HTTP servers and then
     * load SSL client and SSL server public certificates 
     * and private keys
     */    
    ssl_client_certkey_file = cfg_get(grp, octstr_imm("ssl-client-certkey-file"));
    if (ssl_client_certkey_file != NULL) 
        use_global_client_certkey_file(ssl_client_certkey_file);
    
    ssl_server_cert_file = cfg_get(grp, octstr_imm("ssl-server-cert-file"));
    ssl_server_key_file = cfg_get(grp, octstr_imm("ssl-server-key-file"));
    
    if (ssl_server_cert_file != NULL && ssl_server_key_file != NULL) {
        use_global_server_certkey_file(ssl_server_cert_file, 
				       ssl_server_key_file);
    }

    ssl_trusted_ca_file = cfg_get(grp, octstr_imm("ssl-trusted-ca-file"));
    
    use_global_trusted_ca_file(ssl_trusted_ca_file);

    octstr_destroy(ssl_client_certkey_file);
    octstr_destroy(ssl_server_cert_file);
    octstr_destroy(ssl_server_key_file);
    octstr_destroy(ssl_trusted_ca_file);
}

SSL *conn_get_ssl(Connection *conn)
{
    if (conn != NULL)
        return conn->ssl;
    else 
        return NULL;
}

#else

void conn_config_ssl (CfgGroup *grp)
{
    info(0, "SSL not supported, no SSL initialization done.");
}
#endif /* HAVE_LIBSSL */

int conn_get_id(Connection *conn) {
    if(conn == NULL)
	return 0;
    else
	return conn->fd;
}
