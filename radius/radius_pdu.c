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
 * radius_pdu.c - parse and generate RADIUS Accounting PDUs
 *
 * Taken from gw/smsc/smpp_pdu.c writen by Lars Wirzenius.
 * This makes heavy use of C pre-processor macro magic.
 *
 * References: RFC2866 - RADIUS Accounting
 * 
 * Stipe Tolj <stolj@wapme.de>
 */


#include <string.h>
#include "radius_pdu.h"

#define MIN_RADIUS_PDU_LEN  20
#define MAX_RADIUS_PDU_LEN  4095 


static unsigned long decode_integer(Octstr *os, long pos, int octets)
{
    unsigned long u;
    int i;

    gw_assert(octstr_len(os) >= pos + octets);

    u = 0;
    for (i = 0; i < octets; ++i)
    	u = (u << 8) | octstr_get_char(os, pos + i);

    return u;
}


static void append_encoded_integer(Octstr *os, unsigned long u, long octets)
{
    long i;

    for (i = 0; i < octets; ++i)
    	octstr_append_char(os, (u >> ((octets - i - 1) * 8)) & 0xFF);
}


/*
static void *get_header_element(RADIUS_PDU *pdu, unsigned char *e) 
{
    switch (pdu->type) {
    #define INTEGER(name, octets) \
    if (strcmp(#name, e) == 0) return (void*) *(&p->name);
    #define NULTERMINATED(name, max_octets)
    #define OCTETS(name, field_giving_octets) \
    if (strcmp(#name, e) == 0) return (void*) p->name;
    #define PDU(name, id, fields) \
        case id: { \
        struct name *p = &pdu->u.name; \
    } break;
    #include "radius_pdu.def"
    default:
    	error(0, "Unknown RADIUS_PDU type, internal error.");
    	gw_free(pdu);
	   return NULL;
    }
}
*/


RADIUS_PDU *radius_pdu_create(int type, RADIUS_PDU *req)
{
    RADIUS_PDU *pdu;

    pdu = gw_malloc(sizeof(*pdu));
    pdu->type = type;

    switch (type) {
    #define INTEGER(name, octets) \
   	if (strcmp(#name, "code") == 0) p->name = type; \
    else p->name = 0;
    #define OCTETS(name, field_giving_octets) p->name = NULL;
    #define PDU(name, id, fields) \
    	case id: { \
	    struct name *p = &pdu->u.name; \
	    pdu->type_name = #name; \
	    fields \
	} break;
    #include "radius_pdu.def"
    default:
    	error(0, "Unknown RADIUS_PDU type, internal error.");
    	gw_free(pdu);
	return NULL;
    }
    #define ATTR(attr, type, string, min, max)
    #define UNASSIGNED(attr)
    #define ATTRIBUTES(fields) \
        pdu->attr = dict_create(20, (void (*)(void *))octstr_destroy);
    #include "radius_attributes.def"

    return pdu;
}

void radius_pdu_destroy(RADIUS_PDU *pdu)
{
    if (pdu == NULL)
    	return;

    switch (pdu->type) {
    #define INTEGER(name, octets) p->name = 0;
    #define OCTETS(name, field_giving_octets) octstr_destroy(p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "radius_pdu.def"
    default:
    	error(0, "Unknown RADIUS_PDU type, internal error while destroying.");
    }

    #define ATTR(attr, type, string, min, max)
    #define UNASSIGNED(attr)
    #define ATTRIBUTES(fields) dict_destroy(pdu->attr);
    #include "radius_attributes.def"

    gw_free(pdu);
}

/*
static void radius_type_append(Octstr **os, int type, int pmin, int pmax, 
                               Octstr *value) 
{
    long l;

    switch (type) {
        case t_int:
            octstr_parse_long(&l, value, 0, 10);
    	    append_encoded_integer(*os, l, pmin);
            break;
        case t_string:
            octstr_append(*os, value);
            break;
        case t_ipaddr:
            ret = octstr_create("");
            for (i = 0; i < 4; i++) {
                int c = octstr_get_char(value, i);
                Octstr *b = octstr_format("%d", c);
                octstr_append(ret, b);
                i < 3 ? octstr_append_cstr(ret, ".") : NULL;
                octstr_destroy(b);
            }
            break;
        default:
            panic(0, "RADIUS: Attribute type %d does not exist.", type);
            break;
    }
}
*/

static Octstr *radius_attr_pack(RADIUS_PDU *pdu) 
{
    Octstr *os;

    os = octstr_create("");

    gw_assert(pdu != NULL);

    #define ATTR(atype, type, string, pmin, pmax)                                \
        {                                                                        \
            Octstr *attr_strg = octstr_create(string);                           \
            Octstr *attr_val = dict_get(p->attr, attr_str);                      \
            if (attr_str != NULL) {                                              \
                int attr_len = octstr_len(attr_val) + 2;                         \
                octstr_format_append(os, "%02X", atype);                         \
                octstr_append_data(os, (char*) &attr_len, 2);                    \
                radius_type_append(&os, type, pmin, pmax, attr_val);             \
            }                                                                    \
            octstr_destroy(attr_str);                                            \
        } 
    #define UNASSIGNED(attr)
    #define ATTRIBUTES(fields)                                                                     
    #include "radius_attributes.def"

    return os;
}

Octstr *radius_pdu_pack(RADIUS_PDU *pdu)
{
    Octstr *os,*oos;
    Octstr *temp;

    os = octstr_create("");

    gw_assert(pdu != NULL);

    /*
    switch (pdu->type) {
    #define INTEGER(name, octets) p = *(&p);
    #define NULTERMINATED(name, max_octets) p = *(&p);
    #define OCTETS(name, field_giving_octets) \
    	p->field_giving_octets = octstr_len(p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "radius_pdu.def"
    default:
    	error(0, "Unknown RADIUS_PDU type, internal error while packing.");
    }
    */

    switch (pdu->type) {
    #define INTEGER(name, octets) \
    	append_encoded_integer(os, p->name, octets);
    #define OCTETS(name, field_giving_octets) \
    	octstr_append(os, p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields; oos = radius_attr_pack(pdu); \
                   octstr_append(os, oos);octstr_destroy(oos); } break;
    #include "radius_pdu.def"
    default:
    	error(0, "Unknown RADIUS_PDU type, internal error while packing.");
    }

    /* now set PDU length */
    temp = octstr_create("");
    append_encoded_integer(temp, octstr_len(os), 2);
    octstr_delete(os, 2, 2);
    octstr_insert(os, temp, 2);
    octstr_destroy(temp);
    
    return os;
}

static Octstr *radius_type_convert(int type, Octstr *value)
{
    Octstr *ret = NULL;
    int i;

    switch (type) {
        case t_int:
            ret = octstr_format("%ld", decode_integer(value, 0, 4));
            break;
        case t_string:
            ret = octstr_format("%s", octstr_get_cstr(value));
            break;
        case t_ipaddr:
            ret = octstr_create("");
            for (i = 0; i < 4; i++) {
                int c = octstr_get_char(value, i);
                Octstr *b = octstr_format("%d", c);
                octstr_append(ret, b);
                if (i < 3)
                    octstr_append_cstr(ret, ".");
                octstr_destroy(b);
            }
            break;
        default:
            panic(0, "RADIUS: Attribute type %d does not exist.", type);
            break;
    }

    return ret;
}

static void radius_attr_unpack(ParseContext **context, RADIUS_PDU **pdu) 
{
    #define ATTR(atype, type, string, pmin, pmax) \
                if (atype == attr_type) {  \
                    Octstr *tmp, *value; \
                    if ((attr_len-2) < pmin || (attr_len-2) > pmax) { \
                        error(0, "RADIUS: Attribute (%d) `%s' has invalid len %d, droppped.", \
                              attr_type, string, (attr_len-2)); \
                        continue;  \
                    } \
                    attr_val = parse_get_octets(*context, attr_len - 2); \
                    tmp = octstr_format("RADIUS: Attribute (%d) `%s', len %d", \
                          attr_type, string, attr_len - 2); \
                    value = radius_type_convert(type, attr_val); \
                    octstr_destroy(attr_val); \
                    octstr_dump_short(value, 0, octstr_get_cstr(tmp)); \
                    octstr_destroy(tmp); \
                    attr_str = octstr_create(string);  \
                    dict_put((*pdu)->attr, attr_str, value);  \
                    octstr_destroy(attr_str);  \
                    value = NULL;  \
                } else 
    #define UNASSIGNED(attr)  \
                if (attr == attr_type) {  \
                    error(0, "RADIUS: Attribute (%d) is unassigned and should not be used.", \
                              attr_type); \
                    continue;  \
                } else 
    #define ATTRIBUTES(fields)                                                                       \
        while (parse_octets_left(*context) > 0 && !parse_error(*context)) {                          \
            int attr_type, attr_len;                                                                 \
            Octstr *attr_val = NULL;                                                                 \
            Octstr *attr_str = NULL;                                                                 \
            attr_type = parse_get_char(*context);                                                    \
            attr_len = parse_get_char(*context);                                                     \
            fields                                                                                   \
            {                                                                                        \
                debug("radius.unpack", 0, "RADIUS: Unknown attribute type (0x%03lx) "                \
                      "len %d in PDU `%s'.",                                                         \
                        (long unsigned int)attr_type, attr_len, (*pdu)->type_name);                  \
                parse_skip(*context, attr_len - 2);                                                  \
            }                                                                                        \
        }                                                                                          
    #include "radius_attributes.def"
}

RADIUS_PDU *radius_pdu_unpack(Octstr *data_without_len)
{
    RADIUS_PDU *pdu;
    int type, ident;
    long len, pos;
    ParseContext *context;
    Octstr *authenticator; 

    len = octstr_len(data_without_len);

    if (len < 20) {
        error(0, "RADIUS: PDU was too short (%ld bytes).",
              octstr_len(data_without_len));
        return NULL;
    }

    context = parse_context_create(data_without_len);

    type = parse_get_char(context);
    ident = parse_get_char(context);
    pdu = radius_pdu_create(type, NULL);
    if (pdu == NULL)
        return NULL;

    len = decode_integer(data_without_len, 2, 2) - 19;
    parse_skip(context, 2);
    debug("radius", 0, "RADIUS: Attributes len is %ld", len);

    authenticator = parse_get_octets(context, 16);
    octstr_dump_short(authenticator, 0, "RADIUS: Authenticator (md5) is:");

    /* skipping back to context start for macro magic */
    parse_context_destroy(context);
    context = parse_context_create(data_without_len);

    switch (type) {
    #define INTEGER(name, octets) \
        pos = octstr_len(data_without_len) - parse_octets_left(context); \
    	p->name = decode_integer(data_without_len, pos, octets); \
        parse_skip(context, octets);
    #define OCTETS(name, field_giving_octets) \
        p->name = parse_get_octets(context, field_giving_octets); 
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields; \
                   radius_attr_unpack(&context, &pdu); } break;
    #include "radius_pdu.def"
    default:
    	error(0, "Unknown RADIUS_PDU type, internal error while unpacking.");
    }

    parse_context_destroy(context);
    octstr_destroy(authenticator);

    return pdu;
}

int radius_authenticate_pdu(RADIUS_PDU *pdu, Octstr **data, Octstr *secret)
{
    int rc = 0;
    Octstr *stream; 
    Octstr *attributes;
    Octstr *digest;

    stream = attributes = digest = NULL;

    /* first extract attributes from raw data, where
     * the first 20 octets are code, idendifier, length
     * and authenticator value as described in RFC2866, sec. 3 */
    if (octstr_len(*data) > 20)
        attributes = octstr_copy(*data, 20, octstr_len(*data)-20);
  
    switch (pdu->type) {
        case 0x04:  /* Accounting-Request, see RFC2866, page 6 */
            stream = octstr_copy(*data, 0, 4);
            octstr_append_data(stream, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);
            octstr_append(stream, attributes);
            octstr_append(stream, secret);
            digest = md5(stream);
            rc = octstr_compare(pdu->u.Accounting_Request.authenticator, 
                                digest) == 0 ? 1 : 0;
            break;
        case 0x05:  /* Accounting-Response, create Response authenticator */
            stream = octstr_duplicate(*data);
            octstr_append(stream, secret);
            digest = md5(stream);
            octstr_delete(*data, 4, 16);
            octstr_insert(*data, digest, 4);
            break;
        default:
            break;
    }

    octstr_destroy(attributes);
    octstr_destroy(stream);
    octstr_destroy(digest);

    return rc;
}

static void radius_attr_dump(RADIUS_PDU *pdu)
{
    #define UNASSIGNED(attr)
    #define ATTR(atype, type, string, pmin, pmax)  \
        id = atype; \
        key = octstr_create(string); \
        val = dict_get(pdu->attr, key); \
        if (val != NULL) \
            octstr_dump_short(val, 2, #atype); \
        octstr_destroy(key);
    #define ATTRIBUTES(fields) \
	if (pdu->attr != NULL) { \
	    Octstr *key = NULL, *val = NULL; \
        int id; \
        fields \
	}
    #include "radius_attributes.def"
}

void radius_pdu_dump(RADIUS_PDU *pdu)
{
    debug("radius", 0, "RADIUS PDU %p dump:", (void *) pdu);
    debug("radius", 0, "  type_name: %s", pdu->type_name);
    switch (pdu->type) {
    #define INTEGER(name, octets) \
    	debug("radius", 0, "  %s: %lu = 0x%08lx", #name, p->name, p->name);
    #define OCTETS(name, field_giving_octets) \
        octstr_dump_short(p->name, 2, #name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields; \
                   radius_attr_dump(pdu); } break;
    #include "radius_pdu.def"
    default:
    	error(0, "Unknown RADIUS_PDU type, internal error.");
	break;
    }
    debug("radius", 0, "RADIUS PDU dump ends.");
}

Octstr *radius_get_attribute(RADIUS_PDU *pdu, Octstr *attribute)
{
    gw_assert(pdu != NULL);
    
    if (pdu->attr == NULL)
        return NULL;

    return dict_get(pdu->attr, attribute);
}

