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
 * wtls_support.c: pack and unpack WTLS packets
 *
 * Support functions for packing and unpacking PDUs
 * 
 */

#include "gwlib/gwlib.h"

#if (HAVE_WTLS_OPENSSL)

#include "wtls_pdu.h"
#include "wtls_pdusupport.h"
#include "wtls_statesupport.h"

// Change this later !!!!
extern PublicKeyAlgorithm public_key_algo;
extern SignatureAlgorithm signature_algo;

/*****************************************************************
 * PACK functions
 */

int pack_int16(Octstr *data, long charpos, int i) {
	octstr_append_char(data, (i & 0xFF00) >> 8);
	charpos += 1;
	octstr_append_char(data, i & 0x00FF);
	charpos += 1;
	return charpos;
}

int pack_int32(Octstr *data, long charpos, long i) {
	charpos = pack_int16(data, charpos, (i & 0xFFFF0000) >> 16);
	charpos = pack_int16(data, charpos, i & 0xFFFF);
	return charpos;
}

int pack_octstr(Octstr *data, long charpos, Octstr *opaque) {
	octstr_append_char(data, octstr_len(opaque));
	charpos += 1;
	octstr_insert(data, opaque, octstr_len(data));
	charpos += octstr_len(opaque);
	return charpos;
}

int pack_octstr16(Octstr *data, long charpos, Octstr *opaque) {
	charpos += pack_int16(data, charpos, octstr_len(opaque));
	octstr_insert(data, opaque, octstr_len(data));
	charpos += octstr_len(opaque);
	return charpos;
}

int pack_octstr_fixed(Octstr *data, long charpos, Octstr *opaque) {
	octstr_insert(data, opaque, octstr_len(data));
	charpos += octstr_len(opaque);
	return charpos;
}

int pack_random(Octstr *data, long charpos, Random *random) {
	charpos = pack_int32(data, charpos, random->gmt_unix_time);
	charpos = pack_octstr_fixed(data, charpos, random->random_bytes);
	return charpos;
}

int pack_dhparams(Octstr *data, long charpos, DHParameters *dhparams) {
	octstr_append_char(data, dhparams->dh_e);
	charpos += 1;
	charpos = pack_octstr16(data, charpos, dhparams->dh_p);
	charpos = pack_octstr16(data, charpos, dhparams->dh_g);
	return charpos;
}

int pack_ecparams(Octstr *data, long charpos, ECParameters *ecparams) {
	/* field */
	octstr_append_char(data, ecparams->field);
	charpos += 1;
	switch (ecparams->field) {
	case ec_prime_p:
		charpos = pack_octstr(data, charpos, ecparams->prime_p);
		break;
	case ec_characteristic_two:
		/* m (16 bits) */
		charpos = pack_int16(data, charpos, ecparams->m);
		/* basis */
		octstr_append_char(data, ecparams->basis);
		charpos += 1;
		switch (ecparams->basis) {
		case ec_basis_onb:
			break;
		case ec_basis_trinomial:
			charpos = pack_int16(data, charpos, ecparams->k);
			break;
		case ec_basis_pentanomial:
			charpos = pack_int16(data, charpos, ecparams->k1);
			charpos = pack_int16(data, charpos, ecparams->k2);
			charpos = pack_int16(data, charpos, ecparams->k3);
			break;
		case ec_basis_polynomial:
			charpos = pack_octstr(data, charpos, ecparams->irreducible);
			break;
		}
		break;
	}
	/* pack the ECCurve */
	charpos = pack_octstr(data, charpos, ecparams->curve->a);
	charpos = pack_octstr(data, charpos, ecparams->curve->b);
	charpos = pack_octstr(data, charpos, ecparams->curve->seed);
	/* pack the ECPoint */
	charpos = pack_octstr(data, charpos, ecparams->base->point);
	/* order and cofactor */
	charpos = pack_octstr(data, charpos, ecparams->order);
	charpos = pack_octstr(data, charpos, ecparams->cofactor);

	return charpos;	
}

int pack_param_spec(Octstr *data, long charpos, ParameterSpecifier *pspec) {
        if (pspec == NULL)
        {
	octstr_append_char(data, 0);
	charpos += 1;
        return charpos;
        }        

        /* index */
	octstr_append_char(data, pspec->param_index);
	charpos += 1;
	/* ParameterSet struct */
	octstr_append_char(data, pspec->param_set->length);
	charpos += 1;
	switch (public_key_algo) {
	case diffie_hellman_pubkey:
		pack_dhparams(data, charpos, pspec->param_set->dhparams);
		break;
	case elliptic_curve_pubkey:
		pack_ecparams(data, charpos, pspec->param_set->ecparams);
		break;
	}
	return charpos;
}

int pack_public_key(Octstr *data, long charpos, PublicKey *key, PublicKeyType key_type) {
	switch (key_type) {
	case ecdh_key:
		charpos = pack_octstr(data, charpos, key->ecdh_pubkey->point);
		break;
	case ecdsa_key:
		charpos = pack_octstr(data, charpos, key->ecdsa_pubkey->point);
		break;
	case rsa_key:
		charpos = pack_rsa_pubkey(data, charpos, key->rsa_pubkey);
		break;
	}
	return charpos;
}

int pack_rsa_pubkey(Octstr *data, long charpos, RSAPublicKey *key) {
	charpos = pack_octstr16(data, charpos, key->rsa_exponent);
	charpos = pack_octstr16(data, charpos, key->rsa_modulus);
	return charpos;
}

int pack_ec_pubkey(Octstr *data, long charpos, ECPublicKey *key) {
	charpos = pack_octstr(data, charpos, key->point);
	return charpos;
}

int pack_dh_pubkey(Octstr *data, long charpos, DHPublicKey *key) {
	charpos = pack_octstr16(data, charpos, key->dh_Y);
	return charpos;
}

int pack_rsa_secret(Octstr *data, long charpos, RSASecret *secret) {
	octstr_append_char(data, secret->client_version);
	charpos += 1;
	charpos = pack_array(data, charpos, secret->random);
	return charpos;
}

int pack_rsa_encrypted_secret(Octstr *data, long charpos, RSAEncryptedSecret *secret) {
	charpos = pack_octstr16(data, charpos, secret->encrypted_secret);
	return charpos;
}

int pack_key_exchange_id(Octstr *data, long charpos, KeyExchangeId *keyexid) {
	octstr_set_char(data, charpos, keyexid->key_exchange_suite);
	charpos += 1;
	charpos = pack_param_spec(data, charpos, keyexid->param_specif);
	charpos = pack_identifier(data, charpos, keyexid->identifier);
	return charpos;
}

int pack_array(Octstr *data, long charpos, List *array) {
	int i;
	long pos = 0;
	Octstr *buffer;
	
	/* we need to know the length in bytes of the list
	   so we pack everything in a buffer for now. */
	buffer = octstr_create("");
	
	/* pack each entry in the buffer */
	for (i=0; i<gwlist_len(array); i++)
	{
		pos = pack_octstr(buffer, pos, (Octstr *) gwlist_get(array, i));
	}
	
	/* now we know the size of the list */
	charpos = pack_int16(data, charpos, pos);
	
	/* append the buffer */
	charpos = pack_octstr_fixed(data, charpos, buffer);

	return charpos;
}

int pack_key_list(Octstr *data, long charpos, List *key_list) {
	int i;
	long pos = 0;
	Octstr *buffer;
	KeyExchangeId *keyexid;
	
	/* we need to know the length in bytes of the list
	   so we pack everything in a buffer for now. */
	buffer = octstr_create("");
	
	/* pack the KeyExchangeIds */
	for (i=0; i<gwlist_len(key_list); i++) {
		keyexid = (KeyExchangeId *) gwlist_get(key_list, i);
		
		pos = pack_key_exchange_id(buffer, pos, keyexid);
	}
	
	/* now we know the size of the list */
	charpos = pack_int16(data, charpos, pos);
	
	/* append the buffer */
	charpos = pack_octstr_fixed(data, charpos, buffer);
	
	return charpos;
}

int pack_ciphersuite_list(Octstr *data, long charpos, List *ciphersuites) {
	int i;
	CipherSuite *cs;
	
	/* vector starts with its length 
	   Each element uses 2 bytes */
	octstr_set_char(data, charpos, gwlist_len(ciphersuites)*2);
	charpos += 1;
	
	/* pack the CipherSuites */
	for (i=0; i<gwlist_len(ciphersuites); i++) {
		cs = (CipherSuite *) gwlist_get(ciphersuites, i);
		octstr_set_char(data, charpos, cs->bulk_cipher_algo);
		charpos += 1;
		octstr_set_char(data, charpos, cs->mac_algo);
		charpos += 1;
	}
		
	return charpos;
}

int pack_compression_method_list(Octstr *data, long charpos, List *compmethod_list) {
	int i;
	
	/* vector starts with its length */
	octstr_set_char(data, charpos, gwlist_len(compmethod_list));
	charpos += 1;
	
	/* pack the CompressionMethods */
	for (i=0; i<gwlist_len(compmethod_list); i++) {
		octstr_set_char(data, charpos, 
				(CompressionMethod) gwlist_get(compmethod_list, i));
		charpos += 1;
	}
		
	return charpos;
}

int pack_identifier(Octstr *data, long charpos, Identifier *ident) {
	switch (ident->id_type) {
	case text:
		octstr_set_char(data, charpos, ident->charset);
		charpos += 1;
		charpos = pack_octstr(data, charpos, ident->name);
		break;
	case binary:
		charpos = pack_octstr(data, charpos, ident->identifier);
		break;
	case key_hash_sha:
		charpos = pack_octstr(data, charpos, ident->key_hash);
		break;
	case x509_name:
		charpos = pack_octstr(data, charpos, ident->distinguished_name);
		break;
	}
	return charpos;
}

int pack_signature(Octstr *data, long charpos, Signature *sig) {
	switch (signature_algo) {
	case ecdsa_sha:
	case rsa_sha:
		charpos = pack_array(data, charpos, sig->sha_hash);
		break;
	}
	return charpos;
}

int pack_wtls_certificate(Octstr *data, long charpos, WTLSCertificate *cert) {
	/* === pack ToBeSignedCertificate === */
	/* version */
	octstr_set_char(data, charpos, cert->tobesigned_cert->certificate_version);
	charpos += 1;
	/* sig algo */
	octstr_set_char(data, charpos, cert->tobesigned_cert->signature_algo);
	charpos += 1;
	/* identifier */
	octstr_set_char(data, charpos, cert->tobesigned_cert->issuer->id_type);
	charpos += 1;
	/* issuer Identifier */
	charpos = pack_identifier(data, charpos, cert->tobesigned_cert->issuer);
	/* validity periods */
	charpos = pack_int32(data, charpos, cert->tobesigned_cert->valid_not_before);
	charpos = pack_int32(data, charpos, cert->tobesigned_cert->valid_not_after);
	/* subject Identifier */
	charpos = pack_identifier(data, charpos, cert->tobesigned_cert->subject);
	/* public_key_type */
	octstr_set_char(data, charpos, cert->tobesigned_cert->pubkey_type);
	charpos += 1;
	/* parameter specifier */
	charpos = pack_param_spec(data, charpos, cert->tobesigned_cert->param_spec);
	/* public key */
	charpos = pack_public_key(data, charpos, cert->tobesigned_cert->pubkey,
					cert->tobesigned_cert->pubkey_type);

	/* === pack Signature === */
	charpos = pack_signature(data, charpos, cert->signature);
	return charpos;
}


/*****************************************************************
 * UNPACK functions
 */
 
int unpack_int16(Octstr *data, long *charpos) {
	int n;
	
	n =  octstr_get_char(data, *charpos) << 8;
	*charpos += 1;
	n += octstr_get_char(data, *charpos);
	*charpos += 1;
	return n;
}

long unpack_int32(Octstr *data, long *charpos) {
	int n;
	
	n =  octstr_get_char(data, *charpos);
	n = n << 8;
	*charpos += 1;
	n += octstr_get_char(data, *charpos);
	n = n << 8;
	*charpos += 1;
	n += octstr_get_char(data, *charpos);
	n = n << 8;
	*charpos += 1;
	n += octstr_get_char(data, *charpos);
	*charpos += 1;
	return n;
}

Octstr * unpack_octstr(Octstr *data, long *charpos) {
	int length;
	Octstr *opaque;
	
	length = octstr_get_char(data, *charpos);
	*charpos += 1;
	opaque = octstr_copy(data, *charpos, length);
	*charpos += length;
	return opaque;
}

Octstr * unpack_octstr16(Octstr *data, long *charpos) {
	long length;
	Octstr *opaque;
	
	length = unpack_int16(data, charpos);
	opaque = octstr_copy(data, *charpos, length);
	*charpos += length;
	return opaque;
}

Octstr * unpack_octstr_fixed(Octstr *data, long *charpos, long length) {
	Octstr *opaque;

	opaque = octstr_copy(data, *charpos, length);
	*charpos += length;
	return opaque;
}

Random * unpack_random(Octstr *data, long *charpos) {
	Random *random;
	/* create the Random structure */
	random = (Random *)gw_malloc(sizeof(Random));
	
	random->gmt_unix_time = unpack_int32(data, charpos);
	random->random_bytes = unpack_octstr_fixed(data, charpos, 12);
	return random;
}
	
DHParameters * unpack_dhparams(Octstr *data, long *charpos) {
	DHParameters *dhparams;
	
	/* create the DHParameters */
	dhparams = (DHParameters *)gw_malloc(sizeof(DHParameters));
	
	dhparams->dh_e = octstr_get_char(data, *charpos);
	*charpos += 1;
	dhparams->dh_p = unpack_octstr16(data, charpos);
	dhparams->dh_g = unpack_octstr16(data, charpos);
	return dhparams;
}

ECParameters * unpack_ecparams(Octstr *data, long *charpos) {
	ECParameters *ecparams;
	
	/* create the ECParameters */
	ecparams = (ECParameters *)gw_malloc(sizeof(ECParameters));
	
	/* field */
	ecparams->field = octstr_get_char(data, *charpos);
	*charpos += 1;
	switch (ecparams->field) {
	case ec_prime_p:
		ecparams->prime_p = unpack_octstr(data, charpos);
		break;
	case ec_characteristic_two:
		/* m (16 bits) */
		ecparams->m = unpack_int16(data, charpos);
		/* basis */
		ecparams->basis = octstr_get_char(data, *charpos);
		*charpos += 1;
		switch (ecparams->basis) {
		case ec_basis_onb:
			break;
		case ec_basis_trinomial:
			ecparams->k = unpack_int16(data, charpos);
			break;
		case ec_basis_pentanomial:
			ecparams->k1 = unpack_int16(data, charpos);
			ecparams->k2 = unpack_int16(data, charpos);
			ecparams->k3 = unpack_int16(data, charpos);
			break;
		case ec_basis_polynomial:
			ecparams->irreducible = unpack_octstr(data, charpos);
			break;
		}
		break;
	}
	/* pack the ECCurve */
	ecparams->curve->a = unpack_octstr(data, charpos);
	ecparams->curve->b = unpack_octstr(data, charpos);
	ecparams->curve->seed = unpack_octstr(data, charpos);
	/* pack the ECPoint */
	ecparams->base->point = unpack_octstr(data, charpos);
	/* order and cofactor */
	ecparams->order = unpack_octstr(data, charpos);
	ecparams->cofactor = unpack_octstr(data, charpos);

	return ecparams;	
}

ParameterSpecifier * unpack_param_spec(Octstr *data, long *charpos) {
	ParameterSpecifier *pspec;

	/* create the ParameterSpecifier */
	pspec = (ParameterSpecifier *)gw_malloc(sizeof(ParameterSpecifier));
	
	/* index */
	pspec->param_index = octstr_get_char(data, *charpos);
	*charpos += 1;
	/* ParameterSet struct */
	if(pspec->param_index == 255) {
		pspec->param_set = (ParameterSet *)gw_malloc(sizeof(ParameterSet));
		pspec->param_set->length = octstr_get_char(data, *charpos);
		*charpos += 1;
		switch (public_key_algo) {
		case diffie_hellman_pubkey:
			pspec->param_set->dhparams = unpack_dhparams(data, charpos);
			break;
		case elliptic_curve_pubkey:
			pspec->param_set->ecparams = unpack_ecparams(data, charpos);
			break;
		}
	}
	return pspec;
}

RSAPublicKey * unpack_rsa_pubkey(Octstr *data, long *charpos) {
	RSAPublicKey *key;
	
	/* create the RSAPublicKey */
	key = (RSAPublicKey *)gw_malloc(sizeof(RSAPublicKey));
	key->rsa_exponent = unpack_octstr16( data, charpos);
	key->rsa_modulus = unpack_octstr16( data, charpos);
	return key;
}

DHPublicKey * unpack_dh_pubkey(Octstr *data, long *charpos) {
	DHPublicKey *key;
	
	/* create the DHPublicKey */
	key = (DHPublicKey *)gw_malloc(sizeof(DHPublicKey));
	key->dh_Y = unpack_octstr16( data, charpos);
	return key;
}

ECPublicKey * unpack_ec_pubkey(Octstr *data, long *charpos) {
	ECPublicKey *key;
	
	/* create the ECPublicKey */
	key = (ECPublicKey *)gw_malloc(sizeof(ECPublicKey));
	key->point = unpack_octstr( data, charpos);
	return key;
}

RSASecret * unpack_rsa_secret(Octstr *data, long *charpos) {
	RSASecret *secret;
	
	/* create the RSASecret */
	secret = (RSASecret *)gw_malloc(sizeof(RSASecret));
	secret->client_version = octstr_get_char(data, *charpos);
	*charpos += 1;
	secret->random = unpack_array(data, charpos);
	
	return secret;
}

RSAEncryptedSecret * unpack_rsa_encrypted_secret(Octstr *data, long *charpos) {
	RSAEncryptedSecret *secret;
	
	/* create the RSASecret */
	secret = (RSAEncryptedSecret *)gw_malloc(sizeof(RSAEncryptedSecret));
	//secret->encrypted_secret = unpack_octstr16(data, charpos);
	secret->encrypted_secret = unpack_octstr_fixed(data, charpos, octstr_len(data) - *charpos);
	return secret;
}

PublicKey * unpack_public_key(Octstr *data, long *charpos, PublicKeyType key_type) {
	PublicKey *key;
	
	/* create the PublicKey */
	key = (PublicKey *)gw_malloc(sizeof(PublicKey));
	switch (key_type) {
	case ecdh_key:
		key->ecdh_pubkey = unpack_ec_pubkey(data, charpos);
		break;
	case ecdsa_key:
		key->ecdsa_pubkey = unpack_ec_pubkey(data, charpos);
		break;
	case rsa_key:
		key->rsa_pubkey = unpack_rsa_pubkey(data, charpos);
		break;
	}
	return key;
}

KeyExchangeId * unpack_key_exchange_id(Octstr *data, long *charpos) {
	KeyExchangeId *keyexid;
	
	/* create the KeyExchangeID */
	keyexid = (KeyExchangeId *)gw_malloc(sizeof(KeyExchangeId));
	
	keyexid->key_exchange_suite = octstr_get_char(data, *charpos);
	*charpos += 1;
	keyexid->param_specif = unpack_param_spec(data, charpos);
	keyexid->identifier = unpack_identifier(data, charpos);
	return keyexid;
}

List * unpack_array(Octstr *data, long *charpos) {
	int i;
	int array_length;
	List *array;
	
	/* create the list */
	array = gwlist_create();
	
	/* get the size of the array */
	array_length = octstr_get_char(data, *charpos);
	*charpos += 1;
	
	/* store each entry in the list */
	for (i=0; i<array_length; i++) 	{
		gwlist_append(array, (void *)unpack_octstr(data, charpos));
	}
	
	return array;
}

List * unpack_key_list(Octstr *data, long *charpos) {
	KeyExchangeId *keyexid;
	List *key_list;
	int gwlist_length;
	long endpos;
	
	/* create the list */
	key_list = gwlist_create();
	
	/* get the size of the array */
	gwlist_length = unpack_int16(data, charpos);
	endpos = *charpos + gwlist_length;
	
	/* unpack the KeyExchangeIds */
	while (*charpos < endpos)
	{
		keyexid = unpack_key_exchange_id(data, charpos);
		gwlist_append(key_list, (void *)keyexid);
	}
	return key_list;
}

List * unpack_ciphersuite_list(Octstr *data, long *charpos)
{
	List *ciphersuites;
	int gwlist_length;
	int i;
	CipherSuite *cs;
	
	/* create the list */
	ciphersuites = gwlist_create();
	
	/* get the size of the array (in bytes, not elements)*/
	gwlist_length = octstr_get_char(data, *charpos);
	*charpos += 1;
	
	/* unpack the CipherSuites */
	for (i=0; i<gwlist_length; i+=2)
	{
		cs = (CipherSuite *)gw_malloc(sizeof(CipherSuite));
		cs->bulk_cipher_algo = octstr_get_char(data, *charpos);
		*charpos += 1;
		cs->mac_algo = octstr_get_char(data, *charpos);
		*charpos += 1;
		gwlist_append(ciphersuites, (void *)cs);
	}
		
	return ciphersuites;
}

List * unpack_compression_method_list(Octstr *data, long *charpos) {
	List *compmethod_list;
	int gwlist_length;
	int i;
	CompressionMethod *cm;
	
	/* create the list */
	compmethod_list = gwlist_create();
	
	/* get the size of the array */
	gwlist_length = octstr_get_char(data, *charpos);
	*charpos += 1;
	
	/* unpack the CompressionMethods */
	for (i=0; i<gwlist_length; i++)
	{
		cm = gw_malloc(sizeof(CompressionMethod));
		*cm = octstr_get_char(data, *charpos);
		gwlist_append(compmethod_list, (void *)cm);
	}
		
	return compmethod_list;
}

Identifier * unpack_identifier(Octstr *data, long *charpos) {
	Identifier *ident;
	
	/* create Identifier */
	ident = (Identifier *)gw_malloc(sizeof(Identifier));
	
	ident->id_type = octstr_get_char(data, *charpos);
	*charpos += 1;
	switch (ident->id_type) {
	case text:
		ident->charset = octstr_get_char(data, *charpos);
		*charpos += 1;
		ident->name = unpack_octstr(data, charpos);
		break;
	case binary:
		ident->identifier = unpack_octstr(data, charpos);
		break;
	case key_hash_sha:
		ident->key_hash = unpack_octstr(data, charpos);
		break;
	case x509_name:
		ident->distinguished_name = unpack_octstr(data, charpos);
		break;
	}
	return ident;
}


Signature * unpack_signature(Octstr *data, long *charpos) {
	Signature *sig;
	
	/* create Signature */
	sig = (Signature *)gw_malloc(sizeof(Signature));
	
	switch (signature_algo) {
	case ecdsa_sha:
	case rsa_sha:
		sig->sha_hash = unpack_array(data, charpos);
		break;
	}
	return sig;
}

WTLSCertificate * unpack_wtls_certificate(Octstr *data, long *charpos) {
	WTLSCertificate *cert;

	/* create the Certificate */
	cert = (WTLSCertificate *)gw_malloc(sizeof(WTLSCertificate));
	
	/* === unpack ToBeSignedCertificate === */
	cert->tobesigned_cert = (ToBeSignedCertificate *)gw_malloc(sizeof(ToBeSignedCertificate));
	/* version */
	cert->tobesigned_cert->certificate_version = octstr_get_char(data, *charpos);
	*charpos += 1;
	/* sig algo */
	cert->tobesigned_cert->signature_algo = octstr_get_char(data, *charpos);
	*charpos += 1;
	/* identifier */
	cert->tobesigned_cert->issuer->id_type = octstr_get_char(data, *charpos);
	*charpos += 1;
	/* issuer Identifier */
	cert->tobesigned_cert->issuer = unpack_identifier(data, charpos);
	/* validity periods */
	cert->tobesigned_cert->valid_not_before = unpack_int32(data, charpos);
	cert->tobesigned_cert->valid_not_after = unpack_int32(data, charpos);
	/* subject Identifier */
	cert->tobesigned_cert->subject = unpack_identifier(data, charpos);
	/* public_key_type */
	cert->tobesigned_cert->pubkey_type = octstr_get_char(data, *charpos);
	*charpos += 1;
	/* parameter specifier */
	cert->tobesigned_cert->param_spec = unpack_param_spec(data, charpos);
	/* public key */
	cert->tobesigned_cert->pubkey = unpack_public_key(data, charpos,
				cert->tobesigned_cert->pubkey_type);

	/* === pack Signature === */
	cert->signature = unpack_signature(data, charpos);
	return cert;
}


/*****************************************************************
 * DESTROY functions
 */
 

void destroy_octstr(Octstr *data) {
	octstr_destroy(data);
}

void destroy_octstr16(Octstr *data) {
	octstr_destroy(data);
}

void destroy_octstr_fixed(Octstr *data) {
	octstr_destroy(data);
}

void destroy_random(Random *random) {
	octstr_destroy(random->random_bytes);
	gw_free(random);
}

void destroy_dhparams(DHParameters *dhparams) {
	destroy_octstr16(dhparams->dh_p);
	destroy_octstr16(dhparams->dh_g);
	gw_free(dhparams);
}

void destroy_ecparams(ECParameters *ecparams) {
	/* field */
	switch (ecparams->field) {
	case ec_prime_p:
		octstr_destroy(ecparams->prime_p);
		break;
	case ec_characteristic_two:
		switch (ecparams->basis) {
		case ec_basis_onb:
			break;
		case ec_basis_trinomial:
			break;
		case ec_basis_pentanomial:
			break;
		case ec_basis_polynomial:
			octstr_destroy(ecparams->irreducible);
			break;
		}
		break;
	}
	/* pack the ECCurve */
	octstr_destroy(ecparams->curve->a);
	octstr_destroy(ecparams->curve->b);
	octstr_destroy(ecparams->curve->seed);
	/* pack the ECPoint */
	octstr_destroy(ecparams->base->point);
	/* order and cofactor */
	octstr_destroy(ecparams->order);
	octstr_destroy(ecparams->cofactor);

	gw_free(ecparams);	
}

void destroy_param_spec(ParameterSpecifier *pspec) {
 	switch (public_key_algo) {
	case diffie_hellman_pubkey:
		destroy_dhparams(pspec->param_set->dhparams);
		break;
	case elliptic_curve_pubkey:
		destroy_ecparams(pspec->param_set->ecparams);
		break;
	}
	gw_free(pspec);
}

void destroy_public_key(PublicKey *key) {
	if(key->ecdh_pubkey)
	{
		octstr_destroy(key->ecdh_pubkey->point);
		gw_free(key->ecdh_pubkey);
	}
	if(key->ecdsa_pubkey)
	{
		octstr_destroy(key->ecdsa_pubkey->point);
		gw_free(key->ecdsa_pubkey);
	}
	if(key->rsa_pubkey)
	{
		destroy_rsa_pubkey(key->rsa_pubkey);
	}
	gw_free(key);
}

void destroy_rsa_pubkey(RSAPublicKey *key) {
	octstr_destroy(key->rsa_exponent);
	octstr_destroy(key->rsa_modulus);
	gw_free(key);
}

void destroy_ec_pubkey(ECPublicKey *key) {
	octstr_destroy(key->point);
	gw_free(key);
}

void destroy_dh_pubkey(DHPublicKey *key) {
	octstr_destroy(key->dh_Y);
	gw_free(key);
}

void destroy_rsa_secret(RSASecret *secret) {
	destroy_array(secret->random);
	gw_free(secret);
}

void destroy_rsa_encrypted_secret(RSAEncryptedSecret *secret) {
	octstr_destroy(secret->encrypted_secret);
	gw_free(secret);
}

void destroy_key_exchange_id(KeyExchangeId *keyexid) {
	destroy_param_spec(keyexid->param_specif);
	destroy_identifier(keyexid->identifier);
	gw_free(keyexid);
}

void destroy_array(List *array) {
	int i;
	
	/* pack each entry in the array */
	for (i=0; i<gwlist_len(array); i++)
	{
		octstr_destroy((Octstr *) gwlist_get(array, i));
	}
	
	gwlist_destroy(array, NULL);
}

void destroy_key_list(List *key_list) {
	int i;
	/* destroy the KeyExchangeIds */
	for (i=0; i<gwlist_len(key_list); i++) {
		destroy_key_exchange_id((KeyExchangeId *) gwlist_get(key_list, i));
	}
	gwlist_destroy(key_list, NULL);
}

void destroy_ciphersuite_list(List *ciphersuites) {
	int i;
	CipherSuite *cs;
	
	/* destroy the CipherSuites */
	for (i=0; i<gwlist_len(ciphersuites); i++) {
		gw_free( (CipherSuite *) gwlist_get(ciphersuites, i) );
	}
		
	gwlist_destroy(ciphersuites, NULL);
}

void destroy_compression_method_list(List *compmethod_list) {
	int i;
	CompressionMethod *cm;
	
	/* destroy the CompressionMethods */
	for (i=0; i<gwlist_len(compmethod_list); i++) {
		cm = (CompressionMethod*) gwlist_get(compmethod_list, i);
		gw_free(cm);
	}
		
	gw_free(compmethod_list);
}

void destroy_identifier(Identifier *ident) {
	switch (ident->id_type) {
	case text:
		octstr_destroy(ident->name);
		break;
	case binary:
		octstr_destroy(ident->identifier);
		break;
	case key_hash_sha:
		octstr_destroy(ident->key_hash);
		break;
	case x509_name:
		octstr_destroy(ident->distinguished_name);
		break;
	}
	gw_free(ident);
}

void destroy_signature(Signature *sig) {
	switch (signature_algo) {
	case ecdsa_sha:
	case rsa_sha:
		destroy_array(sig->sha_hash);
		break;
	}
	gw_free(sig);
}

void destroy_wtls_certificate(WTLSCertificate *cert) {
	/* === destroy ToBeSignedCertificate === */
	/* issuer Identifier */
	destroy_identifier(cert->tobesigned_cert->issuer);
	/* subject Identifier */
	destroy_identifier(cert->tobesigned_cert->subject);
	/* parameter specifier */
	destroy_param_spec(cert->tobesigned_cert->param_spec);
	/* public key */
	destroy_public_key(cert->tobesigned_cert->pubkey);

	/* === destroy Signature === */
	destroy_signature(cert->signature);
	gw_free(cert);
}


/*****************************************************************
 * DUMP functions
 */
 
void dump_void16(unsigned char *dbg, int level, int i) {
	debug(dbg, 0, "%*s16 bit Int: %p", level, "", i);
}

void dump_int32(unsigned char *dbg, int level, long i) {
	debug(dbg, 0, "%*s32 bit Int: %p", level, "", i);
}

void dump_octstr(unsigned char *dbg, int level, Octstr *opaque) {
	octstr_dump(opaque, 0);
}

void dump_octstr16(unsigned char *dbg, int level, Octstr *opaque) {
	octstr_dump(opaque, 0);
}

void dump_octstr_fixed(unsigned char *dbg, int level, Octstr *opaque) {
	octstr_dump(opaque, 0);
}

void dump_random(unsigned char *dbg, int level, Random *random) {
	debug(dbg, 0, "%*sRandom :", level, "");
	debug(dbg, 0, "%*sGMT Unix Time: %p", level+1, "", random->gmt_unix_time);
	debug(dbg, 0, "%*sRandom Bytes:", level+1, "");
	dump_octstr_fixed(dbg, level+2, random->random_bytes);
}

void dump_dhparams(unsigned char *dbg, int level, DHParameters *dhparams) {
	debug(dbg, 0, "%*sDH Parameters :", level, "");
	debug(dbg, 0, "%*sdh_e: %p", level+1, "", dhparams->dh_e);
	debug(dbg, 0, "%*sdh_p:", level+1, "");
	dump_octstr16(dbg, level+2, dhparams->dh_p);
	debug(dbg, 0, "%*sdh_g:", level+1, "");
	dump_octstr16(dbg, level+2, dhparams->dh_g);
}

void dump_ecparams(unsigned char *dbg, int level, ECParameters *ecparams) {
	debug(dbg, 0, "%*sEC Parameters :", level, "");
	/* field */
	debug(dbg, 0, "%*sField: %p", level+1, "", ecparams->field);
	switch (ecparams->field) {
	case ec_prime_p:
		debug(dbg, 0, "%*sprime_p :", level+1, "");
		dump_octstr(dbg, level+1, ecparams->prime_p);
		break;
	case ec_characteristic_two:
		/* m (16 bits) */
		debug(dbg, 0, "%*sM: %p", level+1, "", ecparams->m);
		/* basis */
		debug(dbg, 0, "%*sBasis: %p", level+1, "", ecparams->basis);
		switch (ecparams->basis) {
		case ec_basis_onb:
			break;
		case ec_basis_trinomial:
			debug(dbg, 0, "%*sK: %p", level+1, "", ecparams->k);
			break;
		case ec_basis_pentanomial:
			debug(dbg, 0, "%*sk1: %p", level+1, "", ecparams->k1);
			debug(dbg, 0, "%*sk2: %p", level+1, "", ecparams->k2);
			debug(dbg, 0, "%*sk3: %p", level+1, "", ecparams->k3);
			break;
		case ec_basis_polynomial:
			debug(dbg, 0, "%*sirreducible: %p", level+1, "");
			dump_octstr(dbg, level+1, ecparams->irreducible);
			break;
		}
		break;
	}
	/* pack the ECCurve */
	debug(dbg, 0, "%*sEC Curve: %p", level+1, "");
	debug(dbg, 0, "%*sa: %p", level+2, "");
	dump_octstr(dbg, level+2, ecparams->curve->a);
	debug(dbg, 0, "%*sb: %p", level+2, "");
	dump_octstr(dbg, level+2, ecparams->curve->b);
	debug(dbg, 0, "%*sseed: %p", level+2, "");
	dump_octstr(dbg, level+2, ecparams->curve->seed);
	/* pack the ECPoint */
	debug(dbg, 0, "%*spoint: %p", level+2, "");
	dump_octstr(dbg, level+2, ecparams->base->point);
	/* order and cofactor */
	debug(dbg, 0, "%*sorder: %p", level+2, "");
	dump_octstr(dbg, level+2, ecparams->order);
	debug(dbg, 0, "%*scofactor: %p", level+2, "");
	dump_octstr(dbg, level+2, ecparams->cofactor);
}

void dump_param_spec(unsigned char *dbg, int level, ParameterSpecifier *pspec) {
	debug(dbg, 0, "%*sParameterSpecifier:", level, "");
	/* index */
	debug(dbg, 0, "%*sParameter Index: %d", level+1, "", pspec->param_index);
	/* ParameterSet struct */
	if(pspec->param_index == 255) {
		debug(dbg, 0, "%*sLength: %p", level+1, "", pspec->param_set->length);
		switch (public_key_algo) {
		case diffie_hellman_pubkey:
			dump_dhparams(dbg, level+1, pspec->param_set->dhparams);
			break;
		case elliptic_curve_pubkey:
			dump_ecparams(dbg, level+1, pspec->param_set->ecparams);
			break;
		}
	}
}

void dump_public_key(unsigned char *dbg, int level, PublicKey *key, PublicKeyType key_type) {
	switch (key_type) {
	case ecdh_key:
		debug(dbg, 0, "%*sPublicKey: %p", level, "");
		debug(dbg, 0, "%*sECDH Point: %p", level+1, "");
		dump_octstr(dbg, level+1, key->ecdh_pubkey->point);
		break;
	case ecdsa_key:
		debug(dbg, 0, "%*sECDSA Point: %p", level+1, "");
		dump_octstr(dbg, level+1, key->ecdsa_pubkey->point);
		break;
	case rsa_key:
		dump_rsa_pubkey(dbg, level+1, key->rsa_pubkey);
		break;
	}
}

void dump_rsa_pubkey(unsigned char *dbg, int level, RSAPublicKey *key) {
	debug(dbg, 0, "%*sRSA Public Key: %p", level, "");
	debug(dbg, 0, "%*sRSA Exponent: %p", level+1, "");
	dump_octstr(dbg, level+2, key->rsa_exponent);
	debug(dbg, 0, "%*sRSA Modulus: %p", level+1, "");
	dump_octstr(dbg, level+2, key->rsa_modulus);
}

void dump_ec_pubkey(unsigned char *dbg, int level, ECPublicKey *key) {
	debug(dbg, 0, "%*sEC Public Key: %p", level, "");
	debug(dbg, 0, "%*sPoint: %p", level+1, "");
	dump_octstr(dbg, level+2, key->point);
}

void dump_dh_pubkey(unsigned char *dbg, int level, DHPublicKey *key) {
	debug(dbg, 0, "%*sDH Public Key: %p", level, "");
	dump_octstr(dbg, level+2, key->dh_Y);
}

void dump_rsa_secret(unsigned char *dbg, int level, RSASecret *secret) {
	debug(dbg, 0, "%*sRSA Secret: %p", level, "");
	debug(dbg, 0, "%*sClient Version: %p", level+1, "", secret->client_version);
	debug(dbg, 0, "%*sRandom: %p", level, "");
	dump_array(dbg, level+2, secret->random);
}

void dump_rsa_encrypted_secret(unsigned char *dbg, int level, RSAEncryptedSecret *secret) {
	debug(dbg, 0, "%*sRSA Encrypted Secret: %p", level, "");
	dump_octstr(dbg, level+1, secret->encrypted_secret);
}

void dump_key_exchange_id(unsigned char *dbg, int level, KeyExchangeId *keyexid) {
	debug(dbg, 0, "%*sKey Exchange Id:", level, "");
	debug(dbg, 0, "%*sKey Exch Suite: %d", level+1, "", keyexid->key_exchange_suite);
	dump_param_spec(dbg, level+1, keyexid->param_specif);
	dump_identifier(dbg, level+1, keyexid->identifier);
}

void dump_array(unsigned char *dbg, int level, List *array) {
	int i;

	/*debug(dbg, 0, "%*sOctstr Array: %p", level, "");*/
	
	/* dump each entry in the array */
	for (i=0; i<gwlist_len(array); i++)
	{
		debug(dbg, 0, "%*sElement %d", level, "", i);
		dump_octstr(dbg, level+1, (Octstr *) gwlist_get(array, i));
	}
}

void dump_key_list(unsigned char *dbg, int level, List *key_list) {
	int i;
	long pos = 0;
	Octstr *buffer;
	KeyExchangeId *keyexid;

	debug(dbg, 0, "%*sKey List: %p", level, "");
	
	/* pack the KeyExchangeIds */
	for (i=0; i<gwlist_len(key_list); i++) {
		keyexid = (KeyExchangeId *) gwlist_get(key_list, i);
		
		dump_key_exchange_id(dbg, level+1, keyexid);
	}
}

void dump_ciphersuite_list(unsigned char *dbg, int level, List *ciphersuites) {
	int i;
	CipherSuite *cs;

	debug(dbg, 0, "%*sCipherSuite List: %p", level, "");
	
	/* dump the CipherSuites */
	for (i=0; i<gwlist_len(ciphersuites); i++) {
		cs = (CipherSuite *) gwlist_get(ciphersuites, i);
		debug(dbg, 0, "%*sBulk Cipher Algo: %p", level, "", cs->bulk_cipher_algo);
		debug(dbg, 0, "%*sMAC Algo: %p", level, "", cs->mac_algo);
	}
}

void dump_compression_method_list(unsigned char *dbg, int level, List *compmethod_list) {
	int i;
	
	debug(dbg, 0, "%*sCompression Method List: %p", level, "");
	/* pack the CompressionMethods */
	for (i=0; i<gwlist_len(compmethod_list); i++) {
		debug(dbg, 0, "%*sMethod %d: %p", level, "", i, 
				(CompressionMethod) gwlist_get(compmethod_list, i));
	}
}

void dump_identifier(unsigned char *dbg, int level, Identifier *ident) {
	debug(dbg, 0, "%*sIdentifier:", level, "");
	debug(dbg, 0, "%*sIdent type: %d", level+1, "", ident->id_type);
	switch (ident->id_type) {
	case text:
		debug(dbg, 0, "%*sCharset: %p", level+1, "", ident->charset);
		debug(dbg, 0, "%*sNamet: %p", level+1, "", ident->name);
		break;
	case binary:
		debug(dbg, 0, "%*sIdentifier: %p", level+1, "");
		dump_octstr(dbg, level+2, ident->identifier);
		break;
	case key_hash_sha:
		debug(dbg, 0, "%*sKey Hash: %p", level+1, "");
		dump_octstr(dbg, level+2, ident->key_hash);
		break;
	case x509_name:
		debug(dbg, 0, "%*sDistinguished Name: %p", level+1, "");
		dump_octstr(dbg, level+2, ident->distinguished_name);
		break;
	}
}

void dump_signature(unsigned char *dbg, int level, Signature *sig) {
	debug(dbg, 0, "%*sSignature: %p", level, "");
	switch (signature_algo) {
	case ecdsa_sha:
	case rsa_sha:
		dump_array(dbg, level+1, sig->sha_hash);
		break;
	}
}

void dump_wtls_certificate(unsigned char *dbg, int level, WTLSCertificate *cert) {
	debug(dbg, 0, "%*sWTLS Certificate: %p", level, "");
	/* === pack ToBeSignedCertificate === */
	/* version */
	debug(dbg, 0, "%*sCertificate Version: %p", level+1, "", cert->tobesigned_cert->certificate_version);
	/* sig algo */
	debug(dbg, 0, "%*sSignature Algo: %p", level+1, "", cert->tobesigned_cert->signature_algo);
	/* identifier */
	debug(dbg, 0, "%*sID Type: %p", level+1, "", cert->tobesigned_cert->issuer->id_type);
	/* issuer Identifier */
	dump_identifier(dbg, level+1, cert->tobesigned_cert->issuer);
	/* validity periods */
	debug(dbg, 0, "%*sValid not Before: %p", level+1, "", cert->tobesigned_cert->valid_not_before);
	debug(dbg, 0, "%*sValid not After: %p", level+1, "", cert->tobesigned_cert->valid_not_after);
	/* subject Identifier */
	dump_identifier(dbg, level+1, cert->tobesigned_cert->subject);
	/* public_key_type */
	debug(dbg, 0, "%*sPublic Key Type: %p", level+1, "", cert->tobesigned_cert->pubkey_type);
	/* parameter specifier */
	dump_param_spec(dbg, level+1, cert->tobesigned_cert->param_spec);
	/* public key */
	dump_public_key(dbg, level+1, cert->tobesigned_cert->pubkey,
					cert->tobesigned_cert->pubkey_type);

	/* === pack Signature === */
	dump_signature(dbg, level+1, cert->signature);
}


#endif
