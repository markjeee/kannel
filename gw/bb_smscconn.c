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
 * SMSC Connection interface for Bearerbox.
 *
 * Includes callback functions called by SMSCConn implementations
 *
 * Handles all startup/shutdown adminstrative work in bearerbox, plus
 * routing, writing actual access logs, handling failed messages etc.
 *
 * Kalle Marjola 2000 for project Kannel
 * Alexander Malysh <amalysh at kannel.org> 2003, 2004, 2005
 */
 
#include "gw-config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>


#include "gwlib/gwlib.h"
#include "msg.h"
#include "sms.h"
#include "bearerbox.h"
#include "numhash.h"
#include "smscconn.h"
#include "dlr.h"
#include "load.h"

#include "bb_smscconn_cb.h"    /* callback functions for connections */
#include "smscconn_p.h"        /* to access counters */

#include "smsc/smpp_pdu.h"     /* access smpp_pdu_init/smpp_pdu_shutdown */

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_sms;
extern List *outgoing_sms;

extern Counter *incoming_sms_counter;
extern Counter *outgoing_sms_counter;
extern Counter *incoming_dlr_counter;
extern Counter *outgoing_dlr_counter;

extern Load *outgoing_sms_load;
extern Load *incoming_sms_load;
extern Load *incoming_dlr_load;
extern Load *outgoing_dlr_load;

extern List *flow_threads;
extern List *suspended;
extern List *isolated;

/* outgoing sms queue control */
extern long max_outgoing_sms_qlength;
/* incoming sms queue control */
extern long max_incoming_sms_qlength;

/* configuration filename */
extern Octstr *cfg_filename;

/* our own thingies */

static volatile sig_atomic_t smsc_running;
static List *smsc_list;
static RWLock smsc_list_lock;
static List *smsc_groups;
static Octstr *unified_prefix;

static RWLock white_black_list_lock;
static Octstr *black_list_url;
static Octstr *white_list_url;
static Numhash *black_list;
static Numhash *white_list;

static regex_t *white_list_regex;
static regex_t *black_list_regex;

static long router_thread = -1;

/* message resend */
static long sms_resend_frequency;
static long sms_resend_retry;

/*
 * Counter for catenated SMS messages. The counter that can be put into
 * the catenated SMS message's UDH headers is actually the lowest 8 bits.
 */
Counter *split_msg_counter;

/* Flag for handling concatenated incoming messages. */
static int handle_concatenated_mo;
/* How long to wait for message parts */
static long concatenated_mo_timeout;
/* Flag for return value of check_concat */
enum {concat_error = -1, concat_complete = 0, concat_pending = 1, concat_none};

/*
 * forward declaration
 */
static long route_incoming_to_smsc(SMSCConn *conn, Msg *msg);

static void init_concat_handler(void);
static void shutdown_concat_handler(void);
static int check_concatenation(Msg **msg, Octstr *smscid);
static void clear_old_concat_parts(void);

/*---------------------------------------------------------------------------
 * CALLBACK FUNCTIONS
 *
 * called by SMSCConn implementations when appropriate
 */

void bb_smscconn_ready(SMSCConn *conn)
{
    gwlist_add_producer(flow_threads);
    gwlist_add_producer(incoming_sms);
}


void bb_smscconn_connected(SMSCConn *conn)
{
    if (router_thread >= 0)
	gwthread_wakeup(router_thread);
}


void bb_smscconn_killed(void)
{
    /* NOTE: after status has been set to SMSCCONN_DEAD, bearerbox
     *   is free to release/delete 'conn'
     */
    gwlist_remove_producer(incoming_sms);
    gwlist_remove_producer(flow_threads);
}


static void handle_split(SMSCConn *conn, Msg *msg, long reason)
{
    struct split_parts *split = msg->sms.split_parts;
    
    /*
     * if the reason is not a success and status is still success
     * then set status of a split to the reason.
     * Note: reason 'malformed','discarded' or 'rejected' has higher priority!
     */
    switch(reason) {
    case SMSCCONN_FAILED_TEMPORARILY:
        /*
         * Check if SMSC link alive and if so increase resend_try and set resend_time.
         * If SMSC link is not active don't increase resend_try and don't set resend_time
         * because we don't want to delay messages due to broken connection.
         */
        if (smscconn_status(conn) == SMSCCONN_ACTIVE) {
            /*
             * Check if sms_resend_retry set and this msg has exceeded a limit also
             * honor "single shot" with sms_resend_retry set to zero.
             */
            if (sms_resend_retry >= 0 && msg->sms.resend_try >= sms_resend_retry) {
                warning(0, "Maximum retries for message exceeded, discarding it!");
                bb_smscconn_send_failed(NULL, msg, SMSCCONN_FAILED_DISCARDED,
                                        octstr_create("Retries Exceeded"));
                return;
            }
            msg->sms.resend_try = (msg->sms.resend_try > 0 ? msg->sms.resend_try + 1 : 1);
            time(&msg->sms.resend_time);
        }
        gwlist_produce(outgoing_sms, msg);
        return;
    case SMSCCONN_FAILED_DISCARDED:
    case SMSCCONN_FAILED_REJECTED:
    case SMSCCONN_FAILED_MALFORMED:
        debug("bb.sms.splits", 0, "Set split msg status to %ld", reason);
        split->status = reason;
        break;
    case SMSCCONN_SUCCESS:
        break; /* nothing todo */
    default:
        if (split->status == SMSCCONN_SUCCESS) {
            debug("bb.sms.splits", 0, "Set split msg status to %ld", reason);
            split->status = reason;
        }
        break;
    }

    /*
     * now destroy this message, because we don't need it anymore.
     * we will split it again in smscconn_send(...).
     */
    msg_destroy(msg);
        
    if (counter_decrease(split->parts_left) <= 1) {
        /* all splited parts were processed */
        counter_destroy(split->parts_left);
        msg = split->orig;
        msg->sms.split_parts = NULL;
        if (split->status == SMSCCONN_SUCCESS)
            bb_smscconn_sent(conn, msg, NULL);
        else {
            debug("bb.sms.splits", 0, "Parts of concatenated message failed.");
            bb_smscconn_send_failed(conn, msg, split->status, NULL);
        }
        gw_free(split);
    }
}


void bb_smscconn_sent(SMSCConn *conn, Msg *sms, Octstr *reply)
{
    if (sms->sms.split_parts != NULL) {
        handle_split(conn, sms, SMSCCONN_SUCCESS);
        octstr_destroy(reply);
        return;
    }

    /* write ACK to store file */
    store_save_ack(sms, ack_success);

    if (sms->sms.sms_type != report_mt) {
        bb_alog_sms(conn, sms, "Sent SMS");
        counter_increase(outgoing_sms_counter);
        load_increase(outgoing_sms_load);
        if (conn != NULL) {
            counter_increase(conn->sent);
            load_increase(conn->outgoing_sms_load);
        }
    } else {
        bb_alog_sms(conn, sms, "Sent DLR");
        counter_increase(outgoing_dlr_counter);
        load_increase(outgoing_dlr_load);
        if (conn != NULL) {
            counter_increase(conn->sent_dlr);
            load_increase(conn->outgoing_dlr_load);
        }
    }

    /* generate relay confirmancy message */
    if (DLR_IS_SMSC_SUCCESS(sms->sms.dlr_mask)) {
        Msg *dlrmsg;

	if (reply == NULL)
	    reply = octstr_create("");

	octstr_insert_data(reply, 0, "ACK/", 4);
        dlrmsg = create_dlr_from_msg((conn->id?conn->id:conn->name), sms,
	                reply, DLR_SMSC_SUCCESS);
        if (dlrmsg != NULL) {
            bb_smscconn_receive(conn, dlrmsg);
        }
    }

    msg_destroy(sms);
    octstr_destroy(reply);
}


void bb_smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason, Octstr *reply)
{
    if (sms->sms.split_parts != NULL) {
        handle_split(conn, sms, reason);
        octstr_destroy(reply);
        return;
    }
    
    switch (reason) {
    case SMSCCONN_FAILED_TEMPORARILY:
        /*
         * Check if SMSC link alive and if so increase resend_try and set resend_time.
         * If SMSC link is not active don't increase resend_try and don't set resend_time
         * because we don't want to delay messages due to a broken connection.
         */
       if (conn && smscconn_status(conn) == SMSCCONN_ACTIVE) {
            /*
             * Check if sms_resend_retry set and this msg has exceeded a limit also
             * honor "single shot" with sms_resend_retry set to zero.
             */
           if (sms_resend_retry >= 0 && sms->sms.resend_try >= sms_resend_retry) {
               warning(0, "Maximum retries for message exceeded, discarding it!");
               bb_smscconn_send_failed(NULL, sms, SMSCCONN_FAILED_DISCARDED, 
                                       octstr_create("Retries Exceeded"));
               break;
           }
           sms->sms.resend_try = (sms->sms.resend_try > 0 ? sms->sms.resend_try + 1 : 1);
           time(&sms->sms.resend_time);
       }
       gwlist_produce(outgoing_sms, sms);
       break;
       
    case SMSCCONN_FAILED_SHUTDOWN:
        gwlist_produce(outgoing_sms, sms);
        break;

    default:
        /* write NACK to store file */
        store_save_ack(sms, ack_failed);

        if (conn) counter_increase(conn->failed);
        if (reason == SMSCCONN_FAILED_DISCARDED) {
            if (sms->sms.sms_type != report_mt)
                bb_alog_sms(conn, sms, "DISCARDED SMS");
            else
                bb_alog_sms(conn, sms, "DISCARDED DLR");
        }
        else if (reason == SMSCCONN_FAILED_EXPIRED) {
            if (sms->sms.sms_type != report_mt)
                bb_alog_sms(conn, sms, "EXPIRED SMS");
            else
                bb_alog_sms(conn, sms, "EXPIRED DLR");
        }
        else {
            if (sms->sms.sms_type != report_mt)
                bb_alog_sms(conn, sms, "FAILED Send SMS");
            else
                bb_alog_sms(conn, sms, "FAILED Send DLR");
        }

        /* generate relay confirmancy message */
        if (DLR_IS_SMSC_FAIL(sms->sms.dlr_mask) ||
	    DLR_IS_FAIL(sms->sms.dlr_mask)) {
            Msg *dlrmsg;

	    if (reply == NULL)
	        reply = octstr_create("");

	    octstr_insert_data(reply, 0, "NACK/", 5);
            dlrmsg = create_dlr_from_msg((conn ? (conn->id?conn->id:conn->name) : NULL), sms,
	                                 reply, DLR_SMSC_FAIL);
            if (dlrmsg != NULL) {
                bb_smscconn_receive(conn, dlrmsg);
            }
        }

        msg_destroy(sms);
        break;
    }

    octstr_destroy(reply);
}

long bb_smscconn_receive(SMSCConn *conn, Msg *sms)
{
    char *uf;
    int rc;
    Msg *copy;

    /*
     * first check whether msgdata data is NULL and set it to empty
     *  because seems too much kannels parts rely on msgdata not to be NULL.
     */
    if (sms->sms.msgdata == NULL)
        sms->sms.msgdata = octstr_create("");

    /*
     * First normalize in smsc level and then on global level.
     * In outbound direction it's vise versa, hence first global then smsc.
     */
    uf = (conn && conn->unified_prefix) ? octstr_get_cstr(conn->unified_prefix) : NULL;
    normalize_number(uf, &(sms->sms.sender));

    uf = unified_prefix ? octstr_get_cstr(unified_prefix) : NULL;
    normalize_number(uf, &(sms->sms.sender));

    gw_rwlock_rdlock(&white_black_list_lock);
    if (white_list && numhash_find_number(white_list, sms->sms.sender) < 1) {
        gw_rwlock_unlock(&white_black_list_lock);
        info(0, "Number <%s> is not in white-list, message discarded",
             octstr_get_cstr(sms->sms.sender));
        bb_alog_sms(conn, sms, "REJECTED - not white-listed SMS");
        msg_destroy(sms);
        return SMSCCONN_FAILED_REJECTED;
    }

    if (white_list_regex && gw_regex_match_pre(white_list_regex, sms->sms.sender) == 0) {
        gw_rwlock_unlock(&white_black_list_lock);
        info(0, "Number <%s> is not in white-list, message discarded",
             octstr_get_cstr(sms->sms.sender));
        bb_alog_sms(conn, sms, "REJECTED - not white-regex-listed SMS");
        msg_destroy(sms);
        return SMSCCONN_FAILED_REJECTED;
    }
    
    if (black_list && numhash_find_number(black_list, sms->sms.sender) == 1) {
        gw_rwlock_unlock(&white_black_list_lock);
        info(0, "Number <%s> is in black-list, message discarded",
             octstr_get_cstr(sms->sms.sender));
        bb_alog_sms(conn, sms, "REJECTED - black-listed SMS");
        msg_destroy(sms);
        return SMSCCONN_FAILED_REJECTED;
    }

    if (black_list_regex && gw_regex_match_pre(black_list_regex, sms->sms.sender) == 0) {
        gw_rwlock_unlock(&white_black_list_lock);
        info(0, "Number <%s> is not in black-list, message discarded",
             octstr_get_cstr(sms->sms.sender));
        bb_alog_sms(conn, sms, "REJECTED - black-regex-listed SMS");
        msg_destroy(sms);
        return SMSCCONN_FAILED_REJECTED;
    }
    gw_rwlock_unlock(&white_black_list_lock);

    /* fix sms type if not set already */
    if (sms->sms.sms_type != report_mo)
        sms->sms.sms_type = mo;

    /* write to store (if enabled) */
    if (store_save(sms) == -1) {
        msg_destroy(sms);
        return SMSCCONN_FAILED_TEMPORARILY;
    }

    copy = msg_duplicate(sms);

    /*
     * Try to reroute internally to an smsc-id without leaving
     * actually bearerbox scope.
     * Scope: internal routing (to smsc-ids)
     */
    if ((rc = route_incoming_to_smsc(conn, copy)) == -1) {
        int ret;
        /* Before routing to some box, do concat handling
         * and replace copy as such.
         */
        if (handle_concatenated_mo && copy->sms.sms_type == mo) {
            ret = check_concatenation(&copy, (conn ? conn->id : NULL));
            switch(ret) {
            case concat_pending:
                counter_increase(incoming_sms_counter); /* ?? */
                load_increase(incoming_sms_load);
                if (conn != NULL) {
                    counter_increase(conn->received);
                    load_increase(conn->incoming_sms_load);
                }
                msg_destroy(sms);
                return SMSCCONN_SUCCESS;
            case concat_complete:
                /* Combined sms received! save new one since it is now combined. */ 
                msg_destroy(sms);
                /* Change the sms. */
                sms = msg_duplicate(copy);
                break;
            case concat_error:
                /* failed to save, go away. */
                msg_destroy(sms);
                return SMSCCONN_FAILED_TEMPORARILY;
            case concat_none:
                break;
            default:
                panic(0, "Internal error: Unhandled concat result.");
                break;
            }
        }
        /*
         * Now try to route the message to a specific smsbox
         * connection based on the existing msg->sms.boxc_id or
         * the registered receiver numbers for specific smsbox'es.
         * Scope: external routing (to smsbox connections)
         */
        rc = route_incoming_to_boxc(copy);
    }
    
    if (rc == -1 || (rc != SMSCCONN_SUCCESS && rc != SMSCCONN_QUEUED)) {
        warning(0, "incoming messages queue too long, dropping a message");
        if (sms->sms.sms_type == report_mo)
           bb_alog_sms(conn, sms, "DROPPED Received DLR");
        else
           bb_alog_sms(conn, sms, "DROPPED Received SMS");

        /* put nack into store-file */
        store_save_ack(sms, ack_failed);

        msg_destroy(copy);
        msg_destroy(sms);
        gwthread_sleep(0.1); /* letting the queue go down */
        return (rc == -1 ? SMSCCONN_FAILED_QFULL : rc);
    }

    if (sms->sms.sms_type != report_mo) {
        bb_alog_sms(conn, sms, "Receive SMS");
        counter_increase(incoming_sms_counter);
        load_increase(incoming_sms_load);
        if (conn != NULL) {
            counter_increase(conn->received);
            load_increase(conn->incoming_sms_load);
        }
    } else {
        bb_alog_sms(conn, sms, "Receive DLR");
        counter_increase(incoming_dlr_counter);
        load_increase(incoming_dlr_load);
        if (conn != NULL) {
            counter_increase(conn->received_dlr);
            load_increase(conn->incoming_dlr_load);
        }
    }

    msg_destroy(sms);

    return SMSCCONN_SUCCESS;
}

int bb_reload_smsc_groups()
{
    Cfg *cfg;

    debug("bb.sms", 0, "Reloading groups list from disk");
    cfg =  cfg_create(cfg_filename);
    if (cfg_read(cfg) == -1) {
        warning(0, "Error opening configuration file %s", octstr_get_cstr(cfg_filename));
        return -1;
    }
    if (smsc_groups != NULL)
        gwlist_destroy(smsc_groups, NULL);
    smsc_groups = cfg_get_multi_group(cfg, octstr_imm("smsc"));
    return 0;
}


/*---------------------------------------------------------------------
 * Other functions
 */



/* function to route outgoing SMS'es from delay-list
 * use some nice magics to route them to proper SMSC
 */
static void sms_router(void *arg)
{
    Msg *msg, *startmsg, *newmsg;
    long ret;
    time_t concat_mo_check;

    gwlist_add_producer(flow_threads);
    gwthread_wakeup(MAIN_THREAD_ID);

    startmsg = newmsg = NULL;
    ret = SMSCCONN_SUCCESS;
    concat_mo_check = time(NULL);

    while(bb_status != BB_SHUTDOWN && bb_status != BB_DEAD) {

	if (newmsg == startmsg) {
            if (ret == SMSCCONN_QUEUED || ret == SMSCCONN_FAILED_QFULL) {
                /* sleep: sms_resend_frequency / 2 , so we reduce amount of msgs to send */
                double sleep_time = (sms_resend_frequency / 2 > 1 ? sms_resend_frequency / 2 : sms_resend_frequency);
                debug("bb.sms", 0, "sms_router: time to sleep %.2f secs.", sleep_time);
                gwthread_sleep(sleep_time);
                debug("bb.sms", 0, "sms_router: gwlist_len = %ld", gwlist_len(outgoing_sms));
            }
            startmsg = msg = gwlist_timed_consume(outgoing_sms, concatenated_mo_timeout);
            newmsg = NULL;
        } else {
            newmsg = msg = gwlist_timed_consume(outgoing_sms, concatenated_mo_timeout);
        }

        if (difftime(time(NULL), concat_mo_check) > concatenated_mo_timeout) {
            concat_mo_check = time(NULL);
            clear_old_concat_parts();
        }

        /* shutdown or timeout */
        if (msg == NULL) {
            newmsg = startmsg = NULL;
            continue;
        }

        debug("bb.sms", 0, "sms_router: handling message (%p vs %p)",
                  msg, startmsg);

        /* handle delayed msgs */
        if (msg->sms.resend_try > 0 && difftime(time(NULL), msg->sms.resend_time) < sms_resend_frequency &&
            bb_status != BB_SHUTDOWN && bb_status != BB_DEAD) {
            debug("bb.sms", 0, "re-queing SMS not-yet-to-be resent");
            gwlist_produce(outgoing_sms, msg);
            ret = SMSCCONN_QUEUED;
            continue;
        }

        ret = smsc2_rout(msg, 1);
        switch(ret) {
        case SMSCCONN_SUCCESS:
            debug("bb.sms", 0, "Message routed successfully.");
            newmsg = startmsg = NULL;
            break;
        case SMSCCONN_QUEUED:
            debug("bb.sms", 0, "Routing failed, re-queued.");
            break;
        case SMSCCONN_FAILED_DISCARDED:
            msg_destroy(msg);
            newmsg = startmsg = NULL;
            break;
        case SMSCCONN_FAILED_QFULL:
            debug("bb.sms", 0, "Routing failed, re-queuing.");
            gwlist_produce(outgoing_sms, msg);
            break;
        case SMSCCONN_FAILED_EXPIRED:
            debug("bb.sms", 0, "Routing failed, expired.");
            msg_destroy(msg);
            newmsg = startmsg = NULL;
            break;
        default:
            break;
        }
    }
    gwlist_remove_producer(flow_threads);
}




/*-------------------------------------------------------------
 * public functions
 *
 */

int smsc2_start(Cfg *cfg)
{
    CfgGroup *grp;
    SMSCConn *conn;
    Octstr *os;
    int i;

    if (smsc_running) return -1;

    /* create split sms counter */
    split_msg_counter = counter_create();
    
    /* create smsc list and rwlock for it */
    smsc_list = gwlist_create();
    gw_rwlock_init_static(&smsc_list_lock);

    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    unified_prefix = cfg_get(grp, octstr_imm("unified-prefix"));

    gw_rwlock_init_static(&white_black_list_lock);
    white_list = black_list = NULL;
    white_list_url = black_list_url = NULL;
    white_list_url = cfg_get(grp, octstr_imm("white-list"));
    if (white_list_url != NULL) {
        if ((white_list = numhash_create(octstr_get_cstr(white_list_url))) == NULL)
            panic(0, "Could not get white-list at URL <%s>", 
                  octstr_get_cstr(white_list_url));
    }
    if ((os = cfg_get(grp, octstr_imm("white-list-regex"))) != NULL) {
        if ((white_list_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
    }
    
    black_list_url = cfg_get(grp, octstr_imm("black-list"));
    if (black_list_url != NULL) {
        if ((black_list = numhash_create(octstr_get_cstr(black_list_url))) == NULL)
            panic(0, "Could not get black-list at URL <%s>", 
                  octstr_get_cstr(black_list_url));
    }
    if ((os = cfg_get(grp, octstr_imm("black-list-regex"))) != NULL) {
        if ((black_list_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
    }

    if (cfg_get_integer(&sms_resend_frequency, grp,
            octstr_imm("sms-resend-freq")) == -1 || sms_resend_frequency <= 0) {
        sms_resend_frequency = 60;
    }
    info(0, "Set SMS resend frequency to %ld seconds.", sms_resend_frequency);
            
    if (cfg_get_integer(&sms_resend_retry, grp, octstr_imm("sms-resend-retry")) == -1) {
        sms_resend_retry = -1;
        info(0, "SMS resend retry set to unlimited.");
    }
    else
        info(0, "SMS resend retry set to %ld.", sms_resend_retry);

    if (cfg_get_bool(&handle_concatenated_mo, grp, octstr_imm("sms-combine-concatenated-mo")) == -1)
        handle_concatenated_mo = 1; /* default is TRUE. */

    if (cfg_get_integer(&concatenated_mo_timeout, grp, octstr_imm("sms-combine-concatenated-mo-timeout")) == -1)
        concatenated_mo_timeout = 1800;

    if (handle_concatenated_mo)
        init_concat_handler();

    /* initialize low level PDUs */
    if (smpp_pdu_init(cfg) == -1)
        panic(0, "Connot start with PDU init failed.");

    smsc_groups = cfg_get_multi_group(cfg, octstr_imm("smsc"));
    gwlist_add_producer(smsc_list);
    for (i = 0; i < gwlist_len(smsc_groups) && 
        (grp = gwlist_get(smsc_groups, i)) != NULL; i++) {
        conn = smscconn_create(grp, 1); 
        if (conn == NULL)
            panic(0, "Cannot start with SMSC connection failing");
        gwlist_append(smsc_list, conn);
    }
    gwlist_remove_producer(smsc_list);
    
    if ((router_thread = gwthread_create(sms_router, NULL)) == -1)
	panic(0, "Failed to start a new thread for SMS routing");
    
    gwlist_add_producer(incoming_sms);
    smsc_running = 1;
    return 0;
}

/*
 * Find a matching smsc-id in the smsc list starting at position start.
 * NOTE: Caller must ensure that smsc_list is properly locked!
 */
static long smsc2_find(Octstr *id, long start)
{
    SMSCConn *conn = NULL;
    long i;

    if (start > gwlist_len(smsc_list) || start < 0)
        return -1;

    for (i = start; i < gwlist_len(smsc_list); i++) {
        conn = gwlist_get(smsc_list, i);
        if (conn != NULL && octstr_compare(conn->admin_id, id) == 0) {
            break;
        }
    }
    if (i >= gwlist_len(smsc_list))
        i = -1;
    return i;
}

int smsc2_stop_smsc(Octstr *id)
{
    SMSCConn *conn;
    long i = -1;
    int success = 0;

    if (!smsc_running)
        return -1;

    gw_rwlock_rdlock(&smsc_list_lock);
    /* find the specific smsc via id */
    while((i = smsc2_find(id, ++i)) != -1) {
        conn = gwlist_get(smsc_list, i);
        if (conn != NULL && smscconn_status(conn) == SMSCCONN_DEAD) {
            info(0, "HTTP: Could not shutdown already dead smsc-id `%s'",
                octstr_get_cstr(id));
        } else {
            info(0,"HTTP: Shutting down smsc-id `%s'", octstr_get_cstr(id));
            smscconn_shutdown(conn, 1);   /* shutdown the smsc */
            success = 1;
        }
    }
    gw_rwlock_unlock(&smsc_list_lock);
    if (success == 0) {
        error(0, "SMSC %s not found", octstr_get_cstr(id));
        return -1;
    }
    return 0;
}

int smsc2_restart_smsc(Octstr *id)
{
    CfgGroup *grp;
    SMSCConn *conn, *new_conn;
    Octstr *smscid = NULL;
    long i = -1;
    int hit;
    int num = 0;
    int success = 0;

    if (!smsc_running)
        return -1;

    gw_rwlock_wrlock(&smsc_list_lock);

    if (bb_reload_smsc_groups() != 0) {
        gw_rwlock_unlock(&smsc_list_lock);
        return -1;
    }
    /* find the specific smsc via id */
    while((i = smsc2_find(id, ++i)) != -1) {
        long group_index;
        /* check if smsc has online status already */
        conn = gwlist_get(smsc_list, i);
        if (conn != NULL && smscconn_status(conn) != SMSCCONN_DEAD) {
            warning(0, "HTTP: Could not re-start already running smsc-id `%s'",
                octstr_get_cstr(id));
            continue;
        }
        /* find the group with equal smsc (admin-)id */
        hit = -1;
        grp = NULL;
        for (group_index = 0; group_index < gwlist_len(smsc_groups) && 
             (grp = gwlist_get(smsc_groups, group_index)) != NULL; group_index++) {
            smscid = cfg_get(grp, octstr_imm("smsc-admin-id"));
            if (smscid == NULL)
            smscid = cfg_get(grp, octstr_imm("smsc-id"));
            if (smscid != NULL && octstr_compare(smscid, id) == 0) {
                if (hit < 0)
                    hit = 0;
                if (hit == num)
                    break;
                else
                    hit++;
            }
            octstr_destroy(smscid);
            smscid = NULL;
        }
        octstr_destroy(smscid);
        if (hit != num) {
            /* config group not found */
            error(0, "HTTP: Could not find config for smsc-id `%s'", octstr_get_cstr(id));
            break;
        }
        
        info(0,"HTTP: Re-starting smsc-id `%s'", octstr_get_cstr(id));

        new_conn = smscconn_create(grp, 1);
        if (new_conn == NULL) {
            error(0, "Start of SMSC connection failed, smsc-id `%s'", octstr_get_cstr(id));
            continue; /* keep old connection on the list */
        }
        
        /* drop old connection from the active smsc list */
        gwlist_delete(smsc_list, i, 1);
        /* destroy the connection */
        smscconn_destroy(conn);
        gwlist_insert(smsc_list, i, new_conn);
        smscconn_start(new_conn);
        success = 1;
        num++;
    }

    gw_rwlock_unlock(&smsc_list_lock);
    
    if (success == 0) {
        error(0, "SMSC %s not found", octstr_get_cstr(id));
        return -1;
    }
    /* wake-up the router */
    if (router_thread >= 0)
        gwthread_wakeup(router_thread);
    return 0;
}

int smsc2_remove_smsc(Octstr *id)
{
    SMSCConn *conn;
    long i = -1;
    int success = 0;

    if (!smsc_running)
        return -1;

    gw_rwlock_wrlock(&smsc_list_lock);

    gwlist_add_producer(smsc_list);
    while((i = smsc2_find(id, ++i)) != -1) {
        conn = gwlist_get(smsc_list, i);
        gwlist_delete(smsc_list, i, 1);
        smscconn_shutdown(conn, 0);
        smscconn_destroy(conn);
        success = 1;
    }
    gwlist_remove_producer(smsc_list);

    gw_rwlock_unlock(&smsc_list_lock);
    if (success == 0) {
        error(0, "SMSC %s not found", octstr_get_cstr(id));
        return -1;
    }
    return 0;
}

int smsc2_add_smsc(Octstr *id)
{
    CfgGroup *grp;
    SMSCConn *conn;
    Octstr *smscid = NULL;
    long i;
    int success = 0;

    if (!smsc_running)
        return -1;

    gw_rwlock_wrlock(&smsc_list_lock);
    if (bb_reload_smsc_groups() != 0) {
        gw_rwlock_unlock(&smsc_list_lock);
        return -1;
    }
    
    if (smsc2_find(id, 0) != -1) {
        warning(0, "Could not add already existing SMSC %s", octstr_get_cstr(id));
        gw_rwlock_unlock(&smsc_list_lock);
        return -1;
    }

    gwlist_add_producer(smsc_list);
    grp = NULL;
    for (i = 0; i < gwlist_len(smsc_groups) &&
        (grp = gwlist_get(smsc_groups, i)) != NULL; i++) {
        smscid = cfg_get(grp, octstr_imm("smsc-admin-id"));
        if (smscid == NULL)
            smscid = cfg_get(grp, octstr_imm("smsc-id"));

        if (smscid != NULL && octstr_compare(smscid, id) == 0) {
            conn = smscconn_create(grp, 1);
            if (conn != NULL) {
                gwlist_append(smsc_list, conn);
                if (conn->dead_start) {
                    /* Shutdown connection if it's not configured to connect at start-up time */
                    smscconn_shutdown(conn, 0);
                } else {
                    smscconn_start(conn);
                }
                success = 1;
            }
        }
    }
    gwlist_remove_producer(smsc_list);
    gw_rwlock_unlock(&smsc_list_lock);
    if (success == 0) {
        error(0, "SMSC %s not found", octstr_get_cstr(id));
        return -1;
    }
    return 0;
}

int smsc2_reload_lists(void)
{
    Numhash *tmp;
    int rc = 1;

    if (white_list_url != NULL) {
        tmp = numhash_create(octstr_get_cstr(white_list_url));
        if (white_list == NULL) {
            error(0, "Unable to reload white_list."),
            rc = -1;
        } else {
        	gw_rwlock_wrlock(&white_black_list_lock);
        	numhash_destroy(white_list);
        	white_list = tmp;
        	gw_rwlock_unlock(&white_black_list_lock);
        }
    }

    if (black_list_url != NULL) {
        tmp = numhash_create(octstr_get_cstr(black_list_url));
        if (black_list == NULL) {
            error(0, "Unable to reload black_list");
            rc = -1;
        } else {
        	gw_rwlock_wrlock(&white_black_list_lock);
        	numhash_destroy(black_list);
        	black_list = tmp;
        	gw_rwlock_unlock(&white_black_list_lock);
        }
    }

    return rc;
}

void smsc2_resume(int is_init)
{
    SMSCConn *conn;
    long i;

    if (!smsc_running)
        return;

    gw_rwlock_rdlock(&smsc_list_lock);
    for (i = 0; i < gwlist_len(smsc_list); i++) {
        conn = gwlist_get(smsc_list, i);
        if (!is_init || !conn->dead_start) {
            smscconn_start(conn);
        } else {
            /* Shutdown the connections that are not configured to start at boot */
            smscconn_shutdown(conn, 0);
        }
    }
    gw_rwlock_unlock(&smsc_list_lock);
    
    if (router_thread >= 0)
        gwthread_wakeup(router_thread);
}


void smsc2_suspend(void)
{
    SMSCConn *conn;
    long i;

    if (!smsc_running)
        return;

    gw_rwlock_rdlock(&smsc_list_lock);
    for (i = 0; i < gwlist_len(smsc_list); i++) {
        conn = gwlist_get(smsc_list, i);
        smscconn_stop(conn);
    }
    gw_rwlock_unlock(&smsc_list_lock);
}


int smsc2_shutdown(void)
{
    SMSCConn *conn;
    long i;

    if (!smsc_running)
        return -1;

    /* Call shutdown for all SMSC Connections; they should
     * handle that they quit, by emptying queues and then dying off
     */
    gw_rwlock_rdlock(&smsc_list_lock);
    for(i=0; i < gwlist_len(smsc_list); i++) {
        conn = gwlist_get(smsc_list, i);
	smscconn_shutdown(conn, 1);
    }
    gw_rwlock_unlock(&smsc_list_lock);
    if (router_thread >= 0)
	gwthread_wakeup(router_thread);

    /* start avalanche by calling shutdown */

    /* XXX shouldn'w we be sure that all smsces have closed their
     * receive thingies? Is this guaranteed by setting bb_status
     * to shutdown before calling these?
     */
    gwlist_remove_producer(incoming_sms);

    /* shutdown low levele PDU things */
    smpp_pdu_shutdown();

    return 0;
}


void smsc2_cleanup(void)
{
    SMSCConn *conn;
    long i;

    if (!smsc_running)
        return;

    debug("smscconn", 0, "final clean-up for SMSCConn");
    
    gw_rwlock_wrlock(&smsc_list_lock);
    for (i = 0; i < gwlist_len(smsc_list); i++) {
        conn = gwlist_get(smsc_list, i);
        smscconn_destroy(conn);
    }
    gwlist_destroy(smsc_list, NULL);
    smsc_list = NULL;
    gw_rwlock_unlock(&smsc_list_lock);
    gwlist_destroy(smsc_groups, NULL);
    octstr_destroy(unified_prefix);    
    numhash_destroy(white_list);
    numhash_destroy(black_list);
    octstr_destroy(white_list_url);
    octstr_destroy(black_list_url);
    if (white_list_regex != NULL)
        gw_regex_destroy(white_list_regex);
    if (black_list_regex != NULL)
        gw_regex_destroy(black_list_regex);
    /* destroy msg split counter */
    counter_destroy(split_msg_counter);
    gw_rwlock_destroy(&smsc_list_lock);
    gw_rwlock_destroy(&white_black_list_lock);

    /* Stop concat handling */
    shutdown_concat_handler();

    smsc_running = 0;
}


Octstr *smsc2_status(int status_type)
{
    Octstr *tmp;
    char tmp3[64];
    char *lb;
    long i;
    int para = 0;
    SMSCConn *conn;
    StatusInfo info;
    const Octstr *conn_id = NULL;
    const Octstr *conn_admin_id = NULL;
    const Octstr *conn_name = NULL;
    float incoming_sms_load_0, incoming_sms_load_1, incoming_sms_load_2;
    float outgoing_sms_load_0, outgoing_sms_load_1, outgoing_sms_load_2;
    float incoming_dlr_load_0, incoming_dlr_load_1, incoming_dlr_load_2;
    float outgoing_dlr_load_0, outgoing_dlr_load_1, outgoing_dlr_load_2;

    if ((lb = bb_status_linebreak(status_type)) == NULL)
        return octstr_create("Un-supported format");

    if (status_type == BBSTATUS_HTML || status_type == BBSTATUS_WML)
        para = 1;

    if (!smsc_running) {
        if (status_type == BBSTATUS_XML)
            return octstr_create ("<smscs>\n\t<count>0</count>\n</smscs>");
        else
            return octstr_format("%sNo SMSC connections%s\n\n", para ? "<p>" : "",
                                 para ? "</p>" : "");
    }

    if (status_type != BBSTATUS_XML)
        tmp = octstr_format("%sSMSC connections:%s", para ? "<p>" : "", lb);
    else
        tmp = octstr_format("<smscs><count>%d</count>\n\t", gwlist_len(smsc_list));

    gw_rwlock_rdlock(&smsc_list_lock);
    for (i = 0; i < gwlist_len(smsc_list); i++) {
        incoming_sms_load_0 = incoming_sms_load_1 = incoming_sms_load_2 = 0.0;
        outgoing_sms_load_0 = outgoing_sms_load_1 = outgoing_sms_load_2 = 0.0;
        incoming_dlr_load_0 = incoming_dlr_load_1 = incoming_dlr_load_2 = 0.0;
        outgoing_dlr_load_0 = outgoing_dlr_load_1 = outgoing_dlr_load_2 = 0.0;
        conn = gwlist_get(smsc_list, i);

        if ((smscconn_info(conn, &info) == -1)) {
            /* 
             * we do not delete SMSCs from the list 
             * this way we can show in the status which links are dead
             */
            continue;
        }

        conn_id = conn ? smscconn_id(conn) : octstr_imm("unknown");
        conn_id = conn_id ? conn_id : octstr_imm("unknown");
        conn_admin_id = conn ? smscconn_admin_id(conn) : octstr_imm("unknown");
        conn_admin_id = conn_admin_id ? conn_admin_id : octstr_imm("unknown");
        conn_name = conn ? smscconn_name(conn) : octstr_imm("unknown");

        if (status_type == BBSTATUS_HTML) {
            octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;<b>");
            octstr_append(tmp, conn_id);
            octstr_append_cstr(tmp, "</b>[");
            octstr_append(tmp, conn_admin_id);
            octstr_append_cstr(tmp, "]&nbsp;&nbsp;&nbsp;&nbsp;");
        } else if (status_type == BBSTATUS_TEXT) {
            octstr_append_cstr(tmp, "    ");
            octstr_append(tmp, conn_id);
            octstr_append_cstr(tmp, "[");
            octstr_append(tmp, conn_admin_id);
            octstr_append_cstr(tmp, "]    ");
        } 
        if (status_type == BBSTATUS_XML) {
            octstr_append_cstr(tmp, "<smsc>\n\t\t<name>");
            octstr_append(tmp, conn_name);
            octstr_append_cstr(tmp, "</name>\n\t\t<admin-id>");
            octstr_append(tmp, conn_admin_id);
            octstr_append_cstr(tmp, "</admin-id>\n\t\t<id>");
            octstr_append(tmp, conn_id);
            octstr_append_cstr(tmp, "</id>\n\t\t");
        } else
            octstr_append(tmp, conn_name);

        switch (info.status) {
            case SMSCCONN_ACTIVE:
            case SMSCCONN_ACTIVE_RECV:
                sprintf(tmp3, "online %lds", info.online);
                incoming_sms_load_0 = load_get(conn->incoming_sms_load,0);
                incoming_sms_load_1 = load_get(conn->incoming_sms_load,1);
                incoming_sms_load_2 = load_get(conn->incoming_sms_load,2);
                outgoing_sms_load_0 = load_get(conn->outgoing_sms_load,0);
                outgoing_sms_load_1 = load_get(conn->outgoing_sms_load,1);
                outgoing_sms_load_2 = load_get(conn->outgoing_sms_load,2);
                incoming_dlr_load_0 = load_get(conn->incoming_dlr_load,0);
                incoming_dlr_load_1 = load_get(conn->incoming_dlr_load,1);
                incoming_dlr_load_2 = load_get(conn->incoming_dlr_load,2);
                outgoing_dlr_load_0 = load_get(conn->outgoing_dlr_load,0);
                outgoing_dlr_load_1 = load_get(conn->outgoing_dlr_load,1);
                outgoing_dlr_load_2 = load_get(conn->outgoing_dlr_load,2);
                break;
            case SMSCCONN_DISCONNECTED:
                sprintf(tmp3, "disconnected");
                break;
            case SMSCCONN_CONNECTING:
                sprintf(tmp3, "connecting");
                break;
            case SMSCCONN_RECONNECTING:
                sprintf(tmp3, "re-connecting");
                break;
            case SMSCCONN_DEAD:
                sprintf(tmp3, "dead");
                break;
            default:
                sprintf(tmp3, "unknown");
        }

        if (status_type == BBSTATUS_XML)
            octstr_format_append(tmp, "<status>%s</status>\n"
                "\t\t<failed>%ld</failed>\n"
                "\t\t<queued>%ld</queued>\n"
                "\t\t<sms>\n"
                "\t\t\t<received>%ld</received>\n"
                "\t\t\t<sent>%ld</sent>\n"
                "\t\t\t<inbound>%.2f,%.2f,%.2f</inbound>\n"
                "\t\t\t<outbound>%.2f,%.2f,%.2f</outbound>\n"
                "\t\t</sms>\n\t\t<dlr>\n"
                "\t\t\t<received>%ld</received>\n"
                "\t\t\t<sent>%ld</sent>\n"
                "\t\t\t<inbound>%.2f,%.2f,%.2f</inbound>\n"
                "\t\t\t<outbound>%.2f,%.2f,%.2f</outbound>\n"
                "\t\t</dlr>\n"
                "\t</smsc>\n", tmp3,
                info.failed, info.queued, info.received, info.sent,
                incoming_sms_load_0, incoming_sms_load_1, incoming_sms_load_2,
                outgoing_sms_load_0, outgoing_sms_load_1, outgoing_sms_load_2,
                info.received_dlr, info.sent_dlr,
                incoming_dlr_load_0, incoming_dlr_load_1, incoming_dlr_load_2,
                outgoing_dlr_load_0, outgoing_dlr_load_1, outgoing_dlr_load_2);
        else
            octstr_format_append(tmp, " (%s, rcvd: sms %ld (%.2f,%.2f,%.2f) / dlr %ld (%.2f,%.2f,%.2f), "
                "sent: sms %ld (%.2f,%.2f,%.2f) / dlr %ld (%.2f,%.2f,%.2f), failed %ld, "
                "queued %ld msgs)%s",
                tmp3,
                info.received,
                incoming_sms_load_0, incoming_sms_load_1, incoming_sms_load_2,
                info.received_dlr,
                incoming_dlr_load_0, incoming_dlr_load_1, incoming_dlr_load_2,
                info.sent,
                outgoing_sms_load_0, outgoing_sms_load_1, outgoing_sms_load_2,
                info.sent_dlr,
                outgoing_dlr_load_0, outgoing_dlr_load_1, outgoing_dlr_load_2,
                info.failed,
                info.queued,
                lb);
    }




    gw_rwlock_unlock(&smsc_list_lock);

    if (para)
        octstr_append_cstr(tmp, "</p>");
    if (status_type == BBSTATUS_XML)
        octstr_append_cstr(tmp, "</smscs>\n");
    else
        octstr_append_cstr(tmp, "\n\n");
    return tmp;
}


/* function to route outgoing SMS'es
 *
 * If finds a good one, puts into it and returns SMSCCONN_SUCCESS
 * If finds only bad ones, but acceptable, queues and
 * returns SMSCCONN_QUEUED  (like all acceptable currently disconnected)
 * if message acceptable but queues full returns SMSCCONN_FAILED_QFULL and
 * message is not destroyed.
 * If cannot find nothing at all, returns SMSCCONN_FAILED_DISCARDED and
 * message is NOT destroyed (otherwise it is)
 */
long smsc2_rout(Msg *msg, int resend)
{
    StatusInfo info;
    SMSCConn *conn, *best_preferred, *best_ok;
    long bp_load, bo_load;
    int i, s, ret, bad_found, full_found;
    long max_queue, queue_length;
    char *uf;

    /* XXX handle ack here? */
    if (msg_type(msg) != sms) {
        error(0, "Attempt to route non SMS message through smsc2_rout!");
        return SMSCCONN_FAILED_DISCARDED;
    }

    /* check if validity period has expired */
    if (msg->sms.validity != SMS_PARAM_UNDEFINED && time(NULL) > msg->sms.validity) {
        bb_smscconn_send_failed(NULL, msg_duplicate(msg), SMSCCONN_FAILED_EXPIRED, octstr_create("validity expired"));
        return SMSCCONN_FAILED_EXPIRED;
    }

    /* unify prefix of receiver, in case of it has not been
     * already done */

    uf = unified_prefix ? octstr_get_cstr(unified_prefix) : NULL;
    normalize_number(uf, &(msg->sms.receiver));

    /* select in which list to add this
     * start - from random SMSCConn, as they are all 'equal'
     */
    gw_rwlock_rdlock(&smsc_list_lock);
    if (gwlist_len(smsc_list) == 0) {
        warning(0, "No SMSCes to receive message");
        gw_rwlock_unlock(&smsc_list_lock);
        return SMSCCONN_FAILED_DISCARDED;
    }

    best_preferred = best_ok = NULL;
    bad_found = full_found = 0;
    bp_load = bo_load = queue_length = 0;

    if (msg->sms.split_parts == NULL) {
    	/*
    	 * if global queue not empty then 20% reserved for old msgs
    	 * and 80% for new msgs. So we can guarantee that old msgs find
    	 * place in the SMSC's queue.
    	 */
    	if (gwlist_len(outgoing_sms) > 0) {
    		max_queue = (resend ? max_outgoing_sms_qlength :
    		max_outgoing_sms_qlength * 0.8);
    	} else
    		max_queue = max_outgoing_sms_qlength;

    	s = gw_rand() % gwlist_len(smsc_list);

    	conn = NULL;
    	for (i = 0; i < gwlist_len(smsc_list); i++) {
    		conn = gwlist_get(smsc_list,  (i+s) % gwlist_len(smsc_list));

    		smscconn_info(conn, &info);
    		queue_length += (info.queued > 0 ? info.queued : 0);

    		ret = smscconn_usable(conn,msg);
    		if (ret == -1)
    			continue;

    		/* if we already have a preferred one, skip non-preferred */
    		if (ret != 1 && best_preferred)
    			continue;

    		/* If connection is not currently answering ... */
    		if (info.status != SMSCCONN_ACTIVE) {
    			bad_found = 1;
    			continue;
    		}
    		/* check queue length */
    		if (info.queued > max_queue) {
    			full_found = 1;
    			continue;
    		}
    		if (ret == 1) {          /* preferred */
    			if (best_preferred == NULL || info.load < bp_load) {
    				best_preferred = conn;
    				bp_load = info.load;
    				continue;
    			}
    		}
    		if (best_ok == NULL || info.load < bo_load) {
    			best_ok = conn;
    			bo_load = info.load;
    		}
    	}
    	queue_length += gwlist_len(outgoing_sms);
    	if (max_outgoing_sms_qlength > 0 && !resend &&
    	    queue_length > gwlist_len(smsc_list) * max_outgoing_sms_qlength) {
    		gw_rwlock_unlock(&smsc_list_lock);
    		debug("bb.sms", 0, "sum(#queues) limit");
    		return SMSCCONN_FAILED_QFULL;
    	}
    } else {
        struct split_parts *parts = msg->sms.split_parts;
        /* check whether this SMSCConn still on the list */
        if (gwlist_search_equal(smsc_list, parts->smsc_conn) != -1)
            best_preferred = parts->smsc_conn;
    }

    if (best_preferred)
        ret = smscconn_send(best_preferred, msg);
    else if (best_ok)
        ret = smscconn_send(best_ok, msg);
    else if (bad_found) {
        gw_rwlock_unlock(&smsc_list_lock);
        if (max_outgoing_sms_qlength < 0 || gwlist_len(outgoing_sms) < max_outgoing_sms_qlength) {
            gwlist_produce(outgoing_sms, msg);
            return SMSCCONN_QUEUED;
        }
        debug("bb.sms", 0, "bad_found queue full");
        return SMSCCONN_FAILED_QFULL; /* queue full */
    } else if (full_found) {
        gw_rwlock_unlock(&smsc_list_lock);
        debug("bb.sms", 0, "full_found queue full");
        return SMSCCONN_FAILED_QFULL;
    } else {
        gw_rwlock_unlock(&smsc_list_lock);
        if (bb_status == BB_SHUTDOWN) {
            msg_destroy(msg);
            return SMSCCONN_QUEUED;
        }
        warning(0, "Cannot find SMSCConn for message to <%s>, rejected.",
                    octstr_get_cstr(msg->sms.receiver));
        bb_smscconn_send_failed(NULL, msg_duplicate(msg), SMSCCONN_FAILED_DISCARDED, octstr_create("no SMSC"));
        return SMSCCONN_FAILED_DISCARDED;
    }

    gw_rwlock_unlock(&smsc_list_lock);
    /* check the status of sending operation */
    if (ret == -1)
        return smsc2_rout(msg, resend); /* re-try */

    msg_destroy(msg);
    return SMSCCONN_SUCCESS;
}


/*
 * Try to reroute to another smsc.
 * @return -1 if no rerouting info available; otherwise return code from smsc2_route.
 */
static long route_incoming_to_smsc(SMSCConn *conn, Msg *msg)
{
    Octstr *smsc;
    
    /* sanity check */
    if (!conn || !msg)
        return -1;
        
    /* check for dlr rerouting */
    if (!conn->reroute_dlr && (msg->sms.sms_type == report_mo || msg->sms.sms_type == report_mt))
        return -1;

    /*
     * Check if we have any "reroute" rules to obey. Which means msg gets
     * transported internally from MO to MT msg.
     */
    if (conn->reroute) {
        /* change message direction */
        store_save_ack(msg, ack_success);
        msg->sms.sms_type = mt_push;
        store_save(msg);
        /* drop into outbound queue again for routing */
        return smsc2_rout(msg, 0);
    }
    
    if (conn->reroute_to_smsc) {
        /* change message direction */
        store_save_ack(msg, ack_success);
        msg->sms.sms_type = mt_push;
        store_save(msg);
        /* apply directly to the given smsc-id for MT traffic */
        octstr_destroy(msg->sms.smsc_id);
        msg->sms.smsc_id = octstr_duplicate(conn->reroute_to_smsc);
        return smsc2_rout(msg, 0);
    }
    
    if (conn->reroute_by_receiver && msg->sms.receiver &&
                 (smsc = dict_get(conn->reroute_by_receiver, msg->sms.receiver))) {
        /* change message direction */
        store_save_ack(msg, ack_success);
        msg->sms.sms_type = mt_push;
        store_save(msg);
        /* route by receiver number */
        /* XXX implement wildcard matching too! */
        octstr_destroy(msg->sms.smsc_id);
        msg->sms.smsc_id = octstr_duplicate(smsc);
        return smsc2_rout(msg, 0);
    }

    return -1; 
}


/*--------------------------------
 * incoming concatenated messages handling
 */

typedef struct ConcatMsg {
    int refnum;
    int total_parts;
    int num_parts;
    Octstr *udh; /* normalized UDH */
    time_t trecv;
    Octstr *key; /* in dict. */
    int ack;     /* set to the type of ack to send when deleting. */
    /* array of parts */
    Msg **parts;
} ConcatMsg;

static Dict *incoming_concat_msgs;
static Mutex *concat_lock;

static void destroy_concatMsg(void *x)
{
    int i;
    ConcatMsg *msg = x;

    gw_assert(msg);
    for (i = 0; i < msg->total_parts; i++) {
        if (msg->parts[i]) {
            store_save_ack(msg->parts[i], msg->ack);
            msg_destroy(msg->parts[i]);
        }
    }
    gw_free(msg->parts);
    octstr_destroy(msg->key);
    octstr_destroy(msg->udh);
    gw_free(msg);
}

static void init_concat_handler(void)
{
    if (incoming_concat_msgs != NULL) /* already initialised? */
        return;
    incoming_concat_msgs = dict_create(max_incoming_sms_qlength > 0 ? max_incoming_sms_qlength : 1024, 
                                       destroy_concatMsg);
    concat_lock = mutex_create();
    debug("bb.sms",0,"MO concatenated message handling enabled");
}

static void shutdown_concat_handler(void)
{
    if (incoming_concat_msgs == NULL)
        return;
    dict_destroy(incoming_concat_msgs);
    mutex_destroy(concat_lock);

    incoming_concat_msgs = NULL;
    concat_lock = NULL;
    debug("bb.sms",0,"MO concatenated message handling cleaned up");
}

static void clear_old_concat_parts(void)
{
    List *keys;
    Octstr *key;

    /* not initialised, go away */
    if (incoming_concat_msgs == NULL)
        return;

    debug("bb.sms.splits", 0, "clear_old_concat_parts called");

    /* Remove any pending messages that are too old. */
    keys = dict_keys(incoming_concat_msgs);
    while((key = gwlist_extract_first(keys)) != NULL) {
        ConcatMsg *x;
        Msg *msg;
        int i, destroy = 1;

        mutex_lock(concat_lock);
        x = dict_get(incoming_concat_msgs, key);
        octstr_destroy(key);
        if (x == NULL || difftime(time(NULL), x->trecv) < concatenated_mo_timeout) {
            mutex_unlock(concat_lock);
            continue;
        }
        dict_remove(incoming_concat_msgs, x->key);
        mutex_unlock(concat_lock);
        warning(0, "Time-out waiting for concatenated message '%s'. Send message parts as is.",
                octstr_get_cstr(x->key));
        for (i = 0; i < x->total_parts && destroy == 1; i++) {
            if (x->parts[i] == NULL)
                continue;
            msg = msg_duplicate(x->parts[i]);
            store_save_ack(x->parts[i], ack_success);
            switch(bb_smscconn_receive(NULL, msg)) {
            case SMSCCONN_FAILED_REJECTED:
            case SMSCCONN_SUCCESS:
                msg_destroy(x->parts[i]);
                x->parts[i] = NULL;
                x->num_parts--;
                break;
            case SMSCCONN_FAILED_TEMPORARILY:
            case SMSCCONN_FAILED_QFULL:
            default:
                /* oops put it back into dict and retry on next run */
                store_save(x->parts[i]);
                destroy = 0;
                break;
            }
        }
        if (destroy) {
            destroy_concatMsg(x);
        } else {
            ConcatMsg *x1;
            mutex_lock(concat_lock);
            x1 = dict_get(incoming_concat_msgs, x->key);
            if (x1 != NULL) { /* oops we have new part */
                int i;
                if (x->total_parts != x1->total_parts) {
                    /* broken handset, don't know what todo here??
                     * for now just put old concatMsg into dict with
                     * another key and it will be cleaned up on next run.
                     */
                    octstr_format_append(x->key, " %d", x->total_parts);
                    dict_put(incoming_concat_msgs, x->key, x);
                } else {
                    for (i = 0; i < x->total_parts; i++) {
                        if (x->parts[i] == NULL)
                            continue;
                        if (x1->parts[i] == NULL) {
                            x1->parts[i] = x->parts[i];
                            x->parts[i] = NULL;
                        }
                    }
                    destroy_concatMsg(x);
                }
            } else {
                dict_put(incoming_concat_msgs, x->key, x);
            }
            mutex_unlock(concat_lock);
        }
    }
    gwlist_destroy(keys, octstr_destroy_item);
}

/* Checks if message is concatenated. Returns:
 * - returns concat_complete if no concat parts, or message complete
 * - returns concat_pending (and sets *pmsg to NULL) if parts pending
 * - returns concat_error if store_save fails
 */
static int check_concatenation(Msg **pmsg, Octstr *smscid)
{
    Msg *msg = *pmsg;
    int l, iel = 0, refnum, pos, c, part, totalparts, i, sixteenbit;
    Octstr *udh = msg->sms.udhdata, *key;
    ConcatMsg *cmsg;
    int ret = concat_complete;

    /* ... module not initialised or there is no UDH or smscid is NULL. */
    if (incoming_concat_msgs == NULL || (l = octstr_len(udh)) == 0 || smscid == NULL)
        return concat_none;

    for (pos = 1, c = -1; pos < l - 1; pos += iel + 2) {
        iel = octstr_get_char(udh, pos + 1);
        if ((c = octstr_get_char(udh, pos)) == 0 || c == 8)
            break;
    }
    if (pos >= l)  /* no concat UDH found. */
        return concat_none;

    /* c = 0 means 8 bit, c = 8 means 16 bit concat info */
    sixteenbit = (c == 8);
    refnum = (!sixteenbit) ? octstr_get_char(udh, pos + 2) :
    		(octstr_get_char(udh, pos + 2) << 8) | octstr_get_char(udh, pos + 3);
    totalparts = octstr_get_char(udh, pos + 3 + sixteenbit);
    part = octstr_get_char(udh, pos + 4 + sixteenbit);

    if (part < 1 || part > totalparts) {
        warning(0, "Invalid concatenation UDH [ref = %d] in message from %s!",
                refnum, octstr_get_cstr(msg->sms.sender));
        return concat_none;
    }

    /* extract UDH */
    udh = octstr_duplicate(msg->sms.udhdata);
    octstr_delete(udh, pos, iel + 2);
    if (octstr_len(udh) <= 1) /* no other UDH elements. */
    	octstr_delete(udh, 0, octstr_len(udh));
    else
    	octstr_set_char(udh, 0, octstr_len(udh) - 1);

    debug("bb.sms.splits", 0, "Got part %d [ref %d, total parts %d] of message from %s. Dump follows:",
          part, refnum, totalparts, octstr_get_cstr(msg->sms.sender));
     
    msg_dump(msg, 0);
     
    key = octstr_format("'%S' '%S' '%S' '%d' '%d' '%H'", msg->sms.sender, msg->sms.receiver, smscid, refnum, totalparts, udh);
    mutex_lock(concat_lock);
    if ((cmsg = dict_get(incoming_concat_msgs, key)) == NULL) {
        cmsg = gw_malloc(sizeof(*cmsg));
        cmsg->refnum = refnum;
        cmsg->total_parts = totalparts;
        cmsg->udh = udh;
        udh = NULL;
        cmsg->num_parts = 0;
        cmsg->key = octstr_duplicate(key);
        cmsg->ack = ack_success;
        cmsg->parts = gw_malloc(totalparts * sizeof(*cmsg->parts));
        memset(cmsg->parts, 0, cmsg->total_parts * sizeof(*cmsg->parts)); /* clear it. */

        dict_put(incoming_concat_msgs, key, cmsg);
    }
    octstr_destroy(key);
    octstr_destroy(udh);

    /* check if we have seen message part before... */
    if (cmsg->parts[part - 1] != NULL) {	  
        warning(0, "Duplicate message part %d, ref %d, from %s, to %s. Discarded!",
                part, refnum, octstr_get_cstr(msg->sms.sender), octstr_get_cstr(msg->sms.receiver));
        store_save_ack(msg, ack_success);
        msg_destroy(msg); 
        *pmsg = msg = NULL;
    } else {
        cmsg->parts[part -1] = msg;
        cmsg->num_parts++;
        /* always update receive time so we have it from last part and don't timeout */
        cmsg->trecv = time(NULL);
    }

    if (cmsg->num_parts < cmsg->total_parts) {  /* wait for more parts. */
        *pmsg = msg = NULL;
        mutex_unlock(concat_lock);
        return concat_pending;
    }

    /* we have all the parts: Put them together, mod UDH, return message. */
    msg = msg_duplicate(cmsg->parts[0]);
    uuid_generate(msg->sms.id); /* give it a new ID. */

    debug("bb.sms.splits",0,"Received all concatenated message parts from %s, to %s, refnum %d",
          octstr_get_cstr(msg->sms.sender), octstr_get_cstr(msg->sms.receiver), refnum);

    for (i = 1; i < cmsg->total_parts; i++)
        octstr_append(msg->sms.msgdata, cmsg->parts[i]->sms.msgdata);

    /* Attempt to save the new one, if that fails, then reply with fail. */
    if (store_save(msg) == -1) {	  
        mutex_unlock(concat_lock);
        msg_destroy(msg);
        *pmsg = msg = NULL;
        return concat_error;
    } else 
        *pmsg = msg; /* return the message part. */

    /* fix up UDH */
    octstr_destroy(msg->sms.udhdata);
    msg->sms.udhdata = cmsg->udh;
    cmsg->udh = NULL;

    /* Delete it from the queue and from the Dict. */
    /* Note: dict_put with NULL value delete and destroy value */
    dict_put(incoming_concat_msgs, cmsg->key, NULL);
    mutex_unlock(concat_lock);

    debug("bb.sms.splits", 0, "Got full message [ref %d] of message from %s to %s. Dumping: ",
          refnum, octstr_get_cstr(msg->sms.sender), octstr_get_cstr(msg->sms.receiver));
    msg_dump(msg,0);

    return ret;
}

