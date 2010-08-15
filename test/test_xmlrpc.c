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
 * test_xmlrpc.c: A simple program to test XML-RPC parsing
 *
 * Stipe Tolj <stolj@wapme.de>
 */


#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "gwlib/gwlib.h"
#include "gwlib/http.h"
#include "gwlib/xmlrpc.h"

#define MAX_THREADS 1024
#define MAX_IN_QUEUE 128

static Counter *counter = NULL; 
static long max_requests = 1;
/*static int verbose = 1;*/
static Octstr *auth_username = NULL;
static Octstr *auth_password = NULL;
static Octstr *ssl_client_certkey_file = NULL;
static Octstr *extra_headers = NULL;
static Octstr *content_file = NULL;
static Octstr *url = NULL;
static int file = 0;
static XMLRPCDocument *msg;


static void start_request(HTTPCaller *caller, List *reqh, long i)
{
    long *id;
    int ret;

    if ((i % 1000) == 0)
	   info(0, "Starting fetch %ld", i);
    id = gw_malloc(sizeof(long));
    *id = i;
                           
    /*
     * not semd the XML-RPC document contained in msg to
     * the URL 'url' using the POST method
     */
    ret = xmlrpc_send_call(msg, caller, url, reqh, id);

    debug("", 0, "Started request %ld.", *id);
    /*
    debug("", 0, "Started request %ld with url:", *id);
    octstr_url_decode(url);
    octstr_dump(url, 0);
    */
}


static int receive_reply(HTTPCaller *caller)
{
    void *id;
    int ret;
    Octstr *final_url;
    List *replyh;
    Octstr *replyb;
    Octstr *output;
    XMLRPCDocument *xrdoc;
    /*
    Octstr *type, *os_xrdoc, *os;
    Octstr *charset;
    */
    
    id = http_receive_result(caller, &ret, &final_url, &replyh, &replyb);
    octstr_destroy(final_url);
    if (id == NULL || ret == -1) {
        error(0, "http POST failed");
        gw_free(id);
        return -1;
    }
    debug("", 0, "Done with request %ld", *(long *) id);
    gw_free(id);

/*
    http_header_get_content_type(replyh, &type, &charset);
    debug("", 0, "Content-type is <%s>, charset is <%s>",
          octstr_get_cstr(type), octstr_get_cstr(charset));
    octstr_destroy(type);
    octstr_destroy(charset);
    if (verbose)
        debug("", 0, "Reply headers:");
    while ((os = gwlist_extract_first(replyh)) != NULL) {
        if (verbose)
            octstr_dump(os, 1);
        octstr_destroy(os);
    }
    if (verbose) {
        debug("", 0, "Reply body:");
        octstr_dump(replyb, 1);
    }
*/
    xrdoc = xmlrpc_parse_response(replyb);
    debug("", 0, "Parsed xmlrpc");
    
    if ((xmlrpc_parse_status(xrdoc) != XMLRPC_COMPILE_OK) && 
        ((output = xmlrpc_parse_error(xrdoc)) != NULL)) {
        /* parse failure */
        error(0, "%s", octstr_get_cstr(output));
        octstr_destroy(output);
        return -1;
    } else { 
        /*parse proper xmlrpc */
        if (xmlrpc_is_fault(xrdoc)) {
            Octstr *fstring = xmlrpc_get_faultstring(xrdoc);
            debug("xr", 0, "Got fault response with code:%ld and description: %s",
                           xmlrpc_get_faultcode(xrdoc),
                           octstr_get_cstr(fstring));
            octstr_destroy(fstring);
            http_destroy_headers(replyh);
            octstr_destroy(replyb);
            return -1;
        } 
        /*
        os_xrdoc = xmlrpc_print_response(xrdoc);
        debug("xr", 0, "XMLRPC response:");
        octstr_dump(os_xrdoc, 0);
        */
    }
    http_destroy_headers(replyh);
    octstr_destroy(replyb);
    return 0;
}


static void client_thread(void *arg) 
{
    List *reqh;
    long i, succeeded, failed;
    HTTPCaller *caller;
    char buf[1024];
    long in_queue;

    caller = arg;
    succeeded = 0;
    failed = 0;
    reqh = gwlist_create();

    sprintf(buf, "%ld", (long) gwthread_self());
    http_header_add(reqh, "X-Thread", buf);
    if (auth_username != NULL && auth_password != NULL)
        http_add_basic_auth(reqh, auth_username, auth_password);

    in_queue = 0;
    
    for (;;) {
        while (in_queue < MAX_IN_QUEUE) {
            i = counter_increase(counter);
            if (i >= max_requests)
                goto receive_rest;
            start_request(caller, reqh, i);
#if 1
            gwthread_sleep(0.1);
#endif
            ++in_queue;
        }
        while (in_queue >= MAX_IN_QUEUE) {
            if (receive_reply(caller) == -1)
                ++failed;
            else
                ++succeeded;
            --in_queue;
        }
    }
    
receive_rest:
    while (in_queue > 0) {
        if (receive_reply(caller) == -1)
            ++failed;
        else
            ++succeeded;
        --in_queue;
    }

    http_caller_destroy(caller);
    info(0, "This thread: %ld succeeded, %ld failed.", succeeded, failed);
}


static void help(void) 
{
    info(0, "Usage: test_xmlrpc [options] xml_source");
    info(0, "where options are:");
    info(0, "-u URL");
    info(0, "    send XML-RPC source as POST HTTP request to URL");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-q");
    info(0, "    don't print the body or headers of the HTTP response");
    info(0, "-r number");
    info(0, "    make `number' requests, repeating URLs as necessary");
    info(0, "-p domain.name");
    info(0, "    use `domain.name' as a proxy");
    info(0, "-P portnumber");
    info(0, "    connect to proxy at port `portnumber'");
    info(0, "-S");
    info(0, "    use HTTPS scheme to access SSL-enabled proxy server");
    info(0, "-e domain1:domain2:...");
    info(0, "    set exception list for proxy use");
    info(0, "-s");
    info(0, "    use HTTPS scheme to access SSL-enabled HTTP server");
    info(0, "-c ssl_client_cert_key_file");
    info(0, "    use this file as the SSL certificate and key file");
}


int main(int argc, char **argv)
{
    int i, opt, num_threads;
    Octstr *proxy;
    List *exceptions;
    long proxy_port;
    int proxy_ssl = 0;
    Octstr *proxy_username;
    Octstr *proxy_password;
    Octstr *exceptions_regex;
    char *p;
    long threads[MAX_THREADS];
    time_t start, end;
    double run_time;
    FILE *fp;
    Octstr *output, *xml_doc; 
    int ssl = 0;
    
    gwlib_init();

    proxy = NULL;
    proxy_port = -1;
    exceptions = gwlist_create();
    proxy_username = NULL;
    proxy_password = NULL;
    exceptions_regex = NULL;
    num_threads = 0;
    file = 0;
    fp = NULL;
    
    while ((opt = getopt(argc, argv, "hvr:t:p:u:P:Se:a:sc:")) != EOF) {
        switch (opt) {
            case 'h':
                help();
                exit(1);
                break;

            case 'v':
                log_set_output_level(atoi(optarg));
                break;

            case 'r':
                max_requests = atoi(optarg);
                break;
	
            case 't':
                num_threads = atoi(optarg);
                if (num_threads > MAX_THREADS)
                    num_threads = MAX_THREADS;
                break;

            case 'p':
                proxy = octstr_create(optarg);
                break;

            case 'u':
                url = octstr_create(optarg);
                break;
	
            case 'P':
                proxy_port = atoi(optarg);
                break;
                
        	case 'S':
                proxy_ssl = 1;
                break;
	
            case 'e':
                p = strtok(optarg, ":");
                while (p != NULL) {
                    gwlist_append(exceptions, octstr_create(p));
                    p = strtok(NULL, ":");
                }
                break;

            case 'E':
                exceptions_regex = octstr_create(optarg);
                break;
	
            case 'a':
                p = strtok(optarg, ":");
                if (p != NULL) {
                    auth_username = octstr_create(p);
                    p = strtok(NULL, "");
                    if (p != NULL)
                        auth_password = octstr_create(p);
                }
                break;

            case 's':
                ssl = 1;
                break;

            case 'c':
	           octstr_destroy(ssl_client_certkey_file);
	           ssl_client_certkey_file = octstr_create(optarg);
                break;

            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping");
                break;
        }
    }

    if (optind >= argc) {
        error(0, "Missing arguments");
        help();
        panic(0, "Stopping");
    }

#ifdef HAVE_LIBSSL
    /*
     * check if we are doing a SSL-enabled client version here
     * load the required cert and key file
     */
    if (ssl || proxy_ssl) {
        if (ssl_client_certkey_file != NULL) {
            use_global_client_certkey_file(ssl_client_certkey_file);
        } else {
            panic(0, "client certkey file need to be given!");
        }
    }
#endif

    if (proxy != NULL && proxy_port > 0) {
        http_use_proxy(proxy, proxy_port, proxy_ssl, exceptions,
                       proxy_username, proxy_password,
                       exceptions_regex);
    }
    octstr_destroy(proxy);
    octstr_destroy(proxy_username);
    octstr_destroy(proxy_password);
    octstr_destroy(exceptions_regex);
    gwlist_destroy(exceptions, octstr_destroy_item);
    
    counter = counter_create();

    xml_doc = octstr_read_file(argv[optind]);
    if (xml_doc == NULL)
        panic(0, "Cannot read the XML document");

    /*
     * parse the XML source
     */
    msg = xmlrpc_parse_call(xml_doc);

    if ((xmlrpc_parse_status(msg) != XMLRPC_COMPILE_OK) && 
        ((output = xmlrpc_parse_error(msg)) != NULL)) {
        /* parse failure */
        error(0, "%s", octstr_get_cstr(output));
        octstr_destroy(output);
    }

    /*
     * if no POST is desired then dump the re-formated XML
     */
    if (url != NULL) {
        
        time(&start);
        if (num_threads == 0)
	       client_thread(http_caller_create());
        else {
            for (i = 0; i < num_threads; ++i)
                threads[i] = gwthread_create(client_thread, http_caller_create());
            for (i = 0; i < num_threads; ++i)
                gwthread_join(threads[i]);
        }
        time(&end);
    
    
        run_time = difftime(end, start);
        info(0, "%ld requests in %f seconds, %f requests/s.",
	         max_requests, run_time, max_requests / run_time);
        
        octstr_destroy(url);

    } else {
        output = xmlrpc_print_call(msg);
        if (output != NULL) {
            octstr_print(stderr, output);
            octstr_destroy(output);
        }
    }

    counter_destroy(counter);
    octstr_destroy(auth_username);
    octstr_destroy(auth_password);
    octstr_destroy(ssl_client_certkey_file);
    octstr_destroy(extra_headers);
    octstr_destroy(content_file);


    xmlrpc_destroy_call(msg);
    octstr_destroy(xml_doc);

    gwlib_shutdown();

    return 0;
}

