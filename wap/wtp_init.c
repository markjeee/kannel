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
 * wtp_init.c - WTP initiator implementation
 *
 * By Aarno Syvänen for Wapit Ltd
 */

#include "gwlib/gwlib.h"
#include "wtp_init.h"
#include "wtp_pack.h"
#include "wap.h"

/*****************************************************************************
 * Internal data structures.
 *
 * List of initiator WTP machines
 */
static List *init_machines = NULL;

/*
 * Counter for initiator WTP machine id numbers, to make sure they are unique.
 */
static Counter *init_machine_id_counter = NULL;

/*
 * When we restart an iniator, we must set tidnew flag to avoid excessive tid
 * validations (WTP 8.8.3.2). Only an iniator uses this flag.
 */
static int tidnew = 1;

/*
 * Queue of events to be handled by WTP initiator.
 */
static List *queue = NULL;

/*
 * Give the status of the wtp initiator:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } initiator_run_status = limbo;

static wap_dispatch_func_t *dispatch_to_wdp;
static wap_dispatch_func_t *dispatch_to_wsp;

/*
 * This is a timer 'tick'. All timer values multiplies of this value.
 */
static long init_timer_freq = -1;

/***************************************************************************
 *
 * Prototypes for internal functions:
 */
static void main_thread(void *arg);
 
/*
 * Create and destroy an uniniatilised wtp initiator state machine
 */
static WTPInitMachine *init_machine_create(WAPAddrTuple *tuple, unsigned short
                                           tid, int tidnew);
static void init_machine_destroy(void *sm);
static void handle_init_event(WTPInitMachine *machine, WAPEvent *event);

/*
 * Checks whether wtp initiator machines data structure includes a specific 
 * machine.
 * The machine in question is identified with with source and destination
 * address and port and tid. 
 */
static WTPInitMachine *init_machine_find_or_create(WAPEvent *event);

/*
 * Creates TR-Abort.ind event.
 */
static WAPEvent *create_tr_abort_ind(WTPInitMachine *sm, long abort_reason);

/*
 * Creates TR-Invoke.cnf event 
 */
static WAPEvent *create_tr_invoke_cnf(WTPInitMachine *machine);
static int tid_wrapped(unsigned short tid);

/*
 * Create a datagram with an Abort PDU and send it to the WDP layer.
 */
static void send_abort(WTPInitMachine *machine, long type, long reason);

/*
 * Create a datagram with an Ack PDU and send it to the WDP layer.
 */
static void send_ack(WTPInitMachine *machine, long ack_type, int rid_flag);

/*
 * We use RcvTID consistently as a internal tid representation. So newly 
 * created tids are converted. SendTID = RcvTID ^ 0x8000 (WTP 10.4.3) and for 
 * an initiator, GenTID = SendTID (WTP 10.5). 
 */
static unsigned short rcv_tid(unsigned short tid);
static void start_initiator_timer_R(WTPInitMachine *machine); 
static void stop_initiator_timer(Timer *timer);

/**************************************************************************
 *
 * EXTERNAL FUNCTIONS
 */

void wtp_initiator_init(wap_dispatch_func_t *datagram_dispatch,
			wap_dispatch_func_t *session_dispatch, long timer_freq) 
{
    init_machines = gwlist_create();
    init_machine_id_counter = counter_create();
     
    queue = gwlist_create();
    gwlist_add_producer(queue);

    dispatch_to_wdp = datagram_dispatch;
    dispatch_to_wsp = session_dispatch;

    timers_init();
    init_timer_freq = timer_freq;

    gw_assert(initiator_run_status == limbo);
    initiator_run_status = running;
    gwthread_create(main_thread, NULL);
}

void wtp_initiator_shutdown(void) 
{
    gw_assert(initiator_run_status == running);
    initiator_run_status = terminating;
    gwlist_remove_producer(queue);
    gwthread_join_every(main_thread);

    debug("wap.wtp", 0, "wtp_initiator_shutdown: %ld init_machines left",
     	  gwlist_len(init_machines));
    gwlist_destroy(init_machines, init_machine_destroy);
    gwlist_destroy(queue, wap_event_destroy_item);

    counter_destroy(init_machine_id_counter);
    timers_shutdown();
}

void wtp_initiator_dispatch_event(WAPEvent *event) 
{
    gwlist_produce(queue, event);
}

/**************************************************************************
 *
 * INTERNAL FUNCTIONS:
 */

static void main_thread(void *arg) 
{
    WTPInitMachine *sm;
    WAPEvent *e;

    while (initiator_run_status == running && 
          (e = gwlist_consume(queue)) != NULL) {
        sm = init_machine_find_or_create(e);
	if (sm == NULL)
	    wap_event_destroy(e);
	else
	    handle_init_event(sm, e);
    }
}

static WTPInitMachine *init_machine_create(WAPAddrTuple *tuple, unsigned short
                                           tid, int tidnew)
{
     WTPInitMachine *init_machine;
	
     init_machine = gw_malloc(sizeof(WTPInitMachine)); 
        
     #define ENUM(name) init_machine->name = INITIATOR_NULL_STATE;
     #define INTEGER(name) init_machine->name = 0; 
     #define EVENT(name) init_machine->name = NULL;
     #define TIMER(name) init_machine->name = gwtimer_create(queue); 
     #define ADDRTUPLE(name) init_machine->name = NULL; 
     #define MACHINE(field) field
     #include "wtp_init_machine.def"

     gwlist_append(init_machines, init_machine);

     init_machine->mid = counter_increase(init_machine_id_counter);
     init_machine->addr_tuple = wap_addr_tuple_duplicate(tuple);
     init_machine->tid = tid;
     init_machine->tidnew = tidnew;
	
     debug("wap.wtp", 0, "WTP: Created WTPInitMachine %p (%ld)", 
	   (void *) init_machine, init_machine->mid);

     return init_machine;
}

/*
 * Destroys a WTPInitMachine. Assumes it is safe to do so. Assumes it has 
 * already been deleted from the machines list.
 */
static void init_machine_destroy(void *p)
{
     WTPInitMachine *init_machine;

     init_machine = p;
     debug("wap.wtp", 0, "WTP: Destroying WTPInitMachine %p (%ld)", 
	    (void *) init_machine, init_machine->mid);
	
     gwlist_delete_equal(init_machines, init_machine);
        
     #define ENUM(name) init_machine->name = INITIATOR_NULL_STATE;
     #define INTEGER(name) init_machine->name = 0; 
     #define EVENT(name) wap_event_destroy(init_machine->name); 
     #define TIMER(name) gwtimer_destroy(init_machine->name); 
     #define ADDRTUPLE(name) wap_addr_tuple_destroy(init_machine->name); 
     #define MACHINE(field) field
     #include "wtp_init_machine.def"
     gw_free(init_machine);
}

/*
 * Give the name of an initiator state in a readable form. 
 */
static unsigned char *name_init_state(int s)
{
       switch (s){
       #define INIT_STATE_NAME(state) case state: return (unsigned char *) #state;
       #define ROW(state, event, condition, action, new_state)
       #include "wtp_init_states.def"
       default:
           return (unsigned char *)"unknown state";
       }
}

/*
 * Feed an event to a WTP initiator state machine. Handle all errors by do not
 * report them to the caller. WSP indication or conformation is handled by an
 * included state table. Note: Do not put {}s of the else block inside the 
 * macro definition . 
 */
static void handle_init_event(WTPInitMachine *init_machine, WAPEvent *event)
{
     WAPEvent *wsp_event = NULL;

     debug("wap.wtp", 0, "WTP_INIT: initiator machine %ld, state %s,"
           " event %s.", 
	   init_machine->mid, 
	   name_init_state(init_machine->state), 
	   wap_event_name(event->type));
       
     #define INIT_STATE_NAME(state)
     #define ROW(init_state, event_type, condition, action, next_state) \
	 if (init_machine->state == init_state && \
	     event->type == event_type && \
	     (condition)) { \
	     action \
	     init_machine->state = next_state; \
	     debug("wap.wtp", 0, "WTP_INIT %ld: New state %s", \
                   init_machine->mid, #next_state); \
	 } else 
      #include "wtp_init_states.def"
	 {
	     error(1, "WTP_INIT: handle_init_event: unhandled event!");
	     debug("wap.wtp.init", 0, "WTP_INIT: handle_init_event:"
                   "Unhandled event was:");
	     wap_event_dump(event);
             wap_event_destroy(event);
             return;
	 }

      if (event != NULL) {
	  wap_event_destroy(event);  
      }

      if (init_machine->state == INITIATOR_NULL_STATE)
     	  init_machine_destroy(init_machine);      
}

static int is_wanted_init_machine(void *a, void *b) 
{
    struct machine_pattern *pat;
    WTPInitMachine *m;
	
    m = a;
    pat = b;

    if (m->mid == pat->mid)
	return 1;

    if (pat->mid != -1)
	return 0;

    return m->tid == pat->tid && 
	   wap_addr_tuple_same(m->addr_tuple, pat->tuple);
}

static WTPInitMachine *init_machine_find(WAPAddrTuple *tuple, long tid, 
                                         long mid) 
{
    struct machine_pattern pat;
    WTPInitMachine *m;
	
    pat.tuple = tuple;
    pat.tid = tid;
    pat.mid = mid;
	
    m = gwlist_search(init_machines, &pat, is_wanted_init_machine);
    return m;
}

/*
 * Checks whether wtp initiator machines data structure includes a specific 
 * machine. The machine in question is identified with with source and 
 * destination address and port and tid.  First test incoming events 
 * (WTP 10.2) (Exception are tests nro 4 and 5: if we have a memory error, 
 * we panic (nro 4); nro 5 is already checked). If we have an ack with tid 
 * verification flag set and no corresponding transaction, we abort.(case nro 
 * 2). If the event was a normal ack or an abort, it is ignored (error nro 3).
 * In the case of TR-Invoke.req a new machine is created, in the case of 
 * TR-Abort.req we have a serious error. We must create a new tid for a new
 * transaction here, because machines are identified by an address tuple and a
 * tid. This tid is GenTID (WTP 10.4.2), which is used only by the wtp iniator 
 * thread.
 * Note that as internal tid representation, module uses RcvTID (as required
 * by module wtp_pack). So we we turn the first bit of the tid stored by the
 * init machine.
 */
static WTPInitMachine *init_machine_find_or_create(WAPEvent *event)
{
    WTPInitMachine *machine = NULL;
    long mid;
    static long tid = -1; 
    WAPAddrTuple *tuple;

    mid = -1;
    tuple = NULL;

    switch (event->type) {
    case RcvAck:
        tid = event->u.RcvAck.tid;
        tuple = event->u.RcvAck.addr_tuple;
    break;

    case RcvAbort:
        tid = event->u.RcvAbort.tid;
        tuple = event->u.RcvAbort.addr_tuple;
    break;

    case RcvErrorPDU:
        mid = event->u.RcvErrorPDU.tid;
        tid = event->u.RcvErrorPDU.tid;
        tuple = event->u.RcvErrorPDU.addr_tuple;
    break;
/*
 * When we are receiving an invoke requirement, we must create a new trans-
 * action and generate a new tid. This can be wrapped, and should have its 
 * first bit turned.
 */
    case TR_Invoke_Req:
	++tid;
        if (tid_wrapped(tid)) {
	    tidnew = 1;
            tid = 0;
        }
                   
	tid = rcv_tid(tid);
        tuple = event->u.TR_Invoke_Req.addr_tuple;
        mid = event->u.TR_Invoke_Req.handle;
    break;

    case TR_Abort_Req:
        tid = event->u.TR_Abort_Req.handle;
    break;

    case TimerTO_R:
        mid = event->u.TimerTO_R.handle;
    break;

    default:
	error(0, "WTP_INIT: machine_find_or_create: unhandled event");
        wap_event_dump(event);
        return NULL;
    }

    gw_assert(tuple != NULL || mid != -1);
    machine = init_machine_find(tuple, tid, mid);

    if (machine == NULL){

	switch (event->type){
	case RcvAck:   
   
/* 
 * Case nro 2 If we do not have a tid asked for, we send a negative answer, 
 * i.e. an abort with reason INVALIDTID. 
 */
	     if (event->u.RcvAck.tid_ok) {
		 dispatch_to_wdp(wtp_pack_abort(PROVIDER, INVALIDTID,
                                                tid, tuple));
             }

/* Case nro 3, normal ack */
             else
                 info(0, "WTP_INIT: machine_find_or_create: ack "
                     "received, yet having no machine");
	break;

/* Case nro 3, abort */
        case RcvAbort:
            info(0, "WTP_INIT: machine_find_or_create: abort "
                 "received, yet having no machine");
	break;

	case TR_Invoke_Req:
	    machine = init_machine_create(tuple, tid, tidnew);
            machine->mid = event->u.TR_Invoke_Req.handle;
	break;

	case TR_Abort_Req:
            error(0, "WTP_INIT: machine_find_or_create: WSP "
                  "primitive to a wrong WTP machine");
	break;

	case TimerTO_R:
	    error(0, "WTP_INIT: machine_find_or_create: timer "
                       "event without a corresponding machine");
        break;
       
        default:
            error(0, "WTP_INIT: machine_find_or_create: unhandled"
                  "event");
            wap_event_dump(event);
        break; 
        }
   } 

   return machine;
}

/*
 * Creates TR-Invoke.cnf event
 */
static WAPEvent *create_tr_invoke_cnf(WTPInitMachine *init_machine)
{
    WAPEvent *event;

    gw_assert(init_machine != NULL);
    event = wap_event_create(TR_Invoke_Cnf);
    event->u.TR_Invoke_Cnf.handle = init_machine->mid;
    event->u.TR_Invoke_Cnf.addr_tuple = 
        wap_addr_tuple_duplicate(init_machine->addr_tuple);

    return event;
}

/*
 * Creates TR-Abort.ind event from an initiator state machine. In addtion, set
 * the ir_flag on.
 */
static WAPEvent *create_tr_abort_ind(WTPInitMachine *sm, long abort_reason) 
{
    WAPEvent *event;
	
    event = wap_event_create(TR_Abort_Ind);

    event->u.TR_Abort_Ind.abort_code = abort_reason;
    event->u.TR_Abort_Ind.addr_tuple = 
	wap_addr_tuple_duplicate(sm->addr_tuple);
    event->u.TR_Abort_Ind.handle = sm->mid;
    event->u.TR_Abort_Ind.ir_flag = INITIATOR_INDICATION;

    return event;
}


static int tid_wrapped(unsigned short tid)
{
    return tid > (1 << 15);
}

static unsigned short rcv_tid(unsigned short tid)
{
    return tid ^ 0x8000;
}

/*
 * Start retry interval timer (strictly speaking, timer iniatilised with retry
 * interval). Multiply timer value with init_timer_freq.
 */
static void start_initiator_timer_R(WTPInitMachine *machine) 
{
    WAPEvent *timer_event;
    int seconds;

    timer_event = wap_event_create(TimerTO_R);
    timer_event->u.TimerTO_R.handle = machine->mid;
    if (machine->u_ack)
        seconds = S_R_WITH_USER_ACK * init_timer_freq;
    else
        seconds = S_R_WITHOUT_USER_ACK * init_timer_freq;
    gwtimer_start(machine->timer, seconds, timer_event);
}

static void stop_initiator_timer(Timer *timer)
{
    debug("wap.wtp_init", 0, "stopping timer");
    gw_assert(timer);
    gwtimer_stop(timer);
}

static void send_abort(WTPInitMachine *machine, long type, long reason)
{
    WAPEvent *e;

    e = wtp_pack_abort(type, reason, machine->tid, machine->addr_tuple);
    dispatch_to_wdp(e);
}

static void send_ack(WTPInitMachine *machine, long ack_type, int rid_flag)
{
    WAPEvent *e;

    e = wtp_pack_ack(ack_type, rid_flag, machine->tid, machine->addr_tuple);
    dispatch_to_wdp(e);
}
