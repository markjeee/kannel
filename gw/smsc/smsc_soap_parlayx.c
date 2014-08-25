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
 * smsc_soap_parlayx.c - Kannel SMSC module for ParlayX 2.1
 * 
 * Stipe Tolj <stolj at kannel.org>
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

#ifdef HAVE_GSOAP

#include "soapH.h"
#include "SendSmsBinding.nsmap"
#include "wsseapi.h"


/*
 * Define DEBUG macro to also activate the DLR sending
 * thread, which allows us to self-inject DLRs.
 */
#undef DEBUG
#define DEBUG 1

/* Default character encoding */
#define DEFAULT_CHARSET "UTF-8"


typedef struct ConnData {
    HTTPCaller *http_ref;
    long receive_thread;
    long senders;   /* number of concurrent sending threads */
    int shutdown;
    int	port;   /* port for receiving SMS'es */
    Octstr *allow_ip;
    Octstr *send_url;
    Octstr *dlr_url;
    long open_sends;
    Octstr *username;   /* if needed */
    Octstr *password;   /* as said */
    Octstr *alt_charset;    /* alternative charset use */
    gw_prioqueue_t *msgs_to_send;
    List *sender_threads;
    long dlr_thread;
    List *dlr_queue;

    /* callback functions */
    void (*send_sms) (SMSCConn *conn, Msg *msg);
    void (*parse_reply) (SMSCConn *conn, Msg *msg, int status,
                         List *headers, Octstr *body);
    void (*receive_sms) (SMSCConn *conn, HTTPClient *client,
                         List *headers, Octstr *body, List *cgivars);
    void (*httpsmsc_sender) (void *arg);
} ConnData;


static void octstr_remove_crlfs(Octstr *ostr);
static void soap_send_sms(struct soap *soap, SMSCConn *conn, Msg *sms);
#ifdef DEBUG
static void soap_send_dlr(struct soap *soap, SMSCConn *conn, Msg *sms);
#endif


/********************************************************************
 * DLR state mapping
 * 
 * See VMP spec v0.9, section 2.4.2, page 21 for the VMP specific
 * value details.
 */

static struct StateTable { 
    unsigned int dlr_mask;
    enum pxSms__DeliveryStatus state;
} state_table[] = { 
    #define ENTRY(mask, state) { mask, state }, 
    ENTRY(DLR_SUCCESS, pxSms__DeliveryStatus__DeliveredToTerminal)
    ENTRY(DLR_FAIL, pxSms__DeliveryStatus__DeliveryImpossible)
    ENTRY(DLR_BUFFERED, pxSms__DeliveryStatus__DeliveredToNetwork)
    ENTRY(DLR_SMSC_FAIL, pxSms__DeliveryStatus__DeliveryNotificationNotSupported)
    ENTRY(DLR_FAIL, pxSms__DeliveryStatus__DeliveryUncertain)
    ENTRY(DLR_BUFFERED, pxSms__DeliveryStatus__MessageWaiting)
    #undef ENTRY 
}; 

static int state_table_entries = sizeof(state_table) / sizeof(state_table[0]); 


/********************************************************************
 * Internal threads
 */

/*
 * Each sender thread has it's own gSOAP IO context that is passed along
 * to the sending function in the consume loop. This ensures we maintain
 * the TCP connection, along with the HTTP/1.1 keep-alive state for the
 * HTTP transport layer.
 */

/*
 * The various ParlayX variants use separate thread logic that are
 * addressed via the corresponding function pointer callback in 
 * conndata.
 * 
 * The variants 'ericsson-sdp' and 'oneapi-v1' have the same ParlayX
 * SOAP XML PDUs, but differ in the authentication scheme they use, where
 * 'ericsson-sdp' uses WS-Security via wsse and 'oneapi-v1' uses plain
 * HTTP basic authentication.
 */

static void httpsmsc_sender_ercisson_sdp(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    Msg *msg;
    struct soap *soap;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    /* establish soap context */
    soap = soap_new1(SOAP_XML_INDENT|SOAP_IO_KEEPALIVE);
    
    /* register wsse plugin */
    soap_register_plugin(soap, soap_wsse);
    
#ifdef WITH_OPENSSL
    /* setup the SSL context */
    if (soap_ssl_client_context(soap, SOAP_SSL_NO_AUTHENTICATION,
            NULL, NULL, NULL, NULL, NULL)) { 
        char buf[1024];
        Octstr *os;

        soap_sprint_fault(soap, buf, 1024);
        os = octstr_create(buf);
        octstr_remove_crlfs(os);

        error(0, "SOAP[%s]: Could not assign gSOAP SSL context:",
              octstr_get_cstr(conn->id));
        error(0, "SOAP[%s]: %s",
              octstr_get_cstr(conn->id), octstr_get_cstr(os));

        octstr_destroy(os);
        goto done;
    } 
#endif

    /* main consume loop for the sender */
    while ((msg = gw_prioqueue_consume(conndata->msgs_to_send)) != NULL) {
        
        /* message lifetime of 10 seconds */
        soap_wsse_add_Timestamp(soap, "Time", 10);
        
        /* add user name with digest password */
        soap_wsse_add_UsernameTokenDigest(soap, "User", 
                octstr_get_cstr(conndata->username), octstr_get_cstr(conndata->password));

        soap_send_sms(soap, conn, msg);
        conndata->open_sends--;
        
        /* clean up security header */
        soap_wsse_delete_Security(soap);
    }
    
done:    
    /* destroy soap context */
    soap_destroy(soap); 
    soap_end(soap);
    soap_free(soap); 
} 


static void httpsmsc_sender_gsma_oneapi(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    Msg *msg;
    struct soap *soap;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    /* establish soap context */
    soap = soap_new1(SOAP_XML_INDENT|SOAP_IO_KEEPALIVE);

    /* assign HTTP basic authentication tokens */
    soap->userid = octstr_get_cstr(conndata->username);
    soap->passwd = octstr_get_cstr(conndata->password);
    
#ifdef WITH_OPENSSL
    /* setup the SSL context */
    if (soap_ssl_client_context(soap, SOAP_SSL_NO_AUTHENTICATION,
            NULL, NULL, NULL, NULL, NULL)) { 
        char buf[1024];
        Octstr *os;

        soap_sprint_fault(soap, buf, 1024);
        os = octstr_create(buf);
        octstr_remove_crlfs(os);

        error(0, "SOAP[%s]: Could not assign gSOAP SSL context:",
              octstr_get_cstr(conn->id));
        error(0, "SOAP[%s]: %s",
              octstr_get_cstr(conn->id), octstr_get_cstr(os));

        octstr_destroy(os);
        goto done;
    } 
#endif

    /* main consume loop for the sender */
    while ((msg = gw_prioqueue_consume(conndata->msgs_to_send)) != NULL) {
        soap_send_sms(soap, conn, msg);
        conndata->open_sends--;
    }
    
done:    
    /* destroy soap context */
    soap_destroy(soap); 
    soap_end(soap);
    soap_free(soap); 
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

        debug("smsc.soap", 0, "SOAP[%s]: Got HTTP request `%s'", 
              octstr_get_cstr(conn->id), octstr_get_cstr(url));

        if (connect_denied(conndata->allow_ip, ip)) {
            info(0, "SOAP[%s]: Connection `%s' tried from denied "
                    "host %s, ignored", octstr_get_cstr(conn->id),
                    octstr_get_cstr(url), octstr_get_cstr(ip));
            http_close_client(client);
        } else
            conndata->receive_sms(conn, client, headers, body, cgivars);

        debug("smsc.soap", 0, "SOAP[%s]: Destroying client information",
              octstr_get_cstr(conn->id));
        octstr_destroy(url);
        octstr_destroy(ip);
        octstr_destroy(body);
        http_destroy_headers(headers);
        http_destroy_cgiargs(cgivars);
    }
    debug("smsc.soap", 0, "SOAP[%s]: httpsmsc_receiver dying",
          octstr_get_cstr(conn->id));

    conndata->shutdown = 1;
    http_close_port(conndata->port);
    
    /* unblock http_receive_result() if there are no open sends */
    if (conndata->open_sends == 0)
        http_caller_signal_shutdown(conndata->http_ref);
}


#ifdef DEBUG
static void dlr_sender(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    Msg *msg;
    struct soap *soap;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    /* establish soap context */
    soap = soap_new1(SOAP_XML_INDENT|SOAP_IO_KEEPALIVE);
    
#ifdef WITH_OPENSSL
    /* setup the SSL context */
    if (soap_ssl_client_context(soap, SOAP_SSL_NO_AUTHENTICATION,
            NULL, NULL, NULL, NULL, NULL)) { 
        char buf[1024];
        Octstr *os;

        soap_sprint_fault(soap, buf, 1024);
        os = octstr_create(buf);
        octstr_remove_crlfs(os);

        error(0, "SOAP[%s]: Could not assign gSOAP SSL context:",
              octstr_get_cstr(conn->id));
        error(0, "SOAP[%s]: %s",
              octstr_get_cstr(conn->id), octstr_get_cstr(os));

        octstr_destroy(os);
        goto done;
    } 
#endif
    
    while ((msg = gwlist_consume(conndata->dlr_queue)) != NULL) {
        /* first we delay a bit to simulate the SMSC latency */
        gwthread_sleep(1);
        soap_send_dlr(soap, conn, msg);
        msg_destroy(msg);
    }
    
done:    
    /* destroy soap context */
    soap_destroy(soap); 
    soap_end(soap);
    soap_free(soap); 
}
#endif


/***********************************************************************
 * Helper functions
 */

#define octstr_cstr(os) \
    (os ? octstr_get_cstr(os) : NULL)

#define octstr(os) \
    (os ? octstr_create(os) : NULL)


static int iscrlf(unsigned char c)
{
    return c == '\n' || c == '\r';
}


static void octstr_remove_crlfs(Octstr *ostr)
{
    int i, end;

    end = octstr_len(ostr);

    for (i = 0; i < end; i++) {
        if (iscrlf(octstr_get_char(ostr, i)))
            octstr_set_char(ostr, i, ' ');
    }
}


static void gw_free_wrapper(void *data)
{
    gw_free(data);
}


/***********************************************************************
 * gSOAP callbacks for internal XML transport
 */

typedef struct gBuffer {
    SMSCConn *conn;
    char *buffer;
    size_t size;
    size_t rlen, slen;
} gBuffer;


static gBuffer *gbuffer_create(SMSCConn *conn, size_t size)
{
    gBuffer *buf;
    
    buf = gw_malloc(sizeof(gBuffer));
    buf->conn = conn;
    buf->buffer = gw_malloc(size);
    buf->size = size;
    buf->rlen = buf->slen = 0;
    
    return buf;
}


static void gbuffer_destroy(gBuffer *buf)
{
    if (buf == NULL)
        return;
    
    gw_free(buf->buffer);
    gw_free(buf);
}


/*
 * Callback function of gSOAP internals that sends response bytes
 * via this callback. We use it to intercept the response to the
 * own handled buffer.
 */
static int mysend(struct soap *soap, const char *s, size_t n) 
{
    gBuffer *buf = (gBuffer*) soap->user;
    
    if (buf->slen + n > buf->size) 
        return SOAP_EOF; 
    
    strcpy(buf->buffer + buf->slen, s); 
    buf->slen += n; 
   
    return SOAP_OK; 
} 


/*
 * Callback function of gSOAP internals to read the HTTP POST
 * body contents into the gSOAP processing. We inject the input
 * via our own mapped buffer here.
 */
static size_t myrecv(struct soap *soap, char *s, size_t n) 
{ 
    gBuffer *buf = (gBuffer*) soap->user;
    
    strncpy(s, buf->buffer + buf->rlen, n); 
    buf->rlen += n;
    
    return n; 
} 


/*
 * Callback function of gSOAP's internal HTTP response headers.
 * We don't use them here and simply ensure that they are skipped.
 */
static int myheader(struct soap *soap, const char *key, const char *value) 
{
    return SOAP_OK;
}


/********************************************************************
 * SOAP specific operations
 */

static void soap_send_sms(struct soap *soap, SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    struct pxSmsSend__sendSmsResponse resp;
    int ret;
    char tid[UUID_STR_LEN + 1];
    Octstr *mid = NULL;
    Octstr *receiver;
    const char *receiv[1];
    
    /* request parameters */
    struct pxSmsSend__sendSms req;
    struct pxCommon__ChargingInformation charge;
    struct pxCommon__SimpleReference dlr;
    
    /* unparse our msg ID */
    if (DLR_IS_ENABLED_DEVICE(sms->sms.dlr_mask) && !uuid_is_null(sms->sms.id)) {
        uuid_unparse(sms->sms.id, tid);
        mid = octstr_create(tid);
    }

    req.__sizeaddresses = 1;
    receiver = octstr_format("tel:%s", octstr_get_cstr(sms->sms.receiver));
    receiv[0] = octstr_get_cstr(receiver);
    req.addresses = (char**)receiv;
    req.senderName = octstr_cstr(sms->sms.sender);
    req.message = octstr_get_cstr(sms->sms.msgdata);
    req.charging = NULL;
    req.receiptRequest = NULL;

    /* 
     * If billing identifier is set then we will parse for the
     * ChargingInformation fields using the following notation:
     * 
     *   binfo = <D><description><D><currency><D><amount><D><code>
     * 
     * where <D> is the delimiter character, defined as the FIRST
     * character of the binfo field. 
     */
    if (sms->sms.binfo) {
        Octstr *delim = octstr_copy(sms->sms.binfo, 0, 1);
        List *l = octstr_split(sms->sms.binfo, delim);
        if (gwlist_len(l) != 5) {
            error(0, "SOAP[%s]: Billing identifier <%s> has wrong format!",
                  octstr_get_cstr(conn->id), octstr_get_cstr(sms->sms.binfo));
        } else {
            charge.description = octstr_get_cstr(gwlist_get(l, 1));
            charge.currency = octstr_get_cstr(gwlist_get(l, 2));
            charge.amount = octstr_get_cstr(gwlist_get(l, 3));
            charge.code = octstr_get_cstr(gwlist_get(l, 3));
            req.charging = &charge;
        }
        gwlist_destroy(l, octstr_destroy_item);
        octstr_destroy(delim);
        gwlist_destroy(l, octstr_destroy_item);
    }

    /*
     * Define callback for DLR notification.
     */
    if (DLR_IS_ENABLED_DEVICE(sms->sms.dlr_mask) && conndata->dlr_url) {
        dlr.endpoint = octstr_get_cstr(conndata->dlr_url);
        dlr.correlator = octstr_get_cstr(mid);
        dlr.interfaceName = NULL;
        req.receiptRequest = &dlr;
    }

    /* perform the SOAP call itself */
    ret = soap_call___px1__sendSms(
            soap, octstr_get_cstr(conndata->send_url), "",
            &req, &resp);

    if (ret) {
        /* HTTP request failed, or any other SOAP fault raised. */
        char buf[1024];
        Octstr *os;

        soap_sprint_fault(soap, buf, 1024);
        os = octstr_create(buf);
        octstr_remove_crlfs(os);
        
        error(0, "SOAP[%s]: Sending HTTP request failed:",
              octstr_get_cstr(conn->id));
        error(0, "SOAP[%s]: %s",
              octstr_get_cstr(conn->id), octstr_get_cstr(os));
        
        /* 
         * TODO: we may consider this also as temporary error,
         * or even better interpret the SOAP fault detail tags.
         */
        bb_smscconn_send_failed(conn, sms, SMSCCONN_FAILED_MALFORMED,
                octstr_duplicate(os));

        octstr_destroy(os);
    } else {
        /* 
         * We got a corresponding SOAP/XML PDU response,
         * so this is considered a successful transaction.
         */

        /* 
         * The following code parts can be used to verify
         * for transaction time and authentication.
         * 
         * They are NOT used in the ParlayX variants we
         * support, but we keep them here for reference
         * to any possible new variant. 
         */
        
        /*
        if (soap_wsse_verify_Timestamp(soap)) {
            error(0, "SOAP[%s]: Server response expired.",
                    octstr_get_cstr(conn->id));
            goto done;
        }

        username = soap_wsse_get_Username(soap);
        if (!username || octstr_str_compare(conndata->username, username) ||
                soap_wsse_verify_Password(soap, octstr_get_cstr(conndata->password))) {
            error(0, "SOAP[%s]: Server authentication failed.",
                    octstr_get_cstr(conn->id));
            goto done;
        }
        */
        
        /*
         * The sendSmsResponse result is defined to be a message identifier
         * for the transaction we did, so keep that as foreign ID.
         */
        if (resp.result) {
            octstr_destroy(sms->sms.foreign_id);
            sms->sms.foreign_id = octstr_create(resp.result);
        }

        /* add to our own DLR storage */               
        if (DLR_IS_ENABLED_DEVICE(sms->sms.dlr_mask) && mid) {
            dlr_add(conn->id, mid, sms, 0);

#ifdef DEBUG
            /* 
             * Pass duplicate into DLR re-inject queue with the 
             * msg ID that the SMSC gave us.
             * BEWARE: This works only for internal tests where 
             * the msg ID returned is a UUID string. 
             */
            {
                Msg *msg = msg_duplicate(sms);
                uuid_clear(msg->sms.id);
                uuid_parse(octstr_get_cstr(mid), msg->sms.id);
                gwlist_produce(conndata->dlr_queue, msg);
            }
#endif
        }
        bb_smscconn_sent(conn, sms, NULL);
    }

    octstr_destroy(mid);     
    octstr_destroy(receiver);
}


static void soap_parse_reply(SMSCConn *conn, Msg *msg, int status,
 			                List *headers, Octstr *body)
{
    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
    
        /* add to our own DLR storage */               
        if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask)) {
            char id[UUID_STR_LEN + 1];
            Octstr *mid;

            uuid_unparse(msg->sms.id, id);
            mid = octstr_create(id); 

            dlr_add(conn->id, mid, msg, 0);

            octstr_destroy(mid);            
        }
            
        bb_smscconn_sent(conn, msg, NULL);
    } else {
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_MALFORMED, 
                octstr_duplicate(body));
    }
}


static void soap_receive_sms(SMSCConn *conn, HTTPClient *client,
                             List *headers, Octstr *body, List *cgivars)
{
    List *reply_headers;
    struct soap *soap;
    gBuffer *buf;
    Octstr *response;
    
    /*
     * We expect a SOAP/XML element as POST body. If there is no
     * body, then return an error instantly.
     */
    if (octstr_len(body) == 0) {
        http_send_reply(client, HTTP_BAD_METHOD, NULL, NULL);
        return;
    }
    
    /* dump the SOAP/XML we got */
    octstr_dump(body, 0);
    
    /* move the XML into the buffer */
    buf = gbuffer_create(conn, 32000);
    octstr_get_many_chars(buf->buffer, body, 0, octstr_len(body));

    /* create gSOAP context and assign the buffer */
    soap = soap_new();
    soap->fsend = mysend;
    soap->frecv = myrecv;
    soap->fposthdr = myheader;
    soap->user = buf;

    /* perform the server operation */
    soap_serve(soap);

    /* move response XML from buffer to octstr */
    response = octstr_create("");
    octstr_append_data(response, buf->buffer, buf->slen);
    
    /* destroy buffer */
    gbuffer_destroy(buf);
    
    /* destroy context */
    soap_destroy(soap); 
    soap_end(soap); 
    soap_free(soap); 

    /* send the HTTP response */
    reply_headers = gwlist_create();
    http_header_add(reply_headers, "SOAPAction" , "\"\"");
    http_header_add(reply_headers, "Content-Type", "text/xml;charset=\"utf-8\"");
    debug("smsc.soap", 0, "SOAP[%s]: Sending HTTP response",
          octstr_get_cstr(conn->id));
    octstr_dump(response, 0);
    http_send_reply(client, HTTP_OK, reply_headers, response);

    octstr_destroy(response);
    http_destroy_headers(reply_headers);
}


static void soap_send_sms_cb(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    
    gw_prioqueue_produce(conndata->msgs_to_send, sms);
}


#ifdef DEBUG
static void soap_send_dlr(struct soap *soap, SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    struct pxSmsNotification__notifySmsDeliveryReceiptResponse resp;
    int ret;
    char tid[UUID_STR_LEN + 1];
    Octstr *mid = NULL;
    Octstr *receiver = octstr_format("tel:%s", sms->sms.receiver);
    
    /* request parameters */
    struct pxSmsNotification__notifySmsDeliveryReceipt req;
    struct pxSms__DeliveryInformation deliveryStatus;
    
    /* get the message id */
    if (!uuid_is_null(sms->sms.id)) {
        uuid_unparse(sms->sms.id, tid);
        mid = octstr_create(tid);
    }

    /* apply values from Kannel msg struct */
    req.correlator = octstr_get_cstr(mid);
    deliveryStatus.address = octstr_get_cstr(receiver);
    deliveryStatus.deliveryStatus = pxSms__DeliveryStatus__DeliveredToTerminal;
    req.deliveryStatus = &deliveryStatus;
    
    /* no SOAP header set */

    /* perform the SOAP call itself */
    ret = soap_call___px2__notifySmsDeliveryReceipt(
            soap, octstr_get_cstr(conndata->send_url), "", &req, &resp);

    if (ret) {
        /* HTTP request failed */
        char buf[1024];
        Octstr *os;

        soap_sprint_fault(soap, buf, 1024);
        os = octstr_create(buf);
        octstr_remove_crlfs(os);
        
        error(0, "SOAP[%s]: Sending DLR HTTP request failed:",
              octstr_get_cstr(conn->id));
        error(0, "SOAP[%s]: %s",
              octstr_get_cstr(conn->id), octstr_get_cstr(os));
        
        octstr_destroy(os);
    } else {
        /* we got a corresponding SOAP/XML response */

        debug("smsc.soap",0,"SOAP[%s] Received DLR HTTP response.",
              octstr_get_cstr(conn->id));
    }

    octstr_destroy(receiver);
    octstr_destroy(mid);
}
#endif


/********************************************************************
 * Internal smscconn operations
 */

static void conndata_destroy(ConnData *conndata)
{
    if (conndata == NULL)
        return;
    
    if (conndata->http_ref)
        http_caller_destroy(conndata->http_ref);

    gwlist_destroy(conndata->sender_threads, (void(*)(void *)) gw_free_wrapper);
    gw_prioqueue_destroy(conndata->msgs_to_send, msg_destroy_item);
#ifdef DEBUG
    gwlist_destroy(conndata->dlr_queue, msg_destroy_item);
#endif
    
    octstr_destroy(conndata->allow_ip);
    octstr_destroy(conndata->send_url);
    octstr_destroy(conndata->dlr_url);
    octstr_destroy(conndata->username);
    octstr_destroy(conndata->password);
    octstr_destroy(conndata->alt_charset);

    gw_free(conndata);
}


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
    long *id;

    debug("httpsmsc_shutdown", 0, "SOAP[%s]: Shutting down",
          octstr_get_cstr(conn->id));
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    conndata->shutdown = 1;

#ifdef DEBUG
    /* stop DLR re-injection thread */
    gwlist_remove_producer(conndata->dlr_queue);
    gwthread_join(conndata->dlr_thread);
#endif    

    /* stop receiver thread */
    http_close_port(conndata->port);

    /* stop all sender threads */
    gw_prioqueue_remove_producer(conndata->msgs_to_send);
    while ((id = gwlist_consume(conndata->sender_threads)) != NULL) {
        gwthread_join(*id);
        gw_free(id);
    }
    
    conn->data = NULL;
    conndata_destroy(conndata);

    conn->status = SMSCCONN_DEAD;
    bb_smscconn_killed();

    return 0;
}


int smsc_soap_parlayx_create(SMSCConn *conn, CfgGroup *cfg)
{
    ConnData *conndata = NULL;
    Octstr *type;
    long portno;   /* has to be long because of cfg_get_integer */
    int ssl = 0;   /* indicate if SSL-enabled server should be used */
    int i;

    if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1) {
        error(0, "SOAP[%s]: 'port' invalid in 'smsc = parlayx' group.",
              octstr_get_cstr(conn->id));
        return -1;
    }
    cfg_get_bool(&ssl, cfg, octstr_imm("use-ssl"));
    if ((type = cfg_get(cfg, octstr_imm("system-type"))) == NULL) {
        error(0, "SOAP[%s]: 'system-type' missing in 'smsc = parlayx' group.",
              octstr_get_cstr(conn->id));
        octstr_destroy(type);
        return -1;
    }
    conndata = gw_malloc(sizeof(ConnData));
    conndata->http_ref = NULL;
    conndata->allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    conndata->send_url = cfg_get(cfg, octstr_imm("send-url"));
    conndata->dlr_url = cfg_get(cfg, octstr_imm("dlr-url"));
    conndata->username = cfg_get(cfg, octstr_imm("smsc-username"));
    conndata->password = cfg_get(cfg, octstr_imm("smsc-password"));
    conndata->alt_charset = cfg_get(cfg, octstr_imm("alt-charset"));
    if (cfg_get_integer(&(conndata->senders), cfg, octstr_imm("window")) == -1) {
        conndata->senders = 1;
    } else {
        info(0, "SOAP[%s]: Using %ld sender threads.", 
             octstr_get_cstr(conn->id), conndata->senders);
    }

    if (conndata->send_url == NULL)
        panic(0, "SOAP[%s]: Sending not allowed. No 'send-url' specified.",
              octstr_get_cstr(conn->id));

    if (conndata->dlr_url == NULL)
        warning(0, "SOAP[%s]: DLR requesting not allowed. No 'dlr-url' specified.",
                octstr_get_cstr(conn->id));

    if (conndata->username == NULL || conndata->password == NULL) {
        error(0, "SOAP[%s]: 'username' and 'password' required for smsc",
              octstr_get_cstr(conn->id));
        goto error;
    }
    
    if (octstr_case_compare(type, octstr_imm("ericsson-sdp")) == 0) {
        conndata->httpsmsc_sender = httpsmsc_sender_ercisson_sdp;
    }
    else if (octstr_case_compare(type, octstr_imm("oneapi-v1")) == 0) {
        conndata->httpsmsc_sender = httpsmsc_sender_gsma_oneapi;
    }
    /*
     * Add new ParlayX variants here
     */
    else {
        error(0, "SOAP[%s]: system-type '%s' unknown in 'smsc = parlayx' group.",
              octstr_get_cstr(conn->id), octstr_get_cstr(type));
        goto error;
    }   
    
#ifdef WITH_OPENSSL    
    /* setup gSOAP SSL internals */
    soap_ssl_init();
#endif
    
    /* setup MT queue */
    conndata->msgs_to_send = gw_prioqueue_create(sms_priority_compare);
    
    /* assign our SOAP operations */
    conndata->receive_sms = soap_receive_sms;
    conndata->send_sms = soap_send_sms_cb;
    conndata->parse_reply = soap_parse_reply;

    conndata->open_sends = 0;
    conndata->http_ref = http_caller_create();
    
    conn->data = conndata;
    conn->name = octstr_create("SOAP");
    conn->status = SMSCCONN_ACTIVE;
    conn->connect_time = time(NULL);

    conn->shutdown = httpsmsc_shutdown;
    conn->queued = httpsmsc_queued;
    conn->send_msg = httpsmsc_send;

    if (http_open_port_if(portno, ssl, conn->our_host) == -1)
        goto error;

    conndata->port = portno;
    conndata->shutdown = 0;

    /* receiver thread */
    if ((conndata->receive_thread = gwthread_create(httpsmsc_receiver, conn)) == -1)
        goto error;
    
#ifdef DEBUG
    /* DLR re-injection thread */
    conndata->dlr_queue = gwlist_create();
    gwlist_add_producer(conndata->dlr_queue);
    if ((conndata->dlr_thread = gwthread_create(dlr_sender, conn)) == -1)
        goto error;
#else
    conndata->dlr_queue = NULL;
    conndata->dlr_thread = -1;
#endif

    /* sender thread(s), keep record of the thread IDs in our 
     * sender_threads list to ensure we join the right threads
     * at the shutdown sequence. */
    conndata->sender_threads = gwlist_create();
    gw_prioqueue_add_producer(conndata->msgs_to_send);
    for (i = 0; i < conndata->senders; i++) {
        long *id = gw_malloc(sizeof(long));
        if ((*id = gwthread_create(conndata->httpsmsc_sender, conn)) == -1) {
            gw_free(id);
            goto error;
        }
        gwlist_produce(conndata->sender_threads, id);
    }

    info(0, "SOAP[%s]: Initiated and ready", octstr_get_cstr(conn->id));
    
    octstr_destroy(type);
    return 0;

error:
    error(0, "SOAP[%s]: Failed to create smsc connection",
          octstr_get_cstr(conn->id));

    conn->data = NULL;
    conndata_destroy(conndata);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;

    octstr_destroy(type);
    return -1;
}


/********************************************************************
 * SOAP specific methods
 */

/*
 * This operation is handled by the server side. We just implement it 
 * here with hard-coded values to be able to test our own gSOAP sending
 * routines to our own HTTP end-point.
 */
int __px1__sendSms (
        /* soap structure hook */
        struct soap *soap,
        /* request struct */
        struct pxSmsSend__sendSms *req,
        /* response struct */ 
        struct pxSmsSend__sendSmsResponse *resp)
{
    /* positive response */
    resp->result = "100";

    /* negative response */
    /*
    resp->result = "300";
    */

    return SOAP_OK;
}


int __px2__notifySmsDeliveryReceipt (
        /* soap structure hook */ 
        struct soap *soap,
        /* request struct */
        struct pxSmsNotification__notifySmsDeliveryReceipt *req,
        /* response struct */ 
        struct pxSmsNotification__notifySmsDeliveryReceiptResponse *resp)
{
    gBuffer *buf = soap->user;
    ConnData *conndata = buf->conn->data;
    Msg *dlrmsg;
    Octstr *id, *destination;
    int sm_status, state, i;
    
    destination = NULL;
    sm_status = -1;
    
    /* get corresponding values from the SOAP/XML request */
    id = octstr(req->correlator);
    if (req->deliveryStatus) {
        destination = octstr(req->deliveryStatus->address);
        
        /* strip 'tel:' from address */
        if (destination)
            octstr_delete_matching(destination, octstr_imm("tel:"));

        sm_status = req->deliveryStatus->deliveryStatus;
    }
    
    /* map the DLR state */        
    state = DLR_NOTHING;
    for (i = 0; sm_status != -1 && i < state_table_entries; ++i) { 
        if (sm_status == state_table[i].state) {
            state = state_table[i].dlr_mask;
            break;
        }
    }
    
    /* resolve the DLR from the DLR temp storage */
    dlrmsg = dlr_find(buf->conn->id,
        id, /* smsc message id */
        destination, /* destination */
        state, 0);
    
    if (dlrmsg != NULL) {

        dlrmsg->sms.sms_type = report_mo;
        dlrmsg->sms.account = octstr_duplicate(conndata->username);

        /* 
         * There is no response values returned.
         */
        
        /* passing DLR to upper layer */
        bb_smscconn_receive(buf->conn, dlrmsg);
    
    }  else {
        error(0,"SOAP[%s]: got DLR but could not find message or was not interested "
                "in it id<%s> dst<%s>, type<%d>",
                octstr_get_cstr(buf->conn->id), octstr_get_cstr(id),
                octstr_get_cstr(destination), state);
    }
    
    octstr_destroy(id);
    octstr_destroy(destination);

    return SOAP_OK; 
}


int __px2__notifySmsReception (
        /* soap structure hook */ 
        struct soap *soap,
        /* request struct */
        struct pxSmsNotification__notifySmsReception *req,
        /* response struct */ 
        struct pxSmsNotification__notifySmsReceptionResponse *resp)
{
    gBuffer *buf = soap->user;
    ConnData *conndata = buf->conn->data;
    Msg *msg;
    int ret;

    msg = msg_create(sms);
    msg->sms.sms_type = mo;
    msg->sms.sender = octstr(req->message->senderAddress);
    msg->sms.receiver = octstr(req->message->smsServiceActivationNumber);
    msg->sms.msgdata = octstr(req->message->message);
    msg->sms.foreign_id = octstr(req->correlator);
    msg->sms.smsc_id = octstr_duplicate(buf->conn->id);
    msg->sms.time = time(NULL);
    msg->sms.account = octstr_duplicate(conndata->username);
    
    ret = bb_smscconn_receive(buf->conn, msg);
    if (ret == SMSCCONN_SUCCESS) {
        /* no response is set */
    } else {
        /* no response is set */
    }
    
    return SOAP_OK; 
}


/********************************************************************
 * SOAP functions stubs that are not used.
 */

int __px1__sendSmsLogo(
  struct soap *soap,
  // request parameters:
  struct pxSmsSend__sendSmsLogo*            pxSmsSend__sendSmsLogo,
  // response parameters:
  struct pxSmsSend__sendSmsLogoResponse*    pxSmsSend__sendSmsLogoResponse
) 
{
    return SOAP_OK; 
}

int __px1__sendSmsRingtone(
  struct soap *soap,
  // request parameters:
  struct pxSmsSend__sendSmsRingtone*        pxSmsSend__sendSmsRingtone,
  // response parameters:
  struct pxSmsSend__sendSmsRingtoneResponse* pxSmsSend__sendSmsRingtoneResponse
) 
{
    return SOAP_OK; 
}

int __px1__getSmsDeliveryStatus(
  struct soap *soap,
  // request parameters:
  struct pxSmsSend__getSmsDeliveryStatus*   pxSmsSend__getSmsDeliveryStatus,
  // response parameters:
  struct pxSmsSend__getSmsDeliveryStatusResponse* pxSmsSend__getSmsDeliveryStatusResponse
) 
{
    return SOAP_OK; 
}

int __px3__getReceivedSms(
  struct soap *soap,
  // request parameters:
  struct pxSmsReceive__getReceivedSms*         pxSmsReceive__getReceivedSms,
  // response parameters:
  struct pxSmsReceive__getReceivedSmsResponse* pxSmsReceive__getReceivedSmsResponse
) 
{
    return SOAP_OK; 
}

#endif  /* HAVE_GSOAP */
