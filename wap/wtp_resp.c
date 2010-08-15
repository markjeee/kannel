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
 * wtp_resp.c - WTP responder implementation
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */

#include "gwlib/gwlib.h"
#include "wtp_resp.h"
#include "wtp_pack.h"
#include "wtp_tid.h"
#include "wtp.h"
#include "timers.h"
#include "wap.h"

/***********************************************************************
 * Internal data structures.
 *
 * List of responder WTP machines.
 */
static List *resp_machines = NULL;


/*
 * Counter for responder WTP machine id numbers, to make sure they are unique.
 */
static Counter *resp_machine_id_counter = NULL;


/*
 * Give the status of wtp responder:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } resp_run_status = limbo;


wap_dispatch_func_t *dispatch_to_wdp;
wap_dispatch_func_t *dispatch_to_wsp;
wap_dispatch_func_t *dispatch_to_push;

/*
 * Queue of events to be handled by WTP responder.
 */
static List *resp_queue = NULL;

/*
 * Timer 'tick'. All wtp responder timer values are multiplies of this one
 */
static long resp_timer_freq = -1;

/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Create and destroy an uniniatilized wtp responder state machine.
 */

static WTPRespMachine *resp_machine_create(WAPAddrTuple *tuple, long tid, 
                                           long tcl);
static void resp_machine_destroy(void *sm);

/*
 * Checks whether wtp responser machines data structure includes a specific 
 * machine.
 * The machine in question is identified with with source and destination
 * address and port and tid. If the machine does not exist and the event is 
 * RcvInvoke, a new machine is created and added in the machines data 
 * structure. 
 * First test incoming events (WTP 10.2) (Exception is tests nro 4 and 5: if 
 * we have a memory error, we panic. Nro 4 is already checked)  If event was 
 * validated and If the event was RcvAck or RcvAbort, the event is ignored. 
 * If the event is RcvErrorPDU, new machine is created.
 */
static WTPRespMachine *resp_machine_find_or_create(WAPEvent *event);


/*
 * Feed an event to a WTP responder state machine. Handle all errors by 
 * itself, do not report them to the caller. WSP indication or confirmation 
 * is handled by an included state table.
 */
static void resp_event_handle(WTPRespMachine *machine, WAPEvent *event);

/*
 * Print a wtp responder machine state as a string.
 */
static char *name_resp_state(int name);

/*
 * Find the wtp responder machine from the global list of wtp responder 
 * structures that corresponds to the five-tuple of source and destination 
 * addresses and ports and the transaction identifier. Return a pointer to 
 * the machine, or NULL if not found.
 */
static WTPRespMachine *resp_machine_find(WAPAddrTuple *tuple, long tid, 
                                         long mid);
static void main_thread(void *);

/*
 * Start acknowledgement interval timer
 */
static void start_timer_A(WTPRespMachine *machine);

/*
 * Start retry interval timer
 */
static void start_timer_R(WTPRespMachine *machine);

/*
 * Start timeout interval timer.
 */
static void start_timer_W(WTPRespMachine *machine);
static WAPEvent *create_tr_invoke_ind(WTPRespMachine *sm, Octstr *user_data);
static WAPEvent *create_tr_abort_ind(WTPRespMachine *sm, long abort_reason);
static WAPEvent *create_tr_result_cnf(WTPRespMachine *sm);
static int erroneous_field_in(WAPEvent *event);
static void handle_wrong_version(WAPEvent *event);

/*
 * SAR related functions.
 */
static WAPEvent *assembly_sar_event (WTPRespMachine *machine, int last_psn);
static int add_sar_transaction (WTPRespMachine *machine, Octstr *data, int psn);
/* static int is_wanted_sar_data (void *a, void *b); */
static int process_sar_transaction(WTPRespMachine *machine, WAPEvent **event);
static void begin_sar_result(WTPRespMachine *machine, WAPEvent *event);
static void continue_sar_result(WTPRespMachine *machine, WAPEvent *event);
static void resend_sar_result(WTPRespMachine *resp_machine, WAPEvent *event);
static void sar_info_destroy(void *sar_info);
static void sardata_destroy(void *sardata);

/*
 * Create a datagram with an Abort PDU and send it to the WDP layer.
 */
static void send_abort(WTPRespMachine *machine, long type, long reason);

/*
 * Create a datagram with an Ack PDU and send it to the WDP layer.
 */
static void send_ack(WTPRespMachine *machine, long ack_type, int rid_flag);


/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */

void wtp_resp_init(wap_dispatch_func_t *datagram_dispatch,
                   wap_dispatch_func_t *session_dispatch,
                   wap_dispatch_func_t *push_dispatch, 
                   long timer_freq) 
{
    resp_machines = gwlist_create();
    resp_machine_id_counter = counter_create();

    resp_queue = gwlist_create();
    gwlist_add_producer(resp_queue);

    dispatch_to_wdp = datagram_dispatch;
    dispatch_to_wsp = session_dispatch;
    dispatch_to_push = push_dispatch;

    timers_init();
    resp_timer_freq = timer_freq;
    wtp_tid_cache_init();

    gw_assert(resp_run_status == limbo);
    resp_run_status = running;
    gwthread_create(main_thread, NULL);
}

void wtp_resp_shutdown(void) 
{
    gw_assert(resp_run_status == running);
    resp_run_status = terminating;
    gwlist_remove_producer(resp_queue);
    gwthread_join_every(main_thread);

    debug("wap.wtp", 0, "wtp_resp_shutdown: %ld resp_machines left",
     	  gwlist_len(resp_machines));
    gwlist_destroy(resp_machines, resp_machine_destroy);
    gwlist_destroy(resp_queue, wap_event_destroy_item);

    counter_destroy(resp_machine_id_counter);

    wtp_tid_cache_shutdown();
    timers_shutdown();
}

void wtp_resp_dispatch_event(WAPEvent *event) 
{
    gwlist_produce(resp_queue, event);
}


/*****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 */

static void main_thread(void *arg) 
{
    WTPRespMachine *sm;
    WAPEvent *e;

    while (resp_run_status == running && 
           (e = gwlist_consume(resp_queue)) != NULL) {

        sm = resp_machine_find_or_create(e);
        if (sm == NULL) {
            wap_event_destroy(e);
        } else {
            resp_event_handle(sm, e);
        }
    }
}

/*
 * Give the name of a responder state in a readable form. 
 */
static char *name_resp_state(int s)
{
       switch (s) {
       #define STATE_NAME(state) case state: return #state;
       #define ROW(state, event, condition, action, new_state)
       #include "wtp_resp_states.def"
       default:
           return "unknown state";
       }
}


/*
 * Feed an event to a WTP responder state machine. Handle all errors yourself,
 * do not report them to the caller. Note: Do not put {}s of the else block 
 * inside the macro definition. 
 */
static void resp_event_handle(WTPRespMachine *resp_machine, WAPEvent *event)
{
    WAPEvent *wsp_event = NULL;

    /* 
     * We don't feed sar packets into state machine 
     * until we got the whole message 
     */
    if (process_sar_transaction(resp_machine,&event) == 0) {
        debug("wap.wtp", 0, "SAR event received, wait for continue");
        /* For removing state machine in case of incomplete sar */
        start_timer_W(resp_machine);
        if (event != NULL) {
            wap_event_destroy(event);  
        }
        return;
    }

    debug("wap.wtp", 0, "WTP: resp_machine %ld, state %s, event %s.", 
	  resp_machine->mid, 
	  name_resp_state(resp_machine->state), 
	  wap_event_name(event->type));

    #define STATE_NAME(state)
    #define ROW(wtp_state, event_type, condition, action, next_state) \
	 if (resp_machine->state == wtp_state && \
	     event->type == event_type && \
	     (condition)) { \
	     action \
	     resp_machine->state = next_state; \
	     debug("wap.wtp", 0, "WTP %ld: New state %s", resp_machine->mid,                      #next_state); \
	 } else 
    #include "wtp_resp_states.def"
	 {
	     error(0, "WTP: handle_event: unhandled event!");
	     debug("wap.wtp", 0, "WTP: handle_event: Unhandled event was:");
	     wap_event_dump(event);
             wap_event_destroy(event);
             return;
	 }

    if (event != NULL) {
	wap_event_destroy(event);  
    }

    if (resp_machine->state == LISTEN)
     	resp_machine_destroy(resp_machine);
}

static void handle_wrong_version(WAPEvent *event)
{       
    WAPEvent *ab;

    if (event->type == RcvInvoke) {
        ab = wtp_pack_abort(PROVIDER, WTPVERSIONZERO, event->u.RcvInvoke.tid, 
                            event->u.RcvInvoke.addr_tuple);
        dispatch_to_wdp(ab);
    }
}

/*
 * Check for features 7 and 9 in WTP 10.2.
 */
static int erroneous_field_in(WAPEvent *event)
{
    return event->type == RcvInvoke && event->u.RcvInvoke.version != 0;
}

/*
 * React features 7 and 9 in WTP 10.2, by aborting with an appropiate error 
 * message.
 */
static void handle_erroneous_field_in(WAPEvent *event)
{
    if (event->type == RcvInvoke) {
        if (event->u.RcvInvoke.version != 0) {
            debug("wap.wtp_resp", 0, "WTP_RESP: wrong version, aborting"
                  "transaction");
            handle_wrong_version(event);
        }
    }
}

/*
 * Checks whether wtp machines data structure includes a specific machine.
 * The machine in question is identified with with source and destination
 * address and port and tid.  First test incoming events (WTP 10.2)
 * (Exception is tests nro 4 and 5: if we have a memory error, we panic. Nro 5 
 * is already checked)  If event was validated and if the machine does not 
 * exist and the event is RcvInvoke, a new machine is created and added in 
 * the machines data structure. If the event was RcvAck or RcvAbort, the 
 * event is ignored (test nro 3). If the event is RcvErrorPDU (test nro 4) 
 * new machine is created for handling this event. If the event is one of WSP 
 * primitives, we have an error.
 */
static WTPRespMachine *resp_machine_find_or_create(WAPEvent *event)
{
    WTPRespMachine *resp_machine = NULL;
    long tid, mid;
    WAPAddrTuple *tuple;

    tid = -1;
    tuple = NULL;
    mid = -1;

    switch (event->type) {
        case RcvInvoke:
            /* check if erroneous fields are given */
            if (erroneous_field_in(event)) {
                handle_erroneous_field_in(event);
                return NULL;
            } else {
                tid = event->u.RcvInvoke.tid;
                tuple = event->u.RcvInvoke.addr_tuple;
            }
            break;

        case RcvSegInvoke:
            tid = event->u.RcvSegInvoke.tid;
            tuple = event->u.RcvSegInvoke.addr_tuple;
            break;

        case RcvAck:
            tid = event->u.RcvAck.tid;
            tuple = event->u.RcvAck.addr_tuple;
            break;

        case RcvNegativeAck:
            tid = event->u.RcvAck.tid;
            tuple = event->u.RcvAck.addr_tuple;
            break;

        case RcvAbort:
            tid = event->u.RcvAbort.tid;
            tuple = event->u.RcvAbort.addr_tuple;
            break;

        case RcvErrorPDU:
            tid = event->u.RcvErrorPDU.tid;
            tuple = event->u.RcvErrorPDU.addr_tuple;
            break;

        case TR_Invoke_Res:
            mid = event->u.TR_Invoke_Res.handle;
            break;

        case TR_Result_Req:
            mid = event->u.TR_Result_Req.handle;
            break;

        case TR_Abort_Req:
            mid = event->u.TR_Abort_Req.handle;
            break;

        case TimerTO_A:
            mid = event->u.TimerTO_A.handle;
            break;

        case TimerTO_R:
            mid = event->u.TimerTO_R.handle;
            break;

        case TimerTO_W:
            mid = event->u.TimerTO_W.handle;
            break;

        default:
            debug("wap.wtp", 0, "WTP: resp_machine_find_or_create:"
                  "unhandled event"); 
            wap_event_dump(event);
            return NULL;
    }

    gw_assert(tuple != NULL || mid != -1);
    resp_machine = resp_machine_find(tuple, tid, mid);
           
    if (resp_machine == NULL){

        switch (event->type) {

        /*
         * When PDU with an illegal header is received, its tcl-field is 
         * irrelevant and possibly meaningless). In this case we must create 
         * a new machine, if there is any. There is a machine for all events 
         * handled stateful manner.
         */
        case RcvErrorPDU:
            debug("wap.wtp_resp", 0, "an erronous pdu received");
            wap_event_dump(event);
            resp_machine = resp_machine_create(tuple, tid, 
                                               event->u.RcvInvoke.tcl); 
            break;
           
        case RcvInvoke:
            resp_machine = resp_machine_create(tuple, tid, 
                                               event->u.RcvInvoke.tcl);
            /* if SAR requested */
            if (!event->u.RcvInvoke.gtr || !event->u.RcvInvoke.ttr) {
                resp_machine->sar = gw_malloc(sizeof(WTPSARData));
                resp_machine->sar->nsegm = 0;
                resp_machine->sar->csegm = 0;
                resp_machine->sar->lsegm = 0;
                resp_machine->sar->data = NULL;		
            }

            break;
	    
        case RcvSegInvoke:
            info(0, "WTP_RESP: resp_machine_find_or_create:"
                 " segmented invoke received, yet having no machine");
            break;

        /*
         * This and the following branch implement test nro 3 in WTP 10.2.
         */
        case RcvAck: 
            info(0, "WTP_RESP: resp_machine_find_or_create:"
                 " ack received, yet having no machine");
            break;

        case RcvNegativeAck: 
            info(0, "WTP_RESP: resp_machine_find_or_create:"
                 " negative ack received, yet having no machine");
            break;

        case RcvAbort: 
            info(0, "WTP_RESP: resp_machine_find_or_create:"
                 " abort received, yet having no machine");
            break;

        case TR_Invoke_Res: 
        case TR_Result_Req: 
        case TR_Abort_Req:
            error(0, "WTP_RESP: resp_machine_find_or_create: WSP primitive to"
                  " a wrong WTP machine");
            break;

        case TimerTO_A: 
        case TimerTO_R: 
        case TimerTO_W:
            error(0, "WTP_RESP: resp_machine_find_or_create: timer event"
                  " without a corresponding machine");
            break;
                 
        default:
            error(0, "WTP_RESP: resp_machine_find_or_create: unhandled event");
            wap_event_dump(event);
            break;
        }
   } /* if machine == NULL */   
   return resp_machine;
}

static int is_wanted_resp_machine(void *a, void *b) 
{
    machine_pattern *pat;
    WTPRespMachine *m;
	
    m = a;
    pat = b;

    if (m->mid == pat->mid)
	return 1;

    if (pat->mid != -1)
	return 0;

    return m->tid == pat->tid && 
           wap_addr_tuple_same(m->addr_tuple, pat->tuple);
}


static WTPRespMachine *resp_machine_find(WAPAddrTuple *tuple, long tid, 
                                         long mid) 
{
    machine_pattern pat;
    WTPRespMachine *m;
	
    pat.tuple = tuple;
    pat.tid = tid;
    pat.mid = mid;
	
    m = gwlist_search(resp_machines, &pat, is_wanted_resp_machine);
    return m;
}


static WTPRespMachine *resp_machine_create(WAPAddrTuple *tuple, long tid, 
                                           long tcl) 
{
    WTPRespMachine *resp_machine;
	
    resp_machine = gw_malloc(sizeof(WTPRespMachine)); 
        
    #define ENUM(name) resp_machine->name = LISTEN;
    #define EVENT(name) resp_machine->name = NULL;
    #define INTEGER(name) resp_machine->name = 0; 
    #define TIMER(name) resp_machine->name = gwtimer_create(resp_queue); 
    #define ADDRTUPLE(name) resp_machine->name = NULL; 
    #define LIST(name) resp_machine->name = NULL;
    #define SARDATA(name) resp_machine->name = NULL;
    #define MACHINE(field) field
    #include "wtp_resp_machine.def"

    gwlist_append(resp_machines, resp_machine);

    resp_machine->mid = counter_increase(resp_machine_id_counter);
    resp_machine->addr_tuple = wap_addr_tuple_duplicate(tuple);
    resp_machine->tid = tid;
    resp_machine->tcl = tcl;
	
    debug("wap.wtp", 0, "WTP: Created WTPRespMachine %p (%ld)", 
	  (void *) resp_machine, resp_machine->mid);

    return resp_machine;
} 


/*
 * Destroys a WTPRespMachine. Assumes it is safe to do so. Assumes it has 
 * already been deleted from the machines list.
 */
static void resp_machine_destroy(void * p)
{
    WTPRespMachine *resp_machine;

    resp_machine = p;
    debug("wap.wtp", 0, "WTP: Destroying WTPRespMachine %p (%ld)", 
	  (void *) resp_machine, resp_machine->mid);
	
    gwlist_delete_equal(resp_machines, resp_machine);
        
    #define ENUM(name) resp_machine->name = LISTEN;
    #define EVENT(name) wap_event_destroy(resp_machine->name);
    #define INTEGER(name) resp_machine->name = 0; 
    #define TIMER(name) gwtimer_destroy(resp_machine->name); 
    #define ADDRTUPLE(name) wap_addr_tuple_destroy(resp_machine->name); 
    #define LIST(name) gwlist_destroy(resp_machine->name,sar_info_destroy);
    #define SARDATA(name) sardata_destroy(resp_machine->name);
    #define MACHINE(field) field
    #include "wtp_resp_machine.def"
    gw_free(resp_machine);
}

/*
 * Create a TR-Invoke.ind event.
 */
static WAPEvent *create_tr_invoke_ind(WTPRespMachine *sm, Octstr *user_data) 
{
    WAPEvent *event;
	
    event = wap_event_create(TR_Invoke_Ind);
    event->u.TR_Invoke_Ind.ack_type = sm->u_ack;
    event->u.TR_Invoke_Ind.user_data = octstr_duplicate(user_data);
    event->u.TR_Invoke_Ind.tcl = sm->tcl;
    event->u.TR_Invoke_Ind.addr_tuple = 
	wap_addr_tuple_duplicate(sm->addr_tuple);
    event->u.TR_Invoke_Ind.handle = sm->mid;
    return event;
}


/*
 * Create a TR-Result.cnf event.
 */
static WAPEvent *create_tr_result_cnf(WTPRespMachine *sm) 
{
    WAPEvent *event;
	
    event = wap_event_create(TR_Result_Cnf);
    event->u.TR_Result_Cnf.addr_tuple = 
	wap_addr_tuple_duplicate(sm->addr_tuple);
    event->u.TR_Result_Cnf.handle = sm->mid;
    return event;
}

/*
 * Creates TR-Abort.ind event from a responder state machine. In addition, set
 * the responder indication flag.
 */
static WAPEvent *create_tr_abort_ind(WTPRespMachine *sm, long abort_reason) {
    WAPEvent *event;
	
    event = wap_event_create(TR_Abort_Ind);
    event->u.TR_Abort_Ind.abort_code = abort_reason;
    event->u.TR_Abort_Ind.addr_tuple = 
	wap_addr_tuple_duplicate(sm->addr_tuple);
    event->u.TR_Abort_Ind.handle = sm->mid;
    event->u.TR_Abort_Ind.ir_flag = RESPONDER_INDICATION;

    return event;
}

/*
 * Start acknowledgement interval timer. Multiply time with
 * resp_timer_freq.
 */
static void start_timer_A(WTPRespMachine *machine) 
{
    WAPEvent *timer_event;

    timer_event = wap_event_create(TimerTO_A);
    timer_event->u.TimerTO_A.handle = machine->mid;
    gwtimer_start(machine->timer, L_A_WITH_USER_ACK * resp_timer_freq, 
                  timer_event);
}

/*
 * Start retry interval timer. Multiply time with resp_timer_freq.
 */
static void start_timer_R(WTPRespMachine *machine) 
{
    WAPEvent *timer_event;

    timer_event = wap_event_create(TimerTO_R);
    timer_event->u.TimerTO_R.handle = machine->mid;
    gwtimer_start(machine->timer, L_R_WITH_USER_ACK * resp_timer_freq, 
                  timer_event);
}

/*
 * Start segmentation timeout interval timer. Multiply time with
 * resp_timer_freq.
 */
static void start_timer_W(WTPRespMachine *machine)
{
    WAPEvent *timer_event;

    timer_event = wap_event_create(TimerTO_W);
    timer_event->u.TimerTO_W.handle = machine->mid;
    gwtimer_start(machine->timer, W_WITH_USER_ACK * resp_timer_freq, 
                  timer_event);
}

static void send_abort(WTPRespMachine *machine, long type, long reason)
{
    WAPEvent *e;

    e = wtp_pack_abort(type, reason, machine->tid, machine->addr_tuple);
    dispatch_to_wdp(e);
}

static void send_ack(WTPRespMachine *machine, long ack_type, int rid_flag)
{
    WAPEvent *e;

    e = wtp_pack_ack(ack_type, rid_flag, machine->tid, machine->addr_tuple);
    dispatch_to_wdp(e);
}

/* 
 * Process incoming event, checking for WTP SAR 
 */
static int process_sar_transaction(WTPRespMachine *machine, WAPEvent **event) 
{
    WAPEvent *e, *orig_event;
    int psn;
  
    orig_event = *event;

    if (orig_event->type == RcvInvoke) { 
        if (!orig_event->u.RcvInvoke.ttr || !orig_event->u.RcvInvoke.gtr) { /* SAR */
            /* Ericcson set TTR flag even if we have the only part */
            if (orig_event->u.RcvInvoke.ttr == 1) {
                return 1; /* Not SAR although TTR flag was set */
            } else {
                /* save initial event */
                machine->sar_invoke = wap_event_duplicate(orig_event);

                /* save data into list with psn = 0 */
                add_sar_transaction(machine, orig_event->u.RcvInvoke.user_data, 0);

                if (orig_event->u.RcvInvoke.gtr == 1) { /* Need to acknowledge */
                    e = wtp_pack_sar_ack(ACKNOWLEDGEMENT, machine->tid,
                                         machine->addr_tuple, 0);
                    dispatch_to_wdp(e);
                }
                return 0;
            }
        } else {
            return 1; /* Not SAR */
        } 
    }

    if (orig_event->type == RcvSegInvoke) {
        add_sar_transaction(machine, orig_event->u.RcvSegInvoke.user_data, 
                            orig_event->u.RcvSegInvoke.psn);

        if (orig_event->u.RcvSegInvoke.gtr == 1) { /* Need to acknowledge */
            e = wtp_pack_sar_ack(ACKNOWLEDGEMENT, machine->tid, machine->addr_tuple,
                                 orig_event->u.RcvSegInvoke.psn);
            dispatch_to_wdp(e);
        }

        if (orig_event->u.RcvSegInvoke.ttr == 1) { /* Need to feed to WSP */

            /* Create assembled event */
            psn = orig_event->u.RcvSegInvoke.psn;
            wap_event_destroy(orig_event);
      
            *event = assembly_sar_event(machine,psn);

            gw_assert(event != NULL);

            return 1;
        }
        return 0;
    }

    /* Not SAR message */
    return 1;
}

static int is_wanted_sar_data(void *a, void *b)
{
    sar_info_t *s;
    int *i;

    s = a;
    i = b;

    if (*i == s->sar_psn) {
        return 1;
    } else {
        return 0;
    }
}

/* 
 * Return 0 if transaction added suscessufully, 1 otherwise.
 */
static int add_sar_transaction(WTPRespMachine *machine, Octstr *data, int psn) 
{
    sar_info_t *sar_info;

    if (machine->sar_info == NULL) {
        machine->sar_info = gwlist_create();
    }

    if (gwlist_search(machine->sar_info, &psn, is_wanted_sar_data) == NULL) {
        sar_info = gw_malloc(sizeof(sar_info_t));
        sar_info->sar_psn = psn;
        sar_info->sar_data = octstr_duplicate(data);
        gwlist_append(machine->sar_info, sar_info);
        return 0;
    } else {
        debug("wap.wtp", 0, "Duplicated psn found, ignore packet");
        return 1;
    } 
}

static WAPEvent *assembly_sar_event(WTPRespMachine *machine, int last_psn) 
{
    WAPEvent *e;
    int i;
    sar_info_t *sar_info;

    e = wap_event_duplicate(machine->sar_invoke);

    for (i = 1; i <= last_psn; i++) {
        if ((sar_info = gwlist_search(machine->sar_info, &i, is_wanted_sar_data)) != NULL) {
            octstr_append(e->u.RcvInvoke.user_data,sar_info->sar_data);
        } else {
            debug("wap.wtp", 0, "Packet with psn %d not found", i);
            return e;
        }
    }

    return e;
}

static void sar_info_destroy(void *p) 
{
    sar_info_t *sar_info;

    sar_info = p;
  
    octstr_destroy(sar_info->sar_data);
    gw_free(sar_info);
}

static void sardata_destroy(void *p) 
{
    WTPSARData * sardata;

    if (p) {
        sardata = p;
        octstr_destroy(sardata->data);
        gw_free(sardata);
    }
}

static void begin_sar_result(WTPRespMachine *resp_machine, WAPEvent *event) 
{
    WAPEvent *result;
    WTPSARData *sar;
    int psn;

    gw_assert(resp_machine->sar != NULL);

    sar = resp_machine->sar;
    sar->data = octstr_duplicate(event->u.TR_Result_Req.user_data);
    sar->nsegm = (octstr_len(sar->data)-1)/SAR_SEGM_SIZE;
    sar->tr = sar->lsegm = 0;
    sar->csegm = -1;

    debug("wap.wtp", 0, "WTP: begin_sar_result(): data len = %lu", 
          octstr_len(sar->data));

    for (psn = 0; !sar->tr; psn++) {
        result = wtp_pack_sar_result(resp_machine, psn);
        if (sar->tr) 
            resp_machine->result = wap_event_duplicate(result);

        debug("wap.wtp", 0, "WTP: dispath_to_wdp(): psn = %u", psn);
        dispatch_to_wdp(result);
        sar->lsegm = psn;
    }

    resp_machine->rid = 1;
}

static void continue_sar_result(WTPRespMachine *resp_machine, WAPEvent *event) 
{
    WAPEvent *result;
    WTPSARData *sar;
    int psn;

    gw_assert(resp_machine->sar != NULL && event->type == RcvAck);

    sar = resp_machine->sar;

    debug("wap.wtp", 0, "WTP: continue_sar_result(): lsegm=%d, nsegm=%d, csegm=%d",
          sar->lsegm, sar->nsegm, sar->csegm);

    start_timer_R(resp_machine);

    if (event->u.RcvAck.psn>sar->csegm) {
        sar->csegm = event->u.RcvAck.psn;
    }

    sar->tr = 0;
    wap_event_destroy(resp_machine->result);
    resp_machine->result = NULL;

    for (psn = sar->csegm + 1; !sar->tr; psn++) {
        result = wtp_pack_sar_result(resp_machine, psn);
        if (sar->tr) 
            resp_machine->result = wap_event_duplicate(result);

        debug("wap.wtp", 0, "WTP: dispath_to_wdp(): psn = %u",psn);
        dispatch_to_wdp(result);
        sar->lsegm = psn;
    }
}

static void resend_sar_result(WTPRespMachine *resp_machine, WAPEvent *event)
{
    WAPEvent *result;
    WTPSARData *sar;
    int	psn, i;

    gw_assert(resp_machine->sar != NULL && event->type == RcvNegativeAck);

    sar = resp_machine->sar;

    debug("wap.wtp", 0, "WTP: resend_sar_result(): lsegm=%d, nsegm=%d, csegm=%d",
          sar->lsegm, sar->nsegm, sar->csegm);

    start_timer_R(resp_machine);

    if (event->u.RcvNegativeAck.nmissing) { 
        /* if we have a list of missed packets */
        for(i = 0; i < event->u.RcvNegativeAck.nmissing; i++) {
            if ((psn = octstr_get_char(event->u.RcvNegativeAck.missing, i)) >= 0) {		
                result = wtp_pack_sar_result(resp_machine, psn);
                wtp_pack_set_rid(result, 1);
                debug("wap.wtp", 0, "WTP: dispath_to_wdp(): psn = %u", psn);
                dispatch_to_wdp(result);
            }
        }
    } else { 
        /* if we have to resend a whole group */
        sar->tr = 0;
        for (psn = sar->csegm+1; !sar->tr; psn++) {
            result = wtp_pack_sar_result(resp_machine, psn);
            wtp_pack_set_rid(result, 1);
            debug("wap.wtp", 0, "WTP: dispath_to_wdp(): psn = %u", psn);
            dispatch_to_wdp(result);
        }
    }
}



