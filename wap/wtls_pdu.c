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
 * wtls_pdu.c: pack and unpack WTLS packets
 *
 * Generates packing and unpacking code from wtls_pdu.def.
 *
 */

#include "gwlib/gwlib.h"

#if (HAVE_WTLS_OPENSSL)

#include "gwlib/octstr.h"
#include "wtls_pdu.h"
#include "wtls_pdusupport.h"
#include "wtls_statesupport.h"

KeyExchangeSuite client_key_exchange_algo = rsa_anon;
PublicKeyAlgorithm public_key_algo;
SignatureAlgorithm signature_algo;
int seqnum;

wtls_Payload* wtls_payload_unpack_from_offset (Octstr *data, int *offset);

wtls_PDU *wtls_pdu_create(int type) {
        wtls_PDU *pdu;
        
        pdu = gw_malloc(sizeof(*pdu));
        pdu->type = type;
        pdu->reserved = 0;
        pdu->cipher = 0;
        pdu->seqnum = 0;
        pdu->rlen = 0;
        
        switch (pdu->type) {
        case ChangeCipher_PDU:
                pdu->u.cc.change = 1;
				break;
        case Alert_PDU:
                pdu->u.alert.level = 0;
                pdu->u.alert.desc = 0;
                pdu->u.alert.chksum = 0;
				break;
        case Handshake_PDU:
                pdu->u.handshake.msg_type = 0;
                pdu->u.handshake.length = 0;
				break;
        case Application_PDU:
                pdu->u.application.data = NULL;
				break;
        default:
                warning(0, "Cannot create unknown WTLS PDU type %d", pdu->type);
                break;
        }

        return pdu;
}

void wtls_payload_destroy(wtls_Payload *payload) {
		octstr_destroy(payload->data);
		gw_free(payload);
}

void wtls_pdu_destroy(wtls_PDU *pdu) {
        if (pdu == NULL)
                return;

        switch (pdu->type) {
        case ChangeCipher_PDU:
				/* no memory was allocated for ChangeCipher_PDU */
 				break;
        case Alert_PDU:
				octstr_destroy(pdu->u.alert.chksum	);
				break;
        case Handshake_PDU:
                switch (pdu->u.handshake.msg_type) {
                case hello_request:
						break;
                case client_hello:
						destroy_random(pdu->u.handshake.client_hello->random);
						octstr_destroy(pdu->u.handshake.client_hello->session_id);
						destroy_key_list(pdu->u.handshake.client_hello->client_key_ids);
						destroy_key_list(pdu->u.handshake.client_hello->trusted_key_ids);
						destroy_ciphersuite_list(pdu->u.handshake.client_hello->ciphersuites);
						destroy_compression_method_list(pdu->u.handshake.client_hello->comp_methods);
						/* destroy the client_hello struct */
						gw_free(pdu->u.handshake.client_hello);
						break;
                case server_hello:
						destroy_random(pdu->u.handshake.server_hello->random);
						octstr_destroy(pdu->u.handshake.server_hello->session_id);
						/* destroy the server_hello struct */
						gw_free(pdu->u.handshake.server_hello);
						break;
                case certificate:
                        switch (pdu->u.handshake.certificate->certificateformat) {
                        case WTLSCert:
								destroy_wtls_certificate(pdu->u.handshake.certificate->wtls_certificate);
                                break;
                        case X509Cert:
								octstr_destroy(pdu->u.handshake.certificate->x509_certificate);
                                break;
                        case X968Cert:
								octstr_destroy(pdu->u.handshake.certificate->x968_certificate);
                                break;
                        }
						gw_free(pdu->u.handshake.certificate);
						break;
                case server_key_exchange:
						destroy_param_spec(pdu->u.handshake.server_key_exchange->param_spec);
                        switch (client_key_exchange_algo) {
                        case rsa_anon:
								destroy_rsa_pubkey(pdu->u.handshake.server_key_exchange->rsa_params);
								break;
                        case dh_anon:
                                destroy_dh_pubkey(pdu->u.handshake.server_key_exchange->dh_params);
								break;
                        case ecdh_anon:
                                destroy_ec_pubkey(pdu->u.handshake.server_key_exchange->ecdh_params);
								break;
						}
						gw_free(pdu->u.handshake.server_key_exchange);
						break;
                case client_key_exchange:
                        switch (client_key_exchange_algo) {
                        case rsa:
                        case rsa_anon:
                                destroy_rsa_encrypted_secret(pdu->u.handshake.client_key_exchange->rsa_params);
                                break;
                        case dh_anon:
                                destroy_dh_pubkey(pdu->u.handshake.client_key_exchange->dh_anon_params);
                                break;
                        case ecdh_anon:
                        case ecdh_ecdsa:
                                destroy_ec_pubkey(pdu->u.handshake.client_key_exchange->ecdh_params);
								break;
						}
						gw_free(pdu->u.handshake.client_key_exchange);
						break;
                case server_hello_done:
						/* nothing to do here */
                    	break;
                }
				break;
        case Application_PDU:
				octstr_destroy(pdu->u.application.data);
				break;
        }

        gw_free(pdu);
}

/* This function will pack a list of WTLS PDUs into a single Octstr, and return
   that Octstr. */
Octstr* wtls_pack_payloadlist (List* payloadlist) {

        Octstr *returnData=0, *tempData1=0, *tempData2 = 0;
        wtls_Payload* retrievedPDU;
        
        /* Assert that our payloadlist is not NULL */
        gw_assert (payloadlist != NULL);

        /* Initialise our return Octstr */
        returnData = octstr_create("");
        
        /* While there are PDUs remaining in our list */
        while (gwlist_len(payloadlist) > 0) {                
                /* Retrieve the next payload from the payloadlist */
                retrievedPDU = (wtls_Payload*) gwlist_extract_first (payloadlist);

                /* Pack the PDU */
                tempData2 = wtls_payload_pack(retrievedPDU);

                /* Shift the current stuff in returnData to a temporary pointer */
                tempData1 = returnData;
                
                /* Tack it onto our Octstr */
                returnData = octstr_cat(tempData1, tempData2);

                /* And now, we can get rid of both tempData1 and tempData2 */
                octstr_destroy (tempData1);
                octstr_destroy (tempData2);                
        }
        
        /* Is the Octstr we finish with of length > 0? */
        if (octstr_len(returnData) > 0) {        
                /* Return the Octstr */
                return returnData;
        }
        
        /* Otherwise, return NULL */
        return NULL;
}

/* This function will unpack an Octstr and return a list of all PDUs contained
   within that Octstr. If the contents of the packet are garbled in some fashion,
   and one packet fails to be decoded correctly, we will continue regardless, and
   a partial list will be returned. NULL is returned if no PDUs can be successfully
   decoded from the supplied data */
List* wtls_unpack_payloadlist (Octstr *data) {

        List* payloadlist = NULL;
        int offset = 0;
        int dataLength = 0;
        wtls_Payload* tempPayload;
        
        /* Has somebody passed in an unpack of a null pointer ? */
        gw_assert(data != NULL);
        
        /* Initialise our list */
        payloadlist = gwlist_create();
        dataLength = octstr_len(data);
        
        /* While offset is less than the size of the data */
        while( offset < dataLength) {

                debug("wtls:wtls_unpack_payloadlist",0,"Offset is now : %d", offset);
                /* Unpack from the supplied offset. This will bump up the value of offset */
                tempPayload = wtls_payload_unpack_from_offset (data, &offset);
                
                /* If the packet returned is not NULL */
                if (tempPayload != NULL) {
                        /* Add the returned packet to the current list of packets */
                        gwlist_append(payloadlist, (void*) tempPayload);
                }
        }

        debug("wtls:wtls_unpack_payloadlist",0,"Finished, found %d PDUs", gwlist_len(payloadlist));
        
        /* If the length of the list is greater than 0 */
        if (gwlist_len(payloadlist) > 0) {
                /* Return the List */
                return payloadlist;
        }
        
        /* Otherwise return NULL */
        return NULL;
}

/* This function tries to determine the length of the PDU at the start of the
   supplied Octstr using (somewhat) intelligent means. If the packet is screwed
   up in some fashion, returns length -1. Returns an int. */

int wtls_payload_guess_length(Octstr* data) {

        int type = 0, lengthFlag = 0, lengthSize = 0, pdu_length = 0;
        long lengthOffset = 1;
        
        /* Is the fragment length indicator on? */
        lengthFlag = octstr_get_bits(data, 0, 1);
        if (lengthFlag) {
                lengthSize = 2;
        }
        
        /* Is the sequence number indicator on? */
        if (octstr_get_bits(data, 1, 1)) {
                /* Yes, so hop over two extra bytes when reading the length */
                lengthOffset += 2;
        }
        /* the message type */
        type = octstr_get_bits(data, 4, 4);
        
        /* If fragment length is turned on, jump to the necessary spot */
        if (lengthFlag == 1) {
                /* After this, lengthOffset + pdu_length == the total length of the PDU */
                pdu_length = unpack_int16(data, &lengthOffset);             
        }

        /* Oh great, so it's not switched on. How considerate. We'll have to make
           a reasonable guess as to what it might be. */
        else {
                switch (type) {
                case ChangeCipher_PDU:
                        /* They're really short */
                        pdu_length = 1;
                        break;
                        
                case Alert_PDU:
                        /* They're a bit longer */
                        pdu_length = 6;
                        break;

                default:
                        /* Otherwise just give up and play dead */
                        pdu_length = -1;
                        break;
                }
        }

        /* And that's the length of the contents, now just add the other doodads on */
        if (pdu_length == -1) {
                return -1;
        }
        else {
                /* The pdu length, plus the sequence number, plus the length of the length value,
                   plus the actual header byte */
                return (pdu_length + lengthOffset);
        }
}

/* This function will unpack an Octstr, starting at the specified offset, and
   return the corresponding wtls_PDU* which was generated from that offset. Offset
   is changed during the running of this function, and ends up as the octet at the start of the
   next pdu */

wtls_Payload* wtls_payload_unpack_from_offset (Octstr *data, int *offset) {
        int guessedPayloadLength = 0;
        int dataLength = 0;
        Octstr* dataFromOffset = 0;
        Octstr* dataFromOffsetToLength = 0;
        wtls_Payload* returnPayload = 0;
        
        /* This would be a sure sign of trouble */
        gw_assert (offset != NULL);
        gw_assert (data != NULL);
        gw_assert (octstr_len(data) >= *offset);

        dataLength = octstr_len(data);
        
        /* First, we need to figure out how long a PDU starting from
           the specified offset is going to be. We need to peek quickly into the
           PDU to check this */
        dataFromOffset = octstr_copy(data, *offset, dataLength);
        guessedPayloadLength = wtls_payload_guess_length(dataFromOffset);

        /* Ooops. Something's wrong. This requested PDU is screwed up. */
        if (guessedPayloadLength == -1) {
                *offset = dataLength;
                return NULL;
        }
        
        /* Quit if we discover that the PDU length plus the requested offset is
           larger than the length of the data supplied - this would mean that we
           would overrun our data, and therefore something is corrupt in this PDU.
           Set the offset as the data length, which will indicate we've gone as far
           as we can */
        if ((*offset + guessedPayloadLength) > dataLength) {
                *offset = dataLength;
                return NULL;
        }
        
        /* If we pass that test, set offset to the correct return value */
        *offset += guessedPayloadLength;
        
        /* Copy the octstr again, so that we end up with an octstr containing
           just the PDU we want */
        dataFromOffsetToLength = octstr_copy(dataFromOffset, 0, guessedPayloadLength);
        
        /* Submit that octstr to the wtls_message_unpack function */
        returnPayload = wtls_payload_unpack(dataFromOffsetToLength);
        
        /* Test to make sure the returned PDU is good */
        if (returnPayload != NULL) {        
                /* And return the PDU to our caller */
                return returnPayload;
        }
        
        /* Otherwise return NULL */
        return NULL;
}        

wtls_Payload *wtls_payload_unpack(Octstr *data) {
        wtls_Payload *payload = NULL;
		Octstr *buffer;
        long bitpos = 0, charpos = 0;
        int msg_length;
        
        gw_assert(data != NULL);

        payload = gw_malloc(sizeof(wtls_Payload));

        /* the record field length flag */
        payload->rlen = octstr_get_bits(data, bitpos, 1);
        bitpos += 1;
        /* the sequence number flag */
        payload->seqnum = octstr_get_bits(data, bitpos, 1);
        bitpos += 1;
        /* the cipher usage flag */
        payload->cipher = octstr_get_bits(data, bitpos, 1);
        bitpos += 1;
        /* the reserved bit */
        payload->reserved = octstr_get_bits(data, bitpos, 1);
        bitpos += 1;
        /* the message type */
        payload->type = octstr_get_bits(data, bitpos, 4);
        bitpos += 4;
        charpos += 1;
        
        /* get the sequence number if present */
        if(payload->seqnum) {
                seqnum = unpack_int16(data, &charpos);
        }
        
        /* get the WTLS plaintext length if present */
        if(payload->rlen) {
                msg_length = unpack_int16(data, &charpos);
        }

		/* the part of data that has just been processed is not
		   needed anymore. We delete it. What is left of data is
		   the payload. */
		octstr_delete(data, 0, charpos);
		payload->data = data;
		
		return payload;
}

void *wtls_payloadlist_destroy(List* payloadList) {
		wtls_Payload* currentPayload;
		int listLen, i;
		
		listLen = gwlist_len(payloadList);
		for( i=0; i<listLen; i++) {
			currentPayload = (wtls_Payload *)gwlist_get(payloadList, i);
			wtls_payload_destroy(currentPayload);
		}
		
		/* delete the list itself */
		gw_free(payloadList);
}

wtls_PDU *wtls_pdu_unpack(wtls_Payload *payload, WTLSMachine* wtls_machine) {
        wtls_PDU *pdu = NULL;
        Octstr *buffer;
        long bitpos = 0, charpos = 0;
        int msg_length;
        
        gw_assert(payload->data != NULL);

        pdu = gw_malloc(sizeof(*pdu));

		pdu->type = payload->type;
		pdu->reserved = payload->reserved;
		pdu->cipher = payload->cipher;
		pdu->seqnum = payload->seqnum;
		pdu->rlen = payload->rlen;
		
		
		/* is the PDU encrypted ? */	
		/*
		if(pdu->cipher) {
				buffer = wtls_decrypt(payload->data, wtls_machine);
		}
		else {
		*/
				buffer = payload->data;
		/*
		}
		*/
		
        switch (pdu->type) {
        case ChangeCipher_PDU:
                pdu->u.cc.change = octstr_get_char(buffer, charpos);
                charpos += 1;
                break;
        case Alert_PDU:
                pdu->u.alert.level = octstr_get_char(buffer, charpos);
                charpos += 1;
                pdu->u.alert.desc = octstr_get_char(buffer, charpos);
                charpos += 1;
                pdu->u.alert.chksum = unpack_octstr_fixed(buffer, &charpos, 4);
                break;  
        case Handshake_PDU:
                pdu->u.handshake.msg_type = octstr_get_char(buffer, charpos);
                charpos += 1;
                pdu->u.handshake.length = unpack_int16(buffer, &charpos);
                switch (pdu->u.handshake.msg_type) {
                case hello_request:
                        break;
                case client_hello:
                        pdu->u.handshake.client_hello = (ClientHello *)gw_malloc(sizeof(ClientHello));
                        pdu->u.handshake.client_hello->clientversion = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        pdu->u.handshake.client_hello->random = unpack_random(buffer, &charpos);
                        pdu->u.handshake.client_hello->session_id = unpack_octstr(buffer, &charpos);

                        /* pack the list of keys */
                        pdu->u.handshake.client_hello->client_key_ids = unpack_key_list(buffer, &charpos);
                        pdu->u.handshake.client_hello->trusted_key_ids = unpack_key_list(buffer, &charpos);

                        /* pack the list of CipherSuites */
                        pdu->u.handshake.client_hello->ciphersuites = unpack_ciphersuite_list(buffer, &charpos);

                        /* CompressionMethods */
                        pdu->u.handshake.client_hello->comp_methods = unpack_compression_method_list(buffer, &charpos);

                        pdu->u.handshake.client_hello->snmode = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        pdu->u.handshake.client_hello->krefresh = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        break;
                case server_hello:
                        pdu->u.handshake.server_hello = (ServerHello *)gw_malloc(sizeof(ServerHello));
                        pdu->u.handshake.server_hello->serverversion = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        pdu->u.handshake.server_hello->random = unpack_random(buffer, &charpos);
                        pdu->u.handshake.server_hello->session_id = unpack_octstr(buffer, &charpos);
                        charpos += 1;
                        pdu->u.handshake.server_hello->client_key_id
                                = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        /* CypherSuite */
                        pdu->u.handshake.server_hello->ciphersuite->bulk_cipher_algo
                                = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        pdu->u.handshake.server_hello->ciphersuite->mac_algo
                                = octstr_get_char(buffer, charpos);
                        charpos += 1;

                        /* CompressionMethod */
                        pdu->u.handshake.server_hello->comp_method = octstr_get_char(buffer, charpos);
                        charpos += 1;

                        pdu->u.handshake.server_hello->snmode = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        pdu->u.handshake.server_hello->krefresh = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        break;
                case certificate:
                        pdu->u.handshake.certificate = (Certificate *)gw_malloc(sizeof(Certificate));
                        pdu->u.handshake.certificate->certificateformat = octstr_get_char(buffer, charpos);
                        charpos += 1;
                        switch (pdu->u.handshake.certificate->certificateformat) {
                        case WTLSCert:
                                pdu->u.handshake.certificate->wtls_certificate = unpack_wtls_certificate(buffer, &charpos);
                                break;
                        case X509Cert:
                                pdu->u.handshake.certificate->x509_certificate = unpack_octstr16(buffer, &charpos);
                                break;
                        case X968Cert:
                                pdu->u.handshake.certificate->x968_certificate = unpack_octstr16(buffer, &charpos);
                                break;
                        }
                        break;
                case server_key_exchange:
                        pdu->u.handshake.server_key_exchange = (ServerKeyExchange *)gw_malloc(sizeof(ServerKeyExchange));
                        /* unpack the ParameterSpecifier  and ParameterSet*/
                        pdu->u.handshake.server_key_exchange->param_spec
                                = unpack_param_spec(buffer, &charpos);
                        switch (client_key_exchange_algo) {
                        case rsa_anon:
                                pdu->u.handshake.server_key_exchange->rsa_params
                                        = unpack_rsa_pubkey(buffer, &charpos);
                                break;
                        case dh_anon:
                                pdu->u.handshake.server_key_exchange->dh_params
                                        = unpack_dh_pubkey(buffer, &charpos);
                                break;
                        case ecdh_anon:
                                pdu->u.handshake.server_key_exchange->ecdh_params
                                        = unpack_ec_pubkey(buffer, &charpos);
                                break;
                        }
                        break;
                case client_key_exchange:
                        pdu->u.handshake.client_key_exchange = (ClientKeyExchange *)gw_malloc(sizeof(ClientKeyExchange));
                        switch (client_key_exchange_algo) {
                        case rsa:
                        case rsa_anon:
                                pdu->u.handshake.client_key_exchange->rsa_params
                                        = unpack_rsa_encrypted_secret(buffer, &charpos);
                                break;
                        case dh_anon:
                                pdu->u.handshake.client_key_exchange->dh_anon_params
                                        = unpack_dh_pubkey(buffer, &charpos);
                                break;
                        case ecdh_anon:
                        case ecdh_ecdsa:
                                pdu->u.handshake.client_key_exchange->ecdh_params
                                        = unpack_ec_pubkey(buffer, &charpos);
                                break;
                        }
                        break;
                case server_hello_done:
                        /* empty */
                        break;
                case finished:
                        pdu->u.handshake.finished = (Finished *)gw_malloc(sizeof(Finished));
                        pdu->u.handshake.finished->verify_data
                                        = unpack_octstr_fixed(buffer, &charpos, 12);
						octstr_dump(pdu->u.handshake.finished->verify_data, 0);
                        break;
                }
                break;
        case Application_PDU:
                /* application message */
                pdu->u.application.data = octstr_duplicate(buffer);
                break;
        default:
                debug("wap.wtls", 0, "%*sPDU: ", 0, "");
                octstr_dump(buffer, 0);
                panic(0, "Unpacking unknown WTLS PDU type %ld", (long) pdu->type);
        }
		
        return pdu;
}


Octstr *wtls_payload_pack(wtls_Payload *payload) {
        Octstr *data;
        long bitpos, charpos;
        long messageSizePos, sizepos;
        /* Used for length calculations */
        int size;
        
        /* We rely on octstr_set_bits to lengthen our octstr as needed. */
        data = octstr_create("");
        bitpos = 0;
        charpos = 0;
        sizepos = 0;
        
        /* the record field length flag - always present*/
        octstr_set_bits(data, bitpos, 1, 1);
        bitpos += 1;
        /* the sequence number flag */
        octstr_set_bits(data, bitpos, 1, payload->seqnum);
        bitpos += 1;
        /* the cipher usage flag */
        octstr_set_bits(data, bitpos, 1, payload->cipher);
        bitpos += 1;
        /* the reserved bit */
        octstr_set_bits(data, bitpos, 1, payload->reserved);
        bitpos += 1;

        /* set the message type */
        octstr_set_bits(data, bitpos, 4, payload->type);
        bitpos += 4;
        charpos += 1;

        /* set the sequence number if present */
        if(payload->seqnum) {
                charpos = pack_int16(data, charpos, payload->seqnum);
        }

        /* set the WTLS length  */
        charpos = pack_int16(data, charpos, payload->rlen);
        
        /* append the data from the wtls_PDU */
        octstr_insert(data, payload->data, octstr_len(data)); 
        
        return data;
}


wtls_Payload *wtls_pdu_pack(wtls_PDU *pdu, WTLSMachine* wtls_machine) {
        Octstr *data, *buffer, *encryptedbuffer;
        wtls_Payload *payload;
        long bitpos, charpos;
        long messageSizePos, sizepos;
        /* Used for length calculations */
        int size, recordType;
        
		/* create the wtls_PDU */
		payload = (wtls_Payload *)gw_malloc(sizeof(wtls_Payload));		
		payload->type = pdu->type;
		payload->reserved = pdu->reserved;
		payload->cipher = pdu->cipher;
		payload->seqnum = pdu->seqnum;
		
        /* We rely on octstr_set_bits to lengthen our octstr as needed. */
        data = octstr_create("");
        buffer = octstr_create("");
        bitpos = 0;
        charpos = 0;
        sizepos = 0;
        
        switch (pdu->type) {
        case ChangeCipher_PDU:
                octstr_append_char(buffer, pdu->u.cc.change);
                charpos += 1;
                break;
        case Alert_PDU:
                octstr_append_char(buffer, pdu->u.alert.level);
                charpos += 1;
                octstr_append_char(buffer, pdu->u.alert.desc);
                charpos += 1;
                charpos = pack_octstr_fixed(buffer, charpos, pdu->u.alert.chksum);
                charpos += 1;
                break;  
        case Handshake_PDU:
                octstr_append_char(buffer, pdu->u.handshake.msg_type);
                charpos += 1;
                /* Save the location of the message size */
                messageSizePos = charpos;
                charpos = pack_int16 (buffer, charpos, pdu->u.handshake.length);
                switch (pdu->u.handshake.msg_type) {
                case hello_request:
                        break;
                case client_hello:
                        octstr_append_char(buffer, pdu->u.handshake.client_hello->clientversion);
                        charpos += 1;
                        charpos = pack_random(buffer, charpos, pdu->u.handshake.client_hello->random);
                        octstr_append_char(buffer, octstr_len(
                                pdu->u.handshake.client_hello->session_id));
                        charpos += 1;
                        charpos = pack_octstr(buffer, charpos, pdu->u.handshake.client_hello->session_id);

                        /* pack the list of keys */
                        charpos = pack_key_list(buffer, charpos,
                                                pdu->u.handshake.client_hello->client_key_ids);
                        charpos = pack_key_list(buffer, charpos,
                                                pdu->u.handshake.client_hello->trusted_key_ids);

                        /* pack the list of CipherSuites */
                        charpos = pack_ciphersuite_list(buffer, charpos,
                                                        pdu->u.handshake.client_hello->ciphersuites);

                        /* CompressionMethods */
                        charpos = pack_compression_method_list(buffer, charpos,
                                                               pdu->u.handshake.client_hello->comp_methods);

                        octstr_append_char(buffer, pdu->u.handshake.client_hello->snmode);
                        charpos += 1;
                        octstr_append_char(buffer, pdu->u.handshake.client_hello->krefresh);
                        charpos += 1;
                        break;
                case server_hello:
                        octstr_append_char(buffer, pdu->u.handshake.server_hello->serverversion);
                        charpos += 1;
                        charpos = pack_random(buffer,  charpos, pdu->u.handshake.server_hello->random);
                        charpos = pack_octstr(buffer, charpos, pdu->u.handshake.server_hello->session_id);
                        charpos += 1;
                        octstr_append_char(buffer, pdu->u.handshake.server_hello->
                                           client_key_id);
                        charpos += 1;
                        /* CypherSuite */
                        octstr_append_char(buffer, pdu->u.handshake.server_hello->
                                           ciphersuite->bulk_cipher_algo);
                        charpos += 1;
                        octstr_append_char(buffer, pdu->u.handshake.server_hello->
                                           ciphersuite->mac_algo);
                        charpos += 1;

                        /* CompressionMethod */
                        octstr_append_char(buffer, pdu->u.handshake.server_hello->comp_method);
                        charpos += 1;

                        octstr_append_char(buffer, pdu->u.handshake.server_hello->snmode);
                        charpos += 1;
                        octstr_append_char(buffer, pdu->u.handshake.server_hello->krefresh);
                        charpos += 1;

                        break;
                case certificate:
                        octstr_append_char(buffer, pdu->u.handshake.certificate->certificateformat);
                        charpos += 1;
                        switch (pdu->u.handshake.certificate->certificateformat) {
                        case WTLSCert:
                                charpos = pack_wtls_certificate(buffer, charpos, pdu->u.handshake.certificate->wtls_certificate);
                                break;
                        case X509Cert:
                                charpos = pack_octstr16(buffer, charpos, pdu->u.handshake.certificate->x509_certificate);
                                break;
                        case X968Cert:
                                charpos = pack_octstr16(buffer, charpos, pdu->u.handshake.certificate->x968_certificate);
                                break;
                        }
                        break;
                case server_key_exchange:
			            debug("wtls: ", 0,"Packing ServerKeyExchange");
                        /* pack the ParameterSpecifier */
                        charpos = pack_param_spec(buffer, charpos, pdu->u.handshake.server_key_exchange->param_spec);

                        switch (client_key_exchange_algo) {
                        case rsa_anon:
                                charpos = pack_rsa_pubkey(buffer, charpos, pdu->u.handshake.server_key_exchange->rsa_params);
                                break;
                        case dh_anon:
                                charpos = pack_dh_pubkey(buffer, charpos, pdu->u.handshake.server_key_exchange->dh_params);
                                break;
                        case ecdh_anon:
                                charpos = pack_ec_pubkey(buffer, charpos, pdu->u.handshake.server_key_exchange->ecdh_params);
                                break;
                        }
                        break;
                case client_key_exchange:
                        switch (client_key_exchange_algo) {
                        case rsa:
                        case rsa_anon:
                                charpos = pack_rsa_encrypted_secret(buffer, charpos, pdu->u.handshake.client_key_exchange->rsa_params);
                                break;
                        case dh_anon:
                                charpos = pack_dh_pubkey(buffer, charpos, pdu->u.handshake.client_key_exchange->dh_anon_params);
                                break;
                        case ecdh_anon:
                        case ecdh_ecdsa:
                                charpos = pack_ec_pubkey(buffer, charpos, pdu->u.handshake.client_key_exchange->ecdh_params);
                                break;
                        }
                        break;
                case server_hello_done:
                        /* empty */
                        break;
				case finished:
						charpos = pack_octstr_fixed(buffer, charpos, pdu->u.handshake.finished->verify_data);
						debug("wtls", 0, "verify_data (in pack)");
						octstr_dump(pdu->u.handshake.finished->verify_data,0 );
						break;
                }
                /* Change the length */
                size = octstr_len(buffer) - messageSizePos - 2;
                debug("wtls_msg.c:length",0,"Setting msg size to : %d",size);
                octstr_set_char(buffer, messageSizePos, (size & 0xFF00) >> 8);
				messageSizePos += 1;
				octstr_set_char(buffer, messageSizePos, (size & 0x00FF));
				
				/* we keep the handshake data to create the Finished PDU */
				octstr_append(wtls_machine->handshake_data, buffer);
                break;                
        case Application_PDU:
                /* application message */
                charpos += pack_octstr(data, charpos, pdu->u.application.data);
                break;
    	default:
                panic(0, "Packing unknown WTLS PDU type %ld", (long) pdu->type);
        }
        
        /* encrypt the buffer if needed */
        if(pdu->cipher) {
				/* the MAC is calculated with the record type so we need it now */
				recordType = 1 << 7; /* length, always present */
				recordType |= pdu->seqnum << 6;
				recordType |= pdu->cipher << 5;
				recordType |= pdu->reserved << 4;
				recordType |= pdu->type;
                encryptedbuffer = wtls_encrypt(buffer, wtls_machine, recordType);

                payload->data = encryptedbuffer;
        }
        else {
                payload->data = buffer;
        }

        payload->rlen = octstr_len(payload->data);
        debug("wtls", 0, "Packed PDU Length: %d", payload->rlen);
        
        return payload;
}

void wtls_pdu_dump(wtls_PDU *pdu, int level) {
	unsigned char *dbg = "wap.wtls";

	/* the message type */
	debug(dbg, 0, "%*sPDU type: %p", level, "", pdu->type);
	/* the reserved bit */
	debug(dbg, 0, "%*sReserved bit: %p", level, "", pdu->reserved);
	/* cipher usage flag */
	debug(dbg, 0, "%*sCipher in use: %p", level, "", pdu->cipher);
	/* the sequence number flag */
	debug(dbg, 0, "%*sSequence number in use: %p", level, "", pdu->seqnum);
	/* the record field length flag */
	debug(dbg, 0, "%*sRecord field length present: %p", level, "", pdu->rlen);

	switch (pdu->type) {
	case ChangeCipher_PDU:
		debug(dbg, 0, "%*sChangeCipher:", level, "");
		debug(dbg, 0, "%*sChange: %d", level+1, "", pdu->u.cc.change);
		break;
	case Alert_PDU:
		debug(dbg, 0, "%*sAlert:", level, "");
		debug(dbg, 0, "%*sLevel: %p", level+1, "", pdu->u.alert.level);
		debug(dbg, 0, "%*sDescription: %d", level+1, "", pdu->u.alert.desc);
		debug(dbg, 0, "%*sChecksum: %p", level+1, "", pdu->u.alert.chksum);
		break;	
	case Handshake_PDU:
		debug(dbg, 0, "%*sHandshake:", level, "");
		debug(dbg, 0, "%*sMessage Type: %d", level+1, "", pdu->u.handshake.msg_type);
		debug(dbg, 0, "%*sLength: %d", level+1, "", pdu->u.handshake.length);
		switch (pdu->u.handshake.msg_type) {
		case hello_request:
			debug(dbg, 0, "%*sHelloRequest.", level, "");
			break;
		case client_hello:
			debug(dbg, 0, "%*sClientHello :", level, "");
			debug(dbg, 0, "%*sClient version: %d", level+1, "", pdu->u.handshake.client_hello->clientversion);
			debug(dbg, 0, "%*sRandom:", level+1, "");
			dump_random(dbg, level+2,
					pdu->u.handshake.client_hello->random);
			debug(dbg, 0, "%*sSessionId: ", level, "");
			octstr_dump(pdu->u.handshake.client_hello->session_id, level + 2);

			/* pack the list of keys */
			debug(dbg, 0, "%*sClient Key IDs: ", level+1, "");
			dump_key_list(dbg, level+2,
					pdu->u.handshake.client_hello->client_key_ids);
			debug(dbg, 0, "%*sTrusted Key IDs: ", level+1, "");
			dump_key_list(dbg, level+2,
					pdu->u.handshake.client_hello->trusted_key_ids);
			
			/* pack the list of CipherSuites */
			debug(dbg, 0, "%*sCipherSuite List: ", level+1, "");
			dump_ciphersuite_list(dbg, level+2,
					pdu->u.handshake.client_hello->ciphersuites);
			
			/* CompressionMethods */
			debug(dbg, 0, "%*sCompression Method List: ", level+1, "");
			dump_compression_method_list(dbg, level+2,
					pdu->u.handshake.client_hello->comp_methods);
			
			debug(dbg, 0, "%*sSeq Number Mode: %d", level+1, "", pdu->u.handshake.client_hello->snmode);
			debug(dbg, 0, "%*sKey Refresh: %p", level+1, "", pdu->u.handshake.client_hello->krefresh);
			break;
		case server_hello:
			debug(dbg, 0, "%*sServerHello :", level, "");
			debug(dbg, 0, "%*sServer version: %d", level+1, "", pdu->u.handshake.server_hello->serverversion);
			debug(dbg, 0, "%*sRandom:", level+1, "");
			dump_random(dbg, level+2,
					pdu->u.handshake.server_hello->random);
			debug(dbg, 0, "%*sSession ID: %d", level+1, "", pdu->u.handshake.server_hello->session_id);
			debug(dbg, 0, "%*sClient Key ID: %p", level+1, "", pdu->u.handshake.server_hello->client_key_id);
			/* CypherSuite */
			debug(dbg, 0, "%*sBulk Cipher Algo: %p", level+1, "", pdu->u.handshake.server_hello->ciphersuite->bulk_cipher_algo);
			debug(dbg, 0, "%*sMAC Algo: %p", level+1, "", pdu->u.handshake.server_hello->ciphersuite->mac_algo);
			
			/* CompressionMethod */
			debug(dbg, 0, "%*sCompression Method: %p", level+1, "", pdu->u.handshake.server_hello->comp_method);
			
			debug(dbg, 0, "%*sSeq Number Mode: %p", level+1, "", pdu->u.handshake.server_hello->snmode);
			debug(dbg, 0, "%*sKey Refresh: %p", level+1, "", pdu->u.handshake.server_hello->krefresh);
			break;
		case certificate:
			debug(dbg, 0, "%*sCertificate :", level, "");
			debug(dbg, 0, "%*sCertificate Format: %p", level+1, "", pdu->u.handshake.certificate->certificateformat);
			switch (pdu->u.handshake.certificate->certificateformat) {
			case WTLSCert:
				debug(dbg, 0, "%*sWTLS Certificate: %p", level+1, "");
				dump_wtls_certificate(dbg, level+2, pdu->u.handshake.certificate->wtls_certificate);
				break;
			case X509Cert:
				debug(dbg, 0, "%*sX509 Certificate: %p", level+1, "");
				octstr_dump(pdu->u.handshake.certificate->x509_certificate, level+2);
				break;
			case X968Cert:
				debug(dbg, 0, "%*sX968 Certificate: %p", level+1, "");
				octstr_dump(pdu->u.handshake.certificate->x968_certificate, level+2);
				break;
			}
			break;
		case server_key_exchange:
			debug(dbg, 0, "%*sServerKeyExchange :", level, "");
			/* ParameterSpecifier */
			debug(dbg, 0, "%*sParameter Index: %p", level+1, "", pdu->u.handshake.server_key_exchange->param_spec->param_index);
			if(pdu->u.handshake.server_key_exchange->param_spec->param_index == 255) {
				/* ParameterSet */
				debug(dbg, 0, "%*sParameter Set: %p", level+1, "", pdu->u.handshake.server_key_exchange->param_spec->param_set);
			}
			switch (client_key_exchange_algo) {
			case rsa_anon:
				dump_rsa_pubkey(dbg, level+1, pdu->u.handshake.server_key_exchange->rsa_params);
				break;
			case dh_anon:
				dump_dh_pubkey(dbg, level+1, pdu->u.handshake.server_key_exchange->dh_params);
				break;
			case ecdh_anon:
				dump_ec_pubkey(dbg, level+1, pdu->u.handshake.server_key_exchange->ecdh_params);
				break;
			}
			break;
		case client_key_exchange:
			debug(dbg, 0, "%*sClientKeyExchange :", level, "");
			switch (client_key_exchange_algo) {
			case rsa:
			case rsa_anon:
				dump_rsa_encrypted_secret(dbg, level+1, pdu->u.handshake.client_key_exchange->rsa_params);
				break;
			case dh_anon:
				dump_dh_pubkey(dbg, level+1, pdu->u.handshake.client_key_exchange->dh_anon_params);
				break;
			case ecdh_anon:
			case ecdh_ecdsa:
				dump_ec_pubkey(dbg, level+1, pdu->u.handshake.client_key_exchange->ecdh_params);
				break;
			}
			break;
		case server_hello_done:
			debug(dbg, 0, "%*sClientHelloDone.", level, "");
			/* empty */
			break;
		case finished:
			debug(dbg, 0, "%*sFinished :", level, "");
			debug(dbg, 0, "%*sverify_data :", level+1, "");
			octstr_dump(pdu->u.handshake.finished->verify_data, level+2);
			break;
		}
		break;
	case Application_PDU:
		debug(dbg, 0, "%*sApplication :", level, "");
		/* application message */
		octstr_dump(pdu->u.application.data, level+1);
		break;
	default:
		debug(dbg, 0, "%*sWTLS PDU at %p:", level, "", (void *)pdu);
		debug(dbg, 0, "%*s unknown type %u", level, "", pdu->type);
	}
	
}

#endif
