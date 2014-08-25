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

#ifndef SMSCCONN_P_H
#define SMSCCONN_P_H

/* SMSC Connection private header
 *
 * Defines internal private structure
 *
 * Kalle Marjola 2000 for project Kannel
 *

 ADDING AND WORKING OF NEW SMS CENTER CONNECTIONS:

 These are guidelines and rules for adding new SMSC Connections to
 Kannel. See file bb_smscconn_cb.h for callback function prototypes.

 An SMSC Connection handler is free-formed module which only has the following
 rules:

 1) Each new SMSC Connection MUST implement function
    smsc_xxx_create(SMSCConn *conn, CfgGrp *cfg), which:

    a) SHOULD NOT block   (XXX)
    b) MUST warn about any configuration group variables it does
       not support    (XXX)
    c) MUST set up send_msg dynamic function to handle messages
       to-be-sent. This function MAY NOT block. This function MAY
       NOT destroy or alter the supplied message, but instead copy
       it if need to be stored
    d) CAN set up private shutdown function, which MAY NOT block
    e) SHOULD set private function to return number of queued messages
       to-be-sent inside the driver
    f) MUST set SMSCConn->name

 2) Each SMSC Connection MUST call certain BB callback functions when
    certain things occur:

    a) Each SMSC Connection MUST call callback function
       bb_smscconn_killed when it dies because it was put down earlier
       with bb_smscconn_shutdown or it simply cannot keep the connection
       up (wrong password etc. When killed,
       SMSC Connection MUST release all memory it has taken EXCEPT for
       the basic SMSCConn struct, which is laterwards released by the
       bearerbox.

    b) When SMSC Connection receives a message from SMSC, it must
       create a new Msg from it and call bb_smscconn_received

    c) When SMSC Connection has sent a message to SMSC, it MUST call
       callback function bb_smscconn_sent. The msg-parameter must be
       identical to msg supplied with smscconn_send, but it can be
       a duplicate of it

    d) When SMSC Connection has failed to send a message to SMSC, it
       MUST call callback function bb_smscconn_send_failed with appropriate
       reason. The message supplied as with bb_smscconn_send

    e) When SMSC Connection changes to SMSCCONN_ACTIVE, connection MUST
       call bb_smscconn_connected

 3) SMSC Connection MUST fill up SMSCConn structure as needed to, and is
    responsible for any concurrency timings. SMSCConn->status MAY NOT be
    set to SMSCCONN_DEAD until the connection is really that.
    Use why_killed to make internally dead, supplied with reason.

    If the connection is disconnected temporarily, the connection SHOULD
    call bb_smscconn_send_failed for each message in its internal list

 4) When SMSC Connection shuts down (shutdown called), it MUST try to send
    all messages so-far relied to it to be sent if 'finish_sending' is set
    to non-zero. If set to 0, it MUST call bb_smscconn_send_failed
    for each message not yet sent.

    After everything is ready (it can happen in different thread), before
    calling callback function bb_smscconn_killed it MUST release all memory it
    has taken except for basic SMSCConn structure, and set status to
    SMSCCONN_DEAD so it can be finally deleted.

 5) Callback bb_smscconn_ready is automatically called by main
    smscconn_create. New implementation MAY NOT call it directly

 6) SMSC Connection driver must obey is_stopped/stopped variable to
    suspend receiving (it can still send/re-connect), or must set
    appropriate function calls. When connection is stopped, it is not
    allowed to receive any new messages
*/

#include <signal.h>
#include "gwlib/gwlib.h"
#include "gwlib/regex.h"
#include "smscconn.h"
#include "load.h"

struct smscconn {
    /* variables set by appropriate SMSCConn driver */
    smscconn_status_t status;		/* see smscconn.h */
    int 	load;	       	/* load factor, 0 = no load */
    smscconn_killed_t why_killed;	/* time to die with reason, set when
				* shutdown called */
    time_t 	connect_time;	/* When connection to SMSC was established */

    Mutex 	*flow_mutex;	/* used to lock SMSCConn structure (both
				 *  in smscconn.c and specific driver) */

    /* connection specific counters (created in smscconn.c, updated
     *  by callback functions in bb_smscconn.c, NOT used by specific driver) */
    Counter *received;
    Counter *received_dlr;
    Counter *sent;
    Counter *sent_dlr;
    Counter *failed;

    /* SMSCConn variables set in smscconn.c */
    volatile sig_atomic_t 	is_stopped;

    Octstr *name;		/* Descriptive name filled from connection info */
    Octstr *id;			/* Abstract name specified in configuration and
				   used for logging and routing */
    Octstr *admin_id;
    List *allowed_smsc_id;
    List *denied_smsc_id;
    List *preferred_smsc_id;
    regex_t *allowed_smsc_id_regex;
    regex_t *denied_smsc_id_regex;
    regex_t *preferred_smsc_id_regex;

    Octstr *allowed_prefix;
    regex_t *allowed_prefix_regex;
    Octstr *denied_prefix;
    regex_t *denied_prefix_regex;
    Octstr *preferred_prefix;
    regex_t *preferred_prefix_regex;
    Octstr *unified_prefix;

    Octstr *our_host;   /* local device IP to bind for TCP communication */

    /* Our smsc specific log-file data */
    Octstr *log_file;
    long log_level;
    int log_idx;    /* index position within the global logfiles[] array in gwlib/log.c */

    long reconnect_delay; /* delay in seconds while re-connect attempts */

    int alt_dcs; /* use alternate DCS 0xFX */

    double throughput;     /* message thoughput per sec. to be delivered to SMSC */

    /* Stores rerouting information for this specific smsc-id */
    int reroute;                /* simply turn MO into MT and process internally */
    Dict *reroute_by_receiver;  /* reroute receiver numbers to specific smsc-ids */
    Octstr *reroute_to_smsc;    /* define a smsc-id to reroute to */
    int reroute_dlr;            /* should DLR's are rereouted too? */
    int dead_start;             /* don't connect this SMSC at startup time */

    long max_sms_octets; /* max allowed octets for this SMSC */

    Load *outgoing_sms_load;
    Load *incoming_sms_load;
    Load *incoming_dlr_load;
    Load *outgoing_dlr_load;

    /* XXX: move rest global data from Smsc here
     */

    /* pointers set by specific driver, but initiated to NULL by smscconn.
     * Note that flow_mutex is always locked before these functions are
     * called, and released after execution returns from them */

    /* pointer to function called when smscconn_shutdown called.
     * Note that this function is not needed always. If set, this
     * function MUST set why_killed */
    int (*shutdown) (SMSCConn *conn, int finish_sending);

    /* pointer to function called when a new message is needed to be sent.
     * MAY NOT block. Connection MAY NOT use msg directly after it has
     * returned from this function, but must instead duplicate it if need to.
     */
    int (*send_msg) (SMSCConn *conn, Msg *msg);

    /* pointer to function which returns current number of queued
     * messages to-be-sent. The function CAN also set load factor directly
     * to SMSCConn structure (above) */
    long (*queued) (SMSCConn *conn);

    /* pointers to functions called when connection started/stopped
     * (suspend/resume), if not NULL */

    void (*start_conn) (SMSCConn *conn);
    void (*stop_conn) (SMSCConn *conn);


    void *data;			/* SMSC specific stuff */
};

/*
 * Initializers for various SMSC connection implementations,
 * each should take same arguments and return an int,
 * which is 0 for okay and -1 for error.
 *
 * Each function is responsible for setting up all dynamic
 * function pointers at SMSCConn structure and starting up any
 * threads it might need.
 *
 * If conn->is_stopped is set (!= 0), create function MUST set
 * its internal state as stopped, so that laterwards called
 * smscconn_start works fine (and until it is called, no messages
 *  are received)
 */

/* generic wrapper for old SMSC implementations (uses old smsc.h).
 * Responsible file: smsc/smsc_wrapper.c */
int smsc_wrapper_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_fake.c */
int smsc_fake_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_cimd2.c */
int smsc_cimd2_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_emi.c */
int smsc_emi2_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_http.c */
int smsc_http_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_smpp.c */
int smsc_smpp_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_cgw.c */
int smsc_cgw_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_at.c. */
int smsc_at2_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_smasi.c */
int smsc_smasi_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_oisd.c */
int smsc_oisd_create(SMSCConn *conn, CfgGroup *cfg);

/* Responsible file: smsc/smsc_loopback.c */
int smsc_loopback_create(SMSCConn *conn, CfgGroup *cfg);

#ifdef HAVE_GSOAP
/* Responsible file: smsc/smsc_soap_parlayx.c */
int smsc_soap_parlayx_create(SMSCConn *conn, CfgGroup *cfg);
#endif

/* ADD NEW CREATE FUNCTIONS HERE
 *
 * int smsc_xxx_create(SMSCConn *conn, CfgGroup *cfg);
 */


#endif
