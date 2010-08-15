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
 * test_prioqueue.c - test priority queue objects
 *
 * Alexander Malysh <olek2002 at hotmail.com>, 2004
 */

#include "gwlib/gwlib.h"
#include "gwlib/gw-prioqueue.h"

static int my_cmp(const void *a, const void *b)
{    
    return octstr_compare((Octstr*) a, (Octstr*) b);
} 

static void my_dump(const void *a, long index)
{    
    debug("", 0, "dump(%p, %ld) called", a, index);    
    debug("", 0, "value=%s", octstr_get_cstr((Octstr*) a));
} 

int main()
{    
    Octstr *os;    
    long i;    
    gw_prioqueue_t *queue;    
    
    gwlib_init();
    
    /* os = octstr_imm("iareanmsgotx"); */    
    os = octstr_imm("123456789");   

    queue = gw_prioqueue_create(my_cmp);    
    
    for (i=0; i < octstr_len(os); i++) {        
        char a[2];       
        a[0] = octstr_get_char(os, i);       
        a[1] = '\0';        
        gw_prioqueue_insert(queue, octstr_create(a));    
    }    
    
    gw_prioqueue_foreach(queue, my_dump);    
    while ((os = gw_prioqueue_remove(queue))) {        
        debug("", 0, "%s", octstr_get_cstr(os));        
        octstr_destroy(os);    
    }   
    
    debug("", 0, "gw_prioqueue_len=%ld", gw_prioqueue_len(queue));    
    
    gwlib_shutdown();    
    return 0;
}

