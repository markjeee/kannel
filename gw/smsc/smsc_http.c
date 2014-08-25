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
 * smsc_http.c - interface to various HTTP based content/SMS gateways
 *
 * HTTP based "SMSC Connection" is meant for gateway connections,
 * and has following features:
 *
 * o Kannel listens to certain (HTTP server) port for MO SMS messages.
 *   The exact format of these HTTP calls are defined by type of HTTP based
 *   connection. Kannel replies to these messages as ACK, but does not
 *   support immediate reply. Thus, if Kannel is linked to another Kannel,
 *   only 'max-messages = 0' services are practically supported - any
 *   replies must be done with SMS PUSH (sendsms)
 *
 * o For MT messages, Kannel does HTTP GET or POST to given address, in format
 *   defined by type of HTTP based protocol
 *
 * The 'type' of requests and replies are defined by 'system-type' variable.
 * The only type of HTTP requests currently supported are basic Kannel.
 * If new support is added, smsc_http_create is modified accordingly and new
 * functions added.
 *
 *
 * KANNEL->KANNEL linking: (UDH not supported in MO messages)
 *
 *****
 * FOR CLIENT/END-POINT KANNEL:
 *
 *  group = smsc
 *  smsc = http
 *  system-type = kannel
 *  port = NNN
 *  smsc-username = XXX
 *  smsc-password = YYY
 *  send-url = "server.host:PORT"
 *
 *****
 * FOR SERVER/RELAY KANNEL:
 *
 *  group = smsbox
 *  sendsms-port = PORT
 *  ...
 *
 *  group = sms-service
 *  keyword = ...
 *  url = "client.host:NNN/sms?user=XXX&pass=YYY&from=%p&to=%P&text=%a"
 *  max-messages = 0
 *
 *  group = send-sms
 *  username = XXX
 *  password = YYY
 *
 * Kalle Marjola for Project Kannel 2001
 * Stipe Tolj <st@tolj.org>
 * Alexander Malysh <amalysh at kannel.org>
 * Tobias Weber <weber@wapme.de>
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
#include "urltrans.h"
#include "meta_data.h"

#define DEFAULT_CHARSET "UTF-8"

/* callback functions set by HTTP-SMSC type */
struct smsc_http_fn_callbacks {
    int (*init) (SMSCConn *conn, CfgGroup *cfg);
    void (*destroy) (SMSCConn *conn);
    int (*send_sms) (SMSCConn *conn, Msg *msg);
    void (*parse_reply) (SMSCConn *conn, Msg *msg, int status, List *headers, Octstr *body);
    void (*receive_sms) (SMSCConn *conn, HTTPClient *client, List *headers, Octstr *body, List *cgivars);
};

typedef struct conndata {
    HTTPCaller *http_ref;
    long receive_thread;
    long send_cb_thread;
    long sender_thread;
    volatile int shutdown;
    long port;   /* port for receiving SMS'es */
    Octstr *allow_ip;
    Octstr *send_url;
    Octstr *dlr_url;
    Counter *open_sends;
    Semaphore *max_pending_sends;
    Octstr *username;   /* if needed */
    Octstr *password;   /* as said */
    Octstr *system_id;	/* api id for clickatell */
    int no_sender;      /* ditto */
    int no_coding;      /* this, too */
    int no_sep;         /* not to mention this */
    Octstr *proxy;      /* proxy a constant string */
    Octstr *alt_charset;    /* alternative charset use */
    List *msg_to_send; /* our send queue */



    /* callback functions set by HTTP-SMSC type */
    struct smsc_http_fn_callbacks *callbacks;

    /* submodule specific data */
    void *data;
} ConnData;


static void conndata_destroy(ConnData *conndata)
{
    if (conndata == NULL)
        return;
    if (conndata->http_ref)
        http_caller_destroy(conndata->http_ref);
    octstr_destroy(conndata->allow_ip);
    octstr_destroy(conndata->send_url);
    octstr_destroy(conndata->dlr_url);
    octstr_destroy(conndata->username);
    octstr_destroy(conndata->password);
    octstr_destroy(conndata->proxy);
    octstr_destroy(conndata->system_id);
    octstr_destroy(conndata->alt_charset);
    counter_destroy(conndata->open_sends);
    gwlist_destroy(conndata->msg_to_send, NULL);
    if (conndata->max_pending_sends)
        semaphore_destroy(conndata->max_pending_sends);

    gw_free(conndata);
}


/*
 * Thread to listen to HTTP requests from SMSC entity
 */
static void httpsmsc_receiver(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    HTTPClient *client;
    Octstr *ip, *url, *body;
    List *headers, *cgivars;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    while (conndata->shutdown == 0) {
        /* reset */
        ip = url = body = NULL;
        headers = cgivars = NULL;

        /* XXX if conn->is_stopped, do not receive new messages.. */

        client = http_accept_request(conndata->port, &ip, &url,
                                     &headers, &body, &cgivars);
        if (client == NULL)
            break;

        if (cgivars != NULL) {
        	octstr_append_char(url, '?');
        	http_cgivar_dump_into(cgivars, url);
        }

        debug("smsc.http", 0, "HTTP[%s]: Got request `%s'",
              octstr_get_cstr(conn->id), octstr_get_cstr(url));

        if (connect_denied(conndata->allow_ip, ip)) {
            info(0, "HTTP[%s]: Connection `%s' tried from denied "
                    "host %s, ignored", octstr_get_cstr(conn->id),
                    octstr_get_cstr(url), octstr_get_cstr(ip));
            http_close_client(client);
        } else
            conndata->callbacks->receive_sms(conn, client, headers, body, cgivars);

        debug("smsc.http", 0, "HTTP[%s]: Destroying client information",
              octstr_get_cstr(conn->id));
        octstr_destroy(url);
        octstr_destroy(ip);
        octstr_destroy(body);
        http_destroy_headers(headers);
        http_destroy_cgiargs(cgivars);
    }
    debug("smsc.http", 0, "HTTP[%s]: httpsmsc_receiver dying",
          octstr_get_cstr(conn->id));

    conndata->shutdown = 1;
    http_close_port(conndata->port);

    /* unblock http_receive_result() if there are no open sends */
    if (counter_value(conndata->open_sends) == 0)
        http_caller_signal_shutdown(conndata->http_ref);

    if (conndata->sender_thread != -1) {
        gwthread_wakeup(conndata->sender_thread);
        gwthread_join(conndata->sender_thread);
    }
    if (conndata->send_cb_thread != -1) {
        gwthread_wakeup(conndata->send_cb_thread);
        gwthread_join(conndata->send_cb_thread);
    }

    mutex_lock(conn->flow_mutex);
    conn->status = SMSCCONN_DEAD;
    mutex_unlock(conn->flow_mutex);

    if (conndata->callbacks != NULL && conndata->callbacks->destroy != NULL)
        conndata->callbacks->destroy(conn);
    conn->data = NULL;
    conndata_destroy(conndata);
    bb_smscconn_killed();
}


/*
 * Thread to send queued messages
 */
static void httpsmsc_sender(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    Msg *msg;
    double delay = 0;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    if (conn->throughput) {
        delay = 1.0 / conn->throughput;
    }

    while (conndata->shutdown == 0) {
        /* check if we can send ; otherwise block on semaphore */
        if (conndata->max_pending_sends)
            semaphore_down(conndata->max_pending_sends);

        if (conndata->shutdown) {
            if (conndata->max_pending_sends)
                semaphore_up(conndata->max_pending_sends);
            break;
        }

        msg = gwlist_consume(conndata->msg_to_send);
        if (msg == NULL)
            break;

        /* obey throughput speed limit, if any */
        if (conn->throughput > 0) {
            gwthread_sleep(delay);
        }
        counter_increase(conndata->open_sends);
        if (conndata->callbacks->send_sms(conn, msg) == -1) {
            counter_decrease(conndata->open_sends);
            if (conndata->max_pending_sends)
                semaphore_up(conndata->max_pending_sends);
        }
    }

    /* put outstanding sends back into global queue */
    while((msg = gwlist_extract_first(conndata->msg_to_send)))
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);

    /* if there no receiver shutdown */
    if (conndata->port <= 0) {
        /* unblock http_receive_result() if there are no open sends */
        if (counter_value(conndata->open_sends) == 0)
            http_caller_signal_shutdown(conndata->http_ref);

        if (conndata->send_cb_thread != -1) {
            gwthread_wakeup(conndata->send_cb_thread);
            gwthread_join(conndata->send_cb_thread);
        }
        mutex_lock(conn->flow_mutex);
        conn->status = SMSCCONN_DEAD;
        mutex_unlock(conn->flow_mutex);

        if (conndata->callbacks != NULL && conndata->callbacks->destroy != NULL)
            conndata->callbacks->destroy(conn);
        conn->data = NULL;
        conndata_destroy(conndata);
        bb_smscconn_killed();
    }
}

/*
 * Thread to handle finished sendings
 */
static void httpsmsc_send_cb(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    Msg *msg;
    int status;
    List *headers;
    Octstr *final_url, *body;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    while(conndata->shutdown == 0 || counter_value(conndata->open_sends)) {

        msg = http_receive_result(conndata->http_ref, &status,
                                  &final_url, &headers, &body);

        if (msg == NULL)
            break;  /* they told us to die, by unlocking */

        counter_decrease(conndata->open_sends);
        if (conndata->max_pending_sends)
            semaphore_up(conndata->max_pending_sends);

        /* Handle various states here. */

        /* request failed and we are not in shutdown mode */
        if (status == -1 && conndata->shutdown == 0) {
            error(0, "HTTP[%s]: Couldn't connect to SMS center."
                     "(retrying in %ld seconds) %ld.",
                     octstr_get_cstr(conn->id), conn->reconnect_delay, counter_value(conndata->open_sends));
            mutex_lock(conn->flow_mutex);
            conn->status = SMSCCONN_RECONNECTING;
            mutex_unlock(conn->flow_mutex);
            /* XXX how should we know whether it's temp. error ?? */
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_TEMPORARILY, NULL);
            /*
             * Just sleep reconnect delay and set conn to ACTIVE again;
             * otherwise if no pending request are here, we leave conn in
             * RECONNECTING state for ever and no routing (trials) take place.
             */
            if (counter_value(conndata->open_sends) == 0) {
                gwthread_sleep(conn->reconnect_delay);
                /* and now enable routing again */
                mutex_lock(conn->flow_mutex);
                conn->status = SMSCCONN_ACTIVE;
                time(&conn->connect_time);
                mutex_unlock(conn->flow_mutex);
                /* tell bearerbox core that we are connected again */
                bb_smscconn_connected(conn);
            }
            continue;
        }
        /* request failed and we *are* in shutdown mode, drop the message */
        else if (status == -1 && conndata->shutdown == 1) {
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
        }
        /* request succeeded */
        else {
            /* we received a response, so this link is considered online again */
            if (conn->status != SMSCCONN_ACTIVE) {
                mutex_lock(conn->flow_mutex);
                conn->status = SMSCCONN_ACTIVE;
                time(&conn->connect_time);
                mutex_unlock(conn->flow_mutex);
                /* tell bearerbox core that we are connected again */
                bb_smscconn_connected(conn);
            }
            conndata->callbacks->parse_reply(conn, msg, status, headers, body);
        }

        http_destroy_headers(headers);
        octstr_destroy(final_url);
        octstr_destroy(body);
    }
    debug("smsc.http", 0, "HTTP[%s]: httpsmsc_send_cb dying",
          octstr_get_cstr(conn->id));
    conndata->shutdown = 1;

    if (counter_value(conndata->open_sends)) {
        warning(0, "HTTP[%s]: Shutdown while <%ld> requests are pending.",
                octstr_get_cstr(conn->id), counter_value(conndata->open_sends));
    }
}


/*----------------------------------------------------------------
 * SMSC-type specific functions
 *
 * 3 functions are needed for each:
 *
 *   1) send SMS
 *   2) parse send SMS result
 *   3) receive SMS (and send reply)
 *
 *   These functions do not return anything and do not destroy
 *   arguments. They must handle everything that happens therein
 *   and must call appropriate bb_smscconn functions
 */

/*----------------------------------------------------------------
 * Kannel
 * 
 * This type allows concatenation of Kannel instances, ie:
 * 
 *  <smsc>--<bearerbox2><smsbox2>--HTTP--<smsc_http><bearerbox1><smsbox1>
 * 
 * Where MT messages are injected via the sendsms HTTP interface at smsbox1,
 * forwarded to bearerbo1 and routed via the SMSC HTTP type kannel to 
 * sendsms HTTP interface of smsbox2 and further on.
 * 
 * This allows chaining of Kannel instances for MO and MT traffic.
 * 
 * DLR handling:
 * For DLR handling we have the usual effect that the "last" smsbox instance
 * of the chain is signaling the DLR-URL, since the last instance receives
 * the DLR from the upstream SMSC and the associated smsbox triggers the
 * DLR-URL.
 * For some considerations this is not what we want. If we want to transport
 * the orginal DLR-URL to the "first" smsbox instance of the calling chain
 * then we need to define a 'dlr-url' config directive in the smsc group.
 * This value defines the inbound MO/DLR port of our own smsc group and
 * maps arround the orginal DLR-URL. So the next smsbox does not signal the
 * orginal DLR-URL, but our own smsc group instance with the DLR, and we can
 * forward on to smsbox and possibly further on the chain to the first
 * instance.
 * 
 * Example: (consider the 2 chain architecture from above)
 * A MT is put to smsbox1 with dlr-url=http://foobar/aaa as DLR-URL. The MT
 * is forwarded to bearerbox.
 * If no 'dlr-url' is given in the smsc HTTP for the next smsbox2, then we
 * simply pass the same value to smsbox2. Resulting that smsbox2 will call
 * the DLR-URL when we receive a DLR from the upstream SMSC connection of
 * bearerbox2.
 * If 'dlr-url = http://localhost:15015/' is given in the smsc HTTP for the
 * next smsbox2, then we map the orginal DLR-URL into this value, resulting
 * in a dlr-url=http://lcoalhost:15015/&dlr-url=http://foobar/aaa call to
 * smsbox2. So smsbox2 is not signaling http://foobar/aaa directly, but our
 * own bearerbox1's smsc HTTP port for MO/DLR receive.
 */

enum { HEX_NOT_UPPERCASE = 0 };


static int kannel_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url;
    List *headers;

    if (!conndata->no_sep) {
        url = octstr_format("%S?"
			    "username=%E&password=%E&to=%E&text=%E",
			     conndata->send_url,
			     conndata->username, conndata->password,
			     sms->sms.receiver, sms->sms.msgdata);
    } else {
        Octstr *msgdata = octstr_duplicate(sms->sms.msgdata);
        
        octstr_binary_to_hex(msgdata, HEX_NOT_UPPERCASE);
        url = octstr_format("%S?"
			    "username=%E&password=%E&to=%E&text=%S",
			     conndata->send_url,
			     conndata->username, conndata->password,
			     sms->sms.receiver, msgdata);
        octstr_destroy(msgdata);
    }   

    if (octstr_len(sms->sms.udhdata)) {
        if (!conndata->no_sep) {
            octstr_format_append(url, "&udh=%E", sms->sms.udhdata);
        } else {
            Octstr *udhdata = octstr_duplicate(sms->sms.udhdata);
            
            octstr_binary_to_hex(udhdata, HEX_NOT_UPPERCASE);
            octstr_format_append(url, "&udh=%S", udhdata);
            octstr_destroy(udhdata);
        }
    }

    if (!conndata->no_sender)
        octstr_format_append(url, "&from=%E", sms->sms.sender);
    if (sms->sms.mclass != MC_UNDEF)
        octstr_format_append(url, "&mclass=%d", sms->sms.mclass);
    if (!conndata->no_coding && sms->sms.coding != DC_UNDEF)
        octstr_format_append(url, "&coding=%d", sms->sms.coding);

    /* Obey that smsbox's sendsms HTTP interface is still expecting 
     * WINDOWS-1252 as default charset, while all other internal parts
     * use UTF-8 as internal encoding. This means, when we pass a SMS
     * into a next Kannel instance, we need to let the smsbox know which
     * charset we have in use.
     * XXX TODO: change smsbox interface to use UTF-8 as default
     * in next major release. */
    if (sms->sms.coding == DC_7BIT)
        octstr_append_cstr(url, "&charset=UTF-8");
    else if (sms->sms.coding == DC_UCS2)
        octstr_append_cstr(url, "&charset=UTF-16BE");

    if (sms->sms.mwi != MWI_UNDEF)
        octstr_format_append(url, "&mwi=%d", sms->sms.mwi);
    if (sms->sms.account) /* prepend account with local username */
        octstr_format_append(url, "&account=%E:%E", sms->sms.service, sms->sms.account);
    if (sms->sms.binfo) /* prepend billing info */
        octstr_format_append(url, "&binfo=%S", sms->sms.binfo);
    if (sms->sms.smsc_id) /* proxy the smsc-id to the next instance */
        octstr_format_append(url, "&smsc=%S", sms->sms.smsc_id);
	if (conndata->dlr_url) {
		char id[UUID_STR_LEN + 1];
		Octstr *mid;

		/* create Octstr from UUID */
		uuid_unparse(sms->sms.id, id);
		mid = octstr_create(id);

		octstr_format_append(url, "&dlr-url=%E", conndata->dlr_url);

		/* encapsulate the original DLR-URL, escape code for DLR mask
		 * and message id */
		octstr_format_append(url, "%E%E%E%E%E",
			octstr_imm("&dlr-url="), sms->sms.dlr_url != NULL ? sms->sms.dlr_url : octstr_imm(""),
			octstr_imm("&dlr-mask=%d"),
			octstr_imm("&dlr-mid="), mid);

		octstr_destroy(mid);
	} else if (sms->sms.dlr_url != NULL)
		octstr_format_append(url, "&dlr-url=%E", sms->sms.dlr_url);
    if (sms->sms.dlr_mask != DLR_UNDEFINED && sms->sms.dlr_mask != DLR_NOTHING)
        octstr_format_append(url, "&dlr-mask=%d", sms->sms.dlr_mask);

    if (sms->sms.validity != SMS_PARAM_UNDEFINED)
    	octstr_format_append(url, "&validity=%ld", (sms->sms.validity - time(NULL)) / 60);
    if (sms->sms.deferred != SMS_PARAM_UNDEFINED)
    	octstr_format_append(url, "&deferred=%ld", (sms->sms.deferred - time(NULL)) / 60);

    headers = gwlist_create();
    debug("smsc.http.kannel", 0, "HTTP[%s]: Start request",
          octstr_get_cstr(conn->id));
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, 
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);

    return 0;
}

static void kannel_parse_reply(SMSCConn *conn, Msg *msg, int status,
			       List *headers, Octstr *body)
{
    /* Test on three cases:
     * 1. an smsbox reply of an remote kannel instance
     * 2. an smsc_http response (if used for MT to MO looping)
     * 3. an smsbox reply of partly successful sendings */
    if ((status == HTTP_OK || status == HTTP_ACCEPTED)
        && (octstr_case_compare(body, octstr_imm("0: Accepted for delivery")) == 0 ||
            octstr_case_compare(body, octstr_imm("Sent.")) == 0 ||
            octstr_case_compare(body, octstr_imm("Ok.")) == 0 ||
            octstr_ncompare(body, octstr_imm("Result: OK"),10) == 0)) {
        char id[UUID_STR_LEN + 1];
        Octstr *mid;

        /* create Octstr from UUID */  
        uuid_unparse(msg->sms.id, id);
        mid = octstr_create(id); 
    
        /* add to our own DLR storage */               
        if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask))
            dlr_add(conn->id, mid, msg, 0);

        octstr_destroy(mid);            
            
        bb_smscconn_sent(conn, msg, NULL);
    } else {
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
    }
}


static void kannel_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    Octstr *user, *pass, *from, *to, *text, *udh, *account, *binfo;
    Octstr *dlrmid, *dlrerr;
    Octstr *tmp_string, *retmsg;
    int	mclass, mwi, coding, validity, deferred, dlrmask;
    List *reply_headers;
    int ret;
	
    mclass = mwi = coding = validity = 
        deferred = dlrmask = SMS_PARAM_UNDEFINED;

    user = http_cgi_variable(cgivars, "username");
    pass = http_cgi_variable(cgivars, "password");
    from = http_cgi_variable(cgivars, "from");
    to = http_cgi_variable(cgivars, "to");
    text = http_cgi_variable(cgivars, "text");
    udh = http_cgi_variable(cgivars, "udh");
    account = http_cgi_variable(cgivars, "account");
    binfo = http_cgi_variable(cgivars, "binfo");
    dlrmid = http_cgi_variable(cgivars, "dlr-mid");
    tmp_string = http_cgi_variable(cgivars, "flash");
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);
    }
    tmp_string = http_cgi_variable(cgivars, "mclass");
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);
    }
    tmp_string = http_cgi_variable(cgivars, "mwi");
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &mwi);
    }
    tmp_string = http_cgi_variable(cgivars, "coding");
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &coding);
    }
    tmp_string = http_cgi_variable(cgivars, "validity");
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &validity);
    }
    tmp_string = http_cgi_variable(cgivars, "deferred");
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &deferred);
    }
    tmp_string = http_cgi_variable(cgivars, "dlr-mask");
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &dlrmask);
    }
    dlrerr = http_cgi_variable(cgivars, "dlr-err");

    debug("smsc.http.kannel", 0, "HTTP[%s]: Received an HTTP request",
          octstr_get_cstr(conn->id));
    
    if (user == NULL || pass == NULL ||
	    octstr_compare(user, conndata->username) != 0 ||
	    octstr_compare(pass, conndata->password) != 0) {

        error(0, "HTTP[%s]: Authorization failure",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Authorization failed for sendsms");
    } else if (dlrmask != 0 && dlrmid != NULL) {
        /* we got a DLR, and we don't require additional values */
        Msg *dlrmsg;
        
        dlrmsg = dlr_find(conn->id,
            dlrmid, /* message id */
            to, /* destination */
            dlrmask, 0);

        if (dlrmsg != NULL) {
            dlrmsg->sms.sms_type = report_mo;
            dlrmsg->sms.msgdata = octstr_duplicate(text);
            dlrmsg->sms.account = octstr_duplicate(conndata->username);

            debug("smsc.http.kannel", 0, "HTTP[%s]: Received DLR for DLR-URL <%s>",
                  octstr_get_cstr(conn->id), octstr_get_cstr(dlrmsg->sms.dlr_url));

            if (dlrerr != NULL) {
                /* pass errorcode as is */
            	if (dlrmsg->sms.meta_data == NULL)
            		dlrmsg->sms.meta_data = octstr_create("");

                meta_data_set_value(dlrmsg->sms.meta_data, METADATA_DLR_GROUP,
                                    octstr_imm(METADATA_DLR_GROUP_ERRORCODE), dlrerr, 1);
            }
            
            ret = bb_smscconn_receive(conn, dlrmsg);
            if (ret == -1)
                retmsg = octstr_create("Not accepted");
            else
                retmsg = octstr_create("Sent.");
        } else {
            error(0,"HTTP[%s]: Got DLR but could not find message or was not interested "
                  "in it id<%s> dst<%s>, type<%d>",
                  octstr_get_cstr(conn->id), octstr_get_cstr(dlrmid),
                  octstr_get_cstr(to), dlrmask);
            retmsg = octstr_create("Unknown DLR, not accepted");
        }                    
    }
    else if (from == NULL || to == NULL || text == NULL) {
	
        error(0, "HTTP[%s]: Insufficient args",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient args, rejected");
    }
    else if (udh != NULL && (octstr_len(udh) != octstr_get_char(udh, 0) + 1)) {
        error(0, "HTTP[%s]: UDH field misformed, rejected",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("UDH field misformed, rejected");
    }
    else if (udh != NULL && octstr_len(udh) > MAX_SMS_OCTETS) {
        error(0, "HTTP[%s]: UDH field is too long, rejected",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("UDH field is too long, rejected");
    }
    else {
        /* we got a normal MO SMS */
        Msg *msg;
        msg = msg_create(sms);

        debug("smsc.http.kannel", 0, "HTTP[%s]: Constructing new SMS",
              octstr_get_cstr(conn->id));
	
        msg->sms.service = octstr_duplicate(user);
        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.msgdata = octstr_duplicate(text);
        msg->sms.udhdata = octstr_duplicate(udh);

        msg->sms.smsc_id = octstr_duplicate(conn->id);
        msg->sms.time = time(NULL);
        msg->sms.mclass = mclass;
        msg->sms.mwi = mwi;
        msg->sms.coding = coding;
        msg->sms.validity = (validity == SMS_PARAM_UNDEFINED ? validity : time(NULL) + validity * 60);
        msg->sms.deferred = (deferred == SMS_PARAM_UNDEFINED ? deferred : time(NULL) + deferred * 60);
        msg->sms.account = octstr_duplicate(account);
        msg->sms.binfo = octstr_duplicate(binfo);
        ret = bb_smscconn_receive(conn, msg);
        if (ret == -1)
            retmsg = octstr_create("Not accepted");
        else
            retmsg = octstr_create("Sent.");
    }

    reply_headers = gwlist_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.kannel", 0, "HTTP[%s]: Sending reply",
          octstr_get_cstr(conn->id));
    http_send_reply(client, HTTP_ACCEPTED, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}

struct smsc_http_fn_callbacks smsc_http_kannel_callback = {
        .send_sms = kannel_send_sms,
        .parse_reply = kannel_parse_reply,
        .receive_sms = kannel_receive_sms,
};

/*-----------------------------------------------------------------
 * functions to implement various smscconn operations
 */

static int httpsmsc_send(SMSCConn *conn, Msg *msg)
{
    ConnData *conndata = conn->data;
    Msg *sms;


    /* don't crash if no send_sms handle defined */
    if (!conndata || !conndata->callbacks->send_sms)
        return -1;

    sms = msg_duplicate(msg);
    /* convert character encoding if required */
    if (msg->sms.coding == DC_7BIT && conndata->alt_charset &&
        charset_convert(sms->sms.msgdata, DEFAULT_CHARSET,
                        octstr_get_cstr(conndata->alt_charset)) != 0) {
        error(0, "Failed to convert msgdata from charset <%s> to <%s>, will send as is.",
              DEFAULT_CHARSET, octstr_get_cstr(conndata->alt_charset));
    }

    gwlist_produce(conndata->msg_to_send, sms);

    return 0;
}


static long httpsmsc_queued(SMSCConn *conn)
{
    ConnData *conndata = conn->data;

    return (conndata ? (conn->status != SMSCCONN_DEAD ? 
            counter_value(conndata->open_sends) : 0) : 0);
}


static int httpsmsc_shutdown(SMSCConn *conn, int finish_sending)
{
    ConnData *conndata = conn->data;

    if (conndata == NULL)
        return 0;

    debug("httpsmsc_shutdown", 0, "HTTP[%s]: Shutting down",
          octstr_get_cstr(conn->id));

    mutex_lock(conn->flow_mutex);
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

    conndata->shutdown = 1;

    if (conndata->port > 0)
        http_close_port(conndata->port);
    gwlist_remove_producer(conndata->msg_to_send);
    if (conndata->receive_thread != -1)
        gwthread_wakeup(conndata->receive_thread);
    if (conndata->sender_thread != -1)
        gwthread_wakeup(conndata->sender_thread);
    mutex_unlock(conn->flow_mutex);

    return 0;
}


#include "http/generic.c"
#include "http/brunet.c"
#include "http/xidris.c"
#include "http/clickatell.c"
#include "http/wapme.c"


int smsc_http_create(SMSCConn *conn, CfgGroup *cfg)
{
    ConnData *conndata = NULL;
    Octstr *type;
    int ssl = 0;   /* indicate if SSL-enabled server should be used */
    long max_ps;

    if ((type = cfg_get(cfg, octstr_imm("system-type"))) == NULL) {
        error(0, "HTTP[%s]: 'system-type' missing in smsc 'http' record.",
              octstr_get_cstr(conn->id));
        octstr_destroy(type);
        return -1;
    }

    conndata = gw_malloc(sizeof(ConnData));
    /* reset conndata */
    memset(conndata, 0, sizeof(ConnData));

    conn->data = conndata;
    conndata->http_ref = NULL;
    conndata->data = NULL;

    if (cfg_get_integer(&conndata->port, cfg, octstr_imm("port")) == -1) {
        warning(0, "HTTP[%s]: 'port' not set in smsc 'http' group.",
              octstr_get_cstr(conn->id));
        conndata->port = -1;
    }

    conndata->allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    conndata->send_url = cfg_get(cfg, octstr_imm("send-url"));
    conndata->username = cfg_get(cfg, octstr_imm("smsc-username"));
    conndata->password = cfg_get(cfg, octstr_imm("smsc-password"));
    conndata->system_id = cfg_get(cfg, octstr_imm("system-id"));
    cfg_get_bool(&conndata->no_sender, cfg, octstr_imm("no-sender"));
    cfg_get_bool(&conndata->no_coding, cfg, octstr_imm("no-coding"));
    cfg_get_bool(&conndata->no_sep, cfg, octstr_imm("no-sep"));
    conndata->proxy = cfg_get(cfg, octstr_imm("system-id"));
    cfg_get_bool(&ssl, cfg, octstr_imm("use-ssl"));
    conndata->dlr_url = cfg_get(cfg, octstr_imm("dlr-url"));
    conndata->alt_charset = cfg_get(cfg, octstr_imm("alt-charset"));

    if (cfg_get_integer(&max_ps, cfg, octstr_imm("max-pending-submits")) == -1 || max_ps < 1)
        max_ps = 10;
    
    conndata->max_pending_sends = semaphore_create(max_ps);

    if (conndata->port <= 0 && conndata->send_url == NULL) {
        error(0, "Sender and receiver disabled. Dummy SMSC not allowed.");
        goto error;
    }
    if (conndata->send_url == NULL)
        panic(0, "HTTP[%s]: Sending not allowed. No 'send-url' specified.",
              octstr_get_cstr(conn->id));

    if (octstr_case_compare(type, octstr_imm("kannel")) == 0) {
        if (conndata->username == NULL || conndata->password == NULL) {
            error(0, "HTTP[%s]: 'username' and 'password' required for Kannel http smsc",
                  octstr_get_cstr(conn->id));
            goto error;
        }
        conndata->callbacks = &smsc_http_kannel_callback;
    } else if (octstr_case_compare(type, octstr_imm("brunet")) == 0) {
        conndata->callbacks = &smsc_http_brunet_callback;
    } else if (octstr_case_compare(type, octstr_imm("xidris")) == 0) {
        conndata->callbacks = &smsc_http_xidris_callback;
    } else if (octstr_case_compare(type, octstr_imm("generic")) == 0) {
        conndata->callbacks = &smsc_http_generic_callback;
    } else if (octstr_case_compare(type, octstr_imm("clickatell")) == 0) {
        conndata->callbacks = &smsc_http_clickatell_callback;
    } else if (octstr_case_compare(type, octstr_imm("wapme")) == 0) {
        conndata->callbacks = &smsc_http_wapme_callback;
    }
    /*
     * ADD NEW HTTP SMSC TYPES HERE
     */
    else {
        error(0, "HTTP[%s]: system-type '%s' unknown smsc 'http' record.",
              octstr_get_cstr(conn->id), octstr_get_cstr(type));
        goto error;
    }

    if (conndata->callbacks != NULL && conndata->callbacks->init != NULL && conndata->callbacks->init(conn, cfg)) {
        error(0, "HTTP[%s]: submodule '%s' init failed.", octstr_get_cstr(conn->id), octstr_get_cstr(type));
        goto error;
    }

    conndata->open_sends = counter_create();
    conndata->msg_to_send = gwlist_create();
    gwlist_add_producer(conndata->msg_to_send);
    conndata->http_ref = http_caller_create();

    conn->name = octstr_format("HTTP%s:%S:%d", (ssl?"S":""), type, conndata->port);

    if (conndata->send_url != NULL) {
        conn->status = SMSCCONN_ACTIVE;
    } else {
        conn->status = SMSCCONN_ACTIVE_RECV;
    }


    conn->connect_time = time(NULL);

    conn->shutdown = httpsmsc_shutdown;
    conn->queued = httpsmsc_queued;
    conn->send_msg = httpsmsc_send;

    conndata->shutdown = 0;

    /* start receiver thread */
    if (conndata->port > 0) {
        if (http_open_port(conndata->port, ssl) == -1)
            goto error;
        if ((conndata->receive_thread = gwthread_create(httpsmsc_receiver, conn)) == -1)
            goto error;
    } else
        conndata->receive_thread = -1;

    /* start sender threads */
    if (conndata->send_url) {
        if ((conndata->send_cb_thread =
	        gwthread_create(httpsmsc_send_cb, conn)) == -1)
	    goto error;
        if ((conndata->sender_thread =
                gwthread_create(httpsmsc_sender, conn)) == -1)
	    goto error;
    }
    else {
        conndata->send_cb_thread = conndata->sender_thread = -1;
    }

    info(0, "HTTP[%s]: Initiated and ready", octstr_get_cstr(conn->id));

    octstr_destroy(type);
    return 0;

error:
    error(0, "HTTP[%s]: Failed to create HTTP SMSC connection",
          octstr_get_cstr(conn->id));

    if (conndata->callbacks != NULL && conndata->callbacks->destroy != NULL)
        conndata->callbacks->destroy(conn);
    conn->data = NULL;
    conndata_destroy(conndata);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    octstr_destroy(type);
    return -1;
}

