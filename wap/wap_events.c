/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2008 Kannel Group  
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
 * wap_events.c - functions for manipulating wapbox events
 *
 * Aarno Syvänen
 * Lars Wirzenius
 * Nikos Balkanas, Inaccess Networks (2009)
 */


#include "gwlib/gwlib.h"
#include "wsp_caps.h"
#include "wap_events.h"
#include "wtls_pdu.h"

WAPEvent *wap_event_create_real(WAPEventName type, const char *file, long line,
                                const char *func) {
	WAPEvent *event;
	
	gw_assert(type >= 0);
	gw_assert(type < WAPEventNameCount);

	event = gw_malloc_trace(sizeof(WAPEvent), file, line, func);
	event->type = type;

	switch (event->type) {
	#define WAPEVENT(name, prettyname, fields) \
		case name: \
			{ struct name *p = &event->u.name; fields } \
			break;
	#define OCTSTR(name) p->name = NULL;
	#define OPTIONAL_OCTSTR(name) p->name = NULL;
	#define INTEGER(name) p->name = 0;
	#define WTLSPDUS(name) p->name = NULL;
	#define HTTPHEADER(name) p->name = NULL;
	#define ADDRTUPLE(name) p->name = NULL;
	#define CAPABILITIES(name) p->name = NULL;
	#include "wap_events.def"
	default:
		panic(0, "Unknown WAP event type %d", event->type);
	}
	
	return event;
}


void wap_event_destroy(WAPEvent *event) {
	if (event == NULL)
		return;

	wap_event_assert(event);

	switch (event->type) {
	#define WAPEVENT(name, prettyname, fields) \
		case name: \
			{ struct name *p = &event->u.name; fields; } \
			break;
	#define OCTSTR(name) octstr_destroy(p->name);
	#define OPTIONAL_OCTSTR(name) octstr_destroy(p->name);
	#define INTEGER(name) p->name = 0;
#ifdef HAVE_WTLS_OPENSSL
	#define WTLSPDUS(name) wtls_pldList_destroy(p->name);
#endif /* HAVE_WTLS_OPENSSL */
	#define HTTPHEADER(name) http_destroy_headers(p->name);
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(p->name);
	#define CAPABILITIES(name) wsp_cap_destroy_list(p->name);
	#include "wap_events.def"
	default:
		panic(0, "Unknown WAPEvent type %d", (int) event->type);
	}
	gw_free(event);
}


void wap_event_destroy_item(void *event) {
	wap_event_destroy(event);
}


WAPEvent *wap_event_duplicate(WAPEvent *event) {
	WAPEvent *new;
	
	if (event == NULL)
		return NULL;

	wap_event_assert(event);

	new = gw_malloc(sizeof(WAPEvent));
	new->type = event->type;

	switch (event->type) {
	#define WAPEVENT(name, prettyname, fields) \
		case name: \
			{ struct name *p = &new->u.name; \
			  struct name *q = &event->u.name; \
			  fields } \
			break;
	#define OCTSTR(name) p->name = octstr_duplicate(q->name);
	#define OPTIONAL_OCTSTR(name) p->name = octstr_duplicate(q->name);
	#define INTEGER(name) p->name = q->name;
    #define WTLSPDUS(name) debug("wap.events",0,"You need to implement wtls_pdulist_duplicate!");
	#define HTTPHEADER(name) p->name = http_header_duplicate(q->name);
	#define ADDRTUPLE(name) p->name = wap_addr_tuple_duplicate(q->name);
	#define CAPABILITIES(name) p->name = wsp_cap_duplicate_list(q->name);
	#include "wap_events.def"
	default:
		panic(0, "Unknown WAP event type %d", event->type);
	}
	
	return new;
}


const char *wap_event_name(WAPEventName type) {
	switch (type) {
	#define WAPEVENT(name, prettyname, fields) \
		case name: return prettyname;
	#include "wap_events.def"
	default:
		panic(0, "Unknown WAPEvent type %d", (int) type);
		return "unknown WAPEventName";
	}
}


void wap_event_dump(WAPEvent *event) {
	debug("wap.event", 0, "Dumping WAPEvent %p", (void *) event);
	if (event != NULL) {
		debug("wap.event", 0, "  type = %s", 
			wap_event_name(event->type));
		switch (event->type) {
		#define WAPEVENT(name, prettyname, fields) \
			case name: \
			{ struct name *p = &event->u.name; fields; break; }
		#define OCTSTR(name) \
			debug("wap.event", 0, "%s =", #name); \
			octstr_dump(p->name, 1);
		#define OPTIONAL_OCTSTR(name) \
			if (p->name == NULL) \
				debug("wap.event", 0, "%s = NULL", #name); \
			else { \
				debug("wap.event", 0, "%s =", #name); \
				octstr_dump(p->name, 1); \
			}
		#define INTEGER(name) \
			debug("wap.event", 0, "  %s = %ld", #name, p->name);
        #define WTLSPDUS(name) \
			debug("wap.event",0,"You need to implement wtls_payloadlist_dump!");
		#define HTTPHEADER(name) \
			if (p->name == NULL) \
				debug("wap.event", 0, "%s = NULL", #name); \
			else \
			    http_header_dump(p->name);
		#define ADDRTUPLE(name)     wap_addr_tuple_dump(p->name);
		#define CAPABILITIES(name)  wsp_cap_dump_list(p->name);
		#include "wap_events.def"
		default:
			debug("wap.event", 0, "Unknown type");
		}
	}
	debug("wap.event", 0, "WAPEvent dump ends.");
}


void wap_event_assert(WAPEvent *event) {
	gw_assert(event != NULL),
	gw_assert(event->type >= 0);
	gw_assert(event->type < WAPEventNameCount);

	switch (event->type) {
#define WAPEVENT(name, prettyname, fields) \
	case name: \
	{ struct name *p = &event->u.name; fields; p = NULL; break; }
#define OCTSTR(name) \
	gw_assert(p->name != NULL); \
	/* This is a trick to make the Octstr module run its assertions */ \
	gw_assert(octstr_len(p->name) >= 0);
#define OPTIONAL_OCTSTR(name) \
	gw_assert(p->name == NULL || octstr_len(p->name) >= 0);
#define INTEGER(name)
#define WTLSPDUS(name)
#define HTTPHEADER(name)
#define ADDRTUPLE(name) \
	gw_assert(p->name != NULL);
#define CAPABILITIES(name)
#include "wap_events.def"
	default:
		debug("wap.event", 0, "Unknown type");
	}
}
