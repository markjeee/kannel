/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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
 * Nikos Balkanas, InAccess Networks (2009)
 */


#include "gwlib/gwlib.h"

#ifdef HAVE_WTLS_OPENSSL

#include "wtls.h"
#include "timers.h"
#include "wap_events.h"
#include "wtls_pdu.h"
#include "wtls_statesupport.h"
#include "wtls_pdusupport.h"
#include "gw/msg.h"

#include "wap.h"
#include "wtp.h"

#define WTLS_CONNECTIONLESS_PORT 9202
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

static wtls_dispatch_func_t *dispatch_resp_to_bb;

/*
 * Create and destroy an uninitialized wtls server state machine.
 */

static WTLSMachine *wtls_machine_create(WAPAddrTuple * tuple);
static void wtls_machine_destroy(void *p);

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
static WTLSMachine *wtls_machine_find_or_create(WAPEvent * event);

/*
 * Feed an event to a WTLS Server state machine. Handle all errors by 
 * itself, do not report them to the caller.
 */
static void wtls_event_handle(WTLSMachine * machine, WAPEvent * event);

/*
 * Find a WTLS Server machine from the global list of wtls server 
 * structures that corresponds to the four-tuple of source and destination 
 * addresses and ports. Return a pointer to the machine, or NULL if not found.
 */
static WTLSMachine *wtls_machine_find(WAPAddrTuple * tuple, long mid);

/* Function prototypes */
static void main_thread(void *);
static WTLSMachine *find_wtls_machine_using_mid(long mid);
static void add_wtls_address(Msg * msg, WTLSMachine * wtls_machine);
static void add_pdu(WTLSMachine * wtls_machine, wtls_PDU * pduToAdd);
static void send_queuedpdus(WTLSMachine * wtls_machine);
void send_alert(WAPEvent * event, WTLSMachine * wtls_machine);
char *stateName(int s);

/* The match* functions are used for searches through lists */
static int match_handshake_type(void *item, void *pattern);
static int match_pdu_type(void *item, void *pattern);

extern void write_to_bearerbox(Msg * pmsg);
extern Octstr *wtls_get_certificate(void);

/*static WAPEvent *create_tr_invoke_ind(WTPRespMachine *sm, Octstr *user_data);
static WAPEvent *create_tr_abort_ind(WTPRespMachine *sm, long abort_reason);
static WAPEvent *create_tr_result_cnf(WTPRespMachine *sm); */

/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */
WAPEvent *wtls_unpack_wdp_datagram(Msg * msg)
{
   WAPEvent *unitdataIndEvent;
   List *wtlsPayloadList;

        /* Dump the Msg */
   msg_dump(msg, 0);
        
        /* Then, stuff it into a T_Unitdata_Ind Event */
        unitdataIndEvent = wap_event_create(T_Unitdata_Ind);
   info(0, "Event created");
        
        /* Firstly, the address */ 
        unitdataIndEvent->u.T_Unitdata_Ind.addr_tuple =
                wap_addr_tuple_create(msg->wdp_datagram.source_address,
                                      msg->wdp_datagram.source_port,
                                      msg->wdp_datagram.destination_address,
                                      msg->wdp_datagram.destination_port);
   info(0, "Set address and stuff");

        /* Attempt to stuff this baby into a list-of-WTLS-PDUs */
   wtlsPayloadList = wtls_unpack_payloadlist(msg->wdp_datagram.user_data);
   info(0, "Datagram unpacked!");
        
        /* Then, the pdu material */
        unitdataIndEvent->u.T_Unitdata_Ind.pdu_list = wtlsPayloadList;

        /* And return the event */
        return unitdataIndEvent;
}

void wtls_init(wtls_dispatch_func_t *responder_dispatch)
{
        /* Initialise our various lists and counters */
        wtls_machines = gwlist_create();
        wtls_machine_id_counter = counter_create();
        
        wtls_queue = gwlist_create();
        gwlist_add_producer(wtls_queue);
   dispatch_resp_to_bb = responder_dispatch;

        /* Idiot check - ensure that we are able to start running */
        gw_assert(wtls_run_status == limbo);
        wtls_run_status = running;
        gwthread_create(main_thread, NULL);
}

void wtls_shutdown(void)
{
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

void wtls_dispatch_event(WAPEvent * event)
{
        /* Stick the event on the incoming events queue */
        gwlist_produce(wtls_queue, event);
}

void wtls_dispatch_resp(WAPEvent * dgram)
{
   WAPAddrTuple *tuple;
   WTLSMachine *wtls_machine;

   tuple = dgram->u.T_DUnitdata_Req.addr_tuple;
   if ((wtls_machine = wtls_machine_find(tuple, -1))) {
      wtls_PDU *respPDU;

      debug("wtls", 0,
            "wtls_dispatch_resp ~> Dispatching datagram to bearerbox");
      respPDU = wtls_pdu_create(Application_PDU);
      respPDU->cipher = 1;
      respPDU->snMode = wtls_machine->sequence_number_mode ? 1 : 0;
      respPDU->rlen = 1;
      respPDU->u.application.data =
          dgram->u.T_DUnitdata_Req.user_data;
      debug("wtls", 0, "Sending Response PDU:");
      wtls_pdu_dump(respPDU, 0);
      add_pdu(wtls_machine, respPDU);
      wtls_pdu_destroy(respPDU);
      send_queuedpdus(wtls_machine);
      dgram->u.T_DUnitdata_Req.user_data = NULL;
   } else error(0, "wtls_dispatch_event: Unable to find state machine. "
             "Dropping datagram.");
}

int wtls_get_address_tuple(long mid, WAPAddrTuple ** tuple)
{
	WTLSMachine *sm;
	
	sm = find_wtls_machine_using_mid(mid);
	if (sm == NULL)
		return -1;

	*tuple = wap_addr_tuple_duplicate(sm->addr_tuple);
	return 0;
}

void send_alert(WAPEvent * event, WTLSMachine * wtls_machine)
{
   wtls_PDU *alertPDU;

   alertPDU = (wtls_PDU *) wtls_pdu_create(Alert_PDU);
   alertPDU->rlen = 1;
   alertPDU->snMode = wtls_machine->sequence_number_mode ? 1 : 0;
   alertPDU->u.alert.level = event->u.SEC_Terminate_Req.alert_level;
   alertPDU->u.alert.desc = event->u.SEC_Terminate_Req.alert_desc;
        
   /* Here's where we should get the current checksum from the wtls_machine */
   alertPDU->u.alert.chksum = octstr_create("0000");

   add_pdu(wtls_machine, alertPDU);
   wtls_pdu_destroy(alertPDU);
   send_queuedpdus(wtls_machine);
}

static void add_pdu(WTLSMachine * wtls_machine, wtls_PDU * pduToAdd)
{
        int currentLength;
   wtls_Payload *payloadToAdd;
   Octstr *packedPDU;

   /* Update sequence number before encryption */
   wtls_machine->server_seq_num++;

        /* Pack and encrypt the pdu */
   if (!(payloadToAdd = wtls_pdu_pack(pduToAdd, wtls_machine))) {
      wtls_machine->server_seq_num--;
      return;
   }
   if (!payloadToAdd->data) {
      wtls_machine->server_seq_num--;
      wtls_payload_destroy(payloadToAdd);
      return;
   }

   /* Check to see if we've already allocated some memory for the list */
   if (!wtls_machine->packet_to_send)
      wtls_machine->packet_to_send = octstr_create("");

        /* If the pdu is a Handshake pdu, append the Octstr to our wtls_machine's
           exchanged_handshakes Octstr */
   packedPDU =
       wtls_payload_pack(payloadToAdd, wtls_machine->server_seq_num);
   if (pduToAdd->type == ChangeCipher_PDU)
      wtls_machine->server_seq_num = -1;

        /* Add it to our list */
        currentLength = octstr_len(wtls_machine->packet_to_send);
        octstr_insert(wtls_machine->packet_to_send, packedPDU, currentLength);
   wtls_payload_destroy(payloadToAdd);
   octstr_destroy(packedPDU);
}

/*
 * Send the pdu_to_send list to the destination specified by the address in the machine
 * structure. Don't return anything, handle all errors internally.
 */
static void send_queuedpdus(WTLSMachine * wtls_machine)
{
   Msg *msg = NULL;

        gw_assert(wtls_machine->packet_to_send != NULL);
   if (!wtls_machine->packet_to_send)
      return;
        
        /* Pack the PDU */
        msg = msg_create(wdp_datagram);
        add_wtls_address(msg, wtls_machine);
   msg->wdp_datagram.user_data =
       octstr_duplicate(wtls_machine->packet_to_send);

        /* Send it off */
   dispatch_resp_to_bb(msg);

        /* Destroy our copy of the sent string */
        octstr_destroy(wtls_machine->packet_to_send);
        wtls_machine->packet_to_send = NULL;
}

/* 
 * Add address from  state machine.
 */
void add_wtls_address(Msg * msg, WTLSMachine * wtls_machine)
{

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

static void main_thread(void *arg)
{
	WTLSMachine *sm;
	WAPEvent *e;
        
   while (wtls_run_status == running && (e = gwlist_consume(wtls_queue))) {
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
char *stateName(int s)
{
   switch (s) {
#define STATE_NAME(state) case state: return #state;
#define ROW(state, event, condition, action, new_state)
#include "wtls_state-decl.h"
              default:
                      return "unknown state";
       }
}

static void fatalAlert(WAPEvent * event, int description)
{
   WAPEvent *abort;

   abort = wap_event_create(SEC_Terminate_Req);
   abort->u.SEC_Terminate_Req.addr_tuple = wap_addr_tuple_duplicate
       (event->u.T_Unitdata_Ind.addr_tuple);
   abort->u.SEC_Terminate_Req.alert_level = fatal_alert;
   abort->u.SEC_Terminate_Req.alert_desc = description;
   wtls_dispatch_event(abort);
}

static void clientHello(WAPEvent * event, WTLSMachine * wtls_machine)
{
   /* The Wap event we have to dispatch */
   WAPEvent *res;
   wtls_Payload *tempPayload;
   wtls_PDU *clientHelloPDU;
   CipherSuite *ciphersuite;
   int randomCounter, algo;

   tempPayload =
       (wtls_Payload *) gwlist_search(event->u.T_Unitdata_Ind.pdu_list,
                  (void *)client_hello,
                  match_handshake_type);
   if (!tempPayload) {
      error(0, "Illegal PDU while waiting for a ClientHello");
      fatalAlert(event, unexpected_message);
      return;
   }
   clientHelloPDU = wtls_pdu_unpack(tempPayload, wtls_machine);

   /* Store the client's random value - use pack for simplicity */
   wtls_machine->client_random = octstr_create("");
   randomCounter = pack_int32(wtls_machine->client_random, 0,
               clientHelloPDU->u.handshake.client_hello->
               random->gmt_unix_time);
   octstr_insert(wtls_machine->client_random,
            clientHelloPDU->u.handshake.client_hello->random->
            random_bytes, randomCounter);

   /* Select the ciphersuite from the supplied list */
   ciphersuite =
       wtls_choose_ciphersuite(clientHelloPDU->u.handshake.client_hello->
                ciphersuites);
   if (!ciphersuite) {
      error(0, "Couldn't agree on encryption cipher. Aborting");
      wtls_pdu_destroy(clientHelloPDU);
      fatalAlert(event, handshake_failure);
      return;
   }
   /* Set the relevant values in the wtls_machine and PDU structure */
   wtls_machine->bulk_cipher_algorithm = ciphersuite->bulk_cipher_algo;
   wtls_machine->mac_algorithm = ciphersuite->mac_algo;

   /* Generate a SEC_Create_Res event, and pass it back into the queue */
   res = wap_event_create(SEC_Create_Res);
   res->u.SEC_Create_Res.addr_tuple =
       wap_addr_tuple_duplicate(event->u.T_Unitdata_Ind.addr_tuple);
   res->u.SEC_Create_Res.bulk_cipher_algo = ciphersuite->bulk_cipher_algo;
   res->u.SEC_Create_Res.mac_algo = ciphersuite->mac_algo;
   res->u.SEC_Create_Res.client_key_id = wtls_choose_clientkeyid
       (clientHelloPDU->u.handshake.client_hello->client_key_ids, &algo);
   if (!res->u.SEC_Create_Res.client_key_id) {
      error(0, "Couldn't agree on key exchange protocol. Aborting");
      wtls_pdu_destroy(clientHelloPDU);
      wap_event_destroy(res);
      fatalAlert(event, unknown_key_id);
      return;
   }
   wtls_machine->key_algorithm = algo;

   /* Set the sequence number mode in both the machine and the outgoing packet */
   res->u.SEC_Create_Res.snmode =
       wtls_choose_snmode(clientHelloPDU->u.handshake.client_hello->
                snmode);
   wtls_machine->sequence_number_mode = res->u.SEC_Create_Res.snmode;

   /* Set the key refresh mode in both the machine and the outgoing packet */
   res->u.SEC_Create_Res.krefresh =
       clientHelloPDU->u.handshake.client_hello->krefresh;
   wtls_machine->key_refresh = res->u.SEC_Create_Res.krefresh;
   /* Global refresh variable */
   debug("wtls", 0, "clientHello ~> Accepted refresh = %d, refresh_rate = "
         "%d", wtls_machine->key_refresh, 1 << wtls_machine->key_refresh);

   /* Keep the data so we can send it back in EXCHANGE
    * temporary - needs to delete old one if exists !
    * wtls_machine->handshake_data = octstr_create("");
    */
   if (wtls_machine->handshake_data)
      octstr_destroy(wtls_machine->handshake_data);
   wtls_machine->handshake_data = octstr_create("");
   octstr_append(wtls_machine->handshake_data, tempPayload->data);

   debug("wtls", 0, "clientHello ~> Dispatching SEC_Create_Res event");
   wtls_pdu_destroy(clientHelloPDU);
   wtls_dispatch_event(res);
}

static void serverHello(WAPEvent * event, WTLSMachine * wtls_machine)
{
   WAPEvent *req;
   wtls_PDU *serverHelloPDU;
//   wtls_PDU* certificatePDU;
   Random *tempRandom;
/*   List *certList;
   Certificate *cert;
*/ int randomCounter = 0;

   /* Our serverHello */
   serverHelloPDU = wtls_pdu_create(Handshake_PDU);
   serverHelloPDU->rlen = 1;
   serverHelloPDU->snMode = wtls_machine->sequence_number_mode ? 1 : 0;
   serverHelloPDU->u.handshake.msg_type = server_hello;
   serverHelloPDU->u.handshake.server_hello =
       (ServerHello *) gw_malloc(sizeof(ServerHello));

   /* Set our server version */
   serverHelloPDU->u.handshake.server_hello->serverversion = 1;

   /* Get a suitably random number - store it in both the machine structure and outgoing PDU */
   tempRandom = wtls_get_random();
   wtls_machine->server_random = octstr_create("");
   randomCounter =
       pack_int32(wtls_machine->server_random, 0,
             tempRandom->gmt_unix_time);
   octstr_insert(wtls_machine->server_random, tempRandom->random_bytes,
            octstr_len(wtls_machine->server_random));

   serverHelloPDU->u.handshake.server_hello->random = tempRandom;

   /* At the moment, we don't support session caching, so tell them to forget about caching us */
   serverHelloPDU->u.handshake.server_hello->session_id =
       octstr_format("%llu", wtls_machine->mid);

   /* We need to select an appropriate mechanism here from the ones listed */
   serverHelloPDU->u.handshake.server_hello->client_key_id =
       event->u.SEC_Create_Res.client_key_id;

   /* Get our ciphersuite details */
   serverHelloPDU->u.handshake.server_hello->ciphersuite = (CipherSuite *)
       gw_malloc(sizeof(CipherSuite));
   serverHelloPDU->u.handshake.server_hello->ciphersuite->bulk_cipher_algo
       = event->u.SEC_Create_Res.bulk_cipher_algo;
   serverHelloPDU->u.handshake.server_hello->ciphersuite->mac_algo =
       event->u.SEC_Create_Res.mac_algo;
   serverHelloPDU->u.handshake.server_hello->comp_method = null_comp;

   /* We need to confirm the client's choice, or if they haven't 
    * specified one, select one ourselves 
    */
   serverHelloPDU->u.handshake.server_hello->snmode =
       event->u.SEC_Create_Res.snmode;

   /* We need to either confirm the client's choice of key refresh rate, or choose a lower rate */
   serverHelloPDU->u.handshake.server_hello->krefresh =
       event->u.SEC_Create_Res.krefresh;

   /* Add the PDUsto the server's outgoing list  */
   add_pdu(wtls_machine, serverHelloPDU);
   wtls_pdu_destroy(serverHelloPDU);

   /* Generate and dispatch a SEC_Exchange_Req or maybe a SEC_Commit_Req */
   req = wap_event_create(SEC_Exchange_Req);
   req->u.SEC_Exchange_Req.addr_tuple =
       wap_addr_tuple_duplicate(event->u.T_Unitdata_Ind.addr_tuple);
   wtls_dispatch_event(req);
   debug("wtls", 0, "serverHello ~> Dispatching SEC_Exchange_Req event");
}

static void exchange_keys(WAPEvent * event, WTLSMachine * wtls_machine)
{
   RSAPublicKey *public_key = NULL;
   Octstr *checking_data = NULL;

   /* The Wap PDUs we have to dispatch */
   wtls_PDU *changeCipherSpecPDU;
   wtls_PDU *finishedPDU;

   /* The PDUs we have to process */
   wtls_Payload *tempPayload;
   wtls_PDU *clientKeyXchgPDU;
   wtls_PDU *changeCipherSpec_incoming_PDU;
   wtls_PDU *finished_incoming_PDU;

   /* For decrypting/encrypting data */
   Octstr *concatenatedRandoms = NULL;
   Octstr *encryptedData = NULL;
   Octstr *decryptedData = NULL;
   Octstr *labelVerify = NULL;
   Octstr *labelMaster = NULL;

   /* Process the incoming event : ClientKeyExchange */
   tempPayload =
       (wtls_Payload *) gwlist_search(event->u.T_Unitdata_Ind.pdu_list,
                  (void *)client_key_exchange,
                  match_handshake_type);

   if (!tempPayload) {
      error(0, "Missing client_key_exchange. Aborting...");
      fatalAlert(event, unexpected_message);
      return;
   }

   /* Keep the data so we can send it back */
   octstr_insert(wtls_machine->handshake_data, tempPayload->data,
            octstr_len(wtls_machine->handshake_data));

   clientKeyXchgPDU = wtls_pdu_unpack(tempPayload, wtls_machine);
   wtls_pdu_dump(clientKeyXchgPDU, 0);

   /* Decrypt the client key exchange PDU */
   encryptedData =
       clientKeyXchgPDU->u.handshake.client_key_exchange->rsa_params->
       encrypted_secret;
   decryptedData =
       wtls_decrypt_key(wtls_machine->key_algorithm, encryptedData);

   if (!decryptedData) {
      error(0,
            "Key Exchange failed. Couldn't decrypt client's secret (%d)."
            " Aborting...", wtls_machine->key_algorithm);
      wtls_pdu_destroy(clientKeyXchgPDU);
      fatalAlert(event, decryption_failed);
      return;
   }
   public_key = wtls_get_rsapublickey();
   pack_int16(decryptedData, octstr_len(decryptedData),
         octstr_len(public_key->rsa_exponent));
   octstr_insert(decryptedData, public_key->rsa_exponent,
            octstr_len(decryptedData));
   pack_int16(decryptedData, octstr_len(decryptedData),
         octstr_len(public_key->rsa_modulus));
   octstr_insert(decryptedData, public_key->rsa_modulus,
            octstr_len(decryptedData));

   /* Concatenate our random data */
   concatenatedRandoms = octstr_cat(wtls_machine->client_random,
                wtls_machine->server_random);

   /* Generate our master secret */
   labelMaster = octstr_create("master secret");
   wtls_machine->master_secret = wtls_calculate_prf(decryptedData,
                      labelMaster,
                      concatenatedRandoms,
                      20, wtls_machine);

   /* Process the incoming event : ChangeCipherSpec */
   tempPayload =
       (wtls_Payload *) gwlist_search(event->u.T_Unitdata_Ind.pdu_list,
                  (void *)ChangeCipher_PDU,
                  match_pdu_type);

   if (!tempPayload) {
      error(0, "Missing change_cipher. Aborting...");
      octstr_destroy(labelMaster);
      octstr_destroy(concatenatedRandoms);
      destroy_rsa_pubkey(public_key);
      octstr_destroy(decryptedData);
      octstr_destroy(encryptedData);
      fatalAlert(event, unexpected_message);
      return;
   }

   changeCipherSpec_incoming_PDU = wtls_pdu_unpack(tempPayload,
                     wtls_machine);
   octstr_dump(wtls_machine->client_write_MAC_secret, 0);

   wtls_pdu_dump(changeCipherSpec_incoming_PDU, 0);

   if (changeCipherSpec_incoming_PDU->u.cc.change == 1) {
      debug("wtls", 0, "Need to decrypt the PDUs from now on...");
      wtls_decrypt_pdu_list(wtls_machine,
                  event->u.T_Unitdata_Ind.pdu_list);
   }

   /* Process the incoming event : Finished */
   tempPayload =
       (wtls_Payload *) gwlist_search(event->u.T_Unitdata_Ind.pdu_list,
                  (void *)finished,
                  match_handshake_type);
   if (!tempPayload) {
      error(0, "Failed to decrypt finished PDU. Aborting...");
      wtls_pdu_destroy(changeCipherSpec_incoming_PDU);
      octstr_destroy(labelMaster);
      octstr_destroy(concatenatedRandoms);
      destroy_rsa_pubkey(public_key);
      octstr_destroy(decryptedData);
      octstr_destroy(encryptedData);
      fatalAlert(event, decrypt_error);
      return;
   }
   finished_incoming_PDU = wtls_pdu_unpack(tempPayload, wtls_machine);
   debug("wtls", 0, "Client Finished PDU:");
   wtls_pdu_dump(finished_incoming_PDU, 0);

   /* Check the verify_data */
   labelVerify = octstr_create("client finished");
   checking_data = wtls_calculate_prf(wtls_machine->master_secret,
                  labelVerify,
                  (Octstr *) wtls_hash(wtls_machine->
                        handshake_data,
                        wtls_machine),
                  12, wtls_machine);

   if (octstr_compare
       (finished_incoming_PDU->u.handshake.finished->verify_data,
        checking_data) == 0) {
      wtls_machine->encrypted = 1;
      debug("wtls", 0, "DATA VERIFICATION OK");
   }

   /* Keep the data so we can send it back in the next message
    * octstr_insert(wtls_machine->handshake_data, tempPayload->data,
    * octstr_len(wtls_machine->handshake_data));
    */
   // temporary fix
   octstr_truncate(tempPayload->data, 15);
   octstr_insert(wtls_machine->handshake_data, tempPayload->data,
            octstr_len(wtls_machine->handshake_data));

   /* Create a new PDU List containing a ChangeCipherSpec and a Finished */
   changeCipherSpecPDU = wtls_pdu_create(ChangeCipher_PDU);
   changeCipherSpecPDU->u.cc.change = 1;
   changeCipherSpecPDU->rlen = 1;
   changeCipherSpecPDU->snMode =
       wtls_machine->sequence_number_mode ? 1 : 0;

   /* Generate our verify data */
   finishedPDU = wtls_pdu_create(Handshake_PDU);
   finishedPDU->u.handshake.msg_type = finished;
   finishedPDU->cipher = 1;
   finishedPDU->rlen = 1;
   finishedPDU->snMode = wtls_machine->sequence_number_mode ? 1 : 0;;
   finishedPDU->u.handshake.finished = gw_malloc(sizeof(Finished));

   octstr_destroy(labelVerify);
   labelVerify = octstr_create("server finished");

   finishedPDU->u.handshake.finished->verify_data = wtls_calculate_prf
       (wtls_machine->master_secret, labelVerify, (Octstr *) wtls_hash
        (wtls_machine->handshake_data, wtls_machine), 12, wtls_machine);

   /* Reset the accumulated Handshake data */
   octstr_destroy(wtls_machine->handshake_data);
   wtls_machine->handshake_data = octstr_create("");

   /* Add the pdus to our list */
   add_pdu(wtls_machine, changeCipherSpecPDU);
   add_pdu(wtls_machine, finishedPDU);

   /* Send it off */
   send_queuedpdus(wtls_machine);

   octstr_destroy(labelMaster);
   octstr_destroy(labelVerify);
   octstr_destroy(decryptedData);
   octstr_destroy(encryptedData);
   octstr_destroy(concatenatedRandoms);

   wtls_pdu_destroy(finished_incoming_PDU);
   wtls_pdu_destroy(changeCipherSpec_incoming_PDU);

   wtls_pdu_destroy(finishedPDU);
   wtls_pdu_destroy(changeCipherSpecPDU);

   octstr_destroy(checking_data);
   destroy_rsa_pubkey(public_key);
}

static void wtls_application(WAPEvent * event, WTLSMachine * wtls_machine)
{
   int listLen, i = 0;
   WAPEvent *dgram;
   wtls_Payload *payLoad;

   /* Apply the negotiated decryption/decoding/MAC check to the received data */
   /* Take the userdata and pass it on up to the WTP/WSP, depending on the destination port */

   listLen = gwlist_len(event->u.T_Unitdata_Ind.pdu_list);
   for (; i < listLen; i++) {
      payLoad = gwlist_consume(event->u.T_Unitdata_Ind.pdu_list);
      dgram = wap_event_create(T_DUnitdata_Ind);
      dgram->u.T_DUnitdata_Ind.addr_tuple =
          wap_addr_tuple_create(event->u.T_Unitdata_Ind.addr_tuple->
                 remote->address,
                 event->u.T_Unitdata_Ind.addr_tuple->
                 remote->port,
                 event->u.T_Unitdata_Ind.addr_tuple->
                 local->address,
                 event->u.T_Unitdata_Ind.addr_tuple->
                 local->port);
      dgram->u.T_DUnitdata_Ind.user_data = payLoad->data;
      wap_dispatch_datagram(dgram);
      payLoad->data = NULL;
      wtls_payload_destroy(payLoad);
   }
}

/*
 * Feed an event to a WTP responder state machine. Handle all errors yourself,
 * do not report them to the caller. Note: Do not put {}s of the else block 
 * inside the macro definition. 
 */
static void wtls_event_handle(WTLSMachine * wtls_machine, WAPEvent * event)
{
     debug("wap.wtls", 0, "WTLS: wtls_machine %ld, state %s, event %s.", 
         wtls_machine->mid, stateName(wtls_machine->state),
	   wap_event_name(event->type));

	/* for T_Unitdata_Ind PDUs */
   if (event->type == T_Unitdata_Ind) {
		/* if encryption: decrypt all pdus in the list */
      if (wtls_machine->encrypted)
         wtls_decrypt_pdu_list(wtls_machine,
                     event->u.T_Unitdata_Ind.pdu_list);
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
      debug("wap.wtls", 0,
            "WTLS: handle_event: Unhandled event was:");
		wap_event_destroy(event);
		return;
	     }

   if (event)
	wap_event_destroy(event);  

   if (wtls_machine->state == NULL_STATE) {
     	wtls_machine_destroy(wtls_machine);
      wtls_machine = NULL;
   }
}

/*
 * Checks whether wtls machines data structure includes a specific machine.
 * The machine in question is identified with with source and destination
 * address and port.
 */

static WTLSMachine *wtls_machine_find_or_create(WAPEvent * event)
{

          WTLSMachine *wtls_machine = NULL;
          long mid;
          WAPAddrTuple *tuple;

          tuple = NULL;
          mid = -1;

   debug("wap.wtls", 0, "event->type = %d", event->type);
		  
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
   if (wtls_machine == NULL) {
      switch (event->type) {
                  case SEC_Create_Request_Req:
                          /* State NULL, case 1 */
         debug("wap.wtls", 0,
               "WTLS: received a SEC_Create_Request_Req, and don't know what to do with it...");
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

static int is_wanted_wtls_machine(void *a, void *b)
{
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

static WTLSMachine *wtls_machine_find(WAPAddrTuple * tuple, long mid)
{
	machine_pattern pat;
	WTLSMachine *m;
	
	pat.tuple = tuple;
	pat.mid = mid;
	
	m = gwlist_search(wtls_machines, &pat, is_wanted_wtls_machine);
	return m;
}

static WTLSMachine *wtls_machine_create(WAPAddrTuple * tuple)
{

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
   wtls_machine->server_seq_num = wtls_machine->client_seq_num = -1;
   wtls_machine->last_refresh = -1;

		wtls_machine->handshake_data = octstr_create("");
		
   debug("wap.wtls", 0, "WTLS: Created WTLSMachine %ld (0x%p)",
         wtls_machine->mid, (void *)wtls_machine);
        return wtls_machine;
}

/*
 * Destroys a WTLSMachine. Assumes it is safe to do so. Assumes it has 
 * already been deleted from the machines list.
 */
static void wtls_machine_destroy(void *p)
{
       WTLSMachine *wtls_machine;

       wtls_machine = p;
   debug("wap.wtls", 0, "WTLS: Destroying WTLSMachine %ld (0x%p)",
         wtls_machine->mid, (void *)wtls_machine);
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

static int wtls_machine_has_mid(void *a, void *b)
{
	WTLSMachine *sm;
	long mid;
	
	sm = a;
   mid = *(long *)b;
	return sm->mid == mid;
}

static WTLSMachine *find_wtls_machine_using_mid(long mid)
{
       return gwlist_search(wtls_machines, &mid, wtls_machine_has_mid);
}

/* Used for list searches */
static int match_handshake_type(void *item, void *pattern)
{
   wtls_Payload *matchingPayload;
        int type;
        int retrievedType;
        
   matchingPayload = (wtls_Payload *) item;
   type = (long)pattern;
        
   if (!matchingPayload->data)
      return (0);
        retrievedType = octstr_get_char(matchingPayload->data, 0);
        
   if (matchingPayload->type == Handshake_PDU && retrievedType == type) {
                return 1;
   } else {
                return 0;
        }        
}

static int match_pdu_type(void *item, void *pattern)
{
   wtls_Payload *matchingPayload;
        int type;
        
   matchingPayload = (wtls_Payload *) item;
   type = (long)pattern;
        
   if (matchingPayload->type == type) {
                return 1;
   } else {
                return 0;
        }        
}

#endif /* HAVE_WTLS_OPENSSL */
