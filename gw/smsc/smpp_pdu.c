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
 * smpp_pdu.c - parse and generate SMPP PDUs
 *
 * Lars Wirzenius
 * Alexander Malysh <amalysh@kannel.org>:
 *     Extended optional parameters implementation.
 */


#include <string.h>
#include "smpp_pdu.h"

#define MIN_SMPP_PDU_LEN    (4*4)
/* old value was (1024). We need more because message_payload can be up to 64K octets*/
#define MAX_SMPP_PDU_LEN    (7424)
/* we use ; in the middle because ; is split char in smsc-id and can't be in the smsc-id */
#define DEFAULT_SMSC_ID "def;ault"

struct smpp_tlv {
    Octstr *name;
    long tag;
    long length;
    enum { SMPP_TLV_OCTETS = 0, SMPP_TLV_NULTERMINATED = 1, SMPP_TLV_INTEGER = 2 } type;
};

/* Dict(smsc_id, Dict(tag, tlv)) */
static Dict *tlvs_by_tag;
/* Dict(smsc_id, Dict(tag_name, tlv)) */
static Dict *tlvs_by_name;
static List *tlvs;
static int initialized;


static void smpp_tlv_destroy(struct smpp_tlv *tlv)
{
    if (tlv == NULL)
        return;
    octstr_destroy(tlv->name);
    gw_free(tlv);
}

static struct smpp_tlv *smpp_tlv_get_by_name(Octstr *smsc_id, Octstr *name)
{
    struct smpp_tlv *res = NULL;
    Dict *tmp_dict;

    if (tlvs_by_name == NULL || name == NULL)
        return NULL;

    if (smsc_id != NULL) {
        tmp_dict = dict_get(tlvs_by_name, smsc_id);
        if (tmp_dict != NULL)
            res = dict_get(tmp_dict, name);
    }
    if (res == NULL) {
        /* try default smsc_id */
        smsc_id = octstr_imm(DEFAULT_SMSC_ID);
        tmp_dict = dict_get(tlvs_by_name, smsc_id);
        if (tmp_dict != NULL)
            res = dict_get(tmp_dict, name);
    }
    return res;
}

static struct smpp_tlv *smpp_tlv_get_by_tag(Octstr *smsc_id, long tag)
{
    struct smpp_tlv *res = NULL;
    Dict *tmp_dict;
    Octstr *tmp;

    if (tlvs_by_tag == NULL)
        return NULL;

    tmp = octstr_format("%ld", tag);

    if (smsc_id != NULL) {
        tmp_dict = dict_get(tlvs_by_tag, smsc_id);
        if (tmp_dict != NULL)
            res = dict_get(tmp_dict, tmp);
    }
    if (res == NULL) {
        /* try default smsc_id */
        smsc_id = octstr_imm(DEFAULT_SMSC_ID);
        tmp_dict = dict_get(tlvs_by_tag, smsc_id);
        if (tmp_dict != NULL)
            res = dict_get(tmp_dict, tmp);
    }

    octstr_destroy(tmp);

    return res;
}

int smpp_pdu_init(Cfg *cfg)
{
    CfgGroup *grp;
    List *l;

    if (initialized)
        return 0;

    l = cfg_get_multi_group(cfg, octstr_imm("smpp-tlv"));
    tlvs = gwlist_create();
    tlvs_by_tag = dict_create(1024, (void(*)(void*))dict_destroy);
    tlvs_by_name = dict_create(1024, (void(*)(void*))dict_destroy);
    while (l != NULL && (grp = gwlist_extract_first(l)) != NULL) {
        struct smpp_tlv *tlv;
        Octstr *tmp, *smsc_id;
        List *l2;

        tlv = gw_malloc(sizeof(*tlv));
        if ((tlv->name = cfg_get(grp, octstr_imm("name"))) == NULL) {
            error(0, "SMPP: Unable to get name for smpp-tlv.");
            smpp_tlv_destroy(tlv);
            goto failed;
        }
        if (cfg_get_integer(&tlv->tag, grp, octstr_imm("tag")) == -1) {
            error(0, "SMPP: Unable to get tag for smpp-tlv.");
            smpp_tlv_destroy(tlv);
            goto failed;
        }
        if (cfg_get_integer(&tlv->length, grp, octstr_imm("length")) == -1) {
            error(0, "SMPP: Unable to get length for smpp-tlv.");
            smpp_tlv_destroy(tlv);
            goto failed;
        }
        if ((tmp = cfg_get(grp, octstr_imm("type"))) == NULL) {
            error(0, "SMPP: Unable to get type for smpp-tlv.");
            smpp_tlv_destroy(tlv);
            goto failed;
        }
        if (octstr_str_case_compare(tmp, "octetstring") == 0)
            tlv->type = SMPP_TLV_OCTETS;
        else if (octstr_str_case_compare(tmp, "nulterminated") == 0)
            tlv->type = SMPP_TLV_NULTERMINATED;
        else if (octstr_str_case_compare(tmp, "integer") == 0)
            tlv->type = SMPP_TLV_INTEGER;
        else {
            error(0, "SMPP: Unknown type for smpp-tlv: `%s'", octstr_get_cstr(tmp));
            octstr_destroy(tmp);
            smpp_tlv_destroy(tlv);
            goto failed;
        }
        octstr_destroy(tmp);

        /* put to all TLVs */
        gwlist_produce(tlvs, tlv);

        smsc_id = cfg_get(grp, octstr_imm("smsc-id"));
        if (smsc_id != NULL) {
            l2 = octstr_split(smsc_id, octstr_imm(";"));
            octstr_destroy(smsc_id);
        } else {
            l2 = gwlist_create();
            gwlist_produce(l2, octstr_create(DEFAULT_SMSC_ID));
        }
        while(l2 != NULL && (smsc_id = gwlist_extract_first(l2)) != NULL) {
            Dict *tmp_dict;

            debug("sms.smpp", 0, "adding smpp-tlv for smsc-id=%s", octstr_get_cstr(smsc_id));

            tmp_dict = dict_get(tlvs_by_name, smsc_id);
            if (tmp_dict == NULL) {
                tmp_dict = dict_create(1024, NULL);
                dict_put(tlvs_by_name, smsc_id, tmp_dict);
            }
            /* put into dict */
            if (!dict_put_once(tmp_dict, tlv->name, tlv)) {
                error(0, "SMPP: Double TLV name %s found.", octstr_get_cstr(tlv->name));
                octstr_destroy(smsc_id);
                goto failed;
            }

            tmp_dict = dict_get(tlvs_by_tag, smsc_id);
            if (tmp_dict == NULL) {
                tmp_dict = dict_create(1024, NULL);
                dict_put(tlvs_by_tag, smsc_id, tmp_dict);
            }
            tmp = octstr_format("%ld", tlv->tag);
            if (!dict_put_once(tmp_dict, tmp, tlv)) {
                error(0, "SMPP: Double TLV tag %s found.", octstr_get_cstr(tmp));
                gwlist_destroy(l2, octstr_destroy_item);
                octstr_destroy(tmp);
                octstr_destroy(smsc_id);
                goto failed;
            }
            octstr_destroy(tmp);
            octstr_destroy(smsc_id);
        }
        gwlist_destroy(l2, octstr_destroy_item);
    }
    gwlist_destroy(l, NULL);

    initialized = 1;
    return 0;

failed:
    gwlist_destroy(tlvs, (void(*)(void*))smpp_tlv_destroy);
    dict_destroy(tlvs_by_tag);
    dict_destroy(tlvs_by_name);
    return -1;
}


int smpp_pdu_shutdown(void)
{
    if (initialized == 0)
        return 0;

    initialized = 0;
    gwlist_destroy(tlvs, (void(*)(void*))smpp_tlv_destroy);
    tlvs = NULL;
    dict_destroy(tlvs_by_tag);
    dict_destroy(tlvs_by_name);
    tlvs_by_tag = tlvs_by_name = NULL;

    return 0;
}


static long decode_integer(Octstr *os, long pos, int octets)
{
    unsigned long u;
    int i;

    if (octstr_len(os) < pos + octets) 
        return -1;

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


static int copy_until_nul(const char *field_name, Octstr *os, long *pos, long max_octets, Octstr **data)
{
    long nul;

    *data = NULL;

    nul = octstr_search_char(os, '\0', *pos);
    if (nul == -1) {
        warning(0, "SMPP: PDU NULL terminated string (%s) has no NULL.", field_name);
        return -1;
    }
    if (*pos + max_octets < nul) {
        error(0, "SMPP: PDU NULL terminated string (%s) longer than allowed.", field_name);
        return -1;
    }
    *data = (nul - *pos > 0) ? octstr_copy(os, *pos, nul - *pos) : NULL;
    *pos = nul + 1;
    return 0;
}


SMPP_PDU *smpp_pdu_create(unsigned long type, unsigned long seq_no)
{
    SMPP_PDU *pdu;

    pdu = gw_malloc(sizeof(*pdu));
    pdu->type = type;

    switch (type) {
    #define OPTIONAL_BEGIN
    #define TLV_INTEGER(name, octets) p->name = -1;
    #define TLV_NULTERMINATED(name, max_len) p->name = NULL;
    #define TLV_OCTETS(name, min_len, max_len) p->name = NULL;
    #define OPTIONAL_END p->tlv = dict_create(1024, octstr_destroy_item);
    #define INTEGER(name, octets) p->name = 0;
    #define NULTERMINATED(name, max_octets) p->name = NULL;
    #define OCTETS(name, field_giving_octetst) p->name = NULL;
    #define PDU(name, id, fields) \
        case id: { \
            struct name *p = &pdu->u.name; \
            pdu->type_name = #name; \
            fields \
            p->command_id = type; \
            p->sequence_number = seq_no; \
        } break;
    #include "smpp_pdu.def"
    default:
        error(0, "Unknown SMPP_PDU type, internal error.");
        gw_free(pdu);
        return NULL;
    }

    return pdu;
}

void smpp_pdu_destroy(SMPP_PDU *pdu)
{
    if (pdu == NULL)
        return;

    switch (pdu->type) {
    #define OPTIONAL_BEGIN
    #define TLV_INTEGER(name, octets) p->name = -1;
    #define TLV_NULTERMINATED(name, max_octets) octstr_destroy(p->name);
    #define TLV_OCTETS(name, min_len, max_len) octstr_destroy(p->name);
    #define OPTIONAL_END dict_destroy(p->tlv);
    #define INTEGER(name, octets) p->name = 0; /* Make sure "p" is used */
    #define NULTERMINATED(name, max_octets) octstr_destroy(p->name);
    #define OCTETS(name, field_giving_octets) octstr_destroy(p->name);
    #define PDU(name, id, fields) \
        case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
        error(0, "Unknown SMPP_PDU type, internal error while destroying.");
    }
    gw_free(pdu);
}


Octstr *smpp_pdu_pack(Octstr *smsc_id, SMPP_PDU *pdu)
{
    Octstr *os;
    Octstr *temp;

    os = octstr_create("");

    gw_assert(pdu != NULL);

    /*
     * Fix lengths of octet string fields.
     */
    switch (pdu->type) {
    #define OPTIONAL_BEGIN
    #define TLV_INTEGER(name, octets)
    #define TLV_NULTERMINATED(name, max_len)
    #define TLV_OCTETS(name, min_len, max_len)
    #define OPTIONAL_END
    #define INTEGER(name, octets) p = *(&p);
    #define NULTERMINATED(name, max_octets) p = *(&p);
    #define OCTETS(name, field_giving_octets) \
    	p->field_giving_octets = octstr_len(p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	error(0, "Unknown SMPP_PDU type, internal error while packing.");
    }

    switch (pdu->type) {
    #define TL(name, octets) \
        append_encoded_integer(os, SMPP_##name, 2); \
        append_encoded_integer(os, octets, 2);
    #define OPTIONAL_BEGIN
    #define TLV_INTEGER(name, octets) \
        if (p->name >= 0) { \
            TL(name, octets); \
            INTEGER(name, octets) \
        }
    #define TLV_NULTERMINATED(name, max_len) \
        if (p->name != NULL) { \
            TL(name, (octstr_len(p->name) > max_len ? max_len : octstr_len(p->name) + 1)); \
            NULTERMINATED(name, max_len) \
        }
    #define TLV_OCTETS(name, min_len, max_len) \
        if (p->name != NULL) { \
            unsigned long len = octstr_len(p->name); \
            if (len > max_len || len < min_len) { \
                error(0, "SMPP: Optional field (%s) with invalid length (%ld) (should be %d - %d) dropped.", \
                    #name, len, min_len, max_len);\
            } else { \
                TL(name, len); \
                octstr_append(os, p->name); \
            } \
        }
    #define OPTIONAL_END \
        if (p->tlv != NULL) { \
            Octstr *key; \
            List *keys; \
            struct smpp_tlv *tlv; \
            keys = dict_keys(p->tlv); \
            while(keys != NULL && (key = gwlist_extract_first(keys)) != NULL) { \
                tlv = smpp_tlv_get_by_name(smsc_id, key); \
                if (tlv == NULL) { \
                    error(0, "SMPP: Unknown TLV `%s', don't send.", octstr_get_cstr(key)); \
                    octstr_destroy(key); \
                    continue; \
                } \
                switch(tlv->type) { \
                case SMPP_TLV_INTEGER: { \
                    long val = atol(octstr_get_cstr(dict_get(p->tlv, key))); \
                    append_encoded_integer(os, tlv->tag, 2); \
                    append_encoded_integer(os, tlv->length, 2); \
                    append_encoded_integer(os, val, tlv->length); \
                    break; \
                } \
                case SMPP_TLV_OCTETS: \
                case SMPP_TLV_NULTERMINATED: { \
                    Octstr *val = dict_get(p->tlv, key); \
                    unsigned long len = octstr_len(val); \
                    if (len > tlv->length) { \
                        error(0, "SMPP: Optional field (%s) with invalid length (%ld) (should be %ld) dropped.", \
                              octstr_get_cstr(key), len, tlv->length);\
                        octstr_destroy(key); \
                        continue; \
                    } \
                    append_encoded_integer(os, tlv->tag, 2); \
                    if (tlv->type == SMPP_TLV_NULTERMINATED) \
                        append_encoded_integer(os, len + 1, 2); \
                    else \
                        append_encoded_integer(os, len, 2); \
                    octstr_append(os, val); \
                    if (tlv->type == SMPP_TLV_NULTERMINATED) \
                        octstr_append_char(os, '\0'); \
                    break; \
                } \
                default: \
                    panic(0, "SMPP: Internal error, unknown configured TLV type %d.", tlv->type); \
                    break; \
                } \
                octstr_destroy(key); \
            } \
            gwlist_destroy(keys, octstr_destroy_item); \
        }
    #define INTEGER(name, octets) \
        append_encoded_integer(os, p->name, octets);
    #define NULTERMINATED(name, max_octets) \
        if (p->name != NULL) { \
            if (octstr_len(p->name) >= max_octets) { \
                warning(0, "SMPP: PDU element <%s> too long " \
                        "(length is %ld, should be %d)", \
                        #name, octstr_len(p->name), max_octets-1); \
                temp = octstr_copy(p->name, 0, max_octets-1); \
            } else \
                temp = octstr_duplicate(p->name); \
            octstr_append(os, temp); \
            octstr_destroy(temp); \
        } \
        octstr_append_char(os, '\0');
    #define OCTETS(name, field_giving_octets) \
        if (p->name) octstr_append(os, p->name);
    #define PDU(name, id, fields) \
        case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
        error(0, "Unknown SMPP_PDU type 0x%08lx, internal error while packing.", pdu->type);
        break;
    }

    temp = octstr_create("");
    append_encoded_integer(temp, octstr_len(os) + 4, 4);
    octstr_insert(os, temp, 0);
    octstr_destroy(temp);

    return os;
}


SMPP_PDU *smpp_pdu_unpack(Octstr *smsc_id, Octstr *data_without_len)
{
    SMPP_PDU *pdu;
    unsigned long type;
    long len, pos;

    len = octstr_len(data_without_len);

    if (len < 4) {
        error(0, "SMPP: PDU was too short (%ld bytes).",
              octstr_len(data_without_len));
        return NULL;
    }

    /* get the PDU type */
    if ((type = decode_integer(data_without_len, 0, 4)) == -1)
        return NULL;

    /* create a coresponding representation structure */
    pdu = smpp_pdu_create(type, 0);
    if (pdu == NULL)
        return NULL;

    pos = 0;

    switch (type) {
    #define OPTIONAL_BEGIN  \
        {   /* Read optional parameters */  \
            while (pos + 4 <= len) { \
                struct smpp_tlv *tlv; \
                unsigned long opt_tag, opt_len; \
                opt_tag = decode_integer(data_without_len, pos, 2); pos += 2; \
                debug("sms.smpp", 0, "Optional parameter tag (0x%04lx)", opt_tag);  \
                opt_len = decode_integer(data_without_len, pos, 2); pos += 2;  \
                debug("sms.smpp", 0, "Optional parameter length read as %ld", opt_len); \
                /* check configured TLVs */ \
                tlv = smpp_tlv_get_by_tag(smsc_id, opt_tag); \
                if (tlv != NULL) debug("sms.smpp", 0, "Found configured optional parameter `%s'", octstr_get_cstr(tlv->name));
    #define TLV_INTEGER(mname, octets) \
                if (SMPP_##mname == opt_tag) { \
                    /* check length */ \
                    if (opt_len > octets) { \
                        error(0, "SMPP: Optional field (%s) with invalid length (%ld) dropped.", #mname, opt_len); \
                        pos += opt_len; \
                        continue; \
                    } \
                    INTEGER(mname, opt_len); \
                    if (tlv != NULL) dict_put(p->tlv, tlv->name, octstr_format("%ld", p->mname)); \
                } else
    #define TLV_NULTERMINATED(mname, max_len) \
                if (SMPP_##mname == opt_tag) { \
                    /* check length */ \
                    if (opt_len > max_len || pos+opt_len > len) { \
                        error(0, "SMPP: Optional field (%s) with invalid length (%ld) dropped.", #mname, opt_len);  \
                        pos += opt_len; \
                        continue; \
                    } \
                    copy_until_nul(#mname, data_without_len, &pos, opt_len, &p->mname); \
                    if (tlv != NULL) dict_put(p->tlv, tlv->name, octstr_duplicate(p->mname)); \
                } else
    #define TLV_OCTETS(mname, min_len, max_len) \
                if (SMPP_##mname == opt_tag) { \
                    /* check length */ \
                    if (opt_len < min_len || opt_len > max_len || pos + opt_len > len) { \
                        error(0, "SMPP: Optional field (%s) with invalid length (%ld) (should be %d - %d) dropped.", \
                            #mname, opt_len, min_len, max_len);  \
                        pos += opt_len; \
                        continue; \
                    } \
                    p->mname = octstr_copy(data_without_len, pos, opt_len); \
                    pos += opt_len; \
                    if (tlv != NULL) dict_put(p->tlv, tlv->name, octstr_duplicate(p->mname)); \
                } else
    #define OPTIONAL_END \
                { \
                    Octstr *val = NULL; \
                    if (tlv != NULL) { \
                        /* found configured tlv */ \
                        /* check length */ \
                        if (opt_len > tlv->length) { \
                            error(0, "SMPP: Optional field (%s) with invalid length (%ld) (should be %ld) dropped.", \
                                  octstr_get_cstr(tlv->name), opt_len, tlv->length); \
                            pos += opt_len; \
                            continue; \
                        } \
                        switch (tlv->type) { \
                        case SMPP_TLV_INTEGER: { \
                            long val_i; \
                            if ((val_i = decode_integer(data_without_len, pos, opt_len)) == -1) \
                                goto err; \
                            val = octstr_format("%ld", val_i); \
                            dict_put(p->tlv, tlv->name, val); \
                            pos += opt_len; \
                            break; \
                        } \
                        case SMPP_TLV_OCTETS: { \
                            val = octstr_copy(data_without_len, pos, opt_len); \
                            dict_put(p->tlv, tlv->name, val); \
                            pos += opt_len; \
                            break; \
                        } \
                        case SMPP_TLV_NULTERMINATED: { \
                            if (copy_until_nul(octstr_get_cstr(tlv->name), data_without_len, &pos, opt_len, &val) == 0) \
                                dict_put(p->tlv, tlv->name, val); \
                            break; \
                        } \
                        default: \
                            panic(0, "SMPP: Internal error, unknown configured TLV type %d.", tlv->type); \
                            break; \
                        } \
                    }  else { \
                        val = octstr_copy(data_without_len, pos, opt_len); \
                        if (val) \
                            octstr_binary_to_hex(val, 0); \
                        else \
                            val = octstr_create(""); \
                        warning(0, "SMPP: Unknown TLV(0x%04lx,0x%04lx,%s) for PDU type (%s) received!", \
                            opt_tag, opt_len, octstr_get_cstr(val), pdu->type_name); \
                        octstr_destroy(val); \
                        pos += opt_len; \
                    } \
                } \
            } \
        }
    #define INTEGER(name, octets) \
        if ((p->name = decode_integer(data_without_len, pos, octets)) == -1) \
            goto err; \
        pos += octets;
    #define NULTERMINATED(name, max_octets) \
        /* just warn about errors but not fail */ \
        copy_until_nul(#name, data_without_len, &pos, max_octets, &p->name);
    #define OCTETS(name, field_giving_octets) \
    	p->name = octstr_copy(data_without_len, pos, \
	    	    	      p->field_giving_octets); \
        if (p->field_giving_octets != (unsigned long) octstr_len(p->name)) { \
            error(0, "smpp_pdu: error while unpacking '" #name "', " \
                     "len is %ld but should have been %ld, dropping.", \
                     octstr_len(p->name), p->field_giving_octets); \
            goto err; \
        } else { \
            pos += p->field_giving_octets; \
        }
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	error(0, "Unknown SMPP_PDU type 0x%08lx, internal error while unpacking.", type);
    	break;
    }

    return pdu;
    
err:
    smpp_pdu_destroy(pdu);
    octstr_dump(data_without_len, 0);
    return NULL;
}


void smpp_pdu_dump(Octstr *smsc_id, SMPP_PDU *pdu)
{
    debug("sms.smpp", 0, "SMPP PDU %p dump:", (void *) pdu);
    debug("sms.smpp", 0, "  type_name: %s", pdu->type_name);
    switch (pdu->type) {
    #define OPTIONAL_BEGIN
    #define TLV_INTEGER(name, max_len) \
        if (p->name != -1)  { \
            INTEGER(name, max_len) \
        }
    #define TLV_NULTERMINATED(name, max_len) \
        if (p->name != NULL) { \
            NULTERMINATED(name, max_len) \
        }
    #define TLV_OCTETS(name, min_len, max_len) \
        if (p->name != NULL) { \
            OCTETS(name, max_len) \
        }
    #define OPTIONAL_END \
        if (p->tlv != NULL) { \
            List *keys; \
            Octstr *key; \
            struct smpp_tlv *tlv; \
            keys = dict_keys(p->tlv); \
            while(keys != NULL && (key = gwlist_extract_first(keys)) != NULL) { \
                tlv = smpp_tlv_get_by_name(smsc_id, key); \
                if (tlv != NULL) { \
                    octstr_dump_short(dict_get(p->tlv, key), 2, octstr_get_cstr(key)); \
                } \
                octstr_destroy(key); \
            } \
            gwlist_destroy(keys, octstr_destroy_item); \
        }
    #define INTEGER(name, octets) \
        debug("sms.smpp", 0, "  %s: %lu = 0x%08lx", #name, p->name, p->name);
    #define NULTERMINATED(name, max_octets) \
        octstr_dump_short(p->name, 2, #name);
    #define OCTETS(name, field_giving_octets) \
        octstr_dump_short(p->name, 2, #name);
    #define PDU(name, id, fields) \
        case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
        error(0, "Unknown SMPP_PDU type, internal error.");
        break;
    }
    debug("sms.smpp", 0, "SMPP PDU dump ends.");
}


long smpp_pdu_read_len(Connection *conn)
{
    Octstr *os;
    unsigned char buf[4];    /* The length is 4 octets. */
    long len;

    os = conn_read_fixed(conn, sizeof(buf));
    if (os == NULL)
    	return 0;
    octstr_get_many_chars((char*) buf, os, 0, sizeof(buf));
    octstr_destroy(os);
    len = decode_network_long(buf);
    if (len < MIN_SMPP_PDU_LEN) {
	error(0, "SMPP: PDU length was too small (%ld, minimum is %ld).",
	      len, (long) MIN_SMPP_PDU_LEN);
    	return -1;
    }
    if (len > MAX_SMPP_PDU_LEN) {
	error(0, "SMPP: PDU length was too large (%ld, maximum is %ld).",
	      len, (long) MAX_SMPP_PDU_LEN);
    	return -1;
    }
    return len;
}


Octstr *smpp_pdu_read_data(Connection *conn, long len)
{
    Octstr *os;

    os = conn_read_fixed(conn, len - 4);    /* `len' includes itself. */
    return os;
}


/*
 * Return error string for given error code
 * NOTE: If you add new error strings here please use
 *       error strings from SMPP spec. and please keep
 *       error codes in switch statement sorted by error
 *       code ID.
 */
const char *smpp_error_to_string(enum SMPP_ERROR_MESSAGES error)
{
    switch (error) {
        case SMPP_ESME_ROK:
            return "OK";
        case SMPP_ESME_RINVMSGLEN:
            return "Message Length is invalid";
        case SMPP_ESME_RINVCMDLEN:
            return "Command Length is invalid";
        case SMPP_ESME_RINVCMDID:
            return "Invalid Command ID";
        case SMPP_ESME_RINVBNDSTS:
            return "Incorrect BIND Status for given command";
        case SMPP_ESME_RALYNBD:
            return "ESME Already in Bound State";
        case SMPP_ESME_RINVPRTFLG:
            return "Invalid Priority Flag";
        case SMPP_ESME_RINVREGDLVFLG:
            return "Invalid Registered Delivery Flag";
        case SMPP_ESME_RSYSERR:
            return "System Error";
        case SMPP_ESME_RINVSRCADR:
            return "Invalid Source Address";
        case SMPP_ESME_RINVDSTADR:
            return "Invalid Destination Address";
        case SMPP_ESME_RBINDFAIL:
            return "Bind Failed";
        case SMPP_ESME_RINVPASWD:
            return "Invalid Password";
        case SMPP_ESME_RINVSYSID:
            return "Invalid System ID";
        case SMPP_ESME_RCANCELFAIL:
            return "Cancel SM Failed";
        case SMPP_ESME_RREPLACEFAIL:
            return "Replace SM Failed";
        case SMPP_ESME_RMSGQFUL:
            return "Message Queue Full";
        case SMPP_ESME_RINVSERTYP:
            return "Invalid Service Type";
        case SMPP_ESME_RINVNUMDESTS:
            return "Invalid number of destinations";
        case SMPP_ESME_RINVDLNAME:
            return "Invalid Distribution List Name";
        case SMPP_ESME_RINVDESTFLAG:
            return "Destination flag is invalid";
        case SMPP_ESME_RINVSUBREP:
            return "Submit w/replace not supported/allowed";
        case SMPP_ESME_RINVESMCLASS:
            return "Invalid esm_class field data";
        case SMPP_ESME_RCNTSUBDL:
            return "Cannot Submit to Distribution List";
        case SMPP_ESME_RSUBMITFAIL:
            return "Submit failed";
        case SMPP_ESME_RINVSRCTON:
            return "Invalid Source address TON"; 
        case SMPP_ESME_RINVSRCNPI:
            return "Invalid Source address NPI";
        case SMPP_ESME_RINVDSTTON:
            return "Invalid Destination address TON";
        case SMPP_ESME_RINVDSTNPI:
            return "Invalid Destination address NPI";
        case SMPP_ESME_RINVSYSTYP:
            return "Invalid system_type field";
        case SMPP_ESME_RINVREPFLAG:
            return "Invalid replace_if_present flag";
        case SMPP_ESME_RINVNUMMSGS:
            return "Invalid number of messages";
        case SMPP_ESME_RTHROTTLED:
            return "Throttling error";
        case SMPP_ESME_RINVSCHED:
            return "Invalid Scheduled Delivery Time";
        case SMPP_ESME_RINVEXPIRY:
            return "Invalid message validity period";
        case SMPP_ESME_RINVDFTMSGID:
            return "Predefined Message ID is Invalid or specific predefined message was not found";
        case SMPP_ESME_RX_T_APPN:
            return "ESME Receiver Temporary App Error Code";
        case SMPP_ESME_RX_P_APPN:
            return "ESME Receiver Permanent App Error Code";
        case SMPP_ESME_RX_R_APPN:
            return "ESME Receiver Reject Message Error Code";
        case SMPP_ESME_RQUERYFAIL:
            return "query_sm request failed";
        case SMPP_ESME_RINVTLVSTREAM:
            return "Error in optional part of the PDU Body";
        case SMPP_ESME_RTLVNOTALLWD:
            return "TLV not allowed";
        case SMPP_ESME_RINVTLVLEN:
            return "Invalid Parameter Length";
        case SMPP_ESME_RMISSINGTLV:
            return "Expected TLV missing";
        case SMPP_ESME_RINVTLVVAL:
            return "Invalid TLV value";
        case SMPP_ESME_RDELIVERYFAILURE:
            return "Transaction Delivery Failure";
        case SMPP_ESME_RUNKNOWNERR:
            return "Unknown Error";
        case SMPP_ESME_RSERTYPUNAUTH:
            return "ESME Not authorized to use specified service_type";
        case SMPP_ESME_RPROHIBITED:
            return "ESME Prohibited from using specified operation";
        case SMPP_ESME_RSERTYPUNAVAIL:
            return "Specified service_type is unavailable";
        case SMPP_ESME_RSERTYPDENIED:
            return "Specified service_type is denied";
        case SMPP_ESME_RINVDCS:
            return "Invalid Data Coding Scheme";
        case SMPP_ESME_RINVSRCADDRSUBUNIT:
            return "Source Address Sub unit is invalid";
        case SMPP_ESME_RINVDSTADDRSUBUNIT:
            return "Destination Address Sub unit is invalid";
        case SMPP_ESME_RINVBCASTFREQINT:
            return "Broadcast Frequency Interval is invalid";
        case SMPP_ESME_RINVBCASTALIAS_NAME:
            return "Broadcast Alias Name is invalid";
        case SMPP_ESME_RINVBCASTAREAFMT:
            return "Broadcast Area Format is invalid";
        case SMPP_ESME_RINVNUMBCAST_AREAS:
            return "Number of Broadcast Areas is invalid";
        case SMPP_ESME_RINVBCASTCNTTYPE:
            return "Broadcast Content Type is invalid";
        case SMPP_ESME_RINVBCASTMSGCLASS:
            return "Broadcast Message Class is invalid";
        case SMPP_ESME_RBCASTFAIL:
            return "broadcast_sm operation failed";
        case SMPP_ESME_RBCASTQUERYFAIL:
            return "broadcast_query_sm operation failed";
        case SMPP_ESME_RBCASTCANCELFAIL:
            return "broadcast_cancel_sm operation failed";
        case SMPP_ESME_RINVBCAST_REP:
            return "Number of Repeated Broadcasts is invalid";
        case SMPP_ESME_RINVBCASTSRVGRP:
            return "Broadcast Service Group is invalid";
        case SMPP_ESME_RINVBCASTCHANIND:
            return "Broadcast Channel Indicator is invalid";

        default:
            /* tell the user that we have a vendor-specific beast here */
            if (error >= 0x0400 && error <= 0x04FF)
                return "Vendor-specific error, please refer to your SMPP provider";
            else
                return "Unknown/Reserved";
    }
}
