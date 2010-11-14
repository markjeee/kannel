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

#define DEFAULT_CHARSET "UTF-8"

/*
 * This maps fields to values for MO parameters
 */
typedef struct fieldmap {
    Octstr *username;
    Octstr *password;
    Octstr *from;
    Octstr *to;
    Octstr *text;
    Octstr *udh;
    Octstr *service;
    Octstr *account;
    Octstr *binfo;
    Octstr *dlr_mask;
    Octstr *dlr_url;
    Octstr *dlr_mid;
    Octstr *flash;
    Octstr *mclass;
    Octstr *mwi;
    Octstr *coding;
    Octstr *validity;
    Octstr *deferred;
    Octstr *foreign_id;
    Octstr *message_sent;
    long status_sent;
    long status_error;
} FieldMap;

typedef struct conndata {
    HTTPCaller *http_ref;
    FieldMap *fieldmap;
    long receive_thread;
    long send_cb_thread;
    int shutdown;
    int	port;   /* port for receiving SMS'es */
    Octstr *allow_ip;
    Octstr *send_url;
    Octstr *dlr_url;    /* our own DLR MO URL */
    long open_sends;
    Octstr *username;   /* if needed */
    Octstr *password;   /* as said */
    Octstr *system_id;	/* api id for clickatell */
    int no_sender;      /* ditto */
    int no_coding;      /* this, too */
    int no_sep;         /* not to mention this */
    Octstr *proxy;      /* proxy a constant string */
    Octstr *alt_charset;    /* alternative charset use */

    /* The following are compiled regex for the 'generic' type for handling 
     * success, permanent failure and temporary failure. For types that use
     * simple HTTP body parsing, these may be used also for other types,
     * ie. for our own Kannel reply parsing. */
    regex_t *success_regex;   
    regex_t *permfail_regex;
    regex_t *tempfail_regex;

    /* Compiled regex for the 'generic' type to get the foreign message id
    * from the HTTP response body */
    regex_t *generic_foreign_id_regex;

    /* callback functions set by HTTP-SMSC type */
    void (*send_sms) (SMSCConn *conn, Msg *msg);
    void (*parse_reply) (SMSCConn *conn, Msg *msg, int status,
                         List *headers, Octstr *body);
    void (*receive_sms) (SMSCConn *conn, HTTPClient *client,
                         List *headers, Octstr *body, List *cgivars);
} ConnData;


/*
 * Destroys the FieldMap structure
 */
static void fieldmap_destroy(FieldMap *fieldmap)
{
    if (fieldmap == NULL)
        return;
    octstr_destroy(fieldmap->username);
    octstr_destroy(fieldmap->password);
    octstr_destroy(fieldmap->from);
    octstr_destroy(fieldmap->to);
    octstr_destroy(fieldmap->text);
    octstr_destroy(fieldmap->udh);
    octstr_destroy(fieldmap->service);
    octstr_destroy(fieldmap->account);
    octstr_destroy(fieldmap->binfo);
    octstr_destroy(fieldmap->dlr_mask);
    octstr_destroy(fieldmap->dlr_url);
    octstr_destroy(fieldmap->dlr_mid);
    octstr_destroy(fieldmap->flash);
    octstr_destroy(fieldmap->mclass);
    octstr_destroy(fieldmap->mwi);
    octstr_destroy(fieldmap->coding);
    octstr_destroy(fieldmap->validity);
    octstr_destroy(fieldmap->deferred);
    octstr_destroy(fieldmap->foreign_id);
    octstr_destroy(fieldmap->message_sent);
    gw_free(fieldmap);
}


static void conndata_destroy(ConnData *conndata)
{
    if (conndata == NULL)
        return;
    if (conndata->http_ref)
        http_caller_destroy(conndata->http_ref);
    if (conndata->success_regex)
        gw_regex_destroy(conndata->success_regex);
    if (conndata->permfail_regex)
        gw_regex_destroy(conndata->permfail_regex);
    if (conndata->tempfail_regex)
        gw_regex_destroy(conndata->tempfail_regex);
    if (conndata->generic_foreign_id_regex)
        gw_regex_destroy(conndata->generic_foreign_id_regex);
    fieldmap_destroy(conndata->fieldmap);
    octstr_destroy(conndata->allow_ip);
    octstr_destroy(conndata->send_url);
    octstr_destroy(conndata->dlr_url);
    octstr_destroy(conndata->username);
    octstr_destroy(conndata->password);
    octstr_destroy(conndata->proxy);
    octstr_destroy(conndata->system_id);
    octstr_destroy(conndata->alt_charset);

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

    ip = url = body = NULL;
    headers = cgivars = NULL;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);
 
    while (conndata->shutdown == 0) {

        /* XXX if conn->is_stopped, do not receive new messages.. */
	
        client = http_accept_request(conndata->port, &ip, &url,
                                     &headers, &body, &cgivars);
        if (client == NULL)
            break;

        debug("smsc.http", 0, "HTTP[%s]: Got request `%s'", 
              octstr_get_cstr(conn->id), octstr_get_cstr(url));

        if (connect_denied(conndata->allow_ip, ip)) {
            info(0, "HTTP[%s]: Connection `%s' tried from denied "
                    "host %s, ignored", octstr_get_cstr(conn->id),
                    octstr_get_cstr(url), octstr_get_cstr(ip));
            http_close_client(client);
        } else
            conndata->receive_sms(conn, client, headers, body, cgivars);

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
    if (conndata->open_sends == 0)
        http_caller_signal_shutdown(conndata->http_ref);
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

    while (conndata->shutdown == 0 || conndata->open_sends) {

        msg = http_receive_result(conndata->http_ref, &status,
                                  &final_url, &headers, &body);

        if (msg == NULL)
            break;  /* they told us to die, by unlocking */

        /* Handle various states here. */

        /* request failed and we are not in shutdown mode */
        if (status == -1 && conndata->shutdown == 0) { 
            error(0, "HTTP[%s]: Couldn't connect to SMS center "
                     "(retrying in %ld seconds).",
                     octstr_get_cstr(conn->id), conn->reconnect_delay);
            conn->status = SMSCCONN_RECONNECTING; 
            gwthread_sleep(conn->reconnect_delay);
            debug("smsc.http.kannel", 0, "HTTP[%s]: Re-sending request",
                  octstr_get_cstr(conn->id));
            conndata->send_sms(conn, msg);
            continue; 
        } 
        /* request failed and we *are* in shutdown mode, drop the message */ 
        else if (status == -1 && conndata->shutdown == 1) {
        }
        /* request succeeded */    
        else {
            /* we received a response, so this link is considered online again */
            if (status && conn->status != SMSCCONN_ACTIVE) {
                conn->status = SMSCCONN_ACTIVE;
                time(&conn->connect_time);
            }
            conndata->parse_reply(conn, msg, status, headers, body);
        }
   
        conndata->open_sends--;

        http_destroy_headers(headers);
        octstr_destroy(final_url);
        octstr_destroy(body);
    }
    debug("smsc.http", 0, "HTTP[%s]: httpsmsc_send_cb dying",
          octstr_get_cstr(conn->id));
    conndata->shutdown = 1;

    if (conndata->open_sends) {
        warning(0, "HTTP[%s]: Shutdown while <%ld> requests are pending.",
                octstr_get_cstr(conn->id), conndata->open_sends);
    }

    gwthread_join(conndata->receive_thread);

    conn->data = NULL;
    conndata_destroy(conndata);

    conn->status = SMSCCONN_DEAD;
    bb_smscconn_killed();
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


static void kannel_send_sms(SMSCConn *conn, Msg *sms)
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
        octstr_binary_to_hex(sms->sms.msgdata, HEX_NOT_UPPERCASE);
        url = octstr_format("%S?"
			    "username=%E&password=%E&to=%E&text=%S",
			     conndata->send_url,
			     conndata->username, conndata->password,
			     sms->sms.receiver, 
                             sms->sms.msgdata); 
    }   

    if (octstr_len(sms->sms.udhdata)) {
        if (!conndata->no_sep) {
	    octstr_format_append(url, "&udh=%E", sms->sms.udhdata);
        } else {
	    octstr_binary_to_hex(sms->sms.udhdata, HEX_NOT_UPPERCASE);
            octstr_format_append(url, "&udh=%S", sms->sms.udhdata);
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
    if (sms->sms.dlr_url) {
        if (conndata->dlr_url) {
            char id[UUID_STR_LEN + 1];
            Octstr *mid;

            /* create Octstr from UUID */  
            uuid_unparse(sms->sms.id, id);
            mid = octstr_create(id); 

            octstr_format_append(url, "&dlr-url=%E", conndata->dlr_url);

            /* encapsulate the orginal DLR-URL, escape code for DLR mask
             * and message id */
            octstr_format_append(url, "%E%E%E%E%E", 
                octstr_imm("&dlr-url="), sms->sms.dlr_url,
                octstr_imm("&dlr-mask=%d"),
                octstr_imm("&dlr-mid="), mid);
                
            octstr_destroy(mid);
        } else             
            octstr_format_append(url, "&dlr-url=%E", sms->sms.dlr_url);
    }
    if (sms->sms.dlr_mask != DLR_UNDEFINED && sms->sms.dlr_mask != DLR_NOTHING)
        octstr_format_append(url, "&dlr-mask=%d", sms->sms.dlr_mask);

    headers = gwlist_create();
    debug("smsc.http.kannel", 0, "HTTP[%s]: Start request",
          octstr_get_cstr(conn->id));
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, 
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);
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
            dlr_add(conn->id, mid, msg);

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
    Octstr *dlrurl, *dlrmid;
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
    dlrurl = http_cgi_variable(cgivars, "dlr-url");
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
    debug("smsc.http.kannel", 0, "HTTP[%s]: Received an HTTP request",
          octstr_get_cstr(conn->id));
    
    if (user == NULL || pass == NULL ||
	    octstr_compare(user, conndata->username) != 0 ||
	    octstr_compare(pass, conndata->password) != 0) {

        error(0, "HTTP[%s]: Authorization failure",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Authorization failed for sendsms");
    }
    else if (dlrmask != 0 && dlrmid != NULL) {
        /* we got a DLR, and we don't require additional values */
        Msg *dlrmsg;
        
        dlrmsg = dlr_find(conn->id,
            dlrmid, /* message id */
            to, /* destination */
            dlrmask, 0);

        if (dlrmsg != NULL) {
            dlrmsg->sms.sms_type = report_mo;

            debug("smsc.http.kannel", 0, "HTTP[%s]: Received DLR for DLR-URL <%s>",
                  octstr_get_cstr(conn->id), octstr_get_cstr(dlrmsg->sms.dlr_url));
    
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
        msg->sms.validity = validity;
        msg->sms.deferred = deferred;
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


/*----------------------------------------------------------------
 * Clickatell - http://api.clickatell.com/
 *
 * Rene Kluwen <rene.kluwen@chimit.nl>
 * Stipe Tolj <st@tolj.org>, <stolj@kannel.org>
 */

/* MT related function */
static void clickatell_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url, *os;
    char id[UUID_STR_LEN + 1];
    List *headers;

    /* form the basic URL */
    url = octstr_format("%S/sendmsg?to=%E&from=%E&api_id=%E&user=%E&password=%E",
        conndata->send_url, sms->sms.receiver, sms->sms.sender, 
        conndata->system_id, conndata->username, conndata->password);
    
    /* append MD5 digest as msg ID from our UUID */
    uuid_unparse(sms->sms.id, id);
    os = octstr_create(id);
    octstr_replace(os, octstr_imm("-"), octstr_imm(""));
    octstr_format_append(url, "&cliMsgId=%E", os);
    octstr_destroy(os);
    
    /* add UDH header */
    if(octstr_len(sms->sms.udhdata)) {
	octstr_format_append(url, "&udh=%H", sms->sms.udhdata);
    }

    if(sms->sms.coding == DC_8BIT) {
        octstr_format_append(url, "&data=%H&mclass=%d", sms->sms.msgdata, sms->sms.mclass);
    } else if(sms->sms.coding == DC_UCS2) {
        octstr_format_append(url, "&unicode=1&text=%H", sms->sms.msgdata);
    } else {
        octstr_format_append(url, "&text=%E", sms->sms.msgdata);
        if (conndata->alt_charset) {
            octstr_format_append(url, "&charset=%E", conndata->alt_charset);
        } else {
            octstr_append_cstr(url, "&charset=UTF-8");
        }
    }

    if (DLR_IS_ENABLED_DEVICE(sms->sms.dlr_mask))
	octstr_format_append(url, "&callback=3&deliv_ack=1");

    headers = http_create_empty_headers();
    debug("smsc.http.clickatell", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));

    /* 
     * Clickatell requires optionally an SSL-enabled HTTP client call, this is handled
     * transparently by the Kannel HTTP layer module.
     */
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);
}


/*
 * Parse a line in the format: ID: XXXXXXXXXXXXXXXXXX
 * and return a Dict with the 'ID' as key and the value as value,
 * otherwise return NULL if a parsing error occures.
 */
static Dict *clickatell_parse_body(Octstr *body)
{
    Dict *param = NULL;
    List *words = NULL;
    long len;
    Octstr *word, *value;

    words = octstr_split_words(body);
    if ((len = gwlist_len(words)) > 1) {
        word = gwlist_extract_first(words);
        if (octstr_compare(word, octstr_imm("ID:")) == 0) {
            value = gwlist_extract_first(words);
            param = dict_create(4, (void(*)(void *)) octstr_destroy);
            dict_put(param, octstr_imm("ID"), value);
        } else if (octstr_compare(word, octstr_imm("ERR:")) == 0) {
            value = gwlist_extract_first(words);
            param = dict_create(4, (void(*)(void *)) octstr_destroy);
            dict_put(param, octstr_imm("ERR"), value);
        }
        octstr_destroy(word);
    }
    gwlist_destroy(words, (void(*)(void *)) octstr_destroy);

    return param;
}


static void clickatell_parse_reply(SMSCConn *conn, Msg *msg, int status,
                               List *headers, Octstr *body)
{
    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        Dict *param;
        Octstr *msgid;

        if ((param = clickatell_parse_body(body)) != NULL &&
            (msgid = dict_get(param, octstr_imm("ID"))) != NULL &&
            msgid != NULL) {

            /* SMSC ACK.. now we have the message id. */
            if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask))
                dlr_add(conn->id, msgid, msg);

            bb_smscconn_sent(conn, msg, NULL);

        } else {
            error(0, "HTTP[%s]: Message was malformed or error was returned. SMSC response `%s'.",
                  octstr_get_cstr(conn->id), octstr_get_cstr(body));
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
        }
        dict_destroy(param);

    } else {
        error(0, "HTTP[%s]: Message was rejected. SMSC reponse `%s'.",
              octstr_get_cstr(conn->id), octstr_get_cstr(body));
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_REJECTED, octstr_duplicate(body));
    }
}

/* MO related function */
static void clickatell_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    List *reply_headers;
    int ret;
    Octstr *apimsgid, *status, *timestamp, *retmsg, *dest, *charge;
    Octstr *api_id, *from, *to, *text, *charset, *udh;
    int httpstatus = HTTP_UNAUTHORIZED, dlrstat;
    Msg *dlrmsg, *momsg;
    struct tm tm;

    /* dlr parameters */
    apimsgid = http_cgi_variable(cgivars, "apiMsgId");
    status = http_cgi_variable(cgivars, "status");
    /* timestamp is for both DLR & MO */
    timestamp = http_cgi_variable(cgivars, "timestamp");
    dest = http_cgi_variable(cgivars, "to");
    charge = http_cgi_variable(cgivars, "charge");
    /* MO parameters */
    api_id = http_cgi_variable(cgivars, "api_id");
    from = http_cgi_variable(cgivars, "from");
    to = http_cgi_variable(cgivars, "to");
    text = http_cgi_variable(cgivars, "text");
    charset = http_cgi_variable(cgivars, "charset");
    udh = http_cgi_variable(cgivars, "udh");

    debug("smsc.http.clickatell", 0, "HTTP[%s]: Received a request",
          octstr_get_cstr(conn->id));
 
    if (api_id != NULL && from != NULL && to != NULL && timestamp != NULL && text != NULL && charset != NULL && udh != NULL) {
	/* we received an MO message */
	debug("smsc.http.clickatell", 0, "HTTP[%s]: Received MO message from %s: <%s>",
            octstr_get_cstr(conn->id), octstr_get_cstr(from), octstr_get_cstr(text));
	momsg = msg_create(sms);
	momsg->sms.sms_type = mo;
	momsg->sms.sender = octstr_duplicate(from);
	momsg->sms.receiver = octstr_duplicate(to);
	momsg->sms.msgdata = octstr_duplicate(text);
	momsg->sms.charset = octstr_duplicate(charset);
    momsg->sms.service = octstr_duplicate(api_id);
	momsg->sms.binfo = octstr_duplicate(api_id);
        momsg->sms.smsc_id = octstr_duplicate(conn->id);
	if (octstr_len(udh) > 0) {
	    momsg->sms.udhdata = octstr_duplicate(udh);
	}
        strptime(octstr_get_cstr(timestamp), "%Y-%m-%d %H:%M:%S", &tm);
        momsg->sms.time = gw_mktime(&tm);
 
	/* note: implicit msg_destroy */
	ret = bb_smscconn_receive(conn, momsg);
        httpstatus = HTTP_OK;
	retmsg = octstr_create("Thanks");
    } else if (apimsgid == NULL || status == NULL || timestamp == NULL || dest == NULL) {
        error(0, "HTTP[%s]: Insufficient args.",
              octstr_get_cstr(conn->id));
        httpstatus = HTTP_BAD_REQUEST;
        retmsg = octstr_create("Insufficient arguments, rejected.");
    } else {
	switch (atoi(octstr_get_cstr(status))) {
	case  1: /* message unknown */
	case  5: /* error with message */
	case  6: /* user cancelled message */
	case  7: /* error delivering message */
	case  9: /* routing error */
	case 10: /* message expired */
	    dlrstat = 2; /* delivery failure */
	    break;
	case  2: /* message queued */
	case  3: /* delivered */
	case 11: /* message queued for later delivery */
	    dlrstat = 4; /* message buffered */
	    break;
	case  4: /* received by recipient */
	case  8: /* OK */
	    dlrstat = 1; /* message received */
	    break;
	default: /* unknown status code */
	    dlrstat = 16; /* smsc reject */
	    break;
	}
        dlrmsg = dlr_find(conn->id,
            apimsgid, /* smsc message id */
            dest, /* destination */
            dlrstat, 0);

        if (dlrmsg != NULL) {
            /* dlrmsg->sms.msgdata = octstr_duplicate(apimsgid); */
            dlrmsg->sms.sms_type = report_mo;
	    dlrmsg->sms.time = atoi(octstr_get_cstr(timestamp));
	    if (charge) {
		/* unsure if smsbox relays the binfo field to dlrs.
		   But it is here in case they will start to do it. */
		dlrmsg->sms.binfo = octstr_duplicate(charge);
	    }
            
            ret = bb_smscconn_receive(conn, dlrmsg);
            httpstatus = (ret == 0 ? HTTP_OK : HTTP_FORBIDDEN);
            retmsg = octstr_create("Sent");
        } else {
            error(0,"HTTP[%s]: got DLR but could not find message or was not interested "
                    "in it id<%s> dst<%s>, type<%d>",
            octstr_get_cstr(conn->id), octstr_get_cstr(apimsgid),
            octstr_get_cstr(dest), dlrstat);
            httpstatus = HTTP_OK;
            retmsg = octstr_create("Thanks");
        }
    }

    reply_headers = gwlist_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.clickatell", 0, "HTTP[%s]: Sending reply `%s'.",
          octstr_get_cstr(conn->id), octstr_get_cstr(retmsg));
    http_send_reply(client, httpstatus, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}


/*----------------------------------------------------------------
 * Brunet - A german aggregator (mainly doing T-Mobil D1 connections)
 *
 *  o bruHTT v1.3L (for MO traffic) 
 *  o bruHTP v2.1 (date 22.04.2003) (for MT traffic)
 *
 * Stipe Tolj <stolj@wapme.de>
 * Tobias Weber <weber@wapme.de>
 */

/* MT related function */
static void brunet_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url, *tid, *xser;
    List *headers;
    char id[UUID_STR_LEN + 1];
    int dcs;

    /* 
     * Construct TransactionId.
     * Beware that brunet needs an "clean" octstr representation, 
     * without the dashes in the string. So remove them.
     */
    uuid_unparse(sms->sms.id, id);
    tid = octstr_create(id);
    octstr_replace(tid, octstr_imm("-"), octstr_imm(""));

    /* form the basic URL */
    url = octstr_format("%S?MsIsdn=%E&Originator=%E",
        conndata->send_url, sms->sms.receiver, sms->sms.sender);
    
    /* 
     * We use &binfo=<foobar> from sendsms interface to encode
     * additional paramters. If a mandatory value is not set,
     * a default value is applied
     */
    if (octstr_len(sms->sms.binfo)) {
        octstr_url_decode(sms->sms.binfo);
        octstr_format_append(url, "&%S", sms->sms.binfo);
    }
    /* CustomerId */
    if (octstr_search(url, octstr_create("CustomerId="), 0) == -1) {
        octstr_format_append(url, "&CustomerId=%S", conndata->username);
    }
    /* TransactionId */
    if (octstr_search(url, octstr_create("TransactionId="), 0) == -1) {
        octstr_format_append(url, "&TransactionId=%S", tid);
    }
    /* SMSCount */
    if (octstr_search(url, octstr_create("SMSCount="), 0) == -1) {
        octstr_format_append(url, "&%s", "SMSCount=1");
    }
    /* ActionType */
    if (octstr_search(url, octstr_create("ActionType="), 0) == -1) {
        octstr_format_append(url, "&%s", "ActionType=A");
    }
    /* ServiceDeliveryType */
    if (octstr_search(url, octstr_create("ServiceDeliveryType="), 0) == -1) {
        octstr_format_append(url, "&%s", "ServiceDeliveryType=P");
    }

    /* if coding is not set and UDH exists, assume DC_8BIT
     * else default to DC_7BIT */
    if (sms->sms.coding == DC_UNDEF)
        sms->sms.coding = octstr_len(sms->sms.udhdata) > 0 ? DC_8BIT : DC_7BIT;

    if (sms->sms.coding == DC_8BIT)
        octstr_format_append(url, "&MessageType=B&Text=%H", sms->sms.msgdata);
    else
        octstr_format_append(url, "&MessageType=S&Text=%E", sms->sms.msgdata);

    dcs = fields_to_dcs(sms,
        (sms->sms.alt_dcs != SMS_PARAM_UNDEFINED ? sms->sms.alt_dcs : 0));

    /* XSer processing */    
    xser = octstr_create("");
    /* XSer DCS values */
    if (dcs != 0 && dcs != 4)
        octstr_format_append(xser, "0201%02x", dcs & 0xff);
    /* add UDH header */
    if (octstr_len(sms->sms.udhdata)) {
        octstr_format_append(xser, "01%02x%H", octstr_len(sms->sms.udhdata), 
                             sms->sms.udhdata);
    }
    if (octstr_len(xser) > 0)
        octstr_format_append(url, "&XSer=%S", xser);
    octstr_destroy(xser);


    headers = http_create_empty_headers();
    debug("smsc.http.brunet", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));

    /* 
     * Brunet requires an SSL-enabled HTTP client call, this is handled
     * transparently by the Kannel HTTP layer module.
     */
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, 
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    octstr_destroy(tid);
    http_destroy_headers(headers);
}


/*
 * Parse a line in the format: <name=value name=value ...>
 * and return a Dict with the name as key and the value as value,
 * otherwise return NULL if a parsing error occures.
 */
static Dict *brunet_parse_body(Octstr *body)
{
    Dict *param = NULL;
    List *words = NULL;
    long len;
    Octstr *word;

    words = octstr_split_words(body);
    if ((len = gwlist_len(words)) > 0) {
        param = dict_create(4, (void(*)(void *)) octstr_destroy);
        while ((word = gwlist_extract_first(words)) != NULL) {
            List *l = octstr_split(word, octstr_imm("="));
            Octstr *key = gwlist_extract_first(l);
            Octstr *value = gwlist_extract_first(l);
            if (octstr_len(key))
                dict_put(param, key, value);
            octstr_destroy(key);
            octstr_destroy(word);
            gwlist_destroy(l, (void(*)(void *)) octstr_destroy);
        }
    }
    gwlist_destroy(words, (void(*)(void *)) octstr_destroy);

    return param;
}


static void brunet_parse_reply(SMSCConn *conn, Msg *msg, int status,
                               List *headers, Octstr *body)
{
    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        Dict *param;
        Octstr *status;

        if ((param = brunet_parse_body(body)) != NULL &&
            (status = dict_get(param, octstr_imm("Status"))) != NULL &&
            octstr_case_compare(status, octstr_imm("0")) == 0) {
            Octstr *msg_id;

            /* pass the MessageId for this MT to the logging facility */
            if ((msg_id = dict_get(param, octstr_imm("MessageId"))) != NULL)
                msg->sms.binfo = octstr_duplicate(msg_id);

            bb_smscconn_sent(conn, msg, NULL);

        } else {
            error(0, "HTTP[%s]: Message was malformed. SMSC response `%s'.",
                  octstr_get_cstr(conn->id), octstr_get_cstr(body));
            bb_smscconn_send_failed(conn, msg,
	                SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
        }
        dict_destroy(param);

    } else {
        error(0, "HTTP[%s]: Message was rejected. SMSC reponse `%s'.",
              octstr_get_cstr(conn->id), octstr_get_cstr(body));
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_REJECTED, octstr_duplicate(body));
    }
}

/* MO related function */
static void brunet_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    Octstr *user, *from, *to, *text, *udh, *date, *type;
    Octstr *retmsg;
    int	mclass, mwi, coding, validity, deferred;
    List *reply_headers;
    int ret;

    mclass = mwi = coding = validity = deferred = 0;

    user = http_cgi_variable(cgivars, "CustomerId");
    from = http_cgi_variable(cgivars, "MsIsdn");
    to = http_cgi_variable(cgivars, "Recipient");
    text = http_cgi_variable(cgivars, "SMMO");
    udh = http_cgi_variable(cgivars, "XSer");
    date = http_cgi_variable(cgivars, "DateReceived");
    type = http_cgi_variable(cgivars, "MessageType");

    debug("smsc.http.brunet", 0, "HTTP[%s]: Received a request",
          octstr_get_cstr(conn->id));
    
    if (user == NULL || octstr_compare(user, conndata->username) != 0) {
        error(0, "HTTP[%s]: Authorization failure. CustomerId was <%s>.",
              octstr_get_cstr(conn->id), octstr_get_cstr(user));
        retmsg = octstr_create("Authorization failed for MO submission.");
    }
    else if (from == NULL || to == NULL || text == NULL) {
        error(0, "HTTP[%s]: Insufficient args.",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient arguments, rejected.");
    }
    else {
        Msg *msg;
        msg = msg_create(sms);

        debug("smsc.http.brunet", 0, "HTTP[%s]: Received new MO SMS.",
              octstr_get_cstr(conn->id));
	
        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.msgdata = octstr_duplicate(text);
        msg->sms.udhdata = octstr_duplicate(udh);

        msg->sms.smsc_id = octstr_duplicate(conn->id);
        msg->sms.time = time(NULL); /* XXX maybe extract from DateReceived */ 
        msg->sms.mclass = mclass;
        msg->sms.mwi = mwi;
        msg->sms.coding = coding;
        msg->sms.validity = validity;
        msg->sms.deferred = deferred;

        ret = bb_smscconn_receive(conn, msg);
        if (ret == -1)
            retmsg = octstr_create("Status=1");
        else
            retmsg = octstr_create("Status=0");
    }

    reply_headers = gwlist_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.brunet", 0, "HTTP[%s]: Sending reply `%s'.",
          octstr_get_cstr(conn->id), octstr_get_cstr(retmsg));
    http_send_reply(client, HTTP_OK, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}


/*----------------------------------------------------------------
 * 3united.com (formerly Xidris) - An austrian (AT) SMS aggregator 
 * Implementing version 1.3, 2003-05-06
 * Updating to version 1.9.1, 2004-09-28
 *
 * Stipe Tolj <stolj@wapme.de>
 */

/* MT related function */
static void xidris_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url, *new_msg;
    List *headers;
    int dcs, esm_class;

    url = new_msg = NULL;
    dcs = esm_class = 0;

    /* format the URL for call */
    url = octstr_format("%S?"
        "app_id=%E&key=%E&dest_addr=%E&source_addr=%E",
        conndata->send_url, conndata->username, 
        conndata->password, sms->sms.receiver, sms->sms.sender);

    if (octstr_len(sms->sms.udhdata)) {
        /* RAW additions for binary (8bit) msgs  */

        /* set the data coding scheme (DCS) and ESM class fields */
        dcs = fields_to_dcs(sms, sms->sms.alt_dcs);
        /* ESM_CLASS_SUBMIT_STORE_AND_FORWARD_MODE | 
           ESM_CLASS_SUBMIT_UDH_INDICATOR */
        esm_class = 0x03 | 0x40; 
    
        /* prepend UDH header to message block */
        new_msg = octstr_duplicate(sms->sms.udhdata);
        octstr_append(new_msg, sms->sms.msgdata);

        octstr_format_append(url, "&type=200&dcs=%d&esm=%d&message=%H",
                             dcs, esm_class, new_msg);
    }  else {
        /* additions for text (7bit) msgs */

        octstr_format_append(url, "&type=%E&message=%E",
                            (sms->sms.mclass ? octstr_imm("1") : octstr_imm("0")),
                            sms->sms.msgdata);
    }

    /* 
     * We use &account=<foobar> from sendsms interface to encode any additionaly
     * proxied parameters, ie. billing information.
     */
    if (octstr_len(sms->sms.account)) {
        octstr_url_decode(sms->sms.account);
        octstr_format_append(url, "&%s", octstr_get_cstr(sms->sms.account));
    }

    headers = gwlist_create();
    debug("smsc.http.xidris", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));

    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, 
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    octstr_destroy(new_msg);
    http_destroy_headers(headers);
}


/* 
 * Parse for an parameter of an given XML tag and return it as Octstr
 */
static Octstr *parse_xml_tag(Octstr *body, Octstr *tag)
{
    Octstr *stag, *etag, *ret;
    int spos, epos;
   
    stag = octstr_format("<%s>", octstr_get_cstr(tag));
    if ((spos = octstr_search(body, stag, 0)) == -1) {
        octstr_destroy(stag);
        return NULL;
    }
    etag = octstr_format("</%s>", octstr_get_cstr(tag));
    if ((epos = octstr_search(body, etag, spos+octstr_len(stag))) == -1) {
        octstr_destroy(stag);
        octstr_destroy(etag);
        return NULL;
    }
    
    ret = octstr_copy(body, spos+octstr_len(stag), epos+1 - (spos+octstr_len(etag)));  
    octstr_strip_blanks(ret);
    octstr_strip_crlfs(ret);

    octstr_destroy(stag);
    octstr_destroy(etag);

    return ret;
}

static void xidris_parse_reply(SMSCConn *conn, Msg *msg, int status,
                               List *headers, Octstr *body)
{
    Octstr *code, *desc, *mid;

    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        /* now parse the XML document for error code */
        code = parse_xml_tag(body, octstr_imm("status"));
        desc = parse_xml_tag(body, octstr_imm("description"));
        
        /* The following parsing assumes we get only *one* message id in the 
         * response XML. Which is ok, since we garantee via previous concat
         * splitting, that we only pass PDUs of 1 SMS size to SMSC. */
        mid = parse_xml_tag(body, octstr_imm("message_id"));

        if (octstr_case_compare(code, octstr_imm("0")) == 0 && mid != NULL) {
            /* ensure the message id gets logged */
            msg->sms.binfo = octstr_duplicate(mid);

            /* SMSC ACK.. now we have the message id. */
            if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask))
                dlr_add(conn->id, mid, msg);

            octstr_destroy(mid);
            bb_smscconn_sent(conn, msg, NULL);

        } else {
            error(0, "HTTP[%s]: Message not accepted. Status code <%s> "
                  "description `%s'.", octstr_get_cstr(conn->id),
                  octstr_get_cstr(code), octstr_get_cstr(desc));
            bb_smscconn_send_failed(conn, msg,
	                SMSCCONN_FAILED_MALFORMED, octstr_duplicate(desc));
        }
    } else {
        error(0, "HTTP[%s]: Message was rejected. SMSC reponse was:",
              octstr_get_cstr(conn->id));
        octstr_dump(body, 0);
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));
    }
}

/* MO related function */
static void xidris_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    Octstr *user, *pass, *from, *to, *text, *account, *binfo;
    Octstr *state, *mid, *dest;
    Octstr *retmsg;
    int	mclass, mwi, coding, validity, deferred; 
    List *reply_headers;
    int ret, status;

    mclass = mwi = coding = validity = deferred = 0;
    retmsg = NULL;

    /* generic values */
    user = http_cgi_variable(cgivars, "app_id");
    pass = http_cgi_variable(cgivars, "key");

    /* MO specific values */
    from = http_cgi_variable(cgivars, "source_addr");
    to = http_cgi_variable(cgivars, "dest_addr");
    text = http_cgi_variable(cgivars, "message");
    account = http_cgi_variable(cgivars, "operator");
    binfo = http_cgi_variable(cgivars, "tariff");

    /* DLR (callback) specific values */
    state = http_cgi_variable(cgivars, "state");
    mid = http_cgi_variable(cgivars, "message_id");
    dest = http_cgi_variable(cgivars, "dest_addr");

    debug("smsc.http.xidris", 0, "HTTP[%s]: Received a request",
          octstr_get_cstr(conn->id));

    if (user == NULL || pass == NULL ||
	    octstr_compare(user, conndata->username) != 0 ||
	    octstr_compare(pass, conndata->password) != 0) {
        error(0, "HTTP[%s]: Authorization failure. username was <%s>.",
              octstr_get_cstr(conn->id), octstr_get_cstr(user));
        retmsg = octstr_create("Authorization failed for MO submission.");
        status = HTTP_UNAUTHORIZED;
    }
    else if (state != NULL && mid != NULL && dest != NULL) {    /* a DLR message */
        Msg *dlrmsg;
        int dlrstat = -1;

        if (octstr_compare(state, octstr_imm("DELIVRD")) == 0)
            dlrstat = DLR_SUCCESS;
        else if (octstr_compare(state, octstr_imm("ACCEPTD")) == 0)
            dlrstat = DLR_BUFFERED;
        else
            dlrstat = DLR_FAIL;

        dlrmsg = dlr_find(conn->id,
            mid, /* smsc message id */
            dest, /* destination */
            dlrstat, 0);

        if (dlrmsg != NULL) {
            dlrmsg->sms.msgdata = octstr_duplicate(mid);
            dlrmsg->sms.sms_type = report_mo;
            
            ret = bb_smscconn_receive(conn, dlrmsg);
            status = (ret == 0 ? HTTP_OK : HTTP_FORBIDDEN);
        } else {
            error(0,"HTTP[%s]: got DLR but could not find message or was not interested "
                    "in it id<%s> dst<%s>, type<%d>",
                octstr_get_cstr(conn->id), octstr_get_cstr(mid),
                octstr_get_cstr(dest), dlrstat);
            status = HTTP_OK;
        }

    }
    else if (from == NULL || to == NULL || text == NULL) {
        error(0, "HTTP[%s]: Insufficient args.",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient arguments, rejected.");
        status = HTTP_BAD_REQUEST;
    }
    else {
        Msg *msg;
        msg = msg_create(sms);

        debug("smsc.http.xidris", 0, "HTTP[%s]: Received new MO SMS.",
              octstr_get_cstr(conn->id));
	
        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.msgdata = octstr_duplicate(text);
        msg->sms.account = octstr_duplicate(account);
        msg->sms.binfo = octstr_duplicate(binfo);

        msg->sms.smsc_id = octstr_duplicate(conn->id);
        msg->sms.time = time(NULL);
        msg->sms.mclass = mclass;
        msg->sms.mwi = mwi;
        msg->sms.coding = coding;
        msg->sms.validity = validity;
        msg->sms.deferred = deferred;

        ret = bb_smscconn_receive(conn, msg);
        status = (ret == 0 ? HTTP_OK : HTTP_FORBIDDEN);
    }

    reply_headers = gwlist_create();
    debug("smsc.http.xidris", 0, "HTTP[%s]: Sending reply with HTTP status <%d>.",
          octstr_get_cstr(conn->id), status);

    http_send_reply(client, status, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}


/*----------------------------------------------------------------
 * Wapme SMS Proxy
 *
 * Stipe Tolj <stolj@kannel.org>
 */

static void wapme_smsproxy_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url;
    List *headers;

    url = octstr_format("%S?command=forward&smsText=%E&phoneNumber=%E"
                        "&serviceNumber=%E&smsc=%E",
                        conndata->send_url,
                        sms->sms.msgdata, sms->sms.sender, sms->sms.receiver,
                        sms->sms.smsc_id);

    headers = gwlist_create();
    debug("smsc.http.wapme", 0, "HTTP[%s]: Start request",
          octstr_get_cstr(conn->id));
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, 
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);
}

static void wapme_smsproxy_parse_reply(SMSCConn *conn, Msg *msg, int status,
			       List *headers, Octstr *body)
{
    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        bb_smscconn_sent(conn, msg, NULL);
    } else {
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
    }
}

/*
 * static void wapme_smsproxy_receive_sms(SMSCConn *conn, HTTPClient *client,
 *                                List *headers, Octstr *body, List *cgivars)
 *
 * The HTTP server for MO messages will act with the same interface as smsbox's 
 * sendsms interface, so that the logical difference is hidden and SMS Proxy 
 * can act transparently. So there is no need for an explicite implementation
 * here.
 */


/*----------------------------------------------------------------
 * Generic HTTP interface
 *
 * This 'generic' type will handle the 'send-url' directive in the 
 * group the same way the 'sms-service' for smsbox does, via 
 * URLTranslation. Response interpretation is based on the three
 * regex value that match against the reponse body. The HTTP reponse
 * code is not obeyed.
 * 
 * Example config group:
 * 
 *  group = smsc
 *  smsc = http
 *  system-type = generic
 *  send-url = "http://<foobar>/<uri>?from=%P&to=%p&text=%b"
 *  status-success-regex = "ok"
 *  status-permfail-regex = "failure"
 *  status-tempfail-regex = "retry later"
 *  generic-foreign-id-regex = "<id>(.+)</id>"
 *  generic-param-from = "phoneNumber"
 *  generic-param-to = "shortCode"
 *  generic-param-text = "message"
 *  generic-message-sent = "OK"
 *  generic-status-sent = 200
 *  generic-status-error = 400
 * 
 * Note that neither 'smsc-username' nor 'smsc-password' is required,
 * since they are coded into the the 'send-url' value directly. 
 * 
 * Stipe Tolj <st@tolj.org>
 *
 * MO processing by Alejandro Guerrieri <aguerrieri at kannel dot org>
 */

/*
 * Get the FieldMap struct to map MO parameters
 */
static FieldMap *generic_get_field_map(CfgGroup *grp)
{
    FieldMap *fm = NULL;
    fm = gw_malloc(sizeof(FieldMap));
    gw_assert(fm != NULL);
    fm->username = cfg_get(grp, octstr_imm("generic-param-username"));
    if (fm->username == NULL)
        fm->username = octstr_create("username");
    fm->password = cfg_get(grp, octstr_imm("generic-param-password"));
    if (fm->password == NULL)
        fm->password = octstr_create("password");
    fm->from = cfg_get(grp, octstr_imm("generic-param-from"));
    if (fm->from == NULL)
        fm->from = octstr_create("from");
    fm->to = cfg_get(grp, octstr_imm("generic-param-to"));
    if (fm->to == NULL)
        fm->to = octstr_create("to");
    fm->text = cfg_get(grp, octstr_imm("generic-param-text"));
    if (fm->text == NULL)
        fm->text = octstr_create("text");
    fm->udh = cfg_get(grp, octstr_imm("generic-param-udh"));
    if (fm->udh == NULL)
        fm->udh = octstr_create("udh");
    /* "service" preloads the "username" parameter to mimic former behaviour */
    fm->service = cfg_get(grp, octstr_imm("generic-param-service"));
    if (fm->service == NULL)
        fm->service = octstr_create("username");
    fm->account = cfg_get(grp, octstr_imm("generic-param-account"));
    if (fm->account == NULL)
        fm->account = octstr_create("account");
    fm->binfo = cfg_get(grp, octstr_imm("generic-param-binfo"));
    if (fm->binfo == NULL)
        fm->binfo = octstr_create("binfo");
    fm->dlr_mask = cfg_get(grp, octstr_imm("generic-param-dlr-mask"));
    if (fm->dlr_mask == NULL)
        fm->dlr_mask = octstr_create("dlr-mask");
    fm->dlr_url = cfg_get(grp, octstr_imm("generic-param-dlr-url"));
    if (fm->dlr_url == NULL)
        fm->dlr_url = octstr_create("dlr-url");
    fm->dlr_mid = cfg_get(grp, octstr_imm("generic-param-dlr-mid"));
    if (fm->dlr_mid == NULL)
        fm->dlr_mid = octstr_create("dlr-mid");
    fm->flash = cfg_get(grp, octstr_imm("generic-param-flash"));
    if (fm->flash == NULL)
        fm->flash = octstr_create("flash");
    fm->mclass = cfg_get(grp, octstr_imm("generic-param-mclass"));
    if (fm->mclass == NULL)
        fm->mclass = octstr_create("mclass");
    fm->mwi = cfg_get(grp, octstr_imm("generic-param-mwi"));
    if (fm->mwi == NULL)
        fm->mwi = octstr_create("mwi");
    fm->coding = cfg_get(grp, octstr_imm("generic-param-coding"));
    if (fm->coding == NULL)
        fm->coding = octstr_create("coding");
    fm->validity = cfg_get(grp, octstr_imm("generic-param-validity"));
    if (fm->validity == NULL)
        fm->validity = octstr_create("validity");
    fm->deferred = cfg_get(grp, octstr_imm("generic-param-deferred"));
    if (fm->deferred == NULL)
        fm->deferred = octstr_create("deferred");
    fm->foreign_id = cfg_get(grp, octstr_imm("generic-param-foreign-id"));
    if (fm->foreign_id == NULL)
        fm->foreign_id = octstr_create("foreign-id");
    fm->message_sent = cfg_get(grp, octstr_imm("generic-message-sent"));
    if (fm->message_sent == NULL)
        fm->message_sent = octstr_create("Sent");
    /* both success and error uses HTTP_ACCEPTED to mimic former behaviour */
    if (cfg_get_integer(&fm->status_sent, grp, octstr_imm("generic-status-sent")) == -1) {
        fm->status_sent = HTTP_ACCEPTED;
    }
    if (cfg_get_integer(&fm->status_error, grp, octstr_imm("generic-status-error")) == -1) {
        fm->status_error = HTTP_ACCEPTED;
    }

    return fm;
}

static void generic_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    FieldMap *fm = conndata->fieldmap;
    Octstr *user, *pass, *from, *to, *text, *udh, *account, *binfo;
    Octstr *dlrurl, *dlrmid;
    Octstr *tmp_string, *retmsg;
    int	mclass, mwi, coding, validity, deferred, dlrmask;
    List *reply_headers;
    int ret, retstatus;

    mclass = mwi = coding = validity =
        deferred = dlrmask = SMS_PARAM_UNDEFINED;

    /* Parse enough parameters to validate the request */
    user = http_cgi_variable(cgivars, octstr_get_cstr(fm->username));
    pass = http_cgi_variable(cgivars, octstr_get_cstr(fm->password));
    from = http_cgi_variable(cgivars, octstr_get_cstr(fm->from));
    to = http_cgi_variable(cgivars, octstr_get_cstr(fm->to));
    text = http_cgi_variable(cgivars, octstr_get_cstr(fm->text));
    udh = http_cgi_variable(cgivars, octstr_get_cstr(fm->udh));
    dlrurl = http_cgi_variable(cgivars, octstr_get_cstr(fm->dlr_url));
    dlrmid = http_cgi_variable(cgivars, octstr_get_cstr(fm->dlr_mid));
    tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->dlr_mask));
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &dlrmask);
    }
    debug("smsc.http.generic", 0, "HTTP[%s]: Received an HTTP request",
          octstr_get_cstr(conn->id));

    if ((conndata->username != NULL && conndata->password != NULL) &&
        (user == NULL || pass == NULL ||
        octstr_compare(user, conndata->username) != 0 ||
        octstr_compare(pass, conndata->password) != 0)) {
        error(0, "HTTP[%s]: Authorization failure",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Authorization failed for sendsms");
        retstatus = fm->status_error;
    }
    else if (dlrmask != 0 && dlrmid != NULL) {
        /* we got a DLR, and we don't require additional values */
        Msg *dlrmsg;

        dlrmsg = dlr_find(conn->id,
            dlrmid, /* message id */
            to, /* destination */
            dlrmask, 0);

        if (dlrmsg != NULL) {
            dlrmsg->sms.sms_type = report_mo;

            debug("smsc.http.generic", 0, "HTTP[%s]: Received DLR for DLR-URL <%s>",
                  octstr_get_cstr(conn->id), octstr_get_cstr(dlrmsg->sms.dlr_url));

            Msg *resp = msg_duplicate(dlrmsg);
            ret = bb_smscconn_receive(conn, dlrmsg);
            if (ret == -1) {
                retmsg = octstr_create("Not accepted");
                retstatus = fm->status_error;
            } else {
                retmsg = urltrans_fill_escape_codes(fm->message_sent, resp);
                retstatus = fm->status_sent;
            }
            msg_destroy(resp);
        } else {
            error(0,"HTTP[%s]: Got DLR but could not find message or was not interested "
                  "in it id<%s> dst<%s>, type<%d>",
                  octstr_get_cstr(conn->id), octstr_get_cstr(dlrmid),
                  octstr_get_cstr(to), dlrmask);
            retmsg = octstr_create("Unknown DLR, not accepted");
            retstatus = fm->status_error;
        }
    }
    else if (from == NULL || to == NULL || text == NULL) {
        error(0, "HTTP[%s]: Insufficient args",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient args, rejected");
        retstatus = fm->status_error;
    }
    else if (udh != NULL && (octstr_len(udh) != octstr_get_char(udh, 0) + 1)) {
        error(0, "HTTP[%s]: UDH field misformed, rejected",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("UDH field misformed, rejected");
        retstatus = fm->status_error;
    }
    else if (udh != NULL && octstr_len(udh) > MAX_SMS_OCTETS) {
        error(0, "HTTP[%s]: UDH field is too long, rejected",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("UDH field is too long, rejected");
        retstatus = fm->status_error;
    }
    else {
        /* we got a normal MO SMS */
        Msg *msg;
        msg = msg_create(sms);

        /* Parse the rest of the parameters */
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->flash));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.mclass);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->mclass));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.mclass);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->mwi));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.mwi);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->coding));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.coding);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->validity));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.validity);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->deferred));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.deferred);
        }
        account = http_cgi_variable(cgivars, octstr_get_cstr(fm->account));
        binfo = http_cgi_variable(cgivars, octstr_get_cstr(fm->binfo));

        debug("smsc.http.generic", 0, "HTTP[%s]: Constructing new SMS",
              octstr_get_cstr(conn->id));

        /* convert character encoding if required */
        if (conndata->alt_charset &&
            charset_convert(text, octstr_get_cstr(conndata->alt_charset),
                    DEFAULT_CHARSET) != 0)
            error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave it as it is.",
                    octstr_get_cstr(conndata->alt_charset), DEFAULT_CHARSET);

        msg->sms.service = octstr_duplicate(user);
        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.msgdata = octstr_duplicate(text);
        msg->sms.udhdata = octstr_duplicate(udh);
        msg->sms.smsc_id = octstr_duplicate(conn->id);
        msg->sms.time = time(NULL);
        msg->sms.account = octstr_duplicate(account);
        msg->sms.binfo = octstr_duplicate(binfo);
        Msg *resp = msg_duplicate(msg);
        ret = bb_smscconn_receive(conn, msg);
        if (ret == -1) {
            retmsg = octstr_create("Not accepted");
            retstatus = fm->status_error;
        } else {
            retmsg = urltrans_fill_escape_codes(fm->message_sent, resp);
            retstatus = fm->status_sent;
        }
        msg_destroy(resp);
    }

    reply_headers = gwlist_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.generic", 0, "HTTP[%s]: Sending reply",
          octstr_get_cstr(conn->id));
    http_send_reply(client, retstatus, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}


static void generic_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url;
    List *headers;

    /* We use the escape code population function from our
     * URLTranslation module to fill in the appropriate values
     * into the URL scheme. */
    url = urltrans_fill_escape_codes(conndata->send_url, sms);

    headers = gwlist_create();
    debug("smsc.http.generic", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, 
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);
}

static void generic_parse_reply(SMSCConn *conn, Msg *msg, int status,
                                List *headers, Octstr *body)
{
    ConnData *conndata = conn->data;
    regmatch_t pmatch[2];
    Octstr *msgid = NULL;
    
    /* 
     * Our generic type checks only content on the HTTP reponse body.
     * We use the pre-compiled regex to match against the states.
     * This is the most generic criteria (at the moment). 
     */
    if ((conndata->success_regex != NULL) && 
        (gw_regex_exec(conndata->success_regex, body, 0, NULL, 0) == 0)) {
        /* SMSC ACK... the message id should be in the body */
        if ((conndata->generic_foreign_id_regex != NULL) && DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask)) {
            if (gw_regex_exec(conndata->generic_foreign_id_regex, body, sizeof(pmatch) / sizeof(regmatch_t), pmatch, 0) == 0) {
                if (pmatch[1].rm_so != -1 && pmatch[1].rm_eo != -1) {
                    msgid = octstr_copy(body, pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
                    debug("smsc.http.generic", 0, "HTTP[%s]: Found foreign message id <%s> in body.", octstr_get_cstr(conn->id), octstr_get_cstr(msgid));
                    dlr_add(conn->id, msgid, msg);
                }
            }
            if (msgid == NULL)
                warning(0, "HTTP[%s]: Can't get the foreign message id from the HTTP body.", octstr_get_cstr(conn->id));
            else
                octstr_destroy(msgid);
        }
        bb_smscconn_sent(conn, msg, NULL);
    } 
    else if ((conndata->permfail_regex != NULL) &&        
        (gw_regex_exec(conndata->permfail_regex, body, 0, NULL, 0) == 0)) {
        error(0, "HTTP[%s]: Message not accepted.", octstr_get_cstr(conn->id));
        bb_smscconn_send_failed(conn, msg,
            SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
    }
    else if ((conndata->tempfail_regex != NULL) &&        
        (gw_regex_exec(conndata->tempfail_regex, body, 0, NULL, 0) == 0)) {
        warning(0, "HTTP[%s]: Message temporary not accepted, will retry.", 
                octstr_get_cstr(conn->id));
        bb_smscconn_send_failed(conn, msg,
            SMSCCONN_FAILED_TEMPORARILY, octstr_duplicate(body));
    }
    else {
        error(0, "HTTP[%s]: Message was rejected. SMSC reponse was:",
              octstr_get_cstr(conn->id));
        octstr_dump(body, 0);
        bb_smscconn_send_failed(conn, msg,
            SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));
    }
}


/*-----------------------------------------------------------------
 * functions to implement various smscconn operations
 */

static int httpsmsc_send(SMSCConn *conn, Msg *msg)
{
    ConnData *conndata = conn->data;
    Msg *sms = msg_duplicate(msg);
    double delay = 0;

    if (conn->throughput > 0) {
        delay = 1.0 / conn->throughput;
    }

    /* convert character encoding if required */
    if (conndata->alt_charset && 
        charset_convert(sms->sms.msgdata, DEFAULT_CHARSET,
                        octstr_get_cstr(conndata->alt_charset)) != 0)
        error(0, "Failed to convert msgdata from charset <%s> to <%s>, will send as is.",
                 DEFAULT_CHARSET, octstr_get_cstr(conndata->alt_charset));

    conndata->open_sends++;
    conndata->send_sms(conn, sms);

    /* obey throughput speed limit, if any */
    if (conn->throughput > 0)
        gwthread_sleep(delay);

    return 0;
}


static long httpsmsc_queued(SMSCConn *conn)
{
    ConnData *conndata = conn->data;

    return (conndata ? (conn->status != SMSCCONN_DEAD ? 
            conndata->open_sends : 0) : 0);
}


static int httpsmsc_shutdown(SMSCConn *conn, int finish_sending)
{
    ConnData *conndata = conn->data;

    debug("httpsmsc_shutdown", 0, "HTTP[%s]: Shutting down",
          octstr_get_cstr(conn->id));
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    conndata->shutdown = 1;

    http_close_port(conndata->port);
    return 0;
}


int smsc_http_create(SMSCConn *conn, CfgGroup *cfg)
{
    ConnData *conndata = NULL;
    Octstr *type;
    long portno;   /* has to be long because of cfg_get_integer */
    int ssl = 0;   /* indicate if SSL-enabled server should be used */
    Octstr *os;

    if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1) {
        error(0, "HTTP[%s]: 'port' invalid in smsc 'http' record.",
              octstr_get_cstr(conn->id));
        return -1;
    }
    cfg_get_bool(&ssl, cfg, octstr_imm("use-ssl"));
    if ((type = cfg_get(cfg, octstr_imm("system-type")))==NULL) {
        error(0, "HTTP[%s]: 'type' missing in smsc 'http' record.",
              octstr_get_cstr(conn->id));
        octstr_destroy(type);
        return -1;
    }
    conndata = gw_malloc(sizeof(ConnData));
    conndata->http_ref = NULL;
    conndata->success_regex = 
        conndata->permfail_regex = conndata->tempfail_regex = NULL;
    conndata->generic_foreign_id_regex = NULL;

    conndata->allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    conndata->send_url = cfg_get(cfg, octstr_imm("send-url"));
    conndata->dlr_url = cfg_get(cfg, octstr_imm("dlr-url"));
    conndata->username = cfg_get(cfg, octstr_imm("smsc-username"));
    conndata->password = cfg_get(cfg, octstr_imm("smsc-password"));
    conndata->system_id = cfg_get(cfg, octstr_imm("system-id"));
    cfg_get_bool(&conndata->no_sender, cfg, octstr_imm("no-sender"));
    cfg_get_bool(&conndata->no_coding, cfg, octstr_imm("no-coding"));
    cfg_get_bool(&conndata->no_sep, cfg, octstr_imm("no-sep"));
    conndata->proxy = cfg_get(cfg, octstr_imm("system-id"));
    conndata->alt_charset = cfg_get(cfg, octstr_imm("alt-charset"));
    conndata->fieldmap = NULL;

    if (conndata->send_url == NULL)
        panic(0, "HTTP[%s]: Sending not allowed. No 'send-url' specified.",
              octstr_get_cstr(conn->id));

    if (octstr_case_compare(type, octstr_imm("kannel")) == 0) {
        if (conndata->username == NULL || conndata->password == NULL) {
            error(0, "HTTP[%s]: 'username' and 'password' required for Kannel http smsc",
                  octstr_get_cstr(conn->id));
            goto error;
        }
        conndata->receive_sms = kannel_receive_sms;
        conndata->send_sms = kannel_send_sms;
        conndata->parse_reply = kannel_parse_reply;
    }
    else if (octstr_case_compare(type, octstr_imm("brunet")) == 0) {
        if (conndata->username == NULL) {
            error(0, "HTTP[%s]: 'username' (=CustomerId) required for bruNET http smsc",
                  octstr_get_cstr(conn->id));
            goto error;
        }
        conndata->receive_sms = brunet_receive_sms;
        conndata->send_sms = brunet_send_sms;
        conndata->parse_reply = brunet_parse_reply;
    }
    else if (octstr_case_compare(type, octstr_imm("xidris")) == 0) {
        if (conndata->username == NULL || conndata->password == NULL) {
            error(0, "HTTP[%s]: 'username' and 'password' required for Xidris http smsc",
                  octstr_get_cstr(conn->id));
            goto error;
        }
        conndata->receive_sms = xidris_receive_sms;
        conndata->send_sms = xidris_send_sms;
        conndata->parse_reply = xidris_parse_reply;
    }
    else if (octstr_case_compare(type, octstr_imm("wapme")) == 0) {
        if (conndata->username == NULL || conndata->password == NULL) {
            error(0, "HTTP[%s]: 'username' and 'password' required for Wapme http smsc",
                  octstr_get_cstr(conn->id));
            goto error;
        }
        conndata->receive_sms = kannel_receive_sms; /* emulate sendsms interface */
        conndata->send_sms = wapme_smsproxy_send_sms;
        conndata->parse_reply = wapme_smsproxy_parse_reply;
    }
    else if (octstr_case_compare(type, octstr_imm("clickatell")) == 0) {
        /* no required data checks here? */
        conndata->receive_sms = clickatell_receive_sms;
        conndata->send_sms = clickatell_send_sms;
        conndata->parse_reply = clickatell_parse_reply;
    }
    else if (octstr_case_compare(type, octstr_imm("generic")) == 0) {
        /* we need at least the criteria for a successfull sent */
        if ((os = cfg_get(cfg, octstr_imm("status-success-regex"))) == NULL) {
            error(0, "HTTP[%s]: 'status-success-regex' required for generic http smsc",
                  octstr_get_cstr(conn->id));
            goto error;
        }
        conndata->fieldmap = generic_get_field_map(cfg);
        conndata->receive_sms = generic_receive_sms; /* emulate sendsms interface */
        conndata->send_sms = generic_send_sms;
        conndata->parse_reply = generic_parse_reply;

        /* pre-compile regex expressions */
        if (os != NULL) {   /* this is implicite due to the above if check */
            if ((conndata->success_regex = gw_regex_comp(os, REG_EXTENDED|REG_NOSUB)) == NULL)
                panic(0, "Could not compile pattern '%s' defined for variable 'status-success-regex'", octstr_get_cstr(os));
            octstr_destroy(os);
        }
        if ((os = cfg_get(cfg, octstr_imm("status-permfail-regex"))) != NULL) {
            if ((conndata->permfail_regex = gw_regex_comp(os, REG_EXTENDED|REG_NOSUB)) == NULL)
                panic(0, "Could not compile pattern '%s' defined for variable 'status-permfail-regex'", octstr_get_cstr(os));
            octstr_destroy(os);
        }
        if ((os = cfg_get(cfg, octstr_imm("status-tempfail-regex"))) != NULL) {
            if ((conndata->tempfail_regex = gw_regex_comp(os, REG_EXTENDED|REG_NOSUB)) == NULL)
                panic(0, "Could not compile pattern '%s' defined for variable 'status-tempfail-regex'", octstr_get_cstr(os));
            octstr_destroy(os);
        }
        if ((os = cfg_get(cfg, octstr_imm("generic-foreign-id-regex"))) != NULL) {
            if ((conndata->generic_foreign_id_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
                panic(0, "Could not compile pattern '%s' defined for variable 'generic-foreign-id-regex'", octstr_get_cstr(os));
            else {
                /* check quickly that at least 1 group seems to be defined in the regex */
                if (octstr_search_char(os, '(', 0) == -1 || octstr_search_char(os, ')', 0) == -1)
                    warning(0, "HTTP[%s]: No group defined in pattern '%s' for variable 'generic-foreign-id-regex'", octstr_get_cstr(conn->id), octstr_get_cstr(os));
            }
            octstr_destroy(os);
        }
    }
    /*
     * ADD NEW HTTP SMSC TYPES HERE
     */
    else {
	error(0, "HTTP[%s]: system-type '%s' unknown smsc 'http' record.",
          octstr_get_cstr(conn->id), octstr_get_cstr(type));

	goto error;
    }	
    conndata->open_sends = 0;
    conndata->http_ref = http_caller_create();
    
    conn->data = conndata;
    conn->name = octstr_format("HTTP:%S", type);
    conn->status = SMSCCONN_ACTIVE;
    conn->connect_time = time(NULL);

    conn->shutdown = httpsmsc_shutdown;
    conn->queued = httpsmsc_queued;
    conn->send_msg = httpsmsc_send;

    if (http_open_port_if(portno, ssl, conn->our_host)==-1)
	goto error;

    conndata->port = portno;
    conndata->shutdown = 0;
    
    if ((conndata->receive_thread =
	 gwthread_create(httpsmsc_receiver, conn)) == -1)
	goto error;

    if ((conndata->send_cb_thread =
	 gwthread_create(httpsmsc_send_cb, conn)) == -1)
	goto error;

    info(0, "HTTP[%s]: Initiated and ready", octstr_get_cstr(conn->id));
    
    octstr_destroy(type);
    return 0;

error:
    error(0, "HTTP[%s]: Failed to create http smsc connection",
          octstr_get_cstr(conn->id));

    conn->data = NULL;
    conndata_destroy(conndata);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    octstr_destroy(type);
    return -1;
}

