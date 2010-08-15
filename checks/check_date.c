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
 * check_date.c - checking of date handling functions
 */

#include <string.h>

#include "gwlib/gwlib.h"

/* Format of test_dates file:
 * Valid date strings, one per line.  If a date string is valid
 * but not in the preferred HTTP format, put the preferred version 
 * after it on the same line.  Separate them with a tab.
 */

static void check_reversible(void)
{
    Octstr *dates;
    long pos, endpos, tabpos;
    Octstr *date, *canondate;
    long timeval;

    dates = octstr_read_file("checks/test_dates");
    if (dates == NULL)
        return;

    for (pos = 0; ; pos = endpos + 1) {
        endpos = octstr_search_char(dates, '\n', pos);
        if (endpos < 0)
            break;

        tabpos = octstr_search_char(dates, '\t', pos);

        if (tabpos >= 0 && tabpos < endpos) {
            date = octstr_copy(dates, pos, tabpos - pos);
            canondate = octstr_copy(dates, tabpos + 1, endpos - tabpos - 1);
        } else {
            date = octstr_copy(dates, pos, endpos - pos);
            canondate = octstr_duplicate(date);
        }

        timeval = date_parse_http(date);
        if (timeval == -1)
            warning(0, "Could not parse date \"%s\"", octstr_get_cstr(date));
        else {
            Octstr *newdate;
            newdate = date_format_http((unsigned long) timeval);
            if (octstr_compare(newdate, canondate) != 0) {
                warning(0, "Date not reversible: \"%s\" becomes \"%s\"",
                        octstr_get_cstr(date), octstr_get_cstr(newdate));
            }
            octstr_destroy(newdate);
        }

        octstr_destroy(date);
        octstr_destroy(canondate);
    }

    octstr_destroy(dates);
}


int main(void)
{
    gwlib_init();
    log_set_output_level(GW_INFO);
    check_reversible();
    gwlib_shutdown();
    return 0;
}
