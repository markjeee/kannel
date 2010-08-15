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
 * wsp_session.c - Implement WSP session oriented service
 *
 * Lars Wirzenius
 * Stipe Tolj
 */


#include <string.h>
#include <limits.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"
#include "wsp_caps.h"
#include "wsp_strings.h"
#include "cookies.h"
#include "wap.h"
#include "wtp.h"


typedef enum {
	#define STATE_NAME(name) name,
	#define ROW(state, event, condition, action, next_state)
	#include "wsp_server_session_states.def"

	#define STATE_NAME(name) name,
	#define ROW(state, event, condition, action, next_state)
	#include "wsp_server_method_states.def"

        #define STATE_NAME(name) name,
        #define ROW(state, event, condition, action, next_state)
        #include "wsp_server_push_states.def"

	WSPState_count
} WSPState;


/*
 * Give the status the module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } run_status = limbo;

static wap_dispatch_func_t *dispatch_to_wtp_resp;
static wap_dispatch_func_t *dispatch_to_wtp_init;
static wap_dispatch_func_t *dispatch_to_appl;
static wap_dispatch_func_t *dispatch_to_ota;

/*
 * True iff "Session resume facility" is enabled.  This means we are
 * willing to let sessions go to SUSPENDED state, and later resume them.
 * Currently we always support it, but this may become configurable
 * at some point.
 */

static int resume_enabled = 1;

static List *queue = NULL;
static List *session_machines = NULL;
static Counter *session_id_counter = NULL;


static WSPMachine *find_session_machine(WAPEvent *event, WSP_PDU *pdu);
static void handle_session_event(WSPMachine *machine, WAPEvent *event, 
				 WSP_PDU *pdu);
static WSPMachine *machine_create(void);
static void machine_destroy(void *p);

static void handle_method_event(WSPMachine *session, WSPMethodMachine *machine, WAPEvent *event, WSP_PDU *pdu);
static void cant_handle_event(WSPMachine *sm, WAPEvent *event);
static WSPMethodMachine *method_machine_create(WSPMachine *, long);
static void method_machine_destroy(void *msm);

static void handle_push_event(WSPMachine *session, WSPPushMachine *machine,
                              WAPEvent *e);
static WSPPushMachine *push_machine_create(WSPMachine *session, long id);
static void push_machine_destroy(void *p);

static char *state_name(WSPState state);
static unsigned long next_wsp_session_id(void);

static List *make_capabilities_reply(WSPMachine *m);
static List *make_reply_headers(WSPMachine *m);
static Octstr *make_connectreply_pdu(WSPMachine *m);
static Octstr *make_resume_reply_pdu(WSPMachine *m, List *headers);
static WSP_PDU *make_confirmedpush_pdu(WAPEvent *e);
static WSP_PDU *make_push_pdu(WAPEvent *e);

static int transaction_belongs_to_session(void *session, void *tuple);
static int find_by_session_id(void *session, void *idp);
static int same_client(void *sm1, void *sm2);
static WSPMethodMachine *find_method_machine(WSPMachine *, long id);
static WSPPushMachine *find_push_machine(WSPMachine *m, long id);

static List *unpack_new_headers(WSPMachine *sm, Octstr *hdrs);

static void disconnect_other_sessions(WSPMachine *sm);
static void send_abort(long reason, long handle);
static void indicate_disconnect(WSPMachine *sm, long reason);
static void indicate_suspend(WSPMachine *sm, long reason);
static void indicate_resume(WSPMachine *sm, WAPAddrTuple *tuple, 
                            List *client_headers);

static void release_holding_methods(WSPMachine *sm);
static void abort_methods(WSPMachine *sm, long reason);
static void abort_pushes(WSPMachine *sm, long reason);

static void method_abort(WSPMethodMachine *msm, long reason);
static void indicate_method_abort(WSPMethodMachine *msm, long reason);

static WAPEvent *make_abort(long reason, long handle);
static void send_invoke(WSPMachine *session, WSP_PDU *pdu, WAPEvent *e,
                        long class);
static void send_abort_to_initiator(long reason, long handle);
static void indicate_pushabort(WSPPushMachine *machine, long reason);
static void confirm_push(WSPPushMachine *machine);

static void main_thread(void *);
static int id_belongs_to_session (void *, void *);
static int wsp_encoding_string_to_version(Octstr *enc);
static Octstr *wsp_encoding_version_to_string(int version);


/***********************************************************************
 * Public functions.
 */


void wsp_session_init(wap_dispatch_func_t *responder_dispatch,
                      wap_dispatch_func_t *initiator_dispatch,
                      wap_dispatch_func_t *application_dispatch,
                      wap_dispatch_func_t *push_ota_dispatch) {
	queue = gwlist_create();
	gwlist_add_producer(queue);
	session_machines = gwlist_create();
	session_id_counter = counter_create();
	dispatch_to_wtp_resp = responder_dispatch;
	dispatch_to_wtp_init = initiator_dispatch;
	dispatch_to_appl = application_dispatch;
        dispatch_to_ota = push_ota_dispatch;
        wsp_strings_init();
	run_status = running;
	gwthread_create(main_thread, NULL);
}


void wsp_session_shutdown(void) {
	gw_assert(run_status == running);
	run_status = terminating;
	gwlist_remove_producer(queue);
	gwthread_join_every(main_thread);

	gwlist_destroy(queue, wap_event_destroy_item);

	debug("wap.wsp", 0, "WSP: %ld session machines left.",
		gwlist_len(session_machines));
	gwlist_destroy(session_machines, machine_destroy);

	counter_destroy(session_id_counter);
        wsp_strings_shutdown();
}


void wsp_session_dispatch_event(WAPEvent *event) {
	wap_event_assert(event);
	gwlist_produce(queue, event);
}


/***********************************************************************
 * Local functions
 */


static void main_thread(void *arg) {
	WAPEvent *e;
	WSPMachine *sm;
	WSP_PDU *pdu;
	
	while (run_status == running && (e = gwlist_consume(queue)) != NULL) {
		wap_event_assert(e);
		switch (e->type) {
		case TR_Invoke_Ind:
			pdu = wsp_pdu_unpack(e->u.TR_Invoke_Ind.user_data);
			if (pdu == NULL) {
				warning(0, "WSP: Broken PDU ignored.");
				wap_event_destroy(e);
				continue;
			}
			break;
	
		default:
			pdu = NULL;
			break;
		}
	
		sm = find_session_machine(e, pdu);
		if (sm == NULL) {
			wap_event_destroy(e);
		} else {
			handle_session_event(sm, e, pdu);
		}
		
		wsp_pdu_destroy(pdu);
	}
}


static WSPMachine *find_session_machine(WAPEvent *event, WSP_PDU *pdu) {
	WSPMachine *sm;
	long session_id;
	WAPAddrTuple *tuple;
	
	tuple = NULL;
	session_id = -1;
	
	switch (event->type) {
	case TR_Invoke_Ind:
		tuple = wap_addr_tuple_duplicate(
				event->u.TR_Invoke_Ind.addr_tuple);
		break;

        case TR_Invoke_Cnf:
                tuple = wap_addr_tuple_duplicate(
				event->u.TR_Invoke_Cnf.addr_tuple);
	        break;

	case TR_Result_Cnf:
		tuple = wap_addr_tuple_duplicate(
				event->u.TR_Result_Cnf.addr_tuple);
		break;

	case TR_Abort_Ind:
		tuple = wap_addr_tuple_duplicate(
				event->u.TR_Abort_Ind.addr_tuple);
		break;

	case S_Connect_Res:
		session_id = event->u.S_Connect_Res.session_id;
		break;

	case S_Resume_Res:
		session_id = event->u.S_Resume_Res.session_id;
		break;

	case Disconnect_Event:
		session_id = event->u.Disconnect_Event.session_handle;
		break;

	case Suspend_Event:
		session_id = event->u.Suspend_Event.session_handle;
		break;

	case S_MethodInvoke_Res:
		session_id = event->u.S_MethodInvoke_Res.session_id;
		break;

	case S_MethodResult_Req:
		session_id = event->u.S_MethodResult_Req.session_id;
		break;

	case S_ConfirmedPush_Req:
                session_id = event->u.S_ConfirmedPush_Req.session_id;
	        break;

        case S_Push_Req:
                session_id = event->u.S_Push_Req.session_id;
	        break;

	default:
		error(0, "WSP: Cannot find machine for %s event",
			wap_event_name(event->type));
	}
	
	gw_assert(tuple != NULL || session_id != -1);

	/* Pre-state-machine tests, according to 7.1.5.  After the tests,
	 * caller will pass the event to sm if sm is not NULL. */
	sm = NULL;
	/* First test is for MRUEXCEEDED, and we don't have a MRU */

	/* Second test is for class 2 TR-Invoke.ind with Connect PDU */
	if (event->type == TR_Invoke_Ind &&
	    event->u.TR_Invoke_Ind.tcl == 2 &&
	    pdu->type == Connect) {
			/* Create a new session, even if there is already
			 * a session open for this address.  The new session
			 * will take care of killing the old ones. */
			sm = machine_create();
			gw_assert(tuple != NULL);
			sm->addr_tuple = wap_addr_tuple_duplicate(tuple);
			sm->connect_handle = event->u.TR_Invoke_Ind.handle;
	/* Third test is for class 2 TR-Invoke.ind with Resume PDU */
	} else if (event->type == TR_Invoke_Ind &&
		   event->u.TR_Invoke_Ind.tcl == 2 &&
	  	   pdu->type == Resume) {
		/* Pass to session identified by session id, not
		 * the address tuple. */
		session_id = pdu->u.Resume.sessionid;
		sm = gwlist_search(session_machines, &session_id,
				find_by_session_id);
		if (sm == NULL) {
			/* No session; TR-Abort.req(DISCONNECT) */
			send_abort(WSP_ABORT_DISCONNECT,
				event->u.TR_Invoke_Ind.handle);
		}
	/* Fourth test is for a class 1 or 2 TR-Invoke.Ind with no
	 * session for that address tuple.  We also handle class 0
	 * TR-Invoke.ind here by ignoring them; this seems to be
	 * an omission in the spec table. */
	} else if (event->type == TR_Invoke_Ind) {
		sm = gwlist_search(session_machines, tuple,
				 transaction_belongs_to_session);
		if (sm == NULL && (event->u.TR_Invoke_Ind.tcl == 1 ||
				event->u.TR_Invoke_Ind.tcl == 2)) {
			send_abort(WSP_ABORT_DISCONNECT,
				event->u.TR_Invoke_Ind.handle);
		}
	/* Other tests are for events not handled by the state tables;
	 * do those later, after we've tried to handle them. */
	} else {
		if (session_id != -1) {
			sm = gwlist_search(session_machines, &session_id,
				find_by_session_id);
		} else {
			sm = gwlist_search(session_machines, tuple,
				transaction_belongs_to_session);
		}
		/* The table doesn't really say what we should do with
		 * non-Invoke events for which there is no session.  But
		 * such a situation means there is an error _somewhere_
		 * in the gateway. */
		if (sm == NULL) {
			error(0, "WSP: Cannot find session machine for event.");
			wap_event_dump(event);
		}
	}

	wap_addr_tuple_destroy(tuple);
	return sm;
}


static void handle_session_event(WSPMachine *sm, WAPEvent *current_event, 
WSP_PDU *pdu) {
	debug("wap.wsp", 0, "WSP: machine %p, state %s, event %s",
		(void *) sm,
		state_name(sm->state), 
		wap_event_name(current_event->type));

	#define STATE_NAME(name)
	#define ROW(state_name, event, condition, action, next_state) \
		{ \
			struct event *e; \
			e = &current_event->u.event; \
			if (sm->state == state_name && \
			   current_event->type == event && \
			   (condition)) { \
				action \
				sm->state = next_state; \
				debug("wap.wsp", 0, "WSP %ld: New state %s", \
					sm->session_id, #next_state); \
				goto end; \
			} \
		}
	#include "wsp_server_session_states.def"
	
	cant_handle_event(sm, current_event);

end:
	wap_event_destroy(current_event);

	if (sm->state == NULL_SESSION)
		machine_destroy(sm);
}


static void cant_handle_event(WSPMachine *sm, WAPEvent *event) {
	/* We do the rest of the pre-state-machine tests here.  The first
	 * four were done in find_session_machine().  The fifth is a
	 * class 1 or 2 TR-Invoke.ind not handled by the state tables. */
	if (event->type == TR_Invoke_Ind &&
	    (event->u.TR_Invoke_Ind.tcl == 1 ||
	     event->u.TR_Invoke_Ind.tcl == 2)) {
		warning(0, "WSP: Can't handle TR-Invoke.ind, aborting transaction.");
		debug("wap.wsp", 0, "WSP: The unhandled event:");
		wap_event_dump(event);
		send_abort(WSP_ABORT_PROTOERR,
			event->u.TR_Invoke_Ind.handle);
	/* The sixth is a class 0 TR-Invoke.ind not handled by state tables. */
	} else if (event->type == TR_Invoke_Ind) {
		warning(0, "WSP: Can't handle TR-Invoke.ind, ignoring.");
		debug("wap.wsp", 0, "WSP: The ignored event:");
		wap_event_dump(event);
	/* The seventh is any other event not handled by state tables. */
	} else {
		error(0, "WSP: Can't handle event. Aborting session.");
		debug("wap.wsp", 0, "WSP: The unhandled event:");
		wap_event_dump(event);
		/* TR-Abort.req(PROTOERR) if it is some other transaction
		 * event than abort. */
		/* Currently that means TR-Result.cnf, because we already
		 * tested for Invoke. */
		/* FIXME We need a better way to get at event values than
		 * by hardcoding the types. */
		if (event->type == TR_Result_Cnf) {
			send_abort(WSP_ABORT_PROTOERR,
				event->u.TR_Result_Cnf.handle);
		}
		/* Abort(PROTOERR) all method and push transactions */
		abort_methods(sm, WSP_ABORT_PROTOERR);
                abort_pushes(sm, WSP_ABORT_PROTOERR);
		/* S-Disconnect.ind(PROTOERR) */
		indicate_disconnect(sm, WSP_ABORT_PROTOERR);
	}
}


static WSPMachine *machine_create(void) {
	WSPMachine *p;
	
	p = gw_malloc(sizeof(WSPMachine));
	debug("wap.wsp", 0, "WSP: Created WSPMachine %p", (void *) p);
	
	#define INTEGER(name) p->name = 0;
	#define OCTSTR(name) p->name = NULL;
	#define HTTPHEADERS(name) p->name = NULL;
	#define ADDRTUPLE(name) p->name = NULL;
	#define MACHINESLIST(name) p->name = gwlist_create();
	#define CAPABILITIES(name) p->name = NULL;
	#define COOKIES(name) p->name = gwlist_create();
	#define REFERER(name) p->name = NULL;
	#define MACHINE(fields) fields
	#include "wsp_server_session_machine.def"
	
	p->state = NULL_SESSION;

	/* set capabilities to default values (defined in 1.1) */

	p->client_SDU_size = 1400;
	p->MOR_push = 1;
	
	/* Insert new machine at the _front_, because 1) it's more likely
	 * to get events than old machines are, so this speeds up the linear
	 * search, and 2) we want the newest machine to get any method
	 * invokes that come through before the Connect is established. */
	gwlist_insert(session_machines, 0, p);

	return p;
}


static void destroy_methodmachines(List *machines) {
	if (gwlist_len(machines) > 0) {
		warning(0, "Destroying WSP session with %ld active methods\n",
			gwlist_len(machines));
	}

	gwlist_destroy(machines, method_machine_destroy);
}

static void destroy_pushmachines(List *machines) {
	if (gwlist_len(machines) > 0) {
		warning(0, "Destroying WSP session with %ld active pushes\n",
			gwlist_len(machines));
	}

	gwlist_destroy(machines, push_machine_destroy);
}

static void machine_destroy(void *pp) {
	WSPMachine *p;
	
	p = pp;
	debug("wap.wsp", 0, "Destroying WSPMachine %p", pp);
	gwlist_delete_equal(session_machines, p);

	#define INTEGER(name) p->name = 0;
	#define OCTSTR(name) octstr_destroy(p->name);
	#define HTTPHEADERS(name) http_destroy_headers(p->name);
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(p->name);
	#define MACHINESLIST(name) destroy_##name(p->name);
	#define CAPABILITIES(name) wsp_cap_destroy_list(p->name);
	#define COOKIES(name) cookies_destroy(p->name);
	#define REFERER(name) octstr_destroy(p->name);
	#define MACHINE(fields) fields
	#include "wsp_server_session_machine.def"
	gw_free(p);
}


struct msm_pattern {
	WAPAddrTuple *addr_tuple;
	long msmid, tid;
};


/* This function does NOT consume its event; it leaves that task up
 * to the parent session */
static void handle_method_event(WSPMachine *sm, WSPMethodMachine *msm, 
WAPEvent *current_event, WSP_PDU *pdu) {

	if (msm == NULL) {
		warning(0, "No method machine for event.");
		wap_event_dump(current_event);
		return;
	}
		
	debug("wap.wsp", 0, "WSP: method %ld, state %s, event %s",
		msm->transaction_id, state_name(msm->state), 
		wap_event_name(current_event->type));

	gw_assert(sm->session_id == msm->session_id);

	#define STATE_NAME(name)
	#define ROW(state_name, event, condition, action, next_state) \
		{ \
			struct event *e; \
			e = &current_event->u.event; \
			if (msm->state == state_name && \
			   current_event->type == event && \
			   (condition)) { \
				action \
				msm->state = next_state; \
				debug("wap.wsp", 0, "WSP %ld/%ld: New method state %s", \
					msm->session_id, msm->transaction_id, #next_state); \
				goto end; \
			} \
		}
	#include "wsp_server_method_states.def"
	
	cant_handle_event(sm, current_event);

end:
	if (msm->state == NULL_METHOD) {
		method_machine_destroy(msm);
		gwlist_delete_equal(sm->methodmachines, msm);
	}
}


static WSPMethodMachine *method_machine_create(WSPMachine *sm,
			long wtp_handle) {
	WSPMethodMachine *msm;
	
	msm = gw_malloc(sizeof(*msm));
	
	#define INTEGER(name) msm->name = 0;
	#define ADDRTUPLE(name) msm->name = NULL;
	#define EVENT(name) msm->name = NULL;
	#define MACHINE(fields) fields
	#include "wsp_server_method_machine.def"
	
	msm->transaction_id = wtp_handle;
	msm->state = NULL_METHOD;
	msm->addr_tuple = wap_addr_tuple_duplicate(sm->addr_tuple);
	msm->session_id = sm->session_id;

	gwlist_append(sm->methodmachines, msm);

	return msm;
}



static void method_machine_destroy(void *p) {
	WSPMethodMachine *msm;

	if (p == NULL)
		return;

	msm = p;

	debug("wap.wsp", 0, "Destroying WSPMethodMachine %ld",
			msm->transaction_id);

	#define INTEGER(name)
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(msm->name);
	#define EVENT(name) wap_event_destroy(msm->name);
	#define MACHINE(fields) fields
	#include "wsp_server_method_machine.def"

	gw_free(msm);
}

static void handle_push_event(WSPMachine *sm, WSPPushMachine *pm, 
                              WAPEvent *current_event)
{
        if (pm == NULL) {
		warning(0, "No push machine for event.");
		wap_event_dump(current_event);
		return;
	}

        debug("wap.wsp", 0, "WSP(tid/pid): push %ld/%ld, state %s, event %s",
		pm->transaction_id, pm->server_push_id, state_name(pm->state), 
		wap_event_name(current_event->type));
	gw_assert(sm->session_id == pm->session_id);

        #define STATE_NAME(name)
	#define ROW(state_name, event, condition, action, next_state) \
		{ \
		    if (pm->state == state_name && \
			current_event->type == event && \
			(condition)) { \
			     action \
			     pm->state = next_state; \
			     debug("wap.wsp", 0, "WSP %ld/%ld: New push state %s", \
			           pm->session_id, pm->transaction_id, #next_state); \
				goto end; \
			} \
		}
	#include "wsp_server_push_states.def"

        cant_handle_event(sm, current_event);
end:
        if (pm->state == SERVER_PUSH_NULL_STATE) {
		push_machine_destroy(pm);
		gwlist_delete_equal(sm->pushmachines, pm);
	}
}

static WSPPushMachine *push_machine_create(WSPMachine *sm, 
        long pid)
{
        WSPPushMachine *m;

        m = gw_malloc(sizeof(WSPPushMachine));

        #define INTEGER(name) m->name = 0;
        #define ADDRTUPLE(name) m->name = NULL;
        #define HTTPHEADER(name) m->name = http_create_empty_headers();
        #define MACHINE(fields) fields
        #include "wsp_server_push_machine.def"

        m->server_push_id = pid;
        m->transaction_id = pid;
	m->state = SERVER_PUSH_NULL_STATE;
	m->addr_tuple = wap_addr_tuple_duplicate(sm->addr_tuple);
	m->session_id = sm->session_id;

	gwlist_append(sm->pushmachines, m);

	return m;      
}

static void push_machine_destroy(void *p)
{
        WSPPushMachine *m = NULL;   

	if (p == NULL)
	       return;  
        m = p;
        debug("wap.wsp", 0, "Destroying WSPPushMachine %ld",
			m->transaction_id);
        #define INTEGER(name) 
        #define ADDRTUPLE(name) wap_addr_tuple_destroy(m->name);
        #define HTTPHEADER(name) http_destroy_headers(m->name);
        #define MACHINE(fields) fields
        #include "wsp_server_push_machine.def"

        gw_free(m);
}

static char *state_name(WSPState state) {
	switch (state) {
	#define STATE_NAME(name) case name: return #name;
	#define ROW(state, event, cond, stmt, next_state)
	#include "wsp_server_session_states.def"

	#define STATE_NAME(name) case name: return #name;
	#define ROW(state, event, cond, stmt, next_state)
	#include "wsp_server_method_states.def"

        #define STATE_NAME(name) case name: return #name;
        #define ROW(state, event, cond, stmt, next_state)
        #include "wsp_server_push_states.def"

	default:
		return "unknown wsp state";
	}
}


static unsigned long next_wsp_session_id(void) {
	return counter_increase(session_id_counter);
}


static void sanitize_capabilities(List *caps, WSPMachine *m) {
	long i;
	Capability *cap;
	unsigned long ui;

	for (i = 0; i < gwlist_len(caps); i++) {
		cap = gwlist_get(caps, i);

		/* We only know numbered capabilities.  Let the application
		 * layer negotiate whatever it wants for unknown ones. */
		if (cap->name != NULL)
			continue;

		switch (cap->id) {
		case WSP_CAPS_CLIENT_SDU_SIZE:
			/* Check if it's a valid uintvar.  The value is the
			 * max SDU size we will send, and there's no
			 * internal limit to that, so accept any value. */
			if (cap->data != NULL &&
			    octstr_extract_uintvar(cap->data, &ui, 0) < 0)
				goto bad_cap;
			else
				m->client_SDU_size = ui;
			break;

		case WSP_CAPS_SERVER_SDU_SIZE:
			/* Check if it's a valid uintvar */
			if (cap->data != NULL &&
			    (octstr_extract_uintvar(cap->data, &ui, 0) < 0))
				goto bad_cap;
			/* XXX Our MRU is not quite unlimited, since we
			 * use signed longs in the library functions --
			 * should we make sure we limit the reply value
			 * to LONG_MAX?  (That's already a 2GB packet) */
			break;

		case WSP_CAPS_PROTOCOL_OPTIONS:
			/* Currently we don't support any Push, nor
			 * session resume, nor acknowledgement headers,
			 * so make sure those bits are not set. */
			if (cap->data != NULL && octstr_len(cap->data) > 0
			   && (octstr_get_char(cap->data, 0) & 0xf0) != 0) {
				warning(0, "WSP: Application layer tried to "
					"negotiate protocol options.");
				octstr_set_bits(cap->data, 0, 4, 0);
			}
			break;

		case WSP_CAPS_EXTENDED_METHODS:
			/* XXX Check format here */
			break;

		
		case WSP_CAPS_HEADER_CODE_PAGES:
			/* We don't support any yet, so don't let this
			 * be negotiated. */
			if (cap->data)
				goto bad_cap;
			break;
		}
		continue;

	bad_cap:
		error(0, "WSP: Found illegal value in capabilities reply.");
		wsp_cap_dump(cap);
		gwlist_delete(caps, i, 1);
		i--;
		wsp_cap_destroy(cap);
		continue;
	}
}


static void reply_known_capabilities(List *caps, List *req, WSPMachine *m) {
	unsigned long ui;
	Capability *cap;
	Octstr *data;

	if (wsp_cap_count(caps, WSP_CAPS_CLIENT_SDU_SIZE, NULL) == 0) {
		if (wsp_cap_get_client_sdu(req, &ui) > 0) {
			/* Accept value if it is not silly. */
			if ((ui >= 256 && ui < LONG_MAX) || ui == 0) {
				m->client_SDU_size = ui;
			}
		}
		/* Reply with the client SDU we decided on */
		data = octstr_create("");
		octstr_append_uintvar(data, m->client_SDU_size);
		cap = wsp_cap_create(WSP_CAPS_CLIENT_SDU_SIZE,
			NULL, data);
		gwlist_append(caps, cap);
	}

	if (wsp_cap_count(caps, WSP_CAPS_SERVER_SDU_SIZE, NULL) == 0) {
		/* Accept whatever size the client is willing
		 * to send.  If the client did not specify anything,
		 * then use the default. */
		if (wsp_cap_get_server_sdu(req, &ui) <= 0) {
			ui = 1400;
		}
		data = octstr_create("");
		octstr_append_uintvar(data, ui);
		cap = wsp_cap_create(WSP_CAPS_SERVER_SDU_SIZE, NULL, data);
		gwlist_append(caps, cap);
	}

	/* Currently we cannot handle any protocol options */
	if (wsp_cap_count(caps, WSP_CAPS_PROTOCOL_OPTIONS, NULL) == 0) {
		data = octstr_create("");
		octstr_append_char(data, 0);
		cap = wsp_cap_create(WSP_CAPS_PROTOCOL_OPTIONS, NULL, data);
		gwlist_append(caps, cap);
	}

	/* Accept any Method-MOR the client sent; if it sent none,
	 * use the default. */
	if (wsp_cap_count(caps, WSP_CAPS_METHOD_MOR, NULL) == 0) {
		if (wsp_cap_get_method_mor(req, &ui) <= 0) {
			ui = 1;
		}
		data = octstr_create("");
		octstr_append_char(data, ui);
		cap = wsp_cap_create(WSP_CAPS_METHOD_MOR, NULL, data);
		gwlist_append(caps, cap);
	}

	/* We will never send any Push requests because we don't support
	 * that yet.  But we already specified that in protocol options;
	 * so, pretend we do, and handle the value that way. */
	if (wsp_cap_count(caps, WSP_CAPS_PUSH_MOR, NULL) == 0) {
		if (wsp_cap_get_push_mor(req, &ui) > 0) {
			m->MOR_push = ui;
		}
		data = octstr_create("");
		octstr_append_char(data, m->MOR_push);
		cap = wsp_cap_create(WSP_CAPS_PUSH_MOR, NULL, data);
		gwlist_append(caps, cap);
	}

	/* Supporting extended methods is up to the application layer,
	 * not up to us.  If the application layer didn't specify any,
	 * then we refuse whatever the client requested.  The default
	 * is to support none, so we don't really have to add anything here. */

	/* We do not support any header code pages.  sanitize_capabilities
	 * must have already deleted any reply that indicates otherwise.
	 * Again, not adding anything here is the same as refusing support. */

	/* Listing aliases is something the application layer can do if
	 * it wants to.  We don't care. */
}


/* Generate a refusal for all requested capabilities that are not
 * replied to. */
static void refuse_unreplied_capabilities(List *caps, List *req) {
	long i, len;
	Capability *cap;

	len = gwlist_len(req);
	for (i = 0; i < len; i++) {
		cap = gwlist_get(req, i);
		if (wsp_cap_count(caps, cap->id, cap->name) == 0) {
			cap = wsp_cap_create(cap->id, cap->name, NULL);
			gwlist_append(caps, cap);
		}
	}
}


static int is_default_cap(Capability *cap) {
	unsigned long ui;

	/* All unknown values are empty by default */
	if (cap->name != NULL || cap->id < 0 || cap->id >= WSP_NUM_CAPS)
		return cap->data == NULL || octstr_len(cap->data) == 0;

	switch (cap->id) {
	case WSP_CAPS_CLIENT_SDU_SIZE:
	case WSP_CAPS_SERVER_SDU_SIZE:
		return (cap->data != NULL &&
		    octstr_extract_uintvar(cap->data, &ui, 0) >= 0 &&
		    ui == 1400);
	case WSP_CAPS_PROTOCOL_OPTIONS:
		return cap->data != NULL && octstr_get_char(cap->data, 0) == 0;
	case WSP_CAPS_METHOD_MOR:
	case WSP_CAPS_PUSH_MOR:
		return cap->data != NULL && octstr_get_char(cap->data, 0) == 1;
	case WSP_CAPS_EXTENDED_METHODS:
	case WSP_CAPS_HEADER_CODE_PAGES:
	case WSP_CAPS_ALIASES:
		return cap->data == NULL || octstr_len(cap->data) == 0;
	default:
		return 0;
	}
}


/* Remove any replies that have no corresponding request and that
 * are equal to the default. */
static void strip_default_capabilities(List *caps, List *req) {
	long i;
	Capability *cap;
	int count;

	/* Hmm, this is an O(N*N) operation, which may be bad. */

	i = 0;
	while (i < gwlist_len(caps)) {
		cap = gwlist_get(caps, i);

		count = wsp_cap_count(req, cap->id, cap->name);
		if (count == 0 && is_default_cap(cap)) {
			gwlist_delete(caps, i, 1);
			wsp_cap_destroy(cap);
		} else {
			i++;
		}
	}
}


static List *make_capabilities_reply(WSPMachine *m) {
	List *caps;

	/* In principle, copy the application layer's capabilities
	 * response, add refusals for all unknown requested capabilities,
	 * and add responses for all known capabilities that are
	 * not already responded to.  Then eliminate any replies that
 	 * would have no effect because they are equal to the default. */

	caps = wsp_cap_duplicate_list(m->reply_caps);

	/* Don't let the application layer negotiate anything we
	 * cannot handle.  Also parse the values it set if we're
	 * interested. */
	sanitize_capabilities(caps, m);

	/* Add capability records for all capabilities we know about
	 * that are not already in the reply list. */
	reply_known_capabilities(caps, m->request_caps, m);

	/* All remaining capabilities in the request list that are
	 * not in the reply list at this point must be unknown ones
	 * that we want to refuse. */
	refuse_unreplied_capabilities(caps, m->request_caps);

	/* Now eliminate replies that would be equal to the requested
	 * value, or (if there was none) to the default value. */
	strip_default_capabilities(caps, m->request_caps);

	return caps;
}


static List *make_reply_headers(WSPMachine *m)
{
    List *headers;
    Octstr *encoding_version;

    /* Add all server wsp level hop-by-hop headers. Currently only 
     * Encoding-Version, as defined by wsp, chapter 8.4.2.70. 
     * What headers belong to which version is defined in appendix A,
     * table 39.. 
    encoding_version = request_version = NULL;
     * Essentially, if the client sends us an Encoding-Version
     * higher than ours (1.3) we send our version number to it,
     * if it is lower, we left version number intact. */
    /* First the case that we have no Encoding-Version header at all. 
     * This case we must assume that the client supports version 1.2
     * or lower. */

    headers = http_create_empty_headers();
    encoding_version = wsp_encoding_version_to_string(m->encoding_version);
    http_header_add(headers, "Encoding-Version", octstr_get_cstr(encoding_version));
    octstr_destroy(encoding_version);

    return headers;
}

static Octstr *make_connectreply_pdu(WSPMachine *m) 
{
    WSP_PDU *pdu;
    Octstr *os;
    List *caps;
    List *reply_headers;
	
    pdu = wsp_pdu_create(ConnectReply);

    pdu->u.ConnectReply.sessionid = m->session_id;

    caps = make_capabilities_reply(m);
    pdu->u.ConnectReply.capabilities = wsp_cap_pack_list(caps);
    wsp_cap_destroy_list(caps);

    reply_headers = make_reply_headers(m);
    pdu->u.ConnectReply.headers = 
        wsp_headers_pack(reply_headers, 0, m->encoding_version);
    http_destroy_headers(reply_headers);
	
    os = wsp_pdu_pack(pdu);
    wsp_pdu_destroy(pdu);

    return os;
}


static Octstr *make_resume_reply_pdu(WSPMachine *m, List *headers) 
{
    WSP_PDU *pdu;
    Octstr *os;

    pdu = wsp_pdu_create(Reply);

    /* Not specified for Resume replies */
    pdu->u.Reply.status = wsp_convert_http_status_to_wsp_status(HTTP_OK);
    if (headers == NULL) {
        headers = http_create_empty_headers();
        pdu->u.Reply.headers = wsp_headers_pack(headers, 1, m->encoding_version);
        http_destroy_headers(headers);
    } else {
        pdu->u.Reply.headers = wsp_headers_pack(headers, 1, m->encoding_version);
    }
    pdu->u.Reply.data = octstr_create("");

    os = wsp_pdu_pack(pdu);
    wsp_pdu_destroy(pdu);

    return os;
}

static WSP_PDU *make_confirmedpush_pdu(WAPEvent *e)
{
        WSP_PDU *pdu;
        List *headers;

        pdu = wsp_pdu_create(ConfirmedPush);
/*
 * Both push headers and push body are optional. 
 */
        if (e->u.S_ConfirmedPush_Req.push_headers == NULL) {
	    headers = http_create_empty_headers();
            pdu->u.ConfirmedPush.headers = wsp_headers_pack(headers, 1, WSP_1_2);
            http_destroy_headers(headers);
        } else
            pdu->u.ConfirmedPush.headers = 
                wsp_headers_pack(e->u.S_ConfirmedPush_Req.push_headers, 1, WSP_1_2);
   
        if (e->u.S_ConfirmedPush_Req.push_body == NULL)
	    pdu->u.ConfirmedPush.data = octstr_create("");
        else
	    pdu->u.ConfirmedPush.data = 
                octstr_duplicate(e->u.S_ConfirmedPush_Req.push_body);        

        return pdu;
}

static WSP_PDU *make_push_pdu(WAPEvent *e)
{
        WSP_PDU *pdu;
        List *headers;

        pdu = wsp_pdu_create(Push);
/*
 * Both push headers and push body are optional
 */
        if (e->u.S_Push_Req.push_headers == NULL) {
	    headers = http_create_empty_headers();
            pdu->u.Push.headers = wsp_headers_pack(headers, 1, WSP_1_2);
            http_destroy_headers(headers);
        } else
            pdu->u.Push.headers = 
                wsp_headers_pack(e->u.S_Push_Req.push_headers, 1, WSP_1_2);
   
        if (e->u.S_Push_Req.push_body == NULL)
	    pdu->u.Push.data = octstr_create("");
        else
	    pdu->u.Push.data = 
                octstr_duplicate(e->u.S_Push_Req.push_body);        

        return pdu;
}

static int transaction_belongs_to_session(void *wsp_ptr, void *tuple_ptr) {
	WSPMachine *wsp;
	WAPAddrTuple *tuple;
	
	wsp = wsp_ptr;
	tuple = tuple_ptr;

	return wap_addr_tuple_same(wsp->addr_tuple, tuple);
}


static int find_by_session_id(void *wsp_ptr, void *id_ptr) {
	WSPMachine *wsp = wsp_ptr;
	long *idp = id_ptr;
	
	return wsp->session_id == *idp;
}


static int find_by_method_id(void *wspm_ptr, void *id_ptr) {
	WSPMethodMachine *msm = wspm_ptr;
	long *idp = id_ptr;

	return msm->transaction_id == *idp;
}

static int find_by_push_id(void *m_ptr, void *id_ptr) {
	WSPPushMachine *m = m_ptr;
	long *idp = id_ptr;

	return m->transaction_id == *idp;
}

static WSPMethodMachine *find_method_machine(WSPMachine *sm, long id) {
	return gwlist_search(sm->methodmachines, &id, find_by_method_id);
}

static WSPPushMachine *find_push_machine(WSPMachine *m, long id)
{
       return gwlist_search(m->pushmachines, &id, find_by_push_id);
}

static int same_client(void *a, void *b) {
	WSPMachine *sm1, *sm2;
	
	sm1 = a;
	sm2 = b;
	return wap_addr_tuple_same(sm1->addr_tuple, sm2->addr_tuple);
}


static void disconnect_other_sessions(WSPMachine *sm) {
	List *old_sessions;
	WAPEvent *disconnect;
	WSPMachine *sm2;
	long i;

	old_sessions = gwlist_search_all(session_machines, sm, same_client);
	if (old_sessions == NULL)
		return;

	for (i = 0; i < gwlist_len(old_sessions); i++) {
		sm2 = gwlist_get(old_sessions, i);
		if (sm2 != sm) {
			disconnect = wap_event_create(Disconnect_Event);
			handle_session_event(sm2, disconnect, NULL);
		}
	}

	gwlist_destroy(old_sessions, NULL);
}


static List *unpack_new_headers(WSPMachine *sm, Octstr *hdrs) {
	List *new_headers;

	if (hdrs && octstr_len(hdrs) > 0) {
		new_headers = wsp_headers_unpack(hdrs, 0);
		if (sm->http_headers == NULL)
			sm->http_headers = http_create_empty_headers();
		http_header_combine(sm->http_headers, new_headers);
		return new_headers;
	}
	return NULL;
}

static WAPEvent *make_abort(long reason, long handle)
{
        WAPEvent *wtp_event;

        wtp_event = wap_event_create(TR_Abort_Req);
        wtp_event->u.TR_Abort_Req.abort_type = 0x01;
        wtp_event->u.TR_Abort_Req.abort_reason = reason;
        wtp_event->u.TR_Abort_Req.handle = handle;

        return wtp_event;
}

static void send_abort(long reason, long handle) {
        WAPEvent *wtp_event;

	wtp_event = make_abort(reason, handle);
	dispatch_to_wtp_resp(wtp_event);
}

static void send_abort_to_initiator(long reason, long handle)
{
       WAPEvent *wtp_event;

       wtp_event = make_abort(reason, handle);
       dispatch_to_wtp_init(wtp_event);
}

/*
 * The server sends invoke (to be exact, makes TR-Invoke.req) only when it is 
 * pushing. (Only the client disconnects sessions.)
 */ 
static void send_invoke(WSPMachine *m, WSP_PDU *pdu, WAPEvent *e, long class)
{
        WAPEvent *wtp_event;

        wtp_event = wap_event_create(TR_Invoke_Req);
        wtp_event->u.TR_Invoke_Req.addr_tuple = 
	    wap_addr_tuple_duplicate(m->addr_tuple);
/*
 * There is no mention of acknowledgement type in the specs. But because 
 * confirmed push is confirmed after response from OTA, provider acknowledge-
 * ments seem redundant.
 */
	wtp_event->u.TR_Invoke_Req.up_flag = USER_ACKNOWLEDGEMENT;
        wtp_event->u.TR_Invoke_Req.tcl = class;
        if (e->type == S_ConfirmedPush_Req)
           wtp_event->u.TR_Invoke_Req.handle = 
               e->u.S_ConfirmedPush_Req.server_push_id;
	wtp_event->u.TR_Invoke_Req.user_data = wsp_pdu_pack(pdu);

        wsp_pdu_destroy(pdu);
        dispatch_to_wtp_init(wtp_event);
}

static void indicate_disconnect(WSPMachine *sm, long reason) {
	WAPEvent *new_event;

	new_event = wap_event_create(S_Disconnect_Ind);
	new_event->u.S_Disconnect_Ind.reason_code = reason;
	new_event->u.S_Disconnect_Ind.redirect_security = 0;
	new_event->u.S_Disconnect_Ind.redirect_addresses = 0;
	new_event->u.S_Disconnect_Ind.error_headers = NULL;
	new_event->u.S_Disconnect_Ind.error_body = NULL;
	new_event->u.S_Disconnect_Ind.session_handle = sm->session_id;
	dispatch_to_appl(new_event);
}


static void indicate_suspend(WSPMachine *sm, long reason) {
	WAPEvent *new_event;

	new_event = wap_event_create(S_Suspend_Ind);
	new_event->u.S_Suspend_Ind.reason = reason;
	new_event->u.S_Suspend_Ind.session_id = sm->session_id;
	dispatch_to_appl(new_event);
}


static void indicate_resume(WSPMachine *sm,
                                WAPAddrTuple *tuple, List *headers) {
	WAPEvent *new_event;

	new_event = wap_event_create(S_Resume_Ind);
	new_event->u.S_Resume_Ind.addr_tuple = wap_addr_tuple_duplicate(tuple);
	new_event->u.S_Resume_Ind.client_headers = http_header_duplicate(headers);
	new_event->u.S_Resume_Ind.session_id = sm->session_id;
	dispatch_to_appl(new_event);
}

static void indicate_pushabort(WSPPushMachine *spm, long reason)
{
       WAPEvent *ota_event;
 
       ota_event = wap_event_create(S_PushAbort_Ind);
       ota_event->u.S_PushAbort_Ind.push_id = spm->server_push_id;
       ota_event->u.S_PushAbort_Ind.reason = reason;
       ota_event->u.S_PushAbort_Ind.session_id = spm->session_id;
       dispatch_to_appl(ota_event);
}

static void confirm_push(WSPPushMachine *m)
{
       WAPEvent *ota_event;

       ota_event = wap_event_create(S_ConfirmedPush_Cnf);
       ota_event->u.S_ConfirmedPush_Cnf.server_push_id = m->server_push_id;
       ota_event->u.S_ConfirmedPush_Cnf.session_id = m->session_id;
       dispatch_to_appl(ota_event);
}

static void method_abort(WSPMethodMachine *msm, long reason) {
	WAPEvent *wtp_event;

	/* Send TR-Abort.req(reason) */
	wtp_event = wap_event_create(TR_Abort_Req);
	/* FIXME: Specs are unclear about this; we may indeed have to
	 * guess abort whether this is a WSP or WTP level abort code */
	if (reason < WSP_ABORT_PROTOERR) {
		wtp_event->u.TR_Abort_Req.abort_type = 0x00;
	} else {
		wtp_event->u.TR_Abort_Req.abort_type = 0x01;
	}
	wtp_event->u.TR_Abort_Req.abort_reason = reason;
	wtp_event->u.TR_Abort_Req.handle = msm->transaction_id;

	dispatch_to_wtp_resp(wtp_event);
}


static void indicate_method_abort(WSPMethodMachine *msm, long reason) {
	WAPEvent *new_event;

	/* Send S-MethodAbort.ind(reason) */
	new_event = wap_event_create(S_MethodAbort_Ind);
	new_event->u.S_MethodAbort_Ind.transaction_id = msm->transaction_id;
	new_event->u.S_MethodAbort_Ind.reason = reason;
	new_event->u.S_MethodAbort_Ind.session_handle = msm->session_id;
	dispatch_to_appl(new_event);
}

	
static int method_is_holding(void *item, void *pattern) {
	WSPMethodMachine *msm = item;

	return msm->state == HOLDING;
}


static void release_holding_methods(WSPMachine *sm) {
	WAPEvent *release;
	WSPMethodMachine *msm;
	List *holding;
	long i, len;

	holding = gwlist_search_all(sm->methodmachines, NULL, method_is_holding);
	if (holding == NULL)
		return;

	/* We can re-use this because wsp_handle_method_event does not
	 * destroy its event */
	release = wap_event_create(Release_Event);

	len = gwlist_len(holding);
	for (i = 0; i < len; i++) {
		msm = gwlist_get(holding, i);
		handle_method_event(sm, msm, release, NULL);
	}
	gwlist_destroy(holding, NULL);
	wap_event_destroy(release);
}


static void abort_methods(WSPMachine *sm, long reason) {
	WAPEvent *ab;
	WSPMethodMachine *msm;
	long i, len;

	ab = wap_event_create(Abort_Event);
	ab->u.Abort_Event.reason = reason;

	/* This loop goes backward because it has to deal with the
	 * possibility of method machines disappearing after their event. */
	len = gwlist_len(sm->methodmachines);
	for (i = len - 1; i >= 0; i--) {
		msm = gwlist_get(sm->methodmachines, i);
		handle_method_event(sm, msm, ab, NULL);
	}

	wap_event_destroy(ab);
}


static void abort_pushes(WSPMachine *sm, long reason)
{
        WAPEvent *ab;
	WSPPushMachine *psm;
	long i, len;

        ab = wap_event_create(Abort_Event);
	ab->u.Abort_Event.reason = reason;

        len = gwlist_len(sm->pushmachines);
	for (i = len - 1; i >= 0; i--) {
		psm = gwlist_get(sm->pushmachines, i);
		handle_push_event(sm, psm, ab);
	}

        wap_event_destroy(ab);
}


WSPMachine *find_session_machine_by_id (int id) {

	return gwlist_search(session_machines, &id, id_belongs_to_session);
}


static int id_belongs_to_session (void *wsp_ptr, void *pid) {
	WSPMachine *wsp;
	int *id;

	wsp = wsp_ptr;
	id = (int *) pid;

	if (*id == wsp->session_id) return 1;
	return 0;
}


static int wsp_encoding_string_to_version(Octstr *enc) 
{
    int v;
    
    /* default will be WSP 1.2, as defined by WAPWSP */
    v = WSP_1_2;    

    if (octstr_compare(enc, octstr_imm("1.1")) == 0) {
        v = WSP_1_1;
    }
    else if (octstr_compare(enc, octstr_imm("1.2")) == 0) {
        v = WSP_1_2;
    }
    else if (octstr_compare(enc, octstr_imm("1.3")) == 0) {
        v = WSP_1_3;
    }
    else if (octstr_compare(enc, octstr_imm("1.4")) == 0) {
        v = WSP_1_4;
    }
    else if (octstr_compare(enc, octstr_imm("1.5")) == 0) {
        v = WSP_1_5;
    }

    return v;
}

static Octstr *wsp_encoding_version_to_string(int version) 
{
    Octstr *os;
    
    switch (version) {
        case WSP_1_1:
            os = octstr_create("1.1");
            break;
        case WSP_1_2:
            os = octstr_create("1.2");
            break;
        case WSP_1_3:
            os = octstr_create("1.3");
            break;
        case WSP_1_4:
            os = octstr_create("1.4");
            break;
        case WSP_1_5:
            os = octstr_create("1.5");
            break;
        default:
            os = octstr_create("1.2");
            break;
    }
    
    return os;
}

