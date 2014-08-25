/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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
 * test_http.c - a simple program to test the http library, server end
 *
 * Lars Wirzenius
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gwlib/http.h"

#define MAX_THREADS 1024

Octstr *whitelist, *blacklist;
Octstr *reply_text = NULL;

int verbose, port;
int ssl = 0;   /* indicate if SSL-enabled server should be used */
static volatile sig_atomic_t run;
static List *extra_headers = NULL;

static void split_headers(Octstr *headers, List **split)
{
    long start;
    long pos;

    *split = gwlist_create();
    start = 0;
    for (pos = 0; pos < octstr_len(headers); pos++) {
        if (octstr_get_char(headers, pos) == '\n') {
            Octstr *line;

            if (pos == start) {
                /* Skip empty lines */
                start = pos + 1;
                continue;
            }
            line = octstr_copy(headers, start, pos - start);
            start = pos + 1;
            gwlist_append(*split, line);
        }
    }
}

static void client_thread(void *arg) 
{
    HTTPClient *client;
    Octstr *body, *url, *ip;
    List *headers, *resph, *cgivars;
    HTTPCGIVar *v;
    Octstr *reply_body, *reply_type;
    unsigned long n = 0;
    int status, i;

    while (run) {
        client = http_accept_request(port, &ip, &url, &headers, &body, &cgivars);

        n++;
        if (client == NULL)
            break;

        info(0, "Request for <%s> from <%s>", 
             octstr_get_cstr(url), octstr_get_cstr(ip));
        if (verbose)
            debug("test.http", 0, "CGI vars were");

        /*
         * Don't use gwlist_extract() here, otherwise we don't have a chance
         * to re-use the cgivars later on.
         */
        for (i = 0; i < gwlist_len(cgivars); i++) {
            if ((v = gwlist_get(cgivars, i)) != NULL && verbose) {
                octstr_dump(v->name, 0);
                octstr_dump(v->value, 0);
            }
        }
    
        if (arg == NULL) {
            reply_body = octstr_duplicate(reply_text);
            reply_type = octstr_create("Content-Type: text/plain; "
                                       "charset=\"UTF-8\"");
        } else {
            reply_body = octstr_duplicate(arg);
            reply_type = octstr_create("Content-Type: text/vnd.wap.wml");
        }

        resph = gwlist_create();
        gwlist_append(resph, reply_type);

        status = HTTP_OK;

        /* check for special URIs and handle those */
        if (octstr_compare(url, octstr_imm("/quit")) == 0) {
	       run = 0;
        } else if (octstr_compare(url, octstr_imm("/whitelist")) == 0) {
	       octstr_destroy(reply_body);
            if (whitelist != NULL) {
                if (verbose) {
                    debug("test.http.server", 0, "we send a white list");
                    octstr_dump(whitelist, 0);
                }
                reply_body = octstr_duplicate(whitelist);
            } else {
	           reply_body = octstr_imm("");
	       }
        } else if (octstr_compare(url, octstr_imm("/blacklist")) == 0) {
            octstr_destroy(reply_body);
            if (blacklist != NULL) {
                if (verbose) {
                    debug("test.http.server", 0, "we send a blacklist");
                    octstr_dump(blacklist, 0);
                }
                reply_body = octstr_duplicate(blacklist);
            } else {
                reply_body = octstr_imm("");
            } 
        } else if (octstr_compare(url, octstr_imm("/save")) == 0) {
            /* safe the body into a temporary file */
            pid_t pid = getpid();
            FILE *f = fopen(octstr_get_cstr(octstr_format("/tmp/body.%ld.%ld", pid, n)), "w");
            octstr_print(f, body);
            fclose(f);
        } else if (octstr_compare(url, octstr_imm("/redirect/")) == 0) {
            /* provide us with a HTTP 302 redirection response
             * will return /redirect/<pid> for the location header 
             * and will return /redirect/ if cgivar loop is set to allow looping
             */
            Octstr *redirect_header, *scheme, *uri, *l;
            pid_t pid = getpid();

            uri = ((l = http_cgi_variable(cgivars, "loop")) != NULL) ?
                octstr_format("%s?loop=%s", octstr_get_cstr(url), 
                              octstr_get_cstr(l)) : 
                octstr_format("%s%ld", octstr_get_cstr(url), pid);

            octstr_destroy(reply_body);
            reply_body = octstr_imm("Here you got a redirection URL that you should follow.");
            scheme = ssl ? octstr_imm("https://") : octstr_imm("http://");
            redirect_header = octstr_format("Location: %s%s%s", 
                octstr_get_cstr(scheme),
                octstr_get_cstr(http_header_value(headers, octstr_imm("Host"))),
                octstr_get_cstr(uri));
            gwlist_append(resph, redirect_header);
            status = HTTP_FOUND; /* will provide 302 */
            octstr_destroy(uri);
        } else if (octstr_compare(url, octstr_imm("/mmsc")) == 0) {
            /* fake a M-Send.conf PDU which is using MMSEncapsulation as body */
            pid_t pid = getpid();
            FILE *f;
            gwlist_destroy(resph, octstr_destroy_item);
            octstr_destroy(reply_body);
            reply_type = octstr_create("Content-Type: application/vnd.wap.mms-message");
            reply_body = octstr_create("");
            octstr_append_from_hex(reply_body, 
                "8c81"              /* X-Mms-Message-Type: m-send-conf */
                "98632d3862343300"  /* X-Mms-Transaction-ID: c-8b43 */
                "8d90"              /* X-Mms-MMS-Version: 1.0 */
                "9280"              /* Response-status: Ok */
                "8b313331373939353434393639383434313731323400"
            );                      /* Message-Id: 13179954496984417124 */
            resph = gwlist_create();
            gwlist_append(resph, reply_type);
            /* safe the M-Send.req body into a temporary file */
            f = fopen(octstr_get_cstr(octstr_format("/tmp/mms-body.%ld.%ld", pid, n)), "w");
            octstr_print(f, body);
            fclose(f);
        }        
            
        if (verbose) {
            debug("test.http", 0, "request headers were");
            http_header_dump(headers);
            if (body != NULL) {
                debug("test.http", 0, "request body was");
                octstr_dump(body, 0);
            }
        }

        if (extra_headers != NULL)
        	http_header_combine(resph, extra_headers);

        /* return response to client */
        http_send_reply(client, status, resph, reply_body);

        octstr_destroy(ip);
        octstr_destroy(url);
        octstr_destroy(body);
        octstr_destroy(reply_body);
        http_destroy_cgiargs(cgivars);
        gwlist_destroy(headers, octstr_destroy_item);
        gwlist_destroy(resph, octstr_destroy_item);
    }

    octstr_destroy(whitelist);
    octstr_destroy(blacklist);
    debug("test.http", 0, "Working thread 'client_thread' terminates");
    http_close_all_ports();
}

static void help(void) {
    info(0, "Usage: test_http_server [options...]");
    info(0, "where options are:");
    info(0, "-t number");
    info(0, "    set number of working threads to use (default: 1)");
    info(0, "-v number");
    info(0, "    set log level for stderr logging (default: 0 - debug)");
    info(0, "-l logfile");
    info(0, "    log all output to a file");
    info(0, "-f file");
    info(0, "    use a specific file content for the response body");
    info(0, "-r reply_text");
    info(0, "    defines which static text to use for replies");
    info(0, "-h");
    info(0, "    provides this usage help information");
    info(0, "-q");
    info(0, "    don't be too verbose with output");
    info(0, "-p port");
    info(0, "    bind server to a specific port");
    info(0, "-s");
    info(0, "    be an SSL-enabled server");
    info(0, "-c ssl_cert");
    info(0, "    file of the SSL certificate to use");
    info(0, "-k ssl_key");
    info(0, "    file of the SSL private key to use");
    info(0, "-w white_list");
    info(0, "    file that is used for whitelist");
    info(0, "-b black_list");
    info(0, "    file that is used for blacklist");
    info(0, "-H filename");
    info(0, "    read HTTP headers from file 'filename' and add them to");
    info(0, "    the request for url 'url'");
    info(0, "specific URIs with special functions are:");
    info(0, "  /quite - shutdown the HTTP server");
    info(0, "  /whitelist - provides the -w whitelist as response");
    info(0, "  /blacklist - provides the -b blacklist as response");
    info(0, "  /save - save a HTTP POST request body to a file /tmp/body.<pid>.<n>");
    info(0, "    where <pid> is the process id and <n> is the received request number");
    info(0, "  /redirect/ - respond with HTTP 302 and the location /redirect/<pid>");
    info(0, "    where <pid> is the process id. if a cgivar loop=<something> is given");
    info(0, "    then HTTP reponses will end up in a loop.");
    info(0, "  /mmsc - fake a MMSC HTTP interface for M-Send.req PDUs send by a");
    info(0, "    mobile MMS-capable device, responds with a M-Send.conf PDU and");
    info(0, "    saves the M-Send.req body to a file /tmp/mms-body.<pid>.<n> in");
    info(0, "    MMSEncapsulation encoded binary format");
     
}

static void sigterm(int signo) {
    run = 0;
    debug("test.gwlib", 0, "Signal %d received, quitting.", signo);
}

int main(int argc, char **argv) {
    int i, opt, use_threads;
    struct sigaction act;
    char *filename;
    Octstr *log_filename;
    Octstr *file_contents;
#ifdef HAVE_LIBSSL
    Octstr *ssl_server_cert_file = NULL;
    Octstr *ssl_server_key_file = NULL;
#endif
    char *whitelist_name;
    char *blacklist_name;
    int white_asked,
        black_asked;
    long threads[MAX_THREADS];
    FILE *fp;

    gwlib_init();

    act.sa_handler = sigterm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);

    port = 8080;
    use_threads = 1;
    verbose = 1;
    run = 1;
    filename = NULL;
    log_filename = NULL;
    blacklist_name = NULL;
    whitelist_name = NULL;
    white_asked = 0;
    black_asked = 0;

    reply_text = octstr_create("Sent.");

    while ((opt = getopt(argc, argv, "hqv:p:t:f:l:sc:k:b:w:r:H:")) != EOF) {
	switch (opt) {
	case 'v':
	    log_set_output_level(atoi(optarg));
	    break;

        case 'q':
	    verbose = 0;                                           
	    break;

	case 'h':
	    help();
	    exit(0);

	case 'p':
	    port = atoi(optarg);
	    break;

	case 't':
	    use_threads = atoi(optarg);
	    if (use_threads > MAX_THREADS)
            use_threads = MAX_THREADS;
	    break;

        case 'c':
#ifdef HAVE_LIBSSL
	    octstr_destroy(ssl_server_cert_file);
	    ssl_server_cert_file = octstr_create(optarg);
#endif
        break;

        case 'k':
#ifdef HAVE_LIBSSL
	    octstr_destroy(ssl_server_key_file);
	    ssl_server_key_file = octstr_create(optarg);
#endif
        break;

	case 's':
#ifdef HAVE_LIBSSL
        ssl = 1;
#endif   
        break;

	case 'f':
	    filename = optarg;
	    break;

	case 'l':
	    octstr_destroy(log_filename);
	    log_filename = octstr_create(optarg);
	break;

    case 'w':
        whitelist_name = optarg;
        if (whitelist_name == NULL)
            whitelist_name = "";
        white_asked = 1;
	break;

    case 'b':
        blacklist_name = optarg;
        if (blacklist_name == NULL)
            blacklist_name = "";
        black_asked = 1;
	break;

	case 'r':
	    octstr_destroy(reply_text);
        reply_text = octstr_create(optarg);
        break;

	case 'H': {
		Octstr *cont;

        fp = fopen(optarg, "a");
        if (fp == NULL)
            panic(0, "Cannot open header text file %s", optarg);
        cont = octstr_read_file(optarg);
        if (cont == NULL)
            panic(0, "Cannot read header text");
        debug("", 0, "headers are");
        octstr_dump(cont, 0);
        split_headers(cont, &extra_headers);
        fclose(fp);
        octstr_destroy(cont);
        break;
	}

	case '?':
	default:
	    error(0, "Invalid option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }

    if (log_filename != NULL) {
    	log_open(octstr_get_cstr(log_filename), GW_DEBUG, GW_NON_EXCL);
	    octstr_destroy(log_filename);
    }

    if (filename == NULL)
    	file_contents = NULL;
    else
    	file_contents = octstr_read_file(filename);

    if (white_asked) {
        whitelist = octstr_read_file(whitelist_name);
        if (whitelist == NULL)
            panic(0, "Cannot read the whitelist");
    }
    
    if (black_asked) {
        blacklist = octstr_read_file(blacklist_name);
        if (blacklist == NULL)
            panic(0, "Cannot read the blacklist");
    }

#ifdef HAVE_LIBSSL
    /*
     * check if we are doing a SSL-enabled server version here
     * load the required cert and key file
     */
    if (ssl) {
        if (ssl_server_cert_file != NULL && ssl_server_key_file != NULL) {
            use_global_server_certkey_file(ssl_server_cert_file, ssl_server_key_file);
            octstr_destroy(ssl_server_cert_file);
            octstr_destroy(ssl_server_key_file);
        } else {
            panic(0, "certificate and public key need to be given!");
        }
    }
#endif
     
    if (http_open_port(port, ssl) == -1)
        panic(0, "http_open_server failed");

    /*
     * Do the real work in a separate thread so that the main
     * thread can catch signals safely.
     */
    for (i = 0; i < use_threads; ++i) 
        threads[i] = gwthread_create(client_thread, file_contents);

    /* wait for all working threads */
    for (i = 0; i < use_threads; ++i)
        gwthread_join(threads[i]);

    octstr_destroy(reply_text);
    gwlist_destroy(extra_headers, octstr_destroy_item);

    debug("test.http", 0, "Program exiting normally.");
    gwlib_shutdown();
    return 0;
}




