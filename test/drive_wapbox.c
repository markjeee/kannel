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

/* drive_wapbox.c - test wapbox through its bearerbox and http interfaces
 *
 * This program starts a wapbox and pretends to be both the bearer box
 * and the http server, so that it can test and benchmark the wapbox in
 * isolation.
 *
 * Richard Braakman
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "gwlib/gwlib.h"
#include "gw/msg.h"

/* These should really be in a header file */
enum wsp_types {
        Bad_PDU = -1,
        Connect_PDU = 0x01,
        ConnectReply_PDU = 0x02,
        Redirect_PDU = 0x03,
        Reply_PDU = 0x04,
        Disconnect_PDU = 0x05,
        Push_PDU = 0x06,
        ConfirmedPush_PDU = 0x07,
        Suspend_PDU = 0x08,
        Resume_PDU = 0x09,
        Get_PDU = 0x40,
        Options_PDU = 0x41,
        Head_PDU = 0x42,
        Delete_PDU = 0x43,
        Trace_PDU = 0x44,
        Post_PDU = 0x60,
        Put_PDU = 0x61
};

enum wtp_types {
	INVOKE = 1,
	RESULT = 2,
	ACK = 3
};

#define WSP_VERSION 0x10

#define TIMEOUT 10.0  /* seconds */

static long max_requests = 1;
static long max_clients = 1;
static long req_per_session = 1;
static unsigned short http_port;
static int wapbox_port = 30188;
static Octstr *http_url = NULL;

static int verbose_debug = 0;
static int user_ack = 0;

static long requests_complete = 0;
static volatile sig_atomic_t dying = 0;

enum WTP_type {
	TR_Invoke = 1,
	TR_Result = 2,
	TR_Ack = 3,
	TR_Abort = 4
};

struct client_status {
	/* True if we expect a WTP reply */
	int wtp_invoked;

	/* Transaction number for WTP level */
	int wtp_tid; /* current tid if wtp_invoked, else next tid to use */

	/* True if we're connected at the WSP level */
	/* Equal to 2 if we're trying to disconnect */
	int wsp_connected;

	/* -1 if we're not connected */
	long wsp_session_id;

	/* Number of successful page fetches this session */
	int pages_fetched;

	/* Source port to use for this client; should be unique. */
	unsigned short port;
};
typedef struct client_status Client;
	
static Client *clients;
List *ready_clients;

static unsigned long get_varint(Octstr *pdu, int pos) {
	int c;
	long result = 0;

	do {
		c = octstr_get_char(pdu, pos++);
		result = (result << 7) | (c & 0x7f);
	} while (c & 0x80);

	return c;
}

static void http_thread(void *arg) {
	HTTPClient *client;
	Octstr *ip;
	Octstr *url;
	List *headers;
	Octstr *body;
	List *cgivars;
	Octstr *reply_body = octstr_create(
		"<?xml version=\"1.0\"?>\n"
		"<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\"\n"
		" \"http://www.wapforum.org/DTD/wml_1.1.xml\">\n"
		"<wml>\n"
		"<card id=\"main\" title=\"Hello, world\" newcontext=\"true\">\n"
		"        <p>Hello, world.</p>\n"
		"</card></wml>\n");
	List *reply_headers = gwlist_create();
	int port;

    	port = *(int *) arg;
	gw_free(arg);

	gwlist_append(reply_headers,
		octstr_create("Content-Type: text/vnd.wap.wml"));

	for (;!dying;) {
		client = http_accept_request(port, &ip, &url, &headers, 
		    	    	    	     &body, &cgivars);
		if (client == NULL)
			break;
		http_send_reply(client, HTTP_OK, reply_headers, reply_body);
		http_destroy_headers(headers);
		octstr_destroy(ip);
		octstr_destroy(url);
		octstr_destroy(body);
		http_destroy_cgiargs(cgivars);
	}

	octstr_destroy(reply_body);
    	http_destroy_headers(reply_headers);
}
	

static long http_thread_id;

static int start_http_thread(void) {
	unsigned short port;
	int *port_copy;
    int ssl = 0;   /* indicate if SSL-enabled server should be used */

	for (port = 40000; port < 41000; port += 13) {
		if (http_open_port(port, ssl) != -1)
		    break;
	}
	if (port == 41000)
		panic(0, "No ports available for http server");

    	port_copy = gw_malloc(sizeof(*port_copy));
	*port_copy = port;
	http_thread_id = gwthread_create(http_thread, port_copy);
	if (http_thread_id == -1) 
		panic(0, "Cannot start http thread");
	return port;
}

static Connection *start_wapbox(void) {
	int wap_socket;
	int wapbox;

	wap_socket = make_server_socket(wapbox_port, NULL);
	if (wap_socket < 0)
		panic(0, "Couldn't make wapbox port\n");

	wapbox = accept(wap_socket, NULL, NULL);
	if (wapbox < 0)
		panic(errno, "Wapbox could not connect\n");

	close(wap_socket);

	return conn_wrap_fd(wapbox, 0);
}

static void initialize_clients(void) {
	long i;

	ready_clients = gwlist_create();

	clients = gw_malloc(max_clients * sizeof(*clients));
	for (i = 0; i < max_clients; i++) {
		clients[i].wtp_invoked = 0;
		clients[i].wtp_tid = 0;
		clients[i].wsp_connected = 0;
		clients[i].wsp_session_id = -1;
		clients[i].pages_fetched = 0;
		clients[i].port = i;
		gwlist_append(ready_clients, &clients[i]);
	}
}

static void destroy_clients(void) {
	gw_free(clients);
	gwlist_destroy(ready_clients, NULL);
}

static Client *find_client(unsigned short port) {
	/* It's easy and fast since we assign ports in linear order */
	if (port >= max_clients)
		return NULL;

	return clients + port;
}

static void client_done(Client *client) {
	requests_complete++;
	gwlist_append(ready_clients, client);
}

static void increment_tid(Client *client) {
	if (client->wtp_tid == 0x7fff) 
		client->wtp_tid = 0;
	else
		client->wtp_tid++;
}

/* Set the U/P flag on an Invoke PDU */
static void set_user_ack(Octstr *pdu) {
	octstr_set_bits(pdu, 3 * 8 + 3, 1, 1);
}

static Octstr *wtp_invoke_create(int class) {
	Octstr *pdu;
	/* data describes a TR-Invoke PDU, with GTR=1 and TTR=1 (segmentation
	 * not supported), and Transaction class 0 (which we replace below) */
	static unsigned char data[] = { 0x0e, 0x00, 0x00, 0x00 };
	gw_assert(class >= 0);
	gw_assert(class <= 2);
	pdu = octstr_create_from_data(data, sizeof(data));
	octstr_set_char(pdu, 3, class);

	if (user_ack)
		set_user_ack(pdu);

	return pdu;
}

static Octstr *wtp_ack_create(void) {
	static unsigned char data[] = { 0x18, 0x00, 0x00 };
	return octstr_create_from_data(data, sizeof(data));
}

static void add_wsp_connect(Octstr *pdu) {
	static unsigned char data[] = { Connect_PDU, WSP_VERSION, 68, 0x00,
	0x03, 0x80, 0x90, 0x00, 0x03, 0x81, 0x90, 0x00,
	0x02, 0x82, 0x30, 0x02, 0x83, 0x01, 0x02, 0x84,
	0x01, 0x28, 0x85, 0x50, 0x58, 0x2d, 0x55, 0x50,
	0x2d, 0x41, 0x47, 0x45, 0x54, 0x00, 0x51, 0x58,
	0x2d, 0x55, 0x50, 0x2d, 0x47, 0x45, 0x54, 0x4e,
	0x4f, 0x54, 0x49, 0x46, 0x59, 0x00, 0x70, 0x58,
	0x2d, 0x55, 0x50, 0x2d, 0x41, 0x50, 0x4f, 0x53,
	0x54, 0x00, 0x09, 0x86, 0x02, 0x78, 0x2d, 0x75,
	0x70, 0x2d, 0x31, 0x00 };
	octstr_append_data(pdu, data, sizeof(data));
}

static void add_wsp_get(Octstr *pdu) {
    	Octstr *urlbuf;

	octstr_append_char(pdu, Get_PDU);
	if (http_url) {
		octstr_append_uintvar(pdu, octstr_len(http_url));
		octstr_append(pdu, http_url);
	} else {
		urlbuf = octstr_format("http://localhost:%ld/hello.wml",
				       (long) http_port);
		octstr_append_uintvar(pdu, octstr_len(urlbuf));
		octstr_append(pdu, urlbuf);
		octstr_destroy(urlbuf);
	}
}

static void add_wsp_disconnect(Octstr *pdu, long session_id) {
	octstr_append_char(pdu, Disconnect_PDU);
	octstr_append_uintvar(pdu, session_id);
}

static void set_tid(Octstr *pdu, int tid) {
	int c;

	/* Tid wraps at 15 bits. */
	tid &= 0x7fff;

	c = octstr_get_char(pdu, 1);
	c = (tid >> 8) | (c & 0x80);
	octstr_set_char(pdu, 1, c);
	octstr_set_char(pdu, 2, tid & 0xff);
}

static int get_tid(Octstr *pdu) {
	return octstr_get_bits(pdu, 8, 16);
}

static int wtp_type(Octstr *pdu) {
	return octstr_get_bits(pdu, 1, 4);
}

static Msg *wdp_create(Octstr *data, Client *client) {
	Msg *msg;

	msg = msg_create(wdp_datagram);
	msg->wdp_datagram.source_address = octstr_create("127.0.0.1");
	msg->wdp_datagram.source_port = client->port;
	msg->wdp_datagram.destination_address = octstr_create("127.0.0.1");
	msg->wdp_datagram.destination_port = 9201;
	msg->wdp_datagram.user_data = octstr_duplicate(data);

	return msg;
}

static void send_pdu(Octstr *pdu, Connection *boxc, Client *client) {
	Msg *msg;
	Octstr *data;

	if (verbose_debug) {
		debug("test", 0, "Sending:");
		octstr_dump(pdu, 0);
	}

	msg = wdp_create(pdu, client);
	data = msg_pack(msg);
	conn_write_withlen(boxc, data);

	octstr_destroy(data);
	msg_destroy(msg);
}

static void send_invoke_connect(Connection *boxc, Client *client) {
	Octstr *pdu;
	
	gw_assert(client != NULL);
	gw_assert(client->wtp_invoked == 0);
	gw_assert(client->wsp_connected == 0);

	pdu = wtp_invoke_create(2);
	set_tid(pdu, client->wtp_tid);
	add_wsp_connect(pdu);

	send_pdu(pdu, boxc, client);
	octstr_destroy(pdu);

	client->wtp_invoked = 1;
}

static void send_invoke_get(Connection *boxc, Client *client) {
	Octstr *pdu;

	gw_assert(client != NULL);
	gw_assert(client->wtp_invoked == 0);
	gw_assert(client->wsp_connected == 1);

	pdu = wtp_invoke_create(2);
	set_tid(pdu, client->wtp_tid);
	add_wsp_get(pdu);

	send_pdu(pdu, boxc, client);
	octstr_destroy(pdu);

	client->wtp_invoked = 1;
}

static void record_disconnect(Client *client) {
	client->wsp_connected = 0;
	client->wsp_session_id = -1;
	client->pages_fetched = 0;
	increment_tid(client);
}

static void send_invoke_disconnect(Connection *boxc, Client *client) {
	Octstr *pdu;

	gw_assert(client != NULL);
	gw_assert(client->wtp_invoked == 0);
	gw_assert(client->wsp_connected == 1);

	/* Kannel can't handle it as class 1 yet, so send class 0 */
	pdu = wtp_invoke_create(0);
	set_tid(pdu, client->wtp_tid);
	add_wsp_disconnect(pdu, client->wsp_session_id);

	send_pdu(pdu, boxc, client);
	octstr_destroy(pdu);

	record_disconnect(client);
	client_done(client);
}

static void handle_connect_reply(Connection *boxc, Client *client, Octstr *pdu) {
	Octstr *ack;

	gw_assert(client);
	gw_assert(client->wtp_invoked);
	gw_assert(!client->wsp_connected);

	if (octstr_get_char(pdu, 3) != ConnectReply_PDU) {
		error(0, "Unexpected CONNECT reply");
		octstr_dump(pdu, 0);
		return;
	}

	ack = wtp_ack_create();
	set_tid(ack, client->wtp_tid);
	send_pdu(ack, boxc, client);
	octstr_destroy(ack);

	client->wtp_invoked = 0;
	increment_tid(client);
	client->wsp_connected = 1;
	client->wsp_session_id = get_varint(pdu, 4);

	send_invoke_get(boxc, client);
}

static void handle_get_reply(Connection *boxc, Client *client, Octstr *pdu) {
	Octstr *ack;

	gw_assert(client);
	gw_assert(client->wtp_invoked);
	gw_assert(client->wsp_connected);

	if (octstr_get_char(pdu, 3) != Reply_PDU) {
		error(0, "Unexpected GET reply");
		octstr_dump(pdu, 0);
		return;
	}

	ack = wtp_ack_create();
	set_tid(ack, client->wtp_tid);
	send_pdu(ack, boxc, client);
	octstr_destroy(ack);

	client->wtp_invoked = 0;
	increment_tid(client);
	client->pages_fetched++;

	if (client->pages_fetched == req_per_session) {
		send_invoke_disconnect(boxc, client);
	} else {
		client_done(client);
	}
}

static void handle_reply(Connection *boxc, Msg *reply) {
	Client *client;
	Octstr *wtp;
	int type;
	int dumped = 0;

	gw_assert(reply != NULL);
	gw_assert(reply->type == wdp_datagram);

	client = find_client(reply->wdp_datagram.destination_port);
	if (client == NULL)
		panic(0, "got packet for nonexisting client %ld",
			(long) reply->wdp_datagram.destination_port);

	wtp = reply->wdp_datagram.user_data;
	type = wtp_type(wtp);

	if (verbose_debug) {
		debug("test", 0, "Received:");
		octstr_dump(wtp, 0);
		dumped = 1;
	}

	if (client->wtp_invoked == 0) {
		error(0, "Got packet for client that wasn't waiting");
		if (!dumped) {
			octstr_dump(wtp, 0);
			dumped = 1;
		}
		return;
	}

	/* Server should invert the MSB of the tid in its replies */
	if (get_tid(wtp) != (client->wtp_tid ^ 0x8000)) {
		error(0, "Got packet with wrong tid %d, expected %d.",
			get_tid(wtp), client->wtp_tid ^ 0x8000);
		if (!dumped) {
			octstr_dump(wtp, 0);
			dumped = 1;
		}
		return;
	}

	/* We're going to be stupid here, and assume that replies that
	 * look vaguely like what we expect are actually what we wanted. */
	if (client->wsp_connected == 0 && type == RESULT) {
		handle_connect_reply(boxc, client, wtp);
	} else if (client->wsp_connected == 1 && type == RESULT) {
		handle_get_reply(boxc, client, wtp);
	} else if (client->wsp_connected == 2 && type == ACK) {
		record_disconnect(client);
		client_done(client);
	} else {
		error(0, "Got unexpected packet");
		if (!dumped) {
			octstr_dump(wtp, 0);
			dumped = 1;
		}
	}
}

static void start_request(Connection *boxc, Client *client) {
	gw_assert(client != NULL);
	gw_assert(client->wsp_connected != 2);
	gw_assert(client->wtp_invoked == 0);

	if (client->wsp_connected == 0) {
		send_invoke_connect(boxc, client); 
	} else {
		send_invoke_get(boxc, client);
	}
}

static long run_requests(Connection *boxc) {
	int requests_sent;
	Octstr *data;
	Msg *msg;
	int ret;

	requests_sent = 0;
	requests_complete = 0;

	while (requests_complete < max_requests) {
		data = conn_read_withlen(boxc);
		if (!data) {
			Client *client;

			if (requests_sent < max_requests
			    && (client = gwlist_extract_first(ready_clients))) {
				start_request(boxc, client);
				requests_sent++;
			}
			ret = conn_wait(boxc, TIMEOUT);
			if (ret < 0 || conn_eof(boxc))
				panic(0, "Wapbox dead.");
			if (ret == 1)
				break; /* Timed out. */
		} else {
			msg = msg_unpack(data);
			if (!msg) {
				octstr_dump(data, 0);
				panic(0, "Received bad data from wapbox.");
			}
			if (msg->type == wdp_datagram)
				handle_reply(boxc, msg);
			msg_destroy(msg);
		}
		octstr_destroy(data);
	}

	if (requests_complete < max_requests)
		info(0, "Timeout.  %ld requests unsatisfied.",
			max_requests - requests_complete);

	return requests_complete;
}

static void help(void) {
	info(0, "Usage: drive_wapbox [options...]\n");
	info(0, "  -r requests  Stop after this many; default 1.");
	info(0, "  -c clients   # of concurrent clients; default 1.");
	info(0, "  -w wapport   Port wapbox should connect to; default 30188");
	info(0, "  -u url       Use this url instead of internal http server");
	info(0, "  -g requests  Number of requests per WSP session; default 1");
	info(0, "  -U           Set the User ack flag on all WTP transactions");
}

int main(int argc, char **argv) {
	int opt;
	struct timeval start, end;
	Connection *boxc;
	long completed;
	double run_time;

	gwlib_init();

	while ((opt = getopt(argc, argv, "hv:r:c:w:du:Ug:")) != EOF) {
		switch (opt) {
		case 'v':
			log_set_output_level(atoi(optarg));
			break;

		case 'r':
			max_requests = atol(optarg);
			break;

		case 'c':
			max_clients = atol(optarg);
			break;

		case 'w':
			wapbox_port = atoi(optarg);
			break;

		case 'u':
			http_url = octstr_create(optarg);
			break;

		case 'U':
			user_ack = 1;
			break;

		case 'h':
			help();
			exit(0);

		case 'd':
			verbose_debug = 1;
			break;

		case 'g':
			req_per_session = atoi(optarg);
			break;

		case '?':
		default:
			error(0, "Invalid option %c", opt);
			help();
			panic(0, "Stopping.");
		}
	}

	if (!http_url)
		http_port = start_http_thread();
	boxc = start_wapbox();

	initialize_clients();

	if (gettimeofday(&start, NULL) < 0)
		panic(errno, "gettimeofday failed");
	completed = run_requests(boxc);
	if (gettimeofday(&end, NULL) < 0)
		panic(errno, "gettimeofday failed");

	conn_destroy(boxc);

	run_time = end.tv_sec - start.tv_sec;
	run_time += (double) (end.tv_usec - start.tv_usec) / 1000000.0;

	/* We must have timed out.  Don't count the waiting time. */
	if (completed < max_requests)
		run_time -= TIMEOUT;

	info(0, "%ld request%s in %0.1f seconds, %0.1f requests/s.",
		completed, completed != 1 ? "s" : "",
		run_time, max_requests / run_time);

	dying = 1;
	http_close_all_ports();
	if (!http_url)
		gwthread_join(http_thread_id);

	destroy_clients();
	octstr_destroy(http_url);
	
	gwlib_shutdown();

	return 0;
}
