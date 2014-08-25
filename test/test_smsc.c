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

#include <unistd.h>
#include "gwlib/gwlib.h"
#include "gw/smsc/smpp_pdu.h"
#include <string.h>

/***********************************************************************
 * Configurable stuff.
 */


/*
 * The port at which our HTTP server emulator listens.
 */
static long http_port = 8080;


/*
 * The HTTP admin port and password for Kannel, needed to do shutdown.
 */
static long admin_port = 13000;
static char *admin_password = "bar";


/*
 * The port at which the SMPP SMS center emulator listens.
 */
static long smpp_port = 2345;


/*
 * Number of messages to use in the "Send N messages as fast as possible"
 * benchmark.
 */
static long num_messages = 1;


/***********************************************************************
 * Events and event queues.
 */

typedef List EventQueue;


typedef struct Event {
    enum event_type {
	got_smsc,
	deliver,
	deliver_ack,
	http_request,
	http_response,
	submit,
	got_enquire_link
    } type;
    long id;
    long time;
    
    Connection *conn;	    /* SMPP: Connection for response PDU */
    long sequence_number;   /* SMPP: Sequence number of resp PDU */

    /* HTTP related stuff */
    HTTPClient *client;
    Octstr *body;
} Event;


static Counter *event_id_counter = NULL;


static const char *eq_type(Event *e)
{
#define TYPE(name) case name: return #name;
    switch (e->type) {
	TYPE(got_smsc)
	TYPE(deliver)
	TYPE(deliver_ack)
	TYPE(http_request)
	TYPE(http_response)
	TYPE(submit)
	TYPE(got_enquire_link)
    }
#undef TYPE
    return "unknown";
}


static Event *eq_create_event(enum event_type type)
{
    Event *e;
    
    e = gw_malloc(sizeof(*e));
    e->type = type;
    e->time = date_universal_now();
    e->id = counter_increase(event_id_counter);
    e->conn = NULL;
    e->sequence_number = -1;
    e->client = NULL;
    e->body = NULL;
    return e;
}


static Event *eq_create_submit(Connection *conn, long sequence_number, 
    	    	    	       Octstr *body)
{
    Event *e;
    
    gw_assert(conn != NULL);
    gw_assert(sequence_number >= 0);

    e = eq_create_event(submit);
    e->conn = conn;
    e->sequence_number = sequence_number;
    e->body = octstr_duplicate(body);
    return e;
}


static Event *eq_create_http_request(HTTPClient *client, Octstr *body)
{
    Event *e;
    
    gw_assert(client != NULL);
    gw_assert(body != NULL);

    e = eq_create_event(http_request);
    e->client = client;
    e->body = octstr_duplicate(body);
    return e;
}


static void eq_destroy_event(Event *e)
{
    octstr_destroy(e->body);
    gw_free(e);
}


static EventQueue *eq_create(void)
{
    return gwlist_create();
}


static void eq_add_producer(EventQueue *eq)
{
    gwlist_add_producer(eq);
}


static void eq_remove_producer(EventQueue *eq)
{
    gwlist_remove_producer(eq);
}


static void eq_destroy(EventQueue *eq)
{
    gwlist_destroy(eq, NULL);
}


static void eq_append(EventQueue *eq, Event *e)
{
    gwlist_produce(eq, e);
}


static Event *eq_extract(EventQueue *eq)
{
    return gwlist_consume(eq);
}


static void eq_log(Event *e)
{
    info(0, "Event %ld, type %s, time %ld", e->id, eq_type(e), e->time);
}


static void eq_init(void)
{
    event_id_counter = counter_create();
}


static void eq_shutdown(void)
{
    counter_destroy(event_id_counter);
}


static long eq_round_trip_time(Event *e)
{
    long now, then;
    
    now = date_universal_now();
    if (octstr_parse_long(&then, e->body, 0, 10) == -1)
    	return 0;
    return now - then;
}


/***********************************************************************
 * SMS center emulator, declarations.
 */


struct smsc_emu_arg {
    Semaphore *sema;
    EventQueue *eq;
};


static EventQueue *undelivered_messages = NULL;


/***********************************************************************
 * SMS center emulator, SMPP internals.
 */


enum { MAX_THREADS = 2 };
enum { SMPP_MAX_QUEUE = 10 };


struct smpp_emu_arg {
    EventQueue *eq;
    Connection *conn;
    long id;
    Semaphore *ok_to_send;
    long writer_id;
    int quit;
};


static Counter *smpp_emu_counter = NULL;


static void smpp_emu_writer(void *arg)
{
    Event *e;
    SMPP_PDU *pdu;
    Octstr *os;
    struct smpp_emu_arg *p;

    p = arg;
    for (;;) {
	semaphore_down(p->ok_to_send);
	e = eq_extract(undelivered_messages);
	if (e == NULL)
	    break;
    	e->time = date_universal_now();
    	eq_log(e);
	pdu = smpp_pdu_create(deliver_sm,
			      counter_increase(smpp_emu_counter));
    	pdu->u.deliver_sm.source_addr = octstr_create("123");
    	pdu->u.deliver_sm.destination_addr = octstr_create("456");
	pdu->u.deliver_sm.short_message = octstr_format("%ld", e->time);
	os = smpp_pdu_pack(NULL, pdu);
	conn_write(p->conn, os);
	octstr_destroy(os);
	smpp_pdu_destroy(pdu);
	eq_destroy_event(e);
    }
}


static void smpp_emu_handle_pdu(struct smpp_emu_arg *p, SMPP_PDU *pdu)
{
    SMPP_PDU *resp;
    Octstr *os;
    
    resp = NULL;
    switch (pdu->type) {
    	case bind_transmitter:
	    resp = smpp_pdu_create(bind_transmitter_resp,
				   pdu->u.bind_transmitter.sequence_number);
	    break;

    	case bind_receiver:
	    resp = smpp_pdu_create(bind_receiver_resp,
				   pdu->u.bind_receiver.sequence_number);
    	    eq_append(p->eq, eq_create_event(got_smsc));
	    gw_assert(p->writer_id == -1);
	    p->writer_id = gwthread_create(smpp_emu_writer, p);
	    if (p->writer_id == -1)
	    	panic(0, "Couldn't create SMPP helper thread.");
    	    break;

    	case submit_sm:
	    eq_append(p->eq, 
	    	eq_create_submit(p->conn, pdu->u.submit_sm.sequence_number,
		    	    	 pdu->u.submit_sm.short_message));
    	    break;

    	case deliver_sm_resp:
	    eq_append(p->eq, eq_create_event(deliver_ack));
	    semaphore_up(p->ok_to_send);
	    break;

    	case enquire_link:
	    eq_append(p->eq, eq_create_event(got_enquire_link));
	    resp = smpp_pdu_create(enquire_link_resp,
	    	    	    	   pdu->u.enquire_link.sequence_number);
	    break;

    	case unbind:
	    resp = smpp_pdu_create(unbind_resp, 
	    	    	    	   pdu->u.unbind.sequence_number);
	    break;

    	default:
	    error(0, "SMPP: Unhandled PDU type %s", pdu->type_name);
	    break;
    }
		
    if (resp != NULL) {
	os = smpp_pdu_pack(NULL, resp);
	conn_write(p->conn, os);
	octstr_destroy(os);
	smpp_pdu_destroy(resp);
    }
}


static void smpp_emu_reader(void *arg)
{
    Octstr *os;
    long len;
    SMPP_PDU *pdu;
    struct smpp_emu_arg *p;

    p = arg;
    
    len = 0;
    while (!p->quit && conn_wait(p->conn, -1.0) != -1) {
    	for (;;) {
	    if (len == 0) {
		len = smpp_pdu_read_len(p->conn);
		if (len == -1) {
		    error(0, "Client sent garbage, closing connection.");
		    goto error;
		} else if (len == 0) {
		    if (conn_eof(p->conn) || conn_error(p->conn))
		    	goto error;
		    break;
		}
	    }
    
    	    gw_assert(len > 0);
	    os = smpp_pdu_read_data(p->conn, len);
	    if (os != NULL) {
    	    	len = 0;
		pdu = smpp_pdu_unpack(NULL, os);
		if (pdu == NULL) {
		    error(0, "PDU unpacking failed!");
		    octstr_dump(os, 0);
		} else {
		    smpp_emu_handle_pdu(p, pdu);
		    smpp_pdu_destroy(pdu);
		}
		octstr_destroy(os);
	    } else if (conn_eof(p->conn) || conn_error(p->conn))
	    	goto error;
	    else
		break;
	}
    }

error:
    if (p->writer_id != -1)
	gwthread_join(p->writer_id);
}


static void smpp_emu(void *arg)
{
    EventQueue *eq;
    struct smsc_emu_arg *p;
    int fd;
    int new_fd;
    Octstr *client_addr;
    long i;
    long num_threads;
    struct smpp_emu_arg *thread[MAX_THREADS];
    
    p = arg;
    eq = p->eq;
    eq_add_producer(eq);
    semaphore_up(p->sema);
    
    /*
     * Wait for SMPP clients.
     */
    fd = make_server_socket(smpp_port, NULL);
    if (fd == -1)
    	panic(0, "Couldn't create SMPP listen port.");
    
    num_threads = 0;
    for (;;) {
    	new_fd = gw_accept(fd, &client_addr);
	if (new_fd == -1)
	    break;
    	octstr_destroy(client_addr);
    	if (num_threads == MAX_THREADS) {
	    warning(0, "Too many SMPP client connections.");
	    (void) close(new_fd);
	} else {
	    thread[num_threads] = gw_malloc(sizeof(*thread[0]));
    	    thread[num_threads]->conn = conn_wrap_fd(new_fd, 0);
	    thread[num_threads]->eq = eq;
	    thread[num_threads]->quit = 0;
	    thread[num_threads]->writer_id = -1;
	    thread[num_threads]->ok_to_send = 
	    	semaphore_create(SMPP_MAX_QUEUE);
	    thread[num_threads]->id = 
	    	gwthread_create(smpp_emu_reader, thread[num_threads]);
	    if (thread[num_threads]->id == -1)
	    	panic(0, "Couldn't start SMPP subthread.");
    	    ++num_threads;
	}
    }
    
    for (i = 0; i < num_threads; ++i) {
	thread[i]->quit = 1;
    	gwthread_wakeup(thread[i]->id);
	gwthread_join(thread[i]->id);
	conn_destroy(thread[i]->conn);
	semaphore_destroy(thread[i]->ok_to_send);
	gw_free(thread[i]);
    }

    eq_remove_producer(eq);
}


/***********************************************************************
 * SMS center emulator, generic interface.
 */


static long smpp_emu_id = -1;


/*
 * Start all SMS center emulators.
 */
static void smsc_emu_create(EventQueue *eq)
{
    struct smsc_emu_arg *arg;
    
    gw_assert(smpp_emu_id == -1);

    arg = gw_malloc(sizeof(*arg));
    arg->sema = semaphore_create(0);
    arg->eq = eq;
    smpp_emu_id = gwthread_create(smpp_emu, arg);
    if (smpp_emu_id == -1)
    	panic(0, "Couldn't start SMPP emulator thread.");
    semaphore_down(arg->sema);
    semaphore_destroy(arg->sema);
    gw_free(arg);
}


static void smsc_emu_destroy(void)
{
    eq_remove_producer(undelivered_messages);
    gw_assert(smpp_emu_id != -1);
    gwthread_wakeup(smpp_emu_id);
    gwthread_join(smpp_emu_id);
}


static void smsc_emu_deliver(void)
{
    eq_append(undelivered_messages, eq_create_event(deliver));
}


static void smsc_emu_submit_ack(Event *e)
{
    SMPP_PDU *resp;
    Octstr *os;

    resp = smpp_pdu_create(submit_sm_resp, e->sequence_number);
    os = smpp_pdu_pack(NULL, resp);
    conn_write(e->conn, os);
    octstr_destroy(os);
    smpp_pdu_destroy(resp);
}


static void smsc_emu_init(void)
{
    smpp_emu_counter = counter_create();
    undelivered_messages = eq_create();
    eq_add_producer(undelivered_messages);
}


static void smsc_emu_shutdown(void)
{
    counter_destroy(smpp_emu_counter);
    eq_destroy(undelivered_messages);
}


/***********************************************************************
 * HTTP server emulator.
 */


static List *httpd_emu_headers = NULL;


struct httpd_emu_arg {
    int port;
    Semaphore *sema;
    EventQueue *eq;
};


/*
 * This is the HTTP server emulator thread.
 */
static void httpd_emu(void *arg)
{
    HTTPClient *client;
    Octstr *ip;
    Octstr *url;
    List *headers;
    Octstr *body;
    List *cgivars;
    struct httpd_emu_arg *p;
    EventQueue *eq;

    p = arg;
    eq = p->eq;
    eq_add_producer(eq);
    semaphore_up(p->sema);

    for (;;) {
	client = http_accept_request(p->port, &ip, &url, &headers, &body, 
	    	    	    	     &cgivars);
	if (client == NULL)
	    break;
    	
	eq_append(eq, eq_create_http_request(client, 
	    	    	    	    http_cgi_variable(cgivars, "arg")));
    	octstr_destroy(ip);
    	octstr_destroy(url);
	http_destroy_headers(headers);
    	octstr_destroy(body);
    	http_destroy_cgiargs(cgivars);
    }
    eq_remove_producer(eq);
    gw_free(p);
}


/*
 * Thread id for HTTP server emulator thread. It is needed for proper
 * shutdown.
 */
static long httpd_emu_tid = -1;


/*
 * Start the HTTP server emulator thread and return when it is 
 * ready to accept clients.
 */
static void httpd_emu_create(EventQueue *eq)
{
    struct httpd_emu_arg *arg;
    int ssl = 0;   /* indicate if SSL-enabled server should be used */

    if (http_open_port(http_port, ssl) == -1)
    	panic(0, "Can't open HTTP server emulator port %ld.", http_port);

    gw_assert(httpd_emu_tid == -1);
    arg = gw_malloc(sizeof(*arg));
    arg->port = http_port;
    arg->sema = semaphore_create(0);
    arg->eq = eq;
    httpd_emu_tid = gwthread_create(httpd_emu, arg);
    if (httpd_emu_tid == -1)
    	panic(0, "Can't start the HTTP server emulator thread.");
    semaphore_down(arg->sema);
    semaphore_destroy(arg->sema);
}


/*
 * Terminate the HTTP server emulator thread. Return when the thread
 * is quite dead.
 */
static void httpd_emu_destroy(void)
{
    gw_assert(httpd_emu_tid != -1);
    http_close_all_ports();
    gwthread_join(httpd_emu_tid);
    httpd_emu_tid = -1;
}


/*
 * Send a reply to an HTTP response.
 */
static void httpd_emu_reply(Event *e)
{
    http_send_reply(e->client, HTTP_OK, httpd_emu_headers, e->body);
}


static void httpd_emu_init(void)
{
    httpd_emu_headers = http_create_empty_headers();
    http_header_add(httpd_emu_headers, "Content-Type", "text/plain");
}


static void httpd_emu_shutdown(void)
{
    http_destroy_headers(httpd_emu_headers);
}


/***********************************************************************
 * Main program for N SMS messages benchmark.
 */


static void kill_kannel(void)
{
    Octstr *url;
    Octstr *final_url;
    List *req_headers;
    List *reply_headers;
    Octstr *reply_body;
    int ret;
    
    url = octstr_format("http://localhost:%ld/shutdown?password=%s",
    	    	    	admin_port, admin_password);
    req_headers = http_create_empty_headers();
    http_header_add(req_headers, "Content-Type", "text/plain");
    ret = http_get_real(HTTP_METHOD_GET, url, req_headers, &final_url, 
                        &reply_headers, &reply_body);
    if (ret != -1) {
    	octstr_destroy(final_url);
	http_destroy_headers(reply_headers);
    	octstr_destroy(reply_body);
    }
    octstr_destroy(url);
    http_destroy_headers(req_headers);
}


/*
 * This will try to have as large a sustained level of traffic as possible.
 */

enum { MAX_IN_AVERAGE = 100 };
enum { MAX_RTT = 1 };
enum { MAX_WAITING = 100 };

static void sustained_level_benchmark(void)
{
    EventQueue *eq;
    Event *e;
    long i;
    long num_deliver;
    long num_submit;
    long rtt;
    long times[MAX_IN_AVERAGE];
    long next_time;
    double time_sum;
    long num_unanswered;

    eq = eq_create();

    httpd_emu_create(eq);
    smsc_emu_create(eq);
    
    /* Wait for an SMS center client to appear. */
    while ((e = eq_extract(eq)) != NULL && e->type != got_smsc)
    	debug("test_smsc", 0, "Discarding event of type %s", eq_type(e));
    debug("test_smsc", 0, "Got event got_smsc.");
    eq_destroy_event(e);

    /* 
     * Send message when there are at most MAX_WAITING unanswered messages
     * and current average round trip time is less than MAX_RTT.
     */
    num_submit = 0;
    for (i = 0; i < MAX_IN_AVERAGE; ++i)
    	times[i] = 0;
    next_time = 0;
    time_sum = 0.0;
    num_unanswered = 0;
    num_deliver = 0;

    while (num_submit < num_messages) {
	for (;;) {
	    if (num_deliver >= num_messages || num_unanswered >= MAX_WAITING)
	    	break;
    	    if (time_sum / MAX_IN_AVERAGE >= MAX_RTT && num_unanswered > 0)
	    	break;
	    smsc_emu_deliver();
	    ++num_unanswered;
	    ++num_deliver;
	}

    	e = eq_extract(eq);
	if (e == NULL)
	    break;
	eq_log(e);

	switch (e->type) {
	case deliver_ack:
	    break;
	    
	case http_request:
	    httpd_emu_reply(e);
	    break;

	case submit:
	    rtt = eq_round_trip_time(e);
	    time_sum -= times[next_time];
	    times[next_time] = rtt;
	    time_sum += times[next_time];
	    debug("", 0, "RTT = %ld", rtt);
	    next_time = (next_time + 1) % MAX_IN_AVERAGE;
	    ++num_submit;
	    --num_unanswered;
	    smsc_emu_submit_ack(e);
	    break;
	    
    	case got_enquire_link:
	    break;

	default:
	    debug("test_smsc", 0, "Ignoring event of type %s", eq_type(e));
	    break;
	}
	
	eq_destroy_event(e);
    }

    kill_kannel();

    debug("test_smsc", 0, "Terminating benchmark.");
    smsc_emu_destroy();
    httpd_emu_destroy();
    eq_destroy(eq);
}


/*
 * This will send `num_messages' SMS messages as quickly as possible.
 */

enum { MAX_IN_QUEUE = 1000 };

static void n_messages_benchmark(void)
{
    EventQueue *eq;
    Event *e;
    long i;
    long num_submit;
    long num_in_queue;
    long num_deliver;

    eq = eq_create();

    httpd_emu_create(eq);
    smsc_emu_create(eq);
    
    /* Wait for an SMS center client to appear. */
    while ((e = eq_extract(eq)) != NULL && e->type != got_smsc)
    	debug("test_smsc", 0, "Discarding event of type %s", eq_type(e));
    debug("test_smsc", 0, "Got event got_smsc.");
    eq_destroy_event(e);

    /* Send the SMS messages, or at least fill the send queue. */
    for (i = 0; i < num_messages && i < MAX_IN_QUEUE; ++i)
    	smsc_emu_deliver();
    num_in_queue = i;
    num_deliver = i;

    /* 
     * Wait for results to be processed. When send queue is not full,
     * fill it.
     */
    num_submit = 0;
    while (num_submit < num_messages && (e = eq_extract(eq)) != NULL) {
    	while (num_deliver < num_messages && num_in_queue < MAX_IN_QUEUE) {
	    smsc_emu_deliver();
	    ++num_in_queue;
	    ++num_deliver;
	}

	eq_log(e);

	switch (e->type) {
	case deliver_ack:
	    break;
	    
	case http_request:
	    httpd_emu_reply(e);
	    break;

	case submit:
	    debug("", 0, "RTT = %ld", eq_round_trip_time(e));
	    smsc_emu_submit_ack(e);
	    ++num_submit;
	    --num_in_queue;
	    break;
	    
    	case got_enquire_link:
	    break;

	default:
	    debug("test_smsc", 0, "Ignoring event of type %s", eq_type(e));
	    break;
	}
	
	eq_destroy_event(e);
    }

    kill_kannel();

    debug("test_smsc", 0, "Terminating benchmark.");
    smsc_emu_destroy();
    httpd_emu_destroy();
    eq_destroy(eq);
}


/***********************************************************************
 * Main program.
 */


int main(int argc, char **argv)
{
    int opt;
    char *main_name;
    int i;
    static struct {
	char *name;
	void (*func)(void);
    } tab[] = {
	{ "n_messages", n_messages_benchmark },
	{ "sustained_level", sustained_level_benchmark },
    };

    gwlib_init();
    eq_init();
    httpd_emu_init();
    smsc_emu_init();

    main_name = "n_messages_benchmark";

    while ((opt = getopt(argc, argv, "m:r:")) != EOF) {
	switch (opt) {
	case 'm':
	    main_name = optarg;
	    break;
	case 'r':
	    num_messages = atoi(optarg);
	    break;
	}
    }

    for (i = 0; (size_t) i < sizeof(tab) / sizeof(tab[0]); ++i) {
	if (strcmp(main_name, tab[i].name) == 0) {
	    tab[i].func();
	    break;
	}
    }

    smsc_emu_shutdown();
    httpd_emu_shutdown();
    eq_shutdown();
    gwlib_shutdown();
    return 0;
}
