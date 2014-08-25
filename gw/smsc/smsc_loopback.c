/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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
 * smsc_loopback.c - loopback SMSC interface
 * 
 * This SMSC type is the MT wise counterpart of the 'reroute' functionality
 * of the smsc group when MOs are re-routed as MT. This SMSC type re-routes
 * therefore MTs as MOs back again.
 *
 * Stipe Tolj <stolj at kannel dot org>
 */

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "dlr.h"

#define O_DESTROY(a) { octstr_destroy(a); a = NULL; }


static int msg_cb(SMSCConn *conn, Msg *msg)
{
    Msg *sms;
    
    /* create duplicates first */
    sms = msg_duplicate(msg);
    
    /* store temporary DLR data for SMSC ACK */
    if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask) && !uuid_is_null(sms->sms.id)) {
        Octstr *mid;
        char id[UUID_STR_LEN + 1];

        uuid_unparse(sms->sms.id, id);
        mid = octstr_create(id);

        dlr_add(conn->id, mid, sms, 0);
        
        octstr_destroy(mid);
    }

	/* 
	 * Inform abstraction layer of sent event,
	 * it will also take care of DLR SMSC events.
	 */
    bb_smscconn_sent(conn, sms, NULL);

    /* now change msg type to reflect flow type */
    sms = msg_duplicate(msg);
    sms->sms.sms_type = mo;
    
    /* Since this is a new MO now, make sure that the
     * values we have still from the MT are cleaned. */
    uuid_clear(sms->sms.id); 
    uuid_generate(sms->sms.id);
    O_DESTROY(sms->sms.boxc_id);
    sms->sms.dlr_mask = MSG_PARAM_UNDEFINED;
    O_DESTROY(sms->sms.dlr_url);
    
    /* 
     * If there is a reroute-smsc-id in the config group,
     * then let's use that value, otherwise assign the
     * smsc-id canonical name for the MO. 
     * This re-assignment of the smsc-id provides some
     * MO routing capabilities, i.e. via smsbox-route group.
     */
    octstr_destroy(sms->sms.smsc_id);
    
    if (conn->reroute_to_smsc) {
        sms->sms.smsc_id = octstr_duplicate(conn->reroute_to_smsc);
    } else {
        sms->sms.smsc_id = octstr_duplicate(conn->id);
    }
    
    /* now pass back again as MO to the abstraction layer */
    bb_smscconn_receive(conn, sms);

    return 0;
}


static int shutdown_cb(SMSCConn *conn, int finish_sending)
{
    debug("smsc.loopback", 0, "Shutting down SMSCConn %s", 
          octstr_get_cstr(conn->name));

    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    conn->status = SMSCCONN_DEAD;
    bb_smscconn_killed();
        
    return 0;
}


static void start_cb(SMSCConn *conn)
{
    conn->status = SMSCCONN_ACTIVE;
    conn->connect_time = time(NULL);
}


static long queued_cb(SMSCConn *conn)
{
    long ret = 0;

    conn->load = ret;
    return ret;
}


int smsc_loopback_create(SMSCConn *conn, CfgGroup *cfg)
{
    conn->data = NULL;
    conn->name = octstr_format("LOOPBACK:%S", conn->id);
  
    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = time(NULL);

    conn->shutdown = shutdown_cb;
    conn->queued = queued_cb;
    conn->start_conn = start_cb;
    conn->send_msg = msg_cb;

    return 0;
}
