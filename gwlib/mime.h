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
 * mime.h - Implement MIME multipart/related handling 
 * 
 * References:
 *   RFC 2387 (The MIME Multipart/Related Content-type)
 *   RFC 2025 (Multipurpose Internet Mail Extensions [MIME])
 *
 * Here we implement generic parsing functions to handle MIME multipart/related
 * messages. Our representation is made by the struct MIMEEntity where each
 * entity of the MIME scope is hold. Every entity should at least have headers.
 * If the body is a "normal" content-type, then it is stored in Octstr *body.
 * if the body is a sub-MIME multipart/related again, then it's single entities
 * are parsed and chained into the List *multiparts. Hence items in the list
 * are of type MIMEEntity again. We result in a recursive linked represenation
 * of the MIME multipart/related structure.
 *
 * We know about two various multipart types:
 *   multipart/mixed - where no "order" is associated among the entities
 *   multipart/related - where the "start" parameter in the Content-Type defines 
 *                       the semantically main processing entitiy in the set.
 *
 * In order to provide a mapping facility between the MIMEEntity representation
 * and an Octstr that holds the MIME document we have the two major functions
 * mime_octstr_to_entity() and mime_entity_to_octstr() that act as converters.
 *
 * Using the MIMEEntity representation structure it is easier to handle the 
 * subsequent MIME entities in case we have a cubed structure in the document.
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#ifndef MIME_H
#define MIME_H

#include "gwlib/gwlib.h"


/* Define an generic MIME entity structure to be 
 * used for MIME multipart parsing. */
typedef struct MIMEEntity MIMEEntity;


/* create and destroy MIME multipart entity */
MIMEEntity *mime_entity_create(void);
void mime_entity_destroy(MIMEEntity *e);

/* make a copy of a MIME object. */
MIMEEntity *mime_entity_duplicate(MIMEEntity *e);


/* Replace top-level MIME headers: Old ones removed completetly */
void mime_replace_headers(MIMEEntity *e, List *headers);


/* Get number of body parts. Returns 0 if this is not
 * a multipart object.
 */
int mime_entity_num_parts(MIMEEntity *e);

/* Append  a new part to list of body parts. Copy is made*/ 
void mime_entity_add_part(MIMEEntity *e, MIMEEntity *part);

/* Get part i in list of body parts. Copy is made*/ 
MIMEEntity *mime_entity_get_part(MIMEEntity *e, int i);

/* Replace part i in list of body parts.  Old one will be deleted */ 
void mime_entity_replace_part(MIMEEntity *e, int i, MIMEEntity *newpart);

/* Remove part i in list of body parts. */ 
void mime_entity_remove_part(MIMEEntity *e, int i);

/* Change body element of non-multipart entity. */
void mime_entity_set_body(MIMEEntity *e, Octstr *body);

/* Returns (copy of) the 'start' element of a multi-part entity. */
MIMEEntity *mime_multipart_start_elem(MIMEEntity *e);

/*
 * Parse the given Octstr that contains a normative text representation
 * of the MIME multipart document into our representation strucuture.
 * Will return parts of it in case an error occures in the recursive
 * call, otherwise will return NULL in case of a fatal error while parsing. 
 */
MIMEEntity *mime_octstr_to_entity(Octstr *mime);

/*
 * Parse the given List (HTTP hedaer) and Octstr (HTTP body) and create
 * a MIME multipart representatin if possible. Return NULL if parsing fails.
 */
MIMEEntity *mime_http_to_entity(List *headers, Octstr *body);

/*
 * Convert a given MIME multipart representation structure to
 * it's Octstr counterpart and return it. Will return parts of it,
 * in case an error occures in a deeper level, otherwise a NULL
 * is return in case of a fatal error while convertion.
 */
Octstr *mime_entity_to_octstr(MIMEEntity *m);

/*
 * Take a MIME multipart representation and return the global header
 * for it. Return a pointer to a HTTP header List.
 */
List *mime_entity_headers(MIMEEntity *m);

/*
 * Take a MIME multipart representation and return the highest level
 * body encoded to an Octstr to tbe used as HTTP POST body.
 * Rerturns an Octstr with the body.
 */
Octstr *mime_entity_body(MIMEEntity *m);

/*
 * Dump the structure (hicharchical view) of the MIME representation
 * structure into our DEBUG log level facility.
 */
void mime_entity_dump(MIMEEntity *m);


#endif
