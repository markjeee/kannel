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
 * xml_shared.h - Common xml tokenizer interface
 * This file contains mainly character set functions and binary manipulating
 * functions used with a binary without a string table.
 *
 * Tuomas Luttinen for Wapit Ltd and Aarno Syvänen for Wiral Ltd.
 */

#ifndef XML_SHARED_H
#define XML_SHARED_H

/*
 * Charset type is used by WML, SI and SL.
 */
typedef struct charset_t charset_t;

/*
 * XML binary type not containing a string table. This is used for SI and SL.
 */

typedef struct simple_binary_t simple_binary_t;

#include "gwlib/gwlib.h"

/*
 * XML binary type not containing a string table. This is used for SI and SL.
 */
struct simple_binary_t {
    unsigned char wbxml_version;
    unsigned char public_id;
    unsigned long charset;
    Octstr *binary;
    int code_page;
};

/*
 * Prototypes of common functions. First functions common with wml, si and sl
 * compilers.
 *
 * set_charset - set the charset of the http headers into the document, if 
 * it has no encoding set.
 */
void set_charset(Octstr *document, Octstr *charset);

/*
 * find_charset_encoding -- parses for a encoding argument within
 * the xml preabmle, ie. <?xml verion="xxx" encoding="ISO-8859-1"?> 
 */
Octstr *find_charset_encoding(Octstr *document);

/*
 * element_check_content - a helper function for checking if an element has 
 * content or attributes. Returns status bit for attributes (0x80) and another
 * for content (0x40) added into one octet.
 */
unsigned char element_check_content(xmlNodePtr node);

/*
 * only_blanks - checks if a text node contains only white space, when it can 
 * be left out as a element content.
 */

int only_blanks(const char *text);

/*
 * Parses the character set of the document given as Octstr. Returns the
 * MIBenum value. If the charset is not found, we default to UTF-8 value. 
 */
int parse_charset(Octstr *os);

/*
 * Return the character sets supported by the WML compiler, as a List
 * of Octstrs, where each string is the MIME identifier for one charset.
 */
List *wml_charsets(void);

/*
 * Macro for creating an octet string from a node content. This has two 
 * versions for different libxml node content implementation methods. 
 */

#ifdef XML_USE_BUFFER_CONTENT
#define create_octstr_from_node(node) (octstr_create(node->content->content))
#else
#define create_octstr_from_node(node) (octstr_create(node->content))
#endif

#endif

/*
 * Functions working with simple binary type (no string table)
 */

simple_binary_t *simple_binary_create(void);

void simple_binary_destroy(simple_binary_t *bxml);

/*
 * Output the sibxml content field after field into octet string os. We add 
 * string table length 0 (no string table) before the content.
 */
void simple_binary_output(Octstr *os, simple_binary_t *bxml);

void parse_end(simple_binary_t **bxml);

void output_char(int byte, simple_binary_t **bxml);

void parse_octet_string(Octstr *os, simple_binary_t **bxml);

/*
 * Add global tokens to the start and to the end of an inline string.
 */ 
void parse_inline_string(Octstr *temp, simple_binary_t **bxml);

void output_octet_string(Octstr *os, simple_binary_t **bxml);




