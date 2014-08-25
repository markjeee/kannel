/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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
 * test_dict.c - test Dict objects
 *
 * Lars Wirzenius
 * Stipe Tolj
 */

#include "gwlib/gwlib.h"

#define HUGE_SIZE 200000

int main(void)
{
    Dict *dict1, *dict2;
    Octstr *key;
    unsigned long i;
    List *keys;
     
    gwlib_init();
    
    debug("",0,"Dict populate phase.");
    dict1 = dict_create(HUGE_SIZE, octstr_destroy_item);
    dict2 = dict_create(HUGE_SIZE, octstr_destroy_item);
    for (i = 1; i <= HUGE_SIZE; i++) {
        Octstr *okey, *oval;
        uuid_t id1, id2;
        char key[UUID_STR_LEN + 1];
        char val[UUID_STR_LEN + 1];
        uuid_generate(id1);
        uuid_generate(id2);
        uuid_unparse(id1, key);
        uuid_unparse(id2, val);
        okey = octstr_create(key);
        oval = octstr_create(val);
        dict_put(dict1, okey, oval);
        dict_put(dict2, oval, okey);
    }

    if (dict_key_count(dict1) == HUGE_SIZE)
        info(0, "ok, got %d entries in dict1.", HUGE_SIZE);
    else
        error(0, "key count is %ld, should be %d in dict1.", dict_key_count(dict1), HUGE_SIZE);
    if (dict_key_count(dict2) == HUGE_SIZE)
        info(0, "ok, got %d entries in dict2.", HUGE_SIZE);
    else
        error(0, "key count is %ld, should be %d in dict2.", dict_key_count(dict2), HUGE_SIZE);

    debug("",0,"Dict lookup phase.");
    keys = dict_keys(dict1);
    while ((key = gwlist_extract_first(keys)) != NULL) {
    	Octstr *oval1, *oval2;
    	if ((oval1 = dict_get(dict1, key)) != NULL) {
    		if ((oval2 = dict_get(dict2, oval1)) != NULL) {
    			if (octstr_compare(oval2, key) != 0) {
            		error(0, "Dict cross-key check inconsistent:");
            		error(0, "dict1: key <%s>, value <%s>", octstr_get_cstr(key), octstr_get_cstr(oval1));
            		error(0, "dict2: key <%s>, value <%s>", octstr_get_cstr(oval1), octstr_get_cstr(oval2));
    			}
    		} else {
        		error(0, "dict2 key %s has NULL value.", octstr_get_cstr(key));
    		}
    	} else {
    		error(0, "dict1 key %s has NULL value.", octstr_get_cstr(key));
    	}
    	octstr_destroy(key);
    }
    gwlist_destroy(keys, NULL);

    dict_destroy(dict1);
    dict_destroy(dict2);

    gwlib_shutdown();
    return 0;
}
