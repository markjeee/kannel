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

/* wsp_pdu.h - definitions for unpacked WTP protocol data units
 *
 * This file generates a structure definition and some function
 * declarations from wtp_pdu.def, using preprocessor magic.
 *
 * Richard Braakman
 */

#ifndef WSP_PDU_H
#define WSP_PDU_H

#include "gwlib/gwlib.h"

/* The Get and Post PDUs contain a "subtype" field.  Sometimes we
 * have to reconstruct the full method number.  For methods encoded
 * in Get PDUs, this is GET_METHODS + subtype.  For methods encoded
 * in Post PDUs, this is POST_METHODS + subtype. */
enum {
    GET_METHODS = 0x40,
    POST_METHODS = 0x60
};

/* Enumerate the symbolic names of the PDUs */
enum wsp_pdu_types {
#define PDU(name, docstring, fields, is_valid) name,
#include "wsp_pdu.def"
#undef PDU
};

struct wsp_pdu {
	int type;

	union {
/* For each PDU, declare a structure with its fields, named after the PDU */
#define PDU(name, docstring, fields, is_valid) struct name { fields } name;
#define UINT(field, docstring, bits) unsigned long field;
#define UINTVAR(field, docstring) unsigned long field;
#define OCTSTR(field, docstring, lengthfield) Octstr *field;
#define REST(field, docstring) Octstr *field;
#define TYPE(bits, value)
#define RESERVED(bits)
#define TPI(confield)
#include "wsp_pdu.def"
#undef TPI
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU
	} u;
};
typedef struct wsp_pdu WSP_PDU;

WSP_PDU *wsp_pdu_create(int type);
WSP_PDU *wsp_pdu_unpack(Octstr *data);
Octstr *wsp_pdu_pack(WSP_PDU *pdu);
void wsp_pdu_dump(WSP_PDU *pdu, int level);
void wsp_pdu_destroy(WSP_PDU *pdu);

#endif
