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
 * bearerbox.h
 *
 * General typedefs and functions for bearerbox
 */

#include "gwlib/gwlib.h"
#include "msg.h"
#include "smscconn.h"
#include "bb_store.h"

/* Default outgoing queue length */
#define DEFAULT_OUTGOING_SMS_QLENGTH    1000000

/* general bearerbox state */

enum {
    BB_RUNNING = 0,
    BB_ISOLATED = 1,	/* do not receive new messgaes from UDP/SMSC */
    BB_SUSPENDED = 2,	/* do not transfer any messages */
    BB_SHUTDOWN = 3,
    BB_DEAD = 4,
    BB_FULL = 5         /* message queue too long, do not accept new messages */
};


/* type of output given by various status functions */
enum {
    BBSTATUS_HTML = 0,
    BBSTATUS_TEXT = 1,
    BBSTATUS_WML = 2,
    BBSTATUS_XML = 3
};

/*---------------------------------------------------------------
 * Module interface to core bearerbox
 *
 * Modules implement one or more of the following interfaces:
 *
 * XXX_start(Cfg *config) - start the module
 * XXX_restart(Cfg *config) - restart the module, according to new config
 * XXX_shutdown() - start the avalanche - started from UDP/SMSC
 * XXX_die() - final cleanup
 *
 * XXX_addwdp() - only for SMSC/UDP: add a new WDP message to outgoing system
 */


/*---------------
 * bb_boxc.c (SMS and WAPBOX connections)
 */

int smsbox_start(Cfg *config);
int smsbox_restart(Cfg *config);

int wapbox_start(Cfg *config);

Octstr *boxc_status(int status_type);
/* tell total number of messages in separate wapbox incoming queues */
int boxc_incoming_wdp_queue(void);

/* Clean up after box connections have died. */
void boxc_cleanup(void);

/*
 * Route the incoming message to one of the following input queues:
 *   a specific smsbox conn
 *   a random smsbox conn if no shortcut routing and msg->sms.boxc_id match.
 * @return -1 if incoming queue full; 0 otherwise.
 */
int route_incoming_to_boxc(Msg *msg);


/*---------------
 * bb_udp.c (UDP receiver/sender)
 */

int udp_start(Cfg *config);
/* int udp_restart(Cfg *config); */
int udp_shutdown(void);
int udp_die(void);	/* called when router dies */

/* add outgoing WDP. If fails, return -1 and msg is untouched, so
 * caller must think of new uses for it */
int udp_addwdp(Msg *msg);
/* tell total number of messages in separate UDP outgoing port queues */
int udp_outgoing_queue(void);



/*---------------
 * bb_smscconn.c (SMS Center connections)
 */

int smsc2_start(Cfg *config);
int smsc2_restart(Cfg *config);

void smsc2_suspend(void);    /* suspend (can still send but not receive) */
void smsc2_resume(int is_init);     /* resume */
int smsc2_shutdown(void);
void smsc2_cleanup(void); /* final clean-up */

Octstr *smsc2_status(int status_type);

/* function to route outgoing SMS'es
 *
 * If finds a good one, puts into it and returns SMSCCONN_SUCCESS
 * If finds only bad ones, but acceptable, queues and
 *  returns SMSCCONN_QUEUED  (like all acceptable currently disconnected)
 * if message acceptable but queues full returns SMSCCONN_FAILED_QFULL and
 * message is not destroyed.
 * If cannot find nothing at all, returns SMSCCONN_FAILED_DISCARDED and
 * message is NOT destroyed (otherwise it is)
 */
long smsc2_rout(Msg *msg, int resend);

int smsc2_stop_smsc(Octstr *id);   /* shutdown a specific smsc */
int smsc2_restart_smsc(Octstr *id);  /* re-start a specific smsc */
int smsc2_add_smsc(Octstr *id);   /* add a new smsc */
int smsc2_remove_smsc(Octstr *id);   /* remove a specific smsc */

int smsc2_reload_lists(void); /* reload blacklists */


/*---------------
 * bb_http.c (HTTP Admin)
 */

int httpadmin_start(Cfg *config);
/* int http_restart(Cfg *config); */
void httpadmin_stop(void);


/*-----------------
 * bb_alog.c (Custom access-log format handling)
 */

/* passes the access-log-format string from config to the module */
void bb_alog_init(const Octstr *format);

/* cleanup for internal things */
void bb_alog_shutdown(void);

/* called from bb_smscconn.c to log the various access-log events */
void bb_alog_sms(SMSCConn *conn, Msg *sms, const char *message);



/*----------------------------------------------------------------
 * Core bearerbox public functions;
 * used only via HTTP adminstration
 */

int bb_shutdown(void);
int bb_isolate(void);
int bb_suspend(void);
int bb_resume(void);
int bb_restart(void);
int bb_flush_dlr(void);
int bb_stop_smsc(Octstr *id);
int bb_add_smsc(Octstr *id);
int bb_remove_smsc(Octstr *id);
int bb_restart_smsc(Octstr *id);
int bb_remove_message(Octstr *id);
int bb_reload_lists(void);
int bb_reload_smsc_groups(void);

/* return string of current status */
Octstr *bb_print_status(int status_type);


/*----------------------------------------------------------------
 * common function to all (in bearerbox.c)
 */

/* return linebreak for given output format, or NULL if format
 * not supported */
char *bb_status_linebreak(int status_type);


