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

/* date.c - utility functions for handling times and dates
 *
 * Richard Braakman
 */

#include <unistd.h>
#include <ctype.h>
#include <string.h>

#include "gwlib.h"

static char const *wkday[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static char const *monthname[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* The starting day of each month, if there's not a leap year.
 * January 1 is day 0, December 31 is day 355. */
static int monthstart[12] = {
    0, 31, 59, 90, 120, 151,
    181, 212, 243, 273, 304, 334
};

/* Value in seconds */
#define MINUTE 60
#define HOUR (60 * MINUTE)
#define DAY (24 * HOUR)

Octstr *date_format_http(unsigned long unixtime)
{
    struct tm tm;
    char buffer[30];

    tm = gw_gmtime((time_t) unixtime);

    /* Make sure gmtime gave us a good date.  We check this to
     * protect the sprintf call below, which might overflow its
     * buffer if the field values are bad. */
    if (tm.tm_wday < 0 || tm.tm_wday > 6 ||
        tm.tm_mday < 0 || tm.tm_mday > 31 ||
        tm.tm_mon < 0 || tm.tm_mon > 11 ||
        tm.tm_year < 0 ||
        tm.tm_hour < 0 || tm.tm_hour > 23 ||
        tm.tm_min < 0 || tm.tm_min > 59 ||
        tm.tm_sec < 0 || tm.tm_sec > 61) {
        warning(0, "Bad date for timestamp %lu, cannot format.",
                unixtime);
        return NULL;
    }

    sprintf(buffer, "%s, %02d %s %04d %02d:%02d:%02d GMT",
            wkday[tm.tm_wday], tm.tm_mday, monthname[tm.tm_mon],
            tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);

    return octstr_create(buffer);
}

long date_convert_universal(struct universaltime *t)
{
    long date;
    int leapyears;
    long year;

    date = (t->year - 1970) * (365 * DAY);

    /* If we haven't had this year's leap day yet, pretend it's
     * the previous year. */
    year = t->year;
    if (t->month <= 1)
        year--;

    /* Add leap years since 1970.  The magic number 477 is the value
     * this formula would give for 1970 itself.  Notice the extra
     * effort we make to keep it correct for the year 2100. */
    leapyears = (year / 4) - (year / 100) + (year / 400) - 477;
    date += leapyears * DAY;

    date += monthstart[t->month] * DAY;
    date += (t->day - 1) * DAY;
    date += t->hour * HOUR;
    date += t->minute * MINUTE;
    date += t->second;

    return date;
}

long date_parse_http(Octstr *date)
{
    long pos;
    struct universaltime t;
    Octstr *monthstr = NULL;

    /* First, skip the leading day-of-week string. */
    pos = octstr_search_char(date, ' ', 0);
    if (pos < 0 || pos == octstr_len(date) - 1)
        return -1;
    pos++;  /* Skip the space */

    /* Distinguish between the three acceptable formats */
    if (isdigit(octstr_get_char(date, pos)) &&
        octstr_get_char(date, pos + 2) == ' ') {
        if (octstr_len(date) - pos < (long)strlen("06 Nov 1994 08:49:37 GMT"))
            goto error;
        if (octstr_parse_long(&t.day, date, pos, 10) != pos + 2)
            goto error;
        monthstr = octstr_copy(date, pos + 3, 3);
        if (octstr_parse_long(&t.year, date, pos + 7, 10) != pos + 11)
            goto error;
        if (octstr_parse_long(&t.hour, date, pos + 12, 10) != pos + 14)
            goto error;
        if (octstr_parse_long(&t.minute, date, pos + 15, 10) != pos + 17)
            goto error;
        if (octstr_parse_long(&t.second, date, pos + 18, 10) != pos + 20)
            goto error;
        /* Take the GMT part on faith. */
    } else if (isdigit(octstr_get_char(date, pos)) &&
               octstr_get_char(date, pos + 2) == '-') {
        if (octstr_len(date) - pos < (long)strlen("06-Nov-94 08:49:37 GMT"))
            goto error;
        if (octstr_parse_long(&t.day, date, pos, 10) != pos + 2)
            goto error;
        monthstr = octstr_copy(date, pos + 3, 3);
        if (octstr_parse_long(&t.year, date, pos + 7, 10) != pos + 9)
            goto error;
        if (t.year > 60)
            t.year += 1900;
        else
            t.year += 2000;
        if (octstr_parse_long(&t.hour, date, pos + 10, 10) != pos + 12)
            goto error;
        if (octstr_parse_long(&t.minute, date, pos + 13, 10) != pos + 15)
            goto error;
        if (octstr_parse_long(&t.second, date, pos + 16, 10) != pos + 18)
            goto error;
        /* Take the GMT part on faith. */
    } else {
        if (octstr_len(date) - pos < (long)strlen(" 6 08:49:37 1994"))
            goto error;
        monthstr = octstr_copy(date, pos, 3);
        if (octstr_parse_long(&t.day, date, pos + 4, 10) != pos + 6)
            goto error;
        if (octstr_parse_long(&t.hour, date, pos + 7, 10) != pos + 9)
            goto error;
        if (octstr_parse_long(&t.minute, date, pos + 10, 10) != pos + 12)
            goto error;
        if (octstr_parse_long(&t.second, date, pos + 13, 10) != pos + 15)
            goto error;
        if (octstr_parse_long(&t.year, date, pos + 16, 10) != pos + 20)
            goto error;
    }

    for (t.month = 0; t.month < 12; t.month++) {
        if (octstr_str_compare(monthstr, monthname[t.month]) == 0)
            break;
    }
    if (t.month == 12)
        goto error;

    octstr_destroy(monthstr);
    return date_convert_universal(&t);

error:
    octstr_destroy(monthstr);
    return -1;
}

int date_parse_iso (struct universaltime *ut, Octstr *os)
{
    int n = 0;
    char *p, *q;

    /* assign defaults */
    ut->month = 0;
    ut->day = 1;
    ut->hour = 0;
    ut->minute = 0;
    ut->second = 0;

    p = octstr_get_cstr(os);
    q = p + ((n = octstr_search_char(os, 'T', 0)) >= 0 ? n : octstr_len(os)); /* stop at the end of string or at the time separator */
    if (sscanf(p, "%4ld%n", &ut->year, &n) < 1)
        return -1;
    p += n;

    if (ut->year < 70)
        ut->year += 2000;
    else if (ut->year < 100)
	ut->year += 1900;
     
    while (p < q && !gw_isdigit(*p))
	p++;     
    if (sscanf(p, "%2ld%n", &ut->month, &n) < 1)
	return 0;
    p += n;
     
     /* 0-based months */
    if (ut->month > 0)
	ut->month--;
     
    while (p < q && !gw_isdigit(*p))
        p++;     
    if (sscanf(p, "%2ld%n", &ut->day, &n) < 1)
	return 0;
    p += n;

    if (*q == 'T') 
	p = q+1;
    else
        return 0;

    while (*p && !gw_isdigit(*p))
	p++;     
    if (sscanf(p, "%2ld%n", &ut->hour, &n) < 1)
	return 0;
    p += n;

    while (*p && !gw_isdigit(*p))
	p++;     
    if (sscanf(p, "%2ld%n", &ut->minute, &n) < 1)
	return 0;
    p += n;
     
    while (*p && !gw_isdigit(*p))
	p++;     
    if (sscanf(p, "%2ld%n", &ut->second, &n) < 1)
	return 0;
     p += n;

    return 0;
}

Octstr* date_create_iso(time_t unixtime) 
{
    struct tm tm;

    tm = gw_gmtime(unixtime);
    
    return octstr_format("%d-%02d-%02dT%02d:%02d:%02dZ", 
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);    
}


/* Note that this implementation makes unportable assumptions about time_t. */
long date_universal_now(void)
{
    return (long) time(NULL);
}
