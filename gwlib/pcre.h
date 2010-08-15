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
 * pcre.h - Perl compatible regular expressions (PCREs) 
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

#ifndef PCRE_H
#define PCRE_H

#ifdef HAVE_PCRE

#include <pcre.h>


#define PCRE_OVECCOUNT 30    /* should be a multiple of 3 */

/*
 * Compile a regular expression provided by pattern and return
 * the regular expression type as function result.
 * If the compilation fails, return NULL.
 */
pcre *gw_pcre_comp_real(const Octstr *pattern, int cflags, const char *file, 
                        long line, const char *func);
#define gw_pcre_comp(pattern, cflags) \
    gw_pcre_comp_real(pattern, cflags, __FILE__, __LINE__, __func__)


/*
 * Execute a previously compile regular expression on a given
 * string and provide the matches via nmatch and pmatch[].
 */
int gw_pcre_exec_real(const pcre *preg, const Octstr *string, int start, 
                      int eflags, int *ovector, int oveccount, 
                      const char *file, long line, const char *func);
#define gw_pcre_exec(preg, string, start, eflags, ovector, oveccount) \
    gw_pcre_exec_real(preg, string, start, eflags, ovector, oveccount, \
                      __FILE__, __LINE__, __func__)


/*
 * Match directly a given regular expression and a source string. This assumes
 * that the RE has not been pre-compiled and hence perform the compile and 
 * exec step in this matching step.
 * Return 1 if the regular expression is successfully matching, 0 otherwise.
 */
int gw_pcre_match_real(const Octstr *re, const Octstr *os, const char *file, 
                       long line, const char *func);
#define gw_pcre_match(re, os) \
    gw_pcre_match_real(re, os, __FILE__, __LINE__, __func__)


/*
 * Match directly a given source string against a previously pre-compiled
 * regular expression.
 * Return 1 if the regular expression is successfully matching, 0 otherwise.
 */
int gw_pcre_match_pre_real(const pcre *preg, const Octstr *os, const char *file, 
                           long line, const char *func);
#define gw_pcre_match_pre(preg, os) \
    gw_pcre_match_pre_real(preg, os, __FILE__, __LINE__, __func__)


#endif  /* HAVE_PCRE */
#endif  /* PCRE_H */


