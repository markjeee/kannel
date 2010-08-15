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
 * protected.h - thread-safe versions of standard library functions
 *
 * The standard (or commonly available) C library functions are not always
 * thread-safe, or re-entrant. This module provides wrappers. The wrappers
 * are not always the most efficient, but the interface is meant to 
 * allow a more efficient version be implemented later.
 *
 * Lars Wirzenius
 */

#ifndef PROTECTED_H
#define PROTECTED_H

#include <netdb.h>
#include <time.h>

void gwlib_protected_init(void);
void gwlib_protected_shutdown(void);
struct tm gw_localtime(time_t t);
struct tm gw_gmtime(time_t t);
time_t gw_mktime(struct tm *tm);
int gw_rand(void);
int gw_gethostbyname(struct hostent *ret, const char *name, char **buff);
size_t gw_strftime(char *s, size_t max, const char *format, const struct tm *tm);

/*
 * Make it harder to use these by mistake.
 */

#undef localtime
#define localtime(t) do_not_use_localtime_directly

#undef gmtime
#define gmtime(t) do_not_use_gmtime_directly

#undef mktime
#define mktime(t) do_not_use_mktime_directly

#undef strftime
#define strftime(a, b, c, d) do_not_use_strftime_directly

#undef rand
#define rand() do_not_use_rand_directly

#undef gethostbyname
#define gethostbyname(a, b, c) do_not_use_gethostbyname_directly

#undef inet_ntoa
#define inet_ntoa(in) use_gw_netaddr_to_octstr_instead_of_inet_ntoa


#endif
