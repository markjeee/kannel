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
 * wtp_tid.h - tid verification implementation header
 *
 * By Aarno Syvänen for WapIT Ltd
 */

#ifndef WTP_TID_H
#define WTP_TID_H

typedef struct WTPCached_tid WTPCached_tid;

#include <math.h>
#include <stdlib.h>

#include "gwlib/gwlib.h"
#include "wap_events.h"
#include "wtp_resp.h"

#define WTP_TID_WINDOW_SIZE (1L << 14)

/*
 * Constants defining the result of tid validation
 */
enum {
    no_cached_tid,
    ok,
    fail
};

/*
 * Tid cache item consists of initiator identifier and cached tid.
 */
struct WTPCached_tid {
    WAPAddrTuple *addr_tuple;
    long tid;
};

/* 
 * Initialize tid cache. MUST be called before calling other functions in this 
 * module.
 */

void wtp_tid_cache_init(void);

/*
 * Shut down the tid cache. MUST be called after tid cache isn't used anymore.
 */
void wtp_tid_cache_shutdown(void);

/*
 * Does the tid validation test, by using a simple window mechanism
 *
 * Returns: no_cached_tid, if the peer has no cached last tid, or the result
 * of the test (ok, fail);
 */

int wtp_tid_is_valid(WAPEvent *event, WTPRespMachine *machine);

/*
 * Changes the tid value belonging to an existing initiator
 */
void wtp_tid_set_by_machine(WTPRespMachine *machine, long tid);

#endif


