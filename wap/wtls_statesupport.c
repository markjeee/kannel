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
 * wtls_statesupport.c
 * 
 * 2001  Nick Clarey, Yann Muller for 3G LAB
 */

#include "gwlib/gwlib.h"

#if (HAVE_WTLS_OPENSSL)

#include <openssl/x509.h>
#ifndef NO_RC5
#include <openssl/rc5.h>
#else
#error "your OpenSSL installation lacks RC5 algorithm support"
#endif

#include "wtls_statesupport.h"

#define BLOCKLENGTH 64
#define INNERPAD 0x36
#define OUTERPAD 0x5C

extern X509* x509_cert;
extern RSA* private_key;
extern KeyExchangeSuite client_key_exchange_algo;
extern PublicKeyAlgorithm public_key_algo;
extern SignatureAlgorithm signature_algo;

Octstr* wtls_hmac_hash(Octstr* key, Octstr* data, WTLSMachine* wtls_machine);
Octstr* wtls_hash(Octstr* inputData, WTLSMachine* wtls_machine);
Octstr* wtls_encrypt_rc5(Octstr* data, WTLSMachine* wtls_machine);
Octstr* wtls_decrypt_rc5(Octstr* encryptedData, WTLSMachine* wtls_machine);


/* Add here the supported KeyExchangeSuites 
   used by wtls_choose_clientkeyid */
KeyExchangeSuite supportedKeyExSuite[] = { rsa_anon };

Octstr* wtls_decrypt(Octstr* buffer, WTLSMachine* wtls_machine)
{
        return wtls_decrypt_rc5(buffer,wtls_machine);
}

/* This function will convert our buffer into a completed GenericBlockCipher */
Octstr* wtls_encrypt(Octstr* buffer, WTLSMachine* wtls_machine, int recordType)
{
        Octstr* bufferCopy;
        Octstr* encryptedContent;
        Octstr* contentMac;
        Octstr* padding;
        Octstr* tempData;

        unsigned char* tempPadding;
        
        int paddingLength, contentLength, macSize, blockLength,
                sequenceNumber, bufferLength;
        int i;

        /* Copy our buffer */
        bufferCopy = octstr_duplicate(buffer);
        
        /* Get the MAC of the content */
        sequenceNumber = wtls_machine->server_seq_num;
        bufferLength  = octstr_len(buffer);

        /* Copy the buffer in preparation for MAC calculation */
        tempData = octstr_create("");
        pack_int16(tempData, 0, sequenceNumber);
        octstr_append_char(tempData, recordType);
        pack_int16(tempData, octstr_len(tempData), bufferLength);        
        octstr_append(tempData, buffer);

        /* Calculate the MAC */
        contentMac = wtls_hmac_hash(wtls_machine->server_write_MAC_secret, tempData ,wtls_machine);

        /* Calculate the padding length */
        contentLength = octstr_len(bufferCopy);
        macSize = hash_table[wtls_machine->mac_algorithm].mac_size;
        blockLength = bulk_table[wtls_machine->bulk_cipher_algorithm].block_size;

        paddingLength = (contentLength + macSize + 1) % (blockLength);

        /* Append the MAC to the bufferCopy */
        octstr_append(bufferCopy,contentMac);
        
        if (paddingLength > 0) {
                /* Pad with the paddingLength itself paddingLength times. Confused yet? */
                tempPadding = gw_malloc(paddingLength);
                for (i=0;i < paddingLength; i++) {
                        /* You're probably really spaced out around now...
                           see section 9.2.3.3 for more details... */
                        tempPadding[i] = paddingLength;
                }
                octstr_append_data(bufferCopy, tempPadding, paddingLength);
        }
        /* Add the length byte */
        octstr_append_char(bufferCopy, paddingLength);                

        /* Encrypt the content */
        encryptedContent =  wtls_encrypt_rc5(bufferCopy,wtls_machine);

        return encryptedContent;
}

/* P_hash as described in WAP WTLS section 11.3.2 */
Octstr* wtls_P_hash(Octstr* secret, Octstr* seed, int byteLength, WTLSMachine* wtls_machine)
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
		a = wtls_hmac_hash(secret, aPrev, wtls_machine);
		aPlusSeed = octstr_cat(a, seed);
		/* HMAC */
		hashTemp = wtls_hmac_hash(secret, aPlusSeed, wtls_machine);
		octstr_append(hashedData, hashTemp);
		octstr_destroy(hashTemp);
		/* Update a(i-1) */
		octstr_destroy(aPrev);
		aPrev = a;
	} while(octstr_len(hashedData) < byteLength);
	
	gw_free(aPlusSeed);

	return hashedData;
}

/* Pseudo Random Function (PRF) as described in WAP WTLS section 11.3.2 */
Octstr* wtls_calculate_prf(Octstr* secret, Octstr* label,
                           Octstr* seed, int byteLength, WTLSMachine* wtls_machine)
{
        Octstr* returnOctstr;
    	Octstr *labelPlusSeed;

    	/* Create label + seed */
		labelPlusSeed = octstr_cat(label, seed);
		
		/* PRF(secret, label, seed) = P_hash(secret, label + seed) */
		returnOctstr = wtls_P_hash(secret, labelPlusSeed, byteLength, wtls_machine);
		
    	/* Return the first nbytes of the hashed data */
		octstr_truncate(returnOctstr, byteLength);
		 
    	gw_free(labelPlusSeed);
    	return returnOctstr;
		
}

/* MAC calculation */
Octstr* wtls_hmac_hash(Octstr* key, Octstr* data, WTLSMachine* wtls_machine)
{
    static unsigned char final_mac[1024];
    unsigned char *mac, *buffer, *keyString;
	int mac_len, bufferlen, keylen;
	Octstr *returnOctstr;
	
	buffer = octstr_get_cstr(data);
	bufferlen = octstr_len(data);
    keyString = octstr_get_cstr(key);
	keylen = octstr_len(key);
	
	mac = final_mac;
	
    switch (wtls_machine->mac_algorithm) {
		case SHA_0:
			/* no keyed MAC is calculated */
			/* So what do we return ? */
			break;
		case SHA_40:
		case SHA_80:
		case SHA_NOLIMIT:
    		HMAC(EVP_sha1(), keyString, keylen,
				 buffer, bufferlen,
        		 mac, &mac_len);
			break;
		case SHA_XOR_40:
			// dunno yet
			break;
		case MD5_40:
		case MD5_80:
		case MD5_NOLIMIT:
    		HMAC(EVP_md5(), keyString, keylen,
				 buffer, bufferlen,
        		 mac, &mac_len);
			break;
	}

    returnOctstr = octstr_create_from_data(mac, mac_len);
}


/* Not to be confused with octstr_hash, this applies the currently set hashing
   algorithm from wtls_machine to the supplied input data, returning a hashed
   Octstr. If it fails, it will return a NULL pointer */
Octstr* wtls_hash(Octstr* inputData, WTLSMachine* wtls_machine)
{
        int inputDataLength;
        int outputDataLength;
        unsigned char* outputDataTemp;
        unsigned char* inputDataTemp;
        unsigned char* tempPointer;
        Octstr* outputData;

        inputDataLength = octstr_len(inputData);
        outputDataLength = hash_table[wtls_machine->mac_algorithm].key_size;
        inputDataTemp = gw_malloc(inputDataLength);
        outputDataTemp = gw_malloc(outputDataLength);

        /* Copy the contents of inputData into inputDataTemp, ready for hashing */
        tempPointer = octstr_get_cstr(inputData);
        memcpy((void*) inputDataTemp, (void*)tempPointer, inputDataLength);
        
        /* Hash away! */
        // Here's where we need to hash on the selected algorithm, not just the SHA-1 algorithm
		//debug("wtls", 0, "mac algo %d", wtls_machine->mac_algorithm);
        switch (wtls_machine->mac_algorithm) {
			case SHA_0:
				/* no keyed MAC is calculated */
				// So what do we return ?
				break;
			case SHA_40:
			case SHA_80:
			case SHA_NOLIMIT:
				tempPointer = SHA1(inputDataTemp, inputDataLength, outputDataTemp);
				break;
			case SHA_XOR_40:
				// dunno yet
				break;
			case MD5_40:
			case MD5_80:
			case MD5_NOLIMIT:
				tempPointer = MD5(inputDataTemp, inputDataLength, outputDataTemp);
				break;
		}
        if (tempPointer == NULL){
                /* Pop out an error */
        }

        /* Get our output data setup */
        outputData = octstr_create_from_data(outputDataTemp,outputDataLength);
        
		/* some algorithms don't use the full length of H */
        octstr_truncate(outputData, hash_table[wtls_machine->mac_algorithm].mac_size);
		
        /* Delete our allocated memory */
        gw_free(outputDataTemp);
        gw_free(inputDataTemp);
        outputDataTemp = NULL;
        inputDataTemp = NULL;
        
        /* Return the outputData */
        return outputData;
}

Octstr* wtls_decrypt_rc5(Octstr* data, WTLSMachine* wtls_machine)
{
        Octstr* encryptedData;
        Octstr* decryptedData;
        Octstr* duplicatedIv;
        unsigned char* output;
        unsigned char* input;
        unsigned char* iv;
        unsigned char* keyData;
        int keyLen;
        int ivLen;
        int dataLen;
        
        RC5_32_KEY* key = NULL;
        ivLen = bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size;
        duplicatedIv = octstr_duplicate(wtls_machine->client_write_IV);
        iv = octstr_get_cstr(duplicatedIv);

        keyLen = bulk_table[wtls_machine->bulk_cipher_algorithm].key_material;
        keyData = octstr_get_cstr(wtls_machine->client_write_enc_key);

        dataLen = octstr_len(data);
        input = octstr_get_cstr(data);
        
        key = gw_malloc (sizeof(RC5_32_KEY));
        
        /* Key generation */
        RC5_32_set_key(key, keyLen, keyData,  RC5_16_ROUNDS);
        
        /* Malloc our output */
        output = gw_malloc (dataLen);
        
        /* Encrypt the string */
         debug("wtls_statesupport",0,"About to decrypt: dataLen = %d, iv = %x", dataLen, iv);
         octstr_dump(data,0);
         RC5_32_cbc_encrypt(input, output, dataLen, key, iv, RC5_DECRYPT);
         debug("wtls_statesupport",0,"Decrypted");
         decryptedData = octstr_create_from_data(output, dataLen);
         octstr_dump(decryptedData,0);         

         /* Encrypt it just to test */
         gw_free(output);
         output = NULL;
         output = gw_malloc (dataLen);
         
          /* Ensure that we preserve the iv */
         octstr_destroy(duplicatedIv);
         duplicatedIv = octstr_duplicate(wtls_machine->client_write_IV);
         iv = octstr_get_cstr(duplicatedIv);

         octstr_get_many_chars(iv, wtls_machine->client_write_IV,0,ivLen);         
         input = octstr_get_cstr(decryptedData);
        
         RC5_32_cbc_encrypt(input, output, dataLen, key, iv, RC5_ENCRYPT);
         encryptedData = octstr_create_from_data(output, dataLen);
         
         gw_free(output);
         output = NULL;
         octstr_destroy(duplicatedIv);
         return decryptedData;
}

Octstr* wtls_encrypt_rc5(Octstr* data, WTLSMachine* wtls_machine)
{
        Octstr* encryptedData;
        Octstr* decryptedData;
        Octstr* duplicatedIv;
        unsigned char* output;
        unsigned char* input;
        unsigned char* iv;
        unsigned char* keyData;
        int keyLen;
        int ivLen;
        int dataLen;
        
        RC5_32_KEY* key = NULL;
        ivLen = bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size;
        duplicatedIv = octstr_duplicate(wtls_machine->server_write_IV);
        iv = octstr_get_cstr(duplicatedIv);

        keyLen = bulk_table[wtls_machine->bulk_cipher_algorithm].key_material;
        keyData = octstr_get_cstr(wtls_machine->server_write_enc_key);

        dataLen = octstr_len(data);
        input = octstr_get_cstr(data);
        
        key = gw_malloc (sizeof(RC5_32_KEY));
        
        /* Key generation */
        debug("wtls_statesupport",0,"Key generation");
        RC5_32_set_key(key, keyLen, keyData,  RC5_16_ROUNDS);
        
        /* Malloc our output */
        output = gw_malloc (dataLen);
        
        /* Encrypt the string */
         RC5_32_cbc_encrypt(input, output, dataLen, key, iv, RC5_ENCRYPT);
         encryptedData = octstr_create_from_data(output, dataLen);

         /* Decrypt it just to test */
         gw_free(output);
         output = NULL;
         output = gw_malloc (dataLen);
         
          /* Ensure that we preserve the iv */
         octstr_destroy(duplicatedIv);
         duplicatedIv = octstr_duplicate(wtls_machine->server_write_IV);
         iv = octstr_get_cstr(duplicatedIv);

         octstr_get_many_chars(iv, wtls_machine->server_write_IV,0,ivLen);         
         input = octstr_get_cstr(encryptedData);
        
         RC5_32_cbc_encrypt(input, output, dataLen, key, iv, RC5_DECRYPT);
         decryptedData = octstr_create_from_data(output, dataLen);

         gw_free(output);
         output = NULL;
         octstr_destroy(duplicatedIv);
         return encryptedData;
}

Octstr* wtls_decrypt_rsa(Octstr* encryptedData)
{
        int numBytesWritten=0,numBytesToRead=0;
        Octstr *decryptedData=0;
        unsigned char* tempDecryptionBuffer=0;
        char* tempEncryptionPointer=0;
        
        /* Allocate some memory for our decryption buffer */
        tempDecryptionBuffer = gw_malloc(RSA_size(private_key));

        /* Calculate the number of bytes to read from encryptedData when decrypting */
        numBytesToRead = octstr_len(encryptedData);

        /* Don't write to this pointer. Ever ever ever. */
        tempEncryptionPointer = octstr_get_cstr(encryptedData);
        
        /* Decrypt the data in encryptedData */
        numBytesWritten = RSA_private_decrypt(numBytesToRead, tempEncryptionPointer,
                                              tempDecryptionBuffer, private_key, RSA_PKCS1_PADDING);

		if(numBytesWritten == -1) {
			tempEncryptionPointer += 2;
			numBytesToRead -= 2;
	        numBytesWritten = RSA_private_decrypt(numBytesToRead, tempEncryptionPointer,
                                              tempDecryptionBuffer, private_key, RSA_PKCS1_PADDING);
		}		
 
        /* Move the tempDecryptionBuffer to an Octstr */
        decryptedData = octstr_create_from_data(tempDecryptionBuffer,numBytesWritten);

        /* Deallocate the tempDecryptionBuffer */
        gw_free(tempDecryptionBuffer);
        tempDecryptionBuffer = NULL;

		debug("wtls",0, "Decrypted secret");
		octstr_dump(   decryptedData, 0);
		     
        /* Return the decrypted data */
        return decryptedData;
}

void wtls_decrypt_pdu_list(WTLSMachine *wtls_machine, List *pdu_list)
{
	int i, listlen;
    Octstr* decryptedData = NULL;
	wtls_Payload *payload;
	
	listlen = gwlist_len(pdu_list);
	for( i=0; i<listlen; i++) {
		payload = (wtls_Payload *)gwlist_get(pdu_list, i);
		
		if(payload->cipher) {
			debug("wtls", 0, "Decrypting PDU %d", i);
            decryptedData = wtls_decrypt(payload->data, wtls_machine);
			/* replace the data */
			octstr_destroy(payload->data);
			payload->data = decryptedData;
		}
		else {
			debug("wtls", 0, "PDU %d is not encrypted.", i);
		}
	}
}


RSAPublicKey* wtls_get_rsapublickey(void)
{
        RSA* rsaStructure=0;
        EVP_PKEY* publicKey=0;
        BIGNUM *modulus=0,*exponent=0;
        unsigned char* tempModulusStorage=0,*tempExponentStorage=0;
        int numbytes=0;
        RSAPublicKey* returnStructure=0;
        Octstr *octstrModulus=0, *octstrExponent=0;
        
        /* First, we need to extract the RSA structure from the X509 Cert */
        /* Get the EVP_PKEY structure from the X509 cert */
        publicKey = X509_PUBKEY_get(x509_cert->cert_info->key);
        
        /* Take said EVP_PKEY structure and get the RSA component */
        if (EVP_PKEY_type(publicKey->type) != EVP_PKEY_RSA)
        {
                return NULL;
        }
        else
        {
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
        octstrExponent = octstr_create_from_data(tempExponentStorage,numbytes);

        /* Then we need to grab the modulus component from the cert */
        modulus = rsaStructure->n;
        
        /* We need to allocate sufficient memory to hold the modulus */
        numbytes = BN_num_bytes(modulus);
        tempModulusStorage = gw_malloc(numbytes);
        
        /* Then we get the modulus */
        numbytes = BN_bn2bin(modulus, tempModulusStorage);
        
        /* And finally we convert the modulus to an Octstr */
        octstrModulus = octstr_create_from_data(tempModulusStorage,numbytes);

        /* Put the components into our return structure */
        returnStructure = gw_malloc(sizeof(RSAPublicKey));
        returnStructure->rsa_exponent = octstrExponent;
        returnStructure->rsa_modulus = octstrModulus;
        
        /* And deallocate the memory allocated for holding the modulus */
        gw_free(tempModulusStorage);
        gw_free(tempExponentStorage);
        tempModulusStorage = NULL;
        tempExponentStorage = NULL;
        
        return returnStructure;
}

Octstr* wtls_get_certificate(void)
{
        unsigned char** pp;
        unsigned char* ppStart;
        int amountWritten = 1260;
        Octstr* returnOctstr;
        
        debug("wtls_get_certificate",0,"x509_cert : %x", x509_cert);
        /* Convert the x509 certificate to DER-encoding */
        amountWritten =i2d_X509(x509_cert, NULL);
        debug("wtls_get_certificate",0,"amountWritten : %d", amountWritten);

        /* Allocate some memory for *pp */
        pp = (unsigned char**) gw_malloc(sizeof(unsigned char**));
        
        /* Allocate the memory and call the same function again?!!?
           What an original idea :-/ */
        ppStart = (unsigned char *) gw_malloc (sizeof(unsigned char)*amountWritten);
        debug("wtls_get_certificate",0,"x509_cert_DER_pre : %x", *pp);
        *pp = ppStart;
        amountWritten =i2d_X509(x509_cert, pp);

        /* And we do this, because otherwise *pp is pointing to the end of the buffer. Yay */
        *pp = ppStart;
        debug("wtls_get_certificate",0,"x509_cert_DER_post : %x", *pp);
        
        /* Convert the DER-encoded char string to an octstr */
        returnOctstr = octstr_create_from_data(*pp,amountWritten);

        /* Destroy the memory allocated temporarily above */
        gw_free(*pp);
        *pp = NULL;
        
        /* Destroy the memory allocated for pp as well */
        gw_free(pp);
        pp = NULL;
        
        /* Return the octstr */
        return returnOctstr;
}

/* Chooses a CipherSuite from the list provided by the client.
   Returns NULL if none is acceptable. */
CipherSuite* wtls_choose_ciphersuite(List* ciphersuites) {
        CipherSuite* returnSuite = NULL;
        CipherSuite* currentCS = NULL;
		int i = 0;
        int listLen;
		
		listLen = gwlist_len(ciphersuites);
		
        //returnSuite = gw_malloc(sizeof(CipherSuite));

		/* the first CS in the list */
		do {
			/* the next CS in the list */
			currentCS = gwlist_get(ciphersuites, i);
			/* Check if we support this BulkCipher */
			if(currentCS->bulk_cipher_algo >= RC5_CBC_40 &&
			   currentCS->bulk_cipher_algo <= IDEA_CBC) {
				
				/* Check if we support this MAC algsorithm */
				if(currentCS->mac_algo >= SHA_0 &&
				   currentCS->mac_algo <= MD5_NOLIMIT) {
					/* We can use this CipherSuite then */
					returnSuite = currentCS;
				}
			}
			i++;
		} while(returnSuite == NULL && i < listLen);
		
        return returnSuite;
}

int isSupportedKeyEx(int keyExId) {
	int maxSupported;
	int i;
	int retCode = 0;
	
	maxSupported = sizeof(supportedKeyExSuite) / sizeof(KeyExchangeSuite);
	
	for(i = 0; i<maxSupported; i++) {
		if(keyExId == supportedKeyExSuite[i]) {
			retCode = 1;
		}
	}
	return retCode;
}

int wtls_choose_clientkeyid(List* clientKeyIds) {
		int returnKey = 0;
		KeyExchangeId *currentKeyId = NULL;
		int i = 0;
		int listLen;
		
		listLen = gwlist_len(clientKeyIds);
		debug("wtls", 0, "listLen = %d", listLen);
		
		do {
			currentKeyId = gwlist_get(clientKeyIds, i);
			debug("wtls", 0, "Key %d", i);
			dump_key_exchange_id("wtls", 0, currentKeyId);
			
			/* check if the current key suite is supported */
			if(isSupportedKeyEx(currentKeyId->key_exchange_suite)) {
				returnKey = i+1;
			}
			i++;

		} while(returnKey == 0 && i < listLen);
		
        return returnKey;
}

int wtls_choose_snmode(int snmode)
{
        return 2;
}

int wtls_choose_krefresh(int krefresh)
{
        return 2;
}

Random* wtls_get_random(void)
{
        Random* randomData;
        randomData = gw_malloc(sizeof(Random));
        randomData->gmt_unix_time = 0x0000;

        /* Yeah, I know, it's not very random */
        randomData->random_bytes = octstr_create("000000000000");
        return randomData;
}


int clienthellos_are_identical (List* pdu_list, List* last_received_packet)
{
        return 0;
}

int certifcateverifys_are_identical (List* pdu_list, List* last_received_packet)
{
        return 0;
}
int certificates_are_identical (List* pdu_list, List* last_received_packet)
{
        return 0;
}
int clientkeyexchanges_are_identical (List* pdu_list, List* last_received_packet)
{
        return 0;
}
int changecipherspecs_are_identical (List* pdu_list, List* last_received_packet)
{
        return 0;
}
int finisheds_are_indentical (List* pdu_list, List* last_received_packet)
{
        return 0;
}

int packet_contains_changecipherspec (List* pdu_list)
{
        return 0;
}

int packet_contains_finished (List* pdu_list)
{
        return 0;
}

int packet_contains_optional_stuff (List* pdu_list)
{
        return 0;
}

int packet_contains_userdata (List* pdu_list)
{
		/* FIXME: need to check if it is really Userdata !! */
        return 1;
}

int packet_contains_clienthello (List* pdu_list)
{
        return 0;
}

int is_critical_alert (List* pdu_list)
{
        return 0;
}

int is_warning_alert (List* pdu_list)
{
        return 0;
}


/* go through the list of wtls_Payloads and add the data of any 
   handshake message to wtls_machine->handshake_data */
void add_all_handshake_data(WTLSMachine *wtls_machine, List *pdu_list)
{
	long i, listlen;
	wtls_Payload *payload;

	gw_assert(pdu_list != NULL);
	
	listlen = gwlist_len(pdu_list);
	debug("wtls", 0,"adding handshake data from %d PDU(s)", listlen);
	for(i=0; i<listlen; i++) {
		payload = (wtls_Payload *)gwlist_get(pdu_list, i);
		if(payload->type == Handshake_PDU) {
            octstr_insert(wtls_machine->handshake_data, payload->data,
                          octstr_len(wtls_machine->handshake_data));
			debug("wtls", 0, "Data from PDU %d:", i);
			octstr_dump(payload->data, 2);
		}
	}
}

void calculate_server_key_block(WTLSMachine *wtls_machine)
{
    Octstr* concatenatedRandoms=0;
    Octstr* labelMaster=0;
    Octstr* key_block;
    Octstr* final_server_write_enc_key = NULL;
    Octstr* final_server_write_IV = NULL;
    Octstr* emptySecret = NULL;

    /* Concatenate our random data */
    concatenatedRandoms = octstr_create("");
    pack_int16(concatenatedRandoms, 0, wtls_machine->server_seq_num);
    octstr_append(concatenatedRandoms, wtls_machine->server_random);
    octstr_append(concatenatedRandoms, wtls_machine->client_random);

    /* Calculate the key_block */
    labelMaster = octstr_create("server expansion");
    key_block = wtls_calculate_prf(wtls_machine->master_secret, labelMaster,
                                   concatenatedRandoms,
                                   hash_table[wtls_machine->mac_algorithm].key_size
                                   + bulk_table[wtls_machine->bulk_cipher_algorithm].key_material
                                   + bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size,
                                   wtls_machine );

    octstr_destroy(labelMaster);
    labelMaster = NULL;
    octstr_destroy(concatenatedRandoms);
    concatenatedRandoms = NULL;

    /* Break the key_block in its 3 parts */
    wtls_machine->server_write_MAC_secret = octstr_copy(key_block, 0, hash_table[wtls_machine->mac_algorithm].key_size);
    octstr_delete(key_block, 0, hash_table[wtls_machine->mac_algorithm].key_size);
    wtls_machine->server_write_enc_key = octstr_copy(key_block, 0, bulk_table[wtls_machine->bulk_cipher_algorithm].key_material);
    octstr_delete(key_block, 0, bulk_table[wtls_machine->bulk_cipher_algorithm].key_material);
    wtls_machine->server_write_IV = octstr_copy(key_block, 0, bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size);

    /* Additional calculations for exportable encryption algos */
    if(bulk_table[wtls_machine->bulk_cipher_algorithm].is_exportable == EXPORTABLE) {
        concatenatedRandoms = octstr_cat(wtls_machine->client_random, wtls_machine->server_random);
        labelMaster = octstr_create("server write key");
        final_server_write_enc_key = wtls_calculate_prf(wtls_machine->server_write_enc_key, labelMaster,
                                                concatenatedRandoms,
                                                bulk_table[wtls_machine->bulk_cipher_algorithm].key_material,
                                                wtls_machine);
        octstr_destroy(labelMaster);
        labelMaster = NULL;
        octstr_destroy(concatenatedRandoms);
        concatenatedRandoms = NULL;

        octstr_destroy(wtls_machine->server_write_enc_key);
        wtls_machine->server_write_enc_key = final_server_write_enc_key;
        final_server_write_enc_key = NULL;

        concatenatedRandoms = octstr_create("");
        octstr_append_char(concatenatedRandoms, wtls_machine->server_seq_num);
        octstr_append(concatenatedRandoms, wtls_machine->client_random);
        octstr_append(concatenatedRandoms, wtls_machine->server_random);

        emptySecret = octstr_create("");
        final_server_write_IV = wtls_calculate_prf(emptySecret, labelMaster,
                                concatenatedRandoms,
                                bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size,
                                wtls_machine);
        octstr_destroy(labelMaster);
        labelMaster = NULL;
        octstr_destroy(concatenatedRandoms);
        concatenatedRandoms = NULL;
    }
}

void calculate_client_key_block(WTLSMachine *wtls_machine) {
    Octstr* concatenatedRandoms=0;
    Octstr* key_block;
    Octstr* final_client_write_enc_key = NULL;
    Octstr* final_client_write_IV = NULL;
    Octstr* emptySecret = NULL;
    Octstr* labelMaster=0;

    /* Concatenate our random data */
    concatenatedRandoms = octstr_create("");
    pack_int16(concatenatedRandoms, 0,wtls_machine->client_seq_num);
    octstr_append(concatenatedRandoms, wtls_machine->server_random);
    octstr_append(concatenatedRandoms, wtls_machine->client_random);

    /* Calculate the key_block */
    labelMaster = octstr_create("client expansion");
    key_block = wtls_calculate_prf(wtls_machine->master_secret, labelMaster,
                                 concatenatedRandoms,
                                 hash_table[wtls_machine->mac_algorithm].key_size
                                 + bulk_table[wtls_machine->bulk_cipher_algorithm].key_material
                                 + bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size,
                                 wtls_machine );


    octstr_destroy(labelMaster);
    labelMaster = NULL;
    octstr_destroy(concatenatedRandoms);
    concatenatedRandoms = NULL;

    /* Break the key_block in its 3 parts */
    wtls_machine->client_write_MAC_secret = octstr_copy(key_block, 0, hash_table[wtls_machine->mac_algorithm].key_size);
    octstr_delete(key_block, 0, hash_table[wtls_machine->mac_algorithm].key_size);
    wtls_machine->client_write_enc_key = octstr_copy(key_block, 0, bulk_table[wtls_machine->bulk_cipher_algorithm].key_material);
    octstr_delete(key_block, 0, bulk_table[wtls_machine->bulk_cipher_algorithm].key_material);
    wtls_machine->client_write_IV = octstr_copy(key_block, 0, bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size);

    /* Additional calculations for exportable encryption algos */
    if(bulk_table[wtls_machine->bulk_cipher_algorithm].is_exportable == EXPORTABLE) {
        concatenatedRandoms = octstr_cat(wtls_machine->client_random, wtls_machine->server_random);
        labelMaster = octstr_create("client write key");
        final_client_write_enc_key = wtls_calculate_prf(wtls_machine->client_write_enc_key, labelMaster,
                                         concatenatedRandoms,
                                         bulk_table[wtls_machine->bulk_cipher_algorithm].key_material,
                                         wtls_machine);
        octstr_destroy(labelMaster);
        labelMaster = NULL;

        octstr_destroy(wtls_machine->client_write_enc_key);
        wtls_machine->client_write_enc_key = final_client_write_enc_key;
        final_client_write_enc_key = NULL;

        octstr_destroy(labelMaster);
        labelMaster = NULL;
        octstr_destroy(concatenatedRandoms);
        concatenatedRandoms = NULL;

        emptySecret = octstr_create("");
        final_client_write_IV = wtls_calculate_prf(emptySecret, labelMaster,
                                concatenatedRandoms,
                                bulk_table[wtls_machine->bulk_cipher_algorithm].iv_size,
                                wtls_machine);
        octstr_destroy(labelMaster);
        labelMaster = NULL;
        octstr_destroy(concatenatedRandoms);
                        concatenatedRandoms = NULL;
    }
}

#endif
