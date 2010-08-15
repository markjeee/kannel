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
 * wtp.c - WTP common functions implementation
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */

#include "wtp.h" 
#include "wap_events.h"
#include "wtp_pdu.h"

/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Parse a datagram event (T-DUnitdata.ind) to create a corresponding
 * WTPEvents list object. Also check that the datagram is syntactically
 * valid. Return a pointer to the event structure that has been created.
 * This will be a RcvError packet if there was a problem unpacking the
 * datagram.
 */

static WAPEvent *unpack_wdp_datagram_real(WAPEvent *datagram);

static int deduce_tid(Octstr *user_data);
static int concatenated_message(Octstr *user_data);
static int truncated_datagram(WAPEvent *event);
static WAPEvent *unpack_invoke(WTP_PDU *pdu, WAPAddrTuple *addr_tuple);
static WAPEvent *unpack_segmented_invoke(WTP_PDU *pdu, WAPAddrTuple *addr_tuple);
static WAPEvent *unpack_result(WTP_PDU *pdu, WAPAddrTuple *addr_tuple);
static WAPEvent *unpack_ack(WTP_PDU *pdu, WAPAddrTuple *addr_tuple);
static WAPEvent *unpack_negative_ack(WTP_PDU *pdu, WAPAddrTuple *addr_tuple);
static WAPEvent *unpack_abort(WTP_PDU *pdu, WAPAddrTuple *addr_tuple);
static WAPEvent *pack_error(WAPEvent *datagram);

/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 * Handles a possible concatenated message. Creates a list of wap events.
 */
List *wtp_unpack_wdp_datagram(WAPEvent *datagram)
{
     List *events = NULL;
     WAPEvent *event = NULL;
     WAPEvent *subdgram = NULL;
     Octstr *data = NULL;
     long pdu_len;

     gw_assert(datagram->type == T_DUnitdata_Ind);

     events = gwlist_create();
        
     if (concatenated_message(datagram->u.T_DUnitdata_Ind.user_data)) {
        data = octstr_duplicate(datagram->u.T_DUnitdata_Ind.user_data);
        octstr_delete(data, 0, 1);

        while (octstr_len(data) != 0) {

            if (octstr_get_bits(data, 0, 1) == 0) {
                pdu_len = octstr_get_char(data, 0);
                octstr_delete(data, 0, 1);
            } else {
                pdu_len = octstr_get_bits(data, 1, 15);
                octstr_delete(data, 0, 2);
            }
      
            subdgram = wap_event_duplicate(datagram);
            octstr_destroy(subdgram->u.T_DUnitdata_Ind.user_data);
            subdgram->u.T_DUnitdata_Ind.user_data = octstr_copy(data, 0, pdu_len);
            wap_event_assert(subdgram);
            if ((event = unpack_wdp_datagram_real(subdgram)) != NULL) {
                wap_event_assert(event);
                gwlist_append(events, event);
            }
            octstr_delete(data, 0, pdu_len);
            wap_event_destroy(subdgram);
        }

        octstr_destroy(data);

    } else if ((event = unpack_wdp_datagram_real(datagram)) != NULL) { 
        wap_event_assert(event);
        gwlist_append(events, event);
    } else {
        warning(0, "WTP: Dropping unhandled datagram data:");
        octstr_dump(datagram->u.T_DUnitdata_Ind.user_data, 0, GW_WARNING);
    }

    return events;
}

/*
 * Responder set the first bit of the tid field. If we get a packet from the 
 * responder, we are the initiator and vice versa.
 *
 * Return 1, when the event is for responder, 0 when it is for initiator and 
 * -1 when error.
 */
int wtp_event_is_for_responder(WAPEvent *event)
{

     switch(event->type){
          
     case RcvInvoke:
         return event->u.RcvInvoke.tid < INITIATOR_TID_LIMIT;

     case RcvSegInvoke:
        return event->u.RcvSegInvoke.tid < INITIATOR_TID_LIMIT;

     case RcvResult:
         return event->u.RcvResult.tid < INITIATOR_TID_LIMIT;

     case RcvAck:
        return event->u.RcvAck.tid < INITIATOR_TID_LIMIT;

     case RcvNegativeAck:
        return event->u.RcvNegativeAck.tid < INITIATOR_TID_LIMIT;

     case RcvAbort:
        return event->u.RcvAbort.tid < INITIATOR_TID_LIMIT;

     case RcvErrorPDU:
        return event->u.RcvErrorPDU.tid < INITIATOR_TID_LIMIT;

     default:
        error(1, "Received an erroneous PDU corresponding an event");
        wap_event_dump(event);
        return -1;
     }
}

/*****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 * If pdu was truncated, tid cannot be trusted. We ignore this message.
 */
static int truncated_datagram(WAPEvent *dgram)
{
    gw_assert(dgram->type == T_DUnitdata_Ind);

    if (octstr_len(dgram->u.T_DUnitdata_Ind.user_data) < 3) {
        debug("wap.wtp", 0, "A too short PDU received");
        wap_event_dump(dgram);
        return 1;
    } else
        return 0;
}

static WAPEvent *unpack_invoke(WTP_PDU *pdu, WAPAddrTuple *addr_tuple)
{
    WAPEvent *event;

    event = wap_event_create(RcvInvoke);
    event->u.RcvInvoke.user_data = 
        octstr_duplicate(pdu->u.Invoke.user_data);
    event->u.RcvInvoke.tcl = pdu->u.Invoke.class;
    event->u.RcvInvoke.tid = pdu->u.Invoke.tid;
    event->u.RcvInvoke.tid_new = pdu->u.Invoke.tidnew;
    event->u.RcvInvoke.rid = pdu->u.Invoke.rid;
    event->u.RcvInvoke.up_flag = pdu->u.Invoke.uack;
    event->u.RcvInvoke.no_cache_supported = 0;
    event->u.RcvInvoke.version = pdu->u.Invoke.version;
    event->u.RcvInvoke.gtr = pdu->u.Invoke.gtr;
    event->u.RcvInvoke.ttr = pdu->u.Invoke.ttr;
    event->u.RcvInvoke.addr_tuple = wap_addr_tuple_duplicate(addr_tuple);

    return event;
}

static WAPEvent *unpack_segmented_invoke(WTP_PDU *pdu, WAPAddrTuple *addr_tuple)
{
    WAPEvent *event;

    event = wap_event_create(RcvSegInvoke);
    event->u.RcvSegInvoke.user_data = 
        octstr_duplicate(pdu->u.Segmented_invoke.user_data);
    event->u.RcvSegInvoke.tid = pdu->u.Segmented_invoke.tid;
    event->u.RcvSegInvoke.rid = pdu->u.Segmented_invoke.rid;
    event->u.RcvSegInvoke.no_cache_supported = 0;
    event->u.RcvSegInvoke.gtr = pdu->u.Segmented_invoke.gtr;
    event->u.RcvSegInvoke.ttr = pdu->u.Segmented_invoke.ttr;
    event->u.RcvSegInvoke.psn = pdu->u.Segmented_invoke.psn;
    event->u.RcvSegInvoke.addr_tuple = wap_addr_tuple_duplicate(addr_tuple);

    return event;
}

static WAPEvent *unpack_result(WTP_PDU *pdu, WAPAddrTuple *addr_tuple)
{
    WAPEvent *event;

    event = wap_event_create(RcvResult);
    event->u.RcvResult.user_data = 
        octstr_duplicate(pdu->u.Result.user_data);
    event->u.RcvResult.tid = pdu->u.Result.tid;
    event->u.RcvResult.rid = pdu->u.Result.rid;
    event->u.RcvResult.gtr = pdu->u.Result.gtr;
    event->u.RcvResult.ttr = pdu->u.Result.ttr;
    event->u.RcvResult.addr_tuple = wap_addr_tuple_duplicate(addr_tuple);

    return event;
}

static WAPEvent *unpack_ack(WTP_PDU *pdu, WAPAddrTuple *addr_tuple)
{
    WAPEvent *event;
    WTP_TPI *tpi;
    int	i, num_tpis;

    event = wap_event_create(RcvAck);
    event->u.RcvAck.tid = pdu->u.Ack.tid;
    event->u.RcvAck.tid_ok = pdu->u.Ack.tidverify;
    event->u.RcvAck.rid = pdu->u.Ack.rid;
    event->u.RcvAck.addr_tuple = wap_addr_tuple_duplicate(addr_tuple);

    /* Set default to 0 because Ack on 1 piece message has no tpi */
    event->u.RcvAck.psn = 0;
    num_tpis = gwlist_len(pdu->options);

    for (i = 0; i < num_tpis; i++) {
        tpi = gwlist_get(pdu->options, i);
        if (tpi->type == TPI_PSN) {
            event->u.RcvAck.psn = octstr_get_bits(tpi->data,0,8);
            break;
        }
    }

    return event;
}

static WAPEvent *unpack_negative_ack(WTP_PDU *pdu, WAPAddrTuple *addr_tuple)
{
    WAPEvent *event;

    event = wap_event_create(RcvNegativeAck);
    event->u.RcvNegativeAck.tid = pdu->u.Negative_ack.tid;
    event->u.RcvNegativeAck.rid = pdu->u.Negative_ack.rid;
    event->u.RcvNegativeAck.nmissing = pdu->u.Negative_ack.nmissing;
    event->u.RcvNegativeAck.missing = octstr_duplicate(pdu->u.Negative_ack.missing);
    event->u.RcvNegativeAck.addr_tuple = wap_addr_tuple_duplicate(addr_tuple);
    
    return event;
}

static WAPEvent *unpack_abort(WTP_PDU *pdu, WAPAddrTuple *addr_tuple)
{
     WAPEvent *event;

     event = wap_event_create(RcvAbort);
     event->u.RcvAbort.tid = pdu->u.Abort.tid;
     event->u.RcvAbort.abort_type = pdu->u.Abort.abort_type;
     event->u.RcvAbort.abort_reason = pdu->u.Abort.abort_reason;
     event->u.RcvAbort.addr_tuple = wap_addr_tuple_duplicate(addr_tuple);

     return event;
}

static WAPEvent *pack_error(WAPEvent *datagram)
{
    WAPEvent *event;

    gw_assert(datagram->type == T_DUnitdata_Ind);

    event = wap_event_create(RcvErrorPDU);
    event->u.RcvErrorPDU.tid = deduce_tid(datagram->u.T_DUnitdata_Ind.user_data);
    event->u.RcvErrorPDU.addr_tuple = 
	wap_addr_tuple_duplicate(datagram->u.T_DUnitdata_Ind.addr_tuple);

    return event;
}

/*
 * Transfers data from fields of a message to fields of WTP event. User data 
 * has the host byte order. Updates the log. 
 *
 * This function does incoming events check nro 4 (checking illegal headers
 * WTP 10.2).
 *
 * Return event, when we have a partially correct message or the message 
 * received has illegal header (WTP 10.2 nro 4); NULL, when the message was 
 * truncated or unpacking function returned NULL.
 */

WAPEvent *unpack_wdp_datagram_real(WAPEvent *datagram)
{
    WTP_PDU *pdu;
    WAPEvent *event;
    Octstr *data;

    gw_assert(datagram->type == T_DUnitdata_Ind);

    data = datagram->u.T_DUnitdata_Ind.user_data;

    if (truncated_datagram(datagram)) {
        warning(0, "WTP: got a truncated datagram, ignoring");
        return NULL;
    }

    pdu = wtp_pdu_unpack(data);

    /*
     * wtp_pdu_unpack returned NULL, we have send here a rcv error event,
     * but now we silently drop the packet. Because we can't figure out
     * in the pack_error() call if the TID value and hence the direction
     * inditation is really for initiator or responder. 
     */
    if (pdu == NULL) {
        error(0, "WTP: cannot unpack pdu, dropping packet.");
        return NULL;
    }   		

    event = NULL;

    switch (pdu->type) {

    case Invoke:
        event = unpack_invoke(pdu, datagram->u.T_DUnitdata_Ind.addr_tuple);
        /* if an WTP initiator gets invoke, it would be an illegal pdu. */
        if (!wtp_event_is_for_responder(event)){
            debug("wap.wtp", 0, "WTP: Invoke when initiator. Message was");
            wap_event_destroy(event);
            event = pack_error(datagram);
        }
        break;

    case Segmented_invoke:
        event = unpack_segmented_invoke(pdu, datagram->u.T_DUnitdata_Ind.addr_tuple);
        break;

    case Result:
        event = unpack_result(pdu, datagram->u.T_DUnitdata_Ind.addr_tuple);
        /* if an WTP responder gets result, it would be an illegal pdu. */
        if (wtp_event_is_for_responder(event)){
            debug("wap.wtp", 0, "WTP: Result when responder. Message was");
            wap_event_destroy(event);
            event = pack_error(datagram);
        }
        break;

    case Ack:
	    event = unpack_ack(pdu, datagram->u.T_DUnitdata_Ind.addr_tuple);    
        break;

    case Negative_ack:
        event = unpack_negative_ack(pdu, datagram->u.T_DUnitdata_Ind.addr_tuple);    
        break;

	case Abort:
	    event = unpack_abort(pdu, datagram->u.T_DUnitdata_Ind.addr_tuple);
        break;         

	default:
	    event = pack_error(datagram);
	    debug("wap.wtp", 0, "WTP: Unhandled PDU type. Message was");
            wap_event_dump(datagram);
	    return event;
	}

    wtp_pdu_destroy(pdu);
	
    wap_event_assert(event);
    return event;
}

/*
 * Used for debugging and when wtp unpack does not return a tid. We include
 * first bit; it tells does message received belong to the initiator or to the
 * responder.
 */

static int deduce_tid(Octstr *user_data)
{ 
    return octstr_get_bits(user_data, 8, 16);
}

static int concatenated_message(Octstr *user_data)
{
       return octstr_get_char(user_data, 0) == 0x00;
}



