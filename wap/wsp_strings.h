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

/* wsp-strings.h: interface to tables defined by WSP standard
 *
 * This file defines an interface to the Assigned Numbers tables
 * in appendix A of the WSP specification.  For each supported
 * table there is a function to convert from number to string and
 * a function to convert from string to number.
 *
 * The tables are in wsp-strings.def, in a special format suitable for
 * use with the C preprocessor, which we abuse liberally to get the
 * interface we want. 
 *
 * For a table named foo, these functions will be declared:
 *
 * Octstr *wsp_foo_to_string(long number);
 *   - return NULL if the number has no assigned string.
 * 
 * unsigned char *wsp_foo_to_cstr(long number);
 *   - return NULL if the number has no assigned string.
 *
 * long wsp_string_to_foo(Octstr *ostr);
 *   - case-insensitive lookup.
 *   - Return -1 if the string has no assigned number.
 *
 * Richard Braakman
 */

#ifndef WSP_STRINGS_H
#define WSP_STRINGS_H

#include "gwlib/gwlib.h"
#include "wap/wsp.h"

/* Must be called before any of the other functions in this file.
 * Can be called more than once, in which case multiple shutdowns
 * are also required. */
void wsp_strings_init(void);

/* Call this to clean up memory allocations */
void wsp_strings_shutdown(void);

/* Declare the functions */
#define LINEAR(name, strings) \
Octstr *wsp_##name##_to_string(long number); \
unsigned char *wsp_##name##_to_cstr(long number); \
long wsp_string_to_##name(Octstr *ostr); \
long wsp_string_to_versioned_##name(Octstr *ostr, int version); 
#define STRING(string)
#include "wsp_strings.def"

/* Define the enumerated types */
#define LINEAR(name, strings)
#define STRING(string)
#define NAMED(name, strings) enum name##_enum { strings name##_dummy };
#define NSTRING(string, name) name,
#define VNSTRING(version, string, name) name,
#include "wsp_strings.def"

#endif
