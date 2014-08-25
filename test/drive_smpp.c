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
 * drive_smpp.c - SMPP server for testing purposes
 *
 * Lars Wirzenius
 */


#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "gwlib/gwlib.h"
#include "gw/smsc/smpp_pdu.h"
#include "gw/msg.h"


static int quitting = 0;
static Octstr *smsc_system_id;
static Octstr *smsc_source_addr;
static Counter *message_id_counter;
static Octstr *bearerbox_host;
static int port_for_smsbox;
static Counter *num_to_esme;
static long max_to_esme;
static Counter *num_from_bearerbox;
static Counter *num_to_bearerbox;
static Counter *num_from_esme;
static time_t start_time = (time_t) -1;
static time_t first_to_esme = (time_t) -1;
static time_t last_to_esme = (time_t) -1;
static time_t last_from_esme = (time_t) -1;
static time_t first_from_bb = (time_t) -1;
static time_t last_to_bb = (time_t) -1;
static long enquire_interval = 1; /* Measured in messages, not time. */


static void quit(void)
{
    quitting = 1;
    gwthread_wakeup_all();
}


typedef struct {
    Connection *conn;
    int transmitter;
    int receiver;
    long version;
} ESME;


static ESME *esme_create(Connection *conn)
{
    ESME *esme;
    
    esme = gw_malloc(sizeof(*esme));
    esme->conn = conn;
    esme->transmitter = 0;
    esme->receiver = 0;
    esme->version = 0;
    return esme;
}


static void esme_destroy(ESME *esme)
{
    if (esme != NULL) {
	conn_destroy(esme->conn);
	gw_free(esme);
    }
}


static SMPP_PDU *handle_bind_transmitter(ESME *esme, SMPP_PDU *pdu)
{
    SMPP_PDU *resp;
    
    esme->transmitter = 1;
    esme->version = pdu->u.bind_transmitter.interface_version;
    resp = smpp_pdu_create(bind_transmitter_resp,
    	    	    	    pdu->u.bind_transmitter.sequence_number);
#if 0 /* XXX system_id is not implemented in the PDU at the moment */
    resp->u.bind_transmitter_resp.system_id = 
    	octstr_duplicate(smsc_system_id);
#endif
    return resp;
}


static SMPP_PDU *handle_bind_receiver(ESME *esme, SMPP_PDU *pdu)
{
    SMPP_PDU *resp;
    
    esme->receiver = 1;
    esme->version = pdu->u.bind_receiver.interface_version;
    resp = smpp_pdu_create(bind_receiver_resp,
    	    	    	    pdu->u.bind_receiver.sequence_number);
#if 0 /* XXX system_id is not implemented in the PDU at the moment */
    resp->u.bind_receiver_resp.system_id = octstr_duplicate(smsc_system_id);
#endif
    return resp;
}


static SMPP_PDU *handle_submit_sm(ESME *esme, SMPP_PDU *pdu)
{
    SMPP_PDU *resp;
    unsigned long id;
    
    debug("test.smpp", 0, "submit_sm: short_message = <%s>",
    	  octstr_get_cstr(pdu->u.submit_sm.short_message));
    id = counter_increase(num_from_esme) + 1;
    if (id == max_to_esme)
    	info(0, "ESME has submitted all messages to SMSC.");
    time(&last_from_esme);

    resp = smpp_pdu_create(submit_sm_resp, pdu->u.submit_sm.sequence_number);
#if 0 /* XXX message_id is not implemented in the PDU at the moment */
    resp->u.submit_sm_resp.message_id = 
    	octstr_format("%ld", counter_increase(message_id_counter));
#endif
    return resp;
}


static SMPP_PDU *handle_deliver_sm_resp(ESME *esme, SMPP_PDU *pdu)
{
    return NULL;
}


static SMPP_PDU *handle_unbind(ESME *esme, SMPP_PDU *pdu)
{
    SMPP_PDU *resp;
    
    resp = smpp_pdu_create(unbind_resp, pdu->u.unbind.sequence_number);
    return resp;
}


static SMPP_PDU *handle_enquire_link(ESME *esme, SMPP_PDU *pdu)
{
    return smpp_pdu_create(enquire_link_resp, 
    	    	    	   pdu->u.enquire_link.sequence_number);
}


static SMPP_PDU *handle_enquire_link_resp(ESME *esme, SMPP_PDU *pdu)
{
    return NULL;
}


static struct {
    unsigned long type;
    SMPP_PDU *(*handler)(ESME *, SMPP_PDU *);
} handlers[] = {
    #define HANDLER(name) { name, handle_ ## name },
    HANDLER(bind_transmitter)
    HANDLER(bind_receiver)
    HANDLER(submit_sm)
    HANDLER(deliver_sm_resp)
    HANDLER(unbind)
    HANDLER(enquire_link)
    HANDLER(enquire_link_resp)
    #undef HANDLER
};
static int num_handlers = sizeof(handlers) / sizeof(handlers[0]);


static void handle_pdu(ESME *esme, SMPP_PDU *pdu)
{
    SMPP_PDU *resp;
    Octstr *os;
    int i;

    debug("test.smpp", 0, "Handling SMPP PDU of type %s", pdu->type_name);
    for (i = 0; i < num_handlers; ++i) {
    	if (handlers[i].type == pdu->type) {
	    resp = handlers[i].handler(esme, pdu);
	    if (resp != NULL) {
	    	os = smpp_pdu_pack(NULL, resp);
		conn_write(esme->conn, os);
		octstr_destroy(os);
		smpp_pdu_destroy(resp);
	    }
	    return;
	}
    }

    error(0, "Unhandled SMPP PDU.");
    smpp_pdu_dump(octstr_imm(""), pdu);
}


static void send_smpp_thread(void *arg)
{
    ESME *esme;
    Octstr *os;
    SMPP_PDU *pdu;
    unsigned long id;

    esme = arg;
    
    id = 0;
    while (!quitting && counter_value(num_to_esme) < max_to_esme) {
        id = counter_increase(num_to_esme) + 1;
        while (!quitting && counter_value(num_from_esme) + 500 < id)
            gwthread_sleep(1.0);
        if (quitting)
            break;
        pdu = smpp_pdu_create(deliver_sm, counter_increase(message_id_counter));
        pdu->u.deliver_sm.source_addr = octstr_create("456");
        pdu->u.deliver_sm.destination_addr = octstr_create("123");
        pdu->u.deliver_sm.short_message = octstr_format("%ld", id);
        if (esme->version > 0x33)
            pdu->u.deliver_sm.receipted_message_id = octstr_create("receipted_message_id\0");
        os = smpp_pdu_pack(NULL, pdu);
        conn_write(esme->conn, os);
        octstr_destroy(os);
        smpp_pdu_destroy(pdu);
        if (first_to_esme == (time_t) -1)
            time(&first_to_esme);
        debug("test.smpp", 0, "Delivered SMS %ld of %ld to bearerbox via SMPP.",
              id, max_to_esme);

        if ((id % enquire_interval) == 0) {
            pdu = smpp_pdu_create(enquire_link, counter_increase(message_id_counter));
            os = smpp_pdu_pack(NULL, pdu);
            conn_write(esme->conn, os);
            octstr_destroy(os);
            smpp_pdu_destroy(pdu);
            debug("test.smpp", 0, "Sent enquire_link to bearerbox.");
        }
    }
    time(&last_to_esme);
    if (id == max_to_esme)
	info(0, "All messages sent to ESME.");
    debug("test.smpp", 0, "%s terminates.", __func__);
}


static void receive_smpp_thread(void *arg)
{
    ESME *esme;
    Octstr *os;
    long len;
    long sender_id;
    SMPP_PDU *pdu;

    esme = arg;
    
    sender_id = -1;
    len = 0;
    while (!quitting && conn_wait(esme->conn, -1.0) != -1) {
    	for (;;) {
	    if (len == 0) {
		len = smpp_pdu_read_len(esme->conn);
		if (len == -1) {
		    error(0, "Client sent garbage, closing connection.");
		    goto error;
		} else if (len == 0) {
		    if (conn_eof(esme->conn) || conn_error(esme->conn))
		    	goto error;
		    break;
		}
	    }
    
    	    gw_assert(len > 0);
	    os = smpp_pdu_read_data(esme->conn, len);
	    if (os != NULL) {
    	    	len = 0;
		pdu = smpp_pdu_unpack(NULL, os);
		if (pdu == NULL) {
		    error(0, "PDU unpacking failed!");
		    octstr_dump(os, 0);
		} else {
		    handle_pdu(esme, pdu);
		    smpp_pdu_destroy(pdu);
		}
		octstr_destroy(os);
	    } else if (conn_eof(esme->conn) || conn_error(esme->conn))
	    	goto error;
	    else
		break;
	}

    	if (!quitting && esme->receiver && sender_id == -1)
	    sender_id = gwthread_create(send_smpp_thread, esme);
    }

error:
    if (sender_id != -1) {
	quit();
	gwthread_join(sender_id);
    }
    esme_destroy(esme);
    quit();
    debug("test.smpp", 0, "%s terminates.", __func__);
}


static void smsbox_thread(void *arg)
{
    Connection *conn;
    Msg *msg;
    Octstr *os;
    Octstr *reply_msg;
    unsigned long count;
    
    msg = msg_create(sms);
    msg->sms.sender = octstr_create("123");
    msg->sms.receiver = octstr_create("456");
    msg->sms.msgdata = octstr_create("hello world");
    reply_msg = msg_pack(msg);
    msg_destroy(msg);

    gwthread_sleep(1.0);
    conn = conn_open_tcp(bearerbox_host, port_for_smsbox, NULL);
    if (conn == NULL) {
	gwthread_sleep(2.0);
	conn = conn_open_tcp(bearerbox_host, port_for_smsbox, NULL);
    	if (conn == NULL)
	    panic(0, "Couldn't connect to bearerbox as smsbox");
    }

    while (!quitting && conn_wait(conn, -1.0) != -1) {
    	for (;;) {
	    os = conn_read_withlen(conn);
	    if (os == NULL) {
		if (conn_eof(conn) || conn_error(conn))
		    goto error;
		break;
	    }
	    
	    msg = msg_unpack(os);
	    if (msg == NULL || msg->type == wdp_datagram)
		error(0, "Bearerbox sent garbage to smsbox");

	    if (msg->type == sms) {
		if (first_from_bb == (time_t) -1)
		    time(&first_from_bb);
		count = counter_increase(num_from_bearerbox) + 1;
		debug("test.smpp", 0, 
		      "Bearerbox sent sms #%ld <%s> to smsbox, sending reply.",
		      count, octstr_get_cstr(msg->sms.msgdata));
		if (count == max_to_esme)
		    info(0, "Bearerbox has sent all messages to smsbox.");
		conn_write_withlen(conn, reply_msg);
		counter_increase(num_to_bearerbox);
	    }
	    msg_destroy(msg);
	    octstr_destroy(os);
	    time(&last_to_bb);
	}
    }
    
error:
    conn_destroy(conn);
    octstr_destroy(reply_msg);
    debug("test.smpp", 0, "%s terminates.", __func__);
}


static void accept_thread(void *arg)
{
    int fd;
    int new_fd;
    int port;
    socklen_t addrlen;
    struct sockaddr addr;
    long smsbox_thread_id;
    
    port = *(int *) arg;
    fd = make_server_socket(port, NULL);
    if (fd == -1)
    	panic(0, "Couldn't create SMPP listen port.");
    
    smsbox_thread_id = -1;
    for (;;) {
	if (gwthread_pollfd(fd, POLLIN, -1.0) != POLLIN)
	    break;
	addrlen = sizeof(addr);
	new_fd = accept(fd, &addr, &addrlen);
    	if (start_time == (time_t) -1)
	    time(&start_time);
	gwthread_create(receive_smpp_thread, 
			esme_create(conn_wrap_fd(new_fd, 0)));
	if (smsbox_thread_id == -1)
	    smsbox_thread_id = gwthread_create(smsbox_thread, NULL);
    }
    
    debug("test.smpp", 0, "%s terminates.", __func__);
}


static void handler(int signal)
{
    panic(0, "Caught signal %d.", signal);
}


static void help(void)
{
    info(0, "drive_smpp [-h] [-v level][-l logfile][-p port][-m msgs][-c config]");
}


int main(int argc, char **argv)
{
    struct sigaction act;
    int port;
    int opt;
    double run_time;
    char *log_file;
    char *config_file;

    gwlib_init();

    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);

    port = 2345;
    smsc_system_id = octstr_create("kannel_smpp");
    smsc_source_addr = octstr_create("123456");
    message_id_counter = counter_create();
    bearerbox_host = octstr_create("127.0.0.1");
    port_for_smsbox = 13001;
    max_to_esme = 1;
    num_to_esme = counter_create();
    num_from_esme = counter_create();
    num_to_bearerbox = counter_create();
    num_from_bearerbox = counter_create();
    log_file = config_file = NULL;

    while ((opt = getopt(argc, argv, "hv:p:m:l:c:")) != EOF) {
	switch (opt) {
	case 'v':
	    log_set_output_level(atoi(optarg));
	    break;

	case 'h':
	    help();
	    exit(0);

	case 'm':
	    max_to_esme = atoi(optarg);
	    break;

	case 'p':
	    port = atoi(optarg);
	    break;

	case 'l':
        log_file = optarg;
        break;
    
    case 'c':
        config_file = optarg;
        break;

	case '?':
	default:
	    error(0, "Invalid option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }

    if (log_file != NULL)
    	log_open(log_file, GW_DEBUG, GW_NON_EXCL);

    if (config_file != NULL) {
        Cfg *cfg;
        Octstr *tmp = octstr_create(config_file);
        
        cfg = cfg_create(tmp);
        octstr_destroy(tmp);
        if (cfg_read(cfg) == -1)
            panic(0, "Errors in config file.");
        smpp_pdu_init(cfg);
        cfg_destroy(cfg);
    }
            
    info(0, "Starting drive_smpp test.");
    gwthread_create(accept_thread, &port);
    gwthread_join_all();
    debug("test.smpp", 0, "Program exiting normally.");

    run_time = difftime(last_from_esme, first_to_esme);

    info(0, "Number of messages sent to ESME: %ld",
    	 counter_value(num_to_esme));
    info(0, "Number of messages sent to smsbox: %ld",
    	 counter_value(num_from_bearerbox));
    info(0, "Number of messages sent to bearerbox: %ld",
    	 counter_value(num_to_bearerbox));
    info(0, "Number of messages sent to SMSC: %ld",
    	 counter_value(num_from_esme));
    info(0, "Time: %.0f secs", run_time);
    info(0, "Time until all sent to ESME: %.0f secs", 
    	 difftime(last_to_esme, start_time));
    info(0, "Time from first from bb to last to bb: %.0f secs", 
    	 difftime(last_to_bb, first_from_bb));
    info(0, "Time until all sent to SMSC: %.0f secs", 
    	 difftime(last_from_esme, start_time));
    info(0, "SMPP messages SMSC to ESME: %.1f msgs/sec",
    	 counter_value(num_to_esme) / run_time);
    info(0, "SMPP messages ESME to SMSC: %.1f msgs/sec",
    	 counter_value(num_from_esme) / run_time);

    octstr_destroy(smsc_system_id);
    octstr_destroy(smsc_source_addr);
    octstr_destroy(bearerbox_host);
    counter_destroy(num_to_esme);
    counter_destroy(num_from_esme);
    counter_destroy(num_to_bearerbox);
    counter_destroy(num_from_bearerbox);
    counter_destroy(message_id_counter);

    gwlib_shutdown();
    return 0;
}
