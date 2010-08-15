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
 * wap_push_si_compiler.c: Tokenizes a SI document. SI DTD is defined in 
 * Wapforum specification WAP-167-ServiceInd-20010731-a (hereafter called si),
 * chapter 8.2.
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#include <ctype.h>
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/debugXML.h>
#include <libxml/encoding.h>

#include "shared.h"
#include "xml_shared.h"
#include "wap_push_si_compiler.h"

/****************************************************************************
 *
 * Global variables
 *
 * Two token table types, one and two token fields
 */

struct si_2table_t {
    char *name;
    unsigned char token;
};

typedef struct si_2table_t si_2table_t;

/*
 * Value part can mean part or whole of the value. It can be NULL, too, which
 * means that no part of the value will be tokenised. See si, chapter 9.3.2.
 */
struct si_3table_t {
    char *name;
    char *value_part;
    unsigned char token;
};

typedef struct si_3table_t si_3table_t;

/*
 * Elements from tag code page zero. These are defined in si, chapter 9.3.1.
 */

static si_2table_t si_elements[] = {
    { "si", 0x05 },
    { "indication", 0x06 },
    { "info", 0x07 },
    { "item", 0x08 }
};

#define NUMBER_OF_ELEMENTS sizeof(si_elements)/sizeof(si_elements[0])

/*
 * Attributes (and start or whole value of ) from attribute code page zero. 
 * These are defined in si, chapter 9.3.2.
 */

static si_3table_t si_attributes[] = {
    { "action", "signal-none", 0x05 },
    { "action", "signal-low", 0x06 },
    { "action", "signal-medium", 0x07 },
    { "action", "signal-high", 0x08 },
    { "action", "delete", 0x09 },
    { "created", NULL, 0x0a },
    { "href", "https://www.", 0x0f },
    { "href", "http://www.", 0x0d },
    { "href", "https://", 0x0e },
    { "href", "http://", 0x0c },
    { "href", NULL, 0x0b },
    { "si-expires", NULL, 0x10 },
    { "si-id", NULL, 0x11 },
    { "class", NULL, 0x12 }
};

#define NUMBER_OF_ATTRIBUTES sizeof(si_attributes)/sizeof(si_attributes[0])

/*
 * Attribute value tokes (URL value codes), from si, chapter 9.3.3.
 */

static si_2table_t si_URL_values[] = {
  { ".com/", 0x85 },
  { ".edu/", 0x86 },
  { ".net/", 0x87 },
  { ".org/", 0x88 }
};

#define NUMBER_OF_URL_VALUES sizeof(si_URL_values)/sizeof(si_URL_values[0])

#include "xml_definitions.h"

/****************************************************************************
 *
 * Prototypes of internal functions. Note that 'Ptr' means here '*'.
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
			  simple_binary_t **si_binary);
static int parse_node(xmlNodePtr node, simple_binary_t **sibxml);    
static int parse_element(xmlNodePtr node, simple_binary_t **sibxml);
static int parse_text(xmlNodePtr node, simple_binary_t **sibxml);   
static int parse_cdata(xmlNodePtr node, simple_binary_t **sibxml);             static int parse_attribute(xmlAttrPtr attr, simple_binary_t **sibxml);       
static int url(int hex);   
static int action(int hex);
static int date(int hex);
static Octstr *tokenize_date(Octstr *date);
static void octstr_drop_trailing_zeros(Octstr **date_token);
static void flag_date_length(Octstr **token);
static void parse_url_value(Octstr *value, simple_binary_t **sibxml);
                          
/****************************************************************************
 *
 * Implementation of the external function
 */

int si_compile(Octstr *si_doc, Octstr *charset, Octstr **si_binary)
{
    simple_binary_t *sibxml;
    int ret;
    xmlDocPtr pDoc;
    size_t size;
    char *si_c_text;

    *si_binary = octstr_create(""); 
    sibxml = simple_binary_create();

    octstr_strip_blanks(si_doc);
    set_charset(si_doc, charset);
    size = octstr_len(si_doc);
    si_c_text = octstr_get_cstr(si_doc);
    pDoc = xmlParseMemory(si_c_text, size);

    ret = 0;
    if (pDoc) {
        ret = parse_document(pDoc, charset, &sibxml);
        simple_binary_output(*si_binary, sibxml);
        xmlFreeDoc(pDoc);
    } else {
        xmlFreeDoc(pDoc);
        octstr_destroy(*si_binary);
        simple_binary_destroy(sibxml);
        error(0, "SI: No document to parse. Probably an error in SI source");
        return -1;
    }

    simple_binary_destroy(sibxml);

    return ret;
}

/****************************************************************************
 *
 * Implementation of internal functions
 *
 * Parse document node. Store si version number, public identifier and char-
 * acter set into the start of the document. FIXME: Add parse_prologue!
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
                          simple_binary_t **sibxml)
{
    xmlNodePtr node;

    (*sibxml)->wbxml_version = 0x02; /* WBXML Version number 1.2  */
    (*sibxml)->public_id = 0x05; /* SI 1.0 Public ID */
    
    charset = octstr_create("UTF-8");
    (*sibxml)->charset = parse_charset(charset);
    octstr_destroy(charset);

    node = xmlDocGetRootElement(document);
    return parse_node(node, sibxml);
}

/*
 * Parse an element node. Check if there is a token for an element tag; if not
 * output the element as a string, else ouput the token. After that, call 
 * attribute parsing functions
 * Returns:      1, add an end tag (element node has no children)
 *               0, do not add an end tag (it has children)
 *              -1, an error occurred
 */
static int parse_element(xmlNodePtr node, simple_binary_t **sibxml)
{
    Octstr *name,
           *outos;
    size_t i;
    unsigned char status_bits,
             si_hex;
    int add_end_tag;
    xmlAttrPtr attribute;

    name = octstr_create((char *)node->name);
    outos = NULL;
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_ELEMENTS) {
        if (octstr_compare(name, octstr_imm(si_elements[i].name)) == 0)
            break;
        ++i;
    }

    status_bits = 0x00;
    si_hex = 0x00;
    add_end_tag = 0;

    if (i != NUMBER_OF_ELEMENTS) {
        si_hex = si_elements[i].token;
        if ((status_bits = element_check_content(node)) > 0) {
	    si_hex = si_hex | status_bits;
	    
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
	        add_end_tag = 1;
        }
        output_char(si_hex, sibxml);
    } else {
        warning(0, "unknown tag %s in SI source", octstr_get_cstr(name));
        si_hex = WBXML_LITERAL;
        if ((status_bits = element_check_content(node)) > 0) {
	    si_hex = si_hex | status_bits;
	    /* If this node has children, the end tag must be added after 
	       them. */
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
		add_end_tag = 1;
	}
	output_char(si_hex, sibxml);
        output_octet_string(outos = octstr_duplicate(name), sibxml);
    }

    if (node->properties != NULL) {
	attribute = node->properties;
	while (attribute != NULL) {
	    parse_attribute(attribute, sibxml);
	    attribute = attribute->next;
	}
	parse_end(sibxml);
    }

    octstr_destroy(outos);
    octstr_destroy(name);
    return add_end_tag;
}

/*
 * Parse a text node of a si document. Ignore empty text nodes (space addi-
 * tions to certain points will produce these). Si codes text nodes as an
 * inline string.
 */

static int parse_text(xmlNodePtr node, simple_binary_t **sibxml)
{
    Octstr *temp;

    temp = create_octstr_from_node((char *)node);

    octstr_shrink_blanks(temp);
    octstr_strip_blanks(temp);

    if (octstr_len(temp) == 0) {
        octstr_destroy(temp);
        return 0;
    }

    parse_inline_string(temp, sibxml);    
    octstr_destroy(temp);

    return 0;
}

/*
 * Tokenises an attribute, and in most cases, the start of its value (some-
 * times whole of it). Tokenisation is based on tables in si, chapters 9.3.2
 * and 9.3.3. 
 * Returns 0 when success, -1 when error.
 */
static int parse_attribute(xmlAttrPtr attr, simple_binary_t **sibxml)
{
    Octstr *name,
           *value,
           *valueos,
           *tokenized_date;
    unsigned char si_hex;
    size_t i,
           value_len;

    name = octstr_create((char *)attr->name);

    if (attr->children != NULL)
	value = create_octstr_from_node((char *)attr->children);
    else 
	value = NULL;

    if (value == NULL)
        goto error;

    i = 0;
    valueos = NULL;
    while (i < NUMBER_OF_ATTRIBUTES) {
        if (octstr_compare(name, octstr_imm(si_attributes[i].name)) == 0) {
	    if (si_attributes[i].value_part == NULL) {
	        break; 
            } else {
                value_len = octstr_len(valueos = 
                    octstr_imm(si_attributes[i].value_part));
	        if (octstr_ncompare(value, valueos, value_len) == 0) {
		    break;
                }
            }
        }
       ++i;
    }

    if (i == NUMBER_OF_ATTRIBUTES)
        goto error;

    tokenized_date = NULL;
    si_hex = si_attributes[i].token;
    if (action(si_hex)) {
        output_char(si_hex, sibxml);
    } else if (url(si_hex)) {
        output_char(si_hex, sibxml);
        octstr_delete(value, 0, octstr_len(valueos));
        parse_url_value(value, sibxml);
    } else if (date(si_hex)) {
        if ((tokenized_date = tokenize_date(value)) == NULL)
            goto error;
        output_char(si_hex, sibxml);
        output_octet_string(tokenized_date, sibxml);
    } else {
        output_char(si_hex, sibxml);
        parse_inline_string(value, sibxml);
    }  

    octstr_destroy(tokenized_date);
    octstr_destroy(name);
    octstr_destroy(value);
    return 0;

error:
    octstr_destroy(name);
    octstr_destroy(value);
    return -1;
}


/*
 * checks whether a si attribute value is an URL or some other kind of value. 
 * Returns 1 for an URL and 0 otherwise.
 */

static int url(int hex)
{
    switch ((unsigned char) hex) {
    case 0x0b:            /* href */
    case 0x0c: case 0x0e: /* href http://, href https:// */
    case 0x0d: case 0x0f: /* href http://www., href https://www. */
	return 1;
    }
    return 0;
}

/*
 * checks whether a si attribute value is an action attribute or some other 
 * kind of value. 
 * Returns 1 for an action attribute and 0 otherwise.
 */

static int action(int hex)
{
    switch ((unsigned char) hex) {
    case 0x05: case 0x06: /* action signal-none, action signal-low */
    case 0x07: case 0x08: /* action signal-medium, action signal-high */
    case 0x09:            /* action delete */
	return 1;
    }
    return 0;
}

/*
 * checks whether a si attribute value is an OSI date or some other kind of 
 * value. 
 * Returns 1 for an action attribute and 0 otherwise.
 */

static int date(int hex)
{
    switch ((unsigned char) hex) {
    case 0x0a: case 0x10: /* created, si-expires */
	return 1;
    }
    return 0;
}

/*
 * Tokenises an OSI date. Procedure is defined in si, chapter 9.2.2. Validate
 * OSI date as specified in 9.2.1.1. Returns NULL when error, a tokenised date 
 * string otherwise.
 */
static Octstr *tokenize_date(Octstr *date)
{
    Octstr *date_token;
    long j;
    size_t i,
           date_len;
    unsigned char c;

    if (!parse_date(date)) {
        return NULL;
    }

    date_token = octstr_create("");
    octstr_append_char(date_token, WBXML_OPAQUE);

    i = 0;
    j = 0;
    date_len = octstr_len(date);
    while (i < date_len) {
        c = octstr_get_char(date, i);
        if (c != 'T' && c != 'Z' && c != '-' && c != ':') {
            if (isdigit(c)) {
                octstr_set_bits(date_token, 4*j + 8, 4, c & 0x0f);
                ++j;
            } else {
                octstr_destroy(date_token);
                return NULL;
            }
        }  
        ++i; 
    }

    octstr_drop_trailing_zeros(&date_token);
    flag_date_length(&date_token);

    return date_token;
}

static void octstr_drop_trailing_zeros(Octstr **date_token)
{
    while (1) {
        if (octstr_get_char(*date_token, octstr_len(*date_token) - 1) == '\0')
            octstr_delete(*date_token, octstr_len(*date_token) - 1, 1);
        else
            return;
    }
}

static void flag_date_length(Octstr **token)
{
    Octstr *lenos;

    lenos = octstr_format("%c", octstr_len(*token) - 1);
    octstr_insert(*token, lenos, 1);

    octstr_destroy(lenos);
}

/*
 * The recursive parsing function for the parsing tree. Function checks the 
 * type of the node, calls for the right parse function for the type, then 
 * calls itself for the first child of the current node if there's one and 
 * after that calls itself for the next child on the list.
 */

static int parse_node(xmlNodePtr node, simple_binary_t **sibxml)
{
    int status = 0;
    
    /* Call for the parser function of the node type. */
    switch (node->type) {
    case XML_ELEMENT_NODE:
	status = parse_element(node, sibxml);
	break;
    case XML_TEXT_NODE:
	status = parse_text(node, sibxml);
	break;
    case XML_CDATA_SECTION_NODE:
	status = parse_cdata(node, sibxml);
	break;
    case XML_COMMENT_NODE:
    case XML_PI_NODE:
	/* Comments and PIs are ignored. */
	break;
	/*
	 * XML has also many other node types, these are not needed with 
	 * SI. Therefore they are assumed to be an error.
	 */
    default:
	error(0, "SI compiler: Unknown XML node in the SI source.");
	return -1;
	break;
    }

    /* 
     * If node is an element with content, it will need an end tag after it's
     * children. The status for it is returned by parse_element.
     */
    switch (status) {
    case 0:

	if (node->children != NULL)
	    if (parse_node(node->children, sibxml) == -1)
		return -1;
	break;
    case 1:
	if (node->children != NULL)
	    if (parse_node(node->children, sibxml) == -1)
		return -1;
	parse_end(sibxml);
	break;

    case -1: /* Something went wrong in the parsing. */
	return -1;
    default:
	warning(0,"SI compiler: undefined return value in a parse function.");
	return -1;
	break;
    }

    if (node->next != NULL)
	if (parse_node(node->next, sibxml) == -1)
	    return -1;

    return 0;
}

/*
 * Cdata section parsing function. Output this "as it is"
 */

static int parse_cdata(xmlNodePtr node, simple_binary_t **sibxml)
{
    int ret = 0;
    Octstr *temp;

    temp = create_octstr_from_node((char *)node);
    parse_octet_string(temp, sibxml);
    octstr_destroy(temp);

    return ret;
}

/*
 * In the case of SI documents, only attribute values to be tokenized are
 * parts of urls (see si, chapter 9.3.3). The caller romoves the start of an
 * url. Check whether we can find parts in the value. If not, parse value a an
 * inline string, otherwise parse parts before and after tokenizable parts as
 * inline strings.
 */
void parse_url_value(Octstr *value, simple_binary_t **sibxml)
{
    size_t i;
    long pos;
    Octstr *urlos,
           *first_part,
	   *last_part;
    size_t first_part_len;

    i = 0;
    first_part_len = 0;
    first_part = NULL;
    last_part = NULL;
    while (i < NUMBER_OF_URL_VALUES) {
        pos = octstr_search(value, 
            urlos = octstr_imm(si_URL_values[i].name), 0);
        if (pos >= 0) {
	    first_part = octstr_duplicate(value);
            octstr_delete(first_part, pos, octstr_len(first_part) - pos);
            first_part_len = octstr_len(first_part);
            parse_inline_string(first_part, sibxml);
            output_char(si_URL_values[i].token, sibxml);
            last_part = octstr_duplicate(value);
            octstr_delete(last_part, 0, first_part_len + octstr_len(urlos));
            parse_inline_string(last_part, sibxml);
	    octstr_destroy(first_part);
            octstr_destroy(last_part);
            break;
        }
        octstr_destroy(urlos);
        ++i;
    }

    if (pos < 0) 
	parse_inline_string(value, sibxml);
        
}

