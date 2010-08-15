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

/* wsp_caps.c - implement interface to WSP capability negotiation
 *
 * Richard Braakman
 */

#include "gwlib/gwlib.h"
#include "wsp_caps.h"


static void wsp_cap_destroy_item(void *cap) {
	wsp_cap_destroy(cap);
}


Capability *wsp_cap_create(int id, Octstr *name, Octstr *data) {
	Capability *new_cap;

	new_cap = gw_malloc(sizeof(*new_cap));
	new_cap->id = id;
	new_cap->name = name;
	new_cap->data = data;
	new_cap->accept = 0;

	return new_cap;
}

void wsp_cap_destroy(Capability *cap) {
	if (cap == NULL)
		return;

	octstr_destroy(cap->name);
	octstr_destroy(cap->data);
	gw_free(cap);
}

void wsp_cap_dump(Capability *cap) {
	debug("wsp", 0, "Dumping capability at %p:", cap);
	if (cap) {
		debug("wsp", 0, " id = %d", cap->id);
		debug("wsp", 0, " name:");
		octstr_dump(cap->name, 1);
		debug("wsp", 0, " data:");
		octstr_dump(cap->data, 1);
		if (cap->data == NULL)
			debug("wsp", 0, " accept: %d", cap->accept);
	}
	debug("wsp", 0, "Capability dump ends.");
}

void wsp_cap_dump_list(List *caps_list) {
	long i;

	if (caps_list == NULL) {
		debug("wsp", 0, "NULL capability list");
		return;
	}
	debug("wsp", 0, "Dumping capability list at %p, length %ld",
		caps_list, gwlist_len(caps_list));
	for (i = 0; i < gwlist_len(caps_list); i++) {
		wsp_cap_dump(gwlist_get(caps_list, i));
	}
	debug("wsp", 0, "End of capability list dump");
}

void wsp_cap_destroy_list(List *caps_list) {
	gwlist_destroy(caps_list, wsp_cap_destroy_item);
}

List *wsp_cap_duplicate_list(List *caps_list) {
	Capability *cap;
	List *new_list;
	long i;

	new_list = gwlist_create();

	if (caps_list == NULL)
		return new_list;

	for (i = 0; i < gwlist_len(caps_list); i++) {
		cap = gwlist_get(caps_list, i);
		gwlist_append(new_list, wsp_cap_duplicate(cap));
	}
	return new_list;
};

Capability *wsp_cap_duplicate(Capability *cap) {
	Capability *new_cap;

	if (!cap)
		return NULL;

	new_cap = wsp_cap_create(cap->id,
				octstr_duplicate(cap->name),
				octstr_duplicate(cap->data));
	new_cap->accept = cap->accept;
	return new_cap;
}

List *wsp_cap_unpack_list(Octstr *caps) {
	List *caps_list;
	long pos, capslen;

	caps_list = gwlist_create();
	if (caps == NULL)
		return caps_list;

	capslen = octstr_len(caps);
	pos = 0;
	while (pos < capslen) {
		unsigned long length;
		int id;
		Octstr *name;
		Octstr *data;

		pos = octstr_extract_uintvar(caps, &length, pos);
		if (pos < 0 || length == 0)
			goto error;

		id = octstr_get_char(caps, pos);
		if (id >= 0x80) {
			id &= 0x7f; /* It's encoded as a short-integer */
			name = NULL;
			data = octstr_copy(caps, pos + 1, length - 1);
		} else {
			long nullpos;
			id = -1;  /* It's encoded as token-text */
			nullpos = octstr_search_char(caps, 0, pos);
			if (nullpos < 0)
                            goto error;
                        /* check length
                         * FIXME: If it's not allowed that data is empty then change check
                         *        to <= .
                         */
                        if (length < (nullpos + 1 - pos))
                            goto error;
			name = octstr_copy(caps, pos, nullpos - pos);
			data = octstr_copy(caps, nullpos + 1,
				length - (nullpos + 1 - pos));
		}
		gwlist_append(caps_list, wsp_cap_create(id, name, data));
		pos += length;
	}

	return caps_list;

error:
	warning(0, "WSP: Error unpacking capabilities");
	return caps_list;
}

Octstr *wsp_cap_pack_list(List *caps_list) {
	Octstr *result;
	Capability *cap;
	long i, len;

	result = octstr_create("");
	len = gwlist_len(caps_list);
	for (i = 0; i < len; i++) {
		long datalen;

		cap = gwlist_get(caps_list, i);

		datalen = 0;
		if (cap->data)
			datalen = octstr_len(cap->data);

		if (datalen == 0 && cap->accept)
			continue;

		if (cap->name) {
			if (octstr_get_char(cap->name, 0) >= 0x80 ||
			    octstr_search_char(cap->name, 0, 0) >= 0) {
				error(0, "WSP: Bad capability.");
				wsp_cap_dump(cap);
				continue;
			}
			/* Add length */
			octstr_append_uintvar(result,
				octstr_len(cap->name) + 1 + datalen);
			/* Add identifier */
			octstr_append(result, cap->name);
			octstr_append_char(result, 0);
		} else {
			if (cap->id >= 0x80 || cap->id < 0) {
				error(0, "WSP: Bad capability.");
				wsp_cap_dump(cap);
				continue;
			}
			/* Add length */
			octstr_append_uintvar(result, 1 + datalen);
			/* Add identifier */
			octstr_append_char(result, 0x80 | cap->id);
		}
		/* Add payload, if any */
		if (cap->data) {
			octstr_append(result, cap->data);
		}
	}

	return result;
}

static int wsp_cap_get_data(List *caps_list, int id, Octstr *name,
			Octstr **data) {
	long i, len;
	Capability *cap;
	int found;

	len = gwlist_len(caps_list);
	found = 0;
	*data = NULL;
	for (i = 0; i < len; i++) {
		cap = gwlist_get(caps_list, i);
		if ((name && cap->name 
		     && octstr_compare(name, cap->name) == 0)
		    || (!name && cap->id == id)) {
			if (!found)
				*data = cap->data;
			found++;
		}
	}

	return found;
}
		
int wsp_cap_count(List *caps_list, int id, Octstr *name) {
	Octstr *data;

	return wsp_cap_get_data(caps_list, id, name, &data);
}

int wsp_cap_get_client_sdu(List *caps_list, unsigned long *sdu) {
	Octstr *data;
	int found;

	found = wsp_cap_get_data(caps_list, WSP_CAPS_CLIENT_SDU_SIZE,
					NULL, &data);
	if (found > 0 && octstr_extract_uintvar(data, sdu, 0) < 0)
		return -1;

	return found;
}

int wsp_cap_get_server_sdu(List *caps_list, unsigned long *sdu) {
	Octstr *data;
	int found;

	found = wsp_cap_get_data(caps_list, WSP_CAPS_SERVER_SDU_SIZE,
					NULL, &data);
	if (found > 0 && octstr_extract_uintvar(data, sdu, 0) < 0)
		return -1;

	return found;
}

int wsp_cap_get_method_mor(List *caps_list, unsigned long *mor) {
	Octstr *data;
	int found;
	int c;

	found = wsp_cap_get_data(caps_list, WSP_CAPS_METHOD_MOR, NULL, &data);
	if (found > 0) {
		c = octstr_get_char(data, 0);
		if (c < 0)
			return -1;
		*mor = c;
	}
	
	return found;
}

int wsp_cap_get_push_mor(List *caps_list, unsigned long *mor) {
	Octstr *data;
	int found;
	int c;

	found = wsp_cap_get_data(caps_list, WSP_CAPS_PUSH_MOR, NULL, &data);
	if (found > 0) {
		c = octstr_get_char(data, 0);
		if (c < 0)
			return -1;
		*mor = c;
	}
	
	return found;
}
