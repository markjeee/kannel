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
 * wml_compiler.c - compiling WML to WML binary
 *
 * This is an implemention for WML compiler for compiling the WML text 
 * format to WML binary format, which is used for transmitting the 
 * decks to the mobile terminal to decrease the use of the bandwidth.
 *
 *
 * Tuomas Luttinen for Wapit Ltd.
 */

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/debugXML.h>
#include <libxml/encoding.h>
#include <libxml/parser.h>
#include <libxml/xmlerror.h>

#include "gwlib/gwlib.h"
#include "wml_compiler.h"
#include "xml_definitions.h"

/***********************************************************************
 * Declarations of data types. 
 * 
 * Binary code values are defined by OMNA, see 
 * http://www.openmobilealliance.org/tech/omna/omna-wbxml-public-docid.htm
 */

struct wml_externalid_t {
    char *string;
    unsigned long value;
};

typedef struct wml_externalid_t wml_externalid_t;

#define NUMBERED(name, strings) \
    static const wml_externalid_t name##_strings[] = { strings };
#define ASSIGN(string, number) { string, number },
#include "wbxml_tokens.def"

#define NUMBER_OF_WML_EXTERNALID ((long) sizeof(public_ids_strings)/sizeof(public_ids_strings[0]))

struct wbxml_version_t {
    char *string;
    char value;
};

typedef struct wbxml_version_t wbxml_version_t;

static wbxml_version_t wbxml_version[] = {
    { "1.1", 0x01 },
    { "1.2", 0x02 },
    { "1.3", 0x03 },
    { "1.4", 0x04 },
    { "1.5", 0x05 }
};

#define NUMBER_OF_WBXML_VERSION sizeof(wbxml_version)/sizeof(wbxml_version[0])


typedef enum { NOESC, ESC, UNESC, FAILED } var_esc_t;


/*
 * The wml token table node with two fields.
 */

typedef struct {
    char *text;
    unsigned char token;
} wml_table_t;


/*
 * The wml token table node with three fields.
 */

typedef struct {
    char *text1;
    char *text2;
    unsigned char token;
} wml_table3_t;


/*
 * The binary WML structure, that has been passed around between the 
 * internal functions. It contains the header fields for wbxml version, 
 * the WML public ID and the character set, the length of the string table, 
 * the list structure implementing the string table and the octet string 
 * containing the encoded WML binary.
 */

typedef struct {
    unsigned char wbxml_version;
    unsigned long wml_public_id;
    unsigned long character_set;
    unsigned long string_table_length;
    List *string_table;
    Octstr *wbxml_string;
} wml_binary_t;


/*
 * The string table list node.
 */

typedef struct {
    unsigned long offset;
    Octstr *string;
} string_table_t;


/*
 * The string table proposal list node.
 */

typedef struct {
    int count;
    Octstr *string;
} string_table_proposal_t;


/*
 * The wml hash table node.
 */

typedef struct {
    Octstr *item;
    unsigned char binary;
} wml_hash_t;


/*
 * The hash table node for attribute and values.
 */

typedef struct {
    Octstr *attribute;
    unsigned char binary;
    List *value_list;
} wml_attribute_t;

#include "xml_shared.h"
#include "wml_definitions.h"


/***********************************************************************
 * Declarations of global variables. 
 */

Dict *wml_elements_dict;

Dict *wml_attributes_dict;

List *wml_attr_values_list;

List *wml_URL_values_list;

int wml_xml_parser_opt;


/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */


/*
 * Parsing functions. These funtions operate on a single node or a 
 * smaller datatype. Look for more details on the functions at the 
 * definitions.
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
			  wml_binary_t **wbxml, Octstr *version);

static int parse_node(xmlNodePtr node, wml_binary_t **wbxml);
static int parse_element(xmlNodePtr node, wml_binary_t **wbxml);
static int parse_attribute(xmlAttrPtr attr, wml_binary_t **wbxml);
static int parse_attr_value(Octstr *attr_value, List *tokens,
			    wml_binary_t **wbxml, int charset, var_esc_t default_esc);
static int parse_text(xmlNodePtr node, wml_binary_t **wbxml);
static int parse_cdata(xmlNodePtr node, wml_binary_t **wbxml);
static int parse_st_octet_string(Octstr *ostr, int cdata, var_esc_t default_esc, wml_binary_t **wbxml);
static void parse_st_end(wml_binary_t **wbxml);
static void parse_entities(Octstr *wml_source);

/*
 * Variable functions. These functions are used to find and parse variables.
 */

static int parse_variable(Octstr *text, int start, var_esc_t default_esc, Octstr **output, 
			  wml_binary_t **wbxml);
static Octstr *get_variable(Octstr *text, int start);
static var_esc_t check_variable_syntax(Octstr *variable, var_esc_t default_esc);


/*
 * wml_binary-functions. These are used to create, destroy and modify
 * wml_binary_t.
 */

static wml_binary_t *wml_binary_create(void);
static void wml_binary_destroy(wml_binary_t *wbxml);
static void wml_binary_output(Octstr *ostr, wml_binary_t *wbxml);

/* Output into the wml_binary. */

static void output_st_char(int byte, wml_binary_t **wbxml);
static void output_st_octet_string(Octstr *ostr, wml_binary_t **wbxml);
static void output_variable(Octstr *variable, Octstr **output, 
			    var_esc_t escaped, wml_binary_t **wbxml);

/*
 * Memory allocation and deallocations.
 */

static wml_hash_t *hash_create(char *text, unsigned char token);
static wml_attribute_t *attribute_create(void);
static void attr_dict_construct(wml_table3_t *attributes, Dict *attr_dict);

static void hash_destroy(void *p);
static void attribute_destroy(void *p);

/*
 * Comparison functions for the hash tables.
 */

static int hash_cmp(void *hash1, void *hash2);

/*
 * Miscellaneous help functions.
 */

static int check_do_elements(xmlNodePtr node);
static var_esc_t check_variable_name(xmlNodePtr node);
static Octstr *get_do_element_name(xmlNodePtr node);
static int check_if_url(int hex);
static int check_if_emphasis(xmlNodePtr node);

static int wml_table_len(wml_table_t *table);
static int wml_table3_len(wml_table3_t *table);

/* 
 * String table functions, used to add and remove strings into and from the
 * string table.
 */

static string_table_t *string_table_create(int offset, Octstr *ostr);
static void string_table_destroy(string_table_t *node);
static string_table_proposal_t *string_table_proposal_create(Octstr *ostr);
static void string_table_proposal_destroy(string_table_proposal_t *node);
static void string_table_build(xmlNodePtr node, wml_binary_t **wbxml);
static void string_table_collect_strings(xmlNodePtr node, List *strings);
static List *string_table_collect_words(List *strings);
static List *string_table_sort_list(List *start);
static List *string_table_add_many(List *sorted, wml_binary_t **wbxml);
static unsigned long string_table_add(Octstr *ostr, wml_binary_t **wbxml);
static void string_table_apply(Octstr *ostr, wml_binary_t **wbxml);
static void string_table_output(Octstr *ostr, wml_binary_t **wbxml);


/***********************************************************************
 * Generic error message formater for libxml2 related errors
 */

static void xml_error(void)
{
    xmlErrorPtr err; 
    Octstr *msg;
    
    /* we should have an error, but be more sensitive */
    if ((err = xmlGetLastError()) == NULL)
        return;
        
    /* replace annoying line feeds */    
    msg = octstr_format("%s", err->message);
    octstr_replace(msg, octstr_imm("\n"), octstr_imm(" "));
    error(0,"XML error: code: %d, level: %d, line: %d, %s",
          err->code, err->level, err->line, octstr_get_cstr(msg));
    octstr_destroy(msg);
}


/***********************************************************************
 * Implementations of the functions declared in wml_compiler.h.
 */

/*
 * The actual compiler function. This operates as interface to the compiler.
 * For more information, look wml_compiler.h. 
 */
int wml_compile(Octstr *wml_text, Octstr *charset, Octstr **wml_binary,
                Octstr *version)
{
    int ret = 0;
    size_t size;
    xmlDocPtr pDoc = NULL;
    char *wml_c_text;
    wml_binary_t *wbxml = NULL;

    *wml_binary = octstr_create("");
    wbxml = wml_binary_create();

    /* Remove the extra space from start and the end of the WML Document. */
    octstr_strip_blanks(wml_text);

    /* Check the WML-code for \0-characters and for WML entities. Fast patch.
       -- tuo */
    parse_entities(wml_text);

    size = octstr_len(wml_text);
    wml_c_text = octstr_get_cstr(wml_text);
    
    debug("wml_compile",0, "WML: Given charset: %s", octstr_get_cstr(charset));

    if (octstr_search_char(wml_text, '\0', 0) != -1) {    
        error(0, "WML compiler: Compiling error: "
                 "\\0 character found in the middle of the WML source.");
        ret = -1;
    } else {
        /* 
         * An empty octet string for the binary output is created, the wml 
         * source is parsed into a parsing tree and the tree is then compiled 
         * into binary.
         */
         
        pDoc = xmlReadMemory(wml_c_text, size, NULL, octstr_get_cstr(charset), 
                             wml_xml_parser_opt);
        
        if (pDoc != NULL) {
            /* 
             * If we have a set internal encoding, then apply this information 
             * to the XML parsing tree document for later transcoding ability.
             */
            if (charset)
                pDoc->charset = xmlParseCharEncoding(octstr_get_cstr(charset));

            ret = parse_document(pDoc, charset, &wbxml, version);
            wml_binary_output(*wml_binary, wbxml);
        } else {    
            error(0, "WML compiler: Compiling error: "
                     "libxml2 returned a NULL pointer");
            xml_error();
            ret = -1;
        }
    }

    wml_binary_destroy(wbxml);

    if (pDoc) 
        xmlFreeDoc(pDoc);

    return ret;
}


/*
 * Initialization: makes up the hash tables for the compiler.
 */

void wml_init(int wml_xml_strict)
{
    int i = 0, len = 0;
    wml_hash_t *temp = NULL;
    
    /* The wml elements into a hash table. */
    len = wml_table_len(wml_elements);
    wml_elements_dict = dict_create(len, hash_destroy);

    for (i = 0; i < len; i++) {
	temp = hash_create(wml_elements[i].text, wml_elements[i].token);
	dict_put(wml_elements_dict, temp->item, temp);
    }

    /* Attributes. */
    len = wml_table3_len(wml_attributes);
    wml_attributes_dict = dict_create(len, attribute_destroy);
    attr_dict_construct(wml_attributes, wml_attributes_dict);

    /* Attribute values. */
    len = wml_table_len(wml_attribute_values);
    wml_attr_values_list = gwlist_create();

    for (i = 0; i < len; i++) {
	temp = hash_create(wml_attribute_values[i].text, 
			   wml_attribute_values[i].token);
	gwlist_append(wml_attr_values_list, temp);
    }

    /* URL values. */
    len = wml_table_len(wml_URL_values);
    wml_URL_values_list = gwlist_create();

    for (i = 0; i < len; i++) {
	temp = hash_create(wml_URL_values[i].text, wml_URL_values[i].token);
	gwlist_append(wml_URL_values_list, temp);
    }
    
    /* Strict XML parsing. */
    wml_xml_parser_opt = wml_xml_strict ? 
            (XML_PARSE_NOERROR | XML_PARSE_NONET) :
            (XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NONET);
}



/*
 * Shutdown: Frees the memory allocated by initialization.
 */

void wml_shutdown()
{
    dict_destroy(wml_elements_dict);
    dict_destroy(wml_attributes_dict);
    gwlist_destroy(wml_attr_values_list, hash_destroy);
    gwlist_destroy(wml_URL_values_list, hash_destroy);
}



/***********************************************************************
 * Internal functions.
 */


/*
 * parse_node - the recursive parsing function for the parsing tree.
 * Function checks the type of the node, calls for the right parse 
 * function for the type, then calls itself for the first child of
 * the current node if there's one and after that calls itself for the 
 * next child on the list.
 */

static int parse_node(xmlNodePtr node, wml_binary_t **wbxml)
{
    int status = 0;
    
    /* Call for the parser function of the node type. */
    switch (node->type) {
    case XML_ELEMENT_NODE:
	status = parse_element(node, wbxml);
	break;
    case XML_TEXT_NODE:
	status = parse_text(node, wbxml);
	break;
    case XML_CDATA_SECTION_NODE:
	status = parse_cdata(node, wbxml);
	break;
    case XML_COMMENT_NODE:
    case XML_PI_NODE:
	/* Comments and PIs are ignored. */
	break;
	/*
	 * XML has also many other node types, these are not needed with 
	 * WML. Therefore they are assumed to be an error.
	 */
    default:
	error(0, "WML compiler: Unknown XML node in the WML source.");
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
	    if (parse_node(node->children, wbxml) == -1)
		return -1;
	break;
    case 1:
	if (node->children != NULL)
	    if (parse_node(node->children, wbxml) == -1)
		return -1;
	parse_st_end(wbxml);
	break;

    case -1: /* Something went wrong in the parsing. */
	return -1;
    default:
	error(0,
	      "WML compiler: undefined return value in a parse function.");
	return -1;
	break;
    }

    if (node->next != NULL)
	if (parse_node(node->next, wbxml) == -1)
	    return -1;

    return 0;
}


/*
 * parse_document - the parsing function for the document node.
 * The function outputs the WBXML version, WML public id and the
 * character set values into start of the wbxml.
 */
static int parse_document(xmlDocPtr document, Octstr *charset,
                          wml_binary_t **wbxml, Octstr *version)
{
    xmlNodePtr node;
    Octstr *externalID = NULL;
    long i;

    if (document == NULL) {
        error(0, "WML compiler: XML parsing failed, no parsed document.");
        error(0, "Most probably an error in the WML source.");
        return -1;
    }

    /* Return WBXML version dependent on device given Encoding-Version */
    if (version == NULL) {
        (*wbxml)->wbxml_version = 0x01; /* WBXML Version number 1.1 */
        info(0, "WBXML: No wbxml version given, assuming 1.1");
    } else {
        for (i = 0; i < NUMBER_OF_WBXML_VERSION; i++) {
            if (octstr_compare(version, octstr_imm(wbxml_version[i].string)) == 0) {
                (*wbxml)->wbxml_version = wbxml_version[i].value;
                debug("parse_document",0,"WBXML: Encoding with wbxml version <%s>",
                      octstr_get_cstr(version));
                break;
            }
        }
        if (i == NUMBER_OF_WBXML_VERSION) {
            (*wbxml)->wbxml_version = 0x01; /* WBXML Version number 1.1 */
            warning(0, "WBXML: Unknown wbxml version, assuming 1.1 (<%s> is unknown)",
                    octstr_get_cstr(version));
        }
    }

    /* Return WML Version dependent on xml ExternalID string */
    if ((document->intSubset != NULL) && (document->intSubset->ExternalID != NULL))    
        externalID = octstr_create((char *)document->intSubset->ExternalID);
    if (externalID == NULL) {
        (*wbxml)->wml_public_id = 0x04; /* WML 1.1 Public ID */
        warning(0, "WBXML: WML without ExternalID, assuming 1.1");
    } else {
        for (i = 0; i < NUMBER_OF_WML_EXTERNALID; i++) {
            if (octstr_compare(externalID, octstr_imm(public_ids_strings[i].string)) == 0) {
                (*wbxml)->wml_public_id = public_ids_strings[i].value;
                debug("parse_document",0,"WBXML: WML with ExternalID <%s>",
                      octstr_get_cstr(externalID));
                break;
            }
        }
        if (i == NUMBER_OF_WML_EXTERNALID) {
            (*wbxml)->wml_public_id = 0x04; /* WML 1.1 Public ID */
            warning(0, "WBXML: WML with unknown ExternalID, assuming 1.1 "
                    "(<%s> is unknown)",
                    octstr_get_cstr(externalID));
        }
    }
    octstr_destroy(externalID);
    
    (*wbxml)->string_table_length = 0x00; /* String table length=0 */

    /*
     * Make sure we set the charset encoding right. If none is given
     * then set UTF-8 as default.
     */
    (*wbxml)->character_set = charset ? 
        parse_charset(charset) : parse_charset(octstr_imm("UTF-8"));

    node = xmlDocGetRootElement(document);
    
    if (node == NULL) {
        error(0, "WML compiler: XML parsing failed, no document root element.");
        error(0, "Most probably an error in the WML source.");
        xml_error();
        return -1;
    }
    
    string_table_build(node, wbxml);

    return parse_node(node, wbxml);
}


/*
 * parse_element - the parsing function for an element node.
 * The element tag is encoded into one octet hexadecimal value, 
 * if possible. Otherwise it is encoded as text. If the element 
 * needs an end tag, the function returns 1, for no end tag 0
 * and -1 for an error.
 */

static int parse_element(xmlNodePtr node, wml_binary_t **wbxml)
{
    int add_end_tag = 0;
    unsigned char wbxml_hex = 0, status_bits;
    xmlAttrPtr attribute;
    Octstr *name;
    wml_hash_t *element;

    name = octstr_create((char *)node->name);

    /* Check, if the tag can be found from the code page. */
    if ((element = dict_get(wml_elements_dict, name)) != NULL) {
	wbxml_hex = element->binary;
	/* A conformance patch: no do-elements of same name in a card or
	   template. An extremely ugly patch. --tuo */
	if (wbxml_hex == 0x27 || /* Card */
	    wbxml_hex == 0x3B)   /* Template */
	    if (check_do_elements(node) == -1) {
		add_end_tag = -1;
		error(0, "WML compiler: Two or more do elements with same"
		         " name in a card or template element.");
	    }
	/* A conformance patch: if variable in setvar has a bad name, it's
	   ignored. */
	if (wbxml_hex == 0x3E) /* Setvar */
	    if (check_variable_name(node) == FAILED) {
		octstr_destroy(name);
		return add_end_tag;
	    }
	if ((status_bits = element_check_content(node)) > 0) {
	    wbxml_hex = wbxml_hex | status_bits;
	    /* If this node has children, the end tag must be added after 
	       them. */
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
		add_end_tag = 1;
	}
	
	output_st_char(wbxml_hex, wbxml);
    } else {    
	/* The tag was not on the code page, it has to be encoded as a 
	   string. */
	wbxml_hex = WBXML_LITERAL;
	if ((status_bits = element_check_content(node)) > 0) {
	    wbxml_hex = wbxml_hex | status_bits;
	    /* If this node has children, the end tag must be added after 
	       them. */
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
		add_end_tag = 1;
	}
	output_st_char(wbxml_hex, wbxml);
	octstr_append_uintvar((*wbxml)->wbxml_string,string_table_add(octstr_duplicate(name), wbxml));
	warning(0, "WML compiler: Unknown tag in WML source: <%s>", 
		octstr_get_cstr(name));
    }

    /* Encode the attribute list for this node and add end tag after the 
       list. */

    if(node->properties != NULL) {
	attribute = node->properties;
	while (attribute != NULL) {
	    parse_attribute(attribute, wbxml);
	    attribute = attribute->next;
	}
	parse_st_end(wbxml);
    }

    octstr_destroy(name);
    return add_end_tag;
}


/*
 * parse_attribute - the parsing function for attributes. The function 
 * encodes the attribute (and probably start of the value) as a one 
 * hexadecimal octet. The value (or the rest of it) is coded as a string 
 * maybe using predefined attribute value tokens to reduce the length
 * of the output. Returns 0 for success, -1 for error.
 */

static int parse_attribute(xmlAttrPtr attr, wml_binary_t **wbxml)
{
    int status = 0;
    int coded_length = 0;
    unsigned char wbxml_hex = 0x00;
    wml_hash_t *hit = NULL;
    wml_attribute_t *attribute = NULL;
    Octstr *name = NULL, *pattern = NULL, *p = NULL;

    name = octstr_create((char *)attr->name);

    if (attr->children != NULL)
	pattern = create_octstr_from_node((char *)attr->children);
    else 
	pattern = NULL;

    /* Check if the attribute is found on the code page. */

    if ((attribute = dict_get(wml_attributes_dict, name)) != NULL) {
	if (attr->children == NULL || 
	    (hit = gwlist_search(attribute->value_list, (void *)pattern, 
			       hash_cmp)) == NULL) {
                if(attribute->binary == 0x00) {
                    warning(0, "WML compiler: can't compile attribute %s%s%s%s", 
                               octstr_get_cstr(attribute->attribute), 
			       (attr->children != NULL ? "=\"": ""), 
			       (attr->children != NULL ? octstr_get_cstr(pattern) : ""), 
			       (attr->children != NULL ? "\"": ""));
	            wbxml_hex = WBXML_LITERAL;
	            output_st_char(wbxml_hex, wbxml);
	            output_st_char(string_table_add(octstr_duplicate(name), wbxml), wbxml);
		} else {
		    wbxml_hex = attribute->binary;
		    output_st_char(wbxml_hex, wbxml);
		}
	} else if (hit->binary) {
	    wbxml_hex = hit->binary;
	    coded_length = octstr_len(hit->item);
	    output_st_char(wbxml_hex, wbxml);
	} else
	    status = -1;
    } else {
	/* The attribute was not on the code page, it has to be encoded as a 
	   string. */
	wbxml_hex = WBXML_LITERAL;
	output_st_char(wbxml_hex, wbxml);
	octstr_append_uintvar((*wbxml)->wbxml_string,string_table_add(octstr_duplicate(name), wbxml));
	warning(0, "WML compiler: Unknown attribute in WML source: <%s>", 
		octstr_get_cstr(name));
    }

    if (status >= 0) {
	var_esc_t default_esc;

	default_esc = (octstr_str_compare (name, "href") == 0) ? ESC : NOESC;

	/* The rest of the attribute is coded as a inline string. */
	if (pattern != NULL && 
	    coded_length < (int) octstr_len(pattern)) {
	    if (coded_length == 0)
		p = create_octstr_from_node((char *)attr->children); 
	    else
		p = octstr_copy(pattern, coded_length, 
				octstr_len(pattern) - coded_length); 

	    if (check_if_url(wbxml_hex))
		status = parse_attr_value(p, wml_URL_values_list,
					  wbxml, attr->doc->charset, default_esc);
	    else
		status = parse_attr_value(p, wml_attr_values_list,
					  wbxml, attr->doc->charset, default_esc);
	    if (status != 0)
		error(0, 
		      "WML compiler: could not output attribute "
		      "value as a string.");
	    octstr_destroy(p);
	}
    }

    /* Memory cleanup. */
    octstr_destroy(name);

    if (pattern != NULL)
	octstr_destroy(pattern);

    return status;
}



/*
 * parse_attr_value - parses an attributes value using WML value codes.
 */

static int parse_attr_value(Octstr *attr_value, List *tokens,
			    wml_binary_t **wbxml, int charset, var_esc_t default_esc)
{
    int i, pos, wbxml_hex;
    wml_hash_t *temp = NULL;
    Octstr *cut_text = NULL;
    char *tmp;

    /*
     * Beware that libxml2 does internal encoding in UTF-8 while parsing.
     * So if our original WML source had a different encoding set, we have
     * to transcode at least here. Only transcode if target encoding differs
     * from libxml2's internal encoding (UTF-8).
     */
    tmp = (char*) xmlGetCharEncodingName(charset);
    if (charset != XML_CHAR_ENCODING_UTF8 && 
        charset_convert(attr_value, "UTF-8", 
                        tmp) != 0) {
        error(0, "Failed to convert XML attribute value from charset "
                 "<%s> to <%s>, will leave as is.", "UTF-8", 
                 tmp ? tmp : "(undef)");
    }


    /*
     * The attribute value is search for text strings that can be replaced 
     * with one byte codes. Note that the algorith is not foolproof; seaching 
     * is done in an order and the text before first hit is not checked for 
     * those tokens that are after the hit in the order. Most likely it would 
     * be waste of time anyway. String table is not used here, since at least 
     * Nokia 7110 doesn't seem to understand string table references here.
     */

    /* A fast patch to allow reserved names to be variable names. May produce 
       a little longer binary at some points. --tuo */
    if (octstr_search_char(attr_value, '$', 0) >= 0) {
	if (parse_st_octet_string(attr_value, 0, default_esc, wbxml) != 0)
	    return -1;
    } else {

	for (i = 0; i < gwlist_len(tokens); i++) {
	    temp = gwlist_get(tokens, i);
	    pos = octstr_search(attr_value, temp->item, 0);
	    switch (pos) {
	    case -1:
		break;
	    case 0:
		wbxml_hex = temp->binary;
		output_st_char(wbxml_hex, wbxml);	
		octstr_delete(attr_value, 0, octstr_len(temp->item));	
		break;
	    default:
		/* 
		 *  There is some text before the first hit, that has to 
		 *  be handled too. 
		 */
		gw_assert(pos <= octstr_len(attr_value));
	
		cut_text = octstr_copy(attr_value, 0, pos);
		if (parse_st_octet_string(cut_text, 0, default_esc, wbxml) != 0)
		    return -1;
		octstr_destroy(cut_text);
	    
		wbxml_hex = temp->binary;
		output_st_char(wbxml_hex, wbxml);	

		octstr_delete(attr_value, 0, pos + octstr_len(temp->item));
		break;
	    }
	}

	/* 
	 * If no hits, then the attr_value is handled as a normal text, 
	 * otherwise the remaining part is searched for other hits too. 
	 */

	if ((int) octstr_len(attr_value) > 0) {
	    if (i < gwlist_len(tokens))
		parse_attr_value(attr_value, tokens, wbxml, charset, default_esc);
	    else
		if (parse_st_octet_string(attr_value, 0, default_esc, wbxml) != 0)
		    return -1;
	}
    }

    return 0;
}



/*
 * parse_st_end - adds end tag to an element.
 */

static void parse_st_end(wml_binary_t **wbxml)
{
    output_st_char(WBXML_END, wbxml);
}



/*
 * parse_text - a text string parsing function.
 * This function parses a text node. 
 */

static int parse_text(xmlNodePtr node, wml_binary_t **wbxml)
{
    int ret;
    Octstr *temp;
    char* tmp;

    temp = create_octstr_from_node((char *)node); /* returns string in UTF-8 */

    /*
     * Beware that libxml2 does internal encoding in UTF-8 while parsing.
     * So if our original WML source had a different encoding set, we have
     * to transcode at least here. Only transcode if target encoding differs
     * from libxml2's internal encoding (UTF-8).
     */
    tmp = (char*) xmlGetCharEncodingName(node->doc->charset);
    if (node->doc->charset != XML_CHAR_ENCODING_UTF8 && 
        charset_convert(temp, "UTF-8", 
                        tmp) != 0) {
        error(0, "Failed to convert XML text entity from charset "
                 "<%s> to <%s>, will leave as is.", "UTF-8", 
                 tmp ? tmp : "(undef)");
    }

    octstr_shrink_blanks(temp);
    if (!check_if_emphasis(node->prev) && !check_if_emphasis(node->next))
	octstr_strip_blanks(temp);

    if (octstr_len(temp) == 0)
        ret = 0;
    else 
        ret = parse_st_octet_string(temp, 0, NOESC, wbxml);

    /* Memory cleanup. */
    octstr_destroy(temp);

    return ret;
}



/*
 * parse_cdata - a cdata section parsing function.
 * This function parses a cdata section that is outputted into the binary 
 * "as is". 
 */

static int parse_cdata(xmlNodePtr node, wml_binary_t **wbxml)
{
    int ret = 0;
    Octstr *temp;

    temp = create_octstr_from_node((char *)node);

    parse_st_octet_string(temp, 1, NOESC, wbxml);
    
    /* Memory cleanup. */
    octstr_destroy(temp);

    return ret;
}



/*
 * parse_variable - a variable parsing function. 
 * Arguments:
 * - text: the octet string containing a variable
 * - start: the starting position of the variable not including 
 *   trailing &
 * Returns: lenth of the variable for success, -1 for failure, 0 for 
 * variable syntax error, when it will be ignored. 
 * Parsed variable is returned as an octet string in Octstr **output.
 */

static int parse_variable(Octstr *text, int start, var_esc_t default_esc, Octstr **output, 
			  wml_binary_t **wbxml)
{
    var_esc_t esc;
    int ret;
    Octstr *variable;

    variable = get_variable(text, start + 1);
    octstr_truncate(*output, 0);

    if (variable == NULL)
	return 0;

    if (octstr_get_char(variable, 0) == '$') {
	octstr_append_char(*output, '$');
	octstr_destroy(variable);
	ret = 2;
    } else {
	if (octstr_get_char(text, start + 1) == '(')
	    ret = octstr_len(variable) + 3;
	else
	    ret = octstr_len(variable) + 1;

	if ((esc = check_variable_syntax(variable, default_esc)) != FAILED)
	    output_variable(variable, output, esc, wbxml);
	else
	    octstr_destroy(variable);
    }

    return ret;
}



/*
 * get_variable - get the variable name from text.
 * Octstr *text contains the text with a variable name starting at point 
 * int start.
 */

static Octstr *get_variable(Octstr *text, int start)
{
    Octstr *var = NULL;
    long end;
    int ch;

    gw_assert(text != NULL);
    gw_assert(start >= 0 && start <= (int) octstr_len(text));

    ch = octstr_get_char(text, start);

    if (ch == '$') {
	var = octstr_create("$");
    } else if (ch == '(') {
	start ++;
	end = octstr_search_char(text, ')', start);
	if (end == -1)
	    error(0, "WML compiler: braces opened, but not closed for a "
		  "variable.");
	else if (end - start == 0)
	    error(0, "WML compiler: empty braces without variable.");
	else
	    var = octstr_copy(text, start, end - start);
    } else {
	end = start + 1;
	while (isalnum(ch = octstr_get_char(text, end)) || (ch == '_'))
	    end ++;

	var = octstr_copy(text, start, end - start);
    }

    return var;
}



/*
 * check_variable_syntax - checks the variable syntax and the possible 
 * escape mode it has. Octstr *variable contains the variable string.
 */

static var_esc_t check_variable_syntax(Octstr *variable, var_esc_t default_esc)
{
    Octstr *escape;
    char ch;
    int pos, len, i;
    var_esc_t ret;

    if ((pos = octstr_search_char(variable, ':', 0)) > 0) {
	len = octstr_len(variable) - pos;
	escape = octstr_copy(variable, pos + 1, len - 1);
	octstr_truncate(variable, pos);
	octstr_truncate(escape, len);
	octstr_convert_range(escape, 0, octstr_len(escape), tolower);

	if (octstr_str_compare(escape, "noesc") == 0 ||
	    octstr_str_compare(escape, "n") == 0 )
	    ret = NOESC;
	else if (octstr_str_compare(escape, "unesc") == 0 ||
		 octstr_str_compare(escape, "u") == 0 )
	    ret = UNESC;
	else if (octstr_str_compare(escape, "escape") == 0 ||
		 octstr_str_compare(escape, "e") == 0 )
	    ret = ESC;
	else {
	    error(0, "WML compiler: syntax error in variable escaping.");
	    octstr_destroy(escape);
	    return FAILED;
	}
	octstr_destroy(escape);
    } else
	ret = default_esc;

    ch = octstr_get_char(variable, 0);
    if (!(isalpha((int)ch)) && ch != '_') {
	error(0, "WML compiler: syntax error in variable; name starting "
	      "with %c.", ch);
	return FAILED;
    } else
	for (i = 1; i < (int) octstr_len(variable); i++)
	    if (!isalnum((int)(ch = octstr_get_char(variable, 0))) && 
		ch != '_') {
		warning(0, "WML compiler: syntax error in variable.");
		return FAILED;
	    }

    return ret;
}



/*
 * parse_st_octet_string - parse an octet string into wbxml_string, the string 
 * is checked for variables. If string is string table applicable, it will 
 * be checked for string insrtances that are in the string table, otherwise 
 * not. Returns 0 for success, -1 for error.
 */

static int parse_st_octet_string(Octstr *ostr, int cdata, var_esc_t default_esc, wml_binary_t **wbxml)
{
    Octstr *output, *var, *temp = NULL;
    int var_len;
    int start = 0, pos = 0, len;

    /* No variables? Ok, let's take the easy way... (CDATA never contains 
       variables.) */

    if ((pos = octstr_search_char(ostr, '$', 0)) < 0 || cdata == 1) {
	string_table_apply(ostr, wbxml);
	return 0;
    }

    len = octstr_len(ostr);
    output = octstr_create("");
    var = octstr_create("");

    while (pos < len) {
	if (octstr_get_char(ostr, pos) == '$') {
	    if (pos > start) {
		temp = octstr_copy(ostr, start, pos - start);
		octstr_insert(output, temp, octstr_len(output));
		octstr_destroy(temp);
	    }
	  
	    if ((var_len = parse_variable(ostr, pos, default_esc, &var, wbxml)) > 0)	{
		if (octstr_len(var) > 0) {
		    if (octstr_get_char(var, 0) == '$')
			/*
			 * No, it's not actually variable, but $-character 
			 * escaped as "$$". So everything should be packed 
			 * into one string. 
			 */
			octstr_insert(output, var, octstr_len(output));
		    else {
			/*
			 * The string is output as a inline string and the 
			 * variable as a string table variable reference. 
			 */
			if (octstr_len(output) > 0)
			    string_table_apply(output, wbxml);
			octstr_truncate(output, 0);
			output_st_octet_string(var, wbxml);
		    }
		    /* Variable had a syntax error, so it's skipped. */
		}

		pos = pos + var_len;
		start = pos;
	    } else
		return -1;
	} else
	    pos ++;
    }

    /* Was there still something after the last variable? */
    if (start < pos) {
	if (octstr_len(output) == 0) {
	    octstr_destroy(output);
	    output = octstr_copy(ostr, start, pos - start);
	} else {
	    temp = octstr_copy(ostr, start, pos - start);
	    octstr_insert(output, temp, octstr_len(output));
	    octstr_destroy(temp);
	}
    }

    if (octstr_len(output) > 0)
	string_table_apply(output, wbxml);
  
    octstr_destroy(output);
    octstr_destroy(var);
  
    return 0;
}




/*
 * parse_entities - replaces WML entites in the WML source with equivalent
 * numerical entities. A fast patch for WAP 1.1 compliance.
 */

static void parse_entities(Octstr *wml_source)
{
    static char entity_nbsp[] = "&nbsp;";
    static char entity_shy[] = "&shy;";
    static char nbsp[] = "&#160;";
    static char shy[] = "&#173;";
    int pos = 0;
    Octstr *temp;

    if ((pos = octstr_search(wml_source, octstr_imm(entity_nbsp),
			     pos)) >= 0) {
	temp = octstr_create(nbsp);
	while (pos >= 0) {
	    octstr_delete(wml_source, pos, strlen(entity_nbsp));
	    octstr_insert(wml_source, temp, pos);
	    pos = octstr_search(wml_source, 
				octstr_imm(entity_nbsp), pos);
	}
	octstr_destroy(temp);
    }

    pos = 0;
    if ((pos = octstr_search(wml_source, octstr_imm(entity_shy),
			     pos)) >= 0) {
	temp = octstr_create(shy);
	while (pos >= 0) {
	    octstr_delete(wml_source, pos, strlen(entity_shy));
	    octstr_insert(wml_source, temp, pos);
	    pos = octstr_search(wml_source, 
				octstr_imm(entity_shy), pos);
	}
	octstr_destroy(temp);
    }	
}



/*
 * wml_binary_create - reserves memory for the wml_binary_t and sets the 
 * fields to zeros and NULLs.
 */

static wml_binary_t *wml_binary_create(void)
{
    wml_binary_t *wbxml;

    wbxml = gw_malloc(sizeof(wml_binary_t));
    wbxml->wbxml_version = 0x00;
    wbxml->wml_public_id = 0x00;
    wbxml->character_set = 0x00;
    wbxml->string_table_length = 0x00;
    wbxml->string_table = gwlist_create();
    wbxml->wbxml_string = octstr_create("");

    return wbxml;
}



/*
 * wml_binary_destroy - frees the memory allocated for the wml_binary_t.
 */

static void wml_binary_destroy(wml_binary_t *wbxml)
{
    if (wbxml != NULL) {
	gwlist_destroy(wbxml->string_table, NULL);
	octstr_destroy(wbxml->wbxml_string);
	gw_free(wbxml);
    }
}



/*
 * wml_binary_output - outputs all the fiels of wml_binary_t into ostr.
 */

static void wml_binary_output(Octstr *ostr, wml_binary_t *wbxml)
{
    octstr_append_char(ostr, wbxml->wbxml_version);
    octstr_append_uintvar(ostr, wbxml->wml_public_id);
    octstr_append_uintvar(ostr, wbxml->character_set);
    octstr_append_uintvar(ostr, wbxml->string_table_length);

    if (wbxml->string_table_length > 0)
	string_table_output(ostr, &wbxml);

    octstr_insert(ostr, wbxml->wbxml_string, octstr_len(ostr));
}



/*
 * output_st_char - output a character into wbxml_string.
 * Returns 0 for success, -1 for error.
 */

static void output_st_char(int byte, wml_binary_t **wbxml)
{
    octstr_append_char((*wbxml)->wbxml_string, byte);
}



/*
 * output_st_octet_string - output an octet string into wbxml.
 * Returns 0 for success, -1 for an error. No conversions.
 */

static void output_st_octet_string(Octstr *ostr, wml_binary_t **wbxml)
{
    octstr_insert((*wbxml)->wbxml_string, ostr, 
		  octstr_len((*wbxml)->wbxml_string));
}



/*
 * output_variable - output a variable reference into the string table.
 */

static void output_variable(Octstr *variable, Octstr **output, 
			    var_esc_t escaped, wml_binary_t **wbxml)
{
  switch (escaped)
    {
    case ESC:
      octstr_append_char(*output, WBXML_EXT_T_0);
      break;
    case UNESC:
      octstr_append_char(*output, WBXML_EXT_T_1);
      break;
    default:
      octstr_append_char(*output, WBXML_EXT_T_2);
      break;
    }

  octstr_append_uintvar(*output, string_table_add(variable, wbxml));
}



/*
 * hash_create - allocates memory for a 2 field hash table node.
 */

static wml_hash_t *hash_create(char *text, unsigned char token)
{
    wml_hash_t *table_node;

    table_node = gw_malloc(sizeof(wml_hash_t));
    table_node->item = octstr_create(text);
    table_node->binary = token;

    return table_node;
}



/*
 * attribute_create - allocates memory for the attributes hash table node 
 * that contains the attribute, the binary for it and a list of binary values
 * tied with the attribute.
 */

static wml_attribute_t *attribute_create(void)
{
    wml_attribute_t *attr;

    attr = gw_malloc(sizeof(wml_attribute_t));
    attr->attribute = NULL;
    attr->binary = 0;
    attr->value_list = gwlist_create();

    return attr;
}



/*
 * attr_dict_construct - takes a table of attributes and their values and 
 * inputs these into a dictionary. 
 */

static void attr_dict_construct(wml_table3_t *attributes, Dict *attr_dict)
{
    int i = 0;
    wml_attribute_t *node = NULL;
    wml_hash_t *temp = NULL;

    node = attribute_create();

    do {
	if (node->attribute == NULL)
	    node->attribute = octstr_create(attributes[i].text1);
	else if (strcmp(attributes[i].text1, attributes[i-1].text1) != 0) {
	    dict_put(attr_dict, node->attribute, node);
	    node = attribute_create();
	    node->attribute = octstr_create(attributes[i].text1);
	}

	if (attributes[i].text2 == NULL)
	    node->binary = attributes[i].token;
	else {
	    temp = hash_create(attributes[i].text2, attributes[i].token);
	    gwlist_append(node->value_list, (void *)temp);
	}	
	i++;
    } while (attributes[i].text1 != NULL);

    dict_put(attr_dict, node->attribute, node);
}



/*
 * hash_destroy - deallocates memory of a 2 field hash table node.
 */

static void hash_destroy(void *p)
{
    wml_hash_t *node;

    if (p == NULL)
        return;

    node = p;

    octstr_destroy(node->item);
    gw_free(node);
}



/*
 * attribute_destroy - deallocates memory of a attribute hash table node.
 */

static void attribute_destroy(void *p)
{
    wml_attribute_t *node;

    if (p == NULL)
	return;

    node = p;

    octstr_destroy(node->attribute);
    gwlist_destroy(node->value_list, hash_destroy);
    gw_free(node);
}



/*
 * hash_cmp - compares pattern against item and if the pattern matches the 
 * item returns 1, else 0.
 */

static int hash_cmp(void *item, void *pattern)
{
    int ret = 0;

    gw_assert(item != NULL && pattern != NULL);
    gw_assert(((wml_hash_t *)item)->item != NULL);

    if (octstr_search(pattern, ((wml_hash_t *)item)->item, 0) == 0)
	ret = 1;

    return ret;
}


/*
 * check_do_elements - a helper function for parse_element for checking if a
 * card or template element has two or more do elements of the same name. 
 * Returns 0 for OK and -1 for an error (== do elements with same name found).
 */

static int check_do_elements(xmlNodePtr node)
{
    xmlNodePtr child;
    int i, status = 0;
    Octstr *name = NULL;
    List *name_list = NULL;
    
    name_list = gwlist_create();

    if ((child = node->children) != NULL) {
        while (child != NULL) {
            if (child->name && strcmp((char *)child->name, "do") == 0) {
                name = get_do_element_name(child);

                if (name == NULL) {
                    error(0, "WML compiler: no name or type in a do element");
                    return -1;
                }

                for (i = 0; i < gwlist_len(name_list); i ++)
                    if (octstr_compare(gwlist_get(name_list, i), name) == 0) {
                        octstr_destroy(name);
                        status = -1;
                        break;
                    }
                if (status != -1)
                    gwlist_append(name_list, name);
                else
                    break;
            }
            child = child->next;
        }
    }

    gwlist_destroy(name_list, octstr_destroy_item);

    return status;
}



/*
 * check_variable_name - checks the name for variable in a setvar element.
 * If the name has syntax error, -1 is returned, else 0.
 */

static var_esc_t check_variable_name(xmlNodePtr node)
{
    Octstr *name = NULL;
    xmlAttrPtr attr; 
    var_esc_t ret = FAILED;

    if ((attr = node->properties) != NULL) {
        while (attr != NULL) {
            if (attr->name && strcmp((char *)attr->name, "name") == 0) {
                name = create_octstr_from_node((char *)attr->children);
                break;
            }
            attr = attr->next;
        }
    }

    if (attr == NULL) {
        error(0, "WML compiler: no name in a setvar element");
        return FAILED;
    }

    ret = check_variable_syntax(name, NOESC);
    octstr_destroy(name);
    
    return ret;
}



/*
 * get_do_element_name - returns the name for a do element. Name is either 
 * name when the element has the attribute or defaults to the type attribute 
 * if there is no name.
 */

static Octstr *get_do_element_name(xmlNodePtr node)
{
    Octstr *name = NULL;
    xmlAttrPtr attr; 

    if ((attr = node->properties) != NULL) {
        while (attr != NULL) {
            if (attr->name && strcmp((char *)attr->name, "name") == 0) {
                name = create_octstr_from_node((char *)attr->children);
                break;
            }
            attr = attr->next;
        }

        if (attr == NULL) {
            attr = node->properties;
            while (attr != NULL) {
                if (attr->name && strcmp((char *)attr->name, "type") == 0) {
                    name = create_octstr_from_node((char *)attr->children);
                    break;
                }
                attr = attr->next;
            }
        }
    }

    return name;
}



/*
 * check_if_url - checks whether the attribute value is an URL or some other 
 * kind of value. Returns 1 for an URL and 0 otherwise.
 */

static int check_if_url(int hex)
{
    switch ((unsigned char) hex) {
    case 0x4A: case 0x4B: case 0x4C: /* href, href http://, href https:// */
    case 0x32: case 0x58: case 0x59: /* src, src http://, src https:// */
	return 1;
	break;
    }
    return 0;
}



/*
 * check_if_emphasis - checks if the node is an emphasis element. 
 * Returns 1 for an emphasis and 0 otherwise.
 */

static int check_if_emphasis(xmlNodePtr node)
{
    if (node == NULL || node->name == NULL)
	return 0;

    if (strcmp((char *)node->name, "b") == 0)
	return 1;
    if (strcmp((char *)node->name, "big") == 0)
	return 1;
    if (strcmp((char *)node->name, "em") == 0)
	return 1;
    if (strcmp((char *)node->name, "i") == 0)
	return 1;
    if (strcmp((char *)node->name, "small") == 0)
	return 1;
    if (strcmp((char *)node->name, "strong") == 0)
	return 1;
    if (strcmp((char *)node->name, "u") == 0)
	return 1;

    return 0;
}


/*
 * wml_table_len - returns the length of a wml_table_t array.
 */

static int wml_table_len(wml_table_t *table)
{
    int i = 0;

    while (table[i].text != NULL)
	i++;

    return i;
}



/*
 * wml_table3_len - returns the length of a wml_table3_t array.
 */

static int wml_table3_len(wml_table3_t *table)
{
    int i = 0;

    while (table[i].text1 != NULL)
	i++;

    return i;
}



/*
 * string_table_create - reserves memory for the string_table_t and sets the 
 * fields.
 */

static string_table_t *string_table_create(int offset, Octstr *ostr)
{
    string_table_t *node;

    node = gw_malloc(sizeof(string_table_t));
    node->offset = offset;
    node->string = ostr;

    return node;
}



/*
 * string_table_destroy - frees the memory allocated for the string_table_t.
 */

static void string_table_destroy(string_table_t *node)
{
    if (node != NULL) {
	octstr_destroy(node->string);
	gw_free(node);
    }
}



/*
 * string_table_proposal_create - reserves memory for the 
 * string_table_proposal_t and sets the fields.
 */

static string_table_proposal_t *string_table_proposal_create(Octstr *ostr)
{
    string_table_proposal_t *node;

    node = gw_malloc(sizeof(string_table_proposal_t));
    node->count = 1;
    node->string = ostr;

    return node;
}



/*
 * string_table_proposal_destroy - frees the memory allocated for the 
 * string_table_proposal_t.
 */

static void string_table_proposal_destroy(string_table_proposal_t *node)
{
    if (node != NULL) {
	octstr_destroy(node->string);
	gw_free(node);
    }
}



/*
 * string_table_build - collects the strings from the WML source into a list, 
 * adds those strings that appear more than once into string table. The rest 
 * of the strings are sliced into words and the same procedure is executed to 
 * the list of these words.
 */

static void string_table_build(xmlNodePtr node, wml_binary_t **wbxml)
{
    string_table_proposal_t *item = NULL;
    List *list = NULL;

    list = gwlist_create();

    string_table_collect_strings(node, list);

    list = string_table_add_many(string_table_sort_list(list), wbxml);

    list =  string_table_collect_words(list);

    /* Don't add strings if there aren't any. (no NULLs please) */
    if (list) {
	list = string_table_add_many(string_table_sort_list(list), wbxml);
    }

    /* Memory cleanup. */
    while (gwlist_len(list)) {
	item = gwlist_extract_first(list);
	string_table_proposal_destroy(item);
    }

    gwlist_destroy(list, NULL);
}



/*
 * string_table_collect_strings - collects the strings from the WML 
 * ocument into a list that is then further processed to build the 
 * string table for the document.
 */

static void string_table_collect_strings(xmlNodePtr node, List *strings)
{
    Octstr *string;
    xmlAttrPtr attribute;

    switch (node->type) {
    case XML_TEXT_NODE:
	string = create_octstr_from_node((char *)node);
	    
	octstr_shrink_blanks(string);
	octstr_strip_blanks(string);
	if (octstr_len(string) > WBXML_STRING_TABLE_MIN)
	    octstr_strip_nonalphanums(string);

	if (octstr_len(string) > WBXML_STRING_TABLE_MIN)
	    gwlist_append(strings, string);
	else 
	    octstr_destroy(string);
	break;
    case XML_ELEMENT_NODE:
	if(node->properties != NULL) {
	    attribute = node->properties;
	    while (attribute != NULL) {
		if (attribute->children != NULL)
		    string_table_collect_strings(attribute->children, strings);
		attribute = attribute->next;
	    }
	}
	break;
    default:
	break;
    }

    if (node->children != NULL)
	string_table_collect_strings(node->children, strings);

    if (node->next != NULL)
	string_table_collect_strings(node->next, strings);
}



/*
 * string_table_sort_list - takes a list of octet strings and returns a list
 * of string_table_proposal_t:s that contains the same strings with number of 
 * instants of every string in the input list.
 */

static List *string_table_sort_list(List *start)
{
    int i;
    Octstr *string = NULL;
    string_table_proposal_t *item = NULL;
    List *sorted = NULL;

    sorted = gwlist_create();

    while (gwlist_len(start)) {
	string = gwlist_extract_first(start);
      
	/* Check whether the string is unique. */
	for (i = 0; i < gwlist_len(sorted); i++) {
	    item = gwlist_get(sorted, i);
	    if (octstr_compare(item->string, string) == 0) {
		octstr_destroy(string);
		string = NULL;
		item->count ++;
		break;
	    }
	}
	
	if (string != NULL) {
	    item = string_table_proposal_create(string);
	    gwlist_append(sorted, item);
	}
    }

    gwlist_destroy(start, NULL);

    return sorted;
}



/*
 * string_table_add_many - takes a list of string with number of instants and
 * adds those whose number is greater than 1 into the string table. Returns 
 * the list ofrejected strings for memory cleanup.
 */

static List *string_table_add_many(List *sorted, wml_binary_t **wbxml)
{
    string_table_proposal_t *item = NULL;
    List *list = NULL;

    list = gwlist_create();

    while (gwlist_len(sorted)) {
	item = gwlist_extract_first(sorted);

	if (item->count > 1 && octstr_len(item->string) > 
	    WBXML_STRING_TABLE_MIN) {
	    string_table_add(octstr_duplicate(item->string), wbxml);
	    string_table_proposal_destroy(item);
	} else
	    gwlist_append(list, item);
    }

    gwlist_destroy(sorted, NULL);

    return list;
}



/*
 * string_table_collect_words - takes a list of strings and returns a list 
 * of words contained by those strings.
 */

static List *string_table_collect_words(List *strings)
{
    Octstr *word = NULL;
    string_table_proposal_t *item = NULL;
    List *list = NULL, *temp_list = NULL;

    while (gwlist_len(strings)) {
	item = gwlist_extract_first(strings);

	if (list == NULL) {
	    list = octstr_split_words(item->string);
	    string_table_proposal_destroy(item);
	} else {
	    temp_list = octstr_split_words(item->string);

	    while ((word = gwlist_extract_first(temp_list)) != NULL)
		gwlist_append(list, word);

	    gwlist_destroy(temp_list, NULL);
	    string_table_proposal_destroy(item);
	}
    }

    gwlist_destroy(strings, NULL);

    return list;
}



/*
 * string_table_add - adds a string to the string table. Duplicates are
 * discarded. The function returns the offset of the string in the 
 * string table; if the string is already in the table then the offset 
 * of the first copy.
 */

static unsigned long string_table_add(Octstr *ostr, wml_binary_t **wbxml)
{
    string_table_t *item = NULL;
    unsigned long i, offset = 0;

    /* Check whether the string is unique. */
    for (i = 0; i < (unsigned long)gwlist_len((*wbxml)->string_table); i++) {
	item = gwlist_get((*wbxml)->string_table, i);
	if (octstr_compare(item->string, ostr) == 0) {
	    octstr_destroy(ostr);
	    return item->offset;
	}
    }

    /* Create a new list item for the string table. */
    offset = (*wbxml)->string_table_length;

    item = string_table_create(offset, ostr);

    (*wbxml)->string_table_length = 
	(*wbxml)->string_table_length + octstr_len(ostr) + 1;
    gwlist_append((*wbxml)->string_table, item);

    return offset;
}



/*
 * string_table_apply - takes a octet string of WML bnary and goes it 
 * through searching for substrings that are in the string table and 
 * replaces them with string table references.
 */

static void string_table_apply(Octstr *ostr, wml_binary_t **wbxml)
{
    Octstr *input = NULL;
    string_table_t *item = NULL;
    long i = 0, word_s = 0, str_e = 0;

    input = octstr_create("");

    for (i = 0; i < gwlist_len((*wbxml)->string_table); i++) {
	item = gwlist_get((*wbxml)->string_table, i);

	if (octstr_len(item->string) > WBXML_STRING_TABLE_MIN)
	    /* No use to replace 1 to 3 character substring, the reference 
	       will eat the saving up. A variable will be in the string table 
	       even though it's only 1 character long. */
	    if ((word_s = octstr_search(ostr, item->string, 0)) >= 0) {
		/* Check whether the octet string are equal if they are equal 
		   in length. */
		if (octstr_len(ostr) == octstr_len(item->string)) {
		    if ((word_s = octstr_compare(ostr, item->string)) == 0)
		    {
			octstr_truncate(ostr, 0);
			octstr_append_char(ostr, WBXML_STR_T);
			octstr_append_uintvar(ostr, item->offset);
			str_e = 1;
		    }
		}
		/* Check the possible substrings. */
		else if (octstr_len(ostr) > octstr_len(item->string))
		{
		    if (word_s + octstr_len(item->string) == octstr_len(ostr))
			str_e = 1;

		    octstr_delete(ostr, word_s, octstr_len(item->string));

		    octstr_truncate(input, 0);
		    /* Substring in the start? No STR_END then. */
		    if (word_s > 0)
			octstr_append_char(input, WBXML_STR_END);
                  
		    octstr_append_char(input, WBXML_STR_T);
		    octstr_append_uintvar(input, item->offset);

		    /* Subtring the end? No need to start a new one. */
		    if ( word_s < octstr_len(ostr))
			octstr_append_char(input, WBXML_STR_I);

		    octstr_insert(ostr, input, word_s);
		}
		/* If te string table entry is longer than the string, it can 
		   be skipped. */
	    }
    }

    octstr_destroy(input);

    if (octstr_get_char(ostr, 0) != WBXML_STR_T)
	output_st_char(WBXML_STR_I, wbxml);
    if (!str_e)
	octstr_append_char(ostr, WBXML_STR_END);    

    output_st_octet_string(ostr, wbxml);
}



/*
 * string_table_output - writes the contents of the string table 
 * into an octet string that is sent to the phone.
 */

static void string_table_output(Octstr *ostr, wml_binary_t **wbxml)
{
    string_table_t *item;

    while ((item = gwlist_extract_first((*wbxml)->string_table)) != NULL) {
	octstr_insert(ostr, item->string, octstr_len(ostr));
	octstr_append_char(ostr, WBXML_STR_END);
	string_table_destroy(item);
    }
}














