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
 * SMSC Connection
 *
 * Interface for main bearerbox to SMS center connection modules
 *
 * Kalle Marjola 2000 for project Kannel
 */

#include <signal.h>
#include <time.h>

#include "gwlib/gwlib.h"
#include "gwlib/regex.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "sms.h"

extern Counter *split_msg_counter;

/*
 * Some defaults
 */
#define SMSCCONN_RECONNECT_DELAY     10.0


/*
 * Add reroute information to the connection data. Where the priority
 * is in the order: reroute, reroute-smsc-id, reroute-receiver.
 */
static void init_reroute(SMSCConn *conn, CfgGroup *grp)
{
    Octstr *rule;
    long i;

    if (cfg_get_bool(&conn->reroute_dlr, grp, octstr_imm("reroute-dlr")) == -1)
        conn->reroute_dlr = 0;
    info(0, "DLR rerouting for smsc id <%s> %s.", octstr_get_cstr(conn->id), (conn->reroute_dlr?"enabled":"disabled"));

    if (cfg_get_bool(&conn->reroute, grp, octstr_imm("reroute")) != -1) {
        debug("smscconn",0,"Adding general internal routing for smsc id <%s>",
              octstr_get_cstr(conn->id));
        return;
    }

    if ((conn->reroute_to_smsc = cfg_get(grp, octstr_imm("reroute-smsc-id"))) != NULL) {
         /* reroute all messages to a specific smsc-id */
         debug("smscconn",0,"Adding internal routing: smsc id <%s> to smsc id <%s>",
               octstr_get_cstr(conn->id), octstr_get_cstr(conn->reroute_to_smsc));
        return;
    }

    if ((rule = cfg_get(grp, octstr_imm("reroute-receiver"))) != NULL) {
        List *routes;

        /* create hash disctionary for this smsc-id */
        conn->reroute_by_receiver = dict_create(100, (void(*)(void *)) octstr_destroy);

        routes = octstr_split(rule, octstr_imm(";"));
        for (i = 0; i < gwlist_len(routes); i++) {
            Octstr *item = gwlist_get(routes, i);
            Octstr *smsc, *receiver;
            List *receivers;

            /* first word is the smsc-id, all other are the receivers */
            receivers = octstr_split(item, octstr_imm(","));
            smsc = gwlist_extract_first(receivers);
            if (smsc)
                octstr_strip_blanks(smsc);

            while((receiver = gwlist_extract_first(receivers))) {
                octstr_strip_blanks(receiver);
                debug("smscconn",0,"Adding internal routing for smsc id <%s>: "
                          "receiver <%s> to smsc id <%s>",
                          octstr_get_cstr(conn->id), octstr_get_cstr(receiver),
                          octstr_get_cstr(smsc));
                if (!dict_put_once(conn->reroute_by_receiver, receiver, octstr_duplicate(smsc)))
                    panic(0, "Could not set internal routing for smsc id <%s>: "
                              "receiver <%s> to smsc id <%s>, because receiver has already routing entry!",
                              octstr_get_cstr(conn->id), octstr_get_cstr(receiver),
                              octstr_get_cstr(smsc));
                octstr_destroy(receiver);
            }
            octstr_destroy(smsc);
            gwlist_destroy(receivers, octstr_destroy_item);
        }
        octstr_destroy(rule);
        gwlist_destroy(routes, octstr_destroy_item);
    }
}


SMSCConn *smscconn_create(CfgGroup *grp, int start_as_stopped)
{
    SMSCConn *conn;
    Octstr *smsc_type;
    int ret;
    Octstr *allowed_smsc_id_regex;
    Octstr *denied_smsc_id_regex;
    Octstr *allowed_prefix_regex;
    Octstr *denied_prefix_regex;
    Octstr *preferred_prefix_regex;
    Octstr *tmp;

    if (grp == NULL)
	return NULL;

    conn = gw_malloc(sizeof(*conn));
    memset(conn, 0, sizeof(*conn));

    conn->why_killed = SMSCCONN_ALIVE;
    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = -1;
    conn->is_stopped = start_as_stopped;

    conn->received = counter_create();
    conn->received_dlr = counter_create();
    conn->sent = counter_create();
    conn->sent_dlr = counter_create();
    conn->failed = counter_create();
    conn->flow_mutex = mutex_create();

    conn->outgoing_sms_load = load_create();
    /* add 60,300,-1 entries */
    load_add_interval(conn->outgoing_sms_load, 60);
    load_add_interval(conn->outgoing_sms_load, 300);
    load_add_interval(conn->outgoing_sms_load, -1);

    conn->incoming_sms_load = load_create();
    /* add 60,300,-1 entries */
    load_add_interval(conn->incoming_sms_load, 60);
    load_add_interval(conn->incoming_sms_load, 300);
    load_add_interval(conn->incoming_sms_load, -1);

    conn->incoming_dlr_load = load_create();
    /* add 60,300,-1 entries to dlr */
    load_add_interval(conn->incoming_dlr_load, 60);
    load_add_interval(conn->incoming_dlr_load, 300);
    load_add_interval(conn->incoming_dlr_load, -1);

    conn->outgoing_dlr_load = load_create();
    /* add 60,300,-1 entries to dlr */
    load_add_interval(conn->outgoing_dlr_load, 60);
    load_add_interval(conn->outgoing_dlr_load, 300);
    load_add_interval(conn->outgoing_dlr_load, -1);


#define GET_OPTIONAL_VAL(x, n) x = cfg_get(grp, octstr_imm(n))
#define SPLIT_OPTIONAL_VAL(x, n) \
        do { \
                Octstr *tmp = cfg_get(grp, octstr_imm(n)); \
                if (tmp) x = octstr_split(tmp, octstr_imm(";")); \
                else x = NULL; \
                octstr_destroy(tmp); \
        }while(0)

    GET_OPTIONAL_VAL(conn->id, "smsc-id");
    SPLIT_OPTIONAL_VAL(conn->allowed_smsc_id, "allowed-smsc-id");
    SPLIT_OPTIONAL_VAL(conn->denied_smsc_id, "denied-smsc-id");
    SPLIT_OPTIONAL_VAL(conn->preferred_smsc_id, "preferred-smsc-id");
    GET_OPTIONAL_VAL(conn->allowed_prefix, "allowed-prefix");
    GET_OPTIONAL_VAL(conn->denied_prefix, "denied-prefix");
    GET_OPTIONAL_VAL(conn->preferred_prefix, "preferred-prefix");
    GET_OPTIONAL_VAL(conn->unified_prefix, "unified-prefix");
    GET_OPTIONAL_VAL(conn->our_host, "our-host");
    GET_OPTIONAL_VAL(conn->log_file, "log-file");
    cfg_get_bool(&conn->alt_dcs, grp, octstr_imm("alt-dcs"));

    GET_OPTIONAL_VAL(allowed_smsc_id_regex, "allowed-smsc-id-regex");
    if (allowed_smsc_id_regex != NULL) 
        if ((conn->allowed_smsc_id_regex = gw_regex_comp(allowed_smsc_id_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(allowed_smsc_id_regex));
    GET_OPTIONAL_VAL(denied_smsc_id_regex, "denied-smsc-id-regex");
    if (denied_smsc_id_regex != NULL) 
        if ((conn->denied_smsc_id_regex = gw_regex_comp(denied_smsc_id_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(denied_smsc_id_regex));
    GET_OPTIONAL_VAL(allowed_prefix_regex, "allowed-prefix-regex");
    if (allowed_prefix_regex != NULL) 
        if ((conn->allowed_prefix_regex = gw_regex_comp(allowed_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(allowed_prefix_regex));
    GET_OPTIONAL_VAL(denied_prefix_regex, "denied-prefix-regex");
    if (denied_prefix_regex != NULL) 
        if ((conn->denied_prefix_regex = gw_regex_comp(denied_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(denied_prefix_regex));
    GET_OPTIONAL_VAL(preferred_prefix_regex, "preferred-prefix-regex");
    if (preferred_prefix_regex != NULL) 
        if ((conn->preferred_prefix_regex = gw_regex_comp(preferred_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(preferred_prefix_regex));

    if ((tmp = cfg_get(grp, octstr_imm("throughput"))) != NULL) {
        if (octstr_parse_double(&conn->throughput, tmp, 0) == -1)
            conn->throughput = 0;
        octstr_destroy(tmp);
        info(0, "Set throughput to %.3f for smsc id <%s>", conn->throughput, octstr_get_cstr(conn->id));
    }
    /* Sets the admin_id. Equals to connection id if empty */
    GET_OPTIONAL_VAL(conn->admin_id, "smsc-admin-id");
    if (conn->admin_id == NULL)
        conn->admin_id = octstr_duplicate(conn->id);

    /* configure the internal rerouting rules for this smsc id */
    init_reroute(conn, grp);

    if (cfg_get_integer(&conn->log_level, grp, octstr_imm("log-level")) == -1)
        conn->log_level = 0;

    if (cfg_get_integer(&conn->max_sms_octets, grp, octstr_imm("max-sms-octets")) == -1)
        conn->max_sms_octets = MAX_SMS_OCTETS;

    if (cfg_get_bool(&conn->dead_start, grp, octstr_imm("dead-start")) == -1)
        conn->dead_start = 0;	/* default to connect at start-up time */

    /* open a smsc-id specific log-file in exlusive mode */
    if (conn->log_file)
        conn->log_idx = log_open(octstr_get_cstr(conn->log_file), 
                                 conn->log_level, GW_EXCL); 
#undef GET_OPTIONAL_VAL
#undef SPLIT_OPTIONAL_VAL

    if (conn->allowed_smsc_id && conn->denied_smsc_id)
	warning(0, "Both 'allowed-smsc-id' and 'denied-smsc-id' set, deny-list "
		"automatically ignored");
    if (conn->allowed_smsc_id_regex && conn->denied_smsc_id_regex)
        warning(0, "Both 'allowed-smsc-id_regex' and 'denied-smsc-id_regex' set, deny-regex "
                "automatically ignored");

    if (cfg_get_integer(&conn->reconnect_delay, grp,
                        octstr_imm("reconnect-delay")) == -1)
        conn->reconnect_delay = SMSCCONN_RECONNECT_DELAY;

    smsc_type = cfg_get(grp, octstr_imm("smsc"));
    if (smsc_type == NULL) {
        error(0, "Required field 'smsc' missing for smsc group.");
        smscconn_destroy(conn);
        octstr_destroy(smsc_type);
        return NULL;
    }

    if (octstr_compare(smsc_type, octstr_imm("fake")) == 0)
        ret = smsc_fake_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("cimd2")) == 0)
	ret = smsc_cimd2_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("emi")) == 0)
	ret = smsc_emi2_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("http")) == 0)
        ret = smsc_http_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("smpp")) == 0)
	ret = smsc_smpp_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("at")) == 0)
	ret = smsc_at2_create(conn,grp);
    else if (octstr_compare(smsc_type, octstr_imm("cgw")) == 0)
        ret = smsc_cgw_create(conn,grp);
    else if (octstr_compare(smsc_type, octstr_imm("smasi")) == 0)
        ret = smsc_smasi_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("oisd")) == 0)
        ret = smsc_oisd_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("loopback")) == 0)
        ret = smsc_loopback_create(conn, grp);
#ifdef HAVE_GSOAP
    else if (octstr_compare(smsc_type, octstr_imm("parlayx")) == 0)
    	ret = smsc_soap_parlayx_create(conn, grp);
#endif
    else
        ret = smsc_wrapper_create(conn, grp);

    octstr_destroy(smsc_type);
    if (ret == -1) {
        smscconn_destroy(conn);
        return NULL;
    }
    gw_assert(conn->send_msg != NULL);

    bb_smscconn_ready(conn);

    return conn;
}


void smscconn_shutdown(SMSCConn *conn, int finish_sending)
{
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD) {
	mutex_unlock(conn->flow_mutex);
	return;
    }

    /* Call SMSC specific destroyer */
    if (conn->shutdown) {
        /* 
         * we must unlock here, because module manipulate their state
         * and will try to lock this mutex.Otherwise we have deadlock!
         */
        mutex_unlock(conn->flow_mutex);
	conn->shutdown(conn, finish_sending);
    }
    else {
	conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
        mutex_unlock(conn->flow_mutex);
    }

    return;
}


int smscconn_destroy(SMSCConn *conn)
{
    if (conn == NULL)
	return 0;
    if (conn->status != SMSCCONN_DEAD)
	return -1;
    mutex_lock(conn->flow_mutex);

    counter_destroy(conn->received);
    counter_destroy(conn->received_dlr);
    counter_destroy(conn->sent);
    counter_destroy(conn->sent_dlr);
    counter_destroy(conn->failed);

    load_destroy(conn->incoming_sms_load);
    load_destroy(conn->incoming_dlr_load);
    load_destroy(conn->outgoing_sms_load);
    load_destroy(conn->outgoing_dlr_load);

    octstr_destroy(conn->name);
    octstr_destroy(conn->id);
    octstr_destroy(conn->admin_id);
    gwlist_destroy(conn->allowed_smsc_id, octstr_destroy_item);
    gwlist_destroy(conn->denied_smsc_id, octstr_destroy_item);
    gwlist_destroy(conn->preferred_smsc_id, octstr_destroy_item);
    octstr_destroy(conn->denied_prefix);
    octstr_destroy(conn->allowed_prefix);
    octstr_destroy(conn->preferred_prefix);
    octstr_destroy(conn->unified_prefix);
    octstr_destroy(conn->our_host);
    octstr_destroy(conn->log_file);

    if (conn->denied_smsc_id_regex != NULL) gw_regex_destroy(conn->denied_smsc_id_regex);
    if (conn->allowed_smsc_id_regex != NULL) gw_regex_destroy(conn->allowed_smsc_id_regex);
    if (conn->preferred_prefix_regex != NULL) gw_regex_destroy(conn->preferred_prefix_regex);
    if (conn->denied_prefix_regex != NULL) gw_regex_destroy(conn->denied_prefix_regex);
    if (conn->allowed_prefix_regex != NULL) gw_regex_destroy(conn->allowed_prefix_regex);

    octstr_destroy(conn->reroute_to_smsc);
    dict_destroy(conn->reroute_by_receiver);

    mutex_unlock(conn->flow_mutex);
    mutex_destroy(conn->flow_mutex);

    gw_free(conn);
    return 0;
}


int smscconn_stop(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD || conn->is_stopped != 0
	|| conn->why_killed != SMSCCONN_ALIVE)
    {
	mutex_unlock(conn->flow_mutex);
	return -1;
    }
    conn->is_stopped = 1;
    mutex_unlock(conn->flow_mutex);

    if (conn->stop_conn)
	conn->stop_conn(conn);

    return 0;
}


void smscconn_start(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD || conn->is_stopped == 0) {
	mutex_unlock(conn->flow_mutex);
	return;
    }
    conn->is_stopped = 0;
    mutex_unlock(conn->flow_mutex);
    
     if (conn->start_conn)
	conn->start_conn(conn);
}


const Octstr *smscconn_name(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    return conn->name;
}


const Octstr *smscconn_id(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    return conn->id;
}


const Octstr *smscconn_admin_id(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    return conn->admin_id;
}


int smscconn_usable(SMSCConn *conn, Msg *msg)
{
    gw_assert(conn != NULL);
    gw_assert(msg != NULL && msg_type(msg) == sms);

    if (conn->status == SMSCCONN_DEAD || conn->why_killed != SMSCCONN_ALIVE)
	return -1;

    /* if allowed-smsc-id set, then only allow this SMSC if message
     * smsc-id matches any of its allowed SMSCes
     */
    if (conn->allowed_smsc_id && (msg->sms.smsc_id == NULL ||
         gwlist_search(conn->allowed_smsc_id, msg->sms.smsc_id, octstr_item_match) == NULL)) {
        return -1;
    }
    /* ..if no allowed-smsc-id set but denied-smsc-id and message smsc-id
     * is set, deny message if smsc-ids match */
    else if (conn->denied_smsc_id && msg->sms.smsc_id != NULL &&
                 gwlist_search(conn->denied_smsc_id, msg->sms.smsc_id, octstr_item_match) != NULL) {
        return -1;
    }

    if (conn->allowed_smsc_id_regex) {
        if (msg->sms.smsc_id == NULL)
            return -1;
        
        if (gw_regex_match_pre(conn->allowed_smsc_id_regex, msg->sms.smsc_id) == 0) 
            return -1;
    }
    else if (conn->denied_smsc_id_regex && msg->sms.smsc_id != NULL) {
        if (gw_regex_match_pre(conn->denied_smsc_id_regex, msg->sms.smsc_id) == 1) 
            return -1;
    }

    /* Have allowed */
    if (conn->allowed_prefix && ! conn->denied_prefix && 
       (does_prefix_match(conn->allowed_prefix, msg->sms.receiver) != 1))
	return -1;
    
    if (conn->allowed_prefix_regex && ! conn->denied_prefix_regex) {
        if (gw_regex_match_pre(conn->allowed_prefix_regex, msg->sms.receiver) == 0)
            return -1;
    }

    /* Have denied */
    if (conn->denied_prefix && ! conn->allowed_prefix &&
       (does_prefix_match(conn->denied_prefix, msg->sms.receiver) == 1))
	return -1;

    if (conn->denied_prefix_regex && ! conn->allowed_prefix_regex) {
        if (gw_regex_match_pre(conn->denied_prefix_regex, msg->sms.receiver) == 1)
            return -1;
    }

    /* Have allowed and denied */
    if (conn->denied_prefix && conn->allowed_prefix &&
       (does_prefix_match(conn->allowed_prefix, msg->sms.receiver) != 1) &&
       (does_prefix_match(conn->denied_prefix, msg->sms.receiver) == 1) )
	return -1;

    if (conn->allowed_prefix_regex && conn->denied_prefix_regex) {
        if (gw_regex_match_pre(conn->allowed_prefix_regex, msg->sms.receiver) == 0 &&
            gw_regex_match_pre(conn->denied_prefix_regex, msg->sms.receiver) == 1)
            return -1;
    }
    
    /* then see if it is preferred one */
    if (conn->preferred_smsc_id && msg->sms.smsc_id != NULL &&
         gwlist_search(conn->preferred_smsc_id, msg->sms.smsc_id, octstr_item_match) != NULL) {
        return 1;
    }

    if (conn->preferred_prefix)
	if (does_prefix_match(conn->preferred_prefix, msg->sms.receiver) == 1)
	    return 1;

    if (conn->preferred_prefix_regex &&
        gw_regex_match_pre(conn->preferred_prefix_regex, msg->sms.receiver) == 1) {
        return 1;
    }
        
    return 0;
}


int smscconn_send(SMSCConn *conn, Msg *msg)
{
    int ret = -1;
    List *parts = NULL;
    
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD || conn->why_killed != SMSCCONN_ALIVE) {
        mutex_unlock(conn->flow_mutex);
        return -1;
    }

    /* if this a retry of splitted message, don't unify prefix and don't try to split */
    if (msg->sms.split_parts == NULL) {    
        /* normalize the destination number for this smsc */
        char *uf = conn->unified_prefix ? octstr_get_cstr(conn->unified_prefix) : NULL;
        normalize_number(uf, &(msg->sms.receiver));

        /* split msg */
        parts = sms_split(msg, NULL, NULL, NULL, NULL, 1, 
            counter_increase(split_msg_counter) & 0xff, 0xff, conn->max_sms_octets);
        if (gwlist_len(parts) == 1) {
            /* don't create split_parts of sms fit into one */
            gwlist_destroy(parts, msg_destroy_item);
            parts = NULL;
        }
    }
    
    if (parts == NULL)
        ret = conn->send_msg(conn, msg);
    else {
        long i, parts_len = gwlist_len(parts);
        struct split_parts *split = gw_malloc(sizeof(*split));
         /* must duplicate, because smsc2_route will destroy this msg */
        split->orig = msg_duplicate(msg);
        split->parts_left = counter_create();
        split->status = SMSCCONN_SUCCESS;
        counter_set(split->parts_left, parts_len);
        split->smsc_conn = conn;
        debug("bb.sms.splits", 0, "new split_parts created %p", split);
        for (i = 0; i < parts_len; i++) {
            msg = gwlist_get(parts, i);
            msg->sms.split_parts = split;
            ret = conn->send_msg(conn, msg);
            if (ret < 0) {
                if (i == 0) {
                    counter_destroy(split->parts_left);
                    gwlist_destroy(parts, msg_destroy_item);
                    gw_free(split);
                    mutex_unlock(conn->flow_mutex);
                    return ret;
                }
                /*
                 * Some parts were sent. So handle this within
                 * bb_smscconn_XXX().
                 */
                split->status = SMSCCONN_FAILED_REJECTED;
                counter_increase_with(split->parts_left, -(parts_len - i));
                warning(0, "Could not send all parts of a split message");
                break;
            }
        }
        gwlist_destroy(parts, msg_destroy_item);
    }
    mutex_unlock(conn->flow_mutex);
    return ret;
}


int smscconn_status(SMSCConn *conn)
{
    gw_assert(conn != NULL);

    return conn->status;
}


int smscconn_info(SMSCConn *conn, StatusInfo *infotable)
{
    if (conn == NULL || infotable == NULL)
	return -1;

    mutex_lock(conn->flow_mutex);

    infotable->status = conn->status;
    infotable->killed = conn->why_killed;
    infotable->is_stopped = conn->is_stopped;
    infotable->online = time(NULL) - conn->connect_time;
    
    infotable->sent = counter_value(conn->sent);
    infotable->received = counter_value(conn->received);
    infotable->sent_dlr = counter_value(conn->sent_dlr);
    infotable->received_dlr = counter_value(conn->received_dlr);
    infotable->failed = counter_value(conn->failed);

    if (conn->queued)
	infotable->queued = conn->queued(conn);
    else
	infotable->queued = -1;

    infotable->load = conn->load;
    
    mutex_unlock(conn->flow_mutex);

    return 0;
}


