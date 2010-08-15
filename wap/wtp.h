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
 * wtp.h - WTP implementation general header, common things for the iniator 
 * and the responder.
 */

#ifndef WTP_H
#define WTP_H

#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "gwlib/gwlib.h"
#include "wap_addr.h"
#include "wap_events.h"

/* 
 * Use this structure for storing segments to be reassembled
 */
typedef struct WTPSegment WTPSegment;

/*
 * For removing the magic
 */
enum {
    NUMBER_OF_ABORT_TYPES = 2,
    NUMBER_OF_ABORT_REASONS  = 10,
    NUMBER_OF_TRANSACTION_CLASSES = 3
};

/*
 * For now, timers are defined. They will depend on bearer information fetched
 * from address (or from a header field of the protocol speaking with the
 * bearerbox). For suggested timers, see WTP, Appendix A.
 */
enum {
    L_A_WITH_USER_ACK = 4,
    L_R_WITH_USER_ACK = 7,
    S_R_WITHOUT_USER_ACK = 3,
    S_R_WITH_USER_ACK = 4,
    G_R_WITHOUT_USER_ACK = 3,
    G_R_WITH_USER_ACK = 3,
    W_WITH_USER_ACK = 30
};

/*
 * Maximum values for counters (for retransmissions and acknowledgement waiting
 * periods)
 */
enum {
    AEC_MAX = 6,
    MAX_RCR = 8
};

/*
 * Types of acknowledgement PDU (normal acknowledgement or tid verification)
 */
enum {
    ACKNOWLEDGEMENT = 0,
    TID_VERIFICATION = 1
};

/*
 * Who is aborting (WTP or WTP user)
 */
enum {
    PROVIDER = 0x00,
    USER = 0x01
};

/*
 * WTP abort types (i.e., provider abort codes defined by WAP)
 */
enum {
    UNKNOWN = 0x00,
    PROTOERR = 0x01,
    INVALIDTID = 0x02,
    NOTIMPLEMENTEDCL2 = 0x03,
    NOTIMPLEMENTEDSAR = 0x04,
    NOTIMPLEMENTEDUACK = 0x05,
    WTPVERSIONZERO = 0x06,
    CAPTEMPEXCEEDED = 0x07,
    NORESPONSE = 0x08,
    MESSAGETOOLARGE = 0x09,
    NOTIMPLEMENTEDESAR = 0x0A
};    

/*
 * Transaction classes
 */
enum {
    TRANSACTION_CLASS_0 = 0,
    TRANSACTION_CLASS_1 = 1,
    TRANSACTION_CLASS_2 = 2
};

/*
 * Types of acknowledgement
 */
enum {
    PROVIDER_ACKNOWLEDGEMENT = 0,
    USER_ACKNOWLEDGEMENT = 1
};

/*
 * Who is indicating, wtp initiator or responder.
 */
enum {
    INITIATOR_INDICATION = 0,
    RESPONDER_INDICATION = 1 
};

enum {
    TPI_ERROR = 0,
    TPI_INFO = 1,
    TPI_OPTION = 2,
    TPI_PSN = 3,
    TPI_SDU_BOUNDARY = 4,
    TPI_FRAME_BOUNDARY = 5
};

/*
 * Responder set first tid, initiator not. So all tids send by initiator are 
 * greater than 2**15.
 */
#define INITIATOR_TID_LIMIT (1 << 15)

/*
 *  Transaction is identified by the address four-tuple and tid.
 */
struct machine_pattern {
    WAPAddrTuple *tuple;
    long tid;
    long mid;
};

typedef struct machine_pattern machine_pattern;

/*
 * Handles possible concatenated messages. Returns a list of wap events, 
 * consisting of these events. 
 */
List *wtp_unpack_wdp_datagram(WAPEvent *datagram);

/*
 * Responder set the first bit of the tid field. If we get a packet from the 
 * responder, we are the initiator. 
 *
 * Returns 1 for responder, 0 for iniator and -1 for error.
 */
int wtp_event_is_for_responder(WAPEvent *event);

#endif
