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
 * gw/wap-appl.c - wapbox application layer and push ota indication, response
 * and confirmation primitive implementation.
 *
 * This module implements various indication and confirmation primitives and 
 * protocol mappings as defined in:
 * 
 *   WAP-189-PushOTA-20000217-a (hereafter called ota)
 *   WAP-200-WDP-20001212-a (wdp)
 *   WAP-248-UAProf-20011020-a (UAProf)
 * 
 * Wapbox application layer itself is not a WAP Forum protocol. 
 *
 * The application layer is reads events from its event queue, fetches the 
 * corresponding URLs and feeds back events to the WSP layer (pull). 
 *
 * In addition, the layer forwards WSP events related to push to the module 
 * wap_push_ppg and wsp, implementing indications, responses and confirma-
 * tions of ota.
 *
 * Note that push header encoding and decoding are divided two parts:
 * first decoding and encoding numeric values and then packing these values
 * into WSP format and unpacking them from WSP format. This module contains
 * encoding part.
 *
 * Lars Wirzenius
 */

#include <string.h>

#include "gwlib/gwlib.h"
#include "wmlscript/ws.h"
#include "xml_shared.h"
#include "wml_compiler.h"
#include "mime_decompiler.h"
#include "wap/wap.h"
#include "wap-appl.h"
#include "wap_push_ppg.h"
#include "wap/wsp_strings.h"
#include "wap/wsp_caps.h"
#include "wap/wsp.h"
#ifdef ENABLE_COOKIES
#include "wap/cookies.h"
#endif
#include "radius/radius_acct.h"
#include "wap-error.h"
#include "wap-maps.h"

#define ENABLE_NOT_ACCEPTED 

/*
 * Give the status the module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } run_status = limbo;


/*
 * The queue of incoming events.
 */
static List *queue = NULL;


/*
 * HTTP caller identifier for application layer.
 */
static HTTPCaller *caller = NULL;


/*
 * Number of currently running HTTP fetching threads.
 */
static Counter *fetches = NULL;


/*
 * Charsets supported by WML compiler, queried from wml_compiler.
 */
static List *charsets = NULL;

struct content {
    Octstr *body;
    Octstr *type;
    Octstr *charset;
    Octstr *url;
    Octstr *version;
};


/*
 * A mapping from HTTP request identifiers to information about the request.
 */
struct request_data {
    long client_SDU_size;
    WAPEvent *event;
    long session_id;
    Octstr *method;
    Octstr *url;
    long x_wap_tod;
    List *request_headers;
    Octstr *msisdn;
};


/*
 * WSP smart error messaging
 */
extern int wsp_smart_errors;
extern Octstr *device_home;

/*
 * Defines if PPG is running in wapbox instance
 */
static int have_ppg = 0;

/*
 * Private functions.
 */

static void main_thread(void *);
static void start_fetch(WAPEvent *);
static void return_replies_thread(void *);

static void dev_null(const char *data, size_t len, void *context);

static Octstr *convert_wml_to_wmlc(struct content *content);
static Octstr *convert_wmlscript_to_wmlscriptc(struct content *content);
/* DAVI: To-Do static Octstr *convert_multipart_mixed(struct content *content); */
static Octstr *deconvert_multipart_formdata(struct content *content);
/* DAVI: To-Do static Octstr *deconvert_mms_message(struct content *content); */
static List *negotiate_capabilities(List *req_caps);

static struct {
    char *type;
    char *result_type;
    Octstr *(*convert)(struct content *);
} converters[] = {
    { "text/vnd.wap.wml",
      "application/vnd.wap.wmlc",
      convert_wml_to_wmlc },
    { "text/vnd.wap.wmlscript",
      "application/vnd.wap.wmlscriptc",
      convert_wmlscript_to_wmlscriptc },
/* DAVI: To-Do
    { "multipart/mixed",
      "application/vnd.wap.multipart.mixed",
      convert_multipart_mixed }, 
*/
};
#define NUM_CONVERTERS ((long)(sizeof(converters) / sizeof(converters[0])))

static struct {
    char *type;
    char *result_type;
    Octstr *(*deconvert)(struct content *);
} deconverters[] = {
    { "application/vnd.wap.multipart.form-data",
      "multipart/form-data; boundary=kannel_boundary",
      deconvert_multipart_formdata },
/* DAVI: To-Do
    { "application/vnd.wap.mms-message",
      "multipart/related; type=application/smil; boundary=kannel_boundary; start=<t0>",
      deconvert_mms_message },
*/
};
#define NUM_DECONVERTERS ((long)(sizeof(deconverters) / sizeof(deconverters[0])))

/*
 * Following functions implement indications and conformations part of Push
 * OTA protocol.
 */
static void indicate_push_connection(WAPEvent *e);
static void indicate_push_disconnect(WAPEvent *e);
static void indicate_push_suspend(WAPEvent *e);
static void indicate_push_resume(WAPEvent *e);
static void confirm_push(WAPEvent *e);
static void indicate_push_abort(WAPEvent *e);
static void split_header_list(List **headers, List **new_headers, char *name);
static void check_application_headers(List **headers, List **app_headers);
static void decode_bearer_indication(List **headers, List **bearer_headers);
static void response_push_connection(WAPEvent *e);

/***********************************************************************
 * The public interface to the application layer.
 */

void wap_appl_init(Cfg *cfg) 
{
    gw_assert(run_status == limbo);
    queue = gwlist_create();
    fetches = counter_create();
    gwlist_add_producer(queue);
    run_status = running;
    charsets = wml_charsets();
    caller = http_caller_create();
    gwthread_create(main_thread, NULL);
    gwthread_create(return_replies_thread, NULL);

    if (cfg != NULL)
        have_ppg = 1;
    else
        have_ppg = 0;
}


void wap_appl_shutdown(void) 
{
    gw_assert(run_status == running);
    run_status = terminating;
    
    gwlist_remove_producer(queue);
    gwthread_join_every(main_thread);
    
    http_caller_signal_shutdown(caller);
    gwthread_join_every(return_replies_thread);
    
    wap_map_destroy(); 
    wap_map_user_destroy(); 
    http_caller_destroy(caller);
    gwlist_destroy(queue, wap_event_destroy_item);
    gwlist_destroy(charsets, octstr_destroy_item);
    counter_destroy(fetches);
}


void wap_appl_dispatch(WAPEvent *event) 
{
    gw_assert(run_status == running);
    gwlist_produce(queue, event);
}


long wap_appl_get_load(void) 
{
    gw_assert(run_status == running);
    return counter_value(fetches) + gwlist_len(queue);
}


/***********************************************************************
 * Private functions.
 */

/*
 * When we have a push event, create ota indication or confirmation and send 
 * it to ppg module. 
 * Because Accept-Application and Bearer-Indication are optional, we cannot 
 * rely on them. We must ask ppg main module do we have an open push session 
 * for this initiator. Push is identified by push id.
 * If there is no ppg configured, do not refer to ppg's sessions' list.
 */
static void main_thread(void *arg) 
{
    WAPEvent *ind, *res;
    long sid;
    WAPAddrTuple *tuple;
    
    while (run_status == running && (ind = gwlist_consume(queue)) != NULL) {
    switch (ind->type) {
    case S_MethodInvoke_Ind:
        res = wap_event_create(S_MethodInvoke_Res);
        res->u.S_MethodInvoke_Res.server_transaction_id =
            ind->u.S_MethodInvoke_Ind.server_transaction_id;
        res->u.S_MethodInvoke_Res.session_id =
        ind->u.S_MethodInvoke_Ind.session_id;
        wsp_session_dispatch_event(res);
        start_fetch(ind);
        break;
	
    case S_Unit_MethodInvoke_Ind:
        start_fetch(ind);
        break;

    case S_Connect_Ind:
        tuple  = ind->u.S_Connect_Ind.addr_tuple;
        if (have_ppg && wap_push_ppg_have_push_session_for(tuple)) {
            indicate_push_connection(ind);
        } else {
            res = wap_event_create(S_Connect_Res);
            /* FIXME: Not yet used by WSP layer */
            res->u.S_Connect_Res.server_headers = NULL;
            res->u.S_Connect_Res.negotiated_capabilities =
                negotiate_capabilities(ind->u.S_Connect_Ind.requested_capabilities);
            res->u.S_Connect_Res.session_id = 
                   ind->u.S_Connect_Ind.session_id;
            wsp_session_dispatch_event(res);
        }
        wap_event_destroy(ind);
        break;
	
    case S_Disconnect_Ind:
	    sid = ind->u.S_Disconnect_Ind.session_handle;
        if (have_ppg && wap_push_ppg_have_push_session_for_sid(sid)) 
            indicate_push_disconnect(ind);
        wap_event_destroy(ind);
        break;

    case S_Suspend_Ind:
	    sid = ind->u.S_Suspend_Ind.session_id;
        if (have_ppg && wap_push_ppg_have_push_session_for_sid(sid)) 
            indicate_push_suspend(ind);
	    wap_event_destroy(ind);
	    break;

    case S_Resume_Ind:
	    sid = ind->u.S_Resume_Ind.session_id;
        if (have_ppg && wap_push_ppg_have_push_session_for_sid(sid)) {
            indicate_push_resume(ind);
        } else {
            res = wap_event_create(S_Resume_Res);
            res->u.S_Resume_Res.server_headers = NULL;
            res->u.S_Resume_Res.session_id = ind->u.S_Resume_Ind.session_id;
            wsp_session_dispatch_event(res);
        }
        wap_event_destroy(ind);
        break;
	
    case S_MethodResult_Cnf:
        wap_event_destroy(ind);
        break;

    case S_ConfirmedPush_Cnf:
        confirm_push(ind);
        wap_event_destroy(ind);
        break;
	
    case S_MethodAbort_Ind:
        /* XXX Interrupt the fetch thread somehow */
        wap_event_destroy(ind);
        break;

    case S_PushAbort_Ind:
        indicate_push_abort(ind);
        wap_event_destroy(ind);
        break;

    case Pom_Connect_Res:
        response_push_connection(ind);
        wap_event_destroy(ind);
        break;
	
	default:
        panic(0, "WAP-APPL: Can't handle %s event", 
              wap_event_name(ind->type));
        break;
    } /* switch */
    } /* while */
}


/*
 * Tries to convert or compile a specific content-type to
 * it's complementing one. It does not convert if the client has explicitely
 * told us via Accept: header that a specific type is supported.
 * Returns 1 if an convertion has been successfull,
 * -1 if an convertion failed and 0 if no convertion routine
 * was maching this content-type
 */
static int convert_content(struct content *content, List *request_headers, 
                           int allow_empty) 
{
    Octstr *new_body;
    int failed = 0;
    int i;

    for (i = 0; i < NUM_CONVERTERS; i++) {
        if (octstr_str_compare(content->type, converters[i].type) == 0 &&
            !http_type_accepted(request_headers, octstr_get_cstr(content->type))) {
            debug("wap.convert",0,"WSP: Converting from <%s> to <%s>", 
                  octstr_get_cstr(content->type), converters[i].result_type);
            /* 
             * Note: if request is HEAD, body is empty and we still need to adapt
             * content-type but we don't need to convert a 0 bytes body 
             */
            if (allow_empty && octstr_len(content->body) == 0) 
                return 1;

            new_body = converters[i].convert(content);
            if (new_body != NULL) {
                long s = octstr_len(content->body);
                octstr_destroy(content->body);
                octstr_destroy(content->type);
                content->body = new_body;
                content->type = octstr_create(converters[i].result_type);
                debug("wap.convert",0,"WSP: Content-type is "
                      "now <%s>, size %ld bytes (before: %ld bytes), content body is:", 
                      converters[i].result_type, octstr_len(new_body), s);
                octstr_dump(new_body, 0);
                return 1;
            }
            debug("wap.convert",0,"WSP: Content convertion failed!");
            failed = 1;
        }
    }
    
    return (failed ? -1 : 0);
}


/* 
 * Tries to deconvert or decompile a specific content-type to
 * it's complementing one.
 * Returns 1 if an deconvertion has been successfull,
 * -1 if an deconvertion failed and 0 if no deconvertion routine
 * was maching this content-type
 */
static int deconvert_content(struct content *content) 
{
    Octstr *new_body;
    int failed = 0;
    int i;

    debug("wap.deconvert",0,"WSP deconvert: Trying to deconvert:"); 
    octstr_dump(content->body, 0);
    for (i = 0; i < NUM_DECONVERTERS; i++) {
        if (octstr_str_compare(content->type, deconverters[i].type) == 0) {
            debug("wap.deconvert",0,"WSP: Deconverting from <%s> to <%s>", 
                  octstr_get_cstr(content->type), 
            deconverters[i].result_type);
            new_body = deconverters[i].deconvert(content);
            if (new_body != NULL) {
                long s = octstr_len(content->body);
                octstr_destroy(content->body);
                octstr_destroy(content->type);
                content->body = new_body;
                content->type = octstr_create(deconverters[i].result_type);
                debug("wap.convert",0,"WSP: Content-type is "
                      "now <%s>, size %ld bytes (before: %ld bytes), content body is:", 
                deconverters[i].result_type, octstr_len(new_body), s);
                octstr_dump(new_body, 0);
                return 1;
            }
            debug("wap.deconvert",0,"WSP: Content convertion failed!");
            failed = 1;
        }
    }
    
    return (failed ? -1 : 0);
}


/* Add a header identifying our gateway version */
static void add_kannel_version(List *headers) 
{
    http_header_add(headers, "X-WAP-Gateway", GW_NAME "/" GW_VERSION);
}


/* Add Accept-Charset: headers for stuff the WML compiler can
 * convert to UTF-8. */
/* XXX This is not really correct, since we will not be able
 * to handle those charsets for all content types, just WML/XHTML. */
static void add_charset_headers(List *headers) 
{
    if (!http_charset_accepted(headers, "utf-8"))
        http_header_add(headers, "Accept-Charset", "utf-8");
}


/* Add Accept: headers for stuff we can convert for the phone */
static void add_accept_headers(List *headers) 
{
    int i;
    
    for (i = 0; i < NUM_CONVERTERS; i++) {
        if (http_type_accepted(headers, "*/*") || (
            http_type_accepted(headers, converters[i].result_type)
            && !http_type_accepted(headers, converters[i].type))) {
            http_header_add(headers, "Accept", converters[i].type);
        }
    }
}


/* Add X-WAP-Network-Client-IP: header to proxy client IP to HTTP server */
static void add_network_info(List *headers, WAPAddrTuple *addr_tuple) 
{
    if (octstr_len(addr_tuple->remote->address) > 0) {
        http_header_add(headers, "X-WAP-Network-Client-IP", 
                        octstr_get_cstr(addr_tuple->remote->address));
    }
}


/* Add X-WAP-Session-ID: header to request */
static void add_session_id(List *headers, long session_id) 
{
    if (session_id != -1) {
        char buf[40];
        sprintf(buf, "%ld", session_id);
        http_header_add(headers, "X-WAP-Session-ID", buf);
    }
}


/* Add X-WAP-Client-SDU-Size: to provide information on client capabilities */
static void add_client_sdu_size(List *headers, long sdu_size) 
{
    if (sdu_size > 0) {
        Octstr *buf;
	
        buf = octstr_format("%ld", sdu_size);
        http_header_add(headers, "X-WAP-Client-SDU-Size", octstr_get_cstr(buf));
        octstr_destroy(buf);
    }
}

/* Add proxy Via: header to request with our Kannel version */
static void add_via(List *headers) 
{
    Octstr *os;
    Octstr *version;
    
    version = http_header_value(headers, octstr_imm("Encoding-Version"));
    os = octstr_format("WAP/%s %S (" GW_NAME "/%s)", 
                       (version ? octstr_get_cstr(version) : "1.1"),
                       get_official_name(), GW_VERSION);
    http_header_add(headers, "Via", octstr_get_cstr(os));
    octstr_destroy(os);
    octstr_destroy(version);
}


/*
 * Add an X-WAP.TOD header to the response headers.  It is defined in
 * the "WAP Caching Model" specification.
 * We generate it in textual form and let WSP header packing convert it
 * to binary form.
 */
static void add_x_wap_tod(List *headers) 
{
    Octstr *gateway_time;
    
    gateway_time = date_format_http(time(NULL));
    if (gateway_time == NULL) {
        warning(0, "Could not add X-WAP.TOD response header.");
        return;
    }
    
    http_header_add(headers, "X-WAP.TOD", octstr_get_cstr(gateway_time));
    octstr_destroy(gateway_time);
}


/* Add MSISDN provisioning information to HTTP header */
static void add_msisdn(List *headers, WAPAddrTuple *addr_tuple, 
                       Octstr *send_msisdn_header) 
{
    Octstr *msisdn = NULL;
    Octstr *value = NULL;

    if (send_msisdn_header == NULL || octstr_len(send_msisdn_header) == 0)
        return;

    /* 
     * Security considerations. If there are headers with the header name we
     * use to pass on the MSISDN number, then remove them.
     */
    if ((value = http_header_value(headers, send_msisdn_header)) != NULL) {
        warning(0, "MSISDN header <%s> already present on request, "
                   "header value=<%s>", octstr_get_cstr(send_msisdn_header), 
                  octstr_get_cstr(value));
        http_header_remove_all(headers, octstr_get_cstr(send_msisdn_header));
    }

    /* 
     * XXX Add generic msisdn provisioning cleanly in here!
     * See revision 1.89 for Bruno's try.
     */

    /* We do not accept NULL values to be added to the HTTP header */
    if ((msisdn = radius_acct_get_msisdn(addr_tuple->remote->address)) != NULL) {
        http_header_add(headers, octstr_get_cstr(send_msisdn_header), octstr_get_cstr(msisdn));
    }

    octstr_destroy(value);
    octstr_destroy(msisdn);
}


/* 
 * Map WSP UAProf headers 'Profile', 'Profile-Diff' to W-HTTP headers
 * 'X-WAP-Profile', 'X-WAP-Profile-Diff' according to WAP-248-UAProf-20011020-a,
 * section 9.2.3.3.
 */
/*
static void map_uaprof_headers(List *headers) 
{
    Octstr *os;
    Octstr *version;
    
    version = http_header_value(headers, octstr_imm("Encoding-Version"));
    os = octstr_format("WAP/%s %S (" GW_NAME "/%s)", 
                       (version ? octstr_get_cstr(version) : "1.1"),
                       get_official_name(), GW_VERSION);
    http_header_add(headers, "Via", octstr_get_cstr(os));
    octstr_destroy(os);
    octstr_destroy(version);
}
*/


/* XXX DAVI: Disabled in cvs revision 1.81 for Opengroup tests
static void add_referer_url(List *headers, Octstr *url) 
{
    if (octstr_len(url) > 0) {
	   http_header_add(headers, "Referer", octstr_get_cstr(url));
    }
}
*/


static void set_referer_url(Octstr *url, WSPMachine *sm)
{
	gw_assert(url != NULL);
	gw_assert(sm != NULL);

    octstr_destroy(sm->referer_url);
    sm->referer_url = octstr_duplicate(url);
}


static Octstr *get_referer_url(const WSPMachine *sm)
{
    return sm ? sm->referer_url : NULL;
}


/*
 * Return the reply from an HTTP request to the phone via a WSP session.
 */
static void return_session_reply(long server_transaction_id, long status,
    	    	    	    	 List *headers, Octstr *body, long session_id)
{
    WAPEvent *e;
    
    e = wap_event_create(S_MethodResult_Req);
    e->u.S_MethodResult_Req.server_transaction_id = server_transaction_id;
    e->u.S_MethodResult_Req.status = status;
    e->u.S_MethodResult_Req.response_headers = headers;
    e->u.S_MethodResult_Req.response_body = body;
    e->u.S_MethodResult_Req.session_id = session_id;
    wsp_session_dispatch_event(e);
}


/*
 * Return the reply from an HTTP request to the phone via connectionless
 * WSP.
 */
static void return_unit_reply(WAPAddrTuple *tuple, long transaction_id,
    	    	    	      long status, List *headers, Octstr *body)
{
    WAPEvent *e;

    e = wap_event_create(S_Unit_MethodResult_Req);
    e->u.S_Unit_MethodResult_Req.addr_tuple = 
    	wap_addr_tuple_duplicate(tuple);
    e->u.S_Unit_MethodResult_Req.transaction_id = transaction_id;
    e->u.S_Unit_MethodResult_Req.status = status;
    e->u.S_Unit_MethodResult_Req.response_headers = headers;
    e->u.S_Unit_MethodResult_Req.response_body = body;
    wsp_unit_dispatch_event(e);
}


static void normalize_charset(struct content * content, List* device_headers)
{
    Octstr* charset;

    if ((charset = find_charset_encoding(content->body)) == NULL) {
        if (octstr_len(content->charset) > 0) {
            charset = octstr_duplicate(content->charset);
        } else {
            charset = octstr_imm("UTF-8");
        }
    }

    debug("wap-appl",0,"Normalizing charset from %s", octstr_get_cstr(charset));

    if (octstr_case_compare(charset, octstr_imm("UTF-8")) != 0 &&
      !http_charset_accepted(device_headers, octstr_get_cstr(charset))) {
        if (!http_charset_accepted(device_headers, "UTF-8")) {
            warning(0, "WSP: Device doesn't support charset <%s> neither UTF-8",
              octstr_get_cstr(charset));
        } else {
            debug("wsp",0,"Converting wml/xhtml from charset <%s> to UTF-8",
              octstr_get_cstr(charset));
            if (charset_convert(content->body,
              octstr_get_cstr(charset), "UTF-8") >= 0) {
                octstr_destroy(content->charset);
                content->charset = octstr_create("UTF-8");
            }
        }
    }
    octstr_destroy(charset);
}

/*
 * Return an HTTP reply back to the phone.
 */
static void return_reply(int status, Octstr *content_body, List *headers,
    	    	    	 long sdu_size, WAPEvent *orig_event, long session_id, 
                         Octstr *method, Octstr *url, int x_wap_tod,
                         List *request_headers, Octstr *msisdn)
{
    struct content content;
    int converted;
    WSPMachine *sm;
    List *device_headers, *t_headers;
    WAPAddrTuple *addr_tuple;
    Octstr *ua, *server;

    content.url = url;
    content.body = content_body;
    content.version = content.type = content.charset = NULL;
    server = ua = NULL;

    /* Get session machine for this session. If this was a connection-less
     * request be obviously will not find any session machine entry. */
    sm = find_session_machine_by_id(session_id);

    device_headers = gwlist_create();

    /* ensure we pass only the original headers to the convertion routine */
    t_headers = (orig_event->type == S_MethodInvoke_Ind) ?
        orig_event->u.S_MethodInvoke_Ind.session_headers :
        NULL;
    if (t_headers != NULL) http_header_combine(device_headers, t_headers);
    t_headers = (orig_event->type == S_MethodInvoke_Ind) ?
        orig_event->u.S_MethodInvoke_Ind.request_headers :
        orig_event->u.S_Unit_MethodInvoke_Ind.request_headers;
    if (t_headers != NULL) http_header_combine(device_headers, t_headers);

    /* 
     * We are acting as a proxy. Hence ensure we log a correct HTTP response
     * code to our access-log file to allow identification of failed proxying
     * requests in the main accesss-log.
     */
    /* get client IP and User-Agent identifier */
    addr_tuple = (orig_event->type == S_MethodInvoke_Ind) ?
        orig_event->u.S_MethodInvoke_Ind.addr_tuple : 
        orig_event->u.S_Unit_MethodInvoke_Ind.addr_tuple;
    ua = http_header_value(request_headers, octstr_imm("User-Agent"));

    if (headers != NULL) {
        /* get response content type and Server identifier */
        http_header_get_content_type(headers, &content.type, &content.charset);
        server = http_header_value(headers, octstr_imm("Server"));
    }

    /* log the access */
    /* XXX make this configurable in the future */
    alog("%s %s %s <%s> (%s, charset='%s') %ld %d <%s> <%s>", 
         octstr_get_cstr(addr_tuple->remote->address), 
         msisdn ? octstr_get_cstr(msisdn) : "-",         
         octstr_get_cstr(method), octstr_get_cstr(url), 
         content.type ? octstr_get_cstr(content.type) : "", 
         content.charset ? octstr_get_cstr(content.charset) : "",
         octstr_len(content.body), status < 0 ? HTTP_BAD_GATEWAY : status,
         ua ? octstr_get_cstr(ua) : "",
         server ? octstr_get_cstr(server) : "");

    octstr_destroy(ua);
    octstr_destroy(server);
    

    if (status < 0) {
        error(0, "WSP: HTTP lookup failed, oops.");
        /* smart WSP error messaging?! */
        if (wsp_smart_errors) {
            Octstr *referer_url;
            status = HTTP_OK;
            content.type = octstr_create("text/vnd.wap.wml");
            content.charset = octstr_create("");
            /*
             * check if a referer for this URL exists and 
             * get back to the previous page in this case
             */
            if ((referer_url = get_referer_url(find_session_machine_by_id(session_id)))) {
                content.body = error_requesting_back(url, referer_url);
                debug("wap.wsp",0,"WSP: returning smart error WML deck for referer URL");
            } 
            /*
             * if there is no referer to retun to, check if we have a
             * device-home defined and return to that, otherwise simply
             * drop an error wml deck.
             */
            else if (device_home != NULL) {
                content.body = error_requesting_back(url, device_home);
                debug("wap.wsp",0,"WSP: returning smart error WML deck for device-home URL");
            } else {
                content.body = error_requesting(url);
                debug("wap.wsp",0,"WSP: returning smart error WML deck");
            }

            /* 
             * if we did not connect at all there is no content in 
             * the headers list, so create for the upcoming transformation
             */
            if (headers == NULL)
                headers = http_create_empty_headers();

            converted = convert_content(&content, device_headers, 0);
            if (converted == 1)
                http_header_mark_transformation(headers, content.body, content.type);

        } else {
            /* no WSP smart error messaging */
            status = HTTP_BAD_GATEWAY;
            content.type = octstr_create("text/plain");
            content.charset = octstr_create("");
            content.body = octstr_create("");
        }

    } else {
        /* received response by HTTP server */
 
#ifdef ENABLE_COOKIES
        if (session_id != -1)
            if (get_cookies(headers, find_session_machine_by_id(session_id)) == -1)
                error(0, "WSP: Failed to extract cookies");
#endif

        /* 
         * XXX why do we transcode charsets on the content body here?!
         * Why is this not in the scope of the HTTP server, rather
         * then doing this inside Kannel?! st. 
         */

        /* 
         * Adapts content body's charset to device.
         * If device doesn't support body's charset but supports UTF-8, this 
         * block tries to convert body to UTF-8. 
         * (This is required for Sharp GX20 for example)
         */
        if (octstr_search(content.type, octstr_imm("text/vnd.wap.wml"), 0) >= 0 || 
            octstr_search(content.type, octstr_imm("application/xhtml+xml"), 0) >= 0 ||
            octstr_search(content.type, octstr_imm("application/vnd.wap.xhtml+xml"), 0) >= 0) {

            normalize_charset(&content, device_headers);
        }

        /* set WBXML Encoding-Version for wml->wmlc conversion */
        if (sm != NULL) {
            content.version = http_header_value(sm->http_headers, 
                                                octstr_imm("Encoding-Version"));
        } else {
            content.version = NULL;
        }

        /* convert content-type by our own converter table */
        converted = convert_content(&content, device_headers, 
                                    octstr_compare(method, octstr_imm("HEAD")) == 0);
        if (converted < 0) {
            warning(0, "WSP: All converters for `%s' at `%s' failed.",
                    octstr_get_cstr(content.type), octstr_get_cstr(url));

            /* 
             * Don't change status; just send the client what we did get.
             * Or if smart error messages are configured, send a wmlc deck
             * with accurate information.
             */
            if (wsp_smart_errors) {
                octstr_destroy(content.body);
                octstr_destroy(content.charset);
                content.body = error_converting(url, content.type);
                content.charset = octstr_create("UTF-8");
                
                debug("wap.wsp",0,"WSP: returning smart error WML deck for failed converters");

                converted = convert_content(&content, device_headers, 0);
                if (converted == 1)
                    http_header_mark_transformation(headers, content.body, content.type);

            }
        }
        else if (converted == 1) {
            http_header_mark_transformation(headers, content.body, content.type);

            /* 
             * set referer URL to WSPMachine, but only if this was a converted
             * content-type, like .wml
             */
            if (session_id != -1) {
                debug("wap.wsp.http",0,"WSP: Setting Referer URL to <%s>", 
                      octstr_get_cstr(url));
                if ((sm = find_session_machine_by_id(session_id)) != NULL) {
                    set_referer_url(url, sm);
                } else {
                    error(0,"WSP: Failed to find session machine for ID %ld",
                          session_id);
                }
            }
        }

        /* if converted == 0 then we pass the content wihtout modification */
    }

    if (headers == NULL)
        headers = http_create_empty_headers();
    http_remove_hop_headers(headers);
    http_header_remove_all(headers, "X-WAP.TOD");
    if (x_wap_tod)
        add_x_wap_tod(headers);

    if (content.body == NULL)
        content.body = octstr_create("");
   
    /*
     * Deal with otherwise wap-aware servers that return text/html error
     * messages if they report an error.
     * (Normally we leave the content type alone even if the client doesn't
     * claim to accept it, because the server might know better than the
     * gateway.)
     */
    if (http_status_class(status) != HTTP_STATUS_SUCCESSFUL &&
        !http_type_accepted(request_headers, octstr_get_cstr(content.type))) {
        warning(0, "WSP: Content type <%s> not supported by client,"
                   " deleting body.", octstr_get_cstr(content.type));
        octstr_destroy(content.body);
        content.body = octstr_create("");
        octstr_destroy(content.type);
        content.type = octstr_create("text/plain");
        http_header_mark_transformation(headers, content.body, content.type);
    }
    /* remove body if request method was HEAD, we act strictly here */
    else if (octstr_compare(method, octstr_imm("HEAD")) == 0) {
        octstr_destroy(content.body);
        content.body = octstr_create("");
        /* change to text/plain if received content-type is not accepted */
        if (!http_type_accepted(request_headers, "*/*") &&
            !http_type_accepted(request_headers, octstr_get_cstr(content.type))) {
            octstr_destroy(content.type);
            content.type = octstr_create("text/plain");
        }
        debug("wsp",0,"WSP: HEAD request, removing body, content-type is now <%s>", 
              octstr_get_cstr(content.type));
        http_header_mark_transformation(headers, content.body, content.type);
    }

#ifdef ENABLE_NOT_ACCEPTED 
    /* Returns HTTP response 406 if content-type is not supported by device */
    else if (request_headers && content.type &&
             !http_type_accepted(request_headers, octstr_get_cstr(content.type)) &&
             !http_type_accepted(request_headers, "*/*")) {
        warning(0, "WSP: content-type <%s> not supported", 
                octstr_get_cstr(content.type));
        status = HTTP_NOT_ACCEPTABLE;
        octstr_destroy(content.type);
        content.type = octstr_create("text/plain");
        octstr_destroy(content.charset);
        octstr_destroy(content.body);
        content.charset = octstr_create("");
        content.body = octstr_create("");
        http_header_mark_transformation(headers, content.body, content.type);
    }
#endif

    /*
     * If the response is too large to be sent to the client,
     * suppress it and inform the client.
     */
    if (octstr_len(content.body) > sdu_size && sdu_size > 0) {
        /*
         * Only change the status if it indicated success.
         * If it indicated an error, then that information is
         * more useful to the client than our "Bad Gateway" would be.
         * The too-large body is probably an error page in html.
         */
        /* XXX add WSP smart messaging here too */
        if (http_status_class(status) == HTTP_STATUS_SUCCESSFUL)
            status = HTTP_BAD_GATEWAY;
        warning(0, "WSP: Entity at %s too large (size %ld B, limit %lu B)",
                octstr_get_cstr(url), octstr_len(content.body), sdu_size);
        octstr_destroy(content.body);
        content.body = octstr_create("");
        http_header_mark_transformation(headers, content.body, content.type);
    }

    if (orig_event->type == S_MethodInvoke_Ind) {
        return_session_reply(orig_event->u.S_MethodInvoke_Ind.server_transaction_id,
                             status, headers, content.body, session_id);
    } else {
        return_unit_reply(orig_event->u.S_Unit_MethodInvoke_Ind.addr_tuple,
                          orig_event->u.S_Unit_MethodInvoke_Ind.transaction_id,
                          status, headers, content.body);
    }

    octstr_destroy(content.version); /* body was re-used above */
    octstr_destroy(content.type); /* body was re-used above */
    octstr_destroy(content.charset);
    octstr_destroy(url);          /* same as content.url */
    http_destroy_headers(device_headers);

    counter_decrease(fetches);
}


/*
 * This thread receives replies from HTTP layer and sends them back to
 * the phone.
 */
static void return_replies_thread(void *arg)
{
    Octstr *body;
    struct request_data *p;
    int status;
    Octstr *final_url;
    List *headers;

    while (run_status == running) {

        p = http_receive_result(caller, &status, &final_url, &headers, &body);
        if (p == NULL)
            break;

        return_reply(status, body, headers, p->client_SDU_size,
                     p->event, p->session_id, p->method, p->url, p->x_wap_tod,
                     p->request_headers, p->msisdn);

        wap_event_destroy(p->event);
        http_destroy_headers(p->request_headers);
        octstr_destroy(p->msisdn);
        gw_free(p);
        octstr_destroy(final_url);
    }
}


/*
 * This WML deck is returned when the user asks for the magic 
 * URL "kannel:alive".
 */
#define HEALTH_DECK \
    "<?xml version=\"1.0\"?>" \
    "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD 1.1//EN\" " \
    "\"http://www.wapforum.org/DTD/wml_1.1.xml\">" \
    "<wml><card id=\"health\"><p>Ok</p></card></wml>"

static void start_fetch(WAPEvent *event) 
{
    int ret;
    long client_SDU_size; /* 0 means no limit */
    Octstr *url;
    List *session_headers;
    List *request_headers;
    List *actual_headers;
    List *resp_headers;
    WAPAddrTuple *addr_tuple;
    long session_id;
    Octstr *content_body;
    Octstr *method;	    /* type of request, normally a get or a post */
    Octstr *request_body;
    int x_wap_tod;          /* X-WAP.TOD header was present in request */
    Octstr *magic_url;
    struct request_data *p;
    Octstr *send_msisdn_query, *send_msisdn_header, *send_msisdn_format;
    int accept_cookies;
    Octstr *msisdn;
    
    counter_increase(fetches);
    
    if (event->type == S_MethodInvoke_Ind) {
        struct S_MethodInvoke_Ind *p;
    
        /* WSP, connection orientated */
        p = &event->u.S_MethodInvoke_Ind;
        session_headers = p->session_headers;
        request_headers = p->request_headers;
        url = octstr_duplicate(p->request_uri);
        addr_tuple = p->addr_tuple;
        session_id = p->session_id;
        client_SDU_size = p->client_SDU_size;
        request_body = octstr_duplicate(p->request_body);
        method = p->method;
    } else {
        struct S_Unit_MethodInvoke_Ind *p;
	
        /* WDP, non orientated */
        p = &event->u.S_Unit_MethodInvoke_Ind;
        session_headers = NULL;
        request_headers = p->request_headers;
        url = octstr_duplicate(p->request_uri);
        addr_tuple = p->addr_tuple;
        session_id = -1;
        client_SDU_size = 0; /* No limit */
        request_body = octstr_duplicate(p->request_body);
        method = p->method;
    }

    msisdn = radius_acct_get_msisdn(addr_tuple->remote->address);
    info(0, "Fetching URL <%s> for MSISDN <%s>, IP <%s:%ld>", octstr_get_cstr(url),
         msisdn ? octstr_get_cstr(msisdn) : "", 
         addr_tuple->remote->address ? octstr_get_cstr(addr_tuple->remote->address) : "",
         addr_tuple->remote->port);

    /* 
     * XXX this URL mapping needs to be rebuild! st. 
     */

    /* try to rewrite URL */
    wap_map_url(&url, &send_msisdn_query, &send_msisdn_header,
                &send_msisdn_format, &accept_cookies);
    /* if no mapping found, then use our RADIUS acct proxy header */
    if (send_msisdn_header == NULL)
        send_msisdn_header = octstr_create("X-WAP-Network-Client-MSISDN");

    actual_headers = gwlist_create();
    
    if (session_headers != NULL)
        http_header_combine(actual_headers, session_headers);
    if (request_headers != NULL)
        http_header_combine(actual_headers, request_headers);
    
    x_wap_tod = http_header_remove_all(actual_headers, "X-WAP.TOD");
    add_accept_headers(actual_headers);
    add_charset_headers(actual_headers);
    add_network_info(actual_headers, addr_tuple);
    add_client_sdu_size(actual_headers, client_SDU_size);
    add_via(actual_headers);

#ifdef ENABLE_COOKIES
    /* DAVI: to finish - accept_cookies -1, 
     * use global accept-cookies, 0 = no, 1 = yes ? */
    if (accept_cookies != 0 && (session_id != -1) &&  
        /* DAVI (set_cookies(url, actual_headers, 
                             find_session_machine_by_id(session_id)) == -1)) */
        (set_cookies(actual_headers, find_session_machine_by_id(session_id)) == -1)) 
        error(0, "WSP: Failed to add cookies");
#endif

    /* set referer URL to HTTP header from WSPMachine */
    /* 
     * XXX This makes Open Group's test suite wml/events/tasks/go/5 failing, 
     * which requires that device is *not* sending referer, but Kannel drops
     * it in. We have to remove this for now.
     */
    /*
    if (session_id != -1) {
        if ((referer_url = get_referer_url(find_session_machine_by_id(session_id))) != NULL) {
            add_referer_url(actual_headers, referer_url);
        }
    }
    */
    
    add_kannel_version(actual_headers);
    add_session_id(actual_headers, session_id);

    add_msisdn(actual_headers, addr_tuple, send_msisdn_header);
    octstr_destroy(send_msisdn_query);
    octstr_destroy(send_msisdn_header);
    octstr_destroy(send_msisdn_format);
    
    http_remove_hop_headers(actual_headers);
    http_header_pack(actual_headers);
    
    magic_url = octstr_imm("kannel:alive");

    /* check if this request is a call for our magic URL */
    if (octstr_str_compare(method, "GET")  == 0 && 
        octstr_compare(url, magic_url) == 0) {
        ret = HTTP_OK;
        resp_headers = gwlist_create();
        http_header_add(resp_headers, "Content-Type", "text/vnd.wap.wml");
        content_body = octstr_create(HEALTH_DECK);
        octstr_destroy(request_body);
        return_reply(ret, content_body, resp_headers, client_SDU_size,
                     event, session_id, method, url, x_wap_tod, actual_headers,
                     msisdn);
        wap_event_destroy(event);
        http_destroy_headers(actual_headers);
        octstr_destroy(msisdn);
    } 
    /* otherwise it should be a GET, POST or HEAD request type */
    else if (octstr_str_compare(method, "GET") == 0 ||
             octstr_str_compare(method, "POST") == 0 ||
             octstr_str_compare(method, "HEAD") == 0) {

        /* we don't allow a body within a GET or HEAD request */
        if (request_body != NULL && (octstr_str_compare(method, "GET") == 0 ||
                                     octstr_str_compare(method, "HEAD") == 0)) {
            octstr_destroy(request_body);
            request_body = NULL;
        }

        /* 
         * Call deconvert_content() here for transformations of binary
         * encoded POST requests from the client into plain text decoded
         * POST requests for the HTTP server.
         * Mainly this is used for multipart/form-data transmissions,
         * including MMS on-the-fly message decoding.
         * When we are doing mms, the phone POSTs contents and acknowled-
         * gements. In this case, we dont do not deconvert anything.
         */
        if (octstr_str_compare(method, "POST") == 0 && request_body && 
            octstr_len(request_body)) {
            struct content content;
            int converted;

            http_header_get_content_type(actual_headers, &content.type, 
                                         &content.charset);
            content.body = request_body;
            converted = deconvert_content(&content); 
            if (converted == 1) 
                http_header_mark_transformation(actual_headers, content.body, 
                                                content.type);
            request_body = content.body;
            octstr_destroy(content.type);
            octstr_destroy(content.charset);
        }

        /* struct that is used for the HTTP response identifier */
        p = gw_malloc(sizeof(*p));
        p->client_SDU_size = client_SDU_size;
        p->event = event;
        p->session_id = session_id;
        p->method = method;
        p->url = url;
        p->x_wap_tod = x_wap_tod;
        p->request_headers = actual_headers;
        p->msisdn = msisdn;

        /* issue the request to the HTTP server */
        http_start_request(caller, http_name2method(method), url, actual_headers, 
                           request_body, 0, p, NULL);

        octstr_destroy(request_body);
    } 
    /* we don't support the WSP/HTTP method the client asked us */
    else {
        error(0, "WSP: Method %s not supported.", octstr_get_cstr(method));
        content_body = octstr_create("");
        resp_headers = http_create_empty_headers();
        ret = HTTP_NOT_IMPLEMENTED;
        octstr_destroy(request_body);
        return_reply(ret, content_body, resp_headers, client_SDU_size,
                     event, session_id, method, url, x_wap_tod, actual_headers,
                     msisdn);
        wap_event_destroy(event);
        http_destroy_headers(actual_headers);
        octstr_destroy(msisdn);
    }
}
                

/* Shut up WMLScript compiler status/trace messages. */
static void dev_null(const char *data, size_t len, void *context) 
{
    /* nothing */
}


static Octstr *convert_wml_to_wmlc(struct content *content) 
{
    Octstr *wmlc;
    int ret;
   
    /* content->charset is passed from the HTTP header parsing */
    ret = wml_compile(content->body, content->charset, &wmlc, 
                      content->version);

    /* wmlc is created implicitely in wml_compile() */
    if (ret == 0)
        return wmlc;

    octstr_destroy(wmlc);
    warning(0, "WSP: WML compilation failed.");
    return NULL;
}


static Octstr *convert_wmlscript_to_wmlscriptc(struct content *content) 
{
    WsCompilerParams params;
    WsCompilerPtr compiler;
    WsResult result;
    unsigned char *result_data;
    size_t result_size;
    Octstr *wmlscriptc;
    
    memset(&params, 0, sizeof(params));
    params.use_latin1_strings = 0;
    params.print_symbolic_assembler = 0;
    params.print_assembler = 0;
    params.meta_name_cb = NULL;
    params.meta_name_cb_context = NULL;
    params.meta_http_equiv_cb = NULL;
    params.meta_http_equiv_cb_context = NULL;
    params.stdout_cb = dev_null;
    params.stderr_cb = dev_null;
    
    compiler = ws_create(&params);
    if (compiler == NULL) {
        panic(0, "WSP: could not create WMLScript compiler");
    }
    
    result = ws_compile_data(compiler, octstr_get_cstr(content->url),
                             (unsigned char *)octstr_get_cstr(content->body),
                             octstr_len(content->body),
                             &result_data, &result_size);
    if (result != WS_OK) {
        warning(0, "WSP: WMLScript compilation failed: %s",
                ws_result_to_string(result));
        wmlscriptc = NULL;
    } else {
        wmlscriptc = octstr_create_from_data((char *)result_data, result_size);
    }
    
    return wmlscriptc;
}


/* 
 * XXX There's a big bug in http_get_content_type that 
 * assumes that header parameter is charset without looking at
 * parameter key. Good!. I'll use its value to catch boundary
 * value for now
 * ie. "Content-Type: (foo/bar);something=(value)" it gets value
 * without caring about what is "something" 
 */

/* DAVI: To-Do
static Octstr *convert_multipart_mixed(struct content *content)
{
    Octstr *result = NULL;

    debug("wap.wsp.multipart.mixed", 0, "WSP.Multipart.Mixed, boundary=[%s]", 
          octstr_get_cstr(content->charset));

    result = octstr_duplicate(content->body);
    return result;
}
*/


static Octstr *deconvert_multipart_formdata(struct content *content)
{
    Octstr *mime;
   
    if ((mime_decompile(content->body, &mime)) == 0)
        return mime;

    return NULL;
}

/* DAVI: To-Do
static Octstr *deconvert_mms_message(struct content *content)
{
    Octstr *mime;
   
    if ((mms_decompile(content->body, &mime)) == 0)
        return mime;

    return NULL;
}
*/


/* The interface for capability negotiation is a bit different from
 * the negotiation at WSP level, to make it easier to program.
 * The application layer gets a list of requested capabilities,
 * basically a straight decoding of the WSP level capabilities.
 * It replies with a list of all capabilities it wants to set or
 * refuse.  (Refuse by setting cap->data to NULL).  Any capabilities
 * it leaves out are considered "unknown; don't care".  The WSP layer
 * will either process those itself, or refuse them.
 *
 * At the WSP level, not sending a reply to a capability means accepting
 * what the client proposed.  If the application layer wants this to 
 * happen, it should set cap->data to NULL and cap->accept to 1.
 * (The WSP layer does not try to guess what kind of reply would be 
 * identical to what the client proposed, because the format of the
 * reply is often different from the format of the request, and this
 * is likely to be true for unknown capabilities too.)
 */
static List *negotiate_capabilities(List *req_caps) 
{
    /* Currently we don't know or care about any capabilities,
     * though it is likely that "Extended Methods" will be
     * the first. */
    return gwlist_create();
}


/*
 * Ota submodule implements indications, responses and confirmations part of 
 * ota.
 */

/*
 * If Accept-Application is empty, add header indicating default application 
 * wml ua (see ota 6.4.1). Otherwise decode application id (see http://www.
 * wapforum.org/wina/push-app-id.htm). FIXME: capability negotiation (no-
 * thing means default, if so negotiated).
 * Function does not allocate memory neither for headers nor application_
 * headers.
 * Returns encoded application headers and input header list without them.
 */
static void check_application_headers(List **headers, 
                                      List **application_headers)
{
    List *inh;
    int i;
    long len;
    Octstr *appid_name, *coded_octstr;
    char *appid_value, *coded_value;

    split_header_list(headers, &inh, "Accept-Application");
    
    if (*headers == NULL || gwlist_len(inh) == 0) {
        http_header_add(*application_headers, "Accept-Application", "wml ua");
        debug("wap.appl.push", 0, "APPL: No push application, assuming wml"
              " ua");
        if (*headers != NULL)
            http_destroy_headers(inh);
        return;
    }

    i = 0;
    len = gwlist_len(inh);
    coded_value = NULL;
    appid_value = NULL;

    while (i < len) {
        http_header_get(inh, i, &appid_name, &coded_octstr);

        /* Greatest value reserved by WINA is 0xFF00 0000*/
        coded_value = octstr_get_cstr(coded_octstr);
        if (coded_value != NULL)
	   appid_value = (char *)wsp_application_id_to_cstr((long) coded_value);

        if (appid_value != NULL && coded_value != NULL)
            http_header_add(*application_headers, "Accept-Application", 
                            appid_value);
        else {
            error(0, "OTA: Unknown application is, skipping: ");
            octstr_dump(coded_octstr, 0, GW_ERROR);
        }

        i++;  
    }
   
    debug("wap.appl.push", 0, "application headers were");
    http_header_dump(*application_headers);

    http_destroy_headers(inh);
    octstr_destroy(appid_name);
    octstr_destroy(coded_octstr);
}


/*
 * Bearer-Indication field is defined in ota 6.4.1. 
 * Skip the header, if it is malformed or if there is more than one bearer 
 * indication.
 * Function does not allocate memory neither for headers nor bearer_headers.
 * Return encoded bearer indication header and input header list without it.
 */
static void decode_bearer_indication(List **headers, List **bearer_headers)
{
    List *inb;
    Octstr *name, *coded_octstr;
    char *value;
    unsigned char coded_value;

    if (*headers == NULL) {
        debug("wap.appl", 0, "APPL: no client headers, continuing");
        return;
    }

    split_header_list(headers, &inb, "Bearer-Indication");

    if (gwlist_len(inb) == 0) {
        debug("wap.appl.push", 0, "APPL: No bearer indication headers,"
              " continuing");
        http_destroy_headers(inb);
        return;  
    }

    if (gwlist_len(inb) > 1) {
        error(0, "APPL: To many bearer indication header(s), skipping"
              " them");
        http_destroy_headers(inb);
        return;
    }

    http_header_get(inb, 0, &name, &coded_octstr);
    http_destroy_headers(inb);

    /* Greatest assigned number for a bearer type is 0xff, see wdp, appendix C */
    coded_value = octstr_get_char(coded_octstr, 0);
    value = (char *)wsp_bearer_indication_to_cstr(coded_value);

    if (value != NULL && coded_value != 0) {
       http_header_add(*bearer_headers, "Bearer-Indication", value);
       debug("wap.appl.push", 0, "bearer indication header was");
       http_header_dump(*bearer_headers);
       return;
    } else {
       error(0, "APPL: Illegal bearer indication value, skipping:");
       octstr_dump(coded_octstr, 0, GW_ERROR);
       http_destroy_headers(*bearer_headers);
       return;
    }
}


/*
 * Separate headers into two lists, one having all headers named "name" and
 * the other rest of them.
 */
static void split_header_list(List **headers, List **new_headers, char *name)
{
    if (*headers == NULL)
        return;

    *new_headers = http_header_find_all(*headers, name);
    http_header_remove_all(*headers, name);  
}


/*
 * Find headers Accept-Application and Bearer-Indication amongst push headers,
 * decode them and add them to their proper field. 
 */
static void indicate_push_connection(WAPEvent *e)
{
    WAPEvent *ppg_event;
    List *push_headers, *application_headers, *bearer_headers;

    push_headers = http_header_duplicate(e->u.S_Connect_Ind.client_headers);
    application_headers = http_create_empty_headers();
    bearer_headers = http_create_empty_headers();
    
    ppg_event = wap_event_create(Pom_Connect_Ind);
    ppg_event->u.Pom_Connect_Ind.addr_tuple = 
        wap_addr_tuple_duplicate(e->u.S_Connect_Ind.addr_tuple);
    ppg_event->u.Pom_Connect_Ind.requested_capabilities = 
        wsp_cap_duplicate_list(e->u.S_Connect_Ind.requested_capabilities);

    check_application_headers(&push_headers, &application_headers);
    ppg_event->u.Pom_Connect_Ind.accept_application = application_headers;

    decode_bearer_indication(&push_headers, &bearer_headers);

    if (gwlist_len(bearer_headers) == 0) {
        http_destroy_headers(bearer_headers);
        ppg_event->u.Pom_Connect_Ind.bearer_indication = NULL;
    } else
        ppg_event->u.Pom_Connect_Ind.bearer_indication = bearer_headers;

    ppg_event->u.Pom_Connect_Ind.push_headers = push_headers;
    ppg_event->u.Pom_Connect_Ind.session_id = e->u.S_Connect_Ind.session_id;
    debug("wap.appl", 0, "APPL: making OTA connection indication to PPG");

    wap_push_ppg_dispatch_event(ppg_event);
}


static void indicate_push_disconnect(WAPEvent *e)
{
    WAPEvent *ppg_event;

    ppg_event = wap_event_create(Pom_Disconnect_Ind);
    ppg_event->u.Pom_Disconnect_Ind.reason_code = 
        e->u.S_Disconnect_Ind.reason_code;
    ppg_event->u.Pom_Disconnect_Ind.error_headers =
        octstr_duplicate(e->u.S_Disconnect_Ind.error_headers);
    ppg_event->u.Pom_Disconnect_Ind.error_body =
        octstr_duplicate(e->u.S_Disconnect_Ind.error_body);
    ppg_event->u.Pom_Disconnect_Ind.session_handle =
        e->u.S_Disconnect_Ind.session_handle;

    wap_push_ppg_dispatch_event(ppg_event);
}


/*
 * We do not implement acknowledgement headers
 */
static void confirm_push(WAPEvent *e)
{
    WAPEvent *ppg_event;

    ppg_event = wap_event_create(Po_ConfirmedPush_Cnf);
    ppg_event->u.Po_ConfirmedPush_Cnf.server_push_id = 
        e->u.S_ConfirmedPush_Cnf.server_push_id;
    ppg_event->u.Po_ConfirmedPush_Cnf.session_handle = 
         e->u.S_ConfirmedPush_Cnf.session_id;

    debug("wap.appl", 0, "OTA: confirming push for ppg");
    wap_push_ppg_dispatch_event(ppg_event);
}


static void indicate_push_abort(WAPEvent *e)
{
    WAPEvent *ppg_event;

    ppg_event = wap_event_create(Po_PushAbort_Ind);
    ppg_event->u.Po_PushAbort_Ind.push_id = e->u.S_PushAbort_Ind.push_id;
    ppg_event->u.Po_PushAbort_Ind.reason = e->u.S_PushAbort_Ind.reason;
    ppg_event->u.Po_PushAbort_Ind.session_handle = 
        e->u.S_PushAbort_Ind.session_id;

    debug("wap.push.ota", 0, "OTA: making push abort indication for ppg");
    wap_push_ppg_dispatch_event(ppg_event);
}


static void indicate_push_suspend(WAPEvent *e)
{
    WAPEvent *ppg_event;

    ppg_event = wap_event_create(Pom_Suspend_Ind);
    ppg_event->u.Pom_Suspend_Ind.reason = e->u.S_Suspend_Ind.reason;
    ppg_event->u.Pom_Suspend_Ind.session_id =  e->u.S_Suspend_Ind.session_id;

    wap_push_ppg_dispatch_event(ppg_event);
}


/*
 * Find Bearer-Indication amongst client headers, decode it and assign it to
 * a separate field in the event structure.
 */
static void indicate_push_resume(WAPEvent *e)
{
    WAPEvent *ppg_event;
    List *push_headers, *bearer_headers;

    push_headers = http_header_duplicate(e->u.S_Resume_Ind.client_headers);
    bearer_headers = http_create_empty_headers();
    
    ppg_event = wap_event_create(Pom_Resume_Ind);
    ppg_event->u.Pom_Resume_Ind.addr_tuple = wap_addr_tuple_duplicate(
        e->u.S_Resume_Ind.addr_tuple);
   
    decode_bearer_indication(&push_headers, &bearer_headers);

    if (gwlist_len(bearer_headers) == 0) {
        http_destroy_headers(bearer_headers);
        ppg_event->u.Pom_Resume_Ind.bearer_indication = NULL;
    } else 
        ppg_event->u.Pom_Resume_Ind.bearer_indication = bearer_headers;

    ppg_event->u.Pom_Resume_Ind.client_headers = push_headers;
    ppg_event->u.Pom_Resume_Ind.session_id = e->u.S_Resume_Ind.session_id;

    wap_push_ppg_dispatch_event(ppg_event);
}


/*
 * Server headers are mentioned in table in ota 6.4.1, but none of the primit-
 * ives use them. They are optional in S_Connect_Res, so we do not use them.
 */
static void response_push_connection(WAPEvent *e)
{
    WAPEvent *wsp_event;

    gw_assert(e->type = Pom_Connect_Res);

    wsp_event = wap_event_create(S_Connect_Res);
    wsp_event->u.S_Connect_Res.session_id = e->u.Pom_Connect_Res.session_id;
    wsp_event->u.S_Connect_Res.negotiated_capabilities =
        wsp_cap_duplicate_list(e->u.Pom_Connect_Res.negotiated_capabilities);
    debug("wap.appl", 0, "APPL: making push connect response");

    wsp_session_dispatch_event(wsp_event);
}

