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
 * smpp_pdu.h - declarations for SMPP PDUs
 *
 * Lars Wirzenius
 * Alexander Malysh <a.malysh@centrium.de>:
 *     Extended optional parameters implementation.
 */


#ifndef SMPP_PDU_H
#define SMPP_PDU_H


#include "gwlib/gwlib.h"
#include "gwlib/dict.h"


enum {
    #define OPTIONAL_BEGIN
    #define TLV_INTEGER(name, max_len)
    #define TLV_NULTERMINATED(name, max_len)
    #define TLV_OCTETS(name, min_len, max_len)
    #define OPTIONAL_END
    #define INTEGER(name, octets)
    #define NULTERMINATED(name, max_octets)
    #define OCTETS(name, field_giving_octets)
    #define PDU(name, id, fields) name = id,
    #include "smpp_pdu.def"
    SMPP_PDU_DUMMY_TYPE
};


typedef struct SMPP_PDU SMPP_PDU;
struct SMPP_PDU {
    unsigned long type;
    const char *type_name;
    union {
        #define OPTIONAL_BEGIN
        #define TLV_INTEGER(name, octets) long name;
        #define TLV_NULTERMINATED(name, max_len) Octstr *name;
        #define TLV_OCTETS(name, min_len, max_len) Octstr *name;
        #define OPTIONAL_END Dict *tlv;
        #define INTEGER(name, octets) unsigned long name;
        #define NULTERMINATED(name, max_octets) Octstr *name;
        #define OCTETS(name, field_giving_octets) Octstr *name;
        #define PDU(name, id, fields) struct name { fields } name;
        #include "smpp_pdu.def"
    } u;
};


/******************************************************************************
* Numbering Plan Indicator and Type of Number codes from
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
#define GSM_ADDR_NPI_INTERNET         0x0000000E /* SMPP v5.0, sec. 4.7.2, page 113 */
#define GSM_ADDR_NPI_EXTENSION        0x0000000F /* Reserved */
#define GSM_ADDR_NPI_WAP_CLIENT_ID    0x00000012 /* SMPP v5.0, sec. 4.7.2, page 113 */

/******************************************************************************
 * esm_class parameters for both submit_sm and deliver_sm PDUs
 */
#define ESM_CLASS_SUBMIT_DEFAULT_SMSC_MODE        0x00000000
#define ESM_CLASS_SUBMIT_DATAGRAM_MODE            0x00000001
#define ESM_CLASS_SUBMIT_FORWARD_MODE             0x00000002
#define ESM_CLASS_SUBMIT_STORE_AND_FORWARD_MODE   0x00000003
#define ESM_CLASS_SUBMIT_DELIVERY_ACK             0x00000008
#define ESM_CLASS_SUBMIT_USER_ACK                 0x00000010
#define ESM_CLASS_SUBMIT_UDH_INDICATOR            0x00000040
#define ESM_CLASS_SUBMIT_RPI                      0x00000080
#define ESM_CLASS_SUBMIT_UDH_AND_RPI              0x000000C0

#define ESM_CLASS_DELIVER_DEFAULT_TYPE            0x00000000
#define ESM_CLASS_DELIVER_SMSC_DELIVER_ACK        0x00000004
#define ESM_CLASS_DELIVER_SME_DELIVER_ACK         0x00000008
#define ESM_CLASS_DELIVER_SME_MANULAL_ACK         0x00000010
#define ESM_CLASS_DELIVER_INTERM_DEL_NOTIFICATION 0x00000020
#define ESM_CLASS_DELIVER_UDH_INDICATOR           0x00000040
#define ESM_CLASS_DELIVER_RPI                     0x00000080
#define ESM_CLASS_DELIVER_UDH_AND_RPI             0x000000C0


/*
 * Some SMPP error messages we come across
 */
enum SMPP_ERROR_MESSAGES {
    SMPP_ESME_ROK = 0x00000000,
    SMPP_ESME_RINVMSGLEN = 0x00000001,
    SMPP_ESME_RINVCMDLEN = 0x00000002,
    SMPP_ESME_RINVCMDID = 0x00000003,
    SMPP_ESME_RINVBNDSTS = 0x00000004,
    SMPP_ESME_RALYNBD = 0x00000005,
    SMPP_ESME_RINVPRTFLG = 0x00000006,
    SMPP_ESME_RINVREGDLVFLG = 0x00000007,
    SMPP_ESME_RSYSERR = 0x00000008,
    SMPP_ESME_RINVSRCADR = 0x0000000A,
    SMPP_ESME_RINVDSTADR = 0x0000000B,
    SMPP_ESME_RINVMSGID = 0x0000000C,
    SMPP_ESME_RBINDFAIL = 0x0000000D,
    SMPP_ESME_RINVPASWD = 0x0000000E,
    SMPP_ESME_RINVSYSID = 0x0000000F,
    SMPP_ESME_RCANCELFAIL = 0x00000011,
    SMPP_ESME_RREPLACEFAIL = 0x00000013,
    SMPP_ESME_RMSGQFUL   = 0x00000014,
    SMPP_ESME_RINVSERTYP = 0x00000015,
    SMPP_ESME_RINVNUMDESTS = 0x00000033,
    SMPP_ESME_RINVDLNAME = 0x00000034,
    SMPP_ESME_RINVDESTFLAG = 0x00000040,
    SMPP_ESME_RINVSUBREP = 0x00000042,
    SMPP_ESME_RINVESMCLASS = 0x00000043,
    SMPP_ESME_RCNTSUBDL = 0x00000044,
    SMPP_ESME_RSUBMITFAIL = 0x00000045,
    SMPP_ESME_RINVSRCTON = 0x00000048,
    SMPP_ESME_RINVSRCNPI = 0x00000049,
    SMPP_ESME_RINVDSTTON = 0x00000050,
    SMPP_ESME_RINVDSTNPI = 0x00000051,
    SMPP_ESME_RINVSYSTYP = 0x00000053,
    SMPP_ESME_RINVREPFLAG = 0x00000054,
    SMPP_ESME_RINVNUMMSGS = 0x00000055,
    SMPP_ESME_RTHROTTLED = 0x00000058,
    SMPP_ESME_RINVSCHED = 0x00000061,
    SMPP_ESME_RINVEXPIRY = 0x00000062,
    SMPP_ESME_RINVDFTMSGID = 0x00000063,
    SMPP_ESME_RX_T_APPN = 0x00000064,
    SMPP_ESME_RX_P_APPN = 0x00000065,
    SMPP_ESME_RX_R_APPN = 0x00000066,
    SMPP_ESME_RQUERYFAIL = 0x00000067,
    SMPP_ESME_RINVTLVSTREAM = 0x000000C0,
    SMPP_ESME_RTLVNOTALLWD = 0x000000C1,
    SMPP_ESME_RINVTLVLEN = 0x000000C2,
    SMPP_ESME_RMISSINGTLV = 0x000000C3,
    SMPP_ESME_RINVTLVVAL = 0x000000C4,
    SMPP_ESME_RDELIVERYFAILURE = 0x000000FE,
    SMPP_ESME_RUNKNOWNERR = 0x000000FF,
    SMPP_ESME_RSERTYPUNAUTH = 0x00000100,
    SMPP_ESME_RPROHIBITED = 0x00000101,
    SMPP_ESME_RSERTYPUNAVAIL = 0x00000102,
    SMPP_ESME_RSERTYPDENIED = 0x00000103,
    SMPP_ESME_RINVDCS = 0x00000104,
    SMPP_ESME_RINVSRCADDRSUBUNIT = 0x00000105,
    SMPP_ESME_RINVDSTADDRSUBUNIT = 0x00000106,
    SMPP_ESME_RINVBCASTFREQINT = 0x00000107,
    SMPP_ESME_RINVBCASTALIAS_NAME = 0x00000108,
    SMPP_ESME_RINVBCASTAREAFMT = 0x00000109,
    SMPP_ESME_RINVNUMBCAST_AREAS = 0x0000010A,
    SMPP_ESME_RINVBCASTCNTTYPE = 0x0000010B,
    SMPP_ESME_RINVBCASTMSGCLASS = 0x0000010C,
    SMPP_ESME_RBCASTFAIL = 0x0000010D,
    SMPP_ESME_RBCASTQUERYFAIL = 0x0000010E,
    SMPP_ESME_RBCASTCANCELFAIL = 0x0000010F,
    SMPP_ESME_RINVBCAST_REP = 0x00000110,
    SMPP_ESME_RINVBCASTSRVGRP = 0x00000111,
    SMPP_ESME_RINVBCASTCHANIND = 0x00000112,
};

/* initialize SMPP PDU */
int smpp_pdu_init(Cfg *cfg);
/* shutdown SMPP PDU */
int smpp_pdu_shutdown(void);

SMPP_PDU *smpp_pdu_create(unsigned long type, unsigned long seq_no);
void smpp_pdu_destroy(SMPP_PDU *pdu);
int smpp_pdu_is_valid(SMPP_PDU *pdu); /* XXX */
Octstr *smpp_pdu_pack(Octstr *smsc_id, SMPP_PDU *pdu);
SMPP_PDU *smpp_pdu_unpack(Octstr *smsc_id, Octstr *data_without_len);
void smpp_pdu_dump(Octstr *smsc_id, SMPP_PDU *pdu);

long smpp_pdu_read_len(Connection *conn);
Octstr *smpp_pdu_read_data(Connection *conn, long len);

/*
 * Return error string for given error code
 */
const char *smpp_error_to_string(enum SMPP_ERROR_MESSAGES error);

#endif
