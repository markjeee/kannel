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
 * check_ipcheck.c - check the is_allowed_ip function
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"


int main(void)
{
    Octstr *ip;
    Octstr *allowed;
    Octstr *denied;
    int result;
    int i;
    static struct {
	char *allowed;
	char *denied;
	char *ip;
	int should_be_allowed;
    } tab[] = {
	{ "127.0.0.1", "", "127.0.0.1", 1 },
	{ "127.0.0.1", "", "127.0.0.2", 1 },
	{ "127.0.0.1", "*.*.*.*", "127.0.0.1", 1 },
	{ "127.0.0.1", "*.*.*.*", "1.2.3.4", 0 },
	{ "127.0.0.1", "127.0.0.*", "1.2.3.4", 1 },
	{ "127.0.0.1", "127.0.0.*", "127.0.0.2", 0 },
    };
    
    gwlib_init();
    log_set_output_level(GW_INFO);
        
    for (i = 0; (size_t) i < sizeof(tab) / sizeof(tab[0]); ++i) {
	allowed = octstr_imm(tab[i].allowed);
	denied = octstr_imm(tab[i].denied);
	ip = octstr_imm(tab[i].ip);
	result = is_allowed_ip(allowed, denied, ip);
	if (!!result != !!tab[i].should_be_allowed) {
	    panic(0, "is_allowed_ip did not work for "
	    	     "allowed=<%s> denied=<%s> ip=<%s>, "
		     "returned %d should be %d",
		     octstr_get_cstr(allowed),
		     octstr_get_cstr(denied),
		     octstr_get_cstr(ip),
		     result,
		     tab[i].should_be_allowed);
	}
    }

    gwlib_shutdown();
    return 0;
}
