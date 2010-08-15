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

/* wtls_pdu.h - definitions for unpacked WTLS protocol data units
 *
 */

#ifndef PDU_H
#define PDU_H

#include "gwlib/list.h"
#include "gwlib/octstr.h"
#include "wtls.h"

typedef enum wtls_pdu_modes {
	ChangeCipher_PDU = 1,
	Alert_PDU,
	Handshake_PDU,
	Application_PDU
} wtls_pdu_modes;

typedef enum handshake_type{
	hello_request = 0,
	client_hello = 1,
	server_hello = 2,
	certificate = 11,
	server_key_exchange = 12,
	certificate_request = 13,
	server_hello_done = 14,
	certificate_verify = 15,
	client_key_exchange = 16,
	finished = 20
} HandshakeType;

typedef enum compmethod {
	null_comp = 0
} CompressionMethod;

typedef enum certificateformat {
	WTLSCert = 1,
	X509Cert,
	X968Cert
} CertificateFormat;

typedef enum sig_algo {
	anonymous,
	ecdsa_sha,
	rsa_sha,
} SignatureAlgorithm;

/*typedef enum keyex_algo {
	rsa,
	rsa_anon,
	dh_anon,
	ecdh_anon,
	ecdh_ecdsa,
} KeyExchangeAlgorithm;*/

typedef enum keyex_suite {
	null_k,
	shared_secret,
	dh_anon,
	dh_anon_512,
	dh_anon_768,
	rsa_anon,
	rsa_anon_512,
	rsa_anon_768,
	rsa,
	rsa_512,
	rsa_768,
	ecdh_anon,
	ecdh_anon_113,
	ecdh_anon_131,
	ecdh_ecdsa,
} KeyExchangeSuite;

typedef enum pubkey_algo {
	rsa_pubkey,
	diffie_hellman_pubkey,
	elliptic_curve_pubkey,
} PublicKeyAlgorithm;

typedef enum identifier_type {
	null = 0,
	text,
	binary,
	key_hash_sha = 254,
	x509_name = 255
} IdentifierType;

typedef enum public_key_type {
	rsa_key = 2,
	ecdh_key = 3,
	ecdsa_key = 4
} PublicKeyType;

typedef enum ecbasistype {
	ec_basis_onb = 1,
	ec_basis_trinomial,
	ec_basis_pentanomial,
	ec_basis_polynomial
} ECBasisType;

typedef enum ecfield {
	ec_prime_p,
	ec_characteristic_two
} ECField;

typedef struct random {
	long gmt_unix_time;
	Octstr *random_bytes;
} Random;

typedef struct ecpoint {
	Octstr *point;
} ECPoint;

typedef ECPoint ECPublicKey;

typedef struct dhpublickey {
	Octstr *dh_Y;
} DHPublicKey;

typedef struct rsa_public_key {
	Octstr *rsa_exponent;
	Octstr *rsa_modulus;
} RSAPublicKey;

typedef struct public_key {
	/* ecdh */
	ECPublicKey *ecdh_pubkey;
	/* ecdsa */
	ECPublicKey *ecdsa_pubkey;
	/* rsa */
	RSAPublicKey *rsa_pubkey;
} PublicKey;

typedef struct identifier {
	IdentifierType id_type;
	/* text */
	int charset;
	Octstr *name;
	/* binary */
	Octstr *identifier;
	/* key_hash_sha */
	Octstr *key_hash;
	/* x509 */
	Octstr *distinguished_name;
} Identifier;

typedef struct eccurve {
	Octstr *a;
	Octstr *b;
	Octstr *seed;
} ECCurve;

typedef struct dh_parameters{
	int dh_e;
	Octstr *dh_p;
	Octstr *dh_g;
} DHParameters;

typedef struct ec_parameters{
	ECField field;
	/* case ec_prime_p */
	Octstr *prime_p;
	/* case ec_characteristic_two */
	int m;
	ECBasisType basis;
		/* case ec_basis_onb : nothing*/
		/* case ec_trinomial */
		int k;
		/* case ec_pentanomial */
		int k1;
		int k2;
		int k3;
		/* case ec_basis_polynomial */
		Octstr *irreducible;
	ECCurve *curve;
	ECPoint *base;
	Octstr *order;
	Octstr *cofactor;
} ECParameters;

typedef struct parameter_set {
	long length;
	/* rsa: empty */
	/* diffie-hellman */
	DHParameters *dhparams;
	/* eliptic curve */
	ECParameters *ecparams;
} ParameterSet;

typedef struct parameter_specifier {
	int param_index;
	ParameterSet *param_set;
} ParameterSpecifier;

typedef struct key_exchange_id {
	int key_exchange_suite;
	ParameterSpecifier *param_specif;
	Identifier *identifier;
} KeyExchangeId;

typedef struct signature {
	/* case anonymous */
	/* nothing */
	/* case ecdsa_sha and rsa_sha */
	List *sha_hash;
} Signature;

typedef struct to_be_signed_cert {
	int certificate_version;
	SignatureAlgorithm signature_algo;
	Identifier *issuer;
	long valid_not_before;
	long valid_not_after;	
	Identifier *subject;
	PublicKeyType pubkey_type;
	ParameterSpecifier *param_spec;
	PublicKey *pubkey;
} ToBeSignedCertificate;	

typedef struct wtls_cert {
	ToBeSignedCertificate *tobesigned_cert;
	Signature *signature;
} WTLSCertificate;

typedef struct rsa_secret{
	int client_version;
	List *random;
} RSASecret;

typedef struct rsa_encrypted_secret {
	Octstr *encrypted_secret;
} RSAEncryptedSecret;

typedef struct cipher_suite {
	int bulk_cipher_algo;
	int mac_algo;
} CipherSuite;

typedef struct cert_request {
	List *trusted_authorities; // List of KeyExchangeIds
} CertificateRequest;

typedef struct cert_verify {
	Signature *signature;
} CertificateVerify;

typedef struct hello_request
{
	int dummy; /* nothing here */
} HelloRequest;

typedef struct client_hello
{
	int clientversion;
	Random *random;
	Octstr *session_id;
	List *client_key_ids;
	List *trusted_key_ids;
	List *ciphersuites; // list of CipherSuites
	List *comp_methods;
	int snmode;
	int krefresh;
} ClientHello;


typedef struct server_hello
{
	int serverversion;
	Random *random;
	Octstr *session_id;
	int client_key_id;
	CipherSuite *ciphersuite;
	CompressionMethod comp_method;
	int snmode;
	int krefresh;
} ServerHello;

typedef struct certificate {
	CertificateFormat certificateformat;
	/* case WTLS */
	WTLSCertificate *wtls_certificate;
	/* case X509 */
	Octstr *x509_certificate;
	/* X968 */
	Octstr *x968_certificate;
} Certificate;

typedef struct server_key_exchange
{
	ParameterSpecifier *param_spec;
	/* case rsa_anon */
	RSAPublicKey *rsa_params;
	/* case dh_anon */
	DHPublicKey *dh_params;
	/* case ecdh_anon */
	ECPublicKey *ecdh_params;
} ServerKeyExchange;

typedef struct client_key_exchange
{
	/* case rsa and rsa_anon*/
	RSAEncryptedSecret *rsa_params;
	/* case dh_anon */
	DHPublicKey *dh_anon_params;
	/* case ecdh_anon and ecdh_ecdsa*/
	ECPublicKey *ecdh_params;
} ClientKeyExchange;

typedef struct finished {
	Octstr *verify_data;
} Finished;

typedef struct server_hello_done
{
	int dummy; /* nothing here */
} ServerHelloDone;
		
typedef struct cc
{
	int change;
} ChangeCipher;

typedef struct alert
{
	int level;
	int desc;
	Octstr *chksum;
} Alert;

typedef struct handshake
{
	HandshakeType msg_type;
	int length;
	/* case hello_request */

	/* case client_hello */
	ClientHello *client_hello;
	/* case server_hello */
	ServerHello *server_hello;
	/* case certificate */
	Certificate *certificate;
	/* case server_key_exchange */
	ServerKeyExchange *server_key_exchange;
	/* case certificate_request */
	CertificateRequest *certificate_request;
	/* case server_hello_done */
	ServerHelloDone *server_hello_done;
	/* case certificate_verify */
	CertificateVerify *cert_verify;
	/* case client_key_exchange */
	ClientKeyExchange *client_key_exchange;
	/* case finished */
	Finished *finished;
} Handshake;

typedef struct application
{
	Octstr *data;
} Application;

typedef struct wtls_pdu {
	int type;
	int reserved;
	int cipher;
	int seqnum;
	int rlen;
        
	union {
		ChangeCipher cc;
		Alert alert;
		Handshake handshake;
		Application application;
	} u;
} wtls_PDU;

typedef struct wtls_payload {
	int type;
	int reserved;
	int cipher;
	int seqnum;
	int rlen;

	Octstr *data;
} wtls_Payload;

/* Prototypes */
wtls_PDU *wtls_pdu_create(int type);
void wtls_pdu_destroy(wtls_PDU *msg);
void wtls_pdu_dump(wtls_PDU *msg, int level);
wtls_PDU *wtls_pdu_unpack(wtls_Payload *payload, WTLSMachine* wtls_machine);
wtls_Payload *wtls_pdu_pack(wtls_PDU *pdu, WTLSMachine* wtls_machine);

wtls_Payload *wtls_payload_unpack(Octstr *data);
Octstr *wtls_payload_pack(wtls_Payload *payload);
void wtls_payload_destroy(wtls_Payload *payload);

List* wtls_unpack_payloadlist (Octstr *data);
Octstr* wtls_pack_payloadlist (List* payloadlist);


#endif
