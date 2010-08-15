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
 * WTLS Server Header
 *
 * Nick Clarey <nclarey@3glab.com>
 */

#ifndef WTLS_H
#define WTLS_H

typedef struct WTLSMachine WTLSMachine;

#include "gw/msg.h"
//#include "gw/wapbox.h"
#include "wap/wap_events.h"
#include "wap/wtls_pdu.h"

/*
 * WTLS Server machine states and WTLS machine.
 * See file wtls_state-decl.h for comments. Note that we must define macro
 * ROW to produce an empty string.
 */
enum serv_states {
    #define STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtls_state-decl.h"
    serv_states_count
};

typedef enum serv_states serv_states;

/*
 * See files wtls_machine-decl.h for comments. We define one macro for 
 * every separate type.
 */ 
struct WTLSMachine {
       unsigned long mid;
       #define ENUM(name) serv_states name;
       #define ADDRTUPLE(name) WAPAddrTuple *name;
       #define INTEGER(name) int name;
       #define OCTSTR(name) Octstr *name;
       #define MACHINE(field) field
       #define PDULIST(name) List *name;
       #include "wtls_machine-decl.h"
};

/*
 * Initialize the WTLS server.
 */
void wtls_init(void);

/*
 * Shut down the WTLS server machines. MUST be called after the subsystem isn't
 * used anymore.
 */
void wtls_shutdown(void);

/*
 * Transfers control of an event to the WTLS server machine subsystem.
 */ 
void wtls_dispatch_event(WAPEvent *event);

/*
 * Handles possible concatenated messages. Returns a list of wap events. 
 * Real unpacking is done by an internal function.
 */
WAPEvent *wtls_unpack_wdp_datagram(Msg *msg);

int wtls_get_address_tuple(long mid, WAPAddrTuple **tuple);

#endif
