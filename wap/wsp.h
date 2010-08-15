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
 * wsp.h - WSP implementation header
 */

#ifndef WSP_H
#define WSP_H

/*
 * int WSP_accepted_extended_methods[] = { -1 };
 * int WSP_accepted_header_code_pages[] = { -1 };
 */

typedef enum {
    WSP_1_1 = 1, 
    WSP_1_2 = 2, 
    WSP_1_3 = 3, 
    WSP_1_4 = 4,
    WSP_1_5 = 5,
    WSP_1_6 = 6,
} wsp_encoding;

/* See Table 35 of the WSP standard */
enum wsp_abort_values {
    WSP_ABORT_PROTOERR = 0xe0,
    WSP_ABORT_DISCONNECT = 0xe1,
    WSP_ABORT_SUSPEND = 0xe2,
    WSP_ABORT_RESUME = 0xe3,
    WSP_ABORT_CONGESTION = 0xe4,
    WSP_ABORT_CONNECTERR = 0xe5,
    WSP_ABORT_MRUEXCEEDED = 0xe6,
    WSP_ABORT_MOREXCEEDED = 0xe7,
    WSP_ABORT_PEERREQ = 0xe8,
    WSP_ABORT_NETERR = 0xe9,
    WSP_ABORT_USERREQ = 0xea,
    WSP_ABORT_USERRFS = 0xeb,
    WSP_ABORT_USERPND = 0xec,
    WSP_ABORT_USERDCR = 0xed,
    WSP_ABORT_USERDCU = 0xee
};


typedef struct WSPMachine WSPMachine;
typedef struct WSPMethodMachine WSPMethodMachine;
typedef struct WSPPushMachine WSPPushMachine;

#include "gwlib/gwlib.h"
#include "wap_addr.h"
#include "wap_events.h"

struct WSPMachine {
	#define INTEGER(name) long name;
	#define OCTSTR(name) Octstr *name;
	#define HTTPHEADERS(name) List *name;
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#define COOKIES(name) List *name;
	#define REFERER(name) Octstr *name;
	#define MACHINESLIST(name) List *name;
	#define CAPABILITIES(name) List *name;
	#define MACHINE(fields) fields
	#include "wsp_server_session_machine.def"
};


struct WSPMethodMachine {
	#define INTEGER(name) long name;
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#define EVENT(name) WAPEvent *name;
	#define MACHINE(fields) fields
	#include "wsp_server_method_machine.def"
};

struct WSPPushMachine {
       #define INTEGER(name) long name;
       #define ADDRTUPLE(name) WAPAddrTuple *name;
       #define HTTPHEADER(name) List *name;
       #define MACHINE(fields) fields
       #include "wsp_server_push_machine.def"
};

/*
 * Shared stuff.
 */
long wsp_convert_http_status_to_wsp_status(long http_status);
WSPMachine *find_session_machine_by_id(int);

#endif



