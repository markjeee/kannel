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
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these. 
 *
 * by Nick Clarey <nclarey@3glab.com>
 */

STATE_NAME(NULL_STATE)
STATE_NAME(CREATING)
STATE_NAME(CREATED)
STATE_NAME(EXCHANGE)
STATE_NAME(COMMIT)
STATE_NAME(OPENING)
STATE_NAME(OPEN)

/* If the packet is a ClientHello */
/* We only include this case in state NULL; the others are handled by the
   wtls_find_or_create function */

ROW(NULL_STATE,
    T_Unitdata_Ind,
    1,
    {
        /* The Wap event we have to dispatch */
        WAPEvent *res;
        wtls_Payload* tempPayload;
        wtls_PDU* clientHelloPDU;
        CipherSuite* ciphersuite;
        int randomCounter;
            
        tempPayload = (wtls_Payload*) gwlist_search (event->u.T_Unitdata_Ind.pdu_list,
                                                   (void*) client_hello,
                                                   match_handshake_type);

        clientHelloPDU = wtls_pdu_unpack(tempPayload,wtls_machine);
            
        /* Store the client's random value - use pack for simplicity */
        wtls_machine->client_random = octstr_create("");
        randomCounter = pack_int32(wtls_machine->client_random,0,
                                   clientHelloPDU->u.handshake.client_hello->random->gmt_unix_time);
        octstr_insert(wtls_machine->client_random,
                      clientHelloPDU->u.handshake.client_hello->random->random_bytes,
                      randomCounter);
            
        /* Generate a SEC_Create_Res event, and pass it back into the queue */
        res = wap_event_create(SEC_Create_Res);
        res->u.SEC_Create_Res.addr_tuple =
                wap_addr_tuple_duplicate(event->u.T_Unitdata_Ind.addr_tuple);

        /* Select the ciphersuite from the supplied list */
        ciphersuite = wtls_choose_ciphersuite(clientHelloPDU->u.handshake.client_hello->ciphersuites);

        /* Set the relevant values in the wtls_machine and PDU structure */
        wtls_machine->bulk_cipher_algorithm = ciphersuite->bulk_cipher_algo;
        wtls_machine->mac_algorithm = ciphersuite->mac_algo;
        res->u.SEC_Create_Res.bulk_cipher_algo = ciphersuite->bulk_cipher_algo;
        res->u.SEC_Create_Res.mac_algo = ciphersuite->mac_algo;
        res->u.SEC_Create_Res.client_key_id =
                wtls_choose_clientkeyid(clientHelloPDU->u.handshake.client_hello->client_key_ids);

        /* Set the sequence number mode in both the machine and the outgoing packet */
        res->u.SEC_Create_Res.snmode = wtls_choose_snmode(clientHelloPDU->u.handshake.client_hello->snmode);
        wtls_machine->sequence_number_mode = res->u.SEC_Create_Res.snmode;

        /* Set the key refresh mode in both the machine and the outgoing packet */
        res->u.SEC_Create_Res.krefresh = wtls_choose_krefresh(clientHelloPDU->u.handshake.client_hello->krefresh);
        wtls_machine->key_refresh = res->u.SEC_Create_Res.krefresh;
            
        /* Keep the data so we can send it back in EXCHANGE */
        // temporary - needs to delete old one if exists !
        //wtls_machine->handshake_data = octstr_create("");
        octstr_append(wtls_machine->handshake_data, tempPayload->data);
            
        debug("wtls:handle_event", 0,"Dispatching SEC_Create_Res event");
        wtls_dispatch_event(res);

},
    CREATING)

/* Creating State */
/* Termination */
ROW(CREATING,
    SEC_Terminate_Req,
    1,
    {
/* Send off a T_Unitdata_Req containing an alert as specified */
            send_alert(event->u.SEC_Terminate_Req.alert_level,
                       event->u.SEC_Terminate_Req.alert_desc,
                       wtls_machine);
    },
    NULL_STATE)

/* Exception */
ROW(CREATING,
    SEC_Exception_Req,
    1,
    {
            /* Send off a T_Unitdata_Req containing an exception as specified */
            send_alert(event->u.SEC_Exception_Req.alert_level,
                       event->u.SEC_Exception_Req.alert_desc, wtls_machine);
    },
    CREATING)

/* Create Response - create a buffer with a "ServerHello" and possibly a Certificate or something else */
ROW(CREATING,
    SEC_Create_Res,
    1,
    {
            WAPEvent *req;
            wtls_PDU* serverHelloPDU;
            Random* tempRandom;
            int randomCounter = 0;
            
            /* Our serverHello */
            serverHelloPDU = wtls_pdu_create(Handshake_PDU);
            serverHelloPDU->u.handshake.msg_type = server_hello;
            serverHelloPDU->u.handshake.server_hello = (ServerHello*) gw_malloc(sizeof(ServerHello));
            
            /* Set our server version */
            serverHelloPDU->u.handshake.server_hello->serverversion = 1;
            
            /* Get a suitably random number - store it in both the machine structure and outgoing PDU */
            tempRandom = wtls_get_random();
            wtls_machine->server_random = octstr_create("");
            randomCounter = pack_int32(wtls_machine->server_random,0,tempRandom->gmt_unix_time);
            octstr_insert(wtls_machine->server_random,tempRandom->random_bytes,octstr_len(wtls_machine->server_random));
            
            serverHelloPDU->u.handshake.server_hello->random = tempRandom;
            
            /* At the moment, we don't support session caching, so tell them to forget about caching us */
            serverHelloPDU->u.handshake.server_hello->session_id = octstr_create("");
            
            /* We need to select an appropriate mechanism here from the ones listed */
            serverHelloPDU->u.handshake.server_hello->client_key_id = event->u.SEC_Create_Res.client_key_id;
            
            /* Get our ciphersuite details */
            serverHelloPDU->u.handshake.server_hello->ciphersuite = (CipherSuite*) gw_malloc(sizeof(CipherSuite));
            serverHelloPDU->u.handshake.server_hello->ciphersuite->bulk_cipher_algo = event->u.SEC_Create_Res.bulk_cipher_algo;
            serverHelloPDU->u.handshake.server_hello->ciphersuite->mac_algo = event->u.SEC_Create_Res.mac_algo;            
            serverHelloPDU->u.handshake.server_hello->comp_method = null_comp;
            
            /* We need to confirm the client's choice, or if they haven't specified one, select
               one ourselves */
            serverHelloPDU->u.handshake.server_hello->snmode = event->u.SEC_Create_Res.snmode;
            
            /* We need to either confirm the client's choice of key refresh rate, or choose a lower rate */
            serverHelloPDU->u.handshake.server_hello->krefresh = event->u.SEC_Create_Res.krefresh;
            
            /* Add the PDUsto the server's outgoing list  */
            add_pdu(wtls_machine, serverHelloPDU);            
            
            /* Generate and dispatch a SEC_Exchange_Req or maybe a SEC_Commit_Req */
            req = wap_event_create(SEC_Exchange_Req);
            req->u.SEC_Exchange_Req.addr_tuple =
                    wap_addr_tuple_duplicate(event->u.T_Unitdata_Ind.addr_tuple);
            wtls_dispatch_event(req);
            debug("wtls: handle_event", 0,"Dispatching SEC_Exchange_Req event");
            
    },
    CREATED)

/* Created State */
/* Exchange Request - Full Handshake will be performed */
ROW(CREATED,
    SEC_Exchange_Req,
    1,
    {
            wtls_PDU* serverKeyXchgPDU;
            wtls_PDU* serverHelloDonePDU;
            
            /* Assert that the PDU list is valid */
            gw_assert(wtls_machine->packet_to_send != NULL);
            
            /* We'll also need a Server Key Exchange message */
            serverKeyXchgPDU = wtls_pdu_create(Handshake_PDU);
            serverKeyXchgPDU->u.handshake.msg_type = server_key_exchange;
            serverKeyXchgPDU->u.handshake.server_key_exchange = (ServerKeyExchange*) gw_malloc(sizeof(ServerKeyExchange));
            serverKeyXchgPDU->u.handshake.server_key_exchange->param_spec = NULL;
            
            /* Allocate memory for the RSA component */
            debug("wtls: ", 0,"Going to get the RSA public key...");
            serverKeyXchgPDU->u.handshake.server_key_exchange->rsa_params = wtls_get_rsapublickey();
            debug("wtls: ", 0,"...got it.");
            add_pdu(wtls_machine, serverKeyXchgPDU);            
            debug("wtls: ", 0,"in CREATED - just added pdu...");

            /* Add some more PDUs to the List - potentially a ServerKeyExchange,
               a CertificateRequest and a ServerHelloDone */
            /* Just a ServerHelloDone for now */
            serverHelloDonePDU = wtls_pdu_create(Handshake_PDU);
            serverHelloDonePDU->u.handshake.msg_type = server_hello_done;
            add_pdu(wtls_machine, serverHelloDonePDU);
            
            /* Translate the buffer and address details into a T_Unitdata_Req
             * and send it winging it's way across the network */
            send_queuedpdus(wtls_machine);
    },
    EXCHANGE)

/* Commit Request - Abbreviated Handshake will be performed */
ROW(CREATED,
    SEC_Commit_Req,
    1,
{
        /* Assert that the PDU list is valid */
        /* Add some more PDUs to the List - a ChangeCipherSpec and a Finished */
        /* Translate the buffer and address details into a T_Unitdata_Req */
        /* And send it winging it's way across the network */
},
        COMMIT)

/* Terminate Request */
ROW(CREATED,
    SEC_Terminate_Req,
    1,
    {
            /* Send off a T_Unitdata_Req containing an alert as specified */
            send_alert(event->u.SEC_Terminate_Req.alert_level,
                       event->u.SEC_Terminate_Req.alert_desc, wtls_machine);
    },
    NULL_STATE)

/* Exception Request */
ROW(CREATED,
    SEC_Exception_Req,
    1,
    {
            /* Send off a T_Unitdata_Req containing an exception as specified */
            send_alert(event->u.SEC_Exception_Req.alert_level,
                       event->u.SEC_Exception_Req.alert_desc, wtls_machine);
    },
    CREATED)

/* Exchange State */
/* Unitdata arrival - identical ClientHello record */
ROW(EXCHANGE,
    T_Unitdata_Ind,
    clienthellos_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1,
        {
                /* It appears as though someone has sent us an identical ClientHello to the last one */
                /* Make ourselves a T_Unitdata_Req with the last_transmitted_packet */
                /* And send it on it's merry  */
        },
    EXCHANGE)

/* Unitdata arrival - non-identical ClientHello record */
//ROW(EXCHANGE,
//    T_Unitdata_Ind,
//    clienthellos_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) != 1,
//    {
/* So, this one's different. They must have changed their mind about something, so try a CREATING again */
/* Do the necessary SEC_Create_Ind stuff */
//    },
//    CREATING)

/* Unitdata arrival - good packet */
ROW(EXCHANGE,
    T_Unitdata_Ind,
    1,
    {
            RSAPublicKey *public_key = NULL;
            Octstr* key_block;
            Octstr* final_client_write_enc_key = NULL;
            Octstr* final_server_write_enc_key = NULL;
            Octstr* final_client_write_IV = NULL;
            Octstr* final_server_write_IV = NULL;
            Octstr* emptySecret = NULL;
			Octstr* checking_data = NULL;
                        
            // packet_contains_changecipherspec (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
            // packet_contains_finished (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
            // packet_contains_optional_stuff (event->u.T_Unitdata_Ind.pdu_list) == 1,

            /* The Wap PDUs we have to dispatch */
            wtls_PDU* changeCipherSpecPDU;
            wtls_PDU* finishedPDU;
            
            /* The PDUs we have to process */
            wtls_Payload* tempPayload;
            wtls_PDU* clientKeyXchgPDU;
            wtls_PDU* changeCipherSpec_incoming_PDU;
            wtls_PDU* finished_incoming_PDU;

            /* For decrypting/encrypting data */
            Octstr* concatenatedRandoms=0;
            Octstr* encryptedData=0;
            Octstr* decryptedData=0;
            Octstr* labelVerify=0;
            Octstr* labelMaster=0;
            
            /* Process the incoming event : ClientKeyExchange*/            
            tempPayload = (wtls_Payload*) gwlist_search (event->u.T_Unitdata_Ind.pdu_list,
                                                      (void*) client_key_exchange,
                                                      match_handshake_type);

            /* Keep the data so we can send it back */
            octstr_insert(wtls_machine->handshake_data, tempPayload->data,
                          octstr_len(wtls_machine->handshake_data));
                                     
            clientKeyXchgPDU = wtls_pdu_unpack(tempPayload,wtls_machine);
            wtls_pdu_dump(clientKeyXchgPDU,0);
                        
            /* Decrypt the client key exchange PDU */
            encryptedData = clientKeyXchgPDU->u.handshake.client_key_exchange->rsa_params->encrypted_secret;
            decryptedData = wtls_decrypt_rsa(encryptedData);
			
            public_key = wtls_get_rsapublickey();
            pack_int16(decryptedData, octstr_len(decryptedData), octstr_len(public_key->rsa_exponent));
            octstr_insert(decryptedData, public_key->rsa_exponent, octstr_len(decryptedData));
            pack_int16(decryptedData, octstr_len(decryptedData), octstr_len(public_key->rsa_modulus));
            octstr_insert(decryptedData, public_key->rsa_modulus, octstr_len(decryptedData));

            /* Concatenate our random data */
            concatenatedRandoms = octstr_cat(wtls_machine->client_random,
                                             wtls_machine->server_random);
         
            /* Generate our master secret */
            labelMaster = octstr_create("master secret");
            wtls_machine->master_secret = wtls_calculate_prf(decryptedData, labelMaster,
                                              concatenatedRandoms,20, wtls_machine );
            octstr_destroy(labelMaster);
            labelMaster = NULL;

			/* calculate the key blocks */
			calculate_server_key_block(wtls_machine);
			calculate_client_key_block(wtls_machine);
                        
            /* Process the incoming event : ChangeCipherSpec*/            
            tempPayload = (wtls_Payload*) gwlist_search (event->u.T_Unitdata_Ind.pdu_list,
                                                      (void*) ChangeCipher_PDU,
                                                      match_pdu_type);

            changeCipherSpec_incoming_PDU = wtls_pdu_unpack(tempPayload, wtls_machine);
            if(changeCipherSpec_incoming_PDU->u.cc.change == 1) {
                debug("wtls", 0,"Need to decrypt the PDUs from now on...");
                wtls_machine->encrypted = 1;
                wtls_decrypt_pdu_list(wtls_machine, event->u.T_Unitdata_Ind.pdu_list);
            }

			octstr_dump(wtls_machine->client_write_MAC_secret,0);
            
            wtls_pdu_dump(changeCipherSpec_incoming_PDU,0);

            /* Process the incoming event : Finished*/            
            tempPayload = (wtls_Payload*) gwlist_search (event->u.T_Unitdata_Ind.pdu_list,
                                                      (void*) finished,
                                                      match_handshake_type);
            if(tempPayload == NULL)
                debug("wtls", 0, "null finished !!!");
            
            finished_incoming_PDU = wtls_pdu_unpack(tempPayload,wtls_machine);
            debug("wtls", 0, "Client Finished PDU:");
            wtls_pdu_dump(finished_incoming_PDU,0);

            /* Check the verify_data */
            labelVerify = octstr_create("client finished");
			checking_data = wtls_calculate_prf(wtls_machine->master_secret, labelVerify,
			                  (Octstr *)wtls_hash(wtls_machine->handshake_data, wtls_machine),
							  12, wtls_machine);
            octstr_destroy(labelVerify);
            labelVerify = NULL;
			
			if(octstr_compare(finished_incoming_PDU->u.handshake.finished->verify_data, checking_data)==0) {
				debug("wtls", 0, "DATA VERIFICATION OK");
			}
			
            /* Keep the data so we can send it back in the next message */
            /*octstr_insert(wtls_machine->handshake_data, tempPayload->data,
                          octstr_len(wtls_machine->handshake_data));
			*/
			// temporary fix
			octstr_truncate(tempPayload->data, 15);
            octstr_insert(wtls_machine->handshake_data, tempPayload->data,
                          octstr_len(wtls_machine->handshake_data));
                                     
            /* Create a new PDU List containing a ChangeCipherSpec and a Finished */
            changeCipherSpecPDU = wtls_pdu_create(ChangeCipher_PDU);
            changeCipherSpecPDU->u.cc.change = 1;
            
            /* Generate our verify data */
            finishedPDU = wtls_pdu_create(Handshake_PDU);
            finishedPDU->u.handshake.msg_type = finished;
            finishedPDU->cipher = 1;
            finishedPDU->u.handshake.finished = gw_malloc(sizeof(Finished));
            
            labelVerify = octstr_create("server finished");

            finishedPDU->u.handshake.finished->verify_data = wtls_calculate_prf(wtls_machine->master_secret,
                                            labelVerify,(Octstr *)wtls_hash(wtls_machine->handshake_data, wtls_machine),
											12,wtls_machine);
                       
            /* Reset the accumulated Handshake data */
            octstr_destroy(wtls_machine->handshake_data);
            wtls_machine->handshake_data = octstr_create("");
                        
            octstr_destroy(labelVerify);
            labelVerify = NULL;
            
            /* Add the pdus to our list */
            add_pdu(wtls_machine, changeCipherSpecPDU);            
            add_pdu(wtls_machine, finishedPDU);
            
            /* Send it off */
            send_queuedpdus(wtls_machine);
                        
            /* reset the seq_num */
            wtls_machine->server_seq_num = 0;
    },
    OPENING)

/* Unitdata arrival - critical/fatal alert */
ROW(EXCHANGE,
        T_Unitdata_Ind,
        is_critical_alert(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Do the necessary SEC_Terminate_Ind stuff */
/* And we're dead :-< */
},
        NULL_STATE)

/* Unitdata arrival - warning alert */
ROW(EXCHANGE,
        T_Unitdata_Ind,
        is_warning_alert(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Do the necessary SEC_Exception_Ind stuff */
},
        EXCHANGE)

/* Terminate */
ROW(EXCHANGE,
        SEC_Terminate_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an alert as specified */
send_alert(event->u.SEC_Terminate_Req.alert_level,
        event->u.SEC_Terminate_Req.alert_desc, wtls_machine);
},
        NULL_STATE)

/* Exception */
ROW(EXCHANGE,
        SEC_Exception_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an exception as specified */
send_alert(event->u.SEC_Exception_Req.alert_level,
        event->u.SEC_Exception_Req.alert_desc, wtls_machine);
},
        EXCHANGE)

/* Commit State */
/* Unitdata arrival - identical ClientHello record */
ROW(COMMIT,
        T_Unitdata_Ind,
        clienthellos_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1,
        {
/* It appears as though someone has sent us an identical ClientHello to the last one */
/* Make ourselves a T_Unitdata_Req with the last_transmitted_packet */
/* And send it on it's merry way */
},
        COMMIT)

/* Unitdata arrival - non-identical ClientHello record */
ROW(COMMIT,
        T_Unitdata_Ind,
        clienthellos_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) != 1,
        {
/* So, this one's different. They must have changed their mind about something, so try a CREATING again */
/* Do the necessary SEC_Create_Ind stuff */
},
        CREATING)

/* Unitdata arrival - good packet with ChangeCipherSpec and Finished */
ROW(COMMIT,
        T_Unitdata_Ind,
        packet_contains_changecipherspec (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_finished (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_userdata (event->u.T_Unitdata_Ind.pdu_list) != 1,
        {
/* Create ourselves a SEC_Commit_Cnf packet to send off */
/* Send it off */
},
        OPEN)

/* Unitdata arrival - good packet with ChangeCipherSpec, Finished and UD */
ROW(COMMIT,
        T_Unitdata_Ind,
        packet_contains_changecipherspec (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_finished (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_userdata (event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Create a SEC_Commit_Cnf packet to send off */
/* Send it off */
/* Relay the contents of the packets up to the WTP or WSP layers,
   depending on the destination port */
},
        OPEN)

/* Unitdata arrival - critical/fatal alert */
ROW(COMMIT,
        T_Unitdata_Ind,
        is_critical_alert(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Do the necessary SEC_Terminate_Ind stuff */
/* And we're dead :-< */
},
        NULL_STATE)

/* Unitdata arrival - warning alert */
ROW(COMMIT,
        T_Unitdata_Ind,
        is_warning_alert(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Do the necessary SEC_Exception_Ind stuff */
},
        COMMIT)

/* Terminate */
ROW(COMMIT,
        SEC_Terminate_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an alert as specified */
send_alert(event->u.SEC_Terminate_Req.alert_level,
        event->u.SEC_Terminate_Req.alert_desc, wtls_machine);
},
        NULL_STATE)

/* Exception */
ROW(COMMIT,
        SEC_Exception_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an exception as specified */
send_alert(event->u.SEC_Exception_Req.alert_level,
        event->u.SEC_Exception_Req.alert_desc, wtls_machine);
},
        COMMIT)

/* Opening State */
/* Create Request */
ROW(OPENING,
        SEC_Create_Request_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing a HelloRequest */
},
        OPENING)

/* Send out UnitData */
ROW(OPENING,
        SEC_Unitdata_Req,
        1,
        {
/* Apply the negotiated security "stuff" to the received packet */
/* Send out the packet to the destination port/address requested */
},
        OPENING)

/* Unitdata received - ClientHello */
//ROW(OPENING,
//        T_Unitdata_Ind,
//        packet_contains_clienthello (event->u.T_Unitdata_Ind.pdu_list) == 0,
//        {
///* Hmm, they're obviously not happy with something we discussed, so let's head back to creating */
///* Do the necessary SEC_Create_Ind stuff */
//},
//        CREATING)

/* Unitdata received */
ROW(OPENING,
        T_Unitdata_Ind,
        packet_contains_userdata(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
            wtls_Payload* tempPayload;
            wtls_PDU* ApplicationPDU;
			
            tempPayload = (wtls_Payload*) gwlist_search (event->u.T_Unitdata_Ind.pdu_list,
                                                      (void*) Application_PDU,
                                                      match_pdu_type);

            if(tempPayload == NULL)
                debug("wtls", 0, "no App PDU found in list !!!");
            
			debug("wtls",0, "PDU type: %d", tempPayload->type);
			octstr_dump(tempPayload->data,0);

			ApplicationPDU = wtls_pdu_unpack(tempPayload, wtls_machine);
			
			wtls_pdu_dump(ApplicationPDU,0);
			
			/* Apply the negotiated decryption/decoding/MAC check to the received data */
			/* Take the userdata and pass it on up to the WTP/WSP, depending on the destination port */
			
			/* calculate the padding length */
	        /*
			contentLength = octstr_len(bufferCopy);
    	    macSize = hash_table[wtls_machine->mac_algorithm].mac_size;
        	blockLength = bulk_table[wtls_machine->bulk_cipher_algorithm].block_size;
			paddingLength = (contentLength + macSize + 1) % (blockLength);
			*/
			/* get the MAC */
},
        OPEN)

/* Unitdata arrival - Certificate, ClientKeyExchange ... Finished */
ROW(OPENING,
        T_Unitdata_Ind,
        certificates_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1 &&
clientkeyexchanges_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1 && 
certifcateverifys_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1 &&
changecipherspecs_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1 &&
finisheds_are_indentical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1,
        {
/* It appears as though someone has sent us an identical ClientHello to the last one */
/* Make ourselves a T_Unitdata_Req with the last_transmitted_packet */
/* And send it on it's merry way */
},
        OPENING)

/* Unitdata arrival - critical/fatal alert */
ROW(OPENING,
        T_Unitdata_Ind,
        is_critical_alert(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Do the necessary SEC_Terminate_Ind stuff */
/* And we're dead :-< */
},
        NULL_STATE)

/* Unitdata arrival - warning alert */
ROW(OPENING,
        T_Unitdata_Ind,
        is_warning_alert(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Do the necessary SEC_Exception_Ind stuff */
},
        OPENING)

/* Terminate */
ROW(OPENING,
        SEC_Terminate_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an alert as specified */
send_alert(event->u.SEC_Terminate_Req.alert_level,
        event->u.SEC_Terminate_Req.alert_desc, wtls_machine);
},
        NULL_STATE)

/* Exception */
ROW(OPENING,
        SEC_Exception_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an exception as specified */
send_alert(event->u.SEC_Exception_Req.alert_level,
        event->u.SEC_Exception_Req.alert_desc, wtls_machine);
},
        OPENING)

/* Open State */
/* Create Request */
ROW(OPEN,
        SEC_Create_Request_Req,
        1,
        {
/* Send off a T_Unitdata_Req with a HelloRequest */
},
        OPEN)

/* Send out UnitData */
ROW(OPEN,
        SEC_Unitdata_Req,
        1,
        { 
/* Apply the negotiated security "stuff" to the received packet */
/* Send out the packet to the destination port/address requested */
},
        OPEN)

/* Unitdata received - ClientHello */
ROW(OPEN,
        T_Unitdata_Ind,
        packet_contains_clienthello (event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Hmm, they're obviously not happy with something we discussed, so let's head back to creating */
/* Do the necessary SEC_Create_Ind stuff */
},
        CREATING)

/* Unitdata received */
ROW(OPEN,
        T_Unitdata_Ind,
        packet_contains_userdata(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Apply the negotiated decryption/decoding/MAC check to the received data */
/* Take the userdata and pass it on up to the WTP/WSP, depending on the destination port */
},
        OPEN)

/* Unitdata arrival - ChangeCipherSpec, Finished */
ROW(OPEN,
        T_Unitdata_Ind,
        packet_contains_changecipherspec (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_finished (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_userdata(event->u.T_Unitdata_Ind.pdu_list) != 1 &&
finisheds_are_indentical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1,
        {
/* Just send out a T_Unitdata_Req with an Alert(duplicate_finished_received) */
},
        OPEN)

/* Unitdata arrival - ChangeCipherSpec, Finished and UD */
ROW(OPEN,
        T_Unitdata_Ind,
        packet_contains_changecipherspec (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_finished (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_userdata(event->u.T_Unitdata_Ind.pdu_list) == 1 &&
finisheds_are_indentical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1,
        {
/* Apply the negotiated decryption/decoding/MAC check to the received data */
/* Take the userdata and pass it on up to the WTP/WSP, depending on the destination port */
/* Send out a T_Unitdata_Req with an Alert(duplicate_finished_received) */
},
        OPEN)

/* Unitdata arrival - critical/fatal alert */
ROW(OPEN,
        T_Unitdata_Ind,
        is_critical_alert(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Do the necessary SEC_Terminate_Ind stuff */
/* And we're dead :-< */
},
        NULL_STATE)

/* Unitdata arrival - warning alert */
ROW(OPEN,
        T_Unitdata_Ind,
        is_warning_alert(event->u.T_Unitdata_Ind.pdu_list) == 1,
        {
/* Do the necessary SEC_Terminate_Ind stuff */
},
        OPEN)

/* Terminate */
ROW(OPEN,
        SEC_Terminate_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an alert as specified */
send_alert(event->u.SEC_Terminate_Req.alert_level,
        event->u.SEC_Terminate_Req.alert_desc, wtls_machine);
},
        NULL_STATE)

/* Exception */
ROW(OPEN,
        SEC_Exception_Req,
        1,
    {
/* Send off a T_Unitdata_Req containing an exception as specified */
send_alert(event->u.SEC_Exception_Req.alert_level,
        event->u.SEC_Exception_Req.alert_desc, wtls_machine);
},
        OPEN)

#undef ROW
#undef STATE_NAME
