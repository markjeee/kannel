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

#ifndef BB_SMSCCONN_CB
#define BB_SMSCCONN_CB

#include "msg.h"
#include "smscconn.h"

/* Callback functions for SMSC Connection implementations.
 * All functions return immediately.
 *
 * NOTE: These callback functions MUST be called by SMSCConn
 *   implementations in given times! See smscconn_p.h for details
 */


/* called immediately after startup is done. This is called
 * AUTOMATICALLY by smscconn_create, no need to call it from
 * various implementations */
void bb_smscconn_ready(SMSCConn *conn);

/* called each time when SMS center connected
 */
void bb_smscconn_connected(SMSCConn *conn);


/* called after SMSCConn is shutdown or it kills itself
 * because of non-recoverable problems. SMSC Connection has already
 * destroyed all its private data areas and set status as SMSCCONN_DEAD.
 * Calling this function must be the last thing done by SMSC Connection
 * before exiting with the last thread
 */
void bb_smscconn_killed(void);


/*
 * Called after successful sending of Msg 'sms'. Generate dlr message if
 * DLR_SMSC_SUCCESS mask is set. 'reply' will be passed as msgdata to
 * generated dlr message. This callback takes
 * care of 'sms' and 'reply' and it CAN NOT be used by caller again.
 */
void bb_smscconn_sent(SMSCConn *conn, Msg *sms, Octstr *reply);


/*
 * Called after failed sending of 'sms'. Generate dlr message if
 * DLR_SMSC_FAIL or DLR_FAIL mask is set. 'reply' will be passed as
 * msgdata to generated dlr message.Reason is set accordingly.
 * callback handles 'sms' and 'reply' and MAY NOT be used by caller again
 */
void bb_smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason, Octstr *reply);

enum {
    SMSCCONN_SUCCESS = 0,
    SMSCCONN_QUEUED,
    SMSCCONN_FAILED_SHUTDOWN,
    SMSCCONN_FAILED_REJECTED,
    SMSCCONN_FAILED_MALFORMED,
    SMSCCONN_FAILED_TEMPORARILY,
    SMSCCONN_FAILED_DISCARDED,
    SMSCCONN_FAILED_QFULL
};


/* called when a new message 'sms' received. Callback handles
 * 'sms' and MAY NOT be used by caller again. Return SMSCCONN_SUCCESS if all went
 * fine, SMSCCONN_FAILED_QFULL if incoming queue full, SMSCCONN_FAILED_TEMPORARILY
 * if store enabled and failed, and SMSCCONN_FAILED_REJECTED if bearerbox does
 * NOT accept the 'sms' (black/whitelisted) */
long bb_smscconn_receive(SMSCConn *conn, Msg *sms);


#endif
