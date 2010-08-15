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
 * wtp_pack.h - WTP implementation, message module header
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#ifndef WTP_SEND_H
#define WTP_SEND_H

#include "gwlib/gwlib.h"
#include "wap_events.h"
#include "wtp_init.h"
#include "wtp_resp.h"
#include "wtp.h"
#include "wap.h"

/*
 * Create a datagram event, having invoke PDU as user data. Fetches address,
 * tid and tid_new from the initiator state machine, other fields from event.
 * Only for the wtp initiator.
 *
 * Return message to be sent.
 */

WAPEvent *wtp_pack_invoke(WTPInitMachine *init_machine, WAPEvent *event);

/*
 * Create a datagram event, having result PDU as user data. Fetches SDU
 * from WTP event, address four-tuple and machine state information
 * (are we resending the packet) from WTP machine. Handles all 
 * errors by itself. Returns message, if OK, else NULL. Only for wtp 
 * responder.
 */

WAPEvent *wtp_pack_result(WTPRespMachine *resp_machine, WAPEvent *event); 

/*
 * Same as above but for a segmented result.
 */
WAPEvent *wtp_pack_sar_result(WTPRespMachine *resp_machine, int psn); 

/*
 * Create a datagram event, having abort PDU as user data. Fetches SDU
 * from WTP event, address four-tuple from WTP machine. 
 * Handles all errors by itself. Both for wtp initiator and responder.
 */

WAPEvent *wtp_pack_abort(long abort_type, long abort_reason, long tid, 
                         WAPAddrTuple *address);

/*
 * Create a datagram event, having ack PDU as user data. Creates SDU by
 * itself, fetches address four-tuple and machine state from WTP machine.
 * Ack_type is a flag telling whether we are doing tid verification or not,
 * rid_flag tells are we retransmitting. Handles all errors by itself.
 * Both for wtp initiator and responder.
 */

WAPEvent *wtp_pack_ack(long ack_type, int rid_flag, long tid, 
                       WAPAddrTuple *address);

/*
 * Same as above but for a segmented ack
 */
WAPEvent *wtp_pack_sar_ack(long ack_type, long tid, WAPAddrTuple *address, int psn);

/*
 * Set or unset the retransmission indicator on a PDU that has already
 * been packed as a datagram.  dgram must be of type T_DUnitdata_Req.
 */
void wtp_pack_set_rid(WAPEvent *dgram, long rid);
#endif
