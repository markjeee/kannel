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
 * smsc_smasi.c - parse and generate SMASI PDUs
 *
 * Stipe Tolj <stolj@wapme.de>
 */


#include <string.h>
#include "smasi_pdu.h"


static Octstr *decode_type(Octstr *os)
{
    long p1, p2;
    Octstr *temp;

    if ((p2 = octstr_search_char(os, ':', 0)) == -1)
        return NULL;

    if ((p1 = octstr_search_char(os, '!', 0)) != -1 && p1 < p2) {
        p1++;
    } else {
        p1 = 0;
    }

    temp = octstr_copy(os, p1, p2 - p1);

    return temp;
}


static Octstr *copy_until_coma(Octstr *os, long *pos)
{
    long nul;
    Octstr *data;

    nul = octstr_search_char(os, ',', *pos);
    if (nul == -1) {
        warning(0, "SMASI: Parameter without value or value not properly terminated: %s", octstr_get_cstr(os));
        return NULL;
    }
    data = octstr_copy(os, *pos, nul - *pos);
    *pos = nul + 1;
    return data;
}


static Octstr *copy_until_assign(Octstr *os, long *pos) 
{
    long nul;
    Octstr *data;

    nul = octstr_search_char(os, '=', *pos);
    if (nul == -1 && (octstr_len(os) - *pos) > 1) {
        warning(0, "SMASI: Garbage at end of parameter list: %s", octstr_get_cstr(os));
        *pos = octstr_len(os);
        return NULL;
    }
    data = octstr_copy(os, *pos, nul - *pos);
    *pos = nul + 1;
    return data;
}


static void skip_until_after_colon(Octstr *os, long *pos) 
{
    long colon = octstr_search_char(os, ':', *pos);

    if (colon == -1) warning(0, "SMASI: No colon after SMASI PDU name: %s", octstr_get_cstr(os));

    *pos = colon + 1;

}


SMASI_PDU *smasi_pdu_create(unsigned long type) 
{
    SMASI_PDU *pdu;

    pdu = gw_malloc(sizeof(*pdu));
    pdu->type = type;

    switch (type) {
    #define NONTERMINATED(name) p->name = NULL;
    #define COMATERMINATED(name) p->name = NULL;
    #define PDU(name, id, fields) \
        case id: { \
        struct name *p = &pdu->u.name; \
        pdu->type_name = #name; \
        pdu->needs_hyphen = (id < SMASI_HYPHEN_ID ? 1 : 0); \
        fields \
    } break;
    #include "smasi_pdu.def"
    default:
        warning(0, "Unknown SMASI_PDU type, internal error.");
        gw_free(pdu);
    return NULL;
    }

    return pdu;
}


void smasi_pdu_destroy(SMASI_PDU *pdu)
{
    if (pdu == NULL)
        return;

    switch (pdu->type) {
    #define NONTERMINATED(name) octstr_destroy(p->name);
    #define COMATERMINATED(name) octstr_destroy(p->name);
    #define PDU(name, id, fields) \
        case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smasi_pdu.def"
    default:
        error(0, "Unknown SMASI_PDU type, internal error while destroying.");
    }
    gw_free(pdu);
}


Octstr *smasi_pdu_pack(SMASI_PDU *pdu)
{
    Octstr *os;
    Octstr *temp;

    os = octstr_create("");

    /*
     * Fix lengths of octet string fields.
     */
    switch (pdu->type) {
    #define NONTERMINATED(name) p = *(&p);
    #define COMATERMINATED(name) p = *(&p);
    #define PDU(name, id, fields) \
        case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smasi_pdu.def"
    default:
        error(0, "Unknown SMASI_PDU type, internal error while packing.");
    }

    switch (pdu->type) {
    #define NONTERMINATED(name) p = *(&p);
    #define COMATERMINATED(name) \
    if (p->name != NULL) { octstr_append_cstr(os, #name); \
    octstr_append_char(os, '='); \
    octstr_append(os, p->name); \
    octstr_append_char(os, ','); }
    #define PDU(name, id, fields) \
        case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smasi_pdu.def"
    default:
        error(0, "Unknown SMASI_PDU type, internal error while packing.");
    }

    octstr_append_char(os, '\n');
    temp = pdu->needs_hyphen ? octstr_create("!") : octstr_create("");
    octstr_append_cstr(temp, pdu->type_name); 
    octstr_append_char(temp, ':');
    octstr_insert(os, temp, 0);
    octstr_destroy(temp);

    return os;
}


SMASI_PDU *smasi_pdu_unpack(Octstr *data_without_len)
{
    SMASI_PDU *pdu;
    char *type_name;
    Octstr *temp;
    long pos;
    unsigned long type = 0;

    /* extract the PDU type identifier */
    temp = decode_type(data_without_len);
    type_name = (temp ? octstr_get_cstr(temp) : "");

    if (strcmp(type_name, "dummy") == 0) type = 0;
    #define NONTERMINATED(name) p = *(&p);
    #define COMATERMINATED(name) p = *(&p);
    #define PDU(name, id, fields) \
    else if (strcmp(type_name, #name) == 0) type = id;
    #include "smasi_pdu.def"
    else warning(0, "unknown SMASI PDU type");

    pdu = smasi_pdu_create(type);
    if (pdu == NULL) return NULL;

    pos = 0;
    skip_until_after_colon(data_without_len, &pos);

    switch (type) {
    #define NONTERMINATED(name) p = *(&p);
    #define COMATERMINATED(name) \
                if (octstr_str_compare(field_name, #name) == 0 && field_value != NULL) \
                    p->name = octstr_duplicate(field_value);
    #define PDU(name, id, fields) \
        case id: { \
            Octstr * field_name = NULL; \
            Octstr * field_value = NULL; \
            struct name *p = &pdu->u.name; \
            while (pos < octstr_len(data_without_len)) { \
                field_name = copy_until_assign(data_without_len, &pos); \
                if (field_name == NULL) break; \
                field_value = copy_until_coma(data_without_len, &pos); \
                if (field_value == NULL) continue; \
                fields \
                octstr_destroy(field_name); \
                octstr_destroy(field_value); \
            } \
        } break;
    #include "smasi_pdu.def"
    default:
        warning(0, "Unknown SMASI_PDU type, internal error while unpacking.");
    }

    octstr_destroy(temp);

    return pdu;
}


void smasi_pdu_dump(SMASI_PDU *pdu)
{
    debug("sms.smasi", 0, "SMASI PDU %p dump:", (void *) pdu);
    debug("sms.smasi", 0, "  type_name: %s", pdu->type_name);
    switch (pdu->type) {
    #define NONTERMINATED(name) p = *(&p);
    #define COMATERMINATED(name) \
    octstr_dump_short(p->name, 2, #name);
    #define PDU(name, id, fields) \
        case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smasi_pdu.def"
    default:
        warning(0, "Unknown SMASI_PDU type, internal error.");
    break;
    }
    debug("sms.smasi", 0, "SMASI PDU dump ends.");
}


Octstr *smasi_pdu_read(Connection *conn)
{
    Octstr *line;

    line = conn_read_line(conn);    /* read one line */
    return line;
}
