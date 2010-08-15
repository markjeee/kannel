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
 * Wap_push_ota.c: implementation of push related requests of OTA protocol
 *
 * This module implements requirement primitives of WAP-189-PushOTA-20000217-a
 * (hereafter called ota).
 * In addition, WAP-203-WSP-20000504-a (wsp) is referred.
 *
 * This module forwards push  requests made by the wap_push_ppg module to 
 * connected or connectionless session services.
 * Indications (for confirmed push, push abort and disconnect, e.g., in the 
 * case of unability to create a session) of OTA protocol are done for wap_
 * push_ppg module by a module common with pull, wap-appl. 
 *
 * Note that push header encoding and decoding are divided into two parts:
 * first decoding and encoding numeric values and then packing these values
 * into WSP format and unpacking them from WSP format. This module contains
 * decoding part.
 *
 * By Aarno Syvänen for Wapit Ltd and Global Networks Inc.
 */

#include <errno.h>

#include "wap_push_ota.h"
#include "gwlib/gwlib.h"
#include "wap/wsp.h"
#include "wap/wsp_strings.h"
#include "wap/wsp_pdu.h"

/**************************************************************************
 *
 * Internal data structures
 */ 

/*
 * Give the status of the push ota module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum {limbo, running, terminating} run_status = limbo;

/*
 * Bearerbox address for the phone (it needs to know who it is talking with)
 */
struct BearerboxAddress {
    Octstr *address;
    Mutex *mutex;
}; 
typedef struct BearerboxAddress BearerboxAddress;

static BearerboxAddress *bearerbox = NULL;

static List *ota_queue = NULL;

wap_dispatch_func_t *dispatch_to_wsp;
wap_dispatch_func_t *dispatch_to_wsp_unit;

/**************************************************************************
 *
 * Prototypes of internal functions
 */

static void main_thread(void *arg);
static void handle_ota_event(WAPEvent *e);
static void make_session_request(WAPEvent *e);
static void make_push_request(WAPEvent *e);
static void make_confirmed_push_request(WAPEvent *e);
static void make_unit_push_request(WAPEvent *e);
static void abort_push(WAPEvent *e);

/*
 * Add push flag into push headers. Push flag is defined in PushOTA, p. 17-18.
 */
static List *add_push_flag(WAPEvent *e);

/*
 * When server is requesting a session with a client, content type and applic-
 * ation headers must be present (this behaviour is defined in PushOTA, p. 14).
 * We check headers for them and add them if they are not already present.
 */
static void check_session_request_headers(List *headers);

/*
 * Contact points and application ids in the push initiator are packed into a
 * specific structure, being like WSP PDUs.
 */
static Octstr *pack_sia(List *headers);
static void flags_assert(WAPEvent *e);
static void reason_assert(long reason);
static Octstr *pack_server_address(void);
static Octstr *pack_appid_list(List *headers);

/*
 * Returns bearerbox ip address. Resolve it, if the address is localhost.
 */
static Octstr *name(Octstr *os);
static BearerboxAddress *bearerbox_address_create(void);
static void bearerbox_address_destroy(BearerboxAddress *ba);

/***************************************************************************
 *
 * EXTERNAL FUNCTIONS
 */

void wap_push_ota_init(wap_dispatch_func_t *wsp_dispatch,
                       wap_dispatch_func_t *wsp_unit_dispatch)
{
    ota_queue = gwlist_create();
    gwlist_add_producer(ota_queue);

    dispatch_to_wsp = wsp_dispatch;
    dispatch_to_wsp_unit = wsp_unit_dispatch;

    bearerbox = bearerbox_address_create();

    gw_assert(run_status == limbo);
    run_status = running;
    gwthread_create(main_thread, NULL);
}

void wap_push_ota_shutdown(void)
{
    gw_assert(run_status == running);
    run_status = terminating;
    gwlist_remove_producer(ota_queue);
    gwthread_join_every(main_thread);

    gwlist_destroy(ota_queue, wap_event_destroy_item);
    bearerbox_address_destroy(bearerbox);
}

void wap_push_ota_dispatch_event(WAPEvent *e)
{
    gw_assert(run_status == running); 
    gwlist_produce(ota_queue, e);
}

/*
 * Sets bearerbox address, used for push contact point. Resolve address local-
 * host before assignment.
 */
void wap_push_ota_bb_address_set(Octstr *in)
{
    gw_assert(in);

    mutex_lock(bearerbox->mutex);
    bearerbox->address = name(in);
    mutex_unlock(bearerbox->mutex);
}

/**************************************************************************
 *
 * INTERNAL FUNCTIONS
 */

static void main_thread(void *arg)
{
    WAPEvent *e;

    while (run_status == running && (e = gwlist_consume(ota_queue)) != NULL) {
        handle_ota_event(e);
    } 

}

static void handle_ota_event(WAPEvent *e)
{
    debug("wap.push.ota", 0, "OTA: event arrived");

    switch (e->type) {
    case Pom_SessionRequest_Req:
        make_session_request(e);
    break;

    case Po_Push_Req:
        make_push_request(e);
    break;

    case Po_ConfirmedPush_Req:
        make_confirmed_push_request(e);
    break;

    case Po_Unit_Push_Req:
        make_unit_push_request(e);
    break;

    case Po_PushAbort_Req:
        abort_push(e);
    break;

    default:
        debug("wap.push.ota", 0, "OTA: unhandled event");
        wap_event_dump(e);
    break;
    }

    wap_event_destroy(e);
}

static void make_session_request(WAPEvent *e)
{
    WAPEvent *wsp_event;
    List *appid_headers, *push_headers;

    gw_assert(e->type == Pom_SessionRequest_Req);
    push_headers = e->u.Pom_SessionRequest_Req.push_headers;

    check_session_request_headers(push_headers);

    wsp_event = wap_event_create(S_Unit_Push_Req);
    wsp_event->u.S_Unit_Push_Req.address_type =
        e->u.Pom_SessionRequest_Req.address_type;
    if (e->u.Pom_SessionRequest_Req.smsc_id != NULL)
        wsp_event->u.S_Unit_Push_Req.smsc_id =
            octstr_duplicate(e->u.Pom_SessionRequest_Req.smsc_id);
    else
        wsp_event->u.S_Unit_Push_Req.smsc_id = NULL;
    if (e->u.Pom_SessionRequest_Req.dlr_url != NULL)
        wsp_event->u.S_Unit_Push_Req.dlr_url =
            octstr_duplicate(e->u.Pom_SessionRequest_Req.dlr_url);
    else
        wsp_event->u.S_Unit_Push_Req.dlr_url = NULL;
    wsp_event->u.S_Unit_Push_Req.dlr_mask = e->u.Pom_SessionRequest_Req.dlr_mask;
    if (e->u.Pom_SessionRequest_Req.smsbox_id != NULL)
        wsp_event->u.S_Unit_Push_Req.smsbox_id =
            octstr_duplicate(e->u.Pom_SessionRequest_Req.smsbox_id);
    else
        wsp_event->u.S_Unit_Push_Req.smsbox_id = NULL;    
    wsp_event->u.S_Unit_Push_Req.service_name = 
        octstr_duplicate(e->u.Pom_SessionRequest_Req.service_name);

    wsp_event->u.S_Unit_Push_Req.push_id = 
        e->u.Pom_SessionRequest_Req.push_id;
    wsp_event->u.S_Unit_Push_Req.addr_tuple = 
        wap_addr_tuple_duplicate(e->u.Pom_SessionRequest_Req.addr_tuple);
    wsp_event->u.S_Unit_Push_Req.push_headers = 
        http_header_duplicate(push_headers);

    appid_headers = http_header_find_all(push_headers, "X-WAP-Application-Id");
    wsp_event->u.S_Unit_Push_Req.push_body = pack_sia(appid_headers);
    
    debug("wap.push.ota", 0, "OTA: making a connectionless session request for"
          " creating a session");

    dispatch_to_wsp_unit(wsp_event);
}

static void make_push_request(WAPEvent *e)
{
    WAPEvent *wsp_event;
    List *push_headers;

    gw_assert(e->type == Po_Push_Req);
    push_headers = add_push_flag(e);
    
    wsp_event = wap_event_create(S_Push_Req);
    wsp_event->u.S_Push_Req.push_headers = push_headers;
    if (e->u.Po_Push_Req.push_body != NULL)
        wsp_event->u.S_Push_Req.push_body = 
            octstr_duplicate(e->u.Po_Push_Req.push_body);
    else
        wsp_event->u.S_Push_Req.push_body = NULL;
    wsp_event->u.S_Push_Req.session_id = e->u.Po_Push_Req.session_handle;

    dispatch_to_wsp(wsp_event);
}

static void make_confirmed_push_request(WAPEvent *e)
{
    WAPEvent *wsp_event;
    List *push_headers;

    gw_assert(e->type == Po_ConfirmedPush_Req);
    push_headers = add_push_flag(e);
    
    wsp_event = wap_event_create(S_ConfirmedPush_Req);
    wsp_event->u.S_ConfirmedPush_Req.server_push_id = 
        e->u.Po_ConfirmedPush_Req.server_push_id;
    wsp_event->u.S_ConfirmedPush_Req.push_headers = push_headers;

    if (e->u.Po_ConfirmedPush_Req.push_body != NULL)
        wsp_event->u.S_ConfirmedPush_Req.push_body =
	        octstr_duplicate(e->u.Po_ConfirmedPush_Req.push_body);
    else
        wsp_event->u.S_ConfirmedPush_Req.push_body = NULL;
     
    wsp_event->u.S_ConfirmedPush_Req.session_id = 
        e->u.Po_ConfirmedPush_Req.session_handle;
    debug("wap.push.ota", 0, "OTA: making confirmed push request to wsp");
    
    dispatch_to_wsp(wsp_event);
}

static void make_unit_push_request(WAPEvent *e)
{
    WAPEvent *wsp_event;
    List *push_headers;
    Octstr *smsc_id;
    Octstr *dlr_url;
    Octstr *smsbox_id;
    Octstr *push_body;
    Octstr *service_name;

    gw_assert(e->type == Po_Unit_Push_Req);
    gw_assert(e->u.Po_Unit_Push_Req.addr_tuple);
    gw_assert(e->u.Po_Unit_Push_Req.service_name);

    smsc_id = octstr_duplicate(e->u.Po_Unit_Push_Req.smsc_id); 
    dlr_url = octstr_duplicate(e->u.Po_Unit_Push_Req.dlr_url);
    smsbox_id = octstr_duplicate(e->u.Po_Unit_Push_Req.smsbox_id);
    push_body = octstr_duplicate(e->u.Po_Unit_Push_Req.push_body);
    service_name = octstr_duplicate(e->u.Po_Unit_Push_Req.service_name); 
    push_headers = add_push_flag(e);

    wsp_event = wap_event_create(S_Unit_Push_Req);
    wsp_event->u.S_Unit_Push_Req.addr_tuple = 
        wap_addr_tuple_duplicate(e->u.Po_Unit_Push_Req.addr_tuple);
    wsp_event->u.S_Unit_Push_Req.push_id = e->u.Po_Unit_Push_Req.push_id;
    wsp_event->u.S_Unit_Push_Req.push_headers = push_headers;

    wsp_event->u.S_Unit_Push_Req.address_type = 
        e->u.Po_Unit_Push_Req.address_type;
    if (smsc_id != NULL)
        wsp_event->u.S_Unit_Push_Req.smsc_id = smsc_id;
    else
        wsp_event->u.S_Unit_Push_Req.smsc_id = NULL;  
    if (dlr_url != NULL)      
        wsp_event->u.S_Unit_Push_Req.dlr_url = dlr_url;        
    else  
        wsp_event->u.S_Unit_Push_Req.dlr_url = NULL;      
    wsp_event->u.S_Unit_Push_Req.dlr_mask = e->u.Po_Unit_Push_Req.dlr_mask;            
    if (smsbox_id != NULL)
        wsp_event->u.S_Unit_Push_Req.smsbox_id = smsbox_id;
    else
        wsp_event->u.S_Unit_Push_Req.smsbox_id = NULL;
    wsp_event->u.S_Unit_Push_Req.service_name = service_name;
    if (push_body != NULL)
        wsp_event->u.S_Unit_Push_Req.push_body = push_body;
    else
        wsp_event->u.S_Unit_Push_Req.push_body = NULL;

    dispatch_to_wsp_unit(wsp_event);
    debug("wap.push.ota", 0, "OTA: made connectionless session service"
          " request");
}

static void abort_push(WAPEvent *e)
{
    WAPEvent *wsp_event;
    long reason;
    
    reason = e->u.Po_PushAbort_Req.reason;
    gw_assert(e->type == Po_PushAbort_Req);
    reason_assert(reason);

    wsp_event = wap_event_create(S_PushAbort_Req);
    wsp_event->u.S_PushAbort_Req.push_id = e->u.Po_PushAbort_Req.push_id;
    wsp_event->u.S_PushAbort_Req.reason = reason;
    wsp_event->u.S_PushAbort_Req.session_handle = 
        e->u.Po_PushAbort_Req.session_id;

    dispatch_to_wsp(wsp_event);    
}

/*
 * Add push flag into push headers. Push flag is defined in ota, p. 17-18.
 * If there is no flags set, no Push-Flag header is added.
 */
static List *add_push_flag(WAPEvent *e)
{
    int push_flag,
        trusted,
        authenticated,
        last;

    Octstr *buf;
    List *headers;

    flags_assert(e);

    if (e->type == Po_Unit_Push_Req) {
        trusted = e->u.Po_Unit_Push_Req.trusted << 1;
        authenticated = e->u.Po_Unit_Push_Req.authenticated;
        last = e->u.Po_Unit_Push_Req.last << 2;

        headers = http_header_duplicate(e->u.Po_Unit_Push_Req.push_headers);

    } else if (e->type == Po_Push_Req) {
        trusted = e->u.Po_Push_Req.trusted << 1;
        authenticated = e->u.Po_Push_Req.authenticated;
        last = e->u.Po_Push_Req.last << 2;

        headers = http_header_duplicate(e->u.Po_Push_Req.push_headers);

    } else if (e->type == Po_ConfirmedPush_Req) {
        trusted = e->u.Po_ConfirmedPush_Req.trusted << 1;
        authenticated = e->u.Po_ConfirmedPush_Req.authenticated;
        last = e->u.Po_ConfirmedPush_Req.last << 2;

        headers = http_header_duplicate(
            e->u.Po_ConfirmedPush_Req.push_headers);

    } else {
        debug("wap.ota", 0, "OTA: no push flag when the event is: \n");
        wap_event_dump(e);
        return NULL;
    }

    push_flag = 0;
    push_flag = push_flag | authenticated | trusted | last;
    
    if (push_flag) {
    buf = octstr_format("%d", push_flag);
    http_header_add(headers, "Push-Flag", octstr_get_cstr(buf)); 
    octstr_destroy(buf);
    }

    return headers;
}

static void flags_assert(WAPEvent *e)
{
    if (e->type == Po_Unit_Push_Req) {
        gw_assert(e->u.Po_Unit_Push_Req.trusted == 0 || 
            e->u.Po_Unit_Push_Req.trusted == 1);
        gw_assert(e->u.Po_Unit_Push_Req.authenticated == 0 || 
            e->u.Po_Unit_Push_Req.authenticated == 1);
        gw_assert(e->u.Po_Unit_Push_Req.last == 0 || 
            e->u.Po_Unit_Push_Req.last == 1);

    } else if (e->type == Po_Push_Req) {
        gw_assert(e->u.Po_Push_Req.trusted == 0 || 
            e->u.Po_Push_Req.trusted == 1);
        gw_assert(e->u.Po_Push_Req.authenticated == 0 || 
            e->u.Po_Push_Req.authenticated == 1);
        gw_assert(e->u.Po_Push_Req.last == 0 || 
            e->u.Po_Push_Req.last == 1);

    } else if (e->type == Po_ConfirmedPush_Req) {
        gw_assert(e->u.Po_ConfirmedPush_Req.trusted == 0 || 
            e->u.Po_ConfirmedPush_Req.trusted == 1);
        gw_assert(e->u.Po_ConfirmedPush_Req.authenticated == 0 || 
            e->u.Po_ConfirmedPush_Req.authenticated == 1);
        gw_assert(e->u.Po_ConfirmedPush_Req.last == 0 || 
            e->u.Po_ConfirmedPush_Req.last == 1);
    }
}

/*
 * Accepted reasons are defined in ota 6.3.3.
 */
static void reason_assert(long reason)
{
    gw_assert(reason == WSP_ABORT_USERREQ || reason == WSP_ABORT_USERRFS || 
              reason == WSP_ABORT_USERPND || reason == WSP_ABORT_USERDCR || 
              reason == WSP_ABORT_USERDCU);
}

/*
 * When server is requesting a session with a client, content type and applic-
 * ation headers must be present (this behaviour is defined in ota, p. 14).
 * We check headers for them and add them if they are not already present. 
 * X-WAP-Application-Id has been added by ppg module.
 */
static void check_session_request_headers(List *headers)
{
    if (!http_type_accepted(headers, "application/wnd.wap.sia"))
         http_header_add(headers, "Content-Type", "application/vnd.wap.sia"); 
}

/*
 * Pack contact points and application id list into sia content type. It is 
 * defined in ota, p. 18. 
 */
static Octstr *pack_sia(List *headers)
{
    Octstr *sia_content;
    WSP_PDU *pdu;
   
    pdu = wsp_pdu_create(sia);
    
    pdu->u.sia.version = CURRENT_VERSION;
    pdu->u.sia.application_id_list = pack_appid_list(headers);
    pdu->u.sia.contactpoints = pack_server_address();
    sia_content = wsp_pdu_pack(pdu);
    
    wsp_pdu_destroy(pdu);
    http_destroy_headers(headers);

    return sia_content;
}

/*
 * Input: List of headers containing only X-Wap-Application-Id headers, values
 * being numeric application id codes. (Ppg module does coding of the header 
 * value part of the X-WAP-Application-Id header).
 * Output: Octstr containing them in a byte list (one id per byte). 
 *
 * Returns: Octstr containing headers, if succesfull, otherwise an empty 
 * octstr.
 */
static Octstr *pack_appid_list(List *headers)
{
    Octstr *appid_os,
           *header_name,
           *header_value;
    long i;
    size_t len;

    i = 0;
    appid_os = octstr_create("");
    len = (size_t) gwlist_len(headers);

    gw_assert(len);

    while (i < len) {
        http_header_get(headers, i, &header_name, &header_value);
        gw_assert(octstr_compare(header_name, 
                  octstr_imm("X-WAP-Application-Id")) == 0);
        octstr_format_append(appid_os, "%S", header_value);
        octstr_destroy(header_name);
        octstr_destroy(header_value);
        ++i;
    }
    
    return appid_os;
}

/*
 * NB: This data includes bearer information. We use IPv4 values. Address Type
 * is defined in wsp, table 16, p. 65
 */
static Octstr *pack_server_address(void)
{
    Octstr *address,
           *ip_address;
    unsigned char address_len;
    long port;
    int bearer_type;

    bearer_type = GSM_CSD_IPV4;
    port = CONNECTED_PORT;

    mutex_lock(bearerbox->mutex);  
    ip_address = octstr_duplicate(bearerbox->address);
    address_len = octstr_len(bearerbox->address);
    mutex_unlock(bearerbox->mutex);  

    address = octstr_create("");
    octstr_append_char(address, address_len);
    octstr_set_bits(address, 0, 1, 1); /* bearer type included */
    octstr_set_bits(address, 1, 1, 1); /* port number included */
    octstr_append_char(address, bearer_type);
    octstr_append_decimal(address, port);
    octstr_append(address, ip_address);
    octstr_destroy(ip_address);
    
    return address;
}

/*
 * Returns bearerbox ip address. Resolve it, if the address is localhost. Do 
 * not panic here. Even if we cannot do push, we still can do pull.
 */ 
static Octstr *name(Octstr *in)
{
    if (octstr_compare(in, octstr_imm("localhost")) != 0)
    	return octstr_duplicate(in);
    else
	    return octstr_duplicate(get_official_ip());
}

static BearerboxAddress *bearerbox_address_create(void) 
{
    BearerboxAddress *ba;    

    ba = gw_malloc(sizeof(BearerboxAddress));
    ba->mutex = mutex_create();
    ba->address = NULL;
    
    return ba;
}

static void bearerbox_address_destroy(BearerboxAddress *ba)
{
    if (ba == NULL)
        return;

    octstr_destroy(ba->address);
    mutex_destroy(ba->mutex);
    gw_free(ba);
}


