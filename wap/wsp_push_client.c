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
 * wsp_push_client.c: Client WSP Push implementation, for testing purposes
 *
 * Aarno Syvänen for Wapit Ltd
 */

#include "wsp_push_client.h"
#include "wsp.h"
#include "wtp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"
#include "wap.h"

/**************************************************************************
 * 
 * Internal data structures:
 *
 * List of client WSP push machines.
 */
static List *push_client_machines = NULL;

/*
 * Counter for client push machine id numbers, to make sure that they are 
 * unique.
 */
static Counter *push_client_machine_id_counter = NULL;

/*
 * Give the status of push client:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum {limbo, running, terminating } push_client_run_status = limbo;

/*
 * Queue of events to be handled by the push client.
 */
static List *push_client_queue = NULL;

wap_dispatch_func_t *dispatch_to_self;
wap_dispatch_func_t *dispatch_to_wtp_resp;

/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Create and destroy an uniniatilised push client state machine.
 */
static WSPPushClientMachine *push_client_machine_create(long cpid);
static void push_client_machine_destroy(void *a);

/*
 * Checks whether the client push machines list includes a specific machine.
 * Creates it, if the event is TR-Invoke.ind
 */
static WSPPushClientMachine *push_client_machine_find_or_create(WAPEvent *e);

/*
 * Feed an event to the client push state machine. Do not report errors to
 * caller.
 */
static void push_client_event_handle(WSPPushClientMachine *cpm, WAPEvent *e);

/*
 * Print WSP client push machine state as a string.
 */
static unsigned char *name_push_client_state(int name);
static void main_thread(void *);
static WAPEvent *indicate_confirmedpush(WSPPushClientMachine *cpm, 
                                            Octstr *push_body);
static WAPEvent *indicate_pushabort(WSPPushClientMachine *cpm, 
                                        long abort_reason);
static WAPEvent *response_confirmedpush(WSPPushClientMachine *cpm);
static WAPEvent *send_abort_to_responder(WSPPushClientMachine *cpm, long reason);
static WAPEvent *response_responder_invoke(WSPPushClientMachine *cpm);


/**************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */

void wsp_push_client_init(wap_dispatch_func_t *dispatch_self, 
                          wap_dispatch_func_t *dispatch_wtp_resp)
{
    push_client_machines = gwlist_create();
    push_client_machine_id_counter = counter_create();

    push_client_queue = gwlist_create();
    gwlist_add_producer(push_client_queue);

    dispatch_to_self = dispatch_self;
    dispatch_to_wtp_resp = dispatch_wtp_resp;

    gw_assert(push_client_run_status == limbo);
    push_client_run_status = running;
    gwthread_create(main_thread, NULL);
}

void wsp_push_client_shutdown(void)
{
    gw_assert(push_client_run_status == running);
    push_client_run_status = terminating;
    gwlist_remove_producer(push_client_queue);
    gwthread_join_every(main_thread);

    debug("wap.wsp", 0, "wsp_push_client_shutdown: %ld push client machines"
          "left", gwlist_len(push_client_machines));
    gwlist_destroy(push_client_machines, push_client_machine_destroy);
    gwlist_destroy(push_client_queue, wap_event_destroy_item);

    counter_destroy(push_client_machine_id_counter);
}

void wsp_push_client_dispatch_event(WAPEvent *e)
{
    gwlist_produce(push_client_queue, e);
}

/***************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 */

static void main_thread(void *arg)
{
    WSPPushClientMachine *cpm;
    WAPEvent *e;

    while (push_client_run_status == running &&
            (e = gwlist_consume(push_client_queue)) != NULL) {
         cpm = push_client_machine_find_or_create(e);
         if (cpm == NULL)
	     wap_event_destroy(e);
         else
	     push_client_event_handle(cpm, e);
    }
}

/*
 * Give the name of a push client machine state in a readable form
 */
static unsigned char *name_push_client_state(int n) {

    switch (n) {
    #define PUSH_CLIENT_STATE_NAME(state) case state : return (unsigned char *)#state;
    #define ROW(state, event, condition, action, new_state)
    #include "wsp_push_client_states.def"
    default:
        return (unsigned char *)"unknown state";
    }
}

/*
 * Feed an event to a WSP push client state machine. Do not report errors to
 * the caller.
 */
static void push_client_event_handle(WSPPushClientMachine *cpm, 
                                     WAPEvent *e)
{
    WAPEvent *wtp_event;
    WSP_PDU *pdu = NULL;

    wap_event_assert(e);
    gw_assert(cpm);
    
    if (e->type == TR_Invoke_Ind) {
        pdu = wsp_pdu_unpack(e->u.TR_Invoke_Ind.user_data);
/*
 * Class 1 tests here
 * Case 4, no session matching address quadruplet, handled by the session mach-
 * ine.
 * Tests from table WSP, page 45. Case 5, a PDU state tables cannot handle.
 */
        if (pdu == NULL || pdu->type != ConfirmedPush) {
	    wap_event_destroy(e);
            wtp_event = send_abort_to_responder(cpm, PROTOERR);
            wtp_resp_dispatch_event(wtp_event);
            return;
        }
    }

    debug("wap.wsp", 0, "WSP_PUSH: WSPPushClientMachine %ld, state %s,"
          " event %s", 
          cpm->client_push_id,
          name_push_client_state(cpm->state),
          wap_event_name(e->type));
    #define PUSH_CLIENT_STATE_NAME(state)
    #define ROW(push_state, event_type, condition, action, next_state) \
         if (cpm->state == push_state && \
             e->type == event_type && \
             (condition)) { \
             action \
             cpm->state = next_state; \
             debug("wap.wsp", 0, "WSP_PUSH %ld: new state %s", \
                   cpm->client_push_id, #next_state); \
         } else
    #include "wsp_push_client_states.def"
         {
             error(0, "WSP_PUSH: handle_event: unhandled event!");
             debug("wap.wsp", 0, "Unhandled event was:");
             wap_event_dump(e);
             wap_event_destroy(e);
             return;
         }

    wsp_pdu_destroy(pdu);
    wap_event_destroy(e);
    
    if (cpm->state == PUSH_CLIENT_NULL_STATE)
        push_client_machine_destroy(cpm);
}

static int push_client_machine_has_transid(void *a, void *b)
{
    long transid;
    WSPPushClientMachine *m;

    m = a;
    transid = *(long *)b;
    return m->transaction_id == transid;   
}

static WSPPushClientMachine *push_client_machine_find_using_transid(
           long transid)
{
    WSPPushClientMachine *m;
  
    m = gwlist_search(push_client_machines, &transid, 
                    push_client_machine_has_transid);
    return m;
}

/*
 * Checks client push machines list for a specific machine. Creates it, if the
 * event is TR-Invoke.ind.
 * Client push machine is identified (when searching) by transcation identifi-
 * er. 
 * Note that only WTP responder send its class 1 messages to client push state
 * machine. So, it is no need to specify WTP machine type. 
 */
static WSPPushClientMachine *push_client_machine_find_or_create(WAPEvent *e)
{
    WSPPushClientMachine *cpm;
    long transid;

    cpm = NULL;
    transid = -1;
   
    switch (e->type) {
    case TR_Invoke_Ind:
         transid = e->u.TR_Invoke_Ind.handle;
    break;

    case S_ConfirmedPush_Res:
         transid = e->u.S_ConfirmedPush_Res.client_push_id;
    break;

    case S_PushAbort_Req:
         transid = e->u.S_PushAbort_Req.push_id;
    break;

    case Abort_Event:
    break;

    case TR_Abort_Ind:
         transid = e->u.TR_Abort_Ind.handle;
    break;

    default:
        debug("wap.wsp", 0, "WSP PUSH: push_client_find_or_create: unhandled"
	      " event");
        wap_event_dump(e);
        wap_event_destroy(e);
        return NULL;
    }

    gw_assert(transid != -1);

    cpm = push_client_machine_find_using_transid(transid);
    
    if (cpm == NULL) {
        switch (e->type) {
        case TR_Invoke_Ind:
	    cpm = push_client_machine_create(transid);
	break;

        case S_ConfirmedPush_Res:
        case S_PushAbort_Req:
	    error(0, "WSP_PUSH_CLIENT: POT primitive to a nonexisting"
                  "  push client machine");
	break;

        case Abort_Event:
	    error(0, "WSP_PUSH_CLIENT: internal abort to a nonexisting"
                  " push client machine");
	break;

        case TR_Abort_Ind:
	    error(0, "WSP_PUSH_CLIENT: WTP abort to a nonexisting push client"
                  " machine");
	break;

	default:
	    error(0, "WSP_PUSH_CLIENT: Cannot handle event type %s",
		wap_event_name(e->type));
	break;
        }
    }

    return cpm;
}

static WSPPushClientMachine *push_client_machine_create(long transid)
{
    WSPPushClientMachine *m;

    m = gw_malloc(sizeof(WSPPushClientMachine));
    debug("wap.wsp", 0, "WSP_PUSH_CLIENT: Created WSPPushClientMachine %p",
          (void *) m);

    #define INTEGER(name) m->name = 0;
    #define HTTPHEADERS(name) m->name = NULL;
    #define MACHINE(fields) fields
    #include "wsp_push_client_machine.def"

    m->state = PUSH_CLIENT_NULL_STATE;
    m->transaction_id = transid;
    m->client_push_id = counter_increase(push_client_machine_id_counter);

    gwlist_append(push_client_machines, m);

    return m;
}

static void push_client_machine_destroy(void *a) 
{
    WSPPushClientMachine *m;

    m = a;
    debug("wap.wsp", 0, "Destroying WSPPushClientMachine %p", (void *) m);
    gwlist_delete_equal(push_client_machines, m); 

    #define INTEGER(name) m->name = 0;
    #define HTTPHEADERS(name) http_destroy_headers(m->name);  
    #define MACHINE(fields) fields;
    #include "wsp_push_client_machine.def"

    gw_free(m);
}


static WAPEvent *indicate_confirmedpush(WSPPushClientMachine *cpm, 
                                            Octstr *push_body)
{
    WAPEvent *e;

    e = wap_event_create(S_ConfirmedPush_Ind);
    e->u.S_ConfirmedPush_Ind.client_push_id = cpm->client_push_id;
    e->u.S_ConfirmedPush_Ind.push_headers = 
        http_header_duplicate(cpm->push_headers);
    e->u.S_ConfirmedPush_Ind.push_body = octstr_duplicate(push_body);

    return e;
}

static WAPEvent *indicate_pushabort(WSPPushClientMachine *cpm, 
                                        long abort_reason)
{
    WAPEvent *e;

    e = wap_event_create(S_PushAbort_Ind);
    e->u.S_PushAbort_Ind.push_id = cpm->client_push_id;
    e->u.S_PushAbort_Ind.reason = abort_reason;

    return e;
}


/*
 * For debugging: create S-ConfirmedPush.res by ourselves. 
 */
static WAPEvent *response_confirmedpush(WSPPushClientMachine *cpm)
{
    WAPEvent *e;

    e = wap_event_create(S_ConfirmedPush_Res);
    e->u.S_ConfirmedPush_Res.client_push_id = cpm->client_push_id;

    return e;
}

static WAPEvent *send_abort_to_responder(WSPPushClientMachine *cpm, 
                                         long reason)
{
    WAPEvent *e;

    e = wap_event_create(TR_Abort_Req);
    e->u.TR_Abort_Req.abort_type = USER;
    e->u.TR_Abort_Req.abort_reason = reason;
    e->u.TR_Abort_Req.handle = cpm->client_push_id;

    return e;
}


static WAPEvent *response_responder_invoke(WSPPushClientMachine *cpm)
{
    WAPEvent *e;

    e = wap_event_create(TR_Invoke_Res);
    e->u.TR_Invoke_Res.handle = cpm->transaction_id;

    return e;
}




