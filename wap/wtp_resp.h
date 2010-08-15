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
 * WTP responder header
 *
 * Aarno Syvänen for Wapit Ltd
 */

#ifndef WTP_RESPONDER_H
#define WTP_RESPONDER_H

typedef struct WTPRespMachine WTPRespMachine;

#include "gwlib/gwlib.h"
#include "wap_events.h"
#include "timers.h"

typedef struct sar_info_t {
    int sar_psn;
    Octstr *sar_data;
} sar_info_t;


/*
 * Structure to keep SAR data during transmission
 */
typedef struct WTPSARData {
    int nsegm;  /* number of the last segment, i.e. total number - 1 */
    int csegm;  /* last segment confirmed by recipient */
    int lsegm;  /* last sent segment */
    int tr;		/* if current psn is gtr or ttr */
    Octstr *data;
} WTPSARData;

/* 
 * Maximum segment size. (Nokia WAP GW uses the size of 576, 
 * but mobiles use 1,5K size).
 */
#define	SAR_SEGM_SIZE 1400
#define	SAR_GROUP_LEN 3


/*
 * Responder machine states and responder WTP machine.
 * See file wtp_resp_state-decl.h for comments. Note that we must define macro
 * ROW to produce an empty string.
 */
enum resp_states {
    #define STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtp_resp_states.def"
    resp_states_count
};

typedef enum resp_states resp_states;

/*
 * See files wtp_resp_machine-decl.h and for comments. We define one macro for 
 * every separate type.
 */ 
struct WTPRespMachine {
       unsigned long mid; 
       #define INTEGER(name) int name; 
       #define TIMER(name) Timer *name; 
       #define ADDRTUPLE(name) WAPAddrTuple *name;
       #define ENUM(name) resp_states name;
       #define EVENT(name) WAPEvent *name;
       #define LIST(name) List *name;
       #define SARDATA(name) WTPSARData *name;
       #define MACHINE(field) field
       #include "wtp_resp_machine.def"
};

#endif
