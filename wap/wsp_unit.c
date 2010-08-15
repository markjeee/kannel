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
 * wsp_unit.c - Implement WSP Connectionless mode
 *
 * Lars Wirzenius
 */


#include <string.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"
#include "wap_events.h"
#include "wsp_strings.h"
#include "wap.h"


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

static wap_dispatch_func_t *dispatch_to_wdp;
static wap_dispatch_func_t *dispatch_to_appl;

static List *queue = NULL;


static void main_thread(void *);
static WAPEvent *pack_into_result_datagram(WAPEvent *event);
static WAPEvent *pack_into_push_datagram(WAPEvent *event);

/***********************************************************************
 * Public functions
 */


void wsp_unit_init(wap_dispatch_func_t *datagram_dispatch,
                   wap_dispatch_func_t *application_dispatch) {
	queue = gwlist_create();
	gwlist_add_producer(queue);
	dispatch_to_wdp = datagram_dispatch;
	dispatch_to_appl = application_dispatch;
        wsp_strings_init();
	run_status = running;
	gwthread_create(main_thread, NULL);
}


void wsp_unit_shutdown(void) {
	gw_assert(run_status == running);
	run_status = terminating;
	gwlist_remove_producer(queue);
	gwthread_join_every(main_thread);
	gwlist_destroy(queue, wap_event_destroy_item);
        wsp_strings_shutdown();
}


void wsp_unit_dispatch_event(WAPEvent *event) {
	wap_event_assert(event);
	gwlist_produce(queue, event);
}


static WAPEvent *unpack_datagram(WAPEvent *datagram) {
	WAPEvent *event;
	Octstr *os;
	WSP_PDU *pdu;
	long tid_byte;
	int method;
	Octstr *method_name;

	gw_assert(datagram->type == T_DUnitdata_Ind);
	
	os = NULL;
	pdu = NULL;
	event = NULL;

	os = octstr_duplicate(datagram->u.T_DUnitdata_Ind.user_data);
	if (os && octstr_len(os) == 0) {
		warning(0, "WSP UNIT: Empty datagram.");
		goto error;
	}
	
	tid_byte = octstr_get_char(os, 0);
	octstr_delete(os, 0, 1);
	
	pdu = wsp_pdu_unpack(os);
	if (pdu == NULL)
		goto error;
	
	if (pdu->type != Get && pdu->type != Post) {
		warning(0, "WSP UNIT: Unsupported PDU type %d", pdu->type);
		goto error;
	}
        
	event = wap_event_create(S_Unit_MethodInvoke_Ind);
	event->u.S_Unit_MethodInvoke_Ind.addr_tuple = wap_addr_tuple_duplicate(
                datagram->u.T_DUnitdata_Ind.addr_tuple);
	event->u.S_Unit_MethodInvoke_Ind.transaction_id = tid_byte;
        
	switch (pdu->type) {
	case Get:
		debug("wap.wsp", 0, "Connectionless Get request received.");
		method = GET_METHODS + pdu->u.Get.subtype;
		event->u.S_Unit_MethodInvoke_Ind.request_uri = 
			octstr_duplicate(pdu->u.Get.uri);
		event->u.S_Unit_MethodInvoke_Ind.request_headers = 
			wsp_headers_unpack(pdu->u.Get.headers, 0);
		event->u.S_Unit_MethodInvoke_Ind.request_body = NULL;
		break;

	case Post:
		debug("wap.wsp", 0, "Connectionless Post request received.");
                method = POST_METHODS + pdu->u.Post.subtype;
		event->u.S_Unit_MethodInvoke_Ind.request_uri = 
			octstr_duplicate(pdu->u.Post.uri);
		event->u.S_Unit_MethodInvoke_Ind.request_headers = 
			wsp_headers_unpack(pdu->u.Post.headers, 1);
		event->u.S_Unit_MethodInvoke_Ind.request_body = 
			octstr_duplicate(pdu->u.Post.data);
		break;

	default:
		warning(0, "WSP UNIT: Unsupported PDU type %d", pdu->type);
		goto error;
	}

	method_name = wsp_method_to_string(method);
	if (method_name == NULL)
		method_name = octstr_format("UNKNOWN%02X", method);
	event->u.S_Unit_MethodInvoke_Ind.method = method_name;

	octstr_destroy(os);
	wsp_pdu_destroy(pdu);
	return event;

error:
	octstr_destroy(os);
	wsp_pdu_destroy(pdu);
	wap_event_destroy(event);
	return NULL;
}


/***********************************************************************
 * Local functions
 */

static void main_thread(void *arg) 
{
    WAPEvent *e;
    WAPEvent *newevent;
	
    while (run_status == running && (e = gwlist_consume(queue)) != NULL) {
        debug("wap.wsp.unit", 0, "WSP (UNIT): event arrived");
        wap_event_assert(e);
        switch (e->type) {
            case T_DUnitdata_Ind:
                newevent = unpack_datagram(e);
                if (newevent != NULL)
                    dispatch_to_appl(newevent);
                break;

            case S_Unit_MethodResult_Req:
                newevent = pack_into_result_datagram(e);
                if (newevent != NULL)
                    dispatch_to_wdp(newevent);
                break;

            case S_Unit_Push_Req:
                newevent = pack_into_push_datagram(e);
                if (newevent != NULL) 
                    dispatch_to_wdp(newevent);
                debug("wsp.unit", 0, "WSP (UNIT): delivering to wdp");
                break;
	
            default:
                warning(0, "WSP UNIT: Unknown event type %d", e->type);
                break;
        }
        wap_event_destroy(e);
    }
}


/*
 * We do not set TUnitData.ind's SMS-specific fields here, because we do not
 * support sending results to the phone over SMS. Wsp, chapter 8.4.1 states
 * that "that each peer entity is always associated with an encoding version.".
 * So we add Encoding-Version when we are sending something to the client.
 * (This includes push, which is not directly mentioned in chapter 8.4.2.70). 
 */
static WAPEvent *pack_into_result_datagram(WAPEvent *event) {
	WAPEvent *datagram;
	struct S_Unit_MethodResult_Req *p;
	WSP_PDU *pdu;
	Octstr *ospdu;
	unsigned char tid;
	
	gw_assert(event->type == S_Unit_MethodResult_Req);
	p = &event->u.S_Unit_MethodResult_Req;

    http_header_add(p->response_headers, "Encoding-Version", "1.3");

	pdu = wsp_pdu_create(Reply);
	pdu->u.Reply.status = wsp_convert_http_status_to_wsp_status(p->status);
	pdu->u.Reply.headers = wsp_headers_pack(p->response_headers, 1, WSP_1_3);
	pdu->u.Reply.data = octstr_duplicate(p->response_body);
	ospdu = wsp_pdu_pack(pdu);
	wsp_pdu_destroy(pdu);
	if (ospdu == NULL)
		return NULL;

	tid = p->transaction_id;
	octstr_insert_data(ospdu, 0, (char *)&tid, 1);

	datagram = wap_event_create(T_DUnitdata_Req);
	datagram->u.T_DUnitdata_Req.addr_tuple =
		wap_addr_tuple_duplicate(p->addr_tuple);
	datagram->u.T_DUnitdata_Req.user_data = ospdu;

	return datagram;
}

/*
 * According to WSP table 12, p. 63, push id and transaction id are stored 
 * into same field. T-UnitData.ind is different for IP and SMS bearer.
 */
static WAPEvent *pack_into_push_datagram(WAPEvent *event) {
        WAPEvent *datagram;
        WSP_PDU *pdu;
        Octstr *ospdu;
	unsigned char push_id;

        gw_assert(event->type == S_Unit_Push_Req);
        debug("wap.wsp.unit", 0, "WSP_UNIT: Connectionless push accepted");

        http_header_add(event->u.S_Unit_Push_Req.push_headers, 
                        "Encoding-Version", "1.3");

        pdu = wsp_pdu_create(Push);
	pdu->u.Push.headers = wsp_headers_pack(
            event->u.S_Unit_Push_Req.push_headers, 1, WSP_1_3);
	pdu->u.Push.data = octstr_duplicate(
            event->u.S_Unit_Push_Req.push_body);
        ospdu = wsp_pdu_pack(pdu);
	wsp_pdu_destroy(pdu);
	if (ospdu == NULL)
	    return NULL;

        push_id = event->u.S_Unit_Push_Req.push_id;
	octstr_insert_data(ospdu, 0, (char *)&push_id, 1);

        datagram = wap_event_create(T_DUnitdata_Req);

        datagram->u.T_DUnitdata_Req.addr_tuple =
	    wap_addr_tuple_duplicate(event->u.S_Unit_Push_Req.addr_tuple);
        datagram->u.T_DUnitdata_Req.address_type = 
	    event->u.S_Unit_Push_Req.address_type;
        if (event->u.S_Unit_Push_Req.smsc_id != NULL)
            datagram->u.T_DUnitdata_Req.smsc_id =
                octstr_duplicate(event->u.S_Unit_Push_Req.smsc_id);
        else
            datagram->u.T_DUnitdata_Req.smsc_id = NULL;
        if (event->u.S_Unit_Push_Req.dlr_url != NULL)
            datagram->u.T_DUnitdata_Req.dlr_url =
                octstr_duplicate(event->u.S_Unit_Push_Req.dlr_url);
        else
            datagram->u.T_DUnitdata_Req.dlr_url = NULL;
        datagram->u.T_DUnitdata_Req.dlr_mask = event->u.S_Unit_Push_Req.dlr_mask;
        if (event->u.S_Unit_Push_Req.smsbox_id != NULL)
            datagram->u.T_DUnitdata_Req.smsbox_id =
                octstr_duplicate(event->u.S_Unit_Push_Req.smsbox_id);
        else       
            datagram->u.T_DUnitdata_Req.smsbox_id = NULL;
        datagram->u.T_DUnitdata_Req.service_name =
            octstr_duplicate(event->u.S_Unit_Push_Req.service_name);

	datagram->u.T_DUnitdata_Req.user_data = ospdu;
        
        return datagram;
}





