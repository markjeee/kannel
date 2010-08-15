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
 * timestamp.c - Convert a textual timestamps to seconds since epoch
 *
 * Read textual timestamps, one per line, from the standard input, and 
 * convert them to integers giving the corresponding number of seconds
 * since the beginning of the epoch (beginning of 1970). Both the input
 * and the results should be in UTC.
 *
 * Lars Wirzenius
 */


#include <stdio.h>

#include "gwlib/gwlib.h"


static Octstr *read_line(FILE *f, Octstr *buf)
{
    Octstr *os;
    char cbuf[8*1024];
    size_t n;
    long pos;
    
    pos = octstr_search_char(buf, '\n', 0);
    while (pos == -1 && (n = fread(cbuf, 1, sizeof(cbuf), f)) > 0) {
	octstr_append_data(buf, cbuf, n);
	pos = octstr_search_char(buf, '\n', 0);
    }

    if (pos == -1) {
    	pos = octstr_len(buf);
	if (pos == 0)
	    return NULL;
    }
    os = octstr_copy(buf, 0, pos);
    octstr_delete(buf, 0, pos + 1);

    return os;
}


static int remove_long(long *p, Octstr *os)
{
    long pos;
    
    pos = octstr_parse_long(p, os, 0, 10);
    if (pos == -1)
    	return -1;
    octstr_delete(os, 0, pos);
    return 0;
}


static int remove_prefix(Octstr *os, Octstr *prefix)
{
    if (octstr_ncompare(os, prefix, octstr_len(prefix)) != 0)
    	return -1;
    octstr_delete(os, 0, octstr_len(prefix));
    return 0;
}


static int parse_date(struct universaltime *ut, Octstr *os)
{
    long pos;

    pos = 0;
    
    if (remove_long(&ut->year, os) == -1 ||
        remove_prefix(os, octstr_imm("-")) == -1 ||
	remove_long(&ut->month, os) == -1 ||
        remove_prefix(os, octstr_imm("-")) == -1 ||
	remove_long(&ut->day, os) == -1 ||
        remove_prefix(os, octstr_imm(" ")) == -1 ||
	remove_long(&ut->hour, os) == -1 ||
        remove_prefix(os, octstr_imm(":")) == -1 ||
	remove_long(&ut->minute, os) == -1 ||
        remove_prefix(os, octstr_imm(":")) == -1 ||
	remove_long(&ut->second, os) == -1 ||
        remove_prefix(os, octstr_imm(" ")) == -1)
    	return -1;
    return 0;
}


int main(void)
{
    struct universaltime ut;
    Octstr *os;
    Octstr *buf;
    
    gwlib_init();
    buf = octstr_create("");
    while ((os = read_line(stdin, buf)) != NULL) {
	if (parse_date(&ut, os) == -1)
	    panic(0, "Bad line: %s", octstr_get_cstr(os));
	printf("%ld %s\n", date_convert_universal(&ut), octstr_get_cstr(os));
	octstr_destroy(os);
    }

    log_set_output_level(GW_PANIC);
    gwlib_shutdown();

    return 0;
}
