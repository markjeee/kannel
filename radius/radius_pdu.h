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
 * radius_pdu.h - declarations for RADIUS Accounting PDUs
 *
 * Stipe Tolj <stolj@wapme.de>
 */


#ifndef RADIUS_PDU_H
#define RADIUS_PDU_H


#include "gwlib/gwlib.h"
#include "gwlib/dict.h"

/* attribute types */
enum {
    t_int, t_string, t_ipaddr
};

enum {
    #define ATTR(attr, type, string, min, max)
    #define UNASSIGNED(attr)
    #define ATTRIBUTES(fields)
    #include "radius_attributes.def"
    #define INTEGER(name, octets)
    #define OCTETS(name, field_giving_octets)
    #define PDU(name, id, fields) name = id,
    #include "radius_pdu.def"
    RADIUS_PDU_DUMMY_TYPE
};


typedef struct RADIUS_PDU RADIUS_PDU;
struct RADIUS_PDU {
    int type;
    const char *type_name;
    Dict *attr;
    union {
    #define ATTR(attr, type, string, min, max)
    #define UNASSIGNED(attr)
	#define ATTRIBUTES(fields)
    #include "radius_attributes.def"
	#define INTEGER(name, octets) unsigned long name;
	#define NULTERMINATED(name, max_octets) Octstr *name;
	#define OCTETS(name, field_giving_octets) Octstr *name;
	#define PDU(name, id, fields) struct name { fields } name;
	#include "radius_pdu.def"
    } u;
};


RADIUS_PDU *radius_pdu_create(int type, RADIUS_PDU *req);
void radius_pdu_destroy(RADIUS_PDU *pdu);

int radius_authenticate_pdu(RADIUS_PDU *pdu, Octstr **data, Octstr *secret);

Octstr *radius_pdu_pack(RADIUS_PDU *pdu);
RADIUS_PDU *radius_pdu_unpack(Octstr *data_without_len);

void radius_pdu_dump(RADIUS_PDU *pdu);

/*
 * Returns the value of an RADIUS attribute inside a PDU as Octstr.
 * If the attribute was not present in the PDU, it returns NULL.
 */
Octstr *radius_get_attribute(RADIUS_PDU *pdu, Octstr *attribute);

#endif
