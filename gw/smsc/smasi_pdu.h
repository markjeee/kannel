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
 * smasi_pdu.h - declarations for SMASI PDUs
 *
 * Stipe Tolj <stolj@wapme.de>
 */


#ifndef SMASI_PDU_H
#define SMASI_PDU_H

#include "gwlib/gwlib.h"

/* 
 * Any PDUs that are below this ID will be packed with a 
 * prefixed hyphen.
 */
#define SMASI_HYPHEN_ID     0x00000010

enum {
    #define NONTERMINATED(name)
    #define COMATERMINATED(name)
    #define PDU(name, id, fields) name = id,
    #include "smasi_pdu.def"
    SMASI_PDU_DUMMY_TYPE
};

typedef struct SMASI_PDU SMASI_PDU;
struct SMASI_PDU {
    unsigned long type;
    const char *type_name;
    unsigned int needs_hyphen;
    union {
    #define NONTERMINATED(name) Octstr *name;
    #define COMATERMINATED(name) Octstr *name;
    #define PDU(name, id, fields) struct name { fields } name;
    #include "smasi_pdu.def"
    } u;
};


/******************************************************************************
* Numering Plan Indicator and Type of Number codes from
* GSM 03.40 Version 5.3.0 Section 9.1.2.5.
* http://www.etsi.org/
*/
#define GSM_ADDR_TON_UNKNOWN          0x00000000
#define GSM_ADDR_TON_INTERNATIONAL    0x00000001
#define GSM_ADDR_TON_NATIONAL         0x00000002
#define GSM_ADDR_TON_NETWORKSPECIFIC  0x00000003
#define GSM_ADDR_TON_SUBSCRIBER       0x00000004
#define GSM_ADDR_TON_ALPHANUMERIC     0x00000005 /* GSM TS 03.38 */
#define GSM_ADDR_TON_ABBREVIATED      0x00000006
#define GSM_ADDR_TON_EXTENSION        0x00000007 /* Reserved */

#define GSM_ADDR_NPI_UNKNOWN          0x00000000
#define GSM_ADDR_NPI_E164             0x00000001
#define GSM_ADDR_NPI_X121             0x00000003
#define GSM_ADDR_NPI_TELEX            0x00000004
#define GSM_ADDR_NPI_NATIONAL         0x00000008
#define GSM_ADDR_NPI_PRIVATE          0x00000009
#define GSM_ADDR_NPI_ERMES            0x0000000A /* ETSI DE/PS 3 01-3 */
#define GSM_ADDR_NPI_EXTENSION        0x0000000F /* Reserved */

/******************************************************************************
 * esm_class parameters
 */
#define ESM_CLASS_DEFAULT_SMSC_MODE        0x00000000
#define ESM_CLASS_DATAGRAM_MODE            0x00000001
#define ESM_CLASS_FORWARD_MODE             0x00000002
#define ESM_CLASS_STORE_AND_FORWARD_MODE   0x00000003
#define ESM_CLASS_DELIVERY_ACK             0x00000008
#define ESM_CLASS_USER_ACK                 0x00000010
#define ESM_CLASS_UDH_INDICATOR            0x00000040
#define ESM_CLASS_RPI                      0x00000080
#define ESM_CLASS_UDH_AND_RPI              0x000000C0


SMASI_PDU *smasi_pdu_create(unsigned long type);
void smasi_pdu_destroy(SMASI_PDU *pdu);
int smasi_pdu_is_valid(SMASI_PDU *pdu); /* XXX */
Octstr *smasi_pdu_pack(SMASI_PDU *pdu);
SMASI_PDU *smasi_pdu_unpack(Octstr *data_without_len);
void smasi_pdu_dump(SMASI_PDU *pdu);

Octstr *smasi_pdu_read(Connection *conn);


#endif
