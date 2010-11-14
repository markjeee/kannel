/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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

/*
 * http.c - HTTP protocol server and client implementation
 *
 * Implements major parts of the Hypertext Transfer Protocol HTTP/1.1 (RFC 2616)
 * See http://www.w3.org/Protocols/rfc2616/rfc2616.txt
 *
 * Lars Wirzenius
 */
 
/* XXX re-implement socket pools, with idle connection killing to 
    	save sockets */
/* XXX implement http_abort */
/* XXX give maximum input size */
/* XXX kill http_get_real */
/* XXX the proxy exceptions list should be a dict, I guess */
/* XXX set maximum number of concurrent connections to same host, total? */
/* XXX 100 status codes. */
/* XXX stop destroying persistent connections when a request is redirected */

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "gwlib.h"
#include "gwlib/regex.h"

/* comment this out if you don't want HTTP responses to be dumped */
#define DUMP_RESPONSE 1

/* define http client connections timeout in seconds (set to -1 for disable) */
static int http_client_timeout = 240;

/* define http server connections timeout in seconds (set to -1 for disable) */
#define HTTP_SERVER_TIMEOUT 60

/***********************************************************************
 * Stuff used in several sub-modules.
 */


/*
 * Default port to connect to for HTTP connections.
 */
enum { HTTP_PORT = 80,
       HTTPS_PORT = 443 };


/*
 * Status of this module.
 */
static enum { 
    limbo, 
    running, 
    terminating 
} run_status = limbo;


/*
 * Which interface to use for outgoing HTTP requests.
 */
static Octstr *http_interface = NULL;


/*
 * Read some headers, i.e., until the first empty line (read and discard
 * the empty line as well). Return -1 for error, 0 for all headers read,
 * 1 for more headers to follow.
 */
static int read_some_headers(Connection *conn, List *headers)
{
    Octstr *line, *prev;

    if (gwlist_len(headers) == 0)
        prev = NULL;
    else
    	prev = gwlist_get(headers, gwlist_len(headers) - 1);

    for (;;) {
	line = conn_read_line(conn);
	if (line == NULL) {
            if (conn_eof(conn) || conn_error(conn))
	    	return -1;
	    return 1;
	}
        if (octstr_len(line) == 0) {
            octstr_destroy(line);
            break;
        }
        if (isspace(octstr_get_char(line, 0)) && prev != NULL) {
            octstr_append(prev, line);
            octstr_destroy(line);
        } else {
            gwlist_append(headers, line);
            prev = line;
        }
    }

    return 0;
}


/*
 * Check that the HTTP version string is valid. Return -1 for invalid,
 * 0 for version 1.0, 1 for 1.x.
 */
static int parse_http_version(Octstr *version)
{
    Octstr *prefix;
    long prefix_len;
    int digit;
    
    prefix = octstr_imm("HTTP/1.");
    prefix_len = octstr_len(prefix);

    if (octstr_ncompare(version, prefix, prefix_len) != 0)
    	return -1;
    if (octstr_len(version) != prefix_len + 1)
    	return -1;
    digit = octstr_get_char(version, prefix_len);
    if (!isdigit(digit))
    	return -1;
    if (digit == '0')
    	return 0;
    return 1;
}


/***********************************************************************
 * Proxy support.
 */


/*
 * Data and functions needed to support proxy operations. If proxy_hostname 
 * is NULL, no proxy is used.
 */
static Mutex *proxy_mutex = NULL;
static Octstr *proxy_hostname = NULL;
static int proxy_port = 0;
static int proxy_ssl = 0;
static Octstr *proxy_username = NULL;
static Octstr *proxy_password = NULL;
static List *proxy_exceptions = NULL;
static regex_t *proxy_exceptions_regex = NULL;


static void proxy_add_authentication(List *headers)
{
    Octstr *os;
    
    if (proxy_username == NULL || proxy_password == NULL)
    	return;

    os = octstr_format("%S:%S", proxy_username, proxy_password);
    octstr_binary_to_base64(os);
    octstr_strip_blanks(os);
    octstr_insert(os, octstr_imm("Basic "), 0);
    http_header_add(headers, "Proxy-Authorization", octstr_get_cstr(os));
    octstr_destroy(os);
}


static void proxy_init(void)
{
    proxy_mutex = mutex_create();
    proxy_exceptions = gwlist_create();
}


static void proxy_shutdown(void)
{
    http_close_proxy();
    mutex_destroy(proxy_mutex);
    proxy_mutex = NULL;
}


static int proxy_used_for_host(Octstr *host, Octstr *url)
{
    int i;

    mutex_lock(proxy_mutex);

    if (proxy_hostname == NULL) {
        mutex_unlock(proxy_mutex);
        return 0;
    }

    for (i = 0; i < gwlist_len(proxy_exceptions); ++i) {
        if (octstr_compare(host, gwlist_get(proxy_exceptions, i)) == 0) {
            mutex_unlock(proxy_mutex);
            return 0;
        }
    }

    if (proxy_exceptions_regex != NULL && gw_regex_match_pre(proxy_exceptions_regex, url)) {
            mutex_unlock(proxy_mutex);
            return 0;
    }

    mutex_unlock(proxy_mutex);
    return 1;
}


void http_use_proxy(Octstr *hostname, int port, int ssl, List *exceptions,
    	    	    Octstr *username, Octstr *password, Octstr *exceptions_regex)
{
    Octstr *e;
    int i;

    gw_assert(run_status == running);
    gw_assert(hostname != NULL);
    gw_assert(octstr_len(hostname) > 0);
    gw_assert(port > 0);

    http_close_proxy();
    mutex_lock(proxy_mutex);

    proxy_hostname = octstr_duplicate(hostname);
    proxy_port = port;
    proxy_ssl = ssl;
    proxy_exceptions = gwlist_create();
    for (i = 0; i < gwlist_len(exceptions); ++i) {
        e = gwlist_get(exceptions, i);
        debug("gwlib.http", 0, "HTTP: Proxy exception `%s'.", octstr_get_cstr(e));
        gwlist_append(proxy_exceptions, octstr_duplicate(e));
    }
    if (exceptions_regex != NULL &&
        (proxy_exceptions_regex = gw_regex_comp(exceptions_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(exceptions_regex));
    proxy_username = octstr_duplicate(username);
    proxy_password = octstr_duplicate(password);
    debug("gwlib.http", 0, "Using proxy <%s:%d> with %s scheme", 
    	  octstr_get_cstr(proxy_hostname), proxy_port,
    	  (proxy_ssl ? "HTTPS" : "HTTP"));

    mutex_unlock(proxy_mutex);
}


void http_close_proxy(void)
{
    gw_assert(run_status == running || run_status == terminating);

    mutex_lock(proxy_mutex);
    proxy_port = 0;
    octstr_destroy(proxy_hostname);
    octstr_destroy(proxy_username);
    octstr_destroy(proxy_password);
    proxy_hostname = NULL;
    proxy_username = NULL;
    proxy_password = NULL;
    gwlist_destroy(proxy_exceptions, octstr_destroy_item);
    gw_regex_destroy(proxy_exceptions_regex);
    proxy_exceptions = NULL;
    proxy_exceptions_regex = NULL;
    mutex_unlock(proxy_mutex);
}


/***********************************************************************
 * Common functions for reading request or result entities.
 */

/*
 * Value to pass to entity_create.
 */
enum body_expectation {
   /*
    * Message must not have a body, even if the headers indicate one.
    * (i.e. response to HEAD method).
    */
   expect_no_body,
   /*
    * Message will have a body if Content-Length or Transfer-Encoding
    * headers are present (i.e. most request methods).
    */
   expect_body_if_indicated,
   /*
    * Message will have a body, possibly zero-length.
    * (i.e. 200 OK responses to a GET method.)
    */
   expect_body
};

enum entity_state {
    reading_headers,
    reading_chunked_body_len,
    reading_chunked_body_data,
    reading_chunked_body_crlf,
    reading_chunked_body_trailer,
    reading_body_until_eof,
    reading_body_with_length,
    body_error,
    entity_done
};

typedef struct {
    List *headers;
    Octstr *body;
    enum body_expectation expect_state;
    enum entity_state state;
    long chunked_body_chunk_len;
    long expected_body_len;
} HTTPEntity;


/*
 * The rules for message bodies (length and presence) are defined
 * in RFC2616 paragraph 4.3 and 4.4.
 */
static void deduce_body_state(HTTPEntity *ent)
{
    Octstr *h = NULL;

    if (ent->expect_state == expect_no_body) {
        ent->state = entity_done;
        return;
    }

    ent->state = body_error;  /* safety net */

    h = http_header_find_first(ent->headers, "Transfer-Encoding");
    if (h != NULL) {
        octstr_strip_blanks(h);
        if (octstr_str_compare(h, "chunked") != 0) {
            error(0, "HTTP: Unknown Transfer-Encoding <%s>",
                  octstr_get_cstr(h));
            ent->state = body_error;
        } else {
            ent->state = reading_chunked_body_len;
        }
        octstr_destroy(h);
        return;
    }

    h = http_header_find_first(ent->headers, "Content-Length");
    if (h != NULL) {
        if (octstr_parse_long(&ent->expected_body_len, h, 0, 10) == -1 ||
            ent->expected_body_len < 0) {
            error(0, "HTTP: Content-Length header wrong: <%s>",
                  octstr_get_cstr(h));
            ent->state = body_error;
        } else if (ent->expected_body_len == 0) {
            ent->state = entity_done;
        } else {
            ent->state = reading_body_with_length;
        }
        octstr_destroy(h);
        return;
    }

    if (ent->expect_state == expect_body)
        ent->state = reading_body_until_eof;
    else
        ent->state = entity_done;
}


/*
 * Create a HTTPEntity structure suitable for reading the expected
 * result or request message and decoding the transferred entity (if any).
 * See the definition of enum body_expectation for the possible values
 * of exp.
 */
static HTTPEntity *entity_create(enum body_expectation exp)
{
    HTTPEntity *ent;

    ent = gw_malloc(sizeof(*ent));
    ent->headers = http_create_empty_headers();
    ent->body = octstr_create("");
    ent->chunked_body_chunk_len = -1;
    ent->expected_body_len = -1;
    ent->state = reading_headers;
    ent->expect_state = exp;

    return ent;
}


static void entity_destroy(HTTPEntity *ent)
{
    if (ent == NULL)
        return;

    http_destroy_headers(ent->headers);
    octstr_destroy(ent->body);
    gw_free(ent);
}


static void read_chunked_body_len(HTTPEntity *ent, Connection *conn)
{
    Octstr *os;
    long len;
    
    os = conn_read_line(conn);
    if (os == NULL) {
        if (conn_error(conn) || conn_eof(conn))
	    ent->state = body_error;
        return;
    }
    if (octstr_parse_long(&len, os, 0, 16) == -1) {
        octstr_destroy(os);
	ent->state = body_error;
        return;
    }
    octstr_destroy(os);
    if (len == 0)
        ent->state = reading_chunked_body_trailer;
    else {
        ent->state = reading_chunked_body_data;
        ent->chunked_body_chunk_len = len;
    }
}


static void read_chunked_body_data(HTTPEntity *ent, Connection *conn)
{
    Octstr *os;

    os = conn_read_fixed(conn, ent->chunked_body_chunk_len);
    if (os == NULL) {
        if (conn_error(conn) || conn_eof(conn))
	    ent->state = body_error;
    } else {
        octstr_append(ent->body, os);
        octstr_destroy(os);
        ent->state = reading_chunked_body_crlf;
    }
}


static void read_chunked_body_crlf(HTTPEntity *ent, Connection *conn)
{
    Octstr *os;

    os = conn_read_line(conn);
    if (os == NULL) {
        if (conn_error(conn) || conn_eof(conn))
	    ent->state = body_error;
    } else {
        octstr_destroy(os);
        ent->state = reading_chunked_body_len;
    }
}


static void read_chunked_body_trailer(HTTPEntity *ent, Connection *conn)
{
    int ret;

    ret = read_some_headers(conn, ent->headers);
    if (ret == -1)
	ent->state = body_error;
    if (ret == 0)
        ent->state = entity_done;
}


static void read_body_until_eof(HTTPEntity *ent, Connection *conn)
{
    Octstr *os;

    while ((os = conn_read_everything(conn)) != NULL) {
        octstr_append(ent->body, os);
        octstr_destroy(os);
    }
    if (conn_error(conn))
	ent->state = body_error;
    if (conn_eof(conn))
	ent->state = entity_done;
}


static void read_body_with_length(HTTPEntity *ent, Connection *conn)
{
    Octstr *os;

    os = conn_read_fixed(conn, ent->expected_body_len);
    if (os == NULL) {
        if (conn_error(conn) || conn_eof(conn))
            ent->state = body_error;
        return;
    }
    octstr_destroy(ent->body);
    ent->body = os;
    ent->state = entity_done;
}


/*
 * Read headers and body (if any) from this connection.  Return 0 if it's
 * complete, 1 if we expect more input, and -1 if there is something wrong.
 */
static int entity_read(HTTPEntity *ent, Connection *conn)
{
    int ret;
    enum entity_state old_state;

    /*
     * In this loop, each state will process as much input as it needs
     * and then switch to the next state, unless it's a final state in
     * which case it returns directly, or unless it needs more input.
     * So keep looping as long as the state changes.
     */
    do {
	old_state = ent->state;
	switch (ent->state) {
	case reading_headers:
	    ret = read_some_headers(conn, ent->headers);
            if (ret == 0)
	        deduce_body_state(ent);
	    if (ret < 0)
		return -1;
	    break;

	case reading_chunked_body_len:
	    read_chunked_body_len(ent, conn);
	    break;
		
	case reading_chunked_body_data:
	    read_chunked_body_data(ent, conn);
	    break;

	case reading_chunked_body_crlf:
	    read_chunked_body_crlf(ent, conn);
	    break;

	case reading_chunked_body_trailer:
	    read_chunked_body_trailer(ent, conn);
	    break;

	case reading_body_until_eof:
	    read_body_until_eof(ent, conn);
	    break;

	case reading_body_with_length:
	    read_body_with_length(ent, conn);
	    break;

	case body_error:
	    return -1;

	case entity_done:
	    return 0;

	default:
	    panic(0, "Internal error: Invalid HTTPEntity state.");
	}
    } while (ent->state != old_state);

    /*
     * If we got here, then the loop ended because a non-final state
     * needed more input.
     */
    return 1;
}


/***********************************************************************
 * HTTP client interface.
 */

/*
 * Internal lists of completely unhandled requests and requests for which
 * a request has been sent but response has not yet been read.
 */
static List *pending_requests = NULL;


/*
 * Have background threads been started?
 */
static Mutex *client_thread_lock = NULL;
static volatile sig_atomic_t client_threads_are_running = 0;


/*
 * Set of all connections to all servers. Used with conn_register to
 * do I/O on several connections with a single thread.
 */
static FDSet *client_fdset = NULL;

/*
 * Maximum number of HTTP redirections to follow. Making this infinite
 * could cause infinite looping if the redirections loop.
 */
#define HTTP_MAX_FOLLOW 5


/*
 * The implemented HTTP method strings
 * Order is sequenced by the enum in the header
 */
static char *http_methods[] = {
    "GET", "POST", "HEAD"
};

/*
 * Information about a server we've connected to.
 */
typedef struct {
    HTTPCaller *caller;
    void *request_id;
    int method;             /* uses enums from http.h for the HTTP methods */
    Octstr *url;            /* the full URL, including scheme, host, etc. */
    Octstr *uri;            /* the HTTP URI path only */
    List *request_headers;
    Octstr *request_body;   /* NULL for GET or HEAD, non-NULL for POST */
    enum {
	connecting,
	request_not_sent,
	reading_status,
	reading_entity,
	transaction_done
    } state;
    long status;
    int persistent;
    HTTPEntity *response; /* Can only be NULL if status < 0 */
    Connection *conn;
    Octstr *host;
    long port;
    int follow_remaining;
    Octstr *certkeyfile;
    int ssl;
    Octstr *username;	/* For basic authentication */
    Octstr *password;
} HTTPServer;


static int send_request(HTTPServer *trans);
static Octstr *build_response(List *headers, Octstr *body);

static HTTPServer *server_create(HTTPCaller *caller, int method, Octstr *url,
                                 List *headers, Octstr *body, int follow_remaining,
                                 Octstr *certkeyfile)
{
    HTTPServer *trans;
    
    trans = gw_malloc(sizeof(*trans));
    trans->caller = caller;
    trans->request_id = NULL;
    trans->method = method;
    trans->url = octstr_duplicate(url);
    trans->uri = NULL;
    trans->request_headers = http_header_duplicate(headers);
    trans->request_body = octstr_duplicate(body);
    trans->state = request_not_sent;
    trans->status = -1;
    trans->persistent = 0;
    trans->response = NULL;
    trans->conn = NULL;
    trans->host = NULL;
    trans->port = 0;
    trans->username = NULL;
    trans->password = NULL;
    trans->follow_remaining = follow_remaining;
    trans->certkeyfile = octstr_duplicate(certkeyfile);
    trans->ssl = 0;
    return trans;
}


static void server_destroy(void *p)
{
    HTTPServer *trans;
    
    trans = p;
    octstr_destroy(trans->url);
    octstr_destroy(trans->uri);
    http_destroy_headers(trans->request_headers);
    trans->request_headers = NULL;
    octstr_destroy(trans->request_body);
    entity_destroy(trans->response);
    octstr_destroy(trans->host);
    octstr_destroy(trans->certkeyfile);
    octstr_destroy(trans->username);
    octstr_destroy(trans->password);
    gw_free(trans);
}


/*
 * Pool of open, but unused connections to servers or proxies. Key is
 * "servername:port", value is List with Connection objects.
 */
static Dict *conn_pool;
static Mutex *conn_pool_lock;


static void conn_pool_item_destroy(void *item)
{
    gwlist_destroy(item, (void(*)(void*))conn_destroy);
}

static void conn_pool_init(void)
{
    conn_pool = dict_create(1024, conn_pool_item_destroy);
    conn_pool_lock = mutex_create();
}


static void conn_pool_shutdown(void)
{
    dict_destroy(conn_pool);
    mutex_destroy(conn_pool_lock);
}


static inline Octstr *conn_pool_key(Octstr *host, int port, int ssl, Octstr *certfile, Octstr *our_host)
{
    return octstr_format("%S:%d:%d:%S:%S", host, port, ssl?1:0, certfile?certfile:octstr_imm(""),
                         our_host?our_host:octstr_imm(""));
}


static Connection *conn_pool_get(Octstr *host, int port, int ssl, Octstr *certkeyfile,
		Octstr *our_host)
{
    Octstr *key;
    List *list = NULL;
    Connection *conn = NULL;
    int retry;

    do {
        retry = 0;
        key = conn_pool_key(host, port, ssl, certkeyfile, our_host);
        mutex_lock(conn_pool_lock);
        list = dict_get(conn_pool, key);
        if (list != NULL)
            conn = gwlist_extract_first(list);
        mutex_unlock(conn_pool_lock);
        /*
         * Note: we don't hold conn_pool_lock when we check/destroy/unregister
         *       connection because otherwise we can deadlock! And it's even better
         *       not to delay other threads while we check connection.
         */
        if (conn != NULL) {
#ifdef USE_KEEPALIVE
            /* unregister our server disconnect callback */
            conn_unregister(conn);
#endif 
            /*
             * Check whether the server has closed the connection while
             * it has been in the pool.
             */
            conn_wait(conn, 0);
            if (conn_eof(conn) || conn_error(conn)) {
                debug("gwlib.http", 0, "HTTP:conn_pool_get: Server closed connection, destroying it <%s><%p><fd:%d>.",
                      octstr_get_cstr(key), conn, conn_get_id(conn));
                conn_destroy(conn);
                retry = 1;
                conn = NULL;
            }
        }
        octstr_destroy(key);
    } while(retry == 1);
    
    if (conn == NULL) {
#ifdef HAVE_LIBSSL
        if (ssl) 
            conn = conn_open_ssl_nb(host, port, certkeyfile, our_host);
        else
#endif /* HAVE_LIBSSL */
            conn = conn_open_tcp_nb(host, port, our_host);
        debug("gwlib.http", 0, "HTTP: Opening connection to `%s:%d' (fd=%d).",
              octstr_get_cstr(host), port, conn_get_id(conn));
    } else {
        debug("gwlib.http", 0, "HTTP: Reusing connection to `%s:%d' (fd=%d).",
              octstr_get_cstr(host), port, conn_get_id(conn)); 
    }
    
    return conn;
}

#ifdef USE_KEEPALIVE
static void check_pool_conn(Connection *conn, void *data)
{
    Octstr *key = data;
    
    if (run_status != running) {
        conn_unregister(conn);
        return;
    }
    /* check if connection still ok */
    if (conn_error(conn) || conn_eof(conn)) {
        List *list;
        mutex_lock(conn_pool_lock);
        list = dict_get(conn_pool, key);
        if (gwlist_delete_equal(list, conn) > 0) {
            /*
             * ok, connection was still within pool. So it's
             * safe to destroy this connection.
             */
            debug("gwlib.http", 0, "HTTP: Server closed connection, destroying it <%s><%p><fd:%d>.",
                  octstr_get_cstr(key), conn, conn_get_id(conn));
            conn_unregister(conn);
            conn_destroy(conn);
        }
        /*
         * it's perfectly valid if connection was not found in connection pool because
         * in 'conn_pool_get' we first removed connection from pool with conn_pool_lock locked
         * and then check connection for errors with conn_pool_lock unlocked. In the meantime
         * fdset's poller may call us. So just ignore such "dummy" call.
        */
        mutex_unlock(conn_pool_lock);
    }
}


static void conn_pool_put(Connection *conn, Octstr *host, int port, int ssl, Octstr *certfile, Octstr *our_host)
{
    Octstr *key;
    List *list;

    key = conn_pool_key(host, port, ssl, certfile, our_host);
    mutex_lock(conn_pool_lock);
    list = dict_get(conn_pool, key);
    if (list == NULL) {
    	list = gwlist_create();
        dict_put(conn_pool, key, list);
    }
    gwlist_append(list, conn);
    /* register connection to get server disconnect */
    conn_register_real(conn, client_fdset, check_pool_conn, key, octstr_destroy_item);
    mutex_unlock(conn_pool_lock);
}
#endif


HTTPCaller *http_caller_create(void)
{
    HTTPCaller *caller;
    
    caller = gwlist_create();
    gwlist_add_producer(caller);
    return caller;
}


void http_caller_destroy(HTTPCaller *caller)
{
    gwlist_destroy(caller, server_destroy);
}


void http_caller_signal_shutdown(HTTPCaller *caller)
{
    gwlist_remove_producer(caller);
}


static Octstr *get_redirection_location(HTTPServer *trans)
{
    if (trans->status < 0 || trans->follow_remaining <= 0)
    	return NULL;
    /* check for the redirection response codes */
    if (trans->status != HTTP_MOVED_PERMANENTLY &&
    	trans->status != HTTP_FOUND && trans->status != HTTP_SEE_OTHER &&
        trans->status != HTTP_TEMPORARY_REDIRECT)
	return NULL;
    if (trans->response == NULL)
        return NULL;
    return http_header_find_first(trans->response->headers, "Location");
}


/* 
 * Recovers a Location header value of format URI /xyz to an 
 * absoluteURI format according to the protocol rules. 
 * This simply implies that we re-create the prefixed scheme,
 * user/passwd (if any), host and port string and prepend it
 * to the location URI.
 */
static void recover_absolute_uri(HTTPServer *trans, Octstr *loc)
{
    Octstr *os;
    
    gw_assert(loc != NULL && trans != NULL);
    
    /* we'll only accept locations with a leading / */
    if (octstr_get_char(loc, 0) == '/') {
        
        /* scheme */
        os = trans->ssl ? octstr_create("https://") : 
            octstr_create("http://");
        
        /* credentials, if any */
        if (trans->username && trans->password) {
            octstr_append(os, trans->username);
            octstr_append_char(os, ':');
            octstr_append(os, trans->password);
            octstr_append_char(os, '@');
        }
        
        /* host */
        octstr_append(os, trans->host);
        
        /* port, only added if literally not default. */
        if (trans->port != 80 || trans->ssl) {
            octstr_format_append(os, ":%ld", trans->port);
        }
        
        /* prepend the created octstr to the loc, and destroy then. */
        octstr_insert(loc, os, 0);
        octstr_destroy(os);
    }
}


/*
 * Read and parse the status response line from an HTTP server.
 * Fill in trans->persistent and trans->status with the findings.
 * Return -1 for error, 1 for status line not yet available, 0 for OK.
 */
static int client_read_status(HTTPServer *trans)
{
    Octstr *line, *version;
    long space;
    int ret;

    line = conn_read_line(trans->conn);
    if (line == NULL) {
	if (conn_eof(trans->conn) || conn_error(trans->conn))
	    return -1;
    	return 1;
    }

    debug("gwlib.http", 0, "HTTP: Status line: <%s>", octstr_get_cstr(line));

    space = octstr_search_char(line, ' ', 0);
    if (space == -1)
    	goto error;
	
    version = octstr_copy(line, 0, space);
    ret = parse_http_version(version);
    octstr_destroy(version);
    if (ret == -1)
    	goto error;
    trans->persistent = ret;

    octstr_delete(line, 0, space + 1);
    space = octstr_search_char(line, ' ', 0);
    if (space == -1)
    	goto error;
    octstr_truncate(line, space);
	
    if (octstr_parse_long(&trans->status, line, 0, 10) == -1)
        goto error;

    octstr_destroy(line);
    return 0;

error:
    error(0, "HTTP: Malformed status line from HTTP server: <%s>",
	  octstr_get_cstr(line));
    octstr_destroy(line);
    return -1;
}

static int response_expectation(int method, int status)
{
    if (status == HTTP_NO_CONTENT ||
        status == HTTP_NOT_MODIFIED ||
        http_status_class(status) == HTTP_STATUS_PROVISIONAL ||
        method == HTTP_METHOD_HEAD)
	return expect_no_body;
    else
        return expect_body;
}

static void handle_transaction(Connection *conn, void *data)
{
    HTTPServer *trans;
    int ret;
    Octstr *h;
    int rc;
    
    trans = data;

    if (run_status != running) {
        conn_unregister(conn);
        return;
    }

    while (trans->state != transaction_done) {
        switch (trans->state) {
        case connecting:
            debug("gwlib.http", 0, "Get info about connecting socket");
            if (conn_get_connect_result(trans->conn) != 0) {
                debug("gwlib.http", 0, "Socket not connected");
                goto error;
            }

            if ((rc = send_request(trans)) == 0) {
                trans->state = reading_status;
            } else {
                debug("gwlib.http", 0, "Failed while sending request");
                goto error;
            }
            break;

	case reading_status:
	    ret = client_read_status(trans);
	    if (ret < 0) {
		/*
		 * Couldn't read the status from the socket. This may mean 
		 * that the socket had been closed by the server after an 
		 * idle timeout.
		 */
                debug("gwlib.http",0,"Failed while reading status");
                goto error;
	    } else if (ret == 0) {
		/* Got the status, go read headers and body next. */
		trans->state = reading_entity;
		trans->response =
		    entity_create(response_expectation(trans->method, trans->status));
	    } else
		return;
	    break;
	    
	case reading_entity:
	    ret = entity_read(trans->response, conn);
	    if (ret < 0) {
	        debug("gwlib.http",0,"Failed reading entity");
	        goto error;
	    } else if (ret == 0 && 
                    http_status_class(trans->status) == HTTP_STATUS_PROVISIONAL) {
                /* This was a provisional reply; get the real one now. */
                trans->state = reading_status;
                entity_destroy(trans->response);
                trans->response = NULL;
            } else if (ret == 0) {
                trans->state = transaction_done;
#ifdef DUMP_RESPONSE
                /* Dump the response */
                debug("gwlib.http", 0, "HTTP: Received response:");
                h = build_response(trans->response->headers, trans->response->body);
                octstr_dump(h, 0);
                octstr_destroy(h);
#endif
	    } else {
                return;
            }
            break;

        default:
            panic(0, "Internal error: Invalid HTTPServer state.");
        }
    }

    conn_unregister(trans->conn);

    /* 
     * Take care of persistent connection handling. 
     * At this point we have only obeyed if server responds in HTTP/1.0 or 1.1
     * and have assigned trans->persistent accordingly. This can be keept
     * for default usage, but if we have [Proxy-]Connection: keep-alive, then
     * we're still forcing persistancy of the connection.
     */
    h = http_header_find_first(trans->response->headers, "Connection");
    if (h != NULL && octstr_case_compare(h, octstr_imm("close")) == 0)
        trans->persistent = 0;
    if (h != NULL && octstr_case_compare(h, octstr_imm("keep-alive")) == 0)
        trans->persistent = 1;
    octstr_destroy(h);
    if (proxy_used_for_host(trans->host, trans->url)) {
        h = http_header_find_first(trans->response->headers, "Proxy-Connection");
        if (h != NULL && octstr_case_compare(h, octstr_imm("close")) == 0)
            trans->persistent = 0;
        if (h != NULL && octstr_case_compare(h, octstr_imm("keep-alive")) == 0)
            trans->persistent = 1;
        octstr_destroy(h);
    }

#ifdef USE_KEEPALIVE 
    if (trans->persistent) {
        if (proxy_used_for_host(trans->host, trans->url))
            conn_pool_put(trans->conn, proxy_hostname, proxy_port, trans->ssl, trans->certkeyfile, http_interface);
        else 
            conn_pool_put(trans->conn, trans->host, trans->port, trans->ssl, trans->certkeyfile, http_interface);
    } else
#endif
        conn_destroy(trans->conn);

    trans->conn = NULL;

    /* 
     * Check if the HTTP server told us to look somewhere else,
     * hence if we got one of the following response codes:
     *   HTTP_MOVED_PERMANENTLY (301)
     *   HTTP_FOUND (302)
     *   HTTP_SEE_OTHER (303)
     *   HTTP_TEMPORARY_REDIRECT (307)
     */
    if ((h = get_redirection_location(trans)) != NULL) {

        /* 
         * This is a redirected response, we have to follow.
         * 
         * According to HTTP/1.1 (RFC 2616), section 14.30 any Location
         * header value should be 'absoluteURI', which is defined in
         * RFC 2616, section 3.2.1 General Syntax, and specifically in
         * RFC 2396, section 3 URI Syntactic Components as
         * 
         *   absoluteURI   = scheme ":" ( hier_part | opaque_part )
         * 
         * Some HTTP servers 'interpret' a leading UDI / as that kind
         * of absoluteURI, which is not correct, following the protocol in
         * detail. But we'll try to recover from that misleaded 
         * interpreation and try to convert the partly absoluteURI to a
         * fully qualified absoluteURI.
         * 
         *   http_URL = "http:" "//" [ userid : password "@"] host 
         *      [ ":" port ] [ abs_path [ "?" query ]] 
         * 
         */
        octstr_strip_blanks(h);
        recover_absolute_uri(trans, h);
        
        /*
         * Clean up all trans stuff for the next request we do.
         */
        octstr_destroy(trans->url);
        octstr_destroy(trans->host);
        trans->port = 0;
        octstr_destroy(trans->uri);
        octstr_destroy(trans->username);
        octstr_destroy(trans->password);
        trans->host = NULL;
        trans->port = 0;
        trans->uri = NULL;
        trans->username = NULL;
        trans->password = NULL;
        trans->ssl = 0;
        trans->url = h; /* apply new absolute URL to next request */
        trans->state = request_not_sent;
        trans->status = -1;
        entity_destroy(trans->response);
        trans->response = NULL;
        --trans->follow_remaining;
        conn_destroy(trans->conn);
        trans->conn = NULL;

        /* re-inject request to the front of the queue */
        gwlist_insert(pending_requests, 0, trans);

    } else {
        /* handle this response as usual */
        gwlist_produce(trans->caller, trans);
    }
    return;

error:
    conn_unregister(trans->conn);
    conn_destroy(trans->conn);
    trans->conn = NULL;
    error(0, "Couldn't fetch <%s>", octstr_get_cstr(trans->url));
    trans->status = -1;
    gwlist_produce(trans->caller, trans);
}


/*
 * Build a complete HTTP request given the host, port, path and headers. 
 * Add Host: and Content-Length: headers (and others that may be necessary).
 * Return the request as an Octstr.
 */
static Octstr *build_request(char *method_name, Octstr *path_or_url, 
                             Octstr *host, long port, List *headers, 
                             Octstr *request_body)
{
    /* XXX headers missing */
    Octstr *request;
    int i;

    request = octstr_format("%s %S HTTP/1.1\r\n",
                            method_name, path_or_url);

    octstr_format_append(request, "Host: %S", host);
    if (port != HTTP_PORT)
        octstr_format_append(request, ":%ld", port);
    octstr_append(request, octstr_imm("\r\n"));
#ifdef USE_KEEPALIVE 
    octstr_append(request, octstr_imm("Connection: keep-alive\r\n"));
#endif

    for (i = 0; headers != NULL && i < gwlist_len(headers); ++i) {
        octstr_append(request, gwlist_get(headers, i));
        octstr_append(request, octstr_imm("\r\n"));
    }
    octstr_append(request, octstr_imm("\r\n"));

    if (request_body != NULL)
        octstr_append(request, request_body);

    return request;
}


/*
 * Re-build the HTTP response given the headers and the body.
 * Return the response as an Octstr.
 */
static Octstr *build_response(List *headers, Octstr *body)
{
    Octstr *response;
    int i;

    response = octstr_create("");

    for (i = 0; headers != NULL && i < gwlist_len(headers); ++i) {
        octstr_append(response, gwlist_get(headers, i));
        octstr_append(response, octstr_imm("\r\n"));
    }
    octstr_append(response, octstr_imm("\r\n"));

    if (body != NULL)
        octstr_append(response, body);

    return response;
}


HTTPURLParse *http_urlparse_create(void)
{
    HTTPURLParse *p;

    p = gw_malloc(sizeof(HTTPURLParse));
    p->url = NULL;
    p->scheme = NULL;
    p->host = NULL;
    p->port = 0;
    p->user = NULL;
    p->pass = NULL;
    p->path = NULL;
    p->query = NULL;
    p->fragment = NULL;
    
    return p;
}


void http_urlparse_destroy(HTTPURLParse *p)
{
    gw_assert(p != NULL);

    octstr_destroy(p->url);
    octstr_destroy(p->scheme);
    octstr_destroy(p->host);
    octstr_destroy(p->user);
    octstr_destroy(p->pass);
    octstr_destroy(p->path);
    octstr_destroy(p->query);
    octstr_destroy(p->fragment);
    gw_free(p);
}


void parse_dump(HTTPURLParse *p) 
{
    if (p == NULL)
        return;
    debug("http.parse_url",0,"Parsing URL `%s':", octstr_get_cstr(p->url));
    debug("http.parse_url",0,"  Scheme: %s", octstr_get_cstr(p->scheme));  
    debug("http.parse_url",0,"  Host: %s", octstr_get_cstr(p->host));  
    debug("http.parse_url",0,"  Port: %ld", p->port);  
    debug("http.parse_url",0,"  Username: %s", octstr_get_cstr(p->user));  
    debug("http.parse_url",0,"  Password: %s", octstr_get_cstr(p->pass));  
    debug("http.parse_url",0,"  Path: %s", octstr_get_cstr(p->path));  
    debug("http.parse_url",0,"  Query: %s", octstr_get_cstr(p->query));  
    debug("http.parse_url",0,"  Fragment: %s", octstr_get_cstr(p->fragment));  
}


/*
 * Parse the URL to get all components, which are: scheme, hostname, 
 * port, username, password, path (URI), query (the CGI parameter list), 
 * fragment (#).
 *
 * On success return the HTTPURLParse structure, otherwise NULL if the URL 
 * seems malformed.
 *
 * We assume HTTP URLs of the form specified in "3.2.2 http URL" in
 * RFC 2616:
 * 
 *  http_URL = "http:" "//" [ userid : password "@"] host [ ":" port ] [ abs_path [ "?" query ]] 
 */
HTTPURLParse *parse_url(Octstr *url)
{
    HTTPURLParse *p;
    Octstr *prefix, *prefix_https;
    long prefix_len;
    int host_len, colon, slash, at, auth_sep, query;
    host_len = colon = slash = at = auth_sep = query = 0;

    prefix = octstr_imm("http://");
    prefix_https = octstr_imm("https://");
    prefix_len = octstr_len(prefix);

    if (octstr_case_search(url, prefix, 0) != 0) {
        if (octstr_case_search(url, prefix_https, 0) == 0) {
#ifdef HAVE_LIBSSL
            debug("gwlib.http", 0, "HTTPS URL; Using SSL for the connection");
            prefix = prefix_https;
            prefix_len = octstr_len(prefix_https);	
#else
            error(0, "Attempt to use HTTPS <%s> but SSL not compiled in", 
                  octstr_get_cstr(url));
            return NULL;
#endif
        } else {
            error(0, "URL <%s> doesn't start with `%s' nor `%s'",
            octstr_get_cstr(url), octstr_get_cstr(prefix),
            octstr_get_cstr(prefix_https));
            return NULL;
        }
    }

    /* an URL should be more (at least one charset) then the scheme itself */
    if (octstr_len(url) == prefix_len) {
        error(0, "URL <%s> is malformed.", octstr_get_cstr(url));
        return NULL;
    }

    /* check if colon and slashes are within scheme */
    colon = octstr_search_char(url, ':', prefix_len);
    slash = octstr_search_char(url, '/', prefix_len);
    if (colon == prefix_len || slash == prefix_len) {
        error(0, "URL <%s> is malformed.", octstr_get_cstr(url));
        return NULL;
    }

    /* create struct and add values succesively while parsing */
    p = http_urlparse_create();
    p->url = octstr_duplicate(url);
    p->scheme = octstr_duplicate(prefix);

    /* try to parse authentication separator */
    at = octstr_search_char(url, '@', prefix_len);
    if (at != -1) {
        if ((slash == -1 || ( slash != -1 && at < slash))) {
            auth_sep = octstr_search_char(url, ':', prefix_len);
            if (auth_sep != -1 && (auth_sep < at)) {
                octstr_set_char(url, auth_sep, '@');
                colon = octstr_search_char(url, ':', prefix_len);
            }
        } else {
            at = -1;
        }
    }

    /*
     * We have to watch out here for 4 cases:
     *  a) hostname, no port or path
     *  b) hostname, port, no path
     *  c) hostname, path, no port
     *  d) hostname, port and path
     */
    
    /* we only have the hostname, no port or path. */
    if (slash == -1 && colon == -1) {
        host_len = octstr_len(url) - prefix_len;
#ifdef HAVE_LIBSSL
        p->port = (octstr_compare(p->scheme, octstr_imm("https://")) == 0) ? 
            HTTPS_PORT : HTTP_PORT;
#else
        p->port = HTTP_PORT;
#endif /* HAVE_LIBSSL */
    } 
    /* we have a port, but no path. */
    else if (slash == -1) {
        host_len = colon - prefix_len;
        if (octstr_parse_long((long*) &(p->port), url, colon + 1, 10) == -1) {
            error(0, "URL <%s> has malformed port number.",
                  octstr_get_cstr(url));
            http_urlparse_destroy(p);
            return NULL;
        }
    } 
    /* we have a path, but no port. */
    else if (colon == -1 || colon > slash) {
        host_len = slash - prefix_len;
#ifdef HAVE_LIBSSL
        p->port = (octstr_compare(p->scheme, octstr_imm("https://")) == 0) ? 
            HTTPS_PORT : HTTP_PORT;
#else
        p->port = HTTP_PORT;
#endif /* HAVE_LIBSSL */
    } 
    /* we have both, path and port. */
    else if (colon < slash) {
        host_len = colon - prefix_len;
        if (octstr_parse_long((long*) &(p->port), url, colon + 1, 10) == -1) {
            error(0, "URL <%s> has malformed port number.",
                  octstr_get_cstr(url));
            http_urlparse_destroy(p);
            return NULL;
        }
    /* none of the above, so there is something wrong here */
    } else {
        error(0, "Internal error in URL parsing logic.");
        http_urlparse_destroy(p);
        return NULL;
    }

    /* there was an authenticator separator, so try to parse 
     * the username and password credentials */
    if (at != -1) {
        int at2;

        at2 = octstr_search_char(url, '@', prefix_len);
        p->user = octstr_copy(url, prefix_len, at2 - prefix_len);
        p->pass = (at2 != at) ? octstr_copy(url, at2 + 1, at - at2 - 1) : NULL;

        if (auth_sep != -1)
            octstr_set_char(url, auth_sep, ':');
  
        host_len = host_len - at + prefix_len - 1;
        prefix_len = at + 1;
    }

    /* query (CGI vars) */
    query = octstr_search_char(url, '?', (slash == -1) ? prefix_len : slash);
    if (query != -1) {
        p->query = octstr_copy(url, query + 1, octstr_len(url));
        if (colon == -1)
            host_len = slash != -1 ? slash - prefix_len : query - prefix_len;
    }

    /* path */
    p->path = (slash == -1) ? 
        octstr_create("/") : ((query != -1) && (query > slash) ? 
            octstr_copy(url, slash, query - slash) :
            octstr_copy(url, slash, octstr_len(url) - slash)); 

    /* hostname */
    p->host = octstr_copy(url, prefix_len, host_len); 

    /* XXX add fragment too */
   
    /* dump components */
    parse_dump(p);

    return p;
}

/* copy all relevant parsed data to the server info struct */
static void parse2trans(HTTPURLParse *p, HTTPServer *t)
{
    if (p == NULL || t == NULL)
        return;

    if (p->user && !t->username)
        t->username = octstr_duplicate(p->user);
    if (p->pass && !t->password)
        t->password = octstr_duplicate(p->pass);
    if (p->host && !t->host) 
        t->host = octstr_duplicate(p->host);
    if (p->port && !t->port)
        t->port = p->port;
    if (p->path && !t->uri) {
        t->uri = octstr_duplicate(p->path);
        if (p->query) { /* add the query too */
            octstr_append_char(t->uri, '?');
            octstr_append(t->uri, p->query);
        }
    }
    t->ssl = (p->scheme && (octstr_compare(p->scheme, octstr_imm("https://")) == 0) 
              && !t->ssl) ? 1 : 0;
}

static Connection *get_connection(HTTPServer *trans) 
{
    Connection *conn = NULL;
    Octstr *host;
    HTTPURLParse *p;
    int port, ssl;
    
    /* if the parsing has not yet been done, then do it now */
    if (!trans->host && trans->port == 0 && trans->url != NULL) {
        if ((p = parse_url(trans->url)) != NULL) {
            parse2trans(p, trans);
            http_urlparse_destroy(p);
        } else {
            goto error;
        }
    }

    if (proxy_used_for_host(trans->host, trans->url)) {
        host = proxy_hostname;
        port = proxy_port;
        ssl = proxy_ssl;
    } else {
        host = trans->host;
        port = trans->port;
        ssl = trans->ssl;
    }

    conn = conn_pool_get(host, port, ssl, trans->certkeyfile,
                         http_interface);
    if (conn == NULL)
        goto error;

    return conn;

error:
    conn_destroy(conn);
    error(0, "Couldn't send request to <%s>", octstr_get_cstr(trans->url));
    return NULL;
}


/*
 * Build and send the HTTP request. Return 0 for success or -1 for error.
 */
static int send_request(HTTPServer *trans)
{
    char buf[128];    
    Octstr *request = NULL;

    if (trans->method == HTTP_METHOD_POST) {
        /* 
         * Add a Content-Length header.  Override an existing one, if
         * necessary.  We must have an accurate one in order to use the
         * connection for more than a single request.
         */
        http_header_remove_all(trans->request_headers, "Content-Length");
        snprintf(buf, sizeof(buf), "%ld", octstr_len(trans->request_body));
        http_header_add(trans->request_headers, "Content-Length", buf);
    } 
    /* 
     * ok, this has to be an GET or HEAD request method then,
     * if it contains a body, then this is not HTTP conform, so at
     * least warn the user 
     */
    else if (trans->request_body != NULL) {
        warning(0, "HTTP: GET or HEAD method request contains body:");
        octstr_dump(trans->request_body, 0);
    }

    /* 
     * we have to assume all values in trans are already set
     * by parse_url() before calling this.
     */

    if (trans->username != NULL)
        http_add_basic_auth(trans->request_headers, trans->username,
                            trans->password);

    if (proxy_used_for_host(trans->host, trans->url)) {
        proxy_add_authentication(trans->request_headers);
        request = build_request(http_method2name(trans->method),
                                trans->url, trans->host, trans->port, 
                                trans->request_headers, 
                                trans->request_body);
    } else {
        request = build_request(http_method2name(trans->method), trans->uri, 
                                trans->host, trans->port,
                                trans->request_headers,
                                trans->request_body);
    }
  
    debug("gwlib.http", 0, "HTTP: Sending request:");
    octstr_dump(request, 0);
    if (conn_write(trans->conn, request) == -1)
        goto error;

    octstr_destroy(request);

    return 0;

error:
    conn_destroy(trans->conn);
    trans->conn = NULL;
    octstr_destroy(request);
    error(0, "Couldn't send request to <%s>", octstr_get_cstr(trans->url));
    return -1;
}


/*
 * This thread starts the transaction: it connects to the server and sends
 * the request. It then sends the transaction to the read_response_thread
 * via started_requests_queue.
 */
static void write_request_thread(void *arg)
{
    HTTPServer *trans;
    int rc;

    while (run_status == running) {
        trans = gwlist_consume(pending_requests);
        if (trans == NULL)
            break;

        gw_assert(trans->state == request_not_sent);

        debug("gwlib.http", 0, "Queue contains %ld pending requests.", gwlist_len(pending_requests));

        /* 
         * get the connection to use
         * also calls parse_url() to populate the trans values
         */
        trans->conn = get_connection(trans);

        if (trans->conn == NULL)
            gwlist_produce(trans->caller, trans);
        else if (conn_is_connected(trans->conn) == 0) {
            debug("gwlib.http", 0, "Socket connected at once");

            if ((rc = send_request(trans)) == 0) {
                trans->state = reading_status;
                conn_register(trans->conn, client_fdset, handle_transaction, 
                                trans);
            } else {
                gwlist_produce(trans->caller, trans);
            }

        } else { /* Socket not connected, wait for connection */
            debug("gwlib.http", 0, "Socket connecting");
            trans->state = connecting;
            conn_register(trans->conn, client_fdset, handle_transaction, trans);
        }
    }
}


static void start_client_threads(void)
{
    if (!client_threads_are_running) {
	/* 
	 * To be really certain, we must repeat the test, but use the
	 * lock first. If the test failed, however, we _know_ we've
	 * already initialized. This strategy of double testing avoids
	 * using the lock more than a few times at startup.
	 */
	mutex_lock(client_thread_lock);
	if (!client_threads_are_running) {
	    client_fdset = fdset_create_real(http_client_timeout);
	    if (gwthread_create(write_request_thread, NULL) == -1) {
                error(0, "HTTP: Could not start client write_request thread.");
                fdset_destroy(client_fdset);
                client_threads_are_running = 0;
            } else
                client_threads_are_running = 1;
	}
	mutex_unlock(client_thread_lock);
    }
}

void http_set_interface(const Octstr *our_host)
{
    http_interface = octstr_duplicate(our_host);
}

void http_set_client_timeout(long timeout)
{
    http_client_timeout = timeout;
    if (client_fdset != NULL) {
        /* we are already initialized set timeout in fdset */
        fdset_set_timeout(client_fdset, http_client_timeout);
    }
}

void http_start_request(HTTPCaller *caller, int method, Octstr *url, List *headers,
    	    	    	Octstr *body, int follow, void *id, Octstr *certkeyfile)
{
    HTTPServer *trans;
    int follow_remaining;
    
    if (follow)
    	follow_remaining = HTTP_MAX_FOLLOW;
    else
    	follow_remaining = 0;

    trans = server_create(caller, method, url, headers, body, follow_remaining, 
			  certkeyfile);

    if (id == NULL)
        /* We don't leave this NULL so http_receive_result can use NULL
         * to signal no more requests */
        trans->request_id = http_start_request;
    else
        trans->request_id = id;
        
    gwlist_produce(pending_requests, trans);
    start_client_threads();
}


void *http_receive_result_real(HTTPCaller *caller, int *status, Octstr **final_url,
    	    	    	 List **headers, Octstr **body, int blocking)
{
    HTTPServer *trans;
    void *request_id;

    if (blocking == 0)
        trans = gwlist_extract_first(caller);
    else
        trans = gwlist_consume(caller);
    if (trans == NULL)
    	return NULL;

    request_id = trans->request_id;
    *status = trans->status;
    
    if (trans->status >= 0) {
        *final_url = trans->url;
        *headers = trans->response->headers;
        *body = trans->response->body;

        trans->url = NULL;
        trans->response->headers = NULL;
        trans->response->body = NULL;
    } else {
       *final_url = NULL;
       *headers = NULL;
       *body = NULL;
    }

    server_destroy(trans);
    return request_id;
}


int http_get_real(int method, Octstr *url, List *request_headers, Octstr **final_url,
                  List **reply_headers, Octstr **reply_body)
{
    HTTPCaller *caller;
    int status;
    void *ret;
    
    caller = http_caller_create();
    http_start_request(caller, method, url, request_headers, 
                       NULL, 1, http_get_real, NULL);
    ret = http_receive_result(caller, &status, final_url, 
    	    	    	      reply_headers, reply_body);
    http_caller_destroy(caller);
    if (ret == NULL)
    	return -1;
    return status;
}


static void client_init(void)
{
    pending_requests = gwlist_create();
    gwlist_add_producer(pending_requests);
    client_thread_lock = mutex_create();
}


static void client_shutdown(void)
{
    gwlist_remove_producer(pending_requests);
    gwthread_join_every(write_request_thread);
    client_threads_are_running = 0;
    gwlist_destroy(pending_requests, server_destroy);
    mutex_destroy(client_thread_lock);
    fdset_destroy(client_fdset);
    client_fdset = NULL;
    octstr_destroy(http_interface);
    http_interface = NULL;
}


/***********************************************************************
 * HTTP server interface.
 */


/*
 * Information about a client that has connected to the server we implement.
 */
struct HTTPClient {
    int port;
    Connection *conn;
    Octstr *ip;
    enum {
        reading_request_line,
        reading_request,
        request_is_being_handled,
        sending_reply
    } state;
    int method;  /* HTTP_METHOD_ value */
    Octstr *url;
    int use_version_1_0;
    int persistent_conn;
    unsigned long conn_time; /* store time for timeouting */
    HTTPEntity *request;
};


/* List with all active HTTPClient's */
static List *active_connections;


static HTTPClient *client_create(int port, Connection *conn, Octstr *ip)
{
    HTTPClient *p;
    
#ifdef HAVE_LIBSSL
    if (conn_get_ssl(conn)) 
        debug("gwlib.http", 0, "HTTP: Creating SSL-enabled HTTPClient for `%s', using cipher '%s'.",
    	      octstr_get_cstr(ip), SSL_get_cipher_version(conn_get_ssl(conn)));
    else
#endif    
        debug("gwlib.http", 0, "HTTP: Creating HTTPClient for `%s'.", octstr_get_cstr(ip));
    p = gw_malloc(sizeof(*p));
    p->port = port;
    p->conn = conn;
    p->ip = ip;
    p->state = reading_request_line;
    p->url = NULL;
    p->use_version_1_0 = 0;
    p->persistent_conn = 1;
    p->conn_time = time(NULL);
    p->request = NULL;
    debug("gwlib.http", 0, "HTTP: Created HTTPClient area %p.", p);
    
    /* add this client to active_connections */
    gwlist_produce(active_connections, p);
    
    return p;
}


static void client_destroy(void *client)
{
    HTTPClient *p;
    
    if (client == NULL)
        return;

    p = client;
    
    /* drop this client from active_connections list */
    gwlist_lock(active_connections);
    if (gwlist_delete_equal(active_connections, p) != 1)
        panic(0, "HTTP: Race condition in client_destroy(%p) detected!", client);
    gwlist_unlock(active_connections);
    
    debug("gwlib.http", 0, "HTTP: Destroying HTTPClient area %p.", p);
    gw_assert_allocated(p, __FILE__, __LINE__, __func__);
    debug("gwlib.http", 0, "HTTP: Destroying HTTPClient for `%s'.",
          octstr_get_cstr(p->ip));
    
    conn_destroy(p->conn);
    octstr_destroy(p->ip);
    octstr_destroy(p->url);
    entity_destroy(p->request);
    gw_free(p);
}


static void client_reset(HTTPClient *p)
{
    debug("gwlib.http", 0, "HTTP: Resetting HTTPClient for `%s'.",
    	  octstr_get_cstr(p->ip));
    p->state = reading_request_line;
    p->conn_time = time(NULL);
    gw_assert(p->request == NULL);
}


/*
 * Checks whether the client connection is meant to be persistent or not.
 * Returns 1 for true, 0 for false.
 */

static int client_is_persistent(List *headers, int use_version_1_0)
{
    Octstr *h = http_header_find_first(headers, "Connection");

    if (h == NULL) {
        return !use_version_1_0;
    } else {
        if (!use_version_1_0) {
            if (octstr_case_compare(h, octstr_imm("keep-alive")) == 0) {
                octstr_destroy(h);
                return 1;
            } else {
                octstr_destroy(h);
                return 0;
            }
	    } else if (octstr_case_compare(h, octstr_imm("close")) == 0) {
            octstr_destroy(h);
            return 0;
        }
        octstr_destroy(h);
    }

    return 1;
}


/*
 * Port specific lists of clients with requests.
 */
struct port {
    List *clients_with_requests;
    Counter *active_consumers;
};


static Mutex *port_mutex = NULL;
static Dict *port_collection = NULL;


static int port_match(void *client, void *port)
{
    return ((HTTPClient*)client)->port == *((int*)port);
}


static void port_init(void)
{
    port_mutex = mutex_create();
    port_collection = dict_create(1024, NULL);
    /* create list with all active_connections */
    active_connections = gwlist_create();
}

static void port_shutdown(void)
{
    mutex_destroy(port_mutex);
    dict_destroy(port_collection);
    /* destroy active_connections list */
    gwlist_destroy(active_connections, client_destroy);
}


static Octstr *port_key(int port)
{
    return octstr_format("%d", port);
}


static void port_add(int port)
{
    Octstr *key;
    struct port *p;

    key = port_key(port);
    mutex_lock(port_mutex);
    if ((p = dict_get(port_collection, key)) == NULL) {
        p = gw_malloc(sizeof(*p));
        p->clients_with_requests = gwlist_create();
        gwlist_add_producer(p->clients_with_requests);
        p->active_consumers = counter_create();
        dict_put(port_collection, key, p);
    } else {
        warning(0, "HTTP: port_add called for existing port (%d)", port);
    }
    mutex_unlock(port_mutex);
    octstr_destroy(key);
}


static void port_remove(int port)
{
    Octstr *key;
    struct port *p;
    List *l;
    HTTPClient *client;

    key = port_key(port);
    mutex_lock(port_mutex);
    p = dict_remove(port_collection, key);
    mutex_unlock(port_mutex);
    octstr_destroy(key);
    
    if (p == NULL) {
        error(0, "HTTP: Could not find port (%d) in port_collection.", port);
        return;
    }

    gwlist_remove_producer(p->clients_with_requests);
    while (counter_value(p->active_consumers) > 0)
       gwthread_sleep(0.1);    /* Reasonable use of busy waiting. */
    gwlist_destroy(p->clients_with_requests, client_destroy);
    counter_destroy(p->active_consumers);
    gw_free(p);

    /*
     * In order to avoid race conditions with FDSet thread, we
     * destroy Clients for this port in two steps:
     * 1) unregister from fdset with gwlist_lock held, so client_destroy
     *    cannot destroy our client that we currently use
     * 2) without gwlist_lock held destroy every client, we can do this
     *    because we only one thread that can use this client struct
     */
    gwlist_lock(active_connections);
    l = gwlist_search_all(active_connections, &port, port_match);
    while(l != NULL && (client = gwlist_extract_first(l)) != NULL)
        conn_unregister(client->conn);
    gwlist_unlock(active_connections);
    gwlist_destroy(l, NULL);
    while((client = gwlist_search(active_connections, &port, port_match)) != NULL)
        client_destroy(client);
}


static void port_put_request(HTTPClient *client)
{
    Octstr *key;
    struct port *p;

    mutex_lock(port_mutex);
    key = port_key(client->port);
    p = dict_get(port_collection, key);
    octstr_destroy(key);
    if (p == NULL) {
        /* client was too slow and we closed port already */
        mutex_unlock(port_mutex);
        client_destroy(client);
        return;
    }
    gwlist_produce(p->clients_with_requests, client);
    mutex_unlock(port_mutex);
}


static HTTPClient *port_get_request(int port)
{
    Octstr *key;
    struct port *p;
    HTTPClient *client;
    
    mutex_lock(port_mutex);
    key = port_key(port);
    p = dict_get(port_collection, key);
    octstr_destroy(key);

    if (p == NULL) {
       client = NULL;
       mutex_unlock(port_mutex);
    } else {
       counter_increase(p->active_consumers);
       mutex_unlock(port_mutex);   /* Placement of this unlock is tricky. */
       client = gwlist_consume(p->clients_with_requests);
       counter_decrease(p->active_consumers);
    }
    return client;
}


/*
 * Variables related to server side implementation.
 */
static Mutex *server_thread_lock = NULL;
static volatile sig_atomic_t server_thread_is_running = 0;
static long server_thread_id = -1;
static FDSet *server_fdset = NULL;
static List *new_server_sockets = NULL;
static List *closed_server_sockets = NULL;
static int keep_servers_open = 0;


static int parse_request_line(int *method, Octstr **url,
                              int *use_version_1_0, Octstr *line)
{
    List *words;
    Octstr *version;
    Octstr *method_str;
    int ret;

    words = octstr_split_words(line);
    if (gwlist_len(words) != 3) {
        gwlist_destroy(words, octstr_destroy_item);
        return -1;
    }

    method_str = gwlist_get(words, 0);
    *url = gwlist_get(words, 1);
    version = gwlist_get(words, 2);
    gwlist_destroy(words, NULL);

    if (octstr_compare(method_str, octstr_imm("GET")) == 0)
        *method = HTTP_METHOD_GET;
    else if (octstr_compare(method_str, octstr_imm("POST")) == 0)
        *method = HTTP_METHOD_POST;
    else if (octstr_compare(method_str, octstr_imm("HEAD")) == 0)
        *method = HTTP_METHOD_HEAD;
    else
        goto error;

    ret = parse_http_version(version);
    if (ret < 0)
        goto error;
    *use_version_1_0 = !ret;

    octstr_destroy(method_str);
    octstr_destroy(version);
    return 0;

error:
    octstr_destroy(method_str);
    octstr_destroy(*url);
    octstr_destroy(version);
    *url = NULL;
    return -1;
}


static void receive_request(Connection *conn, void *data)
{
    HTTPClient *client;
    Octstr *line;
    int ret;

    if (run_status != running) {
        conn_unregister(conn);
        return;
    }

    client = data;
    
    for (;;) {
        switch (client->state) {
            case reading_request_line:
                line = conn_read_line(conn);
                if (line == NULL) {
                    if (conn_eof(conn) || conn_error(conn))
                        goto error;
                    return;
                }
                ret = parse_request_line(&client->method, &client->url,
                                         &client->use_version_1_0, line);
                octstr_destroy(line);
                /* client sent bad request? */
                if (ret == -1) {
                    /*
                     * mark client as not persistent in order to destroy connection
                     * afterwards
                     */
                    client->persistent_conn = 0;
                    /* unregister connection, http_send_reply handle this */
                    conn_unregister(conn);
                    http_send_reply(client, HTTP_BAD_REQUEST, NULL, NULL);
                    return;
                }
                /*
                 * RFC2616 (4.3) says we should read a message body if there
                 * is one, even on GET requests.
                 */
                client->request = entity_create(expect_body_if_indicated);
                client->state = reading_request;
                break;
                
            case reading_request:
                ret = entity_read(client->request, conn);
                if (ret < 0)
                    goto error;
                if (ret == 0) {
                    client->state = request_is_being_handled;
                    conn_unregister(conn);
                    port_put_request(client);
                }
                return;
                
            case sending_reply:
                /* Implicit conn_unregister() and _destroy */
                if (conn_error(conn))
                    goto error;
                if (conn_outbuf_len(conn) > 0)
                    return;
                /* Reply has been sent completely */
                if (!client->persistent_conn) {
                    /*
                     * in order to avoid race conditions while conn will be destroyed but
                     * conn is still in use, we call conn_unregister explicit here because
                     * conn_unregister call uses locks
                     */
                    conn_unregister(conn);
                    client_destroy(client);
                    return;
                }
                /* Start reading another request */
                client_reset(client);
                break;
                
            default:
                panic(0, "Internal error: HTTPClient state is wrong.");
        }
    }
    
error:
    /*
     * in order to avoid race conditions while conn will be destroyed but
     * conn is still in use, we call conn_unregister explicit here because
     * conn_unregister call uses locks
     */
    conn_unregister(conn);
    client_destroy(client);
}


struct server {
    int fd;
    int port;
    int ssl;
};


static void server_thread(void *dummy)
{
    struct pollfd *tab = NULL;
    struct server **ports = NULL;
    int tab_size = 0, n, i, fd, ret;
    struct sockaddr_in addr;
    socklen_t addrlen;
    HTTPClient *client;
    Connection *conn;
    int *portno;

    n = 0;
    while (run_status == running && keep_servers_open) {
        while (n == 0 || gwlist_len(new_server_sockets) > 0) {
            struct server *p = gwlist_consume(new_server_sockets);
            if (p == NULL) {
                debug("gwlib.http", 0, "HTTP: No new servers. Quitting.");
                break;
            } else {
                debug ("gwlib.http", 0, "HTTP: Including port %d, fd %d for polling in server thread", p->port, p->fd);
            }
            if (tab_size <= n) {
                tab_size++;
                tab = gw_realloc(tab, tab_size * sizeof(*tab));
                ports = gw_realloc(ports, tab_size * sizeof(*ports));
                if (tab == NULL || ports == NULL) {
                    tab_size--;
                    gw_free(p);
                    continue;
                }
            }
            tab[n].fd = p->fd;
            tab[n].events = POLLIN;
            ports[n] = p;
            n++;
        }

        if ((ret = gwthread_poll(tab, n, -1.0)) == -1) {
            if (errno != EINTR) /* a signal was caught during poll() function */
                warning(errno, "HTTP: gwthread_poll failed.");
            continue;
        }

        for (i = 0; i < n; ++i) {
            if (tab[i].revents & POLLIN) {
                addrlen = sizeof(addr);
                fd = accept(tab[i].fd, (struct sockaddr *) &addr, &addrlen);
                if (fd == -1) {
                    error(errno, "HTTP: Error accepting a client.");
                } else {
                    Octstr *client_ip = host_ip(addr);
                    /*
                     * Be aware that conn_wrap_fd() will return NULL if SSL 
                     * handshake has failed, so we only client_create() if
                     * there is an conn.
                     */             
                    if ((conn = conn_wrap_fd(fd, ports[i]->ssl))) {
                        client = client_create(ports[i]->port, conn, client_ip);
                        conn_register(conn, server_fdset, receive_request, client);
                    } else {
                        error(0, "HTTP: unsuccessful SSL handshake for client `%s'",
                        octstr_get_cstr(client_ip));
                        octstr_destroy(client_ip);
                    }
                }
            }
        }

        while ((portno = gwlist_extract_first(closed_server_sockets)) != NULL) {
            for (i = 0; i < n; ++i) {
                if (ports[i]->port == *portno) {
                    (void) close(tab[i].fd);
                    tab[i].fd = -1;
                    tab[i].events = 0;
                    port_remove(ports[i]->port);
                    gw_free(ports[i]);
                    ports[i] = NULL;
                    n--;
                    
                    /* now put the last entry on this place */
                    tab[i].fd = tab[n].fd;
                    tab[i].events = tab[n].events;
                    tab[n].fd = -1;
                    tab[n].events = 0;
                    ports[i] = ports[n];
                }
            }
            gw_free(portno);
        }
    }
    
    /* make sure we close all ports */
    for (i = 0; i < n; ++i) {
        (void) close(tab[i].fd);
        port_remove(ports[i]->port);
        gw_free(ports[i]);
    }
    gw_free(tab);
    gw_free(ports);

    server_thread_id = -1;
}


static void start_server_thread(void)
{
    if (!server_thread_is_running) {
        /* 
         * To be really certain, we must repeat the test, but use the
         * lock first. If the test failed, however, we _know_ we've
         * already initialized. This strategy of double testing avoids
         * using the lock more than a few times at startup.
         */
        mutex_lock(server_thread_lock);
        if (!server_thread_is_running) {
            server_fdset = fdset_create_real(HTTP_SERVER_TIMEOUT);
            server_thread_id = gwthread_create(server_thread, NULL);
            server_thread_is_running = 1;
        }
        mutex_unlock(server_thread_lock);
    }
}


int http_open_port_if(int port, int ssl, Octstr *interface)
{
    struct server *p;

    if (ssl) 
        info(0, "HTTP: Opening SSL server at port %d.", port);
    else 
        info(0, "HTTP: Opening server at port %d.", port);
    p = gw_malloc(sizeof(*p));
    p->port = port;
    p->ssl = ssl;
    p->fd = make_server_socket(port, (interface ? octstr_get_cstr(interface) : NULL));
    if (p->fd == -1) {
        gw_free(p);
    	return -1;
    }
    
    port_add(port);
    gwlist_produce(new_server_sockets, p);
    keep_servers_open = 1;
    start_server_thread();
    gwthread_wakeup(server_thread_id);
    
    return 0;
}


int http_open_port(int port, int ssl)
{
    return http_open_port_if(port, ssl, NULL);
}


void http_close_port(int port)
{
    int *p;
    
    p = gw_malloc(sizeof(*p));
    *p = port;
    gwlist_produce(closed_server_sockets, p);
    gwthread_wakeup(server_thread_id);
}


void http_close_all_ports(void)
{
    if (server_thread_id != -1) {
        keep_servers_open = 0;
        gwthread_wakeup(server_thread_id);
        gwthread_join_every(server_thread);
        server_thread_is_running = 0;
        fdset_destroy(server_fdset);
        server_fdset = NULL;
    }
}


/*
 * Parse CGI variables from the path given in a GET. Return a list
 * of HTTPCGIvar pointers. Modify the url so that the variables are
 * removed.
 */
static List *parse_cgivars(Octstr *url)
{
    HTTPCGIVar *v;
    List *list;
    int query, et, equals;
    Octstr *arg, *args;

    query = octstr_search_char(url, '?', 0);
    if (query == -1)
        return gwlist_create();

    args = octstr_copy(url, query + 1, octstr_len(url));
    octstr_truncate(url, query);

    list = gwlist_create();

    while (octstr_len(args) > 0) {
        et = octstr_search_char(args, '&', 0);
        if (et == -1)
            et = octstr_len(args);
        arg = octstr_copy(args, 0, et);
        octstr_delete(args, 0, et + 1);

        equals = octstr_search_char(arg, '=', 0);
        if (equals == -1)
            equals = octstr_len(arg);

        v = gw_malloc(sizeof(HTTPCGIVar));
        v->name = octstr_copy(arg, 0, equals);
        v->value = octstr_copy(arg, equals + 1, octstr_len(arg));
        octstr_url_decode(v->name);
        octstr_url_decode(v->value);

        octstr_destroy(arg);

        gwlist_append(list, v);
    }
    octstr_destroy(args);

    return list;
}


HTTPClient *http_accept_request(int port, Octstr **client_ip, Octstr **url, 
    	    	    	    	List **headers, Octstr **body, 
                                List **cgivars)
{
    HTTPClient *client;
    
    do {
        client = port_get_request(port);
        if (client == NULL) {
            debug("gwlib.http", 0, "HTTP: No clients with requests, quitting.");
            return NULL;
        }
        /* check whether client connection still ok */
        conn_wait(client->conn, 0);
        if (conn_error(client->conn) || conn_eof(client->conn)) {
            client_destroy(client);
            client = NULL;
        }
    } while(client == NULL);
    
    *client_ip = octstr_duplicate(client->ip);
    *url = client->url;
    *headers = client->request->headers;
    *body = client->request->body;
    *cgivars = parse_cgivars(client->url);
    
    if (client->method != HTTP_METHOD_POST) {
        octstr_destroy(*body);
        *body = NULL;
    }
    
    client->persistent_conn = client_is_persistent(client->request->headers,
                                                   client->use_version_1_0);
    
    client->url = NULL;
    client->request->headers = NULL;
    client->request->body = NULL;
    entity_destroy(client->request);
    client->request = NULL;
    
    return client;
}

/*
 * The http_send_reply(...) uses this function to determinate the
 * reason pahrase for a status code.
 */
static const char *http_reason_phrase(int status)
{
	switch (status) {
	case HTTP_OK:
		return "OK";						/* 200 */
	case HTTP_CREATED:                   
		return "Created";					/* 201 */
	case HTTP_ACCEPTED:
		return "Accepted";					/* 202 */
	case HTTP_NO_CONTENT:
		return "No Content";				/* 204 */
	case HTTP_RESET_CONTENT: 
		return "Reset Content";				/* 205 */
	case HTTP_MOVED_PERMANENTLY:
		return "Moved Permanently"; 		/* 301 */
	case HTTP_FOUND:
		return "Found";						/* 302 */
	case HTTP_SEE_OTHER:
		return "See Other";					/* 303 */
	case HTTP_NOT_MODIFIED:
		return "Not Modified";				/* 304 */
	case HTTP_TEMPORARY_REDIRECT:
		return "Temporary Redirect";		/* 307 */
	case HTTP_BAD_REQUEST:
		return "Bad Request";				/* 400 */
	case HTTP_UNAUTHORIZED:
		return "Unauthorized";				/* 401 */
	case HTTP_FORBIDDEN:
		return "Forbidden";					/* 403 */
	case HTTP_NOT_FOUND:           	
		return "Not Found";					/* 404 */
	case HTTP_BAD_METHOD:
		return "Method Not Allowed";		/* 405 */
	case HTTP_NOT_ACCEPTABLE:
		return "Not Acceptable";			/* 406 */
	case HTTP_REQUEST_ENTITY_TOO_LARGE:
		return "Request Entity Too Large";	/* 413 */
	case HTTP_UNSUPPORTED_MEDIA_TYPE:
		return "Unsupported Media Type";	/* 415 */
	case HTTP_INTERNAL_SERVER_ERROR:
		return "Internal Server Error";		/* 500 */
	case HTTP_NOT_IMPLEMENTED:
		return "Not Implemented";			/* 501 */
	case HTTP_BAD_GATEWAY:
		return "Bad Gateway";				/* 502 */
	}
	return "Foo";
}


void http_send_reply(HTTPClient *client, int status, List *headers, 
    	    	     Octstr *body)
{
    Octstr *response;
    Octstr *date;
    long i;
    int ret;

    if (client->use_version_1_0)
    	response = octstr_format("HTTP/1.0 %d %s\r\n", status, http_reason_phrase(status));
    else
    	response = octstr_format("HTTP/1.1 %d %s\r\n", status, http_reason_phrase(status));

    /* identify ourselfs */
    octstr_format_append(response, "Server: " GW_NAME "/%s\r\n", GW_VERSION);
    
    /* let's inform the client of our time */
    date = date_format_http(time(NULL));
    octstr_format_append(response, "Date: %s\r\n", octstr_get_cstr(date));
    octstr_destroy(date);
    
    octstr_format_append(response, "Content-Length: %ld\r\n",
			 octstr_len(body));

    /* 
     * RFC2616, sec. 8.1.2.1 says that if the server chooses to close the 
     * connection, it *should* send a coresponding header
     */
    if (!client->use_version_1_0 && !client->persistent_conn)
        octstr_format_append(response, "Connection: close\r\n");

    for (i = 0; i < gwlist_len(headers); ++i)
    	octstr_format_append(response, "%S\r\n", gwlist_get(headers, i));
    octstr_format_append(response, "\r\n");
    
    if (body != NULL && client->method != HTTP_METHOD_HEAD)
    	octstr_append(response, body);
	
    ret = conn_write(client->conn, response);
    octstr_destroy(response);

    /* obey return code of conn_write() */
    /* sending response was successful */
    if (ret == 0) { 
        /* HTTP/1.0 or 1.1, hence keep-alive or keep-alive */
        if (!client->persistent_conn) {
            client_destroy(client);     
        } else {
            /* XXX mark this HTTPClient in the keep-alive cleaner thread */
            client_reset(client);
            conn_register(client->conn, server_fdset, receive_request, client);
        }
    }
    /* queued for sending, we don't want to block */
    else if (ret == 1) {    
        client->state = sending_reply;
        conn_register(client->conn, server_fdset, receive_request, client);
    }
    /* error while sending response */
    else {     
        client_destroy(client);
    }
}


void http_close_client(HTTPClient *client)
{
    client_destroy(client);
}


static void server_init(void)
{
    new_server_sockets = gwlist_create();
    gwlist_add_producer(new_server_sockets);
    closed_server_sockets = gwlist_create();
    server_thread_lock = mutex_create();
}


static void destroy_struct_server(void *p)
{
    struct server *pp;
    
    pp = p;
    (void) close(pp->fd);
    gw_free(pp);
}


static void destroy_int_pointer(void *p)
{
    (void) close(*(int *) p);
    gw_free(p);
}


static void server_shutdown(void)
{
    gwlist_remove_producer(new_server_sockets);
    if (server_thread_id != -1) {
        gwthread_wakeup(server_thread_id);
        gwthread_join_every(server_thread);
        server_thread_is_running = 0;
    }
    mutex_destroy(server_thread_lock);
    fdset_destroy(server_fdset);
    server_fdset = NULL;
    gwlist_destroy(new_server_sockets, destroy_struct_server);
    gwlist_destroy(closed_server_sockets, destroy_int_pointer);
}


/***********************************************************************
 * CGI variable manipulation.
 */


void http_destroy_cgiargs(List *args)
{
    HTTPCGIVar *v;

    gwlib_assert_init();

    if (args == NULL)
        return ;

    while ((v = gwlist_extract_first(args)) != NULL) {
        octstr_destroy(v->name);
        octstr_destroy(v->value);
        gw_free(v);
    }
    gwlist_destroy(args, NULL);
}


Octstr *http_cgi_variable(List *list, char *name)
{
    int i;
    HTTPCGIVar *v;

    gwlib_assert_init();
    gw_assert(list != NULL);
    gw_assert(name != NULL);

    for (i = 0; i < gwlist_len(list); ++i) {
        v = gwlist_get(list, i);
        if (octstr_str_compare(v->name, name) == 0)
            return v->value;
    }
    return NULL;
}


/***********************************************************************
 * Header manipulation.
 */


static int header_is_called(Octstr *header, char *name)
{
    long colon;

    colon = octstr_search_char(header, ':', 0);
    if (colon == -1)
        return 0;
    if ((long) strlen(name) != colon)
        return 0;
    return strncasecmp(octstr_get_cstr(header), name, colon) == 0;
}


List *http_create_empty_headers(void)
{
    gwlib_assert_init();
    return gwlist_create();
}


void http_destroy_headers(List *headers)
{
    gwlib_assert_init();
    gwlist_destroy(headers, octstr_destroy_item);
}


void http_header_add(List *headers, char *name, char *contents)
{
    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(name != NULL);
    gw_assert(contents != NULL);

    gwlist_append(headers, octstr_format("%s: %s", name, contents));
}


/*
 * Given an headers list and a position, returns its header name and value,
 * or (X-Unknown, header) if it doesn't exist or if it's malformed - missing 
 * ":" for example
 */
void http_header_get(List *headers, long i, Octstr **name, Octstr **value)
{
    Octstr *os;
    long colon;

    gwlib_assert_init();
    gw_assert(i >= 0);
    gw_assert(name != NULL);
    gw_assert(value != NULL);

    os = gwlist_get(headers, i);
    if (os == NULL)
        colon = -1;
    else
        colon = octstr_search_char(os, ':', 0);
    if (colon == -1) {
        error(0, "HTTP: Header does not contain a colon. BAD.");
        *name = octstr_create("X-Unknown");
        *value = octstr_duplicate(os);
    } else {
        *name = octstr_copy(os, 0, colon);
        *value = octstr_copy(os, colon + 1, octstr_len(os) - colon - 1);
        octstr_strip_blanks(*value);
    }
}

/*
 * Given an headers list and a name, returns its value or NULL if it 
 * doesn't exist
 */
Octstr *http_header_value(List *headers, Octstr *name)
{
    Octstr *value;
    long i;
    Octstr *os;
    long colon;
    Octstr *current_name;
    
    gwlib_assert_init();
    gw_assert(name);
    
    value = NULL;
    i = 0;
    while (i < gwlist_len(headers)) {
        os = gwlist_get(headers, i);
        if (os == NULL)
            colon = -1;
        else
            colon = octstr_search_char(os, ':', 0);
        if (colon == -1) {
            return NULL;      
        } else {
            current_name = octstr_copy(os, 0, colon);
        }
        if (octstr_case_compare(current_name, name) == 0) {
            value = octstr_copy(os, colon + 1, octstr_len(os) - colon - 1);
            octstr_strip_blanks(value);
            octstr_destroy(current_name);
            return value;
        }
        octstr_destroy(current_name);
        ++i;
    }
    
    return NULL;
}

List *http_header_duplicate(List *headers)
{
    List *new;
    long i, len;

    gwlib_assert_init();

    if (headers == NULL)
        return NULL;

    new = http_create_empty_headers();
    len = gwlist_len(headers);
    for (i = 0; i < len; ++i)
        gwlist_append(new, octstr_duplicate(gwlist_get(headers, i)));
    return new;
}


#define MAX_HEADER_LENGTH 256
/*
 * Aggregate header in one (or more) lines with several parameters separated
 * by commas, instead of one header per parameter
 */
void http_header_pack(List *headers)
{
    Octstr *name, *value;
    Octstr *name2, *value2;
    long i, j;

    gwlib_assert_init();
    gw_assert(headers != NULL);

    /*
     * For each header, search forward headers for similar ones and if possible, 
     * add it to current header and delete it
     */
    for(i = 0; i < gwlist_len(headers); i++) {
        http_header_get(headers, i, &name, &value);
	/* debug("http_header_pack", 0, "HTTP_HEADER_PACK: Processing header %d. [%s: %s]", 
	       i, octstr_get_cstr(name), octstr_get_cstr(value)); */

        for(j=i+1; j < gwlist_len(headers); j++) {
            http_header_get(headers, j, &name2, &value2);

            if(octstr_case_compare(name, name2) == 0) {
                if(octstr_len(value) + 2 + octstr_len(value2) > MAX_HEADER_LENGTH) {
		    octstr_destroy(name2);
		    octstr_destroy(value2);
                    break;
                } else {
		    Octstr *header;

		    /* Delete old header */
		    header = gwlist_get(headers, i);
		    octstr_destroy(header);
                    gwlist_delete(headers, i, 1);

		    /* Adds comma and new value to old header value */
                    octstr_append(value, octstr_imm(", "));
                    octstr_append(value, value2);
		    /* Creates a new header */
		    header = octstr_create("");
                    octstr_append(header, name);
                    octstr_append(header, octstr_imm(": "));
                    octstr_append(header, value);
                    gwlist_insert(headers, i, header);

		    /* Delete this header */
		    header = gwlist_get(headers, j);
		    octstr_destroy(header);
                    gwlist_delete(headers, j, 1);
                    j--;
                }
            }
	    octstr_destroy(name2);
	    octstr_destroy(value2);
        }
	octstr_destroy(name);
	octstr_destroy(value);
    }
}


void http_append_headers(List *to, List *from)
{
    Octstr *header;
    long i;

    gwlib_assert_init();
    gw_assert(to != NULL);
    gw_assert(from != NULL);

    for (i = 0; i < gwlist_len(from); ++i) {
        header = gwlist_get(from, i);
        gwlist_append(to, octstr_duplicate(header));
    }
}


void http_header_combine(List *old_headers, List *new_headers)
{
    long i;
    Octstr *name;
    Octstr *value;

    /*
     * Avoid doing this scan if old_headers is empty anyway.
     */
    if (gwlist_len(old_headers) > 0) {
        for (i = 0; i < gwlist_len(new_headers); i++) {
  	    http_header_get(new_headers, i, &name, &value);
	    http_header_remove_all(old_headers, octstr_get_cstr(name));
            octstr_destroy(name);
            octstr_destroy(value);
        }
    }

    http_append_headers(old_headers, new_headers);
}


Octstr *http_header_find_first_real(List *headers, char *name, const char *file, long line,
                                    const char *func)
{
    long i, name_len;
    Octstr *h, *value;

    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(name != NULL);

    name_len = strlen(name);

    for (i = 0; i < gwlist_len(headers); ++i) {
        h = gwlist_get(headers, i);
        if (header_is_called(h, name)) {
            value = octstr_copy_real(h, name_len + 1, octstr_len(h),
                                     file, line, func);
	    octstr_strip_blanks(value);
	    return value;
	}
    }
    return NULL;
}


List *http_header_find_all(List *headers, char *name)
{
    List *list;
    long i;
    Octstr *h;

    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(name != NULL);

    list = gwlist_create();
    for (i = 0; i < gwlist_len(headers); ++i) {
        h = gwlist_get(headers, i);
        if (header_is_called(h, name))
            gwlist_append(list, octstr_duplicate(h));
    }
    return list;
}


long http_header_remove_all(List *headers, char *name)
{
    long i;
    Octstr *h;
    long count;

    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(name != NULL);

    i = 0;
    count = 0;
    while (i < gwlist_len(headers)) {
	h = gwlist_get(headers, i);
	if (header_is_called(h, name)) {
	    gwlist_delete(headers, i, 1);
	    octstr_destroy(h);
	    count++;
	} else
	    i++;
    }

    return count;
}


void http_remove_hop_headers(List *headers)
{
    Octstr *h;
    List *connection_headers;

    gwlib_assert_init();
    gw_assert(headers != NULL);

    /*
     * The hop-by-hop headers are a standard list, plus those named
     * in the Connection header(s).
     */

    connection_headers = http_header_find_all(headers, "Connection");
    while ((h = gwlist_consume(connection_headers))) {
	List *hop_headers;
	Octstr *e;

	octstr_delete(h, 0, strlen("Connection:"));
	hop_headers = http_header_split_value(h);
	octstr_destroy(h);

	while ((e = gwlist_consume(hop_headers))) {
	    http_header_remove_all(headers, octstr_get_cstr(e));
	    octstr_destroy(e);
	}

	gwlist_destroy(hop_headers, NULL);
    }
    gwlist_destroy(connection_headers, NULL);
   
    http_header_remove_all(headers, "Connection");
    http_header_remove_all(headers, "Keep-Alive");
    http_header_remove_all(headers, "Proxy-Authenticate");
    http_header_remove_all(headers, "Proxy-Authorization");
    http_header_remove_all(headers, "TE");
    http_header_remove_all(headers, "Trailers");
    http_header_remove_all(headers, "Transfer-Encoding");
    http_header_remove_all(headers, "Upgrade");
}


void http_header_mark_transformation(List *headers,
    	    	    	    	     Octstr *new_body, Octstr *new_type)
{
    Octstr *new_length = NULL;

    /* Remove all headers that no longer apply to the new body. */
    http_header_remove_all(headers, "Content-Length");
    http_header_remove_all(headers, "Content-MD5");
    http_header_remove_all(headers, "Content-Type");

    /* Add headers that we need to describe the new body. */
    new_length = octstr_format("%ld", octstr_len(new_body));
    http_header_add(headers, "Content-Length", octstr_get_cstr(new_length));
    if(octstr_len(new_type))
	http_header_add(headers, "Content-Type", octstr_get_cstr(new_type));

    /* Perhaps we should add Warning: 214 "Transformation applied" too? */

    octstr_destroy(new_length);
}


void http_header_get_content_type(List *headers, Octstr **type,
                                  Octstr **charset)
{
    Octstr *h;
    long semicolon, equals, len;

    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(type != NULL);
    gw_assert(charset != NULL);

    h = http_header_find_first(headers, "Content-Type");
    if (h == NULL) {
        *type = octstr_create("application/octet-stream");
        *charset = octstr_create("");
    } else {
        octstr_strip_blanks(h);
        semicolon = octstr_search_char(h, ';', 0);
        if (semicolon == -1) {
            *type = h;
            *charset = octstr_create("");
        } else {
            *charset = octstr_duplicate(h);
            octstr_delete(*charset, 0, semicolon + 1);
            octstr_strip_blanks(*charset);
            equals = octstr_search_char(*charset, '=', 0);
            if (equals == -1)
                octstr_truncate(*charset, 0);
            else {
                octstr_delete(*charset, 0, equals + 1);
                if (octstr_get_char(*charset, 0) == '"')
                    octstr_delete(*charset, 0, 1);
                len = octstr_len(*charset);
                if (octstr_get_char(*charset, len - 1) == '"')
                    octstr_truncate(*charset, len - 1);
            }

            octstr_truncate(h, semicolon);
            octstr_strip_blanks(h);
            *type = h;
        }

        /* 
         * According to HTTP/1.1 (RFC 2616, section 3.7.1) we have to ensure
         * to return charset 'iso-8859-1' in case of no given encoding and
         * content-type is a 'text' subtype. 
         */
        if (octstr_len(*charset) == 0 && 
            octstr_ncompare(*type, octstr_imm("text"), 4) == 0)
            octstr_append_cstr(*charset, "ISO-8859-1");
    }
}


static void http_header_add_element(List *list, Octstr *value,
				    long start, long end)
{
    Octstr *element;

    element = octstr_copy(value, start, end - start);
    octstr_strip_blanks(element);
    if (octstr_len(element) == 0)
	octstr_destroy(element);
    else
    	gwlist_append(list, element);
}


long http_header_quoted_string_len(Octstr *header, long start)
{
    long len;
    long pos;
    int c;

    if (octstr_get_char(header, start) != '"')
	return -1;

    len = octstr_len(header);
    for (pos = start + 1; pos < len; pos++) {
	c = octstr_get_char(header, pos);
	if (c == '\\')    /* quoted-pair */
	    pos++;
	else if (c == '"')
	    return pos - start + 1;
    }

    warning(0, "Header contains unterminated quoted-string:");
    warning(0, "%s", octstr_get_cstr(header));
    return len - start;
}


List *http_header_split_value(Octstr *value)
{
    long start;  /* start of current element */
    long pos;
    long len;
    List *result;
    int c;

    /*
     * According to RFC2616 section 4.2, a field-value is either *TEXT
     * (the caller is responsible for not feeding us one of those) or
     * combinations of token, separators, and quoted-string.  We're
     * looking for commas which are separators, and have to skip
     * commas in quoted-strings.
     */
 
    result = gwlist_create();
    len = octstr_len(value);
    start = 0;
    for (pos = 0; pos < len; pos++) {
	c = octstr_get_char(value, pos);
	if (c == ',') {
	    http_header_add_element(result, value, start, pos);
	    start = pos + 1;
	} else if (c == '"') {
            pos += http_header_quoted_string_len(value, pos);
	    pos--; /* compensate for the loop's pos++ */
        }
    }
    http_header_add_element(result, value, start, len);
    return result;
}


List *http_header_split_auth_value(Octstr *value)
{
    List *result;
    Octstr *auth_scheme;
    Octstr *element;
    long i;

    /*
     * According to RFC2617, both "challenge" and "credentials"
     * consist of an auth-scheme followed by a list of auth-param.
     * Since we have to parse a list of challenges or credentials,
     * we have to look for auth-scheme to signal the start of
     * a new element.  (We can't just split on commas because
     * they are also used to separate the auth-params.)
     *
     * An auth-scheme is a single token, while an auth-param is
     * always a key=value pair.  So we can recognize an auth-scheme
     * as a token that is not followed by a '=' sign.
     *
     * Simple approach: First split at all commas, then recombine
     * the elements that belong to the same challenge or credential.
     * This is somewhat expensive but saves programmer thinking time.
     *
     * Richard Braakman
     */
 
    result = http_header_split_value(value);
    if (gwlist_len(result) == 0)
        return result;

    auth_scheme = gwlist_get(result, 0);
    i = 1;
    while (i < gwlist_len(result)) {
        int c;
        long pos;

        element = gwlist_get(result, i);

        /*
         * If the element starts with: token '='
         * then it's just an auth_param; append it to the current
         * auth_scheme.  If it starts with: token token '='
         * then it's the start of a new auth scheme.
         *
         * To make the scan easier, we consider anything other
         * than whitespace or '=' to be part of a token.
         */

        /* Skip first token */
        for (pos = 0; pos < octstr_len(element); pos++) {
            c = octstr_get_char(element, pos);
            if (isspace(c) || c == '=')
                break;
        }

        /* Skip whitespace, if any */
        while (isspace(octstr_get_char(element, pos)))
            pos++;

        if (octstr_get_char(element, pos) == '=') {
            octstr_append_char(auth_scheme, ';');
            octstr_append(auth_scheme, element);
            gwlist_delete(result, i, 1);
            octstr_destroy(element);
        } else {
            char semicolon = ';';
            octstr_insert_data(element, pos, &semicolon, 1);
            auth_scheme = element;
            i++;
        }
    }

    return result;
}


void http_header_dump(List *headers)
{
    long i;

    gwlib_assert_init();

    debug("gwlib.http", 0, "Dumping HTTP headers:");
    for (i = 0; headers != NULL && i < gwlist_len(headers); ++i)
        octstr_dump(gwlist_get(headers, i), 1);
    debug("gwlib.http", 0, "End of dump.");
}


void http_cgivar_dump(List *cgiargs)
{
    HTTPCGIVar *v;
    long i, len;

    gwlib_assert_init();

    len = gwlist_len(cgiargs);

    debug("gwlib.http", 0, "Dumping %ld cgi variables:", len);
    for (i = 0; i < len; i++) {
        v = gwlist_get(cgiargs, i);
        octstr_dump(v->name, 0);
        octstr_dump(v->value, 0);
    }
    debug("gwlib.http", 0, "End of dump.");
}


void http_cgivar_dump_into(List *cgiargs, Octstr *os)
{
    HTTPCGIVar *v;

    if (os == NULL)
        return;

    gwlib_assert_init();

    while ((v = gwlist_extract_first(cgiargs)) != NULL)
        octstr_format_append(os, "&%S=%S", v->name, v->value);
}


static int http_something_accepted(List *headers, char *header_name,
                                   char *what)
{
    int found;
    long i;
    List *accepts;
    Octstr *needle = octstr_create(what);

    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(what != NULL);

    /* return all headers with this name */
    accepts = http_header_find_all(headers, header_name);

    found = 0;
    for (i = 0; !found && i < gwlist_len(accepts); ++i) {
        Octstr *header_value = gwlist_get(accepts, i);
        if (octstr_case_search(header_value, needle, 0) != -1)
            found = 1;
    }
	octstr_destroy(needle);
    http_destroy_headers(accepts);
    return found;
}


int http_type_accepted(List *headers, char *type)
{
    return http_something_accepted(headers, "Accept", type);
}


int http_charset_accepted(List *headers, char *charset)
{
    return http_something_accepted(headers, "Accept-Charset", charset);
}


void http_add_basic_auth(List *headers, Octstr *username, Octstr *password)
{
    Octstr *os;
    
    if (password != NULL)
      os = octstr_format("%S:%S", username, password);
    else
      os = octstr_format("%S", username);
    octstr_binary_to_base64(os);
    octstr_strip_blanks(os);
    octstr_insert(os, octstr_imm("Basic "), 0);
    http_header_add(headers, "Authorization", octstr_get_cstr(os));
    octstr_destroy(os);
}


Octstr *http_get_header_parameter(Octstr *value, Octstr *parameter)
{
    long pos, len, end;
    int c, found = 0;
    Octstr *result = NULL;

    len = octstr_len(value);
    /* Find the start of the first parameter. */
    for (pos = 0; pos < len; pos++) {
        c = octstr_get_char(value, pos);
        if (c == ';')
            break;
        else if (c == '"')
            pos += http_header_quoted_string_len(value, pos) - 1;
    }

    if (pos >= len)
        return NULL;   /* no parameters */

    for (pos++; pos > 0 && pos < len && found == 0; pos++) {
        Octstr *key = NULL;
        Octstr *val = NULL;

        end = octstr_search_char(value, '=', pos);
        if (end < 0)
            end = octstr_search_char(value, ';', pos);
        if (end < 0)
            end = octstr_len(value);
        key = octstr_copy(value, pos, end - pos);
        octstr_strip_blanks(key);
        pos = end;

        if (octstr_get_char(value, pos) == '=') {
            pos++;
            while (isspace(octstr_get_char(value, pos)))
                pos++;
            if (octstr_get_char(value, pos) == '"')
                end = pos + http_header_quoted_string_len(value, pos);
            else
                end = octstr_search_char(value, ';', pos);
            if (end < 0)
                end = octstr_len(value);
            val = octstr_copy(value, pos, end - pos);
            octstr_strip_blanks(val);
            pos = end;
            pos = octstr_search_char(value, ';', pos);
        }

        /* is this the pair we look for? bail out then*/
        if (octstr_case_compare(key, parameter) == 0) {
            found++;        
            result = octstr_duplicate(val);
        }

        octstr_destroy(key);
        octstr_destroy(val);
    }

    return result;
}


/***********************************************************************
 * Module initialization and shutdown.
 */


void http_init(void)
{
    gw_assert(run_status == limbo);

#ifdef HAVE_LIBSSL
    openssl_init_locks();
    conn_init_ssl();
#endif /* HAVE_LIBSSL */
    proxy_init();
    client_init();
    conn_pool_init();
    port_init();
    server_init();
#ifdef HAVE_LIBSSL
    server_ssl_init();
#endif /* HAVE_LIBSSL */
    
    run_status = running;
}


void http_shutdown(void)
{
    gwlib_assert_init();
    gw_assert(run_status == running);

    run_status = terminating;

    conn_pool_shutdown();
    client_shutdown();
    server_shutdown();
    port_shutdown();
    proxy_shutdown();
#ifdef HAVE_LIBSSL
    openssl_shutdown_locks();
    conn_shutdown_ssl();
    server_shutdown_ssl();
#endif /* HAVE_LIBSSL */
    run_status = limbo;
}


/*
 * This function relies on the HTTP_STATUS_* enum values being
 * chosen to fit this.
 */
int http_status_class(int code)
{
    int sclass;

    if (code < 100 || code >= 600)
        sclass = HTTP_STATUS_UNKNOWN;
    else
        sclass = code - (code % 100);
    return sclass;
}


int http_name2method(Octstr *method)
{
    gw_assert(method != NULL);

    if (octstr_str_compare(method, "GET") == 0) {
        return HTTP_METHOD_GET;
    } 
    else if (octstr_str_compare(method, "POST") == 0) {
        return HTTP_METHOD_POST;
    } 
    else if (octstr_str_compare(method, "HEAD") == 0) {
        return HTTP_METHOD_HEAD;
    } 

    return -1;
}


char *http_method2name(int method)
{
    gw_assert(method > 0 && method <= 3);

    return http_methods[method-1];
}

