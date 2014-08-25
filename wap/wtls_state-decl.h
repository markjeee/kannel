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
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these. 
 *
 * by Nick Clarey <nclarey@3glab.com>
 * Nikos Balkanas, Inaccess Networks (2009)
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
   packet_contains_clienthello (event->u.T_Unitdata_Ind.pdu_list),
    {
      clientHello(event, wtls_machine);
   },
   CREATING)
            
ROW(NULL_STATE,
   T_Unitdata_Ind,
   1,
   {
      WAPEvent *alert;

      error(0, "send_alert ~> Critical alert (unexpected_message), while waiting for client hello.");
      alert = wap_event_create(SEC_Terminate_Req);
      alert->u.SEC_Terminate_Req.addr_tuple = wap_addr_tuple_duplicate(event->u.T_Unitdata_Ind.addr_tuple);
      alert->u.SEC_Terminate_Req.alert_desc = unexpected_message;
      alert->u.SEC_Terminate_Req.alert_level = critical_alert;
      wtls_dispatch_event(alert);
   },
    CREATING)

/* Creating State */
/* Termination */
ROW(CREATING,
    SEC_Terminate_Req,
    1,
    {
/* Send off a T_Unitdata_Req containing an alert as specified */
      send_alert(event, wtls_machine);
    },
    NULL_STATE)

/* Exception */
ROW(CREATING,
    SEC_Exception_Req,
    1,
    {
            /* Send off a T_Unitdata_Req containing an exception as specified */
            send_alert(event, wtls_machine);
    },
    CREATING)

/* Create Response - create a buffer with a "ServerHello" and possibly a Certificate or something else */
ROW(CREATING,
    SEC_Create_Res,
    1,
    {
       serverHello(event, wtls_machine);
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
      serverKeyXchgPDU->rlen = 1;
      serverKeyXchgPDU->snMode = wtls_machine->sequence_number_mode? 1: 0;
            serverKeyXchgPDU->u.handshake.msg_type = server_key_exchange;
      serverKeyXchgPDU->u.handshake.server_key_exchange =
         (ServerKeyExchange*) gw_malloc(sizeof(ServerKeyExchange));
            serverKeyXchgPDU->u.handshake.server_key_exchange->param_spec = NULL;
            
            /* Allocate memory for the RSA component */
            debug("wtls: ", 0,"Going to get the RSA public key...");
      serverKeyXchgPDU->u.handshake.server_key_exchange->rsa_params =
         wtls_get_rsapublickey();
            debug("wtls: ", 0,"...got it.");
            add_pdu(wtls_machine, serverKeyXchgPDU);            
      wtls_pdu_destroy(serverKeyXchgPDU);
            debug("wtls: ", 0,"in CREATED - just added pdu...");

            /* Add some more PDUs to the List - potentially a ServerKeyExchange,
               a CertificateRequest and a ServerHelloDone */
            /* Just a ServerHelloDone for now */
            serverHelloDonePDU = wtls_pdu_create(Handshake_PDU);
      serverHelloDonePDU->rlen = 1;
      serverHelloDonePDU->snMode = wtls_machine->sequence_number_mode? 1: 0;
            serverHelloDonePDU->u.handshake.msg_type = server_hello_done;
            add_pdu(wtls_machine, serverHelloDonePDU);
      wtls_pdu_destroy(serverHelloDonePDU);
            
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
            send_alert(event, wtls_machine);
    },
    NULL_STATE)

/* Exception Request */
ROW(CREATED,
    SEC_Exception_Req,
    1,
    {
            /* Send off a T_Unitdata_Req containing an exception as specified */
            send_alert(event, wtls_machine);
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

/* Unitdata arrival - warning alert */
ROW(EXCHANGE,
    T_Unitdata_Ind,
        is_warning_alert(event->u.T_Unitdata_Ind.pdu_list, wtls_machine),
    {
/* Do the necessary SEC_Exception_Ind stuff */
    },
        EXCHANGE)

/* Unitdata arrival - critical/fatal alert */
ROW(EXCHANGE,
        T_Unitdata_Ind,
        is_critical_alert(event->u.T_Unitdata_Ind.pdu_list, wtls_machine),
        {
/* Do the necessary SEC_Terminate_Ind stuff */
/* And we're dead :-< */
        },
        NULL_STATE)

/* Terminate */
ROW(EXCHANGE,
        SEC_Terminate_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an alert as specified */
         send_alert(event, wtls_machine);
        },
        NULL_STATE)

/* Unitdata arrival - good packet */
ROW(EXCHANGE,
    T_Unitdata_Ind,
    1,
    {
       exchange_keys(event, wtls_machine);
    },
    OPENING)


/* Exception */
ROW(EXCHANGE,
        SEC_Exception_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an exception as specified */
         send_alert(event, wtls_machine);
        },
        EXCHANGE)

/* Commit State */
/* Unitdata arrival - identical ClientHello record */
ROW(COMMIT,
        T_Unitdata_Ind,
        clienthellos_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet),
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
        packet_contains_changecipherspec (event->u.T_Unitdata_Ind.pdu_list) &&
        packet_contains_finished (event->u.T_Unitdata_Ind.pdu_list) &&
        packet_contains_userdata (event->u.T_Unitdata_Ind.pdu_list),
        {
/* Create ourselves a SEC_Commit_Cnf packet to send off */
/* Send it off */
},
        OPEN)

/* Unitdata arrival - good packet with ChangeCipherSpec, Finished and UD */
ROW(COMMIT,
        T_Unitdata_Ind,
        packet_contains_changecipherspec (event->u.T_Unitdata_Ind.pdu_list) &&
        packet_contains_finished (event->u.T_Unitdata_Ind.pdu_list) &&
        packet_contains_userdata (event->u.T_Unitdata_Ind.pdu_list),
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
        is_critical_alert(event->u.T_Unitdata_Ind.pdu_list, wtls_machine),
        {
/* Do the necessary SEC_Terminate_Ind stuff */
/* And we're dead :-< */
},
        NULL_STATE)

/* Unitdata arrival - warning alert */
ROW(COMMIT,
        T_Unitdata_Ind,
        is_warning_alert(event->u.T_Unitdata_Ind.pdu_list, wtls_machine),
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
         send_alert(event, wtls_machine);
        },
        NULL_STATE)

/* Exception */
ROW(COMMIT,
        SEC_Exception_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an exception as specified */
         send_alert(event, wtls_machine);
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
        packet_is_application_data(event->u.T_Unitdata_Ind.pdu_list),
        {
         wtls_application(event, wtls_machine);
        },
        OPEN)

/* Unitdata arrival - Certificate, ClientKeyExchange ... Finished */
ROW(OPENING,
        T_Unitdata_Ind,
        certificates_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1 &&
clientkeyexchanges_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1 && 
certifcateverifys_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1 &&
changecipherspecs_are_identical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1 &&
finishes_are_indentical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1,
        {
/* It appears as though someone has sent us an identical ClientHello to the last one */
/* Make ourselves a T_Unitdata_Req with the last_transmitted_packet */
/* And send it on it's merry way */
},
        OPENING)

/* Unitdata arrival - critical/fatal alert */
ROW(OPENING,
        T_Unitdata_Ind,
        is_critical_alert(event->u.T_Unitdata_Ind.pdu_list, wtls_machine),
        {
/* Do the necessary SEC_Terminate_Ind stuff */
/* And we're dead :-< */
},
        NULL_STATE)

/* Unitdata arrival - warning alert */
ROW(OPENING,
        T_Unitdata_Ind,
        is_warning_alert(event->u.T_Unitdata_Ind.pdu_list, wtls_machine),
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
         send_alert(event, wtls_machine);
        },
        NULL_STATE)

/* Exception */
ROW(OPENING,
        SEC_Exception_Req,
        1,
        {
/* Send off a T_Unitdata_Req containing an exception as specified */
            send_alert(event, wtls_machine);
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
        packet_contains_clienthello (event->u.T_Unitdata_Ind.pdu_list),
        {
/* Hmm, they're obviously not happy with something we discussed, so let's head back to creating */
/* Do the necessary SEC_Create_Ind stuff */
          wtls_machine->encrypted = 0;
          wtls_machine->last_refresh = -1;
          clientHello(event, wtls_machine);
        },
        CREATING)

/* Unitdata received */
ROW(OPEN,
        T_Unitdata_Ind,
        packet_is_application_data(event->u.T_Unitdata_Ind.pdu_list),
        {
         wtls_application(event, wtls_machine);
        },
        OPEN)

/* Unitdata arrival - ChangeCipherSpec, Finished */
ROW(OPEN,
        T_Unitdata_Ind,
        packet_contains_changecipherspec (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_finished (event->u.T_Unitdata_Ind.pdu_list) == 1 &&
packet_contains_userdata(event->u.T_Unitdata_Ind.pdu_list) != 1 &&
finishes_are_indentical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1,
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
finishes_are_indentical(event->u.T_Unitdata_Ind.pdu_list, wtls_machine->last_received_packet) == 1,
        {
/* Apply the negotiated decryption/decoding/MAC check to the received data */
/* Take the userdata and pass it on up to the WTP/WSP, depending on the destination port */
/* Send out a T_Unitdata_Req with an Alert(duplicate_finished_received) */
},
        OPEN)

/* Unitdata arrival - critical/fatal alert */
ROW(OPEN,
        T_Unitdata_Ind,
        is_critical_alert(event->u.T_Unitdata_Ind.pdu_list, wtls_machine),
        {
/* Do the necessary SEC_Terminate_Ind stuff */
/* And we're dead :-< */
},
        NULL_STATE)

/* Unitdata arrival - warning alert */
ROW(OPEN,
        T_Unitdata_Ind,
        is_warning_alert(event->u.T_Unitdata_Ind.pdu_list, wtls_machine),
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
         send_alert(event, wtls_machine);
        },
        NULL_STATE)

/* Exception */
ROW(OPEN,
        SEC_Exception_Req,
        1,
    {
/* Send off a T_Unitdata_Req containing an exception as specified */
         send_alert(event, wtls_machine);
        },
        OPEN)

#undef ROW
#undef STATE_NAME
