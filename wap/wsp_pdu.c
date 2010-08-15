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

/* wsp_pdu.c - pack and unpack WSP packets
 *
 * Generates packing and unpacking code from wsp_pdu.def.
 * Code is very similar to wsp_pdu.c, please make any changes in both files.
 *
 * Richard Braakman
 */

#include "gwlib/gwlib.h"
#include "wsp_pdu.h"

WSP_PDU *wsp_pdu_create(int type) {
	WSP_PDU *pdu;
	
	pdu = gw_malloc(sizeof(*pdu));
	pdu->type = type;

	switch (pdu->type) {
#define PDU(name, docstring, fields, is_valid) \
	case name: {\
	struct name *p; p = &pdu->u.name; \
	fields \
	} break;
#define UINT(field, docstring, bits) p->field = 0;
#define UINTVAR(field, docstring) p->field = 0;
#define OCTSTR(field, docstring, lengthfield) p->field = NULL;
#define REST(field, docstring) p->field = NULL;
#define TYPE(bits, value)
#define RESERVED(bits)
#include "wsp_pdu.def"
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU
	default:
		panic(0, "Internal error: Unknown PDU type %d", pdu->type);
		break;
	}

	return pdu;
}

void wsp_pdu_destroy(WSP_PDU *pdu) {
	if (pdu == NULL)
		return;

	switch (pdu->type) {
#define PDU(name, docstring, fields, is_valid) \
	case name: {\
	struct name *p; p = &pdu->u.name; \
	fields \
	} break;
#define UINT(field, docstring, bits)
#define UINTVAR(field, docstring)
#define OCTSTR(field, docstring, lengthfield) octstr_destroy(p->field);
#define REST(field, docstring) octstr_destroy(p->field);
#define TYPE(bits, value)
#define RESERVED(bits)
#include "wsp_pdu.def"
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU
	default:
		panic(0, "Cannot destroy unknown WSP PDU type %d", pdu->type);
		break;
	}

	gw_free(pdu);
}

/* Determine which type of PDU this is, using the TYPE macros in
 * the definition file. */
static int wsp_pdu_type(Octstr *data) {
	long bitpos;
	long lastpos = -1;
	long lastnumbits = -1;
	long lastval = -1;
	int thistype;

	/* This code looks slow, but an optimizing compiler will
	 * reduce it considerably.  gcc -O2 will produce a single
	 * call to octstr_get_bits, folllowed by a sequence of
	 * tests on lastval. */

/* Only UINT and RESERVED fields may precede the TYPE */
#define PDU(name, docstring, fields, is_valid) \
	bitpos = 0; \
	thistype = name; \
	fields
#define UINT(field, docstring, bits) bitpos += (bits);
#define UINTVAR(field, docstring)
#define OCTSTR(field, docstring, lengthfield)
#define REST(field, docstring)
#define TYPE(bits, value) \
	if ((bits) != lastnumbits || bitpos != lastpos) { \
		lastval = octstr_get_bits(data, bitpos, (bits)); \
	} \
	if (lastval == (value)) \
		return thistype; \
	lastnumbits = (bits); \
	lastpos = bitpos;
#define RESERVED(bits) bitpos += (bits);
#include "wsp_pdu.def"
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU

	return -1;
}

WSP_PDU *wsp_pdu_unpack(Octstr *data) {
	WSP_PDU *pdu = NULL;
	long bitpos = 0;

	gw_assert(data != NULL);

	pdu = gw_malloc(sizeof(*pdu));

	pdu->type = wsp_pdu_type(data);

	switch (pdu->type) {
#define PDU(name, docstring, fields, is_valid) \
	case name: { \
		struct name *p = &pdu->u.name; \
		fields \
		gw_assert(bitpos % 8 == 0); \
		if (bitpos / 8 != octstr_len(data)) { \
			warning(0, "Bad length for " #name " PDU, " \
				"expected %ld", bitpos / 8); \
		} \
		if (!(is_valid)) { \
			warning(0, #name " PDU failed %s", #is_valid); \
		} \
	} break;
#define UINT(field, docstring, bits) \
	p->field = octstr_get_bits(data, bitpos, (bits)); \
	bitpos += (bits);
#define UINTVAR(field, docstring) \
	gw_assert(bitpos % 8 == 0); \
	p->field = octstr_get_bits(data, bitpos + 1, 7); \
	while (octstr_get_bits(data, bitpos, 1)) { \
		bitpos += 8; \
		p->field <<= 7; \
		p->field |= octstr_get_bits(data, bitpos + 1, 7); \
	} \
	bitpos += 8;
#define OCTSTR(field, docstring, lengthfield) \
	gw_assert(bitpos % 8 == 0); \
	p->field = octstr_copy(data, bitpos / 8, p->lengthfield); \
	bitpos += 8 * p->lengthfield;
#define REST(field, docstring) \
	gw_assert(bitpos % 8 == 0); \
	if (bitpos / 8 <= octstr_len(data)) { \
		p->field = octstr_copy(data, bitpos / 8, \
				octstr_len(data) - bitpos / 8); \
		bitpos = octstr_len(data) * 8; \
	} else { \
		p->field = octstr_create(""); \
	}
#define TYPE(bits, value) bitpos += (bits);
#define RESERVED(bits) bitpos += (bits);
#include "wsp_pdu.def"
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU
	default:
		warning(0, "WSP PDU with unknown type %d", pdu->type);
		gw_free(pdu);
		return NULL;
	}

	return pdu;
}

static void fixup_length_fields(WSP_PDU *pdu) {
	switch (pdu->type) {
#define PDU(name, docstring, fields, is_valid) \
	case name: { \
		struct name *p; p = &pdu->u.name; \
		fields \
	} break;
#define UINT(field, docstring, bits)
#define UINTVAR(field, docstring)
#define OCTSTR(field, docstring, lengthfield) \
	p->lengthfield = octstr_len(p->field);
#define REST(field, docstring)
#define TYPE(bits, value)
#define RESERVED(bits)
#include "wsp_pdu.def"
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU
	}
}

Octstr *wsp_pdu_pack(WSP_PDU *pdu) {
	Octstr *data;
	long bitpos;

	/* We rely on octstr_set_bits to lengthen our octstr as needed. */
	data = octstr_create("");

	fixup_length_fields(pdu);

	bitpos = 0;
	switch (pdu->type) {
#define PDU(name, docstring, fields, is_valid) \
	case name: { \
		struct name *p = &pdu->u.name; \
		fields \
		gw_assert(bitpos % 8 == 0); \
	} break;
#define UINT(field, docstring, bits) \
	octstr_set_bits(data, bitpos, (bits), p->field); \
	bitpos += (bits);
#define UINTVAR(field, docstring) \
	gw_assert(bitpos % 8 == 0); \
	octstr_append_uintvar(data, p->field); \
	bitpos = 8 * octstr_len(data);
#define OCTSTR(field, docstring, lengthfield) \
	gw_assert(bitpos % 8 == 0); \
	if (p->field != NULL) \
		octstr_append(data, p->field); \
	bitpos += 8 * octstr_len(p->field);
#define REST(field, docstring) \
	gw_assert(bitpos % 8 == 0); \
	if (p->field != NULL) \
		octstr_append(data, p->field); \
	bitpos += 8 * octstr_len(p->field);
#define TYPE(bits, value) \
	octstr_set_bits(data, bitpos, (bits), (value)); \
	bitpos += (bits);
#define RESERVED(bits) bitpos += (bits);
#include "wsp_pdu.def"
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU
	default:
		panic(0, "Packing unknown WSP PDU type %ld", (long) pdu->type);
	}

	return data;
}

void wsp_pdu_dump(WSP_PDU *pdu, int level) {
	char *dbg = "wap.wsp";

	switch (pdu->type) {
#define PDU(name, docstring, fields, is_valid) \
	case name: { \
		struct name *p = &pdu->u.name; \
		debug(dbg, 0, "%*sWSP %s PDU at %p:", \
			level, "", #name, (void *)pdu); \
		fields \
	} break;
#define UINT(field, docstring, bits) \
	debug(dbg, 0, "%*s %s: %lu", level, "", docstring, p->field);
#define UINTVAR(field, docstring) \
	debug(dbg, 0, "%*s %s: %lu", level, "", docstring, p->field);
#define OCTSTR(field, docstring, lengthfield) \
	debug(dbg, 0, "%*s %s:", level, "", docstring); \
	octstr_dump(p->field, level + 1);
#define REST(field, docstring) \
	debug(dbg, 0, "%*s %s:", level, "", docstring); \
	octstr_dump(p->field, level + 1);
#define TYPE(bits, value)
#define RESERVED(bits)
#include "wsp_pdu.def"
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU
	default:
		debug(dbg, 0, "%*sWSP PDU at %p:", level, "", (void *)pdu);
		debug(dbg, 0, "%*s unknown type %u", level, "", pdu->type);
		break;
	}
	debug("wap.wsp", 0, "%*sWSP PDU dump ends.", level, "");
}
