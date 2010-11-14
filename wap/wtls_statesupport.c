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
 * wtls_statesupport.c
 * 
 * 2001  Nick Clarey, Yann Muller for 3G LAB
 * Nikos Balkanas, InAccess Networks (2009)
 */

#include "gwlib/gwlib.h"

#ifdef HAVE_WTLS_OPENSSL
#include <openssl/x509.h>
#include <openssl/des.h>
#ifndef NO_RC5
#include <openssl/rc5.h>
#else
#error "your OpenSSL installation lacks RC5 algorithm support"
#endif /* NO_RC5 */

#include "wtls_statesupport.h"
#include "wtls_pdusupport.h"

#define BLOCKLENGTH 64
#define INNERPAD 0x36
#define OUTERPAD 0x5C

/*static keyxchg_table_t keyxchg_table[] = {
        {"NULL",0},
        {"Shared Secret", KEYSIZE_MAX},
        {"DH-anon",KEYSIZE_MAX},
        {"DH-anon-512",512},
        {"DH-anon-768",768},
        {"RSA-anon", KEYSIZE_MAX},
        {"RSA-anon-512",512},
        {"RSA-anon-768",768},
        {"RSA",KEYSIZE_MAX},
        {"RSA-512", 512},
        {"RSA-768",768},
        {"ECDH-anon",KEYSIZE_MAX},
        {"ECDH-anon-113",113},
        {"ECDH-anon-131",131},
        {"ECDH-ECDSA",KEYSIZE_MAX}
}; */

static bulk_table_t bulk_table[] = {
   {"NULL Encryption", EXPORTABLE, STREAM, 0, 0, 0, 0, 0},
   {"RC5-CBC-40", EXPORTABLE, BLOCK, 5, 16, 40, 8, 8},
   {"RC5-CBC-56", EXPORTABLE, BLOCK, 7, 16, 56, 8, 8},
   {"RC5-CBC", NOT_EXPORTABLE, BLOCK, 16, 16, 128, 8, 8},
   {"DES-CBC-40", EXPORTABLE, BLOCK, 5, 8, 40, 8, 8},
   {"DES-CBC", NOT_EXPORTABLE, BLOCK, 8, 8, 56, 8, 8},
   {"3DES-CBC-EDE", NOT_EXPORTABLE, BLOCK, 24, 24, 168, 8, 8},
   {"IDEA-CBC-40", EXPORTABLE, BLOCK, 5, 16, 40, 8, 8},
   {"IDEA-CBC-56", EXPORTABLE, BLOCK, 7, 16, 56, 8, 8},
   {"IDEA-CBC", NOT_EXPORTABLE, BLOCK, 16, 16, 128, 8, 8}
};

static hash_table_t hash_table[] = {
   {"SHA-0", 0, 0},
   {"SHA1-40", 20, 5},
   {"SHA1-80", 20, 10},
   {"SHA1", 20, 20},
   {"SHA-XOR-40", 0, 5},
   {"MD5-40", 16, 5},
   {"MD5-80", 16, 10},
   {"MD5", 16, 16}
};

X509 *x509_cert = NULL;
RSA *private_key = NULL;
int refresh = 2;
extern KeyExchangeSuite client_key_exchange_algo;
extern PublicKeyAlgorithm public_key_algo;
extern SignatureAlgorithm signature_algo;
extern unsigned char *MD5(const unsigned char *d, size_t n, unsigned char
           *md);
extern unsigned char *stateName(int state);

/*
 * Function Prototypes.
 */

Octstr *wtls_hmac_hash(Octstr * key, Octstr * data, int algo);
Octstr *wtls_hash(Octstr * inputData, WTLSMachine * wtls_machine);
Octstr *wtls_rc5(Octstr * data, WTLSMachine * wtls_machine, int crypt);
Octstr *wtls_des(Octstr * data, WTLSMachine * wtls_machine, int crypt);
Octstr *wtls_P_hash(Octstr * secret, Octstr * seed, int byteLength,
          WTLSMachine * wtls_machine);
Octstr *wtls_get_certificate(void);
int isSupportedKeyEx(int keyExId);
void add_all_handshake_data(WTLSMachine * wtls_machine, List * pdu_list);

/* Add here the supported KeyExchangeSuites 
   used by wtls_choose_clientkeyid */
KeyExchangeSuite supportedKeyExSuite[] = { rsa_anon };

Octstr *wtls_decrypt(wtls_Payload * payload, WTLSMachine * wtls_machine)
{
   int len, padLen = 0, macSize, recordType, block, refresh;
   Octstr *openText, *MAContent, *tempData, *result;
   char cipher[20], *p;

   if (payload->seqNum && wtls_machine->client_seq_num > payload->seqNum) {
      error(0,
            "Out of sequence packet received (p: %d < %d :w). Dropping datagram.",
            payload->seqNum, wtls_machine->client_seq_num);
      return (NULL);
   } else
      wtls_machine->client_seq_num = payload->seqNum;
   refresh = 1 << wtls_machine->key_refresh;
   if (wtls_machine->last_refresh < 0 || (wtls_machine->last_refresh +
                      refresh <=
                      wtls_machine->client_seq_num))
      calculate_client_key_block(wtls_machine);
   switch (wtls_machine->bulk_cipher_algorithm) {
   case NULL_bulk:
      openText = octstr_duplicate(payload->data);
      break;

   case RC5_CBC:
   case RC5_CBC_40:
   case RC5_CBC_56:
      openText = wtls_rc5(payload->data, wtls_machine, RC5_DECRYPT);
      break;

   case DES_CBC:
   case DES_CBC_40:
      openText = wtls_des(payload->data, wtls_machine, DES_DECRYPT);
      break;

   default:
      cipherName(cipher, wtls_machine->bulk_cipher_algorithm);
      error(0,
            "wtls_decrypt: Unsupported bulk cipher algorithm (%s).",
            cipher);
      return (NULL);
      break;
   }
   /* Verify MAC */
   recordType = 1 << 7;
   recordType |= payload->snMode << 6;
   recordType |= payload->cipher << 5;
   recordType |= payload->reserved << 4;
   recordType |= payload->type;
   len = octstr_len(openText);
   p = octstr_get_cstr(openText);
   block = bulk_table[wtls_machine->bulk_cipher_algorithm].block_size;

   padLen = *(p + len - 1);
   if (padLen >= block || padLen != *(p + len - 2))
      padLen = 0;
   padLen++;
   macSize = hash_table[wtls_machine->mac_algorithm].mac_size;

   tempData = octstr_create("");
   pack_int16(tempData, 0, wtls_machine->client_seq_num);
   octstr_append_char(tempData, recordType);
   pack_int16(tempData, 3, len - macSize - padLen);
   octstr_append_data(tempData, p, len - macSize - padLen);
   MAContent = wtls_hmac_hash(wtls_machine->client_write_MAC_secret,
               tempData, wtls_machine->mac_algorithm);
   if (memcmp(octstr_get_cstr(MAContent), p + len - padLen - macSize,
         macSize)) {
      octstr_destroy(MAContent);
      octstr_destroy(tempData);
      octstr_destroy(openText);
      error(0, "wtls_decrypt: Rejected packet due to bad MAC");
      return (NULL);
   }
   octstr_destroy(MAContent);
   octstr_destroy(tempData);
   result = octstr_create_from_data((char *)p, len - padLen - macSize);
   octstr_destroy(openText);
   return (result);
}

/* This function will convert our buffer into a completed GenericBlockCipher */
Octstr *wtls_encrypt(Octstr * buffer, WTLSMachine * wtls_machine,
           int recordType)
{
   Octstr *bufferCopy;
   Octstr *encryptedContent;
   Octstr *contentMac;
   Octstr *tempData;
   char *tempPadding = NULL;
   int paddingLength, macSize, blockLength, bufferLength, refresh;
        int i;

   refresh = 1 << wtls_machine->key_refresh;
   if (!(wtls_machine->server_seq_num % refresh))
      calculate_server_key_block(wtls_machine);
        /* Copy our buffer */
        bufferCopy = octstr_duplicate(buffer);
        
        /* Get the MAC of the content */
        bufferLength  = octstr_len(buffer);

        /* Copy the buffer in preparation for MAC calculation */
        tempData = octstr_create("");
   pack_int16(tempData, 0, wtls_machine->server_seq_num);
        octstr_append_char(tempData, recordType);
        pack_int16(tempData, octstr_len(tempData), bufferLength);        
        octstr_append(tempData, buffer);

        /* Calculate the MAC */
   contentMac =
       wtls_hmac_hash(wtls_machine->server_write_MAC_secret, tempData,
            wtls_machine->mac_algorithm);

        /* Calculate the padding length */
        macSize = hash_table[wtls_machine->mac_algorithm].mac_size;
   blockLength =
       bulk_table[wtls_machine->bulk_cipher_algorithm].block_size;

   paddingLength =
       blockLength - ((bufferLength + macSize + 1) % blockLength);

        /* Append the MAC to the bufferCopy */
   octstr_append(bufferCopy, contentMac);
        
        if (paddingLength > 0) {
                /* Pad with the paddingLength itself paddingLength times. Confused yet? */
                tempPadding = gw_malloc(paddingLength);
      for (i = 0; i < paddingLength; i++) {
                        /* You're probably really spaced out around now...
                           see section 9.2.3.3 for more details... */
                        tempPadding[i] = paddingLength;
                }
                octstr_append_data(bufferCopy, tempPadding, paddingLength);
      gw_free(tempPadding);
        }
        /* Add the length byte */
        octstr_append_char(bufferCopy, paddingLength);                

        /* Encrypt the content */
   switch (wtls_machine->bulk_cipher_algorithm) {
   case NULL_bulk:
      encryptedContent = octstr_duplicate(bufferCopy);
      break;

   case RC5_CBC:
   case RC5_CBC_40:
   case RC5_CBC_56:
      encryptedContent =
          wtls_rc5(bufferCopy, wtls_machine, RC5_ENCRYPT);
      break;

   case DES_CBC:
   case DES_CBC_40:
      encryptedContent =
          wtls_des(bufferCopy, wtls_machine, DES_ENCRYPT);
      break;

   default:
      error(0,
            "wtls_encrypt: Unsupported bulk cipher algorithm (%d).",
            wtls_machine->bulk_cipher_algorithm);
      encryptedContent = NULL;
      break;
   }
   octstr_destroy(bufferCopy);
   octstr_destroy(contentMac);
   octstr_destroy(tempData);
   octstr_destroy(buffer);
   return (encryptedContent);
}

/*
 * Naming utilities used in printing
 */

void keyName(char *name, int key)
{
   switch (key) {
   case null_k:
      strcpy(name, "null_k");
      break;

   case shared_secret:
      strcpy(name, "shared_secret");
      break;

   case dh_anon:
      strcpy(name, "dh_anon");
      break;

   case dh_anon_512:
      strcpy(name, "dh_anon_512");
      break;

   case dh_anon_768:
      strcpy(name, "dh_anon_768");
      break;

   case rsa_anon:
      strcpy(name, "rsa_anon");
      break;

   case rsa_anon_512:
      strcpy(name, "rsa_anon_512");
      break;

   case rsa_anon_768:
      strcpy(name, "rsa_anon_768");
      break;

   case rsa:
      strcpy(name, "rsa");
      break;

   case rsa_512:
      strcpy(name, "rsa_512");
      break;

   case rsa_768:
      strcpy(name, "rsa_768");
      break;

   case ecdh_anon:
      strcpy(name, "ecdh_anon");
      break;

   case ecdh_anon_113:
      strcpy(name, "ecdh_anon_113");
      break;

   case ecdh_anon_131:
      strcpy(name, "ecdh_anon_131");
      break;

   case ecdh_ecdsa:
      strcpy(name, "ecdh_ecdsa");
      break;
   }
}

void cipherName(char *name, int cipher)
{
   switch (cipher) {
   case NULL_bulk:
      strcpy(name, "NULL_bulk");
      break;

   case RC5_CBC_40:
      strcpy(name, "RC5_CBC_40");
      break;

   case RC5_CBC_56:
      strcpy(name, "RC5_CBC_56");
      break;

   case RC5_CBC:
      strcpy(name, "RC5_CBC");
      break;

   case DES_CBC_40:
      strcpy(name, "DES_CBC_40");
      break;

   case DES_CBC:
      strcpy(name, "DES_CBC");
      break;

   case TRIPLE_DES_CBC_EDE:
      strcpy(name, "TRIPLE_DES_CBC_EDE");
      break;

   case IDEA_CBC_40:
      strcpy(name, "IDEA_CBC_40");
      break;

   case IDEA_CBC_56:
      strcpy(name, "IDEA_CBC_56");
      break;

   case IDEA_CBC:
      strcpy(name, "IDEA_CBC");
      break;
   }
}

void macName(char *name, int mac)
{
   switch (mac) {
   case SHA_0:
      strcpy(name, "SHA_0");
      break;

   case SHA_40:
      strcpy(name, "SHA_40");
      break;

   case SHA_80:
      strcpy(name, "SHA_80");
      break;

   case SHA_NOLIMIT:
      strcpy(name, "SHA_NOLIMIT");
      break;

   case SHA_XOR_40:
      strcpy(name, "SHA_XOR_40");
      break;

   case MD5_40:
      strcpy(name, "MD5_80");
      break;

   case MD5_80:
      strcpy(name, "MD5_80");
      break;

   case MD5_NOLIMIT:
      strcpy(name, "MD5_NOLIMIT");
      break;
   }
}

void alertName(char *name, int alert)
{
   switch (alert) {
   case connection_close_notify:
      strcpy(name, "connection_close_notify");
      break;

   case session_close_notify:
      strcpy(name, "session_close_notify");
      break;

   case no_connection:
      strcpy(name, "no_connection");
      break;

   case unexpected_message:
      strcpy(name, "unexpected_message");
      break;

   case time_required:
      strcpy(name, "time_required");
      break;

   case bad_record_mac:
      strcpy(name, "bad_record_mac");
      break;

   case decryption_failed:
      strcpy(name, "decryption_failed");
      break;

   case record_overflow:
      strcpy(name, "record_overflow");
      break;

   case decompression_failure:
      strcpy(name, "decompression_failure");
      break;

   case handshake_failure:
      strcpy(name, "handshake_failure");
      break;

   case bad_certificate:
      strcpy(name, "unsupported_certificate");
      break;

   case certificate_revoked:
      strcpy(name, "certificate_revoked");
      break;

   case certificate_expired:
      strcpy(name, "certificate_expired");
      break;

   case certificate_unknown:
      strcpy(name, "certificate_unknown");
      break;

   case illegal_parameter:
      strcpy(name, "illegal_parameter");
      break;

   case unknown_ca:
      strcpy(name, "unknown_ca");
      break;

   case access_denied:
      strcpy(name, "access_denied");
      break;

   case decode_error:
      strcpy(name, "decode_error");
      break;

   case decrypt_error:
      strcpy(name, "decrypt_error");
      break;

   case unknown_key_id:
      strcpy(name, "unknown_key_id");
      break;

   case disabled_key_id:
      strcpy(name, "disabled_key_id");
      break;

   case key_exchange_disabled:
      strcpy(name, "key_exchange_disabled");
      break;

   case session_not_ready:
      strcpy(name, "session_not_ready");
      break;

   case unknown_parameter_index:
      strcpy(name, "unknown_parameter_index");
      break;

   case duplicate_finished_received:
      strcpy(name, "duplicate_finished_received");
      break;

   case export_restriction:
      strcpy(name, "export_restriction");
      break;

   case protocol_version:
      strcpy(name, "protocol_version");
      break;

   case insufficient_security:
      strcpy(name, "insufficient_security");
      break;

   case internal_error:
      strcpy(name, "internal_error");
      break;

   case user_canceled:
      strcpy(name, "user_canceled");
      break;

   case no_renegotiation:
      strcpy(name, "no_renegotiation");
      break;
   }
}

void pduName(char *name, int pdu)
{
   switch (pdu) {
   case ChangeCipher_PDU:
      strcpy(name, "Change Cipher");
      break;

   case Alert_PDU:
      strcpy(name, "Alert");
      break;

   case Handshake_PDU:
      strcpy(name, "Handshake");
      break;

   case Application_PDU:
      strcpy(name, "Application");
      break;
   }
}

void hsName(char *name, int handshake)
{
   switch (handshake) {
   case hello_request:
      strcpy(name, "Hello Request");
      break;

   case client_hello:
      strcpy(name, "Client Hello");
      break;

   case server_hello:
      strcpy(name, "Server Hello");
      break;

   case certificate:
      strcpy(name, "Certificate");
      break;

   case server_key_exchange:
      strcpy(name, "Server Key Exchange");
      break;

   case certificate_request:
      strcpy(name, "Certificate Request");
      break;

   case server_hello_done:
      strcpy(name, "Server Hello Done");
      break;

   case certificate_verify:
      strcpy(name, "Certificate Vaerify");
      break;

   case client_key_exchange:
      strcpy(name, "Client Key Exchange");
      break;

   case finished:
      strcpy(name, "Finished");
      break;
   }
}

/* P_hash as described in WAP WTLS section 11.3.2 */
Octstr *wtls_P_hash(Octstr * secret, Octstr * seed, int byteLength,
          WTLSMachine * wtls_machine)
{
	Octstr *a;
	Octstr *aPrev;
	Octstr *aPlusSeed;
	Octstr *hashTemp;
	Octstr *hashedData;

	hashedData = octstr_create("");
				
	/* start with A(1) = HMAC_hash(secret, seed) */
	aPrev = octstr_duplicate(seed);
	do {
		/* A(i) */
      a = wtls_hmac_hash(secret, aPrev, SHA_80);
		aPlusSeed = octstr_cat(a, seed);
		/* HMAC */
      hashTemp = wtls_hmac_hash(secret, aPlusSeed, SHA_80);
      octstr_destroy(aPlusSeed);
		octstr_append(hashedData, hashTemp);
		octstr_destroy(hashTemp);
		/* Update a(i-1) */
		octstr_destroy(aPrev);
		aPrev = a;
   } while (octstr_len(hashedData) < byteLength);
	
   octstr_destroy(aPrev);
   return (hashedData);
}

/* Pseudo Random Function (PRF) as described in WAP WTLS section 11.3.2 */
Octstr *wtls_calculate_prf(Octstr * secret, Octstr * label, Octstr * seed,
            int byteLength, WTLSMachine * wtls_machine)
{
   Octstr *returnOctstr;
    	Octstr *labelPlusSeed;

    	/* Create label + seed */
		labelPlusSeed = octstr_cat(label, seed);
		
		/* PRF(secret, label, seed) = P_hash(secret, label + seed) */
   returnOctstr = wtls_P_hash(secret, labelPlusSeed, byteLength,
               wtls_machine);
		
    	/* Return the first nbytes of the hashed data */
		octstr_truncate(returnOctstr, byteLength);
		 
   octstr_destroy(labelPlusSeed);
   return (returnOctstr);
}

/* MAC calculation */
Octstr *wtls_hmac_hash(Octstr * key, Octstr * data, int algo)
{
    static unsigned char final_mac[1024];
    unsigned char *mac, *buffer, *keyString;
   int bufferlen, keylen;
   uint mac_len = 0;
   Octstr *returnOctstr = NULL;
	
   buffer = (unsigned char *)octstr_get_cstr(data);
	bufferlen = octstr_len(data);
   keyString = (unsigned char *)octstr_get_cstr(key);
	keylen = octstr_len(key);
	
	mac = final_mac;
	
   switch (algo) {
		case SHA_0:
      /* Do nothing */
			break;

		case SHA_40:
		case SHA_80:
		case SHA_NOLIMIT:
      HMAC(EVP_sha1(), keyString, keylen, buffer, bufferlen, mac,
           &mac_len);
			break;

		case SHA_XOR_40:
      error(0, "wtls_hmac_hash: SHA_XOR_40 Mac not supported");
			// dunno yet
      *mac = '\0';
			break;

		case MD5_40:
		case MD5_80:
		case MD5_NOLIMIT:
      HMAC(EVP_md5(), keyString, keylen, buffer, bufferlen, mac,
           &mac_len);
			break;
	}
   returnOctstr = octstr_create_from_data((char *)mac, mac_len);
   return (returnOctstr);
}

/* Not to be confused with octstr_hash, this applies the currently set hashing
   algorithm from wtls_machine to the supplied input data, returning a hashed
   Octstr. If it fails, it will return a NULL pointer */
Octstr *wtls_hash(Octstr * inputData, WTLSMachine * wtls_machine)
{
        int inputDataLength;
        int outputDataLength;
   unsigned char *outputDataTemp;
   unsigned char *inputDataTemp;
   unsigned char *tempPointer = NULL;
   Octstr *outputData;

        inputDataLength = octstr_len(inputData);
        outputDataLength = hash_table[wtls_machine->mac_algorithm].key_size;
        inputDataTemp = gw_malloc(inputDataLength);
        outputDataTemp = gw_malloc(outputDataLength);

        /* Copy the contents of inputData into inputDataTemp, ready for hashing */
   tempPointer = (unsigned char *)octstr_get_cstr(inputData);
   memcpy((void *)inputDataTemp, (void *)tempPointer, inputDataLength);
        
        /* Hash away! */
        // Here's where we need to hash on the selected algorithm, not just the SHA-1 algorithm
		//debug("wtls", 0, "mac algo %d", wtls_machine->mac_algorithm);
        switch (wtls_machine->mac_algorithm) {
			case SHA_0:
      /* Do nothing */
				break;

			case SHA_40:
			case SHA_80:
			case SHA_NOLIMIT:
      tempPointer =
          SHA1(inputDataTemp, inputDataLength, outputDataTemp);
				break;

			case SHA_XOR_40:
				// dunno yet
				break;

			case MD5_40:
			case MD5_80:
			case MD5_NOLIMIT:
      tempPointer = MD5(inputDataTemp, inputDataLength,
              outputDataTemp);
				break;
		}
   if (!tempPointer) {
      if (wtls_machine->mac_algorithm != SHA_0)
         error(0, "wtls_hash: Failed to hash input");
      gw_free(outputDataTemp);
      gw_free(inputDataTemp);
      return (NULL);
        }

        /* Get our output data setup */
   outputData = octstr_create_from_data((char *)outputDataTemp,
                    outputDataLength);
        
		/* some algorithms don't use the full length of H */
   octstr_truncate(outputData,
         hash_table[wtls_machine->mac_algorithm].mac_size);
		
        /* Delete our allocated memory */
        gw_free(outputDataTemp);
        gw_free(inputDataTemp);
        
        /* Return the outputData */
   return (outputData);
}

Octstr *wtls_des(Octstr * data, WTLSMachine * wtls_machine, int crypt)
{
   Octstr *result;
   unsigned char *output, iv[20], c[2];
   des_key_schedule des_ks;
   des_cblock des_key, des_iv;
   int i, len = octstr_len(data);

   if (!data)
      return (NULL);
   if (crypt == DES_ENCRYPT) {
      memcpy(iv, octstr_get_cstr(wtls_machine->server_write_IV),
             octstr_len(wtls_machine->server_write_IV));
      c[0] = (wtls_machine->server_seq_num & 0xFF00) >> 8;
      c[1] = wtls_machine->server_seq_num & 0xFF;
      for (i = 0;
           i <
           bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size;
           i++)
         iv[i] = iv[i] ^ c[i % 2];
      memcpy(des_iv, iv, sizeof(des_iv));
      memcpy(des_key,
             octstr_get_cstr(wtls_machine->server_write_enc_key),
             sizeof(des_key));
   } else {
      memcpy(iv, octstr_get_cstr(wtls_machine->client_write_IV),
             octstr_len(wtls_machine->client_write_IV));
      c[0] = (wtls_machine->client_seq_num & 0xFF00) >> 8;
      c[1] = wtls_machine->client_seq_num & 0xFF;
      for (i = 0;
           i <
           bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size;
           i++)
         iv[i] = iv[i] ^ c[i % 2];
      memcpy(des_iv, iv, sizeof(des_iv));
      memcpy(des_key,
             octstr_get_cstr(wtls_machine->client_write_enc_key),
             sizeof(des_key));
   }
   des_set_odd_parity(&des_key);
   if (des_set_key_checked(&des_key, des_ks)) {
      error(0, "wtls_des ~> Unable to set key schedule");
      return (NULL);
   }
   output = (unsigned char *)gw_malloc((len + 1) * sizeof(unsigned char));
   des_ncbc_encrypt((unsigned char *)octstr_get_cstr(data), output, len,
          des_ks, &des_iv, crypt);
   result = octstr_create_from_data((char *)output, len);
         gw_free(output);

   return (result);
}

Octstr *wtls_rc5(Octstr * data, WTLSMachine * wtls_machine, int crypt)
{
   Octstr *result;
   EVP_CIPHER_CTX ectx;
   unsigned char ebuf[20], *output, *input, iv[20], c[2];
   int i = 0, len = octstr_len(data);

   if (!data)
      return (NULL);
   EVP_CipherInit(&ectx, ALG, NULL, NULL, crypt);
   switch (wtls_machine->bulk_cipher_algorithm) {
   case RC5_CBC_40:
   case RC5_CBC_56:
      i = 12;
      break;
         
   default:
      i = 16;
      break;
   }
   EVP_CIPHER_CTX_ctrl(&ectx, EVP_CTRL_SET_RC5_ROUNDS, i, NULL);
   if (crypt == RC5_ENCRYPT) {
      memcpy(iv, octstr_get_cstr(wtls_machine->server_write_IV),
        octstr_len(wtls_machine->server_write_IV));
      c[0] = (wtls_machine->server_seq_num & 0xFF00) >> 8;
      c[1] = wtls_machine->server_seq_num & 0xFF;
      for (i = 0; i < bulk_table[wtls_machine->bulk_cipher_algorithm].
            iv_size; i++)
         iv[i] = iv[i] ^ c[i % 2];
      EVP_CipherInit(&ectx, NULL, (unsigned char *)octstr_get_cstr(
         wtls_machine->server_write_enc_key), iv, RC5_ENCRYPT);
   } else {
      memcpy(iv, octstr_get_cstr(wtls_machine->client_write_IV),
        octstr_len(wtls_machine->client_write_IV));
      c[0] = (wtls_machine->client_seq_num & 0xFF00) >> 8;
      c[1] = wtls_machine->client_seq_num & 0xFF;
      for (i = 0; i < bulk_table[wtls_machine->bulk_cipher_algorithm].
            iv_size; i++)
         iv[i] = iv[i] ^ c[i % 2];
      EVP_CipherInit(&ectx, NULL, (unsigned char *)octstr_get_cstr(
         wtls_machine->client_write_enc_key), iv, RC5_DECRYPT);
   }

   output = gw_malloc(len + 1);
   input = (unsigned char *)octstr_get_cstr(data);
   i = 0;

   for (i = 0; i <= len - 8; i += 8) {
      EVP_Cipher(&ectx, ebuf, input + i, 8);
      memmove(output + i, ebuf, 8);
   }
        
   // Leftovers...
   if (i < len) {
      EVP_Cipher(&ectx, ebuf, input + i, len - i);
      memmove(output + i, ebuf, len - i);
   }

   result = octstr_create_from_data((char *)output, len);
         gw_free(output);
   return (result);
}

static Octstr *wtls_decrypt_rsa(Octstr * encryptedData)
{
   int numBytesWritten = 0, numBytesToRead = 0;
   Octstr *decryptedData = NULL;
   unsigned char *tempDecryptionBuffer = NULL, *tempEncryptionPointer =
       NULL;
        
        /* Allocate some memory for our decryption buffer */
        tempDecryptionBuffer = gw_malloc(RSA_size(private_key));

   /* Calculate the number of bytes to read from encryptedData
    * when decrypting
    * */
        numBytesToRead = octstr_len(encryptedData);

        /* Don't write to this pointer. Ever ever ever. */
   tempEncryptionPointer = (unsigned char *)octstr_get_cstr(encryptedData);
        
        /* Decrypt the data in encryptedData */
   debug("wtls", 0, "RSA_private_decrypt: Private_key: 0x%p", private_key);
   numBytesWritten = RSA_private_decrypt(numBytesToRead,
                     tempEncryptionPointer,
                     tempDecryptionBuffer, private_key,
                     RSA_PKCS1_PADDING);

   if (numBytesWritten == -1) {
			tempEncryptionPointer += 2;
			numBytesToRead -= 2;
      numBytesWritten = RSA_private_decrypt(numBytesToRead,
                        tempEncryptionPointer,
                        tempDecryptionBuffer,
                        private_key,
                        RSA_PKCS1_PADDING);
		}		
 
   if (numBytesWritten > 0) {
        /* Move the tempDecryptionBuffer to an Octstr */
      decryptedData =
          octstr_create_from_data((char *)tempDecryptionBuffer,
                   numBytesWritten);
      debug("wtls", 0, "Client's secret decrypted succesfully");
   }

        /* Deallocate the tempDecryptionBuffer */
        gw_free(tempDecryptionBuffer);
        /* Return the decrypted data */
        return decryptedData;
}

Octstr *wtls_decrypt_key(int type, Octstr * encryptedData)
{
   switch (type) {
   case rsa_anon:
   case rsa:
      return (wtls_decrypt_rsa(encryptedData));
      break;
   default:
      break;
   }
   return (NULL);
}

void wtls_decrypt_pdu_list(WTLSMachine * wtls_machine, List * pdu_list)
{
	int i, listlen;
   Octstr *decryptedData = NULL;
	wtls_Payload *payload;
	
	listlen = gwlist_len(pdu_list);
   for (i = 0; i < listlen; i++) {
      payload = (wtls_Payload *) gwlist_get(pdu_list, i);
      wtls_machine->client_seq_num++;
		
      if (payload->cipher) {
			debug("wtls", 0, "Decrypting PDU %d", i);
         if ((decryptedData =
              wtls_decrypt(payload, wtls_machine))) {
			/* replace the data */
			octstr_destroy(payload->data);
            /* get rid of MAC leftovers and wtls headers */
			payload->data = decryptedData;
         } else {
            gwlist_delete(pdu_list, i, 1);
            wtls_payload_destroy(payload);
            payload = NULL;
            i--;
            listlen--;
		}
      } else
			debug("wtls", 0, "PDU %d is not encrypted.", i);
      if (payload && octstr_get_char(payload->data, 0) == '\1')
         wtls_machine->client_seq_num = -1;   // Change Cipher
      debug("wtls", 0, "Received Incoming Payload:");
      wtls_payload_dump(payload, 1);
	}
}

RSAPublicKey *wtls_get_rsapublickey(void)
{
   RSA *rsaStructure = NULL;
   EVP_PKEY *publicKey = NULL;
   BIGNUM *modulus = 0, *exponent = NULL;
   unsigned char *tempModulusStorage = 0, *tempExponentStorage = NULL;
   int numbytes = 0;
   RSAPublicKey *returnStructure = NULL;
   Octstr *Modulus = NULL, *Exponent = NULL;
        
        /* First, we need to extract the RSA structure from the X509 Cert */
        /* Get the EVP_PKEY structure from the X509 cert */
        publicKey = X509_PUBKEY_get(x509_cert->cert_info->key);
        
        /* Take said EVP_PKEY structure and get the RSA component */
   if (EVP_PKEY_type(publicKey->type) != EVP_PKEY_RSA) {
                return NULL;
   } else {
                rsaStructure = publicKey->pkey.rsa;
        }
        
        /* Then we need to grab the exponent component from the cert */
        exponent = rsaStructure->e;
        
        /* We need to allocate sufficient memory to hold the exponent */
        numbytes = BN_num_bytes(exponent);
        tempExponentStorage = gw_malloc(numbytes);
        
        /* Then we get the exponent */
        numbytes = BN_bn2bin(exponent, tempExponentStorage);
        
        /* And finally we convert the exponent to an Octstr */
   Exponent = octstr_create_from_data((char *)tempExponentStorage,
                  numbytes);

        /* Then we need to grab the modulus component from the cert */
        modulus = rsaStructure->n;
        
        /* We need to allocate sufficient memory to hold the modulus */
        numbytes = BN_num_bytes(modulus);
        tempModulusStorage = gw_malloc(numbytes);
        
        /* Then we get the modulus */
        numbytes = BN_bn2bin(modulus, tempModulusStorage);
        
        /* And finally we convert the modulus to an Octstr */
   Modulus = octstr_create_from_data((char *)tempModulusStorage, numbytes);

        /* Put the components into our return structure */
        returnStructure = gw_malloc(sizeof(RSAPublicKey));
   returnStructure->rsa_exponent = Exponent;
   returnStructure->rsa_modulus = Modulus;
        
        /* And deallocate the memory allocated for holding the modulus */
        gw_free(tempModulusStorage);
        gw_free(tempExponentStorage);
        
   return (returnStructure);
}

Octstr *wtls_get_certificate(void)
{
   unsigned char **pp;
   unsigned char *ppStart;
        int amountWritten = 1260;
   Octstr *returnOctstr;
        
   debug("wtls_get_certificate", 0, "x509_cert : 0x%p", x509_cert);
        /* Convert the x509 certificate to DER-encoding */
   amountWritten = i2d_X509(x509_cert, NULL);
   debug("wtls_get_certificate", 0, "amountWritten : %d", amountWritten);

        /* Allocate some memory for *pp */
   pp = (unsigned char **)gw_malloc(sizeof(unsigned char **));
        
        /* Allocate the memory and call the same function again?!!?
           What an original idea :-/ */
   ppStart =
       (unsigned char *)gw_malloc(sizeof(unsigned char) * amountWritten);
   debug("wtls_get_certificate", 0, "x509_cert_DER_pre : 0x%p", *pp);
        *pp = ppStart;
   amountWritten = i2d_X509(x509_cert, pp);

        /* And we do this, because otherwise *pp is pointing to the end of the buffer. Yay */
        *pp = ppStart;
   debug("wtls_get_certificate", 0, "x509_cert_DER_post : 0x%p", *pp);
        
        /* Convert the DER-encoded char string to an octstr */
   returnOctstr = octstr_create_from_data((char *)*pp, amountWritten);

        /* Destroy the memory allocated temporarily above */
        gw_free(*pp);
        
        /* Destroy the memory allocated for pp as well */
        gw_free(pp);
        
        /* Return the octstr */
        return returnOctstr;
}

/* Chooses a CipherSuite from the list provided by the client.
   Returns NULL if none is acceptable. */
CipherSuite *wtls_choose_ciphersuite(List * ciphersuites)
{
   CipherSuite *currentCS;
   int i = 0, listLen;
		
		listLen = gwlist_len(ciphersuites);
		
		/* the first CS in the list */
   for (; i < listLen; i++) {
			/* the next CS in the list */
			currentCS = gwlist_get(ciphersuites, i);
			/* Check if we support this BulkCipher */
      if (currentCS->bulk_cipher_algo == DES_CBC ||
          (currentCS->bulk_cipher_algo <= RC5_CBC &&
           currentCS->bulk_cipher_algo >= RC5_CBC_40))
/*      if(currentCS->bulk_cipher_algo >= NULL_bulk &&
         currentCS->bulk_cipher_algo <= IDEA_CBC)
*/  {
				/* Check if we support this MAC algsorithm */
         if (currentCS->mac_algo >= SHA_0 &&
				   currentCS->mac_algo <= MD5_NOLIMIT) {
            char cipher[20], mac[10];

					/* We can use this CipherSuite then */
            cipherName(cipher, currentCS->bulk_cipher_algo);
            macName(mac, currentCS->mac_algo);
            debug("wtls", 0,
                  "wtls_choose_ciphersuite ~> Accepted cipher: %s, mac: %s (#%d/%d)",
                  cipher, mac, i + 1, listLen);
            break;
				}
			}
   }
   if (i < listLen)
      return (currentCS);
   else
      return (NULL);
}

int isSupportedKeyEx(int keyExId)
{
	int maxSupported;
   int i = 0, retCode = 0;
	
	maxSupported = sizeof(supportedKeyExSuite) / sizeof(KeyExchangeSuite);
	
   for (; i < maxSupported; i++) {
      if (keyExId == supportedKeyExSuite[i]) {
			retCode = 1;
         break;
		}
	}
	return retCode;
}

int wtls_choose_clientkeyid(List * clientKeyIds, int *algo)
{
		int returnKey = 0;
		KeyExchangeId *currentKeyId = NULL;
   int i = 0, listLen;
		
		listLen = gwlist_len(clientKeyIds);
		
   for (; i < listLen; i++) {
			currentKeyId = gwlist_get(clientKeyIds, i);
			
			/* check if the current key suite is supported */
      if (isSupportedKeyEx(currentKeyId->key_exchange_suite)) {
         char key[20];
		
         *algo = currentKeyId->key_exchange_suite;
         returnKey = i + 1;
         keyName(key, *algo);
         debug("wtls", 0,
               "wtls_choose_clientkeyid ~> Accepted key algorithm: %s (#%d/%d)",
               key, returnKey, listLen);
         dump_key_exchange_id("wtls", 0, currentKeyId);
         break;
      }
   }
        return returnKey;
}

int wtls_choose_snmode(int snmode)
{
        return 2;
}

Random *wtls_get_random(void)
{
   Random *randomData;
   unsigned char bytes[13], *p;
   struct timeval tp;

        randomData = gw_malloc(sizeof(Random));
   gettimeofday(&tp, NULL);
   randomData->gmt_unix_time = tp.tv_sec;
   p = bytes;
   while (p - bytes < 12) {
      while (!(*p = rand_r((uint *) & tp.tv_usec))) ;
      p++;
   }
   bytes[12] = '\0';

   randomData->random_bytes = octstr_create((char *)bytes);
   return (randomData);
}

int clienthellos_are_identical(List * pdu_list, List * last_received_packet)
{
        return 0;
}

int certifcateverifys_are_identical(List * pdu_list,
                List * last_received_packet)
{
        return 0;
}

int certificates_are_identical(List * pdu_list, List * last_received_packet)
{
        return 0;
}

int clientkeyexchanges_are_identical(List * pdu_list,
                 List * last_received_packet)
{
        return 0;
}

int changecipherspecs_are_identical(List * pdu_list,
                List * last_received_packet)
{
        return 0;
}

int finishes_are_indentical(List * pdu_list, List * last_received_packet)
{
        return 0;
}

int packet_contains_changecipherspec(List * pdu_list)
{
        return 0;
}

int packet_contains_finished(List * pdu_list)
{
        return 0;
}

int packet_contains_optional_stuff(List * pdu_list)
{
        return 0;
}

int packet_is_application_data(List * pdu_list)
{
   int i, len = gwlist_len(pdu_list);
   wtls_Payload *tempPayload;

   for (i = 0; i < len; i++) {
      tempPayload = gwlist_get(pdu_list, i);
      if (tempPayload->type != Application_PDU)
         return (0);
   }
        return 1;
}

int packet_contains_userdata(List * pdu_list)
{
   return 1;
}

int packet_contains_clienthello(List * pdu_list)
{
   int i, len = gwlist_len(pdu_list);
   wtls_Payload *tempPayload;

   for (i = 0; i < len; i++) {
      tempPayload = gwlist_get(pdu_list, i);
      if (tempPayload->type == Handshake_PDU) {
         if (octstr_get_char(tempPayload->data, 0) ==
             client_hello)
            return (1);
      }
   }
   return (0);
}

int is_critical_alert(List * pdu_list, WTLSMachine * wtls_machine)
{
   int i, listlen;
   wtls_Payload *payload;

   listlen = gwlist_len(pdu_list);

   for (i = 0; i < listlen; i++) {
      payload = gwlist_get(pdu_list, i);

      if (payload->type == Alert_PDU &&
          octstr_get_char(payload->data, 0) >= critical_alert) {
         char alert[40];

         alertName(alert, octstr_get_char(payload->data, 1));
         error(0, "Received critical alert (%s) in %s. Aborting",
               alert, stateName(wtls_machine->state));
         return (1);
      }
   }
        return 0;
}

int is_warning_alert(List * pdu_list, WTLSMachine * wtls_machine)
{
   int i, listlen;
   wtls_Payload *payload;

   listlen = gwlist_len(pdu_list);

   for (i = 0; i < listlen; i++) {
      payload = gwlist_get(pdu_list, i);

      if (payload->type == Alert_PDU &&
          octstr_get_char(payload->data, 0) == warning_alert) {
         char alert[40];

         alertName(alert, octstr_get_char(payload->data, 1));
         warning(0, "Received warning (%s) in %s.", alert,
            stateName(wtls_machine->state));
         return (1);
      }
   }
   return 0;
}

/* go through the list of wtls_Payloads and add the data of any 
   handshake message to wtls_machine->handshake_data */
void add_all_handshake_data(WTLSMachine * wtls_machine, List * pdu_list)
{
	long i, listlen;
	wtls_Payload *payload;

	gw_assert(pdu_list != NULL);
	
	listlen = gwlist_len(pdu_list);
   debug("wtls", 0, "adding handshake data from %ld PDU(s)", listlen);
   for (i = 0; i < listlen; i++) {
      payload = (wtls_Payload *) gwlist_get(pdu_list, i);
      if (payload->type == Handshake_PDU) {
         octstr_insert(wtls_machine->handshake_data,
                  payload->data,
                          octstr_len(wtls_machine->handshake_data));
         debug("wtls", 0, "Data from PDU %ld:", i);
			octstr_dump(payload->data, 2);
		}
	}
}

void calculate_server_key_block(WTLSMachine * wtls_machine)
{
   Octstr *concatenatedRandoms = NULL;
   Octstr *labelMaster = NULL;
   Octstr *key_block;
   int seqNum, refresh;

   refresh = 1 << wtls_machine->key_refresh;
    /* Concatenate our random data */
    concatenatedRandoms = octstr_create("");
   seqNum = wtls_machine->server_seq_num;
   seqNum -= wtls_machine->server_seq_num % refresh;
   pack_int16(concatenatedRandoms, 0, seqNum);
    octstr_append(concatenatedRandoms, wtls_machine->server_random);
    octstr_append(concatenatedRandoms, wtls_machine->client_random);

    /* Calculate the key_block */
    labelMaster = octstr_create("server expansion");
    key_block = wtls_calculate_prf(wtls_machine->master_secret, labelMaster,
                                   concatenatedRandoms,
                   hash_table[wtls_machine->mac_algorithm].
                   key_size +
                   bulk_table[wtls_machine->
                    bulk_cipher_algorithm].
                   key_material +
                   bulk_table[wtls_machine->
                    bulk_cipher_algorithm].
                   iv_size, wtls_machine);

    octstr_destroy(labelMaster);
    octstr_destroy(concatenatedRandoms);
   labelMaster = NULL;

    /* Break the key_block in its 3 parts */
   wtls_machine->server_write_MAC_secret =
       octstr_copy(key_block, 0,
         hash_table[wtls_machine->mac_algorithm].key_size);
   octstr_delete(key_block, 0,
            hash_table[wtls_machine->mac_algorithm].key_size);
   wtls_machine->server_write_enc_key =
       octstr_copy(key_block, 0,
         bulk_table[wtls_machine->bulk_cipher_algorithm].
         key_material);
   octstr_delete(key_block, 0,
            bulk_table[wtls_machine->bulk_cipher_algorithm].
            key_material);
   wtls_machine->server_write_IV =
       octstr_copy(key_block, 0,
         bulk_table[wtls_machine->bulk_cipher_algorithm].
         iv_size);
   octstr_destroy(key_block);

    /* Additional calculations for exportable encryption algos */
   if (bulk_table[wtls_machine->bulk_cipher_algorithm].is_exportable ==
       EXPORTABLE) {
      Octstr *final_server_write_enc_key = NULL;
      Octstr *final_server_write_IV = NULL;
      Octstr *emptySecret = NULL;

      concatenatedRandoms =
          octstr_cat(wtls_machine->client_random,
                wtls_machine->server_random);
        labelMaster = octstr_create("server write key");
      final_server_write_enc_key =
          wtls_calculate_prf(wtls_machine->server_write_enc_key,
                   labelMaster, concatenatedRandoms,
                   bulk_table[wtls_machine->
                    bulk_cipher_algorithm].
                   expanded_key_material, wtls_machine);
        octstr_destroy(labelMaster);
        octstr_destroy(wtls_machine->server_write_enc_key);
        wtls_machine->server_write_enc_key = final_server_write_enc_key;
        final_server_write_enc_key = NULL;

        concatenatedRandoms = octstr_create("");
      pack_int16(concatenatedRandoms, 0, seqNum);
        octstr_append(concatenatedRandoms, wtls_machine->client_random);
        octstr_append(concatenatedRandoms, wtls_machine->server_random);

      labelMaster = octstr_create("server write IV");
        emptySecret = octstr_create("");
      final_server_write_IV =
          wtls_calculate_prf(emptySecret, labelMaster,
                                concatenatedRandoms,
                   bulk_table[wtls_machine->
                    bulk_cipher_algorithm].
                   iv_size, wtls_machine);
      octstr_destroy(wtls_machine->server_write_IV);
      wtls_machine->server_write_IV = final_server_write_IV;
      octstr_destroy(emptySecret);
        octstr_destroy(labelMaster);
        octstr_destroy(concatenatedRandoms);
    }
}

void calculate_client_key_block(WTLSMachine * wtls_machine)
{
   Octstr *concatenatedRandoms = NULL;
   Octstr *key_block;
   Octstr *labelMaster = NULL;
   int seqNum, refresh;

   refresh = 1 << wtls_machine->key_refresh;
    /* Concatenate our random data */
    concatenatedRandoms = octstr_create("");
   seqNum = wtls_machine->client_seq_num;
   seqNum -= wtls_machine->client_seq_num % refresh;
   wtls_machine->last_refresh = seqNum;
   pack_int16(concatenatedRandoms, 0, seqNum);
    octstr_append(concatenatedRandoms, wtls_machine->server_random);
    octstr_append(concatenatedRandoms, wtls_machine->client_random);

    /* Calculate the key_block */
    labelMaster = octstr_create("client expansion");
    key_block = wtls_calculate_prf(wtls_machine->master_secret, labelMaster,
                                 concatenatedRandoms,
                   hash_table[wtls_machine->mac_algorithm].
                   key_size +
                   bulk_table
                   [wtls_machine->bulk_cipher_algorithm].
                   key_material +
                   bulk_table[wtls_machine->
                    bulk_cipher_algorithm].
                   iv_size, wtls_machine);

    octstr_destroy(labelMaster);
    octstr_destroy(concatenatedRandoms);
   labelMaster = NULL;

    /* Break the key_block in its 3 parts */
   wtls_machine->client_write_MAC_secret = octstr_copy(key_block, 0,
                         hash_table
                         [wtls_machine->
                          mac_algorithm].
                         key_size);
   octstr_delete(key_block, 0,
            hash_table[wtls_machine->mac_algorithm].key_size);
   wtls_machine->client_write_enc_key =
       octstr_copy(key_block, 0,
         bulk_table[wtls_machine->bulk_cipher_algorithm].
         key_material);
   octstr_delete(key_block, 0,
            bulk_table[wtls_machine->bulk_cipher_algorithm].
            key_material);
   wtls_machine->client_write_IV =
       octstr_copy(key_block, 0,
         bulk_table[wtls_machine->bulk_cipher_algorithm].
         iv_size);
   octstr_destroy(key_block);

    /* Additional calculations for exportable encryption algos */
   if (bulk_table[wtls_machine->bulk_cipher_algorithm].is_exportable ==
       EXPORTABLE) {
      Octstr *final_client_write_enc_key = NULL;
      Octstr *final_client_write_IV = NULL;
      Octstr *emptySecret = NULL;

      concatenatedRandoms = octstr_cat(wtls_machine->client_random,
                   wtls_machine->server_random);
        labelMaster = octstr_create("client write key");
      final_client_write_enc_key =
          wtls_calculate_prf(wtls_machine->client_write_enc_key,
                   labelMaster, concatenatedRandoms,
                   bulk_table
                   [wtls_machine->bulk_cipher_algorithm].
                   expanded_key_material, wtls_machine);

        octstr_destroy(wtls_machine->client_write_enc_key);
      octstr_destroy(labelMaster);
        wtls_machine->client_write_enc_key = final_client_write_enc_key;
        final_client_write_enc_key = NULL;
        octstr_destroy(concatenatedRandoms);

      concatenatedRandoms = octstr_create("");
      pack_int16(concatenatedRandoms, 0, seqNum);
      octstr_append(concatenatedRandoms, wtls_machine->client_random);
      octstr_append(concatenatedRandoms, wtls_machine->server_random);

      labelMaster = octstr_create("client write IV");
        emptySecret = octstr_create("");
      final_client_write_IV =
          wtls_calculate_prf(emptySecret, labelMaster,
                                concatenatedRandoms,
                   bulk_table
                   [wtls_machine->bulk_cipher_algorithm].
                   iv_size, wtls_machine);
      octstr_destroy(wtls_machine->client_write_IV);
      wtls_machine->client_write_IV = final_client_write_IV;
      final_client_write_IV = NULL;

      octstr_destroy(emptySecret);
        octstr_destroy(labelMaster);
        octstr_destroy(concatenatedRandoms);
    }
}

#endif /* HAVE_WTLS_OPENSSL */
