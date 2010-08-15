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
 * wsp_headers.h - WSP PDU headers implementation header
 *
 * Kalle Marjola <rpr@wapit.com>
 */

#ifndef WSP_HEADERS_H
#define WSP_HEADERS_H

#include "gwlib/gwlib.h"

#define WSP_FIELD_VALUE_NUL_STRING	1
#define WSP_FIELD_VALUE_ENCODED 	2
#define WSP_FIELD_VALUE_DATA		3
#define WSP_FIELD_VALUE_NONE		4 /* secondary_field_value only */

/* The value defined as Quote in 8.4.2.1 */
#define WSP_QUOTE  127

/* Largest value that will fit in a Short-integer encoding */
#define MAX_SHORT_INTEGER 127

/* Marker values used in the encoding */
#define BASIC_AUTHENTICATION 128
#define ABSOLUTE_TIME 128
#define RELATIVE_TIME 129
#define BYTE_RANGE 128
#define SUFFIX_BYTE_RANGE 129

/* Use this value for Expires headers if we can't parse the expiration
 * date.  It's about one day after the start of the epoch.  We don't
 * use the exact start of the epoch because some clients have trouble
 * with that. */
#define LONG_AGO_VALUE 100000

/* LIST is a comma-separated list such as is described in the "#rule"
 * entry of RFC2616 section 2.1. */
#define LIST 1
/* BROKEN_LIST is a list of "challenge" or "credentials" elements
 * such as described in RFC2617.  I call it broken because the
 * parameters are separated with commas, instead of with semicolons
 * like everywhere else in HTTP.  Parsing is more difficult because
 * commas are also used to separate list elements. */
#define BROKEN_LIST 2

#define TABLE_SIZE(table) ((long)(sizeof(table) / sizeof(table[0])))

struct parameter
{
    Octstr *key;
    Octstr *value;
};
typedef struct parameter Parameter;

typedef int header_pack_func_t(Octstr *packed, Octstr *value);

struct headerinfo
{
    /* The WSP_HEADER_* enumeration value for this header */
    int header;
    header_pack_func_t *func;
    /* True if this header type allows multiple elements per
     * header on the HTTP side. */
    int allows_list;
};

/* All WSP packing/unpacking routines that are exported for use within
 * external modules, ie. MMS encoding/decoding.
 */
int wsp_field_value(ParseContext *context, int *well_known_value);
void wsp_skip_field_value(ParseContext *context);
int wsp_secondary_field_value(ParseContext *context, long *result);
void parm_destroy_item(void *parm);
List *wsp_strip_parameters(Octstr *value);
/* unpacking */
Octstr *wsp_unpack_integer_value(ParseContext *context);
Octstr *wsp_unpack_version_value(long value);
void wsp_unpack_all_parameters(ParseContext *context, Octstr *decoded);
Octstr *wsp_unpack_date_value(ParseContext *context);
void wsp_unpack_well_known_field(List *unpacked, int field_type,
                                 ParseContext *context);
void wsp_unpack_app_header(List *unpacked, ParseContext *context);
/* packing */
void wsp_pack_integer_value(Octstr *packed, unsigned long integer);
int wsp_pack_date(Octstr *packet, Octstr *value);
int wsp_pack_retry_after(Octstr *packet, Octstr *value);
int wsp_pack_text(Octstr *packet, Octstr *value);
int wsp_pack_quoted_text(Octstr *packed, Octstr *text);
int wsp_pack_integer_string(Octstr *packet, Octstr *value);
int wsp_pack_version_value(Octstr *packet, Octstr *value);
int wsp_pack_constrained_value(Octstr *packed, Octstr *text, long value);
void wsp_pack_value(Octstr *packed, Octstr *encoded);
void wsp_pack_parameters(Octstr *packed, List *parms);
int wsp_pack_list(Octstr *packed, long fieldnum, List *elements, int i);
void wsp_pack_short_integer(Octstr *packed, unsigned long integer);
void wsp_pack_separate_content_type(Octstr *packed, List *headers);
Octstr *wsp_unpack_accept_general_form(ParseContext *context);
Octstr *wsp_unpack_accept_charset_general_form(ParseContext *context);
int wsp_pack_content_type(Octstr *packet, Octstr *value);
int wsp_pack_application_header(Octstr *packed,
				Octstr *fieldname, Octstr *value);
void wsp_pack_long_integer(Octstr *packed, unsigned long integer);

/* Return an HTTPHeader linked list which must be freed by the caller
 * (see http.h for details of HTTPHeaders). Cannot fail.
 * The second argument is true if the headers will have a leading
 * Content-Type field.  Some WSP PDUs encode Content-Type separately
 * this way for historical reasons.
 */
List *wsp_headers_unpack(Octstr *headers, int content_type);

/* Take a List of headers, encode them according to the WSP spec,
 * and return the encoded headers as an Octstr. 
 * The second argument is true if the encoded headers should have
 * a leading content-type field.  See the note for wsp_headers_unpack. */
Octstr *wsp_headers_pack(List *headers, int separate_content_type, int wsp_version);

#endif
