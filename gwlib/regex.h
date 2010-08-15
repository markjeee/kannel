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
 * regex.h - POSIX regular expressions (REs) 
 *
 * This modules implements wrapper functions to regcomp(3), regexec(3),
 * et all functions from the POSIX compliance standard. Additinally
 * it provides subexpression substitution routines in order to easily
 * substitute strings arround regular expressions.
 *
 * See regex(3) man page for more details on POSIX regular expressions.
 * 
 * PCRE allows wrapper functions for POSIX regex via an own API. So we
 * use PCRE in favor, before falling back to POSIX regex.
 *
 * Stipe Tolj <stolj@kannel.org>
 */

#ifndef REGEX_H
#define REGEX_H

#ifdef HAVE_PCRE
# include <pcreposix.h>
#elif HAVE_REGEX
# include <regex.h>
#endif

#if defined(HAVE_REGEX) || defined(HAVE_PCRE)


/*
 * We handle a maximum of 10 subexpression matches and 
 * substitution escape codes $0 to $9 in gw_regex_sub().
 */
#define REGEX_MAX_SUB_MATCH 10


/*
 * Destroy a previously compiled regular expression.
 */
void gw_regex_destroy(regex_t *preg);


/*
 * Compile a regular expression provided by pattern and return
 * the regular expression type as function result.
 * If the compilation fails, return NULL.
 */
regex_t *gw_regex_comp_real(const Octstr *pattern, int cflags, const char *file, 
                            long line, const char *func);
#define gw_regex_comp(pattern, cflags) \
    gw_regex_comp_real(pattern, cflags, __FILE__, __LINE__, __func__)


/*
 * Execute a previously compile regular expression on a given
 * string and provide the matches via nmatch and pmatch[].
 */
int gw_regex_exec_real(const regex_t *preg, const Octstr *string, size_t nmatch, 
                       regmatch_t pmatch[], int eflags, const char *file, long line, 
                       const char *func);
#define gw_regex_exec(preg, string, nmatch, pmatch, eflags) \
    gw_regex_exec_real(preg, string, nmatch, pmatch, eflags, \
                       __FILE__, __LINE__, __func__)


/*
 * Provide the error description string of an regex operation as
 * Octstr instead of a char[].
 */
Octstr *gw_regex_error(int errcode, const regex_t *preg);


/* This function substitutes for $0-$9, filling in regular expression
 * submatches. Pass it the same nmatch and pmatch arguments that you
 * passed gw_regexec(). pmatch should not be greater than the maximum number
 * of subexpressions - i.e. one more than the re_nsub member of regex_t.
 *
 * input should be the string with the $-expressions, source should be the
 * string that was matched against.
 *
 * It returns the substituted string, or NULL on error.
 *
 * Parts of this code are based on Henry Spencer's regsub(), from his
 * AT&T V8 regexp package. Function borrowed from apache-1.3/src/main/util.c
 */
char *gw_regex_sub(const char *input, const char *source,
                   size_t nmatch, regmatch_t pmatch[]);


/*
 * Match directly a given regular expression and a source string. This assumes
 * that the RE has not been pre-compiled and hence perform the compile and 
 * exec step in this matching step.
 * Return 1 if the regular expression is successfully matching, 0 otherwise.
 */
int gw_regex_match_real(const Octstr *re, const Octstr *os, const char *file, 
                        long line, const char *func);
#define gw_regex_match(re, os) \
    gw_regex_match_real(re, os, __FILE__, __LINE__, __func__)


/*
 * Match directly a given source string against a previously pre-compiled
 * regular expression.
 * Return 1 if the regular expression is successfully matching, 0 otherwise.
 */
int gw_regex_match_pre_real(const regex_t *preg, const Octstr *os, const char *file, 
                            long line, const char *func);
#define gw_regex_match_pre(preg, os) \
    gw_regex_match_pre_real(preg, os, __FILE__, __LINE__, __func__)


/*
 * Match directly a given regular expression and a source string. RE has not
 * been precompiled. Apply substitution rule accoding to Octstr 'rule' and
 * return the substituted Ocstr as result. Return NULL if failed.
 * Use \$0 up to \$9 as escape codes for subexpression matchings in the rule.
 * Ie. os="+4914287756", re="^(00|\+)([0-9]{6,20})$" rule="\$2" would cause
 * to return "4914287756" because the rule returns only the second regular
 * expression atom that matched via the expression ([0-9]{6,20}).
 */
Octstr *gw_regex_subst_real(const Octstr *re, const Octstr *os, const Octstr *rule, 
                            const char *file, long line, const char *func);
#define gw_regex_subst(re, os, rule) \
    gw_regex_subst_real(re, os, rule, __FILE__, __LINE__, __func__)

/*
 * Math directly a given source string against a previously pre-compiled
 * regular expression. Apply substitution rule according to Ocstr 'rule' and
 * return the substitued Octstr as result. Same as gw_regex_subst() but a 
 * pre-compiled RE is passed as first argument.
 */
Octstr *gw_regex_subst_pre_real(const regex_t *preg, const Octstr *os, const Octstr *rule, 
                                const char *file, long line, const char *func);
#define gw_regex_subst_pre(preg, os, rule) \
    gw_regex_subst_pre_real(preg, os, rule, __FILE__, __LINE__, __func__)


#endif
#endif  /* REGEX_H */


