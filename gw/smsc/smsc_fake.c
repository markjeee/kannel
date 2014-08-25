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
 * smsc_fake.c - interface to fakesmsc.c
 *
 * Uoti Urpala 2001
 */

/* Doesn't support multi-send
 * Doesn't warn about unrecognized configuration variables */

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

typedef struct privdata {
    List	*outgoing_queue;
    long	connection_thread;
    int		shutdown; /* Signal to the connection thread to shut down */
    int		listening_socket; /* File descriptor */
    int		port;		  /* Port number to listen */
    Octstr	*allow_ip, *deny_ip;
} PrivData;


static int fake_open_connection(SMSCConn *conn, PrivData *privdata)
{
    int s;

    if ((s = make_server_socket(privdata->port, (conn->our_host ? octstr_get_cstr(conn->our_host) : NULL))) == -1) {
        error(0, "smsc_fake: could not create listening socket in port %d",
	          privdata->port);
        return -1;
    }
    if (socket_set_blocking(s, 0) == -1) {
        error(0, "smsc_fake: couldn't make listening socket port %d non-blocking",
	          privdata->port);
        return -1;
    }
    privdata->listening_socket = s;
    return 0;
}


static int sms_to_client(Connection *client, Msg *msg)
{
    Octstr *line;
    Octstr *msgdata = NULL; /* NULL to allow octstr_destroy */

    debug("bb.sms", 0, "smsc_fake: sending message to client");
    /* msg_dump(msg, 0); */

    line = octstr_duplicate(msg->sms.sender);
    octstr_append_char(line, ' ');
    octstr_append(line, msg->sms.receiver);
    if (octstr_len(msg->sms.udhdata)) {
        octstr_append(line, octstr_imm(" udh "));
        msgdata = octstr_duplicate(msg->sms.udhdata);
        octstr_url_encode(msgdata);
        octstr_append(line, msgdata);
        octstr_destroy(msgdata);
        octstr_append(line, octstr_imm(" data "));
        msgdata = octstr_duplicate(msg->sms.msgdata);
        octstr_url_encode(msgdata);
        octstr_append(line, msgdata);
    } else {
    	if (msg->sms.coding == DC_8BIT) {
            octstr_append(line, octstr_imm(" data "));
            msgdata = octstr_duplicate(msg->sms.msgdata);
            octstr_url_encode(msgdata);
            octstr_append(line, msgdata);
    	}
    	else if (msg->sms.coding == DC_UCS2) {
            octstr_append(line, octstr_imm(" ucs-2 "));
            msgdata = octstr_duplicate(msg->sms.msgdata);
            octstr_url_encode(msgdata);
            octstr_append(line, msgdata);
    	}
    	else {
            octstr_append(line, octstr_imm(" text "));
            octstr_append(line, msg->sms.msgdata);
    	}
    }

    octstr_append_char(line, 10);

    if (conn_write(client, line) == -1) {
        octstr_destroy(msgdata);
        octstr_destroy(line);
        return -1;
    }
    octstr_destroy(msgdata);
    octstr_destroy(line);
    return 1;
}


static void msg_to_bb(SMSCConn *conn, Octstr *line)
{
    long p, p2;
    Msg *msg;
    Octstr *type = NULL; /* might be destroyed after error before created */

    msg = msg_create(sms);
    p = octstr_search_char(line, ' ', 0);
    if (p == -1)
        goto error;
    msg->sms.sender = octstr_copy(line, 0, p);
    p2 = octstr_search_char(line, ' ', p + 1);
    if (p2 == -1)
        goto error;
    msg->sms.receiver = octstr_copy(line, p + 1, p2 - p - 1);
    p = octstr_search_char(line, ' ', p2 + 1);
    if (p == -1)
        goto error;
    type = octstr_copy(line, p2 + 1, p - p2 - 1);
    if (!octstr_compare(type, octstr_imm("text"))) {
        msg->sms.msgdata = octstr_copy(line, p + 1, LONG_MAX);
        msg->sms.coding = DC_7BIT;
    }
    else if (!octstr_compare(type, octstr_imm("data"))) {
        msg->sms.msgdata = octstr_copy(line, p + 1, LONG_MAX);
        msg->sms.coding = DC_8BIT;
        if (octstr_url_decode(msg->sms.msgdata) == -1)
            warning(0, "smsc_fake: urlcoded data from client looks malformed");
    }
    else if (!octstr_compare(type, octstr_imm("route"))) {
        p2 = octstr_search_char(line, ' ', p + 1);
        if (p2 == -1)
            goto error;
        msg->sms.boxc_id = octstr_copy(line, p + 1, p2 - p - 1);
        msg->sms.msgdata = octstr_copy(line, p2 + 1, LONG_MAX);
    }
    else if (!octstr_compare(type, octstr_imm("udh"))) {
        p2 = octstr_search_char(line, ' ', p + 1);
        if (p2 == -1)
            goto error;
        msg->sms.udhdata = octstr_copy(line, p + 1, p2 - p - 1);
        msg->sms.msgdata = octstr_copy(line, p2 + 1, LONG_MAX);
        if (msg->sms.coding == DC_UNDEF)
            msg->sms.coding = DC_8BIT;
        if (octstr_url_decode(msg->sms.msgdata) == -1 ||
            octstr_url_decode(msg->sms.udhdata) == -1)
            warning(0, "smsc_fake: urlcoded data from client looks malformed");
    }
    else if (!octstr_compare(type, octstr_imm("dlr-mask"))) {
        Octstr *tmp;
        p2 = octstr_search_char(line, ' ', p + 1);
        if (p2 == -1)
            goto error;
        tmp = octstr_copy(line, p + 1, p2 - p - 1);
        msg->sms.dlr_mask = atoi(octstr_get_cstr(tmp));
        octstr_destroy(tmp);
        msg->sms.msgdata = octstr_copy(line, p2 + 1, LONG_MAX);
    }
    else
        goto error;
    octstr_destroy(line);
    octstr_destroy(type);
    time(&msg->sms.time);
    msg->sms.smsc_id = octstr_duplicate(conn->id);

    debug("bb.sms", 0, "smsc_fake: new message received");
    /* msg_dump(msg, 0); */
    bb_smscconn_receive(conn, msg);
    return;
error:
    warning(0, "smsc_fake: invalid message syntax from client, ignored");
    msg_destroy(msg);
    octstr_destroy(line);
    octstr_destroy(type);
    return;
}


static void main_connection_loop(SMSCConn *conn, Connection *client)
{
    PrivData *privdata = conn->data;
    Octstr *line;
    Msg	*msg;
    double delay = 0;

    if (conn->throughput > 0) {
        delay = 1.0 / conn->throughput;
    }

    while (1) {
        while (!conn->is_stopped && !privdata->shutdown &&
                (line = conn_read_line(client)))
            msg_to_bb(conn, line);
        if (conn_error(client))
            goto error;
        if (conn_eof(client))
            goto eof;

        /* 
         * We won't get DLRs from fakesmsc itself, due that we don't have
         * corresponding message IDs etc. We threat the DLR receiving here. So
         * DLR "originate" from the protocol layer towards abstraction layer.
         * This is all for pure debugging and testing.
         */

        while ((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL) {

            /* pass msg to fakesmsc daemon */            
            if (sms_to_client(client, msg) == 1) {
                Msg *copy = msg_duplicate(msg);
                
                /* 
                 * Actually no guarantee of it having been really sent,
                 * but I suppose that doesn't matter since this interface
                 * is just for debugging anyway. The upper layer will send
                 * a SMSC success DLR if mask is set. Be aware that msg is
                 * destroyed in abstraction layer, that's why we use a copy
                 * afterwards to handle the final DLR. 
                 */
                bb_smscconn_sent(conn, msg, NULL);

                /* and now the final DLR */
                if (DLR_IS_SUCCESS_OR_FAIL(copy->sms.dlr_mask)) {
                    Msg *dlrmsg;
                    Octstr *tmp;
                    int dlrstat = DLR_SUCCESS;
                    char id[UUID_STR_LEN + 1];

                    uuid_unparse(copy->sms.id, id);
                    tmp = octstr_create(id);
                    dlrmsg = dlr_find(conn->id,
                                      tmp, /* smsc message id */
                                      copy->sms.receiver, /* destination */
                                      dlrstat, 0);
                    if (dlrmsg != NULL) {
                        /* XXX TODO: Provide a SMPP DLR text in msgdata */
                        bb_smscconn_receive(conn, dlrmsg);
                    } else {
                        error(0,"smsc_fale: got DLR but could not find message or "
                        		"was not interested in it");
                    }
                    octstr_destroy(tmp);
                }
                msg_destroy(copy);

            } else {
                bb_smscconn_send_failed(conn, msg,
		            SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));
                goto error;
            }

            /* obey throughput speed limit, if any */
            if (conn->throughput > 0) {
                gwthread_sleep(delay);
            }
        }
        if (privdata->shutdown) {
            debug("bb.sms", 0, "smsc_fake shutting down, closing client socket");
            conn_destroy(client);
            return;
        }
        conn_wait(client, -1);
        if (conn_error(client))
            goto error;
        if (conn_eof(client))
            goto eof;
    }
error:
    info(0, "IO error to fakesmsc client. Closing connection.");
    conn_destroy(client);
    return;
eof:
    info(0, "EOF from fakesmsc client. Closing connection.");
    conn_destroy(client);
    return;
}


static void fake_listener(void *arg)
{
    SMSCConn *conn = arg;
    PrivData *privdata = conn->data;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    Octstr *ip;
    Connection *client;
    int s, ret;
    Msg	*msg;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    while (1) {
        client_addr_len = sizeof(client_addr);
        ret = gwthread_pollfd(privdata->listening_socket, POLLIN, -1);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            error(0, "Poll for fakesmsc connections failed, shutting down");
            break;
        }
        if (privdata->shutdown)
            break;
        if (ret == 0) 
            /* 
             * This thread was woke up from elsewhere, but
             * if we're not shutting down nothing to do here. 
             */
            continue;
        s = accept(privdata->listening_socket, (struct sockaddr *)&client_addr,
                   &client_addr_len);
        if (s == -1) {
            warning(errno, "fake_listener: accept() failed, retrying...");
            continue;
        }
        ip = host_ip(client_addr);
        if (!is_allowed_ip(privdata->allow_ip, privdata->deny_ip, ip)) {
            info(0, "Fakesmsc connection tried from denied host <%s>, "
                    "disconnected", octstr_get_cstr(ip));
            octstr_destroy(ip);
            close(s);
            continue;
        }
        client = conn_wrap_fd(s, 0);
        if (client == NULL) {
            error(0, "fake_listener: conn_wrap_fd failed on accept()ed fd");
            octstr_destroy(ip);
            close(s);
            continue;
        }
        conn_claim(client);
        info(0, "Fakesmsc client connected from %s", octstr_get_cstr(ip));
        octstr_destroy(ip);
        mutex_lock(conn->flow_mutex);
        conn->status = SMSCCONN_ACTIVE;
        conn->connect_time = time(NULL);
        mutex_unlock(conn->flow_mutex);
        bb_smscconn_connected(conn);

        main_connection_loop(conn, client);

        if (privdata->shutdown)
            break;
        mutex_lock(conn->flow_mutex);
        conn->status = SMSCCONN_RECONNECTING;
        mutex_unlock(conn->flow_mutex);
        while ((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL) {
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_TEMPORARILY, NULL);
        }
    }
    if (close(privdata->listening_socket) == -1)
        warning(errno, "smsc_fake: couldn't close listening socket at shutdown");
    mutex_lock(conn->flow_mutex);

    conn->status = SMSCCONN_DEAD;

    while ((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL) {
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
    }
    gwlist_destroy(privdata->outgoing_queue, NULL);
    octstr_destroy(privdata->allow_ip);
    octstr_destroy(privdata->deny_ip);
    gw_free(privdata);
    conn->data = NULL;

    mutex_unlock(conn->flow_mutex);
    debug("bb.sms", 0, "smsc_fake connection has completed shutdown.");
    bb_smscconn_killed();
}


static int add_msg_cb(SMSCConn *conn, Msg *sms)
{
    PrivData *privdata = conn->data;
    Msg *copy;

    copy = msg_duplicate(sms);
  
    /*  
     * Send DLR if desired, which means first add the DLR entry 
     * and then later find it and remove it. We need to ensure
     * that we put the DLR in first before producing the copy
     * to the list.
     */
    if (DLR_IS_ENABLED_DEVICE(sms->sms.dlr_mask)) {
        Octstr *tmp;
        char id[UUID_STR_LEN + 1];
        uuid_unparse(sms->sms.id, id);
        tmp = octstr_format("%s", id);
        dlr_add(conn->id, tmp, sms, 0);
        octstr_destroy(tmp);
    }
    gwlist_produce(privdata->outgoing_queue, copy);

    gwthread_wakeup(privdata->connection_thread);

    return 0;
}


static int shutdown_cb(SMSCConn *conn, int finish_sending)
{
    PrivData *privdata = conn->data;

    debug("bb.sms", 0, "Shutting down SMSCConn FAKE, %s",
          finish_sending ? "slow" : "instant");

    /* 
     * Documentation claims this would have been done by smscconn.c,
     * but isn't when this code is being written. 
     */
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    privdata->shutdown = 1; 
    /*
     * Separate from why_killed to avoid locking, as
     * why_killed may be changed from outside? 
     */

    if (finish_sending == 0) {
        Msg *msg;
        while((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL) {
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
        }
    }

    gwthread_wakeup(privdata->connection_thread);
    return 0;
}


static void start_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;

    /* in case there are messages in the buffer already */
    gwthread_wakeup(privdata->connection_thread);
    debug("bb.sms", 0, "smsc_fake: start called");
}


static long queued_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    long ret;
    
    ret = (privdata ? gwlist_len(privdata->outgoing_queue) : 0);

    /* use internal queue as load, maybe something else later */

    conn->load = ret;
    return ret;
}


int smsc_fake_create(SMSCConn *conn, CfgGroup *cfg)
{
    PrivData *privdata = NULL;
    Octstr *allow_ip, *deny_ip;
    long portno;   /* has to be long because of cfg_get_integer */

    if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1)
        portno = 0;
    allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    if (allow_ip)
        deny_ip = octstr_create("*.*.*.*");
    else
        deny_ip = NULL;

    if (portno == 0) {
        error(0, "'port' invalid in 'fake' record.");
        goto error;
    }
    privdata = gw_malloc(sizeof(PrivData));
    privdata->listening_socket = -1;

    privdata->port = portno;
    privdata->allow_ip = allow_ip;
    privdata->deny_ip = deny_ip;

    if (fake_open_connection(conn, privdata) < 0) {
        gw_free(privdata);
        privdata = NULL;
        goto error;
    }

    conn->data = privdata;

    conn->name = octstr_format("FAKE:%d", privdata->port);

    privdata->outgoing_queue = gwlist_create();
    privdata->shutdown = 0;

    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = time(NULL);

    if ((privdata->connection_thread = gwthread_create(fake_listener, conn)) == -1)
        goto error;

    conn->shutdown = shutdown_cb;
    conn->queued = queued_cb;
    conn->start_conn = start_cb;
    conn->send_msg = add_msg_cb;

    return 0;

error:
    error(0, "Failed to create fake smsc connection");
    if (privdata != NULL) {
        gwlist_destroy(privdata->outgoing_queue, NULL);
        if (close(privdata->listening_socket == -1)) {
            error(errno, "smsc_fake: closing listening socket port %d failed",
                  privdata->listening_socket);
        }
    }
    gw_free(privdata);
    octstr_destroy(allow_ip);
    octstr_destroy(deny_ip);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    return -1;
}
