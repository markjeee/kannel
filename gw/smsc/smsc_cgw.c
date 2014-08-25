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
 * smsc_cgw.c - Implements an interface to the Sonera ContentGateway software
 *
 * Anders Lindh, FlyerOne Ltd <alindh@flyerone.com>
 *
 *
 * Changelog:
 *
 * 22/02/2002: Preliminary support for the Euro character (req. ProviderServer 2.5.2)
 * 25/01/2002: Caught a potentially nasty bug
 * 16/01/2002: Some code cleanup
 * 10/01/2002: Fixed a bug in trn handling
 * 16/11/2001: Some minor fixes (Thanks to Tuomas Luttinen)
 * 12/11/2001: Delivery reports, better acking and numerous other small fixes
 * 05/11/2001: Initial release. Based heavily on smsc_emi2 and smsc_at2. 
 *
 *
 * TO-DO: Do some real life testing
 *        Squash bugs
 *
 * Usage: add the following to kannel.conf:
 *
 * group = smsc
 * smsc = cgw
 * host = x.x.x.x      <- CGW server host
 * port = xxxx 	       <- CGW server otp port (if omitted, defaults to 21772)
 * receive-port = xxxx <- our port for incoming messages
 * appname = xxxx      <- Name of a "Send only" service. Defaults to "send".
 *	           	  All outgoing messages are routed through this service.
 *
 * Configure ContentGateway software to use the above port as text-port
 * (in provider.cnf). This is documented in their "Guide for Service
 * development 2.5", page 80.
 *
 * Add a new "Receive only" service (with the Remote Control tool), and set
 * the "Application for Incoming Message" to "text://kannelhost:receive-port".
 *
 *
 * Note:
 *
 * Do NOT define the service as a "Query/Reply" service, as it expects response
 * messages to have a matching session-id tag. Kannel does not store the
 * relation between a query and a reply, so the response message will not have
 * the session-id tag. Even though the messages are delivered successfully,
 * billing of premium priced services will fail.
 *
 *
 */


#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "msg.h"
#include "sms.h"
#include "dlr.h"


#define CGW_DEFPORT 	21772
#define	CGW_EOL     	0x0A
#define CGW_TRN_MAX 	500	/* Size of our internal message buffer. */

#define CGWOP_MAXARGS	10     	/* max. number of name:value pairs in cgwop */

/* Valid CGW operations */

#define CGW_OP_NOP	0     	/* this doesn't really exist.. */
#define CGW_OP_MSG	1
#define CGW_OP_OK	2
#define CGW_OP_ERR	3
#define CGW_OP_DELIVERY	4
#define CGW_OP_HELLO	5
#define CGW_OP_STATUS	6

struct cgwop
{
    int op;    			/* one of above */
    int num_fields;
    int trn;                    /* transaction number, used to ACK messages */
    Octstr **name;    		/* for storing name/value pairs */
    Octstr **value;
};

static char *cgw_ops[6] = {"nop", "msg", "ok", "err", "delivery", "hello"};


typedef struct privdata
{
    List *outgoing_queue;
    long receiver_thread;
    long sender_thread;
    int	shutdown;    	       	/* Internal signal to shut down */
    int	listening_socket;     	/* File descriptor */
    int	send_socket;
    int	port;    	        /* SMSC port */
    int	rport;    		/* port for receiving messages*/
    int	our_port;    	  	/* Optional local port number in which to
                	        	 * bind our end of send connection */
    Octstr *host;
    Octstr *allow_ip, *deny_ip;
    Octstr *appname;    	/* Application name as defined in Sonera Remote manager */

    Msg	*sendmsg[CGW_TRN_MAX];
    time_t sendtime[CGW_TRN_MAX];
    int	dlr[CGW_TRN_MAX];    	/* dlr = DLR_SMSC_SUCCESS || DLR_SMSC_FAIL */
    int	unacked;    		/* Sent messages not acked */
    int	waitack;        	/* Seconds to wait to ack */
    int	nexttrn;
    long check_time;     	/* last checked ack/nack status */
}
PrivData;


static int cgw_add_msg_cb(SMSCConn *conn, Msg *sms);
static int cgw_shutdown_cb(SMSCConn *conn, int finish_sending);
static void cgw_start_cb(SMSCConn *conn);
static long cgw_queued_cb(SMSCConn *conn);
static void cgw_sender(void *arg);
static Connection *cgw_open_send_connection(SMSCConn *conn);
static int cgw_send_loop(SMSCConn *conn, Connection *server);
void cgw_check_acks(PrivData *privdata);
int cgw_wait_command(PrivData *privdata, SMSCConn *conn, Connection *server, int timeout);
static int cgw_open_listening_socket(SMSCConn *conn, PrivData *privdata);
static void cgw_listener(void *arg);
static void cgw_receiver(SMSCConn *conn, Connection *server);
static int cgw_handle_op(SMSCConn *conn, Connection *server, struct cgwop *cgwop);
struct cgwop *cgw_read_op(PrivData *privdata, SMSCConn *conn, Connection *server, time_t timeout);



/******************************************************************************
 * Functions for handling cgwop -structures
 */

static void cgwop_add(struct cgwop *cgwop, Octstr *name, Octstr *value)
{
    if (cgwop->num_fields < CGWOP_MAXARGS)
    {
        cgwop->name[cgwop->num_fields] = octstr_duplicate(name);
        cgwop->value[cgwop->num_fields] = octstr_duplicate(value);

        cgwop->num_fields++;
    } else
    {
        info(0, "cgw: CGWOP_MAXARGS exceeded.");
    }
}


static struct cgwop *cgwop_create(int op, int trn)
{
    struct cgwop *ret;
    Octstr *trnstr;

    ret = gw_malloc(sizeof(struct cgwop));

    ret->op = op;
    ret->num_fields = 0;
    ret->trn = trn;

    ret->name = gw_malloc(CGWOP_MAXARGS * sizeof(Octstr *));
    ret->value = gw_malloc(CGWOP_MAXARGS * sizeof(Octstr *));

    if (trn != -1)
    {
        trnstr = octstr_create("");
        octstr_append_decimal(trnstr, trn);
        cgwop_add(ret, octstr_imm("client-id"), trnstr);
        octstr_destroy(trnstr);
    }

    return ret;
}

static void cgwop_destroy(struct cgwop *cgwop)
{
    int len;

    len = cgwop->num_fields;
    while (--len >= 0)
    {
        octstr_destroy(cgwop->name[len]);      /* octstr_destroy(NULL) is ok */
        octstr_destroy(cgwop->value[len]);
    }

    gw_free(cgwop->name);
    gw_free(cgwop->value);
    gw_free(cgwop);
}

static Octstr *cgwop_get(struct cgwop *cgwop, Octstr *name)
{
    int len = cgwop->num_fields;

    while (--len >= 0)
        if (octstr_compare(name, cgwop->name[len]) == 0)
            return cgwop->value[len];
    return NULL;
}

static Octstr *cgwop_tostr(struct cgwop *cgwop)
{
    int len = cgwop->num_fields;
    Octstr *str;

    if (cgw_ops[cgwop->op] == NULL) return NULL;     /* invalid operation */

    str = octstr_create("");

    octstr_append(str, octstr_imm("op:"));
    octstr_append(str, octstr_imm(cgw_ops[cgwop->op]));
    octstr_append_char(str, CGW_EOL);

    while (--len >= 0)
    {
        octstr_append(str, cgwop->name[len]);
        octstr_append_char(str, ':');
        octstr_append(str, cgwop->value[len]);
        octstr_append_char(str, CGW_EOL);
    }
    octstr_append(str, octstr_imm("end:"));
    octstr_append(str, octstr_imm(cgw_ops[cgwop->op]));
    octstr_append_char(str, CGW_EOL);

    return str;
}

static int cgwop_send(Connection *conn, struct cgwop *cgwop)
{
    Octstr *dta = cgwop_tostr(cgwop);

    if (dta == NULL) return -1;     /* couldn't convert to string */

    if (conn_write(conn, dta) == -1)
    {
        octstr_destroy(dta);
        return -1;
    }

    octstr_destroy(dta);
    return 1;
}

/******************************************************************************
 * cgw_encode_msg - Encode a msg according to specifications
 */

static Octstr *cgw_encode_msg(Octstr* str)
{
    int i;
    char esc = 27;
    char e = 'e';

    /* Euro char (0x80) -> ESC + e. We do this conversion as long as the message 
       length is under 160 chars (the checking could probably be done better) */

    while ((i = octstr_search_char(str, 0x80, 0)) != -1) {    
        octstr_delete(str, i, 1);     /* delete Euro char */
	if (octstr_len(str) < 160) {
	    octstr_insert_data(str, i, &esc, 1);  /* replace with ESC + e */
	    octstr_insert_data(str, i+1, &e, 1);  
	} else {
	    octstr_insert_data(str, i, &e, 1);  /* no room for ESC + e, just replace with an e */
        }
    }


    /* Escape backslash characters */
    while ((i = octstr_search_char(str, '\\', 0)) != -1) {
        octstr_insert(str, octstr_imm("\\"), i);
    }
    /* Remove Line Feed characters */
    while ((i = octstr_search_char(str, CGW_EOL, 0)) != -1) {
        octstr_delete(str, i, 1);     /* delete EOL char */
        octstr_insert(str, octstr_imm("\\n"), i);
    }
    /* Remove Carriage return characters */
    while ((i = octstr_search_char(str, 13, 0)) != -1) {
        octstr_delete(str, i, 1);     /* delete EOL char */
        octstr_insert(str, octstr_imm("\\r"), i);
    }

    return str;
}

/******************************************************************************
 * cgw_decode_msg - Decode an incoming msg
 */

static Octstr *cgw_decode_msg(Octstr* str)
{
    int i;

    /* make \n -> linefeed */
    while ((i = octstr_search(str, octstr_imm("\\n"), 0)) != -1) {
        octstr_delete(str, i, 2);     /* delete "\n" str */
        octstr_insert(str, octstr_imm("\n"), i);
    }
    /* make \r -> carriage return */
    while ((i = octstr_search(str, octstr_imm("\\r"), 0)) != -1) {
        octstr_delete(str, i, 2);     /* delete EOL char */
        octstr_insert(str, octstr_imm("\r"), i);
    }
    /* remove double backslashes */
    while ((i = octstr_search(str, octstr_imm("\\\\"), 0)) != -1) {
        octstr_delete(str, i, 1);
    }

    return str;
}

/******************************************************************************
 * msg_to_cgwop - Create a send cgwop from a message
 */

static struct cgwop *msg_to_cgwop(PrivData *privdata, Msg *msg, int trn)
{
    struct cgwop *cgwop;
    Octstr *sender, *udh, *dta;

    cgwop = cgwop_create(CGW_OP_MSG, trn);

    if (cgwop == NULL) return NULL;

    if (!octstr_check_range(msg->sms.sender, 0, octstr_len(msg->sms.sender), gw_isdigit))
    {
        /* If alphanumeric, simply prefix sender with '$' char */
        sender = octstr_create("$");
        octstr_append(sender, msg->sms.sender);
    } else sender = octstr_duplicate(msg->sms.sender);

    cgwop_add(cgwop, octstr_imm("app"), privdata->appname);
    cgwop_add(cgwop, octstr_imm("from"), sender);
    cgwop_add(cgwop, octstr_imm("to"), msg->sms.receiver);

    /* If delivery reports are asked, ask for them by adding a nrq:anything field */
    if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask))
        cgwop_add(cgwop, octstr_imm("nrq"), octstr_imm("true"));

    octstr_destroy(sender);

    if (octstr_len(msg->sms.udhdata))
    {
        udh = octstr_duplicate(msg->sms.udhdata);
        octstr_binary_to_hex(udh, 1);
        cgwop_add(cgwop, octstr_imm("udh"), udh);
        octstr_destroy(udh);

        dta = octstr_duplicate(msg->sms.msgdata);
        octstr_binary_to_hex(dta, 1);
        cgwop_add(cgwop, octstr_imm("msg"), dta);
        cgwop_add(cgwop, octstr_imm("type"), octstr_imm("bin"));
        octstr_destroy(dta);
    } else
    {
        cgwop_add(cgwop, octstr_imm("msg"), cgw_encode_msg(msg->sms.msgdata));
    }

    return cgwop;
}


/******************************************************************************
 * Called to create the SMSC. This is our entry point.
 */

int smsc_cgw_create(SMSCConn *conn, CfgGroup *cfg)
{
    PrivData *privdata;
    Octstr *allow_ip, *deny_ip, *host, *appname;
    long portno, our_port, waitack;
    int i;

    privdata = gw_malloc(sizeof(PrivData));
    privdata->outgoing_queue = gwlist_create();
    privdata->listening_socket = -1;

    if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1)
        portno = 0;
    privdata->port = portno;

    if (cfg_get_integer(&portno, cfg, octstr_imm("receive-port")) < 0)
        portno = 0;
    privdata->rport = portno;

    host = cfg_get(cfg, octstr_imm("host"));
    appname = cfg_get(cfg, octstr_imm("appname"));

    if (cfg_get_integer(&our_port, cfg, octstr_imm("our-port")) == -1)
        privdata->our_port = 0;     /* 0 means use any port */
    else
        privdata->our_port = our_port;

    allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    if (allow_ip)
        deny_ip = octstr_create("*.*.*.*");
    else
        deny_ip = NULL;

    if (cfg_get_integer(&waitack, cfg, octstr_imm("wait-ack")) < 0)
        privdata->waitack = 60;
    else
        privdata->waitack = waitack;

    if (privdata->port <= 0 || privdata->port > 65535) {
        info(1, "No port defined for cgw -> using default (%d)", CGW_DEFPORT);
        privdata->port = CGW_DEFPORT;
    }


    if (host == NULL) {
        error(0, "'host' missing in cgw configuration.");
        goto error;
    }

    if (appname == NULL)
        appname = octstr_create("send");

    privdata->allow_ip = allow_ip;
    privdata->deny_ip = deny_ip;
    privdata->host = host;
    privdata->appname = appname;
    privdata->nexttrn = 0;
    privdata->check_time = 0;

    for (i = 0; i < CGW_TRN_MAX; i++) {
        privdata->sendtime[i] = 0;
        privdata->dlr[i] = 0;
    }

    if (privdata->rport > 0 && cgw_open_listening_socket(conn,privdata) < 0) {
        gw_free(privdata);
        privdata = NULL;
        goto error;
    }


    conn->data = privdata;

    conn->name = octstr_format("CGW:%d", privdata->port);

    privdata->shutdown = 0;

    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = time(NULL);

    if (privdata->rport > 0 && (privdata->receiver_thread = gwthread_create(cgw_listener, conn)) == -1)
        goto error;

    if ((privdata->sender_thread = gwthread_create(cgw_sender, conn)) == -1) {
        privdata->shutdown = 1;
        goto error;
    }

    conn->shutdown = cgw_shutdown_cb;
    conn->queued = cgw_queued_cb;
    conn->start_conn = cgw_start_cb;
    conn->send_msg = cgw_add_msg_cb;

    return 0;

error:
    error(0, "Failed to create CGW smsc connection");
    if (privdata != NULL)
        gwlist_destroy(privdata->outgoing_queue, NULL);

    gw_free(privdata);
    octstr_destroy(host);
    octstr_destroy(allow_ip);
    octstr_destroy(deny_ip);
    octstr_destroy(appname);

    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    info(0, "exiting");
    return -1;
}


/******************************************************************************
 * Callbacks for startup, shutdown, incoming and outgoing messages
 */


static int cgw_add_msg_cb(SMSCConn *conn, Msg *sms)
{
    PrivData *privdata = conn->data;
    Msg *copy;

    copy = msg_duplicate(sms);
    gwlist_produce(privdata->outgoing_queue, copy);
    gwthread_wakeup(privdata->sender_thread);

    return 0;
}


static int cgw_shutdown_cb(SMSCConn *conn, int finish_sending)
{
    PrivData *privdata = conn->data;

    debug("bb.sms", 0, "Shutting down SMSCConn CGW, %s",
          finish_sending ? "slow" : "instant");

    /* Documentation claims this would have been done by smscconn.c,
       but isn't when this code is being written. */
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    privdata->shutdown = 1;     /* Separate from why_killed to avoid locking, as
                		 * why_killed may be changed from outside? */

    if (finish_sending == 0) {
        Msg *msg;
        while ((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL) {
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
        }
    }

    if (privdata->rport > 0)
        gwthread_wakeup(privdata->receiver_thread);
    return 0;
}


static void cgw_start_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;

    /* in case there are messages in the buffer already */
    if (privdata->rport > 0)
        gwthread_wakeup(privdata->receiver_thread);
    debug("smsc.cgw", 0, "smsc_cgw: start called");
}


static long cgw_queued_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    long ret = gwlist_len(privdata->outgoing_queue);

    /* use internal queue as load, maybe something else later */

    conn->load = ret;
    return ret;
}


/******************************************************************************
 * This is the entry point for out sender thread. This function is responsible
 * for sending and acking messages in queue
 */

static void cgw_sender(void *arg)
{
    SMSCConn *conn = arg;
    PrivData *privdata = conn->data;
    Msg *msg = NULL;
    Connection *server = NULL;
    int l = 0;
    int ret = 0;

    conn->status = SMSCCONN_CONNECTING;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    while (!privdata->shutdown) {

        /* check that connection is active */
        if (conn->status != SMSCCONN_ACTIVE) {
            if ((server = cgw_open_send_connection(conn)) == NULL) {
                privdata->shutdown = 1;
                error(0, "Unable to connect to CGW server");
                return ;
            }

            conn->status = SMSCCONN_ACTIVE;
            bb_smscconn_connected(conn);
        } else {
	    ret = 0;
            l = gwlist_len(privdata->outgoing_queue);
            if (l > 0)
               ret = cgw_send_loop(conn, server);     /* send any messages in queue */

            if (ret != -1) ret = cgw_wait_command(privdata, conn, server, 1);     /* read ack's and delivery reports */
            if (ret != -1) cgw_check_acks(privdata);     /* check un-acked messages */
 
            if (ret == -1) {
                mutex_lock(conn->flow_mutex);
                conn->status = SMSCCONN_RECONNECTING;
                mutex_unlock(conn->flow_mutex);
	    }
        }
    }

    conn_destroy(server);

    while ((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL)
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
    mutex_lock(conn->flow_mutex);

    conn->status = SMSCCONN_DEAD;

    gwlist_destroy(privdata->outgoing_queue, NULL);
    octstr_destroy(privdata->host);
    octstr_destroy(privdata->allow_ip);
    octstr_destroy(privdata->deny_ip);

    gw_free(privdata);
    conn->data = NULL;

    mutex_unlock(conn->flow_mutex);
    debug("bb.sms", 0, "smsc_cgw connection has completed shutdown.");
    bb_smscconn_killed();
}


static Connection *cgw_open_send_connection(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    int wait;
    Connection *server;
    Msg *msg;

    wait = 0;
    while (!privdata->shutdown) {

        /* Change status only if the first attempt to form a
	 * connection fails, as it's possible that the SMSC closed the
	 * connection because of idle timeout and a new one will be
	 * created quickly. */
        if (wait) {
            if (conn->status == SMSCCONN_ACTIVE) {
                mutex_lock(conn->flow_mutex);
                conn->status = SMSCCONN_RECONNECTING;
                mutex_unlock(conn->flow_mutex);
            }
            while ((msg = gwlist_extract_first(privdata->outgoing_queue)))
                bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_TEMPORARILY, NULL);
            info(0, "smsc_cgw: waiting for %d minutes before trying to connect again", wait);
            gwthread_sleep(wait * 60);
            wait = wait > 5 ? 10 : wait * 2;
        } else
            wait = 1;

        server = conn_open_tcp_with_port(privdata->host, privdata->port,
                                         privdata->our_port, conn->our_host);

        if (privdata->shutdown) {
            conn_destroy(server);
            return NULL;
        }

        if (server == NULL) {
            error(0, "smsc_cgw: opening TCP connection to %s failed", octstr_get_cstr(privdata->host));
            continue;
        }

        if (conn->status != SMSCCONN_ACTIVE) {
            mutex_lock(conn->flow_mutex);
            conn->status = SMSCCONN_ACTIVE;
            conn->connect_time = time(NULL);
            mutex_unlock(conn->flow_mutex);
            bb_smscconn_connected(conn);
        }
        return server;
    }
    return NULL;
}


/******************************************************************************
 * Send messages in queue.
 */

static int cgw_send_loop(SMSCConn *conn, Connection *server)
{
    PrivData *privdata = conn->data;
    struct cgwop *cgwop;
    Msg	*msg;
    int firsttrn;

    /* Send messages in queue */
    while ((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL) {
        firsttrn = privdata->nexttrn;
        while (privdata->sendtime[privdata->nexttrn] != 0) { 
            if (++privdata->nexttrn >= CGW_TRN_MAX) privdata->nexttrn = 0;    
            if (privdata->nexttrn == firsttrn) { /* no available trn */
	        /* this happens too many messages are sent, and old messages
		 * haven't been acked. In this case, increase size of 
                 * CGW_TRN_MAX */
                info(0, "cgw: Saturated, increase size of CGW_TRN_MAX!");
                gwlist_produce(privdata->outgoing_queue, msg);
                return 1;     /* re-insert, and go check for acks */
            }
        }

        cgwop = msg_to_cgwop(privdata, msg, privdata->nexttrn);

        if (cgwop == NULL) {
            info(0, "cgw: cgwop == NULL");
            return 0;
        }

        privdata->sendmsg[privdata->nexttrn] = msg;
        privdata->sendtime[privdata->nexttrn] = time(NULL);

        if (cgwop_send(server, cgwop) == -1) {
            cgwop_destroy(cgwop);
            info(0, "cgw: Unable to send (cgwop_send() == -1)");
            return -1;
        }

        privdata->unacked++;

        cgwop_destroy(cgwop);
    }
    return 0;
}

/* Check whether there are messages the server hasn't acked in a
 * reasonable time */

void cgw_check_acks(PrivData *privdata)
{
    time_t	current_time;
    int i;

    current_time = time(NULL);
    if (privdata->unacked && (current_time > privdata->check_time + 30)) {
        privdata->check_time = current_time;
        for (i = 0; i < CGW_TRN_MAX; i++)
            if (privdata->sendtime[i] && privdata->sendtime[i] < (current_time - privdata->waitack)) {
                privdata->sendtime[i] = 0;
                privdata->unacked--;
                warning(0, "smsc_cgw: received neither OK nor ERR for message %d "
                        "in %d seconds, resending message", i, privdata->waitack);
                gwlist_produce(privdata->outgoing_queue, privdata->sendmsg[i]);
            }
    }
}


/******************************************************************************
 * cgw_wait_command - Used by cgw_sender thread to read delivery reports
 */

int cgw_wait_command(PrivData *privdata, SMSCConn *conn, Connection *server, int timeout)
{
    int ret;
    struct cgwop *cgwop;

    /* is there data to be read? */
    ret = gwthread_pollfd(privdata->send_socket, POLLIN, 0.2); 
    
    if (ret != -1) {
        /* read all waiting ops */
	cgwop = cgw_read_op(privdata, conn, server, timeout);
        if (cgwop != NULL) {
	    do {
		if (conn_eof(server)) {
		    info(0, "cgw: Connection closed by SMSC");
		    conn->status = SMSCCONN_DISCONNECTED;
		    if (cgwop != NULL) cgwop_destroy(cgwop);
		    return -1;
		}
		if (conn_error(server)) {
		    error(0, "cgw: Error trying to read ACKs from SMSC");
		    if (cgwop != NULL) cgwop_destroy(cgwop);
		    return -1;
		}

		cgw_handle_op(conn, server, cgwop);
		cgwop_destroy(cgwop);
	    } while ((cgwop = cgw_read_op(privdata, conn, server, timeout)) != NULL);
	} else 
	    conn_wait(server, 1); /* added because gwthread_pollfd
				     seems to always return 1. This will keep
				     the load on a reasonable level */
    }

    return 0;
}


/******************************************************************************
 * cgw_read_op - read an operation, and return it as a *cgwop structure
 *
 * This function will not lock and wait for data if none is available. It will
 * however lock until a whole op has been read. Timeout not implemented yet.
 */

struct cgwop *cgw_read_op(PrivData *privdata, SMSCConn *conn, Connection *server, time_t timeout)
{
    Octstr *line, *name, *value;
    int finished = 0;
    int c = 0;
    struct cgwop *cgwop = NULL;

    int op = CGW_OP_NOP;

    if ((line = conn_read_line(server)) == NULL) 
        return NULL;     /* don't block */

    do
    {
        while (line == NULL)
            line = conn_read_line(server);     /* wait for more data */

        c = octstr_search_char(line, ':', 0);
        if (c != -1) {
            name = octstr_copy(line, 0, c);
            value = octstr_copy(line, c + 1, octstr_len(line) - (c + 1));

            if (octstr_compare(name, octstr_imm("hello")) == 0) {
                /* A connection is started by CGW by sending a 
		 * "hello: Provider Server..." line. */

                cgwop = cgwop_create(CGW_OP_HELLO, 0);
                cgwop_add(cgwop, octstr_imm("hello"), value);

                octstr_destroy(name);
                octstr_destroy(value);
                octstr_destroy(line);

                return cgwop;
            }

            if (octstr_compare(name, octstr_imm("op")) == 0) {
                /* check different ops */
                if (octstr_compare(value, octstr_imm("msg")) == 0)
                    op = CGW_OP_MSG;
                else
                    if (octstr_compare(value, octstr_imm("ok")) == 0)
                        op = CGW_OP_OK;
                    else
                        if (octstr_compare(value, octstr_imm("delivery")) == 0)
                            op = CGW_OP_DELIVERY;
                        else
                            if (octstr_compare(value, octstr_imm("err")) == 0)
                                op = CGW_OP_ERR;
                            else
                                if (octstr_compare(value, octstr_imm("status")) == 0)
                                    op = CGW_OP_STATUS;
                                else
                                    info(0, "CGW: Received unknown op: %s", octstr_get_cstr(value));

                if (cgwop == NULL)
                    cgwop = cgwop_create(op, 0);
                else
                    info(0, "cgw: cgwop != null");
            }

            if (op != CGW_OP_NOP) {
                /* All commands have to be inside an op:xx ... end:xx statement */

                if (octstr_compare(name, octstr_imm("end")) == 0) { /* found end of op */
                    finished = 1;
                } else {
                    /* store in name/value fields in cgwop */
                    if (cgwop != NULL) {
                        cgwop_add(cgwop, name, value);
                    }
                }
            }
            octstr_destroy(name);
            octstr_destroy(value);
            octstr_destroy(line);

            if (!finished) line = conn_read_line(server);
        } else {
            info(0, "cgw: Received invalid input: %s", octstr_get_cstr(line));
            octstr_destroy(line);
            finished = 1;
        }

    } while (!finished);

    return cgwop;
}

static int cgw_open_listening_socket(SMSCConn *conn, PrivData *privdata)
{
    int s;

    if ((s = make_server_socket(privdata->rport, (conn->our_host ? octstr_get_cstr(conn->our_host) : NULL))) == -1) {
        error(0, "smsc_cgw: could not create listening socket in port %d", privdata->rport);
        return -1;
    }
    if (socket_set_blocking(s, 0) == -1) {
        error(0, "smsc_cgw: couldn't make listening socket port %d non-blocking", privdata->rport);
        close(s);
        return -1;
    }
    privdata->listening_socket = s;
    return 0;
}

/******************************************************************************
 * This is the entry point for our receiver thread. Listens for incoming 
 * connections and handles operations.
 */

static void cgw_listener(void *arg)
{
    SMSCConn	*conn = arg;
    PrivData	*privdata = conn->data;
    struct sockaddr_in server_addr;
    socklen_t	server_addr_len;
    Octstr	*ip;
    Connection	*server;
    int s, ret;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    while (!privdata->shutdown) {
        server_addr_len = sizeof(server_addr);
	
        ret = gwthread_pollfd(privdata->listening_socket, POLLIN, -1);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            error(0, "Poll for cgw smsc connections failed, shutting down");
            break;
        }

        if (privdata->shutdown)
            break;
        if (ret == 0) /* This thread was woken up from elsewhere, but
                       * if we're not shutting down nothing to do here. */
            continue;
        s = accept(privdata->listening_socket, (struct sockaddr *) & server_addr,
                   &server_addr_len);
        if (s == -1) {
            warning(errno, "cgw_listener: accept() failed, retrying...");
            continue;
        }

        ip = host_ip(server_addr);
        if (!is_allowed_ip(privdata->allow_ip, privdata->deny_ip, ip)) {
            info(0, "CGW smsc connection tried from denied host <%s>, disconnected", octstr_get_cstr(ip));
            octstr_destroy(ip);
            close(s);
            continue;
        }
        server = conn_wrap_fd(s, 0);
        if (server == NULL) {
            error(0, "cgw_listener: conn_wrap_fd failed on accept()ed fd");
            octstr_destroy(ip);
            close(s);
            continue;
        }
        conn_claim(server);
        info(0, "cgw: smsc connected from %s", octstr_get_cstr(ip));
        octstr_destroy(ip);

        cgw_receiver(conn, server);
        conn_destroy(server);
    }
    if (close(privdata->listening_socket) == -1)
        warning(errno, "smsc_cgw: couldn't close listening socket at shutdown");
    gwthread_wakeup(privdata->sender_thread);
}

static void cgw_receiver(SMSCConn *conn, Connection *server)
{
    PrivData *privdata = conn->data;
    struct cgwop *cgwop;

    while (1) {
        if (conn_eof(server)) {
            info(0, "cgw: receive connection closed by SMSC");
            return ;
        }
        if (conn_error(server)) {
            error(0, "cgw: receive connection broken");
            return ;
        }

        cgwop = cgw_read_op(conn->data, conn, server, 0);

        if (cgwop != NULL) {
            cgw_handle_op(conn, server, cgwop);
            cgwop_destroy(cgwop);
        } else
            conn_wait(server, -1);

        if (privdata->shutdown)
            break;
    }
    return ;
}

/******************************************************************************
 * This function handles incoming operations. Used by both receiver and sender
 * threads (i.e. sender thread uses this function for delivery and ack
 * operations).
 * Returns 1 if successfull, otherwise 0
 */
static int cgw_handle_op(SMSCConn *conn, Connection *server, struct cgwop *cgwop)
{
    PrivData *privdata = conn->data;
    Msg *msg = NULL;
    Octstr *from, *sid, *to, *msgdata; /* for messages */
    Octstr *msid, *status, *txt;    		       /* delivery reports */
    Octstr *clid;    		       		       /* for acks */
    struct cgwop *reply = NULL;
    long trn, stat;                          /* transaction number for ack */
    Msg *dlrmsg = NULL, *origmsg = NULL;
    Octstr *ts;

    if (cgwop == NULL) return 0;

    from = cgwop_get(cgwop, octstr_imm("from"));
    sid = cgwop_get(cgwop, octstr_imm("session-id"));
    to = cgwop_get(cgwop, octstr_imm("to"));
    msgdata = cgwop_get(cgwop, octstr_imm("msg"));
    txt = cgwop_get(cgwop, octstr_imm("txt"));

    msid = cgwop_get(cgwop, octstr_imm("msid"));
    status = cgwop_get(cgwop, octstr_imm("status"));
    clid = cgwop_get(cgwop, octstr_imm("client-id"));

    if (clid != NULL)
    {
        octstr_parse_long(&trn, clid, 0, 10);
        if ((trn < 0) || (trn >= CGW_TRN_MAX)) { /* invalid transaction number */
	    info(0, "cgw: Invalid transaction number: %d", (int) trn);
            trn = -1;            
	    return 0;
        }
    }

    switch (cgwop->op)
    {
    case CGW_OP_MSG:
        msg = msg_create(sms);
        time(&msg->sms.time);
        msg->sms.msgdata = cgw_decode_msg(octstr_duplicate(msgdata));
        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.smsc_id = octstr_duplicate(conn->id);
        bb_smscconn_receive(conn, msg);

        reply = cgwop_create(CGW_OP_OK, -1);
        cgwop_add(reply, octstr_imm("session-id"), sid);
        cgwop_send(server, reply);     /* send reply */

        cgwop_destroy(reply);

        break;

    case CGW_OP_DELIVERY:
        if (privdata->dlr[trn]) {

            octstr_parse_long(&stat, status, 0, 10);
            origmsg = privdata->sendmsg[trn];

            if (origmsg == NULL) break;

            ts = octstr_create("");
            octstr_append(ts, conn->id);
            octstr_append_char(ts, '-');
            octstr_append_decimal(ts, trn);

            switch (stat) {
            case 0:     /* delivered */
                dlrmsg = dlr_find(conn->id,
                                            ts,     /* timestamp */
                                            msid,   /* destination */
                                  DLR_SUCCESS, 0);
                break;
            case 1:     /* buffered */
                dlrmsg = dlr_find(conn->id,
                                            ts,     /* timestamp */
                                            msid,   /* destination */
                                  DLR_BUFFERED, 0);
                break;
            case 2:     /* not delivered */
                dlrmsg = dlr_find(conn->id,
                                            ts,     /* timestamp */
                                            msid,   /* destination */
                                  DLR_FAIL, 0);
                break;
            }

            octstr_destroy(ts);
            if (dlrmsg != NULL) {
                dlrmsg->sms.msgdata = octstr_duplicate(txt);
                bb_smscconn_receive(conn, dlrmsg);
            }
        }

        break;

    case CGW_OP_OK:
        if (trn == -1) break;     /* invalid transaction number */
        /* info(0, "cgw: Got ACK: %s", octstr_get_cstr(clid)); */

        privdata->sendtime[trn] = 0;
        privdata->unacked--;

        /* add delivery notification request if wanted */

        msg = privdata->sendmsg[trn];

        if (msg && msg->sms.dlr_url && DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask)) {
            Octstr *ts;

            ts = octstr_create("");
            octstr_append(ts, conn->id);
            octstr_append_char(ts, '-');
            octstr_append_decimal(ts, trn);

            dlr_add(conn->id, ts, msg, 0);

            octstr_destroy(ts);
            privdata->dlr[trn] = 1;
        } else {
            privdata->dlr[trn] = 0;
        }

	/* mark as successfully sent */
        bb_smscconn_sent(conn, msg, NULL);

        break;

    case CGW_OP_STATUS:
        info(0, "CGW: Warning: Got session status");
        /* op:status messages are sent by ProviderServer to tell if there are problems with
           the session status. These are not wanted, and should never occur, as the delivery is
           cancelled, and no end-user billing is done. */

        break;


    case CGW_OP_HELLO:
        info(0, "CGW: Server said: %s", octstr_get_cstr(cgwop_get(cgwop, octstr_imm("hello"))));
        break;

    case CGW_OP_ERR:
        if (trn == -1) break;     /* invalid transaction number */

        info(0, "CGW: Received error: %s", octstr_get_cstr(txt));

        privdata->sendtime[trn] = 0;
        privdata->unacked--;

        bb_smscconn_send_failed(conn, privdata->sendmsg[trn],
                            SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));

        break;

    default:
        info(0, "cgw: Unknown operation: %d", cgwop->op);
        return 0;
    }

    return 1;
}



