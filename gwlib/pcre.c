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
 * pcre.c - Perl compatible regular expressions (PCREs) 
 *
 * This modules implements wrapper functions to the pcre_foobar() et all
 * functions implemented in the libpcre.a library.
 * PCRE is a library of functions to support regular expressions whose syntax
 * and semantics are as close as possible to those of the Perl 5 language.
 *
 * See http://www.pcre.org/ for more details on PCRE regular expressions.
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <ctype.h>

#include "gwlib/gwlib.h"
#include "pcre.h"

#ifdef HAVE_PCRE


/********************************************************************
 * Generic pcre functions.
 */

pcre *gw_pcre_comp_real(const Octstr *pattern, int cflags, const char *file, 
                        long line, const char *func)
{
    pcre *preg;
    const char *err;
    const char *pat;
    int erroffset;

    pat = pattern ? octstr_get_cstr(pattern) : NULL;
    if ((preg = pcre_compile(pat, cflags, &err, &erroffset, NULL)) == NULL) {
        error(0, "%s:%ld: %s: pcre compilation `%s' failed at offset %d: %s "
                 "(Called from %s:%ld:%s.)",
              __FILE__, (long) __LINE__, __func__, octstr_get_cstr(pattern), 
              erroffset, err, (file), (long) (line), (func));
    }

    return preg;
}


int gw_pcre_exec_real(const pcre *preg, const Octstr *string, int start, 
                      int eflags, int *ovector, int oveccount, 
                      const char *file, long line, const char *func)
{
    int rc;
    char *sub;

    gw_assert(preg != NULL);

    sub = string ? octstr_get_cstr(string) : NULL;
    rc = pcre_exec(preg, NULL, sub,  octstr_len(string), start, eflags,
                   ovector, oveccount);

    if (rc < 0 && rc != PCRE_ERROR_NOMATCH) {
        error(0, "%s:%ld: %s: pcre execution on `%s' failed with error %d "
                 "(Called from %s:%ld:%s.)",
              __FILE__, (long) __LINE__, __func__, octstr_get_cstr(string), rc,
              (file), (long) (line), (func));
    }

    return rc;
}


/********************************************************************
 * Matching wrapper functions.
 *
 * Beware that the regex compilation takes the most significant CPU time,
 * so always try to have pre-compiled regular expressions that keep being
 * reused and re-matched on variable string patterns.
 */

int gw_pcre_match_real(const Octstr *re, const Octstr *os, const char *file, 
                       long line, const char *func)
{
    pcre *regexp;
    int rc;
    int ovector[PCRE_OVECCOUNT];

    /* compile */
    regexp = gw_pcre_comp_real(re, 0, file, line, func);
    if (regexp == NULL)
        return 0;

    /* execute and match */
    rc = gw_pcre_exec_real(regexp, os, 0, 0, ovector, PCRE_OVECCOUNT,
                           file, line, func);

    return (rc > 0) ? 1 : 0;
}


int gw_pcre_match_pre_real(const pcre *preg, const Octstr *os, const char *file, 
                           long line, const char *func)
{
    int rc;
    int ovector[PCRE_OVECCOUNT];

    gw_assert(preg != NULL);

    /* execute and match */
    rc = gw_pcre_exec_real(preg, os, 0, 0, ovector, PCRE_OVECCOUNT,
                           file, line, func);

    return (rc > 0) ? 1 : 0;
}


#endif  /* HAVE_PCRE */

