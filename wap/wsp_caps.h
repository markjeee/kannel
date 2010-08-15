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

/* wsp_caps.h - interface to WSP capability negotiation
 *
 * Richard Braakman
 */

#ifndef WSP_CAPS_H
#define WSP_CAPS_H

#include "gwlib/gwlib.h"

struct capability {
	/* One or the other of these is set.  id is only meaningful
	 * if name is NULL.  (Unfortunately the WSP spec does not
	 * really assign names to the numeric ids, so we can't translate
	 * them all to text.) */
	int id;
	Octstr *name;

	/* Raw data for this capability.  Can be NULL if there is none. */
	Octstr *data;

	/* If data is NULL, this field determines if the request should
	 * be accepted or rejected. */
	int accept;
};

typedef struct capability Capability;

/* See table 37 */
enum known_caps {
	WSP_CAPS_CLIENT_SDU_SIZE = 0,
	WSP_CAPS_SERVER_SDU_SIZE = 1,
	WSP_CAPS_PROTOCOL_OPTIONS = 2,
	WSP_CAPS_METHOD_MOR = 3,
	WSP_CAPS_PUSH_MOR = 4,
	WSP_CAPS_EXTENDED_METHODS = 5,
	WSP_CAPS_HEADER_CODE_PAGES = 6,
	WSP_CAPS_ALIASES = 7,
	WSP_NUM_CAPS
};

/* Create a new Capability structure.  For numbered capabilities (which
 * is all of the known ones), use NULL for the name.  The data may also
 * be NULL. */
Capability *wsp_cap_create(int id, Octstr *name, Octstr *data);
void wsp_cap_destroy(Capability *cap);

void wsp_cap_dump(Capability *cap);
void wsp_cap_dump_list(List *caps_list);

/* Destroy all Capabilities in a list, as well as the list itself. */
void wsp_cap_destroy_list(List *caps_list);

/* Duplicate a list of Capabilities */
List *wsp_cap_duplicate_list(List *cap);
Capability *wsp_cap_duplicate(Capability *cap);

/* Return a list of Capability structures */
List *wsp_cap_unpack_list(Octstr *caps);

/* Encode a list of Capability structures according to the WSP spec */
Octstr *wsp_cap_pack_list(List *caps_list);

/* Access functions.  All of them return the number of requests that 
 * match the capability being searched for, and if they have an output
 * parameter, they set it to the value of the first such request. */
int wsp_cap_count(List *caps_list, int id, Octstr *name);
int wsp_cap_get_client_sdu(List *caps_list, unsigned long *sdu);
int wsp_cap_get_server_sdu(List *caps_list, unsigned long *sdu);
int wsp_cap_get_method_mor(List *caps_list, unsigned long *mor);
int wsp_cap_get_push_mor(List *caps_list, unsigned long *mor);

#endif
