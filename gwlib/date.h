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
 * date.h - interface to utilities for handling date and time values
 *
 * Richard Braakman
 */

#include "gwlib.h"

/* Broken-down time structure without timezone.  The values are
 * longs because that makes them easier to use with octstr_parse_long().
 */
struct universaltime
{
    long day;      /* 1-31 */
    long month;    /* 0-11 */
    long year;     /* 1970- */
    long hour;     /* 0-23 */
    long minute;   /* 0-59 */
    long second;   /* 0-59 */
};


/* Calculate the unix time value (seconds since 1970) given a broken-down
 * date structure in GMT. */
long date_convert_universal(struct universaltime *t);


/*
 * Convert a unix time value to a value of the form
 * Sun, 06 Nov 1994 08:49:37 GMT
 * This is the format required by the HTTP protocol (RFC 2616),
 * and it is defined in RFC 822 as updated by RFC 1123.
 */
Octstr *date_format_http(unsigned long unixtime);


/*
 * Convert a date string as defined by the HTTP protocol (RFC 2616)
 * to a unix time value.  Return -1 if the date string was invalid.
 * Three date formats are acceptable:
 *   Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
 *   Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
 *   Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
 * White space is significant.
 */
long date_parse_http(Octstr *date);

/*
 * attempt to read an ISO-8601 format or similar, making no assumptions on 
 * seperators and number of elements, adding 0 or 1 to missing fields
 * For example, acceptable formats :
 *  2002-05-15 13:23:44
 *  02/05/15:13:23
 * support of 2 digit years is done by assuming years 70 an over are 20th century. this will
 * have to be revised sometime in the next 50 or so years
 */
int date_parse_iso(struct universaltime *ut, Octstr *os);

/*
 * create an ISO-8601 formated time stamp
 */
Octstr* date_create_iso(time_t unixtime);
 
/*
 * Return the current date and time as a unix time value.
 */
long date_universal_now(void);
