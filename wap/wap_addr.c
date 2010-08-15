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
 * wap_addr.c - implement WAPAddr and WAPAddrTuple types.
 */

#include <stdlib.h>
#include <arpa/inet.h>

#include "gwlib/gwlib.h"
#include "wap_addr.h"


WAPAddr *wap_addr_create(Octstr *address, long port) 
{
    WAPAddr *addr;
    
    addr = gw_malloc(sizeof(*addr));
    addr->address = octstr_duplicate(address);
    addr->iaddr = inet_addr(octstr_get_cstr(address));
    addr->port = port;
    return addr;
}


void wap_addr_destroy(WAPAddr *addr) 
{
    if (addr != NULL) {
        octstr_destroy(addr->address);
        gw_free(addr);
    }
}


int wap_addr_same(WAPAddr *a, WAPAddr *b) 
{
    /* XXX which ordering gives the best heuristical performance? */
    return a->port == b->port && a->iaddr == b->iaddr;
}


WAPAddrTuple *wap_addr_tuple_create(Octstr *rmt_addr, long rmt_port,
    	    	    	    	    Octstr *lcl_addr, long lcl_port) 
{
    WAPAddrTuple *tuple;
    
    tuple = gw_malloc(sizeof(*tuple));
    tuple->remote = wap_addr_create(rmt_addr, rmt_port);
    tuple->local = wap_addr_create(lcl_addr, lcl_port);
    return tuple;
}


void wap_addr_tuple_destroy(WAPAddrTuple *tuple) 
{
    if (tuple != NULL) {
	wap_addr_destroy(tuple->remote);
	wap_addr_destroy(tuple->local);
	gw_free(tuple);
    }
}


int wap_addr_tuple_same(WAPAddrTuple *a, WAPAddrTuple *b) 
{
    return wap_addr_same(a->remote, b->remote) &&
    	   wap_addr_same(a->local, b->local);
}


WAPAddrTuple *wap_addr_tuple_duplicate(WAPAddrTuple *tuple) 
{
    if (tuple == NULL)
	return NULL;
    
    return wap_addr_tuple_create(tuple->remote->address,
    	    	    	    	 tuple->remote->port,
				 tuple->local->address,
				 tuple->local->port);
}


void wap_addr_tuple_dump(WAPAddrTuple *tuple) 
{
    debug("wap", 0, "WAPAddrTuple %p = <%s:%ld> - <%s:%ld>", 
	  (void *) tuple,
	  octstr_get_cstr(tuple->remote->address),
	  tuple->remote->port,
	  octstr_get_cstr(tuple->local->address),
	  tuple->local->port);
}
