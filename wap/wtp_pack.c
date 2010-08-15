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
 * 3. The end-user documentation includPd with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software 	eveloped by the 
 *        Kann†l Group (http://www.kannel.org/)." 
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
 * wtp_pack.c - WTP message packing module implementation
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "gwlib/gwlib.h"
#include "wtp_pack.h"
#include "wtp_pdu.h"


/*
 * Readable names for octets
 */
enum {
    first_byte,
    second_byte,
    third_byte,
    fourth_byte
};

/*
 * Types of header information added by the user (TPIs, or transportation 
 * information items). 
 */
enum {
    ERROR_DATA = 0x00,
    INFO_DATA = 0x01,
    OPTION = 0x02,
    PACKET_SEQUENCE_NUMBER = 0x03,
    SDU_BOUNDARY = 0x04,
    FRAME_BOUNDARY = 0x05
};

/*****************************************************************************
 *
 * Prototypes of internal functions
 */

/*
 * WTP defines SendTID and RcvTID.  We should use SendTID in all PDUs
 * we send.  The RcvTID is the one we got from the initial Invoke and
 * is the one we expect on all future PDUs for this machine.  
 * SendTID is always RcvTID xor 0x8000.
 * 
 * Note that when we are the Initiator, for example with WSP PUSH,
 * we must still store the RcvTID in machine->tid, to be consistent
 * with the current code.  So we'll choose the SendTID and then calculate
 * the RcvTID.
 */
static unsigned short send_tid(unsigned short tid);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */

WAPEvent *wtp_pack_invoke(WTPInitMachine *machine, WAPEvent *event)
{
    WAPEvent *dgram = NULL;
    WTP_PDU *pdu = NULL;

    gw_assert(event->type == TR_Invoke_Req);
    pdu = wtp_pdu_create(Invoke);
    pdu->u.Invoke.con = 0;
    pdu->u.Invoke.gtr = 1;
    pdu->u.Invoke.ttr = 1;
    pdu->u.Invoke.rid = 0;
    pdu->u.Invoke.version = 0;
    pdu->u.Invoke.tid = send_tid(machine->tid);
    pdu->u.Invoke.tidnew = machine->tidnew;
    pdu->u.Invoke.user_data =
	    octstr_duplicate(event->u.TR_Invoke_Req.user_data);
    pdu->u.Invoke.class = event->u.TR_Invoke_Req.tcl;
    pdu->u.Invoke.uack = event->u.TR_Invoke_Req.up_flag;

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple =
	wap_addr_tuple_duplicate(machine->addr_tuple);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    return dgram;
}
 
WAPEvent *wtp_pack_result(WTPRespMachine *machine, WAPEvent *event)
{
    WAPEvent *dgram = NULL;
    WTP_PDU *pdu = NULL;
     
    gw_assert(event->type == TR_Result_Req);
    pdu = wtp_pdu_create(Result);
    pdu->u.Result.con = 0;
    pdu->u.Result.gtr = 1;
    pdu->u.Result.ttr = 1;
    pdu->u.Result.rid = 0;
    pdu->u.Result.tid = send_tid(machine->tid);
    pdu->u.Result.user_data = 
     	octstr_duplicate(event->u.TR_Result_Req.user_data);

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple =
	wap_addr_tuple_duplicate(machine->addr_tuple);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);
  
    return dgram;
}

WAPEvent *wtp_pack_sar_result(WTPRespMachine *machine, int psn)
{
    WAPEvent *dgram = NULL;
    WTP_PDU *pdu = NULL;
    Octstr *data = NULL;
    int gtr, ttr;

    gw_assert(machine->sar && machine->sar->data);

    if (psn > machine->sar->nsegm)
        return dgram;

    ttr = psn == machine->sar->nsegm ? 1 : 0;
    gtr = ttr ? 0 : (psn+1)%SAR_GROUP_LEN ? 0 : 1;

    if (gtr || ttr) 
        machine->sar->tr = 1;

    data = octstr_copy(machine->sar->data,psn*SAR_SEGM_SIZE,SAR_SEGM_SIZE);

    if (!psn) {
        pdu = wtp_pdu_create(Result);
        pdu->u.Result.con = 0;
        pdu->u.Result.gtr = gtr;
        pdu->u.Result.ttr = ttr;
        pdu->u.Result.rid = 0;
        pdu->u.Result.tid = send_tid(machine->tid);
        pdu->u.Result.user_data = data;
    } else {
        pdu = wtp_pdu_create(Segmented_result);
        pdu->u.Segmented_result.con = 0;
        pdu->u.Segmented_result.gtr = gtr;
        pdu->u.Segmented_result.ttr = ttr;
        pdu->u.Segmented_result.rid = 0;
        pdu->u.Segmented_result.tid = send_tid(machine->tid);
        pdu->u.Segmented_result.psn = psn;
        pdu->u.Segmented_result.user_data = data;
    }

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple =
        wap_addr_tuple_duplicate(machine->addr_tuple);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    return dgram;
}

void wtp_pack_set_rid(WAPEvent *dgram, long rid)
{
    gw_assert(dgram != NULL);
    gw_assert(dgram->type == T_DUnitdata_Req);

    octstr_set_bits(dgram->u.T_DUnitdata_Req.user_data, 7, 1, rid);
}

WAPEvent *wtp_pack_abort(long abort_type, long abort_reason, long tid, 
                         WAPAddrTuple *address) 
{
    WAPEvent *dgram;
    WTP_PDU *pdu;

    pdu = wtp_pdu_create(Abort);
    pdu->u.Abort.con = 0;
    pdu->u.Abort.abort_type = abort_type;
    pdu->u.Abort.tid = send_tid(tid);
    pdu->u.Abort.abort_reason = abort_reason;

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple = wap_addr_tuple_duplicate(address);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    return dgram;
}

WAPEvent *wtp_pack_ack(long ack_type, int rid_flag, long tid, 
                       WAPAddrTuple *address)
{
    WAPEvent *dgram = NULL;
    WTP_PDU *pdu;
     
    pdu = wtp_pdu_create(Ack);
    pdu->u.Ack.con = 0;
    pdu->u.Ack.tidverify = ack_type;
    pdu->u.Ack.rid = rid_flag;
    pdu->u.Ack.tid = send_tid(tid);

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple = wap_addr_tuple_duplicate(address);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    return dgram;
}

WAPEvent *wtp_pack_sar_ack(long ack_type, long tid, WAPAddrTuple *address, int psn)
{
    WAPEvent *dgram = NULL;
    WTP_PDU *pdu;
    unsigned char cpsn = psn;
    
    pdu = wtp_pdu_create(Ack);
    pdu->u.Ack.con = 1;
    pdu->u.Ack.tidverify = ack_type;
    pdu->u.Ack.rid = 0;
    pdu->u.Ack.tid = send_tid(tid);

    wtp_pdu_append_tpi(pdu, 3, octstr_create_from_data((char*) &cpsn, 1));

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple = wap_addr_tuple_duplicate(address);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    return dgram;
}

/****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 */

static unsigned short send_tid(unsigned short tid)
{
       return tid ^ 0x8000;
}

/****************************************************************************/








