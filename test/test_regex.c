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
 * test_regex.c - test regex module
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gwlib/regex.h"

#if defined(HAVE_REGEX) || defined(HAVE_PCRE)

int main(int argc, char **argv)
{
    Octstr *re, *os, *sub;
    Octstr *tmp;
    regex_t *regexp;
    regmatch_t pmatch[REGEX_MAX_SUB_MATCH];
    int rc;

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    if (argc < 4)
        panic(0, "Syntax: %s <os> <re> <sub>\n", argv[0]);

    os = octstr_create(argv[1]);
    re = octstr_create(argv[2]);
    sub = octstr_create(argv[3]);

    info(0, "step 1: generic functions");

    /* compile */
    if ((regexp = gw_regex_comp(re, REG_EXTENDED)) == NULL)
        panic(0, "regex compilation failed!");

    debug("regex",0,"RE: regex <%s> has %ld subexpressions.",
          octstr_get_cstr(re), (long)regexp->re_nsub);

    /* execute */
    rc = gw_regex_exec(regexp, os, REGEX_MAX_SUB_MATCH, &pmatch[0], 0);
    if (rc == REG_NOMATCH) {
        info(0, "RE: regex <%s> did not match on string <%s>.",
             octstr_get_cstr(re), octstr_get_cstr(os));
    } else if (rc != 0) {
        Octstr *err = gw_regex_error(rc, regexp);
        error(0, "RE: regex <%s> execution failed: %s",
              octstr_get_cstr(re), octstr_get_cstr(err));
        octstr_destroy(err);
    } else {
        int i;
        char *rsub;
        debug("regex",0,"RE: regex <%s> matches.", octstr_get_cstr(re));
        debug("regex",0,"RE: substring matches are:");
        for (i = 0; i <= REGEX_MAX_SUB_MATCH; i++) {
            if (pmatch[i].rm_so != -1 && pmatch[i].rm_eo != -1) {
                Octstr *s = octstr_copy(os, pmatch[i].rm_so, pmatch[i].rm_eo - pmatch[i].rm_so);
                debug("regex",0,"RE:  %d: <%s>", i, octstr_get_cstr(s));
                octstr_destroy(s);
            }
        }
        rsub = gw_regex_sub(octstr_get_cstr(sub), octstr_get_cstr(os),
                            REGEX_MAX_SUB_MATCH, &pmatch[0]);
        debug("regex",0,"RE: substituted string is <%s>.", rsub);
        gw_free(rsub);
    }
    
    info(0, "step 2: wrapper functions");

    debug("regex",0,"RE: regex_match <%s> on <%s> did: %s",
          octstr_get_cstr(re), octstr_get_cstr(os),
          gw_regex_match(re, os) ? "match" : "NOT match");

    debug("regex",0,"RE: regex_match_pre on <%s> did: %s",
          octstr_get_cstr(os),
          gw_regex_match_pre(regexp, os) ? "match" : "NOT match");

    tmp = gw_regex_subst(re, os, sub);
    debug("regex",0,"RE: regex_subst <%s> on <%s> rule <%s>: %s",
          octstr_get_cstr(re), octstr_get_cstr(os), octstr_get_cstr(sub),
          octstr_get_cstr(tmp));
    octstr_destroy(tmp);

    tmp = gw_regex_subst_pre(regexp, os, sub);
    debug("regex",0,"RE: regex_subst_pre on <%s> rule <%s>: %s",
          octstr_get_cstr(os), octstr_get_cstr(sub), octstr_get_cstr(tmp));

    gw_regex_destroy(regexp);
    octstr_destroy(tmp);
    octstr_destroy(re);
    octstr_destroy(os);
    gwlib_shutdown();
    return 0;
}

#endif
