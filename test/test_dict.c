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
 * test_dict.c - test Dict objects
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"

#define HUGE_SIZE 200000

int main(void)
{
    Dict *dict;
    Octstr *foo, *bar;
    unsigned long i;
     
    gwlib_init();
    
    foo = octstr_imm("foo");
    bar = octstr_imm("bar");
    
    debug("",0,"Dict simple test.");
    dict = dict_create(10, NULL);
    dict_put(dict, foo, bar);
    info(0, "foo gives %s", octstr_get_cstr(dict_get(dict, foo)));
    if (dict_key_count(dict) == 1)
        info(0, "there is but one foo.");
    else
        error(0, "key count is %ld, should be 1.", dict_key_count(dict));
    dict_destroy(dict);

    debug("",0,"Dict extended/huge test.");
    dict = dict_create(HUGE_SIZE, (void (*)(void *))octstr_destroy);
    for (i = 1; i <= HUGE_SIZE; i++) {
        unsigned long val;
        Octstr *okey, *oval;
        uuid_t id;
        char key[UUID_STR_LEN + 1];
        uuid_generate(id);
        uuid_unparse(id, key);
        val = gw_rand();
        okey = octstr_create(key);
        oval = octstr_format("%ld", val);
        dict_put(dict, okey, oval);
    }
    gwthread_sleep(5); /* give hash table some time */
    if (dict_key_count(dict) == HUGE_SIZE)
        info(0, "ok, got %d entries in the dictionary.", HUGE_SIZE);
    else
        error(0, "key count is %ld, should be %d.", dict_key_count(dict), HUGE_SIZE);
    dict_destroy(dict);

    gwlib_shutdown();
    return 0;
}
