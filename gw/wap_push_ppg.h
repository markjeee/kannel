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
 * Push PPG main module header
 *
 * This module implements following Wapforum specifications:
 *      WAP-151-PPGService-19990816-a (called afterwards ppg),
 *      WAP-164-PAP-19991108-a (pap),
 *      WAP-164_100-PAP-20000218-a (pap implementation note).
 * 
 * We refer following Wapforum specifications:
 *      WAP-145-PushMessage-19990816-a (push message)
 *      WAP-200-WDP-20001212-a (wdp)
 *      WAP-203-WSP-20000504-a (wsp)
 *      WAP-189-PushOTA-20000217-a (ota).
 *
 * In addition, rfcs 1521, 2045 and 2617 are referred.
 *
 * By Aarno Syvänen for Wapit Ltd and for Wiral Ltd.
 */

#ifndef WAP_PUSH_PPG_H
#define WAP_PUSH_PPG_H

#include "wap/wap_events.h"
#include "wap/wap.h"
#include "wap/wap_addr.h"
#include "gwlib/gwlib.h"

typedef struct PPGSessionMachine PPGSessionMachine;
typedef struct PPGPushMachine PPGPushMachine;

/*
 * Enumerations used by PPG main module for PAP attribute, see PPG Services, 
 * Chapter 6.
 *
 * Message state
 */
enum {
    PAP_UNDELIVERABLE,         /* general message status */
    PAP_UNDELIVERABLE1,        /* transformation failure */
    PAP_UNDELIVERABLE2,        /* no bearer support */
    PAP_PENDING,
    PAP_EXPIRED,
    PAP_DELIVERED,             /* general message status */
    PAP_DELIVERED1,            /* for unconfirmed push, PPG internal */
    PAP_DELIVERED2,            /* for confirmed push, PPG internal  */
    PAP_ABORTED,
    PAP_TIMEOUT,
    PAP_CANCELLED
};

/*
 * PAP protocol status codes used by PPG main module. See Push Access Protocol,
 * 9.13 and 9.14. 
 */
enum {
    PAP_OK = 1000,
    PAP_ACCEPTED_FOR_PROCESSING = 1001,
    PAP_BAD_REQUEST = 2000, 
    PAP_FORBIDDEN = 2001,
    PAP_ADDRESS_ERROR = 2002,
    PAP_CAPABILITIES_MISMATCH = 2005,
    PAP_DUPLICATE_PUSH_ID = 2007,
    PAP_INTERNAL_SERVER_ERROR = 3000,
    PAP_TRANSFORMATION_FAILURE = 3006,
    PAP_REQUIRED_BEARER_NOT_AVAILABLE = 3010,
    PAP_SERVICE_FAILURE = 4000,
    PAP_CLIENT_ABORTED = 5000,
    PAP_ABORT_USERPND = 5028
};

/*
 * Values for last attribute (it is, is this message last using this bearer).
 */
enum {
    NOT_LAST,
    LAST
};

/*
 * Enumerations used by PAP message fields, see Push Access Protocol, Chapter
 * 9. Default values are the first ones (ones having value 0)
 *
 * Simple answer to question is something required or not
 */
enum {
    PAP_FALSE,
    PAP_TRUE
};

/*
 * Priority
 */
enum {
    PAP_MEDIUM,
    PAP_HIGH,
    PAP_LOW
};

/*
 * Delivery method
 */
enum {
    PAP_NOT_SPECIFIED = 0,
    PAP_PREFERCONFIRMED = 1,
    PAP_UNCONFIRMED = 2,
    PAP_CONFIRMED = 3
};

/*
 * Port number definitions
 */
enum {
    CONNECTIONLESS_PUSH_CLIPORT = 2948,
    CONNECTIONLESS_SERVPORT = 9200,
    CONNECTED_CLIPORT = 9209,
    CONNECTED_SERVPORT = 9201
};

struct PPGSessionMachine {
    #define OCTSTR(name) Octstr *name;
    #define ADDRTUPLE(name) WAPAddrTuple *name;
    #define INTEGER(name) long name;
    #define PUSHMACHINES(name) List *name;
    #define CAPABILITIES(name) List *name;
    #define MACHINE(fields) fields
    #include "wap_ppg_session_machine.def"
};

struct PPGPushMachine {
    #define OCTSTR(name) Octstr *name;
    #define OPTIONAL_OCTSTR(name) Octstr *name;
    #define INTEGER(name) long name;
    #define ADDRTUPLE(name) WAPAddrTuple *name;
    #define HTTPHEADER(name) List *name;
    #define CAPABILITIES(name) List *name;
    #define MACHINE(fields) fields
    #include "wap_ppg_push_machine.def"
};

void wap_push_ppg_init(wap_dispatch_func_t *ota_dispatch,
                       wap_dispatch_func_t *appl_dispatch, Cfg *cfg);
void wap_push_ppg_shutdown(void);
void wap_push_ppg_dispatch_event(WAPEvent *e);

/*
 * Check do we have established a session with an initiator for this push.
 * Initiators are identified by their address tuple (ppg main module does not
 * know wsp sessions until told. 
 */
PPGSessionMachine *wap_push_ppg_have_push_session_for(WAPAddrTuple *tuple);

/*
 * Now iniator are identified by their session id. This function is used after
 * the session is established.
 */
PPGSessionMachine *wap_push_ppg_have_push_session_for_sid(long sid);

#endif
