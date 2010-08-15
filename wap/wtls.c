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
 * wtls.c: WTLS server-side implementation
 *
 * Nick Clarey <nclarey@3glab.com>
 */

#include "gwlib/gwlib.h"

#if (HAVE_WTLS_OPENSSL)

#include "wtls.h"
#include "timers.h"
#include "wap_events.h"
#include "wtls_pdu.h"
#include "wtls_statesupport.h"
#include "gw/msg.h"

#include "wtp.h"

/***********************************************************************
 * Internal data structures.
 *
 * List of WTLS Server machines.
 */
static List *wtls_machines = NULL;

/*
 * Counter for WTLS Server machine id numbers, to make sure they are unique.
 */
static Counter *wtls_machine_id_counter = NULL;

/*
 * Give the status of wtls server layer:
 *	limbo - not running at all
 *	running - operating normally
 *	terminating - waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } wtls_run_status = limbo;

/*
 * Queue of events to be handled by WTLS Server machines.
 */
static List *wtls_queue = NULL;


/*****************************************************************************
 *
 * Prototypes of internal functions:
 */

/*
 * Create and destroy an uninitialized wtls server state machine.
 */

static WTLSMachine* wtls_machine_create(WAPAddrTuple *tuple);
static void wtls_machine_create_destroy(void *sm);
static void wtls_machine_destroy(void * p);

/*
 * Checks whether the list of wlts server machines includes a specific machine.
 *
 * The machine in question is identified with with source and destination
 * address and port. If the machine does not exist and the event is either;
 * - A SEC-Create-Request.req or
 * - A T-Unitdata.ind containing a ClientHello packet or
 * - A T-Unitdata.ind containing an Alert(no_renegotiation) packet
 * a new machine is created and added in the machines data structure. 
 *
 * See WTLS 7.2 for details of this check.
 */
static WTLSMachine *wtls_machine_find_or_create(WAPEvent *event);

/*
 * Feed an event to a WTLS Server state machine. Handle all errors by 
 * itself, do not report them to the caller.
 */
static void wtls_event_handle(WTLSMachine *machine, WAPEvent *event);

/*
 * Print a WTLS Server machine state as a string.
 */
static unsigned char *name_wtlser_state(int name);

/*
 * Find a WTLS Server machine from the global list of wtls server 
 * structures that corresponds to the four-tuple of source and destination 
 * addresses and ports. Return a pointer to the machine, or NULL if not found.
 */
static WTLSMachine *wtls_machine_find(WAPAddrTuple *tuple, long mid);

static void main_thread(void *);
static WTLSMachine *find_wtls_machine_using_mid(long mid);
static void add_wtls_address(Msg *msg, WTLSMachine *wtls_machine);

/* The match* functions are used for searches through lists */
static int match_handshake_type(void* item, void* pattern);
static int match_pdu_type(void* item, void* pattern);

/*static WAPEvent *create_tr_invoke_ind(WTPRespMachine *sm, Octstr *user_data);
static WAPEvent *create_tr_abort_ind(WTPRespMachine *sm, long abort_reason);
static WAPEvent *create_tr_result_cnf(WTPRespMachine *sm); */

/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */
WAPEvent *wtls_unpack_wdp_datagram(Msg *msg)
{
        WAPEvent* unitdataIndEvent;
        List* wtlsPayloadList;

        /* Dump the Msg */
        msg_dump(msg,0);
        
        /* Then, stuff it into a T_Unitdata_Ind Event */
        unitdataIndEvent = wap_event_create(T_Unitdata_Ind);
        info(0,"Event created");
        
        /* Firstly, the address */ 
        unitdataIndEvent->u.T_Unitdata_Ind.addr_tuple =
                wap_addr_tuple_create(msg->wdp_datagram.source_address,
                                      msg->wdp_datagram.source_port,
                                      msg->wdp_datagram.destination_address,
                                      msg->wdp_datagram.destination_port);
        info(0,"Set address and stuff");

        /* Attempt to stuff this baby into a list-of-WTLS-PDUs */
        wtlsPayloadList = wtls_unpack_payloadlist (msg->wdp_datagram.user_data);
        info(0,"Datagram unpacked!");
        
        /* Then, the pdu material */
        unitdataIndEvent->u.T_Unitdata_Ind.pdu_list = wtlsPayloadList;

        /* And return the event */
        return unitdataIndEvent;
}

void wtls_init(void) {
        /* Initialise our various lists and counters */
        wtls_machines = gwlist_create();
        wtls_machine_id_counter = counter_create();
        
        wtls_queue = gwlist_create();
        gwlist_add_producer(wtls_queue);

        /* Idiot check - ensure that we are able to start running */
        gw_assert(wtls_run_status == limbo);
        wtls_run_status = running;
        gwthread_create(main_thread, NULL);
}

void wtls_shutdown(void) {
        /* Make sure that we're actually running; if so, then
           prepare for termination */
        gw_assert(wtls_run_status == running);
        wtls_run_status = terminating;
        gwlist_remove_producer(wtls_queue);
        gwthread_join_every(main_thread);

        /* Print out a friendly message stating that we're going to die */
        debug("wap.wtls", 0, "wtls_shutdown: %ld wtls machines left",
              gwlist_len(wtls_machines));

        /* And clean up nicely after ourselves */
        gwlist_destroy(wtls_machines, wtls_machine_destroy);
        gwlist_destroy(wtls_queue, wap_event_destroy_item);     
        counter_destroy(wtls_machine_id_counter);
}

void wtls_dispatch_event(WAPEvent *event) {
        /* Stick the event on the incoming events queue */
        gwlist_produce(wtls_queue, event);
}

int wtls_get_address_tuple(long mid, WAPAddrTuple **tuple) {
	WTLSMachine *sm;
	
	sm = find_wtls_machine_using_mid(mid);
	if (sm == NULL)
		return -1;

	*tuple = wap_addr_tuple_duplicate(sm->addr_tuple);
	return 0;
}

void send_alert(int alertLevel, int alertDescription, WTLSMachine* wtls_machine) {
        wtls_Payload* alertPayload;
        wtls_PDU* alertPDU;
        
        Octstr* packedAlert;
        Msg* msg = NULL;
        
        alertPDU = (wtls_PDU*) wtls_pdu_create(Alert_PDU);
        alertPDU->u.alert.level = alertLevel;
        alertPDU->u.alert.desc = alertDescription;

        /* Here's where we should get the current checksum from the wtls_machine */
        alertPDU->u.alert.chksum = 0;

        /* Pack the PDU */
        msg = msg_create(wdp_datagram);
        add_wtls_address(msg, wtls_machine);

        /* Pack the message */
        alertPayload = wtls_pdu_pack(alertPDU, wtls_machine);

        packedAlert = (Octstr*) wtls_payload_pack(alertPayload);
        msg->wdp_datagram.user_data = packedAlert;

        /* And destroy the structure */
        wtls_payload_destroy(alertPayload);
        alertPayload = NULL;
        
        /* Send it off */
        write_to_bearerbox(msg);
}

void clear_queuedpdus(WTLSMachine* wtls_machine)
{
}

void add_pdu(WTLSMachine* wtls_machine, wtls_PDU* pduToAdd)
{
        int currentLength;
        wtls_Payload* payloadToAdd;
        Octstr* packedPDU;

        /* Check to see if we've already allocated some memory for the list */
        if (wtls_machine->packet_to_send == NULL) {
                wtls_machine->packet_to_send = octstr_create("");
        }

        /* Pack and encrypt the pdu */
        payloadToAdd = wtls_pdu_pack(pduToAdd, wtls_machine);

        /* If the pdu is a Handshake pdu, append the Octstr to our wtls_machine's
           exchanged_handshakes Octstr */
        packedPDU = wtls_payload_pack(payloadToAdd);

        /* Add it to our list */
        currentLength = octstr_len(wtls_machine->packet_to_send);
        octstr_insert(wtls_machine->packet_to_send, packedPDU, currentLength);
}


/*
 * Send the pdu_to_send list to the destination specified by the address in the machine
 * structure. Don't return anything, handle all errors internally.
 */
void send_queuedpdus(WTLSMachine* wtls_machine)
{
        Msg* msg = NULL;

        gw_assert(wtls_machine->packet_to_send != NULL);
        
        /* Pack the PDU */
        msg = msg_create(wdp_datagram);
        add_wtls_address(msg, wtls_machine);
        msg->wdp_datagram.user_data = octstr_duplicate(wtls_machine->packet_to_send);

        /* Send it off */
        write_to_bearerbox(msg);

        /* Destroy our copy of the sent string */
        octstr_destroy(wtls_machine->packet_to_send);
        wtls_machine->packet_to_send = NULL;
        
}


/* 
 * Add address from  state machine.
 */
void add_wtls_address(Msg *msg, WTLSMachine *wtls_machine){

       debug("wap.wtls", 0, "adding address");
       msg->wdp_datagram.source_address = 
    	    octstr_duplicate(wtls_machine->addr_tuple->local->address);
       msg->wdp_datagram.source_port = wtls_machine->addr_tuple->local->port;
       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(wtls_machine->addr_tuple->remote->address);
       msg->wdp_datagram.destination_port = 
            wtls_machine->addr_tuple->remote->port;
}

/*****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 */

static void main_thread(void *arg) {
	WTLSMachine *sm;
	WAPEvent *e;
        
	while (wtls_run_status == running && 
               (e = gwlist_consume(wtls_queue)) != NULL) {
		sm = wtls_machine_find_or_create(e);
		if (sm == NULL)
			wap_event_destroy(e);
		else
			wtls_event_handle(sm, e);
                }
}

/*
 * Give the name of a WTLS Server state in a readable form. 
 */
static unsigned char *name_wtls_state(int s){
       switch (s){
              #define STATE_NAME(state) case state: return #state;
              #define ROW(state, event, condition, action, new_state)
              #include "wtls_state-decl.h"
              default:
                      return "unknown state";
       }
}


/*
 * Feed an event to a WTP responder state machine. Handle all errors yourself,
 * do not report them to the caller. Note: Do not put {}s of the else block 
 * inside the macro definition. 
 */
static void wtls_event_handle(WTLSMachine *wtls_machine, WAPEvent *event){

     debug("wap.wtls", 0, "WTLS: wtls_machine %ld, state %s, event %s.", 
	   wtls_machine->mid, 
	   name_wtls_state(wtls_machine->state), 
	   wap_event_name(event->type));

	/* for T_Unitdata_Ind PDUs */
	if(event->type == T_Unitdata_Ind) {
		/* if encryption: decrypt all pdus in the list */
		if( wtls_machine->encrypted ) {
			wtls_decrypt_pdu_list(wtls_machine, event->u.T_Unitdata_Ind.pdu_list);
		}
		/* add all handshake data to wtls_machine->handshake_data */
		//add_all_handshake_data(wtls_machine, event->u.T_Unitdata_Ind.pdu_list);

	}
	
     #define STATE_NAME(state)
     #define ROW(wtls_state, event_type, condition, action, next_state) \
	     if (wtls_machine->state == wtls_state && \
		event->type == event_type && \
		(condition)) { \
		action \
		wtls_machine->state = next_state; \
		debug("wap.wtls", 0, "WTLS %ld: New state %s", wtls_machine->mid, #next_state); \
	     } else 
     #include "wtls_state-decl.h"
	     {
		error(0, "WTLS: handle_event: unhandled event!");
		debug("wap.wtls", 0, "WTLS: handle_event: Unhandled event was:");
		wap_event_destroy(event);
		return;
	     }

     if (event != NULL) {
	wap_event_destroy(event);  
     }

     if (wtls_machine->state == NULL_STATE)
     	wtls_machine_destroy(wtls_machine);
}

/*
 * Checks whether wtls machines data structure includes a specific machine.
 * The machine in question is identified with with source and destination
 * address and port.
 */

static WTLSMachine *wtls_machine_find_or_create(WAPEvent *event) {

          WTLSMachine *wtls_machine = NULL;
          long mid;
          WAPAddrTuple *tuple;

          tuple = NULL;
          mid = -1;

		  debug("wap.wtls",0, "event->type = %d", event->type);
		  
          /* Get the address that this PDU came in from */
          switch (event->type) {
          case T_Unitdata_Ind:
          case T_DUnitdata_Ind:
                  tuple = event->u.T_Unitdata_Ind.addr_tuple;
                  break;
          case SEC_Create_Request_Req:
          case SEC_Terminate_Req:
          case SEC_Exception_Req:
          case SEC_Create_Res:
          case SEC_Exchange_Req:
          case SEC_Commit_Req:
          case SEC_Unitdata_Req:
                  tuple = event->u.T_Unitdata_Ind.addr_tuple;
                  break;
          default:
                  debug("wap.wtls", 0, "WTLS: wtls_machine_find_or_create:"
                        "unhandled event (1)"); 
                  wap_event_dump(event);
                  return NULL;
          }

          /* Either the address or the machine id must be available at this point */
          gw_assert(tuple != NULL || mid != -1);

          /* Look for the machine owning this address */
          wtls_machine = wtls_machine_find(tuple, mid);

          /* Oh well, we didn't find one. We'll create one instead, provided
             it meets certain criteria */
          if (wtls_machine == NULL){
                  switch (event->type){
                  case SEC_Create_Request_Req:
                          /* State NULL, case 1 */
                          debug("wap.wtls",0,"WTLS: received a SEC_Create_Request_Req, and don't know what to do with it...");
                          /* Create and dispatch a T_Unitdata_Req containing a HelloRequest */
                          /* And there's no need to do anything else, 'cause we return to state NULL */
                          break;
                  case T_Unitdata_Ind:
                  case T_DUnitdata_Ind:
                          /* State NULL, case 3 */
/*                           if (wtls_event_type(event) == Alert_No_Renegotiation) { */
                                  /* Create and dispatch a SEC_Exception_Ind event */
/*                                   debug("wap.wtls",0,"WTLS: received an Alert_no_Renegotiation; just dropped it."); */
                                  /* And there's no need to do anything else, 'cause we return to state NULL */
/*                                   break; */
/*                           } else */
/*                           if (event->u.T_Unitdata_Ind == ClientHello) { */
                                  /* State NULL, case 2 */
                          wtls_machine = wtls_machine_create(tuple);
                          /* And stick said event into machine, which should push us into state
                             CREATING after a SEC_Create_Ind */
/*                           } */
                          break;
                  default:
                          error(0, "WTLS: wtls_machine_find_or_create:"
                                " unhandled event (2)");
                          wap_event_dump(event);
                          break;
                  }
          }
          return wtls_machine;
}

static int is_wanted_wtls_machine(void *a, void *b) {
	machine_pattern *pat;
	WTLSMachine *m;
	
	m = a;
	pat = b;

	if (m->mid == pat->mid)
		return 1;

	if (pat->mid != -1)
		return 0;

	return wap_addr_tuple_same(m->addr_tuple, pat->tuple);
}


static WTLSMachine *wtls_machine_find(WAPAddrTuple *tuple, 
                                         long mid) {
	machine_pattern pat;
	WTLSMachine *m;
	
	pat.tuple = tuple;
	pat.mid = mid;
	
	m = gwlist_search(wtls_machines, &pat, is_wanted_wtls_machine);
	return m;
}


static WTLSMachine *wtls_machine_create(WAPAddrTuple *tuple) {

        WTLSMachine *wtls_machine;
        wtls_machine = gw_malloc(sizeof(WTLSMachine)); 
        
        #define MACHINE(field) field
        #define ENUM(name) wtls_machine->name = NULL_STATE;
        #define ADDRTUPLE(name) wtls_machine->name = NULL; 
        #define INTEGER(name) wtls_machine->name = 0; 
        #define OCTSTR(name) wtls_machine->name = NULL;
        #define PDULIST(name) wtls_machine->name = NULL;
        #include "wtls_machine-decl.h"
        
        gwlist_append(wtls_machines, wtls_machine);
        wtls_machine->mid = counter_increase(wtls_machine_id_counter);
        wtls_machine->addr_tuple = wap_addr_tuple_duplicate(tuple);

		wtls_machine->handshake_data = octstr_create("");
		
        debug("wap.wtls", 0, "WTLS: Created WTLSMachine %p (%ld)",
              (void *) wtls_machine, wtls_machine->mid);
        return wtls_machine;
}

/*
 * Destroys a WTLSMachine. Assumes it is safe to do so. Assumes it has 
 * already been deleted from the machines list.
 */
static void wtls_machine_destroy(void * p) {
       WTLSMachine *wtls_machine;

       wtls_machine = p;
       debug("wap.wtls", 0, "WTLS: Destroying WTLSMachine %p (%ld)",
             (void *) wtls_machine, wtls_machine->mid);
       gwlist_delete_equal(wtls_machines, wtls_machine);        
        
       #define MACHINE(field) field
       #define ENUM(name) wtls_machine->name = NULL_STATE;
       #define ADDRTUPLE(name) wap_addr_tuple_destroy(wtls_machine->name); 
       #define INTEGER(name) wtls_machine->name = 0; 
       #define OCTSTR(name) octstr_destroy(wtls_machine->name);
       #define PDULIST(name) wtls_machine->name = NULL;
       #include "wtls_machine-decl.h"

        gw_free(wtls_machine);
}


/*
 * Create a TR-Invoke.ind event.
static WAPEvent *create_tr_invoke_ind(WTPRespMachine *sm, Octstr *user_data) {
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
*/

/*
 * Create a TR-Result.cnf event.
static WAPEvent *create_tr_result_cnf(WTPRespMachine *sm) {
	WAPEvent *event;
	
	event = wap_event_create(TR_Result_Cnf);
	event->u.TR_Result_Cnf.addr_tuple = 
		wap_addr_tuple_duplicate(sm->addr_tuple);
	event->u.TR_Result_Cnf.handle = sm->mid;
	return event;
}
*/

/*
 * Creates TR-Abort.ind event from a responder state machine. 
 
static WAPEvent *create_tr_abort_ind(WTPRespMachine *sm, long abort_reason) {
	WAPEvent *event;
	
	event = wap_event_create(TR_Abort_Ind);

	event->u.TR_Abort_Ind.abort_code = abort_reason;
	event->u.TR_Abort_Ind.addr_tuple = 
		wap_addr_tuple_duplicate(sm->addr_tuple);
	event->u.TR_Abort_Ind.handle = sm->mid;

	return event;
}
*/

static int wtls_machine_has_mid(void *a, void *b) {
	WTLSMachine *sm;
	long mid;
	
	sm = a;
	mid = *(long *) b;
	return sm->mid == mid;
}

static WTLSMachine *find_wtls_machine_using_mid(long mid) {
       return gwlist_search(wtls_machines, &mid, wtls_machine_has_mid);
}

/* Used for list searches */
static int match_handshake_type(void* item, void* pattern)
{
        wtls_Payload* matchingPayload;
        int type;
        int retrievedType;
        
        matchingPayload = (wtls_Payload*) item;
        type = (int) pattern;
        
        retrievedType = octstr_get_char(matchingPayload->data, 0);
        
        if (matchingPayload->type == Handshake_PDU && retrievedType == type)
        {
                return 1;
        }
        else
        {
                return 0;
        }        
}

static int match_pdu_type(void* item, void* pattern)
{
        wtls_Payload* matchingPayload;
        int type;
        
        matchingPayload = (wtls_Payload*) item;
        type = (int) pattern;
        
        
        if (matchingPayload->type == type)
        {
                return 1;
        }
        else
        {
                return 0;
        }        
}

#endif
