/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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
 * regex.c - POSIX regular expressions (REs) 
 *
 * This modules implements wrapper functions to regcomp(3), regexec(3),
 * et all functions from the POSIX compliance standard. Additinally
 * it provides subexpression substitution routines in order to easily
 * substitute strings arround regular expressions.
 *
 * See regex(3) man page for more details on POSIX regular expressions.
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <ctype.h>

#include "gwlib/gwlib.h"
#include "regex.h"

/* 
 * We allow to substitute the POSIX compliant regex routines via PCRE 
 * provided routines if no system own regex implementation is available.
 */
#if defined(HAVE_REGEX) || defined(HAVE_PCRE)


/********************************************************************
 * Generic regular expression functions.
 */

void gw_regex_destroy(regex_t *preg)
{
    if (preg == NULL)
        return;
        
    regfree(preg);
    gw_free(preg);
}


regex_t *gw_regex_comp_real(const Octstr *pattern, int cflags, const char *file, 
                            long line, const char *func)
{
    int rc;
    regex_t *preg;
    
    preg = gw_malloc(sizeof(regex_t));

    if ((rc = regcomp(preg, pattern ? octstr_get_cstr(pattern) : NULL, cflags)) != 0) {
        char buffer[512];
        regerror(rc, preg, buffer, sizeof(buffer)); 
        error(0, "%s:%ld: %s: regex compilation `%s' failed: %s (Called from %s:%ld:%s.)",
              __FILE__, (long) __LINE__, __func__, octstr_get_cstr(pattern), buffer, 
              (file), (long) (line), (func));
        gw_free(preg);
        return NULL;
    }

    return preg;
}


int gw_regex_exec_real(const regex_t *preg, const Octstr *string, size_t nmatch, 
                       regmatch_t pmatch[], int eflags, const char *file, long line, 
                       const char *func)
{
    int rc;

    gw_assert(preg != NULL);

    rc = regexec(preg, string ? octstr_get_cstr(string) : NULL,  nmatch, pmatch, eflags);
    if (rc != REG_NOMATCH && rc != 0) {
        char buffer[512];
        regerror(rc, preg, buffer, sizeof(buffer)); 
        error(0, "%s:%ld: %s: regex execution on `%s' failed: %s (Called from %s:%ld:%s.)",
              __FILE__, (long) __LINE__, __func__, octstr_get_cstr(string), buffer,
              (file), (long) (line), (func));
    }

    return rc;
}


Octstr *gw_regex_error(int errcode, const regex_t *preg)
{
    char errbuf[512];
    Octstr *os;

    regerror(errcode, preg, errbuf, sizeof(errbuf));
    os = octstr_create(errbuf);

    return os;
}


/* Duplicate a string. */
static char *pstrdup(const char *s)
{
    char *res;
    size_t len;

    if (s == NULL)
        return NULL;
    len = strlen(s) + 1;
    res = gw_malloc(len);
    memcpy(res, s, len);
    return res;
}


/* This function substitutes for $0-$9, filling in regular expression
 * submatches. Pass it the same nmatch and pmatch arguments that you
 * passed gw_regexec(). pmatch should not be greater than the maximum number
 * of subexpressions - i.e. one more than the re_nsub member of regex_t.
 *
 * input should be the string with the $-expressions, source should be the
 * string that was matched against.
 *
 * It returns the substituted string, or NULL on error.
 * BEWARE: Caller must free allocated memory of the result.
 *
 * Parts of this code are based on Henry Spencer's regsub(), from his
 * AT&T V8 regexp package. Function borrowed from apache-1.3/src/main/util.c
 */
char *gw_regex_sub(const char *input, const char *source,
                   size_t nmatch, regmatch_t pmatch[])
{
    const char *src = input;
    char *dest, *dst;
    char c;
    size_t no;
    int len;

    if (!source)
        return NULL;
    if (!nmatch)
        return pstrdup(src);

    /* First pass, find the size */
    len = 0;
    while ((c = *src++) != '\0') {
        if (c == '&')
            no = 0;
        else if (c == '$' && isdigit(*src))
            no = *src++ - '0';
        else
            no = 10;

        if (no > 9) {           /* Ordinary character. */
            if (c == '\\' && (*src == '$' || *src == '&'))
                c = *src++;
            len++;
        }
        else if (no < nmatch && pmatch[no].rm_so < pmatch[no].rm_eo) {
            len += pmatch[no].rm_eo - pmatch[no].rm_so;
        }
    }

    dest = dst = gw_malloc(len + 1);

    /* Now actually fill in the string */
    src = input;
    while ((c = *src++) != '\0') {
        if (c == '&')
            no = 0;
        else if (c == '$' && isdigit(*src))
            no = *src++ - '0';
        else
            no = 10;

        if (no > 9) {           /* Ordinary character. */
            if (c == '\\' && (*src == '$' || *src == '&'))
                c = *src++;
            *dst++ = c;
        }
        else if (no < nmatch && pmatch[no].rm_so < pmatch[no].rm_eo) {
            len = pmatch[no].rm_eo - pmatch[no].rm_so;
            memcpy(dst, source + pmatch[no].rm_so, len);
            dst += len;
        }
    }
    *dst = '\0';

    return dest;
}


/********************************************************************
 * Matching and substitution wrapper functions.
 *
 * Beware that the regex compilation takes the most significant CPU time,
 * so always try to have pre-compiled regular expressions that keep being
 * reused and re-matched on variable string patterns.
 */

int gw_regex_match_real(const Octstr *re, const Octstr *os, const char *file, 
                        long line, const char *func)
{
    regex_t *regexp;
    int rc;

    /* compile */
    regexp = gw_regex_comp_real(re, REG_EXTENDED|REG_ICASE, file, line, func);
    if (regexp == NULL)
        return 0;

    /* execute and match */
    rc = gw_regex_exec_real(regexp, os, 0, NULL, 0, file, line, func);

    gw_regex_destroy(regexp);

    return (rc == 0) ? 1 : 0;
}


int gw_regex_match_pre_real(const regex_t *preg, const Octstr *os, const char *file, 
                            long line, const char *func)
{
    int rc;

    gw_assert(preg != NULL);

    /* execute and match */
    rc = gw_regex_exec_real(preg, os, 0, NULL, 0, file, line, func);

    return (rc == 0) ? 1 : 0;
}


Octstr *gw_regex_subst_real(const Octstr *re, const Octstr *os, const Octstr *rule, 
                            const char *file, long line, const char *func)
{
    Octstr *result;
    regex_t *regexp;
    regmatch_t pmatch[REGEX_MAX_SUB_MATCH];
    int rc;
    char *rsub;

    /* compile */
    regexp = gw_regex_comp_real(re, REG_EXTENDED|REG_ICASE, file, line, func);
    if (regexp == NULL)
        return 0;

    /* execute and match */
    rc = gw_regex_exec_real(regexp, os, REGEX_MAX_SUB_MATCH, &pmatch[0], 0, 
                            file, line, func);
    gw_regex_destroy(regexp);

    /* substitute via rule if matched */
    if (rc != 0)
        return NULL;

    rsub = gw_regex_sub(octstr_get_cstr(rule), octstr_get_cstr(os),
                        REGEX_MAX_SUB_MATCH, &pmatch[0]);
    if (rsub == NULL)
        return NULL;

    result = octstr_create(rsub);
    gw_free(rsub);
    
    return result;
}


Octstr *gw_regex_subst_pre_real(const regex_t *preg, const Octstr *os, const Octstr *rule, 
                                const char *file, long line, const char *func)
{
    Octstr *result;
    regmatch_t pmatch[REGEX_MAX_SUB_MATCH];
    int rc;
    char *rsub;

    gw_assert(preg != NULL);

    /* execute and match */
    rc = gw_regex_exec_real(preg, os, REGEX_MAX_SUB_MATCH, &pmatch[0], 0, 
                            file, line, func);

    /* substitute via rule if matched */
    if (rc != 0)
        return NULL;

    rsub = gw_regex_sub(octstr_get_cstr(rule), octstr_get_cstr(os),
                        REGEX_MAX_SUB_MATCH, &pmatch[0]);
    if (rsub == NULL)
        return NULL;

    result = octstr_create(rsub);
    gw_free(rsub);
    
    return result;
}

#endif  /* HAVE_REGEX || HAVE_PCRE */

