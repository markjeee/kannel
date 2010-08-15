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

/*
 * http.h - HTTP protocol implementation
 *
 * This header file defines the interface to the HTTP implementation
 * in Kannel.
 * 
 * We implement both the client and the server side of the protocol.
 * We don't implement HTTP completely - only those parts that Kannel needs.
 * You may or may not be able to use this code for other projects. It has
 * not been a goal, but it might be possible, though you do need other
 * parts of Kannel's gwlib as well.
 * 
 * Initialization
 * ==============
 *
 * The library MUST be initialized by a call to http_init. Failure to
 * initialize means the library WILL NOT work. Note that the library
 * can't initialize itself implicitly, because it cannot reliably
 * create a mutex to protect the initialization. Therefore, it is the
 * caller's responsibility to call http_init exactly once (no more, no
 * less) at the beginning of the process, before any other thread makes
 * any calls to the library.
 * 
 * Client functionality
 * ====================
 * 
 * The library will invisibly keep the connections to HTTP servers open,
 * so that it is possible to make several HTTP requests over a single
 * TCP connection. This makes it much more efficient in high-load situations.
 * On the other hand, if one request takes long, the library will still
 * use several connections to the same server anyway.
 * 
 * The library user can specify an HTTP proxy to be used. There can be only
 * one proxy at a time, but it is possible to specify a list of hosts for
 * which the proxy is not used. The proxy can be changed at run time.
 * 
 * Server functionality
 * ====================
 * 
 * The library allows the implementation of an HTTP server by having
 * functions to specify which ports should be open, and receiving requests
 * from those ports.
 * 
 * Header manipulation
 * ===================
 * 
 * The library additionally has some functions for manipulating lists of
 * headers. These take a `List' (see gwlib/list.h) of Octstr's. The list
 * represents a list of headers in an HTTP request or reply. The functions
 * manipulate the list by adding and removing headers by name. It is a
 * very bad idea to manipulate the list without using the header
 * manipulation functions, however.
 *
 * Basic Authentication
 * ====================
 *
 * Basic Authentication is the standard way for a client to authenticate
 * itself to a server. It is done by adding an "Authorization" header
 * to the request. The interface in this header therefore doesn't mention
 * it, but the client and the server can do it by checking the headers
 * using the generic functions provided.
 *
 * Acknowledgements
 * ================
 *
 * Design: Lars Wirzenius, Richard Braakman
 * Implementation: Lars Wirzenius
 */


#ifndef HTTP_H
#define HTTP_H

#include "gwlib/list.h"
#include "gwlib/octstr.h"


/*
 * Well-known return values from HTTP servers. This is a complete
 * list as defined by the W3C in RFC 2616, section 10.4.3.
 */

enum {
    HTTP_CONTINUE                           = 100,
    HTTP_SWITCHING_PROTOCOLS                = 101,
    HTTP_OK                                 = 200,
    HTTP_CREATED                            = 201,
    HTTP_ACCEPTED                           = 202,
    HTTP_NON_AUTHORATIVE_INFORMATION        = 203,
    HTTP_NO_CONTENT                         = 204,
    HTTP_RESET_CONTENT                      = 205,
    HTTP_PARTIAL_CONTENT                    = 206,
    HTTP_MULTIPLE_CHOICES                   = 300,
    HTTP_MOVED_PERMANENTLY                  = 301,
    HTTP_FOUND                              = 302,
    HTTP_SEE_OTHER                          = 303,
    HTTP_NOT_MODIFIED                       = 304,
    HTTP_USE_PROXY                          = 305,
    /* HTTP 306 is not used and reserved */
    HTTP_TEMPORARY_REDIRECT                 = 307,
    HTTP_BAD_REQUEST                        = 400,
    HTTP_UNAUTHORIZED                       = 401,
    HTTP_PAYMENT_REQUIRED                   = 402,
    HTTP_FORBIDDEN                          = 403,
    HTTP_NOT_FOUND                          = 404,
    HTTP_BAD_METHOD                         = 405,
    HTTP_NOT_ACCEPTABLE                     = 406,
    HTTP_PROXY_AUTHENTICATION_REQUIRED      = 407,
    HTTP_REQUEST_TIMEOUT                    = 408,
    HTTP_CONFLICT                           = 409,
    HTTP_GONE                               = 410,
    HTTP_LENGTH_REQUIRED                    = 411,
    HTTP_PRECONDITION_FAILED                = 412,
    HTTP_REQUEST_ENTITY_TOO_LARGE           = 413,
    HTTP_REQUEST_URI_TOO_LARGE              = 414,
    HTTP_UNSUPPORTED_MEDIA_TYPE             = 415,
    HTTP_REQUESTED_RANGE_NOT_SATISFIABLE    = 416,
    HTTP_EXPECTATION_FAILED                 = 417,
    HTTP_INTERNAL_SERVER_ERROR              = 500,
    HTTP_NOT_IMPLEMENTED                    = 501,
    HTTP_BAD_GATEWAY                        = 502,
    HTTP_SERVICE_UNAVAILABLE                = 503,
    HTTP_GATEWAY_TIMEOUT                    = 504,
    HTTP_HTTP_VERSION_NOT_SUPPORTED         = 505
};

/*
 * Groupings of the status codes listed above.
 * See the http_status_class() function.
 */

enum {
	HTTP_STATUS_PROVISIONAL = 100,
	HTTP_STATUS_SUCCESSFUL = 200,
	HTTP_STATUS_REDIRECTION = 300,
	HTTP_STATUS_CLIENT_ERROR = 400,
	HTTP_STATUS_SERVER_ERROR = 500,
	HTTP_STATUS_UNKNOWN = 0
};


/*
 * Methods supported by this HTTP library.  Currently not public but
 * probably should be.
 */
enum {
	HTTP_METHOD_GET = 1,
	HTTP_METHOD_POST = 2,
	HTTP_METHOD_HEAD = 3
};

/*
 * A structure describing a CGI-BIN argument/variable.
 */
typedef struct {
	Octstr *name;
	Octstr *value;
} HTTPCGIVar;


/*
 * Initialization function. This MUST be called before any other function
 * declared in this header file.
 */
void http_init(void);


/*
 * Shutdown function. This MUST be called when no other function
 * declared in this header file will be called anymore.
 */
void http_shutdown(void);


/***********************************************************************
 * HTTP URL parsing.
 */

/*
 * A structure describing a full URL with it's components.
 */
typedef struct {
    Octstr *url;
    Octstr *scheme;
    Octstr *host;
    unsigned long port;
    Octstr *user;
    Octstr *pass;
    Octstr *path;
    Octstr *query;
    Octstr *fragment;
} HTTPURLParse;

/* 
 * Create an URL parsing structure.
 */
HTTPURLParse *http_urlparse_create(void);

/* 
 * Destroy an URL parsing structure. 
 */
void http_urlparse_destroy(HTTPURLParse *p);

/* 
 * Parse the given URL and return a parsed struct containing all
 * parsed components. If parsing failed, returns NULL.
 */
HTTPURLParse *parse_url(Octstr *url);

/* 
 * Dump the parsed struct to debug log level. 
 */
void parse_dump(HTTPURLParse *p);


/***********************************************************************
 * HTTP proxy interface.
 */


/*
 * Functions for controlling proxy use. http_use_proxy sets the proxy to
 * use; if another proxy was already in use, it is closed and forgotten
 * about as soon as all existing requests via it have been served.
 *
 * http_close_proxy closes the current proxy connection, after any
 * pending requests have been served.
 */
void http_use_proxy(Octstr *hostname, int port, int ssl, List *exceptions,
    	    	    Octstr *username, Octstr *password, Octstr *exceptions_regex);
void http_close_proxy(void);


/***********************************************************************
 * HTTP client interface.
 */

/*
 * Define interface from which all http requestes will be served
 */
void http_set_interface(const Octstr *our_host);

/**
 * Define timeout in seconds for which HTTP clint will wait for
 * response. Set -1 to disable timeouts.
 */
void http_set_client_timeout(long timeout);

/*
 * Functions for doing a GET request. The difference is that _real follows
 * redirections, plain http_get does not. Return value is the status
 * code of the request as a numeric value, or -1 if a response from the
 * server was not received. If return value is not -1, reply_headers and
 * reply_body are set and MUST be destroyed by caller.
 *
 * XXX these are going away in the future
 */
int http_get_real(int method, Octstr *url, List *request_headers, 
                  Octstr **final_url, List **reply_headers, 
                  Octstr **reply_body);

/*
 * An identification for a caller of HTTP. This is used with
 * http_start_request, and http_receive_result to route results to the right
 * callers.
 *
 * Implementation note: We use a List as the type so that we can use
 * that list for communicating the results. This makes it unnecessary
 * to map the caller identifier to a List internally in the HTTP module.
 */
typedef List HTTPCaller;


/*
 * Create an HTTP caller identifier.
 */
HTTPCaller *http_caller_create(void);


/*
 * Destroy an HTTP caller identifier. Those that aren't destroyed
 * explicitly are destroyed by http_shutdown.
 */
void http_caller_destroy(HTTPCaller *caller);


/*
 * Signal to a caller (presumably waiting in http_receive_result) that
 * we're entering shutdown phase. This will make http_receive_result
 * no longer block if the queue is empty.
 */
void http_caller_signal_shutdown(HTTPCaller *caller);


/*
 * Start an HTTP request. It will be completed in the background, and
 * the result will eventually be received by http_receive_result.
 * http_receive_result will return the id parameter passed to this function,
 * and the caller can use this to keep track of which request and which
 * response belong together. If id is NULL, it is changed to a non-null
 * value (NULL replies from http_receive_result are reserved for cases
 * when it doesn't return a reply).
 *
 * If `body' is NULL, it is a GET request, otherwise as POST request.
 * If `follow' is true, HTTP redirections are followed, otherwise not.
 *
 * 'certkeyfile' defines a filename where openssl looks for a PEM-encoded
 * certificate and a private key, if openssl is compiled in and an https 
 * URL is used. It can be NULL, in which case none is used and thus there 
 * is no ssl authentication, unless you have set a global one with
 * use_global_certkey_file() from conn.c.
 */
void http_start_request(HTTPCaller *caller, int method, Octstr *url, 
                        List *headers, Octstr *body, int follow, void *id, 
    	    	    	Octstr *certkeyfile); 


/*
 * Get the result of a GET or a POST request. Returns either the id pointer
 * (the one passed to http_start request if non-NULL) or NULL if
 * http_caller_signal_shutdown has been called and there are no queued results.
 */
void *http_receive_result_real(HTTPCaller *caller, int *status, Octstr **final_url,
    	    	    	 List **headers, Octstr **body, int blocking);

/* old compatibility mode, always blocking */
#define http_receive_result(caller, status, final_url, headers, body) \
    http_receive_result_real(caller, status, final_url, headers, body, 1)

/***********************************************************************
 * HTTP server interface.
 */


/*
 * Data structure representing an HTTP client that has connected to
 * the server we implement. It is used to route responses correctly.
 */
typedef struct HTTPClient HTTPClient;


/*
 * Open an HTTP server at a given port. Return -1 for errors (invalid
 * port number, etc), 0 for OK. This will also start a background thread
 * to listen for connections to that port and read the requests from them.
 * Second boolean variable indicates if the HTTP server should be started 
 * for SSL-enabled connections.
 */
int http_open_port(int port, int ssl);


/*
 * Same as above, but bind to a specific interface.
 */
int http_open_port_if(int port, int ssl, Octstr *interface);


/*
 * Accept a request from a client to the specified open port. Return NULL
 * if the port is closed, otherwise a pointer to a client descriptor.
 * Return the IP number (as a string) and other related information about
 * the request via arguments if function return value is non-NULL. The
 * caller is responsible for destroying the values returned via arguments,
 * the caller descriptor is destroyed by http_send_reply.
 *
 * The requests are actually read by a background thread handled by the
 * HTTP implementation, so it is not necessary by the HTTP user to have
 * many threads to be fast. The HTTP user should use a single thread,
 * unless requests can block.
 */
HTTPClient *http_accept_request(int port, Octstr **client_ip, 
    	    	    	    	Octstr **url, List **headers, Octstr **body,
				List **cgivars);


/*
 * Send a reply to a previously accepted request. The caller is responsible
 * for destroying the headers and body after the call to http_send_reply
 * finishes. This allows using them in several replies in an efficient way.
 */
void http_send_reply(HTTPClient *client, int status, List *headers, 
    	    	     Octstr *body);


/*
 * Don't send a reply to a previously accepted request, but only close
 * the connection to the client. This can be used to reject requests from
 * clients that are not authorized to access us.
 */
void http_close_client(HTTPClient *client);


/*
 * Close a currently open port and stop corresponding background threads.
 */
void http_close_port(int port);


/*
 * Close all currently open ports and stop background threads.
 */
void http_close_all_ports(void);


/*
 * Destroy a list of HTTPCGIVar objects.
 */
void http_destroy_cgiargs(List *args);


/*
 * Return reference to CGI argument 'name', or NULL if not matching.
 */
Octstr *http_cgi_variable(List *list, char *name);


/***********************************************************************
 * HTTP header interface.
 */


/*
 * Functions for manipulating a list of headers. You can use a list of
 * headers returned by one of the functions above, or create an empty
 * list with http_create_empty_headers. Use http_destroy_headers to
 * destroy a list of headers (not just the list, but the headers
 * themselves). You can also use http_parse_header_string to create a list:
 * it takes a textual representation of headers as an Octstr and returns
 * the corresponding List. http_generate_header_string goes the other
 * way.
 *
 * Once you have a list of headers, you can use http_header_add and the
 * other functions to manipulate it.
 */
List *http_create_empty_headers(void);
void http_destroy_headers(List *headers);
void http_header_add(List *headers, char *name, char *contents);
void http_header_get(List *headers, long i, Octstr **name, Octstr **value);
List *http_header_duplicate(List *headers);
void http_header_pack(List *headers);
void http_append_headers(List *to, List *from);
Octstr *http_header_value(List *headers, Octstr *header);


/*
 * Append all headers from new_headers to old_headers.  Headers from
 * new_headers _replace_ the ones in old_headers if they have the same
 * name.  For example, if you have:
 * old_headers
 *    Accept: text/html
 *    Accept: text/plain
 *    Accept: image/jpeg
 *    Accept-Language: en
 * new_headers
 *    Accept: text/html
 *    Accept: text/plain
 * then after the operation, old_headers will have
 *    Accept-Language: en
 *    Accept: text/html
 *    Accept: text/plain
 */
void http_header_combine(List *old_headers, List *new_headers);

/*
 * Return the length of the quoted-string (a HTTP field element)
 * starting at position pos in the header.  Return -1 if there
 * is no quoted-string at that position.
 */
long http_header_quoted_string_len(Octstr *header, long pos);


/*
 * Take the value part of a header that has a format that allows
 * multiple comma-separated elements, and split it into a list of
 * those elements.  Note that the function may have surprising
 * results for values of headers that are not in this format.
 */
List *http_header_split_value(Octstr *value);


/*
 * The same as http_header_split_value, except that it splits 
 * headers containing 'credentials' or 'challenge' lists, which
 * have a slightly different format.  It also normalizes the list
 * elements, so that parameters are introduced with ';'.
 */
List *http_header_split_auth_value(Octstr *value);


/*
 * Remove all headers with name 'name' from the list.  Return the
 * number of headers removed.
 */
long http_header_remove_all(List *headers, char *name);


/*
 * Remove the hop-by-hop headers from a header list.  These are the
 * headers that describe a specific connection, not anything about
 * the content.  RFC2616 section 13.5.1 defines these.
 */
void http_remove_hop_headers(List *headers);


/*
 * Update the headers to reflect that a transformation has been
 * applied to the entity body.
 */
void http_header_mark_transformation(List *headers, Octstr *new_body, 
    	    	    	    	     Octstr *new_type);


/*
 * Find the first header called `name' in `headers'. Returns its contents
 * as a new Octet string, which the caller must free. Return NULL for
 * not found.
 */
Octstr *http_header_find_first_real(List *headers, char *name, 
                                    const char *file, long line, const char *func);
#define http_header_find_first(headers, name) \
    gw_claim_area(http_header_find_first_real((headers), (name), __FILE__, __LINE__, __func__))
List *http_header_find_all(List *headers, char *name);


/*
 * Find the Content-Type header and returns the type and charset.
 */
void http_header_get_content_type(List *headers, Octstr **type, 
	Octstr **charset);


/*
 * Check if a specific mime-type can be handled by a client. This is
 * indicated via 'Accept' headers. Returns 1 if the mime-type is acceptable,
 * otherwise 0.
 */
int http_type_accepted(List *headers, char *type);


/*
 * Dump the contents of a header list with debug.
 */
void http_header_dump(List *headers);

/* 
 * Ditto with cgi variables. Do not panic, when an empty are found from the 
 * list.
 */
void http_cgivar_dump(List *cgiargs);

/*
 * As above function except that dump appended to Octstr.
 */
void http_cgivar_dump_into(List *cgiargs, Octstr *os);

/*
 * Check if the passed charset is in the 'Accept-Charset' header list
 * alues of the client. Returns 1 if the charset is acceptable, otherwise 0.
 */
int http_charset_accepted(List *headers, char *charset);


/*
 * Add Basic Authentication headers headers.
 */
void http_add_basic_auth(List *headers, Octstr *username, Octstr *password);


/* 
 * Many HTTP field elements can take parameters in a standardized
 * form: parameters appear after the main value, each is introduced
 * by a semicolon (;), and consists of a key=value pair or just
 * a key, where the key is a token and the value is either a token
 * or a quoted-string.
 * The main value itself is a series of tokens, separators, and
 * quoted-strings.
 *
 * This function will take such a field element, and look for the 
 * value of a specific key, which is then returned. If the key
 * is not found within the header value NULL is returned.
 * 
 * BEWARE: value is *only* the header value, not the whole header with
 * field name.
 * 
 * Example:
 *    * assume to have "Content-Type: application/xml; charset=UTF-8" 
 *    * within List *headers 
 *   value = http_header_value(headers, octstr_imm("Content-Type"))
 *   val = http_get_header_parameter(value, octstr_imm("charset"));
 * will return "UTF-8" to lvalue.
 */
Octstr *http_get_header_parameter(Octstr *value, Octstr *parameter);


/*
 * Return the general class of a status code.  For example, all
 * 2xx codes are HTTP_STATUS_SUCCESSFUL.  See the list at the top
 * of this file.
 */
int http_status_class(int code);


/*
 * Return the HTTP_METHOD_xxx enum code for a Octstr containing 
 * the HTTP method name.
 */
int http_name2method(Octstr *method);


/*
 * Return the char containing the HTTP method name.
 */
char *http_method2name(int method);

#endif
