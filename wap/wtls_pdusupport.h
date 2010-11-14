/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2008 Kannel Group  
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
 *
 * Nikos Balkanas, Inaccess Networks (2009)
 */ 

#ifndef PDUSUPPORT_H
#define PDUSUPPORT_H

int pack_int16(Octstr *data, long charpos, int i);
int pack_int32(Octstr *data, long charpos, long i);
int pack_octstr(Octstr *data, long charpos, Octstr *opaque);
int pack_octstr16(Octstr *data, long charpos, Octstr *opaque);
int pack_octstr_fixed(Octstr *data, long charpos, Octstr *opaque);
int pack_random(Octstr *data, long charpos, Random *random);
int pack_dhparams(Octstr *data, long charpos, DHParameters *dhparams);
int pack_ecparams(Octstr *data, long charpos, ECParameters *ecparams);
int pack_param_spec(Octstr *data, long charpos, ParameterSpecifier *pspec);
int pack_public_key(Octstr *data, long charpos, PublicKey *key, PublicKeyType key_type);
int pack_rsa_pubkey(Octstr *data, long charpos, RSAPublicKey *key);
int pack_dh_pubkey(Octstr *data, long charpos, DHPublicKey *key);
int pack_ec_pubkey(Octstr *data, long charpos, ECPublicKey *key);
int pack_rsa_secret(Octstr *data, long charpos, RSASecret *secret);
int pack_rsa_encrypted_secret(Octstr *data, long charpos, RSAEncryptedSecret *secret);
int pack_key_exchange_id(Octstr *data, long charpos, KeyExchangeId *keyexid);
int pack_array(Octstr *data, long charpos, List *array);
int pack_key_list(Octstr *data, long charpos, List *key_list);
int pack_ciphersuite_list(Octstr *data, long charpos, List *ciphersuites);
int pack_compression_method_list(Octstr *data, long charpos, List *compmethod_list);
int pack_identifier(Octstr *data, long charpos, Identifier *ident);
int pack_signature(Octstr *data, long charpos, Signature *sig);
int pack_wtls_certificate(Octstr *data, long charpos, WTLSCertificate *cert);


int unpack_int16(Octstr *data, long *charpos);
long unpack_int32(Octstr *data, long *charpos);
Octstr * unpack_octstr(Octstr *data, long *charpos);
Octstr * unpack_octstr16(Octstr *data, long *charpos);
Octstr * unpack_octstr_fixed(Octstr *data, long *charpos, long length);
Random * unpack_random(Octstr *data, long *charpos);
DHParameters * unpack_dhparams(Octstr *data, long *charpos);
ECParameters * unpack_ecparams(Octstr *data, long *charpos);
ParameterSpecifier * unpack_param_spec(Octstr *data, long *charpos);
PublicKey * unpack_public_key(Octstr *data, long *charpos, PublicKeyType key_type);
RSAPublicKey * unpack_rsa_pubkey(Octstr *data, long *charpos);
DHPublicKey * unpack_dh_pubkey(Octstr *data, long *charpos);
ECPublicKey * unpack_ec_pubkey(Octstr *data, long *charpos);
RSASecret * unpack_rsa_secret(Octstr *data, long *charpos);
RSAEncryptedSecret * unpack_rsa_encrypted_secret(Octstr *data, long *charpos);
KeyExchangeId * unpack_key_exchange_id(Octstr *data, long *charpos);
List * unpack_array(Octstr *data, long *charpos);
List * unpack_ciphersuite_list(Octstr *data, long *charpos);
List * unpack_key_list(Octstr *data, long *charpos);
List * unpack_compression_method_list(Octstr *data, long *charpos);
Identifier * unpack_identifier(Octstr *data, long *charpos);
Signature * unpack_signature(Octstr *data, long *charpos);
WTLSCertificate * unpack_wtls_certificate(Octstr *data, long *charpos);

void dump_int16(char *dbg, int level, int i);
void dump_int32(char *dbg, int level, long i);
void dump_octstr(char *dbg, int level, Octstr *opaque);
void dump_octstr16(char *dbg, int level, Octstr *opaque);
void dump_octstr_fixed(char *dbg, int level, Octstr *opaque);
void dump_random(char *dbg, int level, Random *random);
void dump_dhparams(char *dbg, int level, DHParameters *dhparams);
void dump_ecparams(char *dbg, int level, ECParameters *ecparams);
void dump_param_spec(char *dbg, int level, ParameterSpecifier *pspec);
void dump_public_key(char *dbg, int level, PublicKey *key, PublicKeyType key_type);
void dump_rsa_pubkey(char *dbg, int level, RSAPublicKey *key);
void dump_dh_pubkey(char *dbg, int level, DHPublicKey *key);
void dump_ec_pubkey(char *dbg, int level, ECPublicKey *key);
void dump_rsa_secret(char *dbg, int level, RSASecret *secret);
void dump_rsa_encrypted_secret(char *dbg, int level, RSAEncryptedSecret *secret);
void dump_key_exchange_id(char *dbg, int level, KeyExchangeId *keyexid);
void dump_array(char *dbg, int level, List *array);
void dump_key_list(char *dbg, int level, List *key_list);
void dump_ciphersuite_list(char *dbg, int level, List *ciphersuites);
void dump_compression_method_list(char *dbg, int level, List *compmethod_list);
void dump_identifier(char *dbg, int level, Identifier *ident);
void dump_signature(char *dbg, int level, Signature *sig);
void dump_wtls_certificate(char *dbg, int level, WTLSCertificate *cert);

void destroy_rsa_pubkey(RSAPublicKey *key);
void destroy_array(List *array);
void destroy_identifier(Identifier *ident);
void destroy_random(Random *random);
void destroy_key_list(List *key_list);
void destroy_ciphersuite_list(List *ciphersuites);
void destroy_compression_method_list(List *compmethod_list);
void destroy_wtls_certificate(WTLSCertificate *cert);
void destroy_param_spec(ParameterSpecifier *pspec);
void destroy_dh_pubkey(DHPublicKey *key);
void destroy_ec_pubkey(ECPublicKey *key);
void destroy_rsa_encrypted_secret(RSAEncryptedSecret *secret);
#endif /* PDUSUPPORT_H */
