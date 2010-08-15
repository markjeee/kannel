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
 * wap.c - Generic functions for wap library
 */

#include "gwlib/gwlib.h"
#include "wap.h"
#include "wtp.h"

#define CONNECTIONLESS_PORT 9200

void wap_dispatch_datagram(WAPEvent *dgram)
{
    gw_assert(dgram != NULL);

    if (dgram->type != T_DUnitdata_Ind) {
	warning(0, "wap_dispatch_datagram got event of unexpected type.");
	wap_event_dump(dgram);
	wap_event_destroy(dgram);
        return;
    }

    /* XXX Assumption does not hold for client side */
    if (dgram->u.T_DUnitdata_Ind.addr_tuple->local->port == CONNECTIONLESS_PORT) {
	wsp_unit_dispatch_event(dgram);
    } else {
        List *events;

        events = wtp_unpack_wdp_datagram(dgram);

        if (!events) {
            debug("wap.wap", 0, "ignoring truncated datagram");
            wap_event_dump(dgram);
            wap_event_destroy(dgram);
            return;
        }

        while (gwlist_len(events) > 0) {
	    WAPEvent *event;

	    event = gwlist_extract_first(events);
            if (wtp_event_is_for_responder(event))
                wtp_resp_dispatch_event(event);
            else
                wtp_initiator_dispatch_event(event);
        }

        wap_event_destroy(dgram);
        gwlist_destroy(events, NULL);
    }
}
