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
 * wap_push_pap_compiler.c - implementation of wap_push_pap_compiler.h inter-
 * face (compiling pap documents to Kannel WAPEvents)
 *
 * This module implements PAP document DTD and status codes, defined in   
 *       WAP-164-PAP-19991108-a (called hereafter pap), chapter 9 and
 * PPG client addressing (it is. parsing client address of a pap document), 
 * defined in
 *       WAP-151-PPGService-19990816-a (ppg), chapter 7.
 *
 * In addition, Wapforum specification WAP-200-WDP-20001212-a (wdp) is re-
 * ferred.
 *
 * Compiler can be used by PI or PPG (it will handle all possible PAP DTD 
 * elements). It checks that attribute values are legal and that an element 
 * has only legal attributes, but does not otherwise validate PAP documents 
 * against PAP DTD. (XML validation is quite another matter, of course.) 
 * Client address is parsed out from the relevant PAP message attribute 
 * containing lots of additional data, see ppg, 7.1. We do not yet support 
 * user defined addresses.
 *
 * After compiling, some semantic analysing of the resulted event, and sett-
 * ing some defaults (however, relying on them is quite a bad policy). In 
 * addition changing undefined values (any) to defined ones.
 *
 * By  Aarno Syvänen for Wapit Ltd and for Wiral Ltd.
 */

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/debugXML.h>
#include <libxml/encoding.h>
#include <ctype.h>
#include <string.h>

#include "shared.h"
#include "wap_push_pap_compiler.h"
#include "wap_push_ppg.h"

/****************************************************************************
 *
 * Global data structures
 *
 * Table for pap elements. These are defined in PAP, Chapter 9.
 */
static char *pap_elements[] = {
    "pap",
    "push-message",
    "address",
    "quality-of-service",
    "push-response",
    "progress-note",
    "response-result",
    "cancel-message",
    "cancel-result",
    "cancel-response",
    "resultnotification-message",
    "resultnotification-response",
    "statusquery-message",
    "statusquery-response",
    "statusquery-result",
    "ccq-message",
    "ccq-response",
    "badmessage-response"
};

#define NUM_ELEMENTS sizeof(pap_elements)/sizeof(pap_elements[0])

/*
 * Table for PAP attributes. These are defined in pap, Chapter 9.
 */
struct pap_attributes_t {
    char *name;
    char *value;
};

typedef struct pap_attributes_t pap_attributes_t;

static pap_attributes_t pap_attributes[] = {
    { "product-name", NULL },
    { "push-id", NULL },
    { "deliver-before-timestamp", NULL },
    { "deliver-after-timestamp", NULL },
    { "source-reference", NULL },
    { "progress-notes-requested", "true" },
    { "progress-notes-requested", "false" },
    { "ppg-notify-requested-to", NULL },
    { "address-value", NULL },
    { "priority", "high" },
    { "priority", "medium" },
    { "priority", "low" },
    { "delivery-method", "confirmed" },
    { "delivery-method", "preferconfirmed" },
    { "delivery-method", "unconfirmed" },
    { "delivery-method", "notspecified" },
    { "network", NULL },
    { "network-required", "true" },
    { "network-required", "false" },
    { "bearer", NULL },
    { "bearer-required", "true" },
    { "bearer-required", "false" },
    { "sender-address", NULL },
    { "sender-name", NULL },
    { "reply-time", NULL },
    { "stage", NULL },
    { "note", NULL },
    { "time", NULL },
    { "code", NULL },
    { "desc", NULL },
    { "received-time", NULL },
    { "event-time", NULL },
    { "message-state", NULL },
    { "query-id", NULL },
    { "app-id", NULL },
    { "bad-message-fragment", NULL}
};

#define NUM_ATTRIBUTES sizeof(pap_attributes)/sizeof(pap_attributes[0])

/*
 * Status codes are defined in pap, chapter 9.13.
 */
static int pap_codes[] = {
    PAP_ACCEPTED_FOR_PROCESSING,
    PAP_BAD_REQUEST,
    PAP_FORBIDDEN,
    PAP_ADDRESS_ERROR,
    PAP_CAPABILITIES_MISMATCH,
    PAP_DUPLICATE_PUSH_ID,
    PAP_TRANSFORMATION_FAILURE,
    PAP_REQUIRED_BEARER_NOT_AVAILABLE,
    PAP_ABORT_USERPND
};

#define NUM_CODES sizeof(pap_codes)/sizeof(pap_codes[0])

/*
 * Possible bearer types. These are defined in wdp, appendix C.
 */
static char *pap_bearer_types[] = {
    "Any",
    "USSD",
    "SMS",
    "GUTS/R-Data",
    "CSD",
    "Packet Data",
    "GPRS",
    "CDPD",
    "FLEX",
    "SDS",
    "ReFLEX",
    "MPAK",
    "GHOST/R_DATA"
};

#define NUM_BEARER_TYPES sizeof(pap_bearer_types)/sizeof(pap_bearer_types[0])

/*
 * Possible network types. These are defined in wdp, appendix C.
 */

static char *pap_network_types[] = {
    "Any",
    "GSM",
    "ANSI-136",
    "IS-95 CDMA",
    "AMPS",
    "PDC",
    "IDEN",
    "Paging network",
    "PHS",
    "TETRA",
    "Mobitex",
};

#define NUM_NETWORK_TYPES sizeof(pap_network_types)/ \
                          sizeof(pap_network_types[0])

/****************************************************************************
 *
 * Prototypes of internal functions. Note that suffix 'Ptr' means '*'.
 */

static int parse_document(xmlDocPtr doc_p, WAPEvent **e);
static int parse_node(xmlNodePtr node, WAPEvent **e, long *type_of_address,
                      int *is_any);  
static int parse_element(xmlNodePtr node, WAPEvent **e, 
                         long *type_of_address, int *is_any); 
static int parse_attribute(Octstr *element_name, xmlAttrPtr attribute, 
                           WAPEvent **e, long *type_of_address, int *is_any);
static int parse_attr_value(Octstr *element_name, Octstr *attr_name, 
                            Octstr *attr_value, WAPEvent **e,
                            long *type_of_address, int *is_any);
static int set_attribute_value(Octstr *element_name, Octstr *attr_value, 
                               Octstr *attr_name, WAPEvent **e);
static int return_flag(Octstr *ros);
static void wap_event_accept_or_create(Octstr *element_name, WAPEvent **e);
static int parse_pap_value(Octstr *attr_name, Octstr *attr_value, WAPEvent **e);
static int parse_push_message_value(Octstr *attr_name, Octstr *attr_value,
                                     WAPEvent **e);
static int parse_address_value(Octstr *attr_name, Octstr *attr_value, 
                               WAPEvent **e, long *type_of_address);
static int parse_quality_of_service_value(Octstr *attr_name, 
                                          Octstr *attr_value, WAPEvent **e,
                                          int *is_any);
static int parse_push_response_value(Octstr *attr_name, Octstr *attr_value,
                                     WAPEvent **e);
static int parse_progress_note_value(Octstr *attr_name, Octstr *attr_value,
                                     WAPEvent **e);
static int parse_bad_message_response_value(Octstr *attr_name, 
                                            Octstr *attr_value, WAPEvent **e);
static int parse_response_result_value(Octstr *attr_name, 
                                      Octstr *attr_value, WAPEvent **e);
static int parse_code(Octstr *attr_value);
static Octstr *parse_bearer(Octstr *attr_value);
static Octstr *parse_network(Octstr *attr_value);
static int parse_requirement(Octstr *attr_value);
static int parse_priority(Octstr *attr_value);
static int parse_delivery_method(Octstr *attr_value);
static int parse_state(Octstr *attr_value);
static long parse_wappush_client_address(Octstr **address, long pos,
                                         long *type_of_address);
static long parse_ppg_specifier(Octstr **address, long pos);
static long parse_client_specifier(Octstr **address, long pos, 
                                   long *type_of_address);
static long parse_constant(const char *field_name, Octstr **address, long pos);
static long parse_dom_fragment(Octstr **address, long pos);
static long drop_character(Octstr **address, long pos);
static long parse_type(Octstr **address, Octstr **type_value, long pos);
static long parse_ext_qualifiers(Octstr **address, long pos, 
                                 Octstr *type_value);
static long parse_global_phone_number(Octstr **address, long pos);
static long parse_ipv4(Octstr **address, long pos);
static long parse_ipv6(Octstr **address, long pos);
static long parse_escaped_value(Octstr **address, long pos); 
static Octstr *prepend_char(Octstr *address, unsigned char c);
static int qualifiers(Octstr *address, long pos, Octstr *type);
static long parse_qualifier_value(Octstr **address, long pos);
static long parse_qualifier_keyword(Octstr **address, long pos);
static long parse_ipv4_fragment(Octstr **address, long pos);
static long parse_ipv6_fragment(Octstr **address, long pos);
static int wina_bearer_identifier(Octstr *type_value);
static int create_peek_window(Octstr **address, long *pos);
static long rest_unescaped(Octstr **address, long pos);
static int issafe(Octstr **address, long pos);
static long accept_safe(Octstr **address, long pos);
static long accept_escaped(Octstr **address, long pos);
static long handle_two_terminators (Octstr **address, long pos, 
    unsigned char comma, unsigned char point, unsigned char c, 
    long fragment_parsed, long fragment_length);
static int uses_gsm_msisdn_address(long bearer_required, Octstr *bearer);
static int uses_ipv4_address(long bearer_required, Octstr *bearer);
static int uses_ipv6_address(long bearer_required, Octstr *bearer);
static int event_semantically_valid(WAPEvent *e, long type_of_address);
static char *address_type(long type_of_address);
static void set_defaults(WAPEvent **e, long type_of_address);
static void set_bearer_defaults(WAPEvent **e, long type_of_address);
static void set_network_defaults(WAPEvent **e, long type_of_address);
static int set_anys(WAPEvent **e, long type_of_address, int is_any);
static void set_any_value(int *is_any, Octstr *attr_name, Octstr *attr_value);

/*
 * Macro for creating an octet string from a node content. This has two 
 * versions for different libxml node content implementation methods.
 */
#ifdef XML_USE_BUFFER_CONTENT
#define create_octstr_from_node(node) (octstr_create(node->content->content))
#else
#define create_octstr_from_node(node) (octstr_create(node->content))
#endif

/****************************************************************************
 *
 * Compile PAP control document to a corresponding Kannel event. Checks vali-
 * dity of the document. The caller must initialize wap event to NULL. In add-
 * ition, it must free memory allocated by this function. 
 *
 * After compiling, some semantic analysing of the resulted event. 
 *
 * Note that entities in the DTD are parameter entities and they can appear 
 * only in DTD (See site http://www.w3.org/TR/REC-xml, Chapter 4.1). So we do 
 * not need to worry about them in the document itself.
 *
 * Returns 0, when success
 *        -1, when a non-implemented pap feature is asked for
 *        -2, when error
 * In addition, returns a newly created wap event corresponding the pap 
 * control message, if success, wap event NULL otherwise. 
 */


int pap_compile(Octstr *pap_content, WAPEvent **e)
{
    xmlDocPtr doc_p;
    size_t oslen;
    int ret;

    if (octstr_search_char(pap_content, '\0', 0) != -1) {
        warning(0, "PAP COMPILER: pap_compile: pap source contained a \\0"
                   " character");
        return -2;
    }

    octstr_strip_blanks(pap_content);
    oslen = octstr_len(pap_content);
    doc_p = xmlParseMemory(octstr_get_cstr(pap_content), oslen);
    if (doc_p == NULL) {
        goto error;
    }

    if ((ret = parse_document(doc_p, e)) < 0) { 
        goto parserror;
    }

    xmlFreeDoc(doc_p);
    return 0;

parserror:
    xmlFreeDoc(doc_p);
    wap_event_destroy(*e);
    *e = NULL;
    return ret;

error:
    warning(0, "PAP COMPILER: pap_compile: parse error in pap source");
    xmlFreeDoc(doc_p);
    wap_event_destroy(*e);
    *e = NULL;
    return -2;
}

/****************************************************************************
 *
 * Implementation of internal functions
 *
 */

enum {
    NEITHER = 0,
    BEARER_ANY = 1,
    NETWORK_ANY = 2,
    EITHER = 3,
    ERROR_ANY = 4
};

/*
 * Parse the document node of libxml syntax tree. FIXME: Add parsing of pap
 * version.
 * After parsing, some semantic analysing of the resulted event. Then set
 * a default network and bearer deduced from address type, if the correspond-
 * ing pap attribute is missing.
 * 
 * Returns 0, when success
 *        -1, when a non-implemented pap feature is requested
 *        -2, when error
 * In addition, return a newly created wap event corresponding the pap 
 * control message, if success, or partially parsed pap document, if not. Set
 * a field containing address type.
 */

static int parse_document(xmlDocPtr doc_p, WAPEvent **e)
{
    xmlNodePtr node;
    int ret,
        is_any;                   /* is bearer and/or network set any in qos
                                     attribute */
    long type_of_address;

    gw_assert(doc_p);
    node = xmlDocGetRootElement(doc_p);
    is_any = NEITHER;

    if ((ret = parse_node(node, e, &type_of_address, &is_any)) < 0)
        return ret;

    (*e)->u.Push_Message.address_type = type_of_address;

    if ((ret= event_semantically_valid(*e, type_of_address)) == 0) {
        warning(0, "wrong type of address for requested bearer");
        return -2;
    } else if (ret == -1) {
        info(0, "reverting to default bearer and network");
        set_defaults(e, type_of_address);
        return 0;
    }

    if (!set_anys(e, type_of_address, is_any)) {
        warning(0, "unable to handle any values in qos");
        return -2;
    } else {
        debug("wap.push.pap.compiler", 0, "using defaults instead of anys");
    }

    wap_event_assert(*e);

    return 0;
}

static int set_anys(WAPEvent **e, long type_of_address, int is_any)
{
    switch (is_any) {
    case NEITHER:
    return 1;

    case BEARER_ANY:
        set_bearer_defaults(e, type_of_address);
    return 1;

    case NETWORK_ANY:
        set_network_defaults(e, type_of_address);
    return 1;

    case EITHER:
        set_defaults(e, type_of_address);
    return 1;

    default:
    return 0;
    }
}

/*
 * We actually use address_type field of a wap event for controlling the bearer
 * selection. Bearer and network filed are used for debugging purposes.
 */
static void set_defaults(WAPEvent **e, long type_of_address)
{
    set_bearer_defaults(e, type_of_address);
    set_network_defaults(e, type_of_address);   
}

static void set_bearer_defaults(WAPEvent **e, long type_of_address)
{
    gw_assert(type_of_address == ADDR_USER || type_of_address == ADDR_PLMN ||
              type_of_address == ADDR_IPV4 || type_of_address == ADDR_IPV6 ||
              type_of_address == ADDR_WINA);

    if ((*e)->type != Push_Message)
        return;

    (*e)->u.Push_Message.bearer_required = PAP_TRUE;
    octstr_destroy((*e)->u.Push_Message.bearer);

    switch (type_of_address) {
    case ADDR_PLMN:
	(*e)->u.Push_Message.bearer = octstr_format("%s", "SMS"); 
    break;   
 
    case ADDR_IPV4:
	(*e)->u.Push_Message.bearer = octstr_format("%s", "CSD");     
    break;

    case ADDR_IPV6:
    break;
    }
}

static void set_network_defaults(WAPEvent **e, long type_of_address)
{
    gw_assert(type_of_address == ADDR_USER || type_of_address == ADDR_PLMN ||
              type_of_address == ADDR_IPV4 || type_of_address == ADDR_IPV6 ||
              type_of_address == ADDR_WINA);

    if ((*e)->type != Push_Message)
        return;

    (*e)->u.Push_Message.network_required = PAP_TRUE;
    octstr_destroy((*e)->u.Push_Message.network);

    switch (type_of_address) {
    case ADDR_PLMN:
	(*e)->u.Push_Message.network = octstr_format("%s", "GSM");
    break;   
 
    case ADDR_IPV4:
        (*e)->u.Push_Message.network = octstr_format("%s", "GSM");   
    break;

    case ADDR_IPV6: 
    break;
    }
}

static char *address_type(long type_of_address)
{
    switch(type_of_address) {
    case ADDR_USER:
    return "user defined address";
    case ADDR_PLMN:
    return "a phone number";
    case ADDR_IPV4:
    return "a IPv4 address";
    case ADDR_IPV6:
    return "a IPv6 address";
    case ADDR_WINA:
    return "a WINA accepted address";
    default:
    return "unknown address";
    }
}

/*
 * Do semantic analysis, when the event was Push_Message. Do not accept an IP 
 * address, when a non-IP bearer is requested, and a phone number, when an IP
 * bearer is requested.
 * Return 0, when event is unacceptable
 *        1, when it is acceptable
 *       -1, when there are no bearer or network specified
 */

static int event_semantically_valid(WAPEvent *e, long type_of_address)
{
    int ret;

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: doing semantic analysis"
          " for address type %s", address_type(type_of_address));
    if (e->type != Push_Message) {
        return 1;
    }
    
    if (e->u.Push_Message.network_required != 
	     e->u.Push_Message.bearer_required) {
       debug("wap.push.pap.compiler", 0, "PAP COMPILER: network-required and"
             " bearer-required must have same value");
       return 0;
    }

    if (type_of_address == ADDR_PLMN) {
            if ((ret = uses_gsm_msisdn_address(
                     e->u.Push_Message.bearer_required,
                     e->u.Push_Message.bearer)) == 0) {
                debug("wap.push.pap.compiler", 0, "PAP COMPILER: bearer does"
                      " not accept PLMN address");
                return 0;
            } else if (ret == -1) {
	        debug("wap.push.pap.compiler", 0, "PAP COMPILER: network or"
                      "bearer missing, reverting to GSM+SMS");
                return -1;
            } else
	        return 1;
                
    }
    
    if (type_of_address == ADDR_IPV4) { 
            if ((ret = uses_ipv4_address(e->u.Push_Message.bearer_required,
                    e->u.Push_Message.bearer)) == 0) {
                debug("wap.push.pap.compiler", 0, "PAP COMPILER: bearer does"
                      " not accept IPv4 address");
                return 0;
            } else if (ret == -1) {
                debug("wap.push.pap.compiler", 0, "PAP COMPILER: network or"
                      " bearer missing, reverting to GSM+CSD");
	        return -1;
            } else
	        return 1;
    }
    
    if (type_of_address == ADDR_IPV6) { 
        if ((ret = uses_ipv6_address(e->u.Push_Message.bearer_required,
                 e->u.Push_Message.bearer)) == 0) {
             debug("wap.push.pap.compiler", 0, "PAP COMPILER: network or"
                   " bearer does not accept IPv6 address");
             return 0;
        } else if (ret == -1) {
             debug("wap.push.pap.compiler", 0, "PAP COMPILER: network or"
                   " bearer missing, reverting Any+Any");
	     return -1;
        } else
	     return 1;
    }
    
    return 0;
}

/*
 * Bearers accepting IP addresses. These are defined in wdp, appendix c. Note
 * that when ipv6 bearers begin to appear, they must be added to the following
 * table. Currently none are specified.
 */
static char *ip6_bearers[] = {
    "Any"
};

#define NUMBER_OF_IP6_BEARERS sizeof(ip6_bearers)/sizeof(ip6_bearers[0])

static char *ip4_bearers[] = {
    "Any",
    "CSD",
    "Packet Data",
    "GPRS",
    "USSD"
};

#define NUMBER_OF_IP4_BEARERS sizeof(ip4_bearers)/sizeof(ip4_bearers[0])

/*
 * Bearers accepting gsm msisdn addresses are defined in wdp, appendix c. We
 * add any, because Kannel PPG will change this to SMS.
 * Return -1, when there are no bearer defined
 *        0, when a bearer not accepting msisdn address is found
 *        1, when a bearer is accepting msisdn addresesses 
 */
static int uses_gsm_msisdn_address(long bearer_required, Octstr *bearer)
{
    if (!bearer_required)
        return -1;
    
    if (!bearer)
        return 1;
    
    return (octstr_case_compare(bearer, octstr_imm("SMS")) == 0 ||
            octstr_case_compare(bearer, octstr_imm("GHOST/R_DATA")) == 0 ||
            octstr_case_compare(bearer, octstr_imm("Any")) == 0);
}

/*
 * Bearers accepting ipv4 addresses are defined in wdp, appendix c.
 * Return -1, when there are no bearer defined
 *        0, when a bearer not accepting ipv4  address is found
 *        1, when a bearer is accepting ipv4 addresesses 
 */
static int uses_ipv4_address(long bearer_required, Octstr *bearer)
{
    long i;

    if (!bearer_required) {
        return -1;
    }

    if (!bearer)
        return -1;
    
    i = 0;
    while (i < NUMBER_OF_IP4_BEARERS) {
        if (octstr_case_compare(bearer, octstr_imm(ip4_bearers[i])) == 0) {
	    return 1;
        }
        ++i;
    }

    return 0;
}

/*
 * Bearers accepting ipv6 addresses (currently *not* accepting) are defined in
 * wdp, appendix c.
 * Return -1, when there are no bearer defined
 *        0, when a bearer not accepting ipv6 address is found
 *        1, when a bearer is accepting ipv6 addresesses 
 */
static int uses_ipv6_address(long bearer_required, Octstr *bearer)
{
    long i;

    if (!bearer_required)
        return -1;

    if (!bearer)
        return -1;

    i = 0;
    while (i < NUMBER_OF_IP6_BEARERS) {
        if (octstr_case_compare(bearer, octstr_imm(ip6_bearers[i])) == 0) {
	    return 1;
        }
        ++i;
    }

    return 0;
}


/*
 * Parse node of the syntax tree. DTD, as defined in pap, chapter 9, contains
 * only elements (entities are restricted to DTDs). 
 * The caller must initialize the value of is_any to 0.
 *
 * Output: a) a newly created wap event containing attributes from pap 
 *         document node, if success; partially parsed node, if not. 
 *         b) the type of of the client address 
 *         c) is bearer and/or network any
 * Returns 0, when success
 *        -1, when a non-implemented feature is requested
 *        -2, when error
 */
static int parse_node(xmlNodePtr node, WAPEvent **e, long *type_of_address,
                      int *is_any)
{
    int ret;

    switch (node->type) {
    case XML_COMMENT_NODE:        /* ignore text, comments and pi nodes */
    case XML_PI_NODE:
    case XML_TEXT_NODE:
    break;

    case XML_ELEMENT_NODE:
        if ((ret = parse_element(node, e, type_of_address, is_any)) < 0) {
	    return ret;
        }
    break;

    default:
        warning(0, "PAP COMPILER: parse_node: Unknown XML node in PAP source");
        return -2;
    }

    if (node->children != NULL)
        if ((ret = parse_node(node->children, e, type_of_address, 
                is_any)) < 0) {
            return ret;
	}

    if (node->next != NULL)
        if ((ret = parse_node(node->next, e, type_of_address, is_any)) < 0) {
            return ret;
        }
    
    return 0;
}

/*
 * Parse elements of a PAP source. 
 *
 * Output: a) a newly created wap event containing attributes from the
 *         element, if success; containing some unparsed attributes, if not.
 *         b) the type of the client address
 *         c) is bearer and/or network any
 * Returns 0, when success
 *        -1, when a non-implemented feature is requested
 *        -2, when error
 * In addition, return 
 */
static int parse_element(xmlNodePtr node, WAPEvent **e, long *type_of_address,
                         int *is_any)
{
    Octstr *name;
    xmlAttrPtr attribute;
    size_t i;
    int ret;

    name = octstr_create((char *)node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: element name length"
              " zero");
        return -2;
    }
    
    i = 0;
    while (i < NUM_ELEMENTS) {
        if (octstr_compare(name, octstr_imm(pap_elements[i])) == 0)
            break;
        ++i;
    }

    if (i == NUM_ELEMENTS) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown element:");
        octstr_dump(name, 0);
        octstr_destroy(name);
        return -2;
    }

    if (node->properties != NULL) {
        attribute = node->properties;
        while (attribute != NULL) {
	    if ((ret = parse_attribute(name, attribute, e,
                    type_of_address, is_any)) < 0) {
	        octstr_destroy(name);
                return ret;
            }
            attribute = attribute->next;
        }
    }

    octstr_destroy(name);

    return 0;                     /* If we reach this point, our node does not
                                     have any attributes left (or it had no 
                                     attributes to start with). This is *not* 
                                     an error. */
}

/*
 * Parse attribute updates corresponding fields of the  wap event. Check that 
 * both attribute name and value are papwise legal. If value is enumerated, 
 * legal values are stored in the attributes table. Otherwise, call a separate
 * parsing function. If an attribute value is empty, use value "erroneous".
 * 
 * Output: a) a newly created wap event containing parsed attribute from pap 
 *         source, if successfull, an uncomplete wap event otherwise.
 *         b) the type of the client address 
 *         c) is bearer and/or network set any
 * Returns 0, when success
 *        -1, when a non-implemented feature is requested
 *        -2, when error
 */
static int parse_attribute(Octstr *element_name, xmlAttrPtr attribute, 
                           WAPEvent **e, long *type_of_address, int *is_any)
{
    Octstr *attr_name, *value, *nameos;
    size_t i;
    int ret;

    nameos = octstr_imm("erroneous");
    attr_name = octstr_create((char *)attribute->name);

    if (attribute->children != NULL)
        value = create_octstr_from_node((char *)attribute->children);
    else
        value = octstr_imm("erroneous");

    i = 0;
    while (i < NUM_ATTRIBUTES) {
        if (octstr_compare(attr_name, nameos = 
                           octstr_imm(pap_attributes[i].name)) == 0)
	    break;
        ++i;
    }

    if (i == NUM_ATTRIBUTES) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown attribute `%s' "
              "within XML entity `%s'", octstr_get_cstr(attr_name), 
              octstr_get_cstr(element_name));
        goto error;
    }

/*
 * Parse an attribute (it is, check cdata is has for a value) that is *not* an
 * enumeration. Legal values are defined in pap, chapter 9. 
 */
    if (pap_attributes[i].value == NULL) {
        ret = parse_attr_value(element_name, attr_name, value, e, 
                               type_of_address, is_any);

	if (ret == -2) {
	    goto error;
        } else {
	    goto parsed;
        }
    }

    while (octstr_compare(attr_name, 
            nameos = octstr_imm(pap_attributes[i].name)) == 0) {
        if (octstr_compare(value, octstr_imm(pap_attributes[i].value)) == 0)
	    break;
        ++i;
    }

    if (octstr_compare(attr_name, nameos) != 0) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown attribute "
              "value `%s' for attribute `%s' within XML entity `%s'", 
              octstr_get_cstr(value), octstr_get_cstr(attr_name), 
              octstr_get_cstr(element_name));
        goto error;
    }

/*
 * Check that the value of the attribute is one enumerated for this attribute
 * in pap, chapter 9.
 */
    if (set_attribute_value(element_name, value, attr_name, e) == -1) 
        goto error;

    octstr_destroy(attr_name);
    octstr_destroy(value);

    return 0;

error:
    octstr_destroy(attr_name);
    octstr_destroy(value);
    return -2;

parsed:
    octstr_destroy(attr_name);
    octstr_destroy(value);
    return ret;
}

/* 
 * Attribute value parsing functions for the PAP element.
 * Defined in PAP, chapter 8.1.  
 */
static int parse_pap_value(Octstr *attr_name, Octstr *attr_value, WAPEvent **e)
{
    Octstr *ros;

    if (*e != NULL)
        wap_event_dump(*e);

    ros = octstr_imm("erroneous");
    if (octstr_compare(attr_name, octstr_imm("product-name")) == 0) {
        /* 
         * XXX This is a kludge. 
         * We can't add the product-name value to the WAPEvent, because
         * the wap_event_create() is created in the deeper layer, which
         * means as soon as we see <push-message> or <reponse-message>.
         * But we would have to decide which WAPEvent to create while 
         * being on the higher <pap> level. 
         * How's this to be solved?! -- Stipe
         */
        return 0;
    } 

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown pap"
          " element attribute `%s'", octstr_get_cstr(attr_name));
    return -2;
}  

/* 
 * Value parsing functions return the newly created wap event containing 
 * attribute value from pap source, if successfull; NULL otherwise . Value 
 * types of attributes are defined in pap, chapter 9.  
 */

static int parse_push_message_value(Octstr *attr_name, Octstr *attr_value, 
                                    WAPEvent **e)
{
    Octstr *ros;

    ros = octstr_imm("erroneous");
    if (octstr_compare(attr_name, octstr_imm("push-id")) == 0) {
        octstr_destroy((**e).u.Push_Message.pi_push_id);
	(**e).u.Push_Message.pi_push_id = octstr_duplicate(attr_value);
        return 0;
    } else if (octstr_compare(attr_name, 
             octstr_imm("deliver-before-timestamp")) == 0) {
	(**e).u.Push_Message.deliver_before_timestamp = 
             (ros = parse_date(attr_value)) ? 
             octstr_duplicate(attr_value) : octstr_imm("erroneous");  
        return return_flag(ros);
    } else if (octstr_compare(attr_name, 
             octstr_imm("deliver-after-timestamp")) == 0) {
	(**e).u.Push_Message.deliver_after_timestamp = 
             (ros = parse_date(attr_value)) ? 
             octstr_duplicate(attr_value) : octstr_imm("erroneous");
        return return_flag(ros);
    } else if (octstr_compare(attr_name, 
             octstr_imm("source-reference")) == 0) {
	(**e).u.Push_Message.source_reference = octstr_duplicate(attr_value);
        return 0;
    } else if (octstr_compare(attr_name, 
             octstr_imm("ppg-notify-requested-to")) == 0) {
	(**e).u.Push_Message.ppg_notify_requested_to = 
             octstr_duplicate(attr_value);
        return 0;
    }

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown push message"
          " element attribute");
    return -2;
}  

/*
 * When there is no legal address to be stored in field (either parsing was
 * unsuccessful or an unimplemented address format was requested by the push
 * initiator) we use value "erroneous". This is necessary, because this a 
 * mandatory field.
 *
 * Output a) a newly created wap event
 *        b) the type of the client address
 */
static int parse_address_value(Octstr *attr_name, Octstr *attr_value, 
                               WAPEvent **e, long *type_of_address)
{
    int ret;

    ret = -2;
    if (octstr_compare(attr_name, octstr_imm("address-value")) == 0) {
        octstr_destroy((**e).u.Push_Message.address_value);
	(**e).u.Push_Message.address_value = 
             (ret = parse_address(&attr_value, type_of_address)) > -1 ? 
             octstr_duplicate(attr_value) : octstr_imm("erroneous");
        return ret;
    } 

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown address element"
          " attribute");
    return -2;
}

static int parse_quality_of_service_value(Octstr *attr_name, 
                                          Octstr *attr_value, WAPEvent **e,
                                          int *is_any)
{
    Octstr *ros;

    ros = octstr_imm("erroneous");
    if (octstr_compare(attr_name, octstr_imm("network")) == 0) {
	(**e).u.Push_Message.network = (ros = parse_network(attr_value)) ? 
            octstr_duplicate(attr_value) : octstr_imm("erroneous");
        set_any_value(is_any, attr_name, attr_value);
        return return_flag(ros);
    }

    if (octstr_compare(attr_name, octstr_imm("bearer")) == 0) {
	(**e).u.Push_Message.bearer = (ros = parse_bearer(attr_value)) ? 
            octstr_duplicate(attr_value) : octstr_imm("erroneous");
        set_any_value(is_any, attr_name, attr_value);
        return return_flag(ros);
    }

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown quality of"
          " service attribute");
    return -2;
}

static void set_any_value(int *is_any, Octstr *attr_name, Octstr *attr_value)
{
    switch (*is_any) {
    case NEITHER:
        if (octstr_compare(attr_name, octstr_imm("bearer")) == 0 &&
                octstr_case_compare(attr_value, octstr_imm("any")) == 0)
	    *is_any = BEARER_ANY;
        else if (octstr_compare(attr_name, octstr_imm("network")) == 0 &&
                octstr_case_compare(attr_value, octstr_imm("any")) == 0)
	    *is_any = NETWORK_ANY;
    return;

    case BEARER_ANY:
        if (octstr_compare(attr_name, octstr_imm("network")) == 0 &&
                octstr_case_compare(attr_value, octstr_imm("any")) == 0)
	    *is_any = EITHER;
    return;

    case NETWORK_ANY:
        if (octstr_compare(attr_name, octstr_imm("bearer")) == 0 &&
                octstr_case_compare(attr_value, octstr_imm("any")) == 0)
	    *is_any = EITHER;
    return;

    case EITHER:
         debug("wap.push.pap.compiler", 0, "PAP COMPILER: problems with"
               " setting any");
         *is_any = ERROR_ANY;
    return;

    default:
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: wrong any value");
        *is_any = ERROR_ANY;
    return;
    }
}

static int parse_push_response_value(Octstr *attr_name, Octstr *attr_value,
                                     WAPEvent **e)
{
    Octstr *ros;
    int ret;

    ret = -2;
    ros = octstr_imm("erroneous");

    if (octstr_compare(attr_name, octstr_imm("push-id")) == 0) {
        octstr_destroy((**e).u.Push_Response.pi_push_id);
	(**e).u.Push_Response.pi_push_id = octstr_duplicate(attr_value);
        return 0;
    } else if (octstr_compare(attr_name, octstr_imm("sender-address")) == 0) {
	(**e).u.Push_Response.sender_address = octstr_duplicate(attr_value);
        return 0;
    } else if (octstr_compare(attr_name, octstr_imm("reply-time")) == 0) {
	(**e).u.Push_Response.reply_time = (ros = parse_date(attr_value)) ?
             octstr_duplicate(attr_value) : NULL;
        return return_flag(ros);
    } else if (octstr_compare(attr_name, octstr_imm("sender-name")) == 0) {
	(**e).u.Push_Response.sender_name = octstr_duplicate(attr_value);
        return 0;
    }

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown push response"
          " element attribute");
    return -2;
}

static int parse_progress_note_value(Octstr *attr_name, Octstr *attr_value,
                                     WAPEvent **e)
{
    Octstr *ros;
    int ret;

    ret = -2;
    ros = octstr_imm("erroneous");

    if (octstr_compare(attr_name, octstr_imm("stage")) == 0) {
        (**e).u.Progress_Note.stage = 
             (ret = parse_state(attr_value)) ? ret : 0;
        return ret;
    } else if (octstr_compare(attr_name, octstr_imm("note")) == 0) {
	(**e).u.Progress_Note.note = octstr_duplicate(attr_value);
        return 0;
    } else if (octstr_compare(attr_name, octstr_imm("time")) == 0) {
	(**e).u.Progress_Note.time = (ros = parse_date(attr_value)) ?
             octstr_duplicate(attr_value) : octstr_imm("erroneous");
	return return_flag(ros);
    }

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown progress note"
          " element attribute");
    return -2;
}

static int parse_bad_message_response_value(Octstr *attr_name, 
                                            Octstr *attr_value, WAPEvent **e)
{
    if (octstr_compare(attr_name, octstr_imm("code")) == 0) {
	(**e).u.Bad_Message_Response.code = parse_code(attr_value);
        return 0;
    } else if (octstr_compare(attr_name, octstr_imm("desc")) == 0) {
	(**e).u.Bad_Message_Response.desc = octstr_duplicate(attr_value);
        return 0;
    } else if (octstr_compare(attr_name, 
	    octstr_imm("bad-message-fragment")) == 0) {
        (**e).u.Bad_Message_Response.bad_message_fragment = 
            octstr_duplicate(attr_value);
        return 0;
    }

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown bad message"
          " response element attribute");
    return -2;
}

static int parse_response_result_value(Octstr *attr_name, 
                                      Octstr *attr_value, WAPEvent **e)
{
    if (octstr_compare(attr_name, octstr_imm("code")) == 0) {
	(**e).u.Push_Response.code = parse_code(attr_value);
        return 0;
    } else if (octstr_compare(attr_name, octstr_imm("desc")) == 0) {
	(**e).u.Push_Response.desc = octstr_duplicate(attr_value);
        return 0;
    }

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: unknown response result"
          " attribute");
    return -2;
} 

/*
 * Do not create multiple events. If *e points to NULL, we have not yet creat-
 * ed a wap event. Create a wap event mandatory fields set to error values 
 * (these will be latter overwritten). This hack will disappear when we have
 * PAP validation.
 */

static void wap_event_accept_or_create(Octstr *element_name, WAPEvent **e)
{
    if (octstr_compare(element_name, octstr_imm("push-message")) == 0 
            && *e == NULL) {         
        *e = wap_event_create(Push_Message); 
        (**e).u.Push_Message.pi_push_id = octstr_format("%s", "erroneous");
        (**e).u.Push_Message.address_value = octstr_format("%s", "erroneous");
    } else if (octstr_compare(element_name, octstr_imm("push-response")) == 0 
            && *e == NULL) {
        *e = wap_event_create(Push_Response);
        (**e).u.Push_Response.pi_push_id = octstr_format("%s", "erroneous");
    } else if (octstr_compare(element_name, octstr_imm("progress-note")) == 0 
            && *e == NULL) {
        *e = wap_event_create(Progress_Note);
    } else if (octstr_compare(element_name, 
            octstr_imm("badmessage-response")) == 0 && *e == NULL) {
        *e = wap_event_create(Bad_Message_Response);
    } 
}

static int return_flag(Octstr *ros)
{
    if (ros) {
        return 0;
    } else {
        return -2;
    }
}

/*
 * Validates non-enumeration attributes and stores their value to a newly
 * created wap event e. (Even when attribute value parsing was not success-
 * full.) We do not accept NULL or empty attributes (if this kind of an 
 * attribute is optional, we just drop it from the tokenised document).
 *
 * Output: a) a wap event, as created by subroutines
 *         b) the type of the client address
 *         c) is bearer or network set any
 * Returns 0, when success,
 *        -1, when a non-implemented feature requested.
 *        -2, when an error
 */
static int parse_attr_value(Octstr *element_name, Octstr *attr_name, 
                            Octstr *attr_value, WAPEvent **e, 
                            long *type_of_address, int *is_any)
{
    if (octstr_compare(attr_value, octstr_imm("erroneous")) == 0) {
        debug("wap.push.pap.compiler", 0, "unknown value for an attribute");
        return -2;
    }

    wap_event_accept_or_create(element_name, e);

    if (octstr_compare(element_name, octstr_imm("pap")) == 0) {
        return parse_pap_value(attr_name, attr_value, e);
    } else if (octstr_compare(element_name, octstr_imm("push-message")) == 0) {
        return parse_push_message_value(attr_name, attr_value, e);
    } else if (octstr_compare(element_name, octstr_imm("address")) == 0) {
        return parse_address_value(attr_name, attr_value, e, type_of_address);
    } else if (octstr_compare(element_name, 
                   octstr_imm("quality-of-service")) == 0) {
        return parse_quality_of_service_value(attr_name, attr_value, e, 
                                              is_any);
    } else if (octstr_compare(element_name, 
                              octstr_imm("push-response")) == 0) {
        return parse_push_response_value(attr_name, attr_value, e);
    } else if (octstr_compare(element_name, 
                   octstr_imm("progress-note")) == 0) {
        return parse_progress_note_value(attr_name, attr_value, e);
    } else if (octstr_compare(element_name, 
                   octstr_imm("badmessage-response")) == 0) {
        return parse_bad_message_response_value(attr_name, attr_value, e);
    } else if (octstr_compare(element_name, 
                   octstr_imm("response-result")) == 0) {
        return parse_response_result_value(attr_name, attr_value, e);
    }

    return -2; 
}

/*
 * Stores values of enumeration fields of a pap control message to wap event e.
 */
static int set_attribute_value(Octstr *element_name, Octstr *attr_value, 
                               Octstr *attr_name, WAPEvent **e)
{
    int ret;

    ret = -2;
    if (octstr_compare(element_name, octstr_imm("push-message")) == 0) {
        if (octstr_compare(attr_name, 
                          octstr_imm("progress-notes-requested")) == 0)
            (**e).u.Push_Message.progress_notes_requested = 
                 (ret = parse_requirement(attr_value)) >= 0 ? ret : 0;

    } else if (octstr_compare(element_name, 
			     octstr_imm("quality-of-service")) == 0) {
        if (octstr_compare(attr_name, octstr_imm("priority")) == 0)
            (**e).u.Push_Message.priority = 
                 (ret = parse_priority(attr_value)) >= 0 ? ret : 0;
        else if (octstr_compare(attr_name, octstr_imm("delivery-method")) == 0)
            (**e).u.Push_Message.delivery_method = 
                 (ret = parse_delivery_method(attr_value)) >= 0 ? ret : 0;
        else if (octstr_compare(attr_name, 
                               octstr_imm("network-required")) == 0)
            (**e).u.Push_Message.network_required = 
                 (ret = parse_requirement(attr_value)) >= 0 ? ret : 0;
        else if (octstr_compare(attr_name, octstr_imm("bearer-required")) == 0)
            (**e).u.Push_Message.bearer_required = 
                 (ret = parse_requirement(attr_value)) >= 0 ? ret : 0;
    }

    return ret;
}

/*
 * We must recognize status class and treat unrecognized codes as a x000 code,
 * as required by pap, 9.13, p 27.
 */
static int parse_code(Octstr *attr_value)
{
    long attr_as_number,
         len;
    size_t i;
    Octstr *ros;

    for (i = 0; i < NUM_CODES; i++) {
         ros = octstr_format("%d", pap_codes[i]);
         if (octstr_compare(attr_value, ros) == 0) {
	     octstr_destroy(ros);
	     return pap_codes[i];
         }
         octstr_destroy(ros);
    }

    warning(0, "PAP COMPILER: parse_code: no such return code, reversing to"
               " x000 code");
    len = octstr_parse_long(&attr_as_number, attr_value, 0, 10);
    if (attr_as_number >= PAP_OK && attr_as_number < PAP_BAD_REQUEST) {
        attr_as_number = PAP_OK;
    } else if (attr_as_number >= PAP_BAD_REQUEST && 
            attr_as_number < PAP_INTERNAL_SERVER_ERROR) {
        attr_as_number = PAP_BAD_REQUEST;
    } else if (attr_as_number >= PAP_INTERNAL_SERVER_ERROR &&
	    attr_as_number < PAP_SERVICE_FAILURE) {
        attr_as_number = PAP_INTERNAL_SERVER_ERROR;
    } else if (attr_as_number >= PAP_SERVICE_FAILURE &&
	    attr_as_number < PAP_CLIENT_ABORTED) {
        attr_as_number = PAP_SERVICE_FAILURE;
    } else {
        attr_as_number = PAP_CLIENT_ABORTED;
    }
    
    return attr_as_number;
}

static Octstr *parse_bearer(Octstr *attr_value)
{
    size_t i;
    Octstr *ros;

    for (i = 0; i < NUM_BEARER_TYPES; i++) {
         if (octstr_case_compare(attr_value, 
                 ros = octstr_imm(pap_bearer_types[i])) == 0)
	     return ros;
    }

    warning(0, "no such bearer");
    return NULL;
}

static Octstr *parse_network(Octstr *attr_value)
{
    size_t i;
    Octstr *ros;

    for (i = 0; i < NUM_NETWORK_TYPES; i++) {
         if (octstr_case_compare(attr_value, 
	         ros = octstr_imm(pap_network_types[i])) == 0) {
	     return ros;
         }
    }

    warning(0, "no such network");
    return NULL;
}

/*
 * Used for attributes accepting logical values.
 */
static int parse_requirement(Octstr *attr_value)
{
    long attr_as_number;

    attr_as_number = -2;
    if (octstr_case_compare(attr_value, octstr_imm("false")) == 0)
        attr_as_number = PAP_FALSE;
    else if (octstr_case_compare(attr_value, octstr_imm("true")) == 0)
        attr_as_number = PAP_TRUE;
    else
        warning(0, "in a requirement, value not a truth value");

    return attr_as_number;
}

/*
 * Priority is defined in pap, chapter 9.2.2.
 */
static int parse_priority(Octstr *attr_value)
{
    long attr_as_number;

    attr_as_number = -2;
    if (octstr_case_compare(attr_value, octstr_imm("high")) == 0)
        attr_as_number = PAP_HIGH;
    else if (octstr_case_compare(attr_value, octstr_imm("medium")) == 0)
        attr_as_number = PAP_MEDIUM;
    else if (octstr_case_compare(attr_value, octstr_imm("low")) == 0)
        attr_as_number = PAP_LOW;
    else
        warning(0, "illegal priority");

    return attr_as_number;
}

/*
 * Delivery-method is defined in pap, chapter 9.2.2.
 */
static int parse_delivery_method(Octstr *attr_value)
{
    long attr_as_number;

    attr_as_number = -2;
    if (octstr_case_compare(attr_value, octstr_imm("confirmed")) == 0)
        attr_as_number = PAP_CONFIRMED;
    else if (octstr_case_compare(attr_value, 
            octstr_imm("preferconfirmed")) == 0)
        attr_as_number = PAP_PREFERCONFIRMED;
    else if (octstr_case_compare(attr_value, octstr_imm("unconfirmed")) == 0)
        attr_as_number = PAP_UNCONFIRMED;
    else if (octstr_case_compare(attr_value, octstr_imm("notspecified")) == 0)
	attr_as_number = PAP_NOT_SPECIFIED;
    else
        warning(0, "illegal delivery method");
    
    return attr_as_number;
}

/*
 * PAP states are defined in ppg, chapter 6.
 */
static int parse_state(Octstr *attr_value)
{
    long attr_as_number;

    attr_as_number = -2;
    if (octstr_case_compare(attr_value, octstr_imm("undeliverable")) == 0)
        attr_as_number = PAP_UNDELIVERABLE; 
    else if (octstr_case_compare(attr_value, octstr_imm("pending")) == 0)
        attr_as_number = PAP_PENDING; 
    else if (octstr_case_compare(attr_value, octstr_imm("expired")) == 0)
        attr_as_number = PAP_EXPIRED;
    else if (octstr_case_compare(attr_value, octstr_imm("delivered")) == 0)
        attr_as_number = PAP_DELIVERED;
    else if (octstr_case_compare(attr_value, octstr_imm("aborted")) == 0)
        attr_as_number = PAP_ABORTED;
    else if (octstr_case_compare(attr_value, octstr_imm("timeout")) == 0)
        attr_as_number = PAP_TIMEOUT;
    else if (octstr_case_compare(attr_value, octstr_imm("cancelled")) == 0)
        attr_as_number = PAP_CANCELLED;
    else 
         warning(0, "illegal ppg state");

    return attr_as_number;
}

/*
 * Check legality of pap client address attribute and transform it to the 
 * client address usable in Kannel wap address tuple data type. The grammar 
 * for client address is specified in ppg, chapter 7.1.
 *
 * Output: the address type of the client address
 * Returns:   0, when success
 *           -1, a non-implemented pap feature requested by pi
 *           -2, address parsing error  
 */

int parse_address(Octstr **address, long *type_of_address)
{
    long pos;
    Octstr *copy;

    pos = octstr_len(*address) - 1;
/*
 * Delete first separator, if there is one. This will help our parsing later.
 */
    if (octstr_get_char(*address, 0) == '/')
        octstr_delete(*address, 0, 1);

/*
 * WAP-209, chapter 8 states that addresses with telephone numbers
 * should not have a ppg specifier. WAP-151 grammar, however, makes it
 * mandatory. Best way to solve this contradiction seems to be regarding
 * ppg specifier optional - MMSC is very important type of pi.
 */
    if (octstr_search_char(*address, '@', 0) >= 0) {
        if ((pos = parse_ppg_specifier(address, pos)) < 0)
        return -2;
    }

    if ((pos = parse_wappush_client_address(address, pos, 
            type_of_address)) == -2) {
        warning(0, "illegal client address");
        return -2;
    } else if (pos == -1) {
        warning(0, "unimplemented feature");
        return -1;
    }

    info(0, "client address was <%s>, accepted", 
         octstr_get_cstr(copy = octstr_duplicate(*address)));    
    octstr_destroy(copy);

    return pos;
}

/*
 * Output: the type of the client address
 */
static long parse_wappush_client_address(Octstr **address, long pos, 
                                         long *type_of_address)
{
    if ((pos = parse_client_specifier(address, pos, type_of_address)) < 0) {
        return pos;
    }

    pos = parse_constant("WAPPUSH", address, pos);
    
    return pos;
}

/*
 * We are not interested of ppg specifier, but we must check its format,
 * if we find it - it is optional.
 */
static long parse_ppg_specifier(Octstr **address, long pos)
{
    if (pos >= 0) {
        pos = parse_dom_fragment(address, pos);
    }

    while (octstr_get_char(*address, pos) != '@' && pos >= 0) {
        if (octstr_get_char(*address, pos) == '.') {
	    octstr_delete(*address, pos, 1);
            --pos;
        } else {
	    return -2;
        }

        pos = parse_dom_fragment(address, pos);
    } 

    pos = drop_character(address, pos);

    if (octstr_get_char(*address, pos) == '/' && pos >= 0) {
        octstr_delete(*address, pos, 1);
        if (pos > 0)
            --pos;
    }

    if (pos < 0) {
       return -2;
    }

    return pos;
}

/*
 * Output: the type of a client address.
 * Return a negative value, when error, positive (the position of the parsing 
 * cursor) otherwise.
 */
static long parse_client_specifier(Octstr **address, long pos, 
                                   long *type_of_address)
{
    Octstr *type_value;

    type_value = octstr_create("");

    if ((pos = parse_type(address, &type_value, pos)) < 0) {
        goto parse_error;
    }

    pos = drop_character(address, pos);

    if ((pos = parse_constant("/TYPE", address, pos)) < 0) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: constant TYPE"
              " missing from the client address");
        goto parse_error;
    }

    if (octstr_case_compare(type_value, octstr_imm("USER")) == 0) {
        *type_of_address = ADDR_USER;
        goto not_implemented;
    }

    if ((pos = parse_ext_qualifiers(address, pos, type_value)) < 0) {
        goto parse_error;
    }

    if (octstr_case_compare(type_value, octstr_imm("PLMN")) == 0) {
        *type_of_address = ADDR_PLMN;
        pos = parse_global_phone_number(address, pos);
    }

    else if (octstr_case_compare(type_value, octstr_imm("IPv4")) == 0) {
        *type_of_address = ADDR_IPV4;
        pos = parse_ipv4(address, pos);
    }

    else if (octstr_case_compare(type_value, octstr_imm("IPv6")) == 0) {
        *type_of_address = ADDR_IPV6;
        pos = parse_ipv6(address, pos);
    }

    else if (wina_bearer_identifier(type_value)) {
        *type_of_address = ADDR_WINA;
        pos = parse_escaped_value(address, pos); 
    }    

    else {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: wrong address type"
              " in the client address");
        goto parse_error; 
    }

    octstr_destroy(type_value);
    return pos;

not_implemented:
    octstr_destroy(type_value);
    return -1;

parse_error:
    octstr_destroy(type_value);
    return -2;
}

/*
 * XXX We have a kludge here. WAP-249-PPGService-20010713-a defines in 
 * section 6.1 the constant strings "WAPPUSH" and "TYPE" in upper-case. 
 * But in the examples of section 6.2 they use lower-case too. Some PI
 * vendors (ie. Jatayuu's MMSC) have implemented lower-case in their PAP 
 * documents. So we'll support this too for sake of operatibility -- st.
 */
static long parse_constant(const char *field_name, Octstr **address, long pos)
{
    size_t i, size;
    Octstr *nameos;

    nameos = octstr_format("%s", field_name);
    size = octstr_len(nameos);
    i = 0;
    
    /* convert both to lower case, see above note */
    octstr_convert_range(nameos, 0, octstr_len(nameos), tolower);
    octstr_convert_range(*address, 0, octstr_len(*address), tolower);
    
    while (octstr_get_char(*address, pos - i)  == 
               octstr_get_char(nameos, size-1 - i) && i <  size) {
        ++i;
    }

    while ((octstr_len(*address) > 0) && octstr_get_char(*address, pos) !=
               octstr_get_char(nameos, 0) && pos >= 0) {
        pos = drop_character(address, pos);
    }

    pos = drop_character(address, pos);    

    if (pos < 0 || i != size) {
        debug("wap.push.pap.compiler", 0, "parse_constant: unparsable"
              " constant %s", field_name);
        octstr_destroy(nameos);
        return -2;
    }

    octstr_destroy(nameos);
    return pos;
}

static long parse_dom_fragment(Octstr **address, long pos)
{
    unsigned char c;

    if (pos >= 0) { 
        if (isalnum(octstr_get_char(*address, pos))) {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    }

    while ((c = octstr_get_char(*address, pos)) != '@' && 
               octstr_get_char(*address, pos) != '.' && pos >= 0)  {
        if (isalnum(c) || c == '-') {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    } 

    return pos;
}

static long drop_character(Octstr **address, long pos)
{
    if (pos >= 0) {
        octstr_delete(*address, pos, 1);
        if (pos > 0)
            --pos;
    }

    return pos;
}

static long parse_type(Octstr **address, Octstr **type_value, long pos)
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '=' && pos >= 0) {   
        *type_value = prepend_char(*type_value, c);
        pos = drop_character(address, pos);
    } 

    if (pos < 0)
        return -2;

    return pos;
}

static long parse_ext_qualifiers(Octstr **address, long pos, 
                                 Octstr *type)
{
    int ret;

    while ((ret = qualifiers(*address, pos, type)) == 1) {
        if ((pos = parse_qualifier_value(address, pos)) < 0)
            return pos;

        if ((pos = parse_qualifier_keyword(address, pos)) < 0)
            return pos;
    }

    if (ret == 1) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: erroneous qualifiers"
              " in the client address");
        return -2;
    }

    return pos;
}

/*
 * According to ppg, chapter 7.1, global phone number starts with +. Phone
 * number is here an unique identifier, so if it does not conform the inter-
 * national format, we return an error. (Is up to bearerbox to transform it
 * to an usable phone number)
 */
static long parse_global_phone_number(Octstr **address, long pos)
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '+' && pos >= 0) {
         if (!isdigit(c) && c != '-' && c != '.') {
             debug("wap.push.pap.compiler", 0, "PAP COMPILER: wrong separator"
                   " in a phone number (- and . allowed)");
             return -2;
	 } else {
	     --pos;
         }
    }

    if (pos == 0) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER:a phone number must"
              " start with +");
        return -2;
    }

    if (pos > 0)
        --pos;

    pos = drop_character(address, pos);

    return pos;
}

static long parse_ipv4(Octstr **address, long pos)
{
    long i;

    if ((pos = parse_ipv4_fragment(address, pos)) < 0) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: wrong separator in a"
              " ipv4 address");
        return -2;
    }

    i = 1;

    while (i <= 3 && octstr_get_char(*address, pos) != '=' && pos >= 0) {
        pos = parse_ipv4_fragment(address, pos);
        ++i;
    }

    if (pos == 0) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: missing separator at"
              " beginning of a client address (=)");
        return -2;
    }

    return pos;
}

static long parse_ipv6(Octstr **address, long pos)
{
    long i;

    if ((pos = parse_ipv6_fragment(address, pos)) < 0) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: wrong separator in a"
              " ipv6 address");
        return -2;
    }

    i = 1;

    while (i <= 7 && octstr_get_char(*address, pos) != '=' && pos >= 0) {
        pos = parse_ipv6_fragment(address, pos);
        ++i;
    }

    if (pos == 0) {
        debug("wap.push.pap.compiler", 0, "PAP COMPILER: missing separator at"
              " beginning of a client address (=)");
        return -2;
    }

    return pos;
}

/*
 * WINA web page does not include address type identifiers. Following ones are
 * from wdp, Appendix C.
 */

static char *bearer_address[] = {
    "GSM_MSISDN",
    "ANSI_136_MSISDN",
    "IS_637_MSISDN",
    "iDEN_MSISDN",
    "FLEX_MSISDN",
    "PHS_MSISDN",
    "GSM_Service_Code",
    "TETRA_ITSI",
    "TETRA_MSISDN",
    "ReFLEX_MSIDDN",
    "MAN",
};

static size_t bearer_address_size = sizeof(bearer_address) / 
                                    sizeof(bearer_address[0]);

static int wina_bearer_identifier(Octstr *type_value)
{
    size_t i;

    i = 0;
    while (i < bearer_address_size) {
        if (octstr_case_compare(type_value, 
                octstr_imm(bearer_address[i])) == 0)
	    return 1;
        ++i;
    }

    debug("wap.push.pap.compiler", 0, "PAP COMPILER: a bearer not registered"
          " by wina");
    return 0;
}

/*
 * Note that we parse backwards. First we create a window of three characters
 * (representing a possible escaped character). If the first character of the 
 * window is not escape, we handle the last character and move the window one
 * character backwards; if it is, we handle escaped sequence and create a new
 * window. If we cannot create a window, rest of characters are unescaped.
 */
static long parse_escaped_value(Octstr **address, long pos)
{
    int ret;

    if (create_peek_window(address, &pos) == 0)
         if ((pos = rest_unescaped(address, pos)) == -2)
             return -2;

    while (octstr_get_char(*address, pos) != '=' && pos >= 0) {
        if ((ret = issafe(address, pos)) == 1) {
	    pos = accept_safe(address, pos);

        } else if (ret == 0) {
	    if ((pos = accept_escaped(address, pos)) < 0)
                return -2;  
            if (create_peek_window(address, &pos) == 0)
                if ((pos = rest_unescaped(address, pos)) == -2)
                    return -2;
        }
    }

    pos = drop_character(address, pos);

    return pos;
}

static Octstr *prepend_char(Octstr *os, unsigned char c)
{
    Octstr *tmp;

    tmp = octstr_format("%c", c);
    octstr_insert(os, tmp, 0);
    octstr_destroy(tmp);
    return os;
}

/*
 * Ext qualifiers contain /, ipv4 address contains . , ipv6 address contains :.
 * phone number contains + and escaped-value contain no specific tokens. Lastly
 * mentioned are for future extensions, but we must parse them.
 * Return 1, when qualifiers found
 *        0, when not
 *       -1, when an error was found during the process
 */
static int qualifiers(Octstr *address, long pos, Octstr *type)
{
    unsigned char term,
         c;
    long i;

    i = pos;
    c = 'E';

    if (octstr_case_compare(type, octstr_imm("PLMN")) == 0)
        term = '+';
    else if (octstr_case_compare(type, octstr_imm("IPv4")) == 0)
        term = '.';
    else if (octstr_case_compare(type, octstr_imm("IPv6")) == 0)
        term = ':';
    else
        term = 'N';

    if (term != 'N') {
        while ((c = octstr_get_char(address, i)) != term && i != 0) {
            if (c == '/')
                return 1;
            --i;
        }
        if (i == 0)
	    return 0;
    }

    if (term == 'N') {
        while (i != 0) {
            if (c == '/')
                return 1;
            --i;
        }
    } 

    return 0;
}

static long parse_qualifier_value(Octstr **address, long pos)
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '=' && pos >= 0) {
        if (c < 0x20 || (c > 0x2e && c < 0x30) || (c > 0x3c && c < 0x3e) ||
            c > 0x7e)
            return -2;

        pos = drop_character(address, pos);
    }

    pos = drop_character(address, pos);
  
    return pos;
}

static long parse_qualifier_keyword(Octstr **address, long pos)
{
    unsigned char c;  

    while ((c = octstr_get_char(*address, pos)) != '/') {
        if (isalnum(c) || c == '-') {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    }

    pos = drop_character(address, pos);       

    return pos;
}

static long parse_ipv4_fragment(Octstr **address, long pos)
{
    long i;
    unsigned char c;

    i = 0;
    c = '=';

    if (isdigit(c = octstr_get_char(*address, pos)) && pos >= 0) {
        --pos;
        ++i;
    } else {
        debug("wap.push.pap.compiler", 0, "non-digit found in ip address,"
              " address unacceptable");
        return -2;
    }
    
    while (i <= 3 && ((c = octstr_get_char(*address, pos)) != '.' &&  c != '=')
            && pos >= 0) {
        if (isdigit(c)) {
            --pos;
            ++i;
        } else {
	    debug("wap.push.pap.compiler", 0, "parse_ipv4_fragment: non-digit"
                  " in ipv4 address, address unacceptable");
	    return -2;
        }
    }

    pos = handle_two_terminators(address, pos, '.', '=', c, i, 3);

    return pos;
}

static long parse_ipv6_fragment(Octstr **address, long pos)
{
    long i;
    unsigned char c;

    i = 0;

    if (isxdigit(octstr_get_char(*address, pos)) && pos >= 0) {
        --pos;
        ++i;
    } else {
        return -2;
    }

    c = '=';

    while (i <= 4 && ((c = octstr_get_char(*address, pos)) != ':' && c != '=')
            && pos >= 0) {
        if (isxdigit(c)) {
	    --pos;
            ++i;
        } else {
	    return -2;
        }
    }

    pos = handle_two_terminators(address, pos, ':', '=', c, i, 4);

    return pos;
}

/*
 * Return -1, it was impossible to create the window because of there is no
 * more enough characters left and 0 if OK.
 */
static int create_peek_window(Octstr **address, long *pos)
{
   long i;
    unsigned char c;

    i = 0;
    c = '=';
    while (i < 2 && (c = octstr_get_char(*address, *pos)) != '=') {
        if (*pos > 0)
            --*pos;
        ++i;
    }

    if (c == '=')
        return 0;

    return 1; 
}

static long rest_unescaped(Octstr **address, long pos)
{
    long i,
         ret;

    for (i = 2; i > 0; i--) {
         if ((ret = accept_safe(address, pos)) == -2)
	     return -2;
         else if (ret == -1)
	     return pos;
    }

    return pos;
}

static int issafe(Octstr **address, long pos)
{
    if (octstr_get_char(*address, pos) == '%')
        return 0;
    else
        return 1;
}

static long accept_safe(Octstr **address, long pos)
{
    unsigned char c;

    c = octstr_get_char(*address, pos);
    if ((isalnum(c) || c == '+' || c == '-' || c == '.' || c == '_') && 
            pos >= 0)
	--pos;
    else if (c == '=')
        return -1;
    else
        return -2;

    return pos;
}

static long accept_escaped(Octstr **address, long pos)
{
    Octstr *temp;
    long i;
    unsigned char c;

    pos = drop_character(address, pos);
    temp = octstr_create("");

    for (i = 2; i > 0; i--) {
        c = octstr_get_char(*address, pos + i);
        temp = prepend_char(temp, c);
        pos = drop_character(address, pos + i);
        if (pos > 0)
	  --pos;
    }

    if (octstr_hex_to_binary(temp) < 0) {
        octstr_destroy(temp);
        return -2;
    }

    octstr_insert(*address, temp, pos + 2);   /* To the end of the window */

    octstr_destroy(temp);
    return pos + 1;                           /* The position preceding the 
                                                 inserted character */
              
}

/*
 * Point ends the string to be parsed, comma separates its fragments.
 */
static long handle_two_terminators (Octstr **address, long pos, 
    unsigned char comma, unsigned char point, unsigned char c, 
    long fragment_parsed, long fragment_length)
{
    if (fragment_parsed == fragment_length && c != comma && c != point)
        return -2;

    if (c == point) 
        octstr_delete(*address, pos, 1);

    --pos;

    return pos;
}













