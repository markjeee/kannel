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
 * wap_push_sl_compiler.c: Tokenizes a SL document. SL DTD is defined in 
 * Wapforum specification WAP-168-ServiceLoad-20010731-a (hereafter called sl),
 * chapter 9.2.
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#include <ctype.h>
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/debugXML.h>
#include <libxml/encoding.h>

#include "xml_shared.h"
#include "wap_push_sl_compiler.h"

/******************************************************************************
 *
 * Following global variables are unique to SL compiler. See sl, chapter 10.3.
 *
 * Two token table types, with one and two token fields.
 */

struct sl_2table_t {
    char *name;
    unsigned char token;
};

typedef struct sl_2table_t sl_2table_t;

/*
 * Value part can mean whole or part of the value. It can be NULL, too; then 
 * no part of the value will be tokenized, see sl, chapter 10.3.2.
 */
struct sl_3table_t {
    char *name;
    char *value_part;
    unsigned char token;
};

typedef struct sl_3table_t sl_3table_t;

/*
 * Element from tag code page zero. It is defined in sl, chapter 10.3.1.
 */

static sl_2table_t sl_elements[] = {
    { "sl", 0x05 }
};

#define NUMBER_OF_ELEMENTS sizeof(sl_elements)/sizeof(sl_elements[0])

/*
 * Attributes (and sometimes start or whole of their value) from code page 
 * zero. These are defined in sl, chapter 10.3.2. 
 */

static sl_3table_t sl_attributes[] = {
    { "action", "execute-low", 0x05 }, 
    { "action", "execute-high", 0x06 }, 
    { "action", "cache", 0x07 }, 
    { "href", "http://", 0x09 },
    { "href", "http://www.", 0x0a }, 
    { "href", "https://", 0x0b },    
    { "href", "https://www.", 0x0c },
    { "href", NULL, 0x08 }
};

#define NUMBER_OF_ATTRIBUTES sizeof(sl_attributes)/sizeof(sl_attributes[0])

/*
 * URL value codes from code page zero. These are defined in sl, chapter 
 * 10.3.3.
 */

static sl_2table_t sl_url_values[] = {
    { ".com/", 0x85 },
    { ".edu/", 0x86 },
    { ".net/", 0x87 },
    { ".org/", 0x88 },
};

#define NUMBER_OF_URL_VALUES sizeof(sl_url_values)/sizeof(sl_url_values[0])

#include "xml_definitions.h"

/****************************************************************************
 *
 * Prototypes of internal functions. Note that 'Ptr' means here '*'.
 */
static int parse_document(xmlDocPtr document, Octstr *charset, 
                          simple_binary_t **slbxml);
static int parse_node(xmlNodePtr node, simple_binary_t **slbxml);
static int parse_element(xmlNodePtr node, simple_binary_t **slbxml);
static int parse_attribute(xmlAttrPtr attr, simple_binary_t **slbxml);
static int url(int hex);
static int action(int hex);
static void parse_url_value(Octstr *value, simple_binary_t **slbxml);

/****************************************************************************
 *
 * Implementation of the external function
 */

int sl_compile(Octstr *sl_doc, Octstr *charset, Octstr **sl_binary)
{
    simple_binary_t *slbxml;
    int ret;
    xmlDocPtr pDoc;
    size_t size;
    char *sl_c_text;

    *sl_binary = octstr_create(""); 
    slbxml = simple_binary_create();

    octstr_strip_blanks(sl_doc);
    set_charset(sl_doc, charset);
    size = octstr_len(sl_doc);
    sl_c_text = octstr_get_cstr(sl_doc);
    pDoc = xmlParseMemory(sl_c_text, size);

    ret = 0;
    if (pDoc) {
        ret = parse_document(pDoc, charset, &slbxml);
        simple_binary_output(*sl_binary, slbxml);
        xmlFreeDoc(pDoc);
    } else {
        xmlFreeDoc(pDoc);
        octstr_destroy(*sl_binary);
        simple_binary_destroy(slbxml);
        error(0, "SL: No document to parse. Probably an error in SL source");
        return -1;
    }

    simple_binary_destroy(slbxml);

    return ret;
}

/****************************************************************************
 *
 * Implementation of internal functions
 *
 * Parse document node. Store sl version number, public identifier and 
 * character set at the start of the document
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
                          simple_binary_t **slbxml)
{
    xmlNodePtr node;

    (**slbxml).wbxml_version = 0x02; /* WBXML Version number 1.2  */
    (**slbxml).public_id = 0x06;  /* SL 1.0 Public ID */
    
    charset = octstr_create("UTF-8");
    (**slbxml).charset = parse_charset(charset);
    octstr_destroy(charset);

    node = xmlDocGetRootElement(document);
    return parse_node(node, slbxml);
}

/*
 * The recursive parsing function for the parsing tree. Function checks the 
 * type of the node, calls for the right parse function for the type, then 
 * calls itself for the first child of the current node if there's one and 
 * after that calls itself for the next child on the list. We parse whole 
 * tree, even though SL DTD defines only one node (see sl, chapter 9.2); this
 * allows us throw an error message when an unknown element is found.
 */

static int parse_node(xmlNodePtr node, simple_binary_t **slbxml)
{
    int status = 0;
    
    /* Call for the parser function of the node type. */
    switch (node->type) {
    case XML_ELEMENT_NODE:
	status = parse_element(node, slbxml);
	break;
    case XML_TEXT_NODE:
    case XML_COMMENT_NODE:
    case XML_PI_NODE:
	/* Text nodes, comments and PIs are ignored. */
	break;
	/*
	 * XML has also many other node types, these are not needed with 
	 * SL. Therefore they are assumed to be an error.
	 */
    default:
	error(0, "SL COMPILER: Unknown XML node in the SL source.");
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
	    if (parse_node(node->children, slbxml) == -1)
		return -1;
	break;
    case 1:
	if (node->children != NULL)
	    if (parse_node(node->children, slbxml) == -1)
		return -1;
	parse_end(slbxml);
	break;

    case -1: /* Something went wrong in the parsing. */
	return -1;
    default:
	warning(0,"SL compiler: undefined return value in a parse function.");
	return -1;
	break;
    }

    if (node->next != NULL)
	if (parse_node(node->next, slbxml) == -1)
	    return -1;

    return 0;
}

/*
 * Parse an element node. Check if there is a token for an element tag; if not
 * output the element as a string, else ouput the token. After that, call 
 * attribute parsing functions. Note that we take advantage of the fact that
 * sl documents have only one element (see sl, chapter 6.2).
 * Returns:      1, add an end tag (element node has no children)
 *               0, do not add an end tag (it has children)
 *              -1, an error occurred
 */
static int parse_element(xmlNodePtr node, simple_binary_t **slbxml)
{
    Octstr *name,
           *nameos;
    unsigned char status_bits,
             sl_hex;
    int add_end_tag;
    xmlAttrPtr attribute;

    name = octstr_create((char *)node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    status_bits = 0x00;
    sl_hex = 0x00;
    add_end_tag = 0;

    if (octstr_compare(name, octstr_imm(sl_elements[0].name)) != 0) {
        warning(0, "unknown tag %s in SL source", octstr_get_cstr(name));
        sl_hex = WBXML_LITERAL;
        if ((status_bits = element_check_content(node)) > 0) {
	    sl_hex = sl_hex | status_bits;
	    /* If this node has children, the end tag must be added after 
	       them. */
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
		add_end_tag = 1;
	}
	output_char(sl_hex, slbxml);
        output_octet_string(nameos = octstr_duplicate(name), slbxml);
        octstr_destroy(nameos);
    } else {
        sl_hex = sl_elements[0].token;
        if ((status_bits = element_check_content(node)) > 0) {
	    sl_hex = sl_hex | status_bits;
	    
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT) {
	        add_end_tag = 1;
            }
            output_char(sl_hex, slbxml);
        }
    }

    if (node->properties != NULL) {
	attribute = node->properties;
	while (attribute != NULL) {
	    parse_attribute(attribute, slbxml);
	    attribute = attribute->next;
	}
	parse_end(slbxml);
    }

    octstr_destroy(name);
    return add_end_tag;
}

static int parse_attribute(xmlAttrPtr attr, simple_binary_t **slbxml)
{
    Octstr *name,
           *value,
           *valueos;
    unsigned char sl_hex;
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
        if (octstr_compare(name, octstr_imm(sl_attributes[i].name)) == 0) {
	    if (sl_attributes[i].value_part == NULL) {
	        debug("wap.push.sl.compiler", 0, "value part was NULL");
	        break; 
            } else {
                value_len = octstr_len(valueos = 
                    octstr_imm(sl_attributes[i].value_part));
	        if (octstr_ncompare(value, valueos, value_len) == 0) {
		    break;
                }
            }
        }
       ++i;
    }

    if (i == NUMBER_OF_ATTRIBUTES) {
        warning(0, "unknown attribute in SL source");
        goto error;
    }

    sl_hex = sl_attributes[i].token;
    if (action(sl_hex)) {
        output_char(sl_hex, slbxml);
    } else if (url(sl_hex)) {
        output_char(sl_hex, slbxml);
        octstr_delete(value, 0, octstr_len(valueos));
        parse_url_value(value, slbxml);
    } else {
        output_char(sl_hex, slbxml);
        parse_inline_string(value, slbxml);
    } 

    octstr_destroy(name);
    octstr_destroy(value);
    return 0;

error:
    octstr_destroy(name);
    octstr_destroy(value);
    return -1;    
}

/*
 * checks whether a sl attribute value is an URL or some other kind of value. 
 * Returns 1 for an URL and 0 otherwise.
 */

static int url(int hex)
{
    switch ((unsigned char) hex) {
    case 0x08:            /* href */
    case 0x09: case 0x0b: /* href http://, href https:// */
    case 0x0a: case 0x0c: /* href http://www., href https://www. */
	return 1;
    }
    return 0;
}

/*
 * checks whether a sl attribute value is an action attribute or some other 
 * kind of value. 
 * Returns 1 for an action attribute and 0 otherwise.
 */

static int action(int hex)
{
    switch ((unsigned char) hex) {
    case 0x05: case 0x06: /* action execute-low, action execute-high */
    case 0x07:            /* action cache */
	return 1;
    }
    return 0;
}

/*
 * In the case of SL document, only attribute values to be tokenised are parts
 * of urls. See sl, chapter 10.3.3. The caller removes the start of the url.
 * Check whether we can find one of tokenisable values in value. If not, parse
 * value as a inline string, else parse parts before and after the tokenisable
 * url value as a inline string.
 */
static void parse_url_value(Octstr *value, simple_binary_t **slbxml)
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
            urlos = octstr_imm(sl_url_values[i].name), 0);
        if (pos >= 0) {
	    first_part = octstr_duplicate(value);
            octstr_delete(first_part, pos, octstr_len(first_part) - pos);
            first_part_len = octstr_len(first_part);
            parse_inline_string(first_part, slbxml);
            output_char(sl_url_values[i].token, slbxml);
            last_part = octstr_duplicate(value);
            octstr_delete(last_part, 0, first_part_len + octstr_len(urlos));
            parse_inline_string(last_part, slbxml);
	    octstr_destroy(first_part);
            octstr_destroy(last_part);
            break;
        }
        octstr_destroy(urlos);
        ++i;
    }

    if (pos < 0) 
	parse_inline_string(value, slbxml);
        
}







