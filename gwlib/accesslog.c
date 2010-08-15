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
 * accesslog.c - implement access logging functions
 *
 * see accesslog.h.
 *
 * Kalle Marjola 2000 for Project Kannel
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#include "gwlib.h"

static FILE *file = NULL;
static char filename[FILENAME_MAX + 1]; /* to allow re-open */
static int use_localtime;
static int markers = 1;     /* can be turned-off by 'access-log-clean = yes' */

/*
 * Reopen/rotate lock.
 */
static List *writers = NULL;

void alog_reopen(void)
{
    if (file == NULL)
	return;

    if (markers)
        alog("Log ends");

    gwlist_lock(writers);
    /* wait for writers to complete */
    gwlist_consume(writers);

    fclose(file);
    file = fopen(filename, "a");

    gwlist_unlock(writers);

    if (file == NULL) {
        error(errno, "Couldn't re-open access logfile `%s'.", filename);
    } 
    else if (markers) {
        alog("Log begins");
    }
}


void alog_close(void)
{

    if (file != NULL) {
        if (markers)
            alog("Log ends");
        gwlist_lock(writers);
        /* wait for writers to complete */
        gwlist_consume(writers);
        fclose(file);
        file = NULL;
        gwlist_unlock(writers);
        gwlist_destroy(writers, NULL);
        writers = NULL;
    }
}


void alog_open(char *fname, int use_localtm, int use_markers)
{
    FILE *f;
    
    use_localtime = use_localtm;
    markers = use_markers;

    if (file != NULL) {
        warning(0, "Opening an already opened access log");
        alog_close();
    }
    if (strlen(fname) > FILENAME_MAX) {
        error(0, "Access Log filename too long: `%s', cannot open.", fname);
        return;
    }

    if (writers == NULL)
        writers = gwlist_create();

    f = fopen(fname, "a");
    if (f == NULL) {
        error(errno, "Couldn't open logfile `%s'.", fname);
        return;
    }
    file = f;
    strcpy(filename, fname);
    info(0, "Started access logfile `%s'.", filename);
    if (markers)
        alog("Log begins");
}


void alog_use_localtime(void)
{
    use_localtime = 1;
}


void alog_use_gmtime(void)
{
    use_localtime = 0;
}


#define FORMAT_SIZE (10*1024)
static void format(char *buf, const char *fmt)
{
    time_t t;
    struct tm tm;
    char *p, prefix[1024];
	
    p = prefix;

    if (markers) {
        time(&t);
        if (use_localtime)
            tm = gw_localtime(t);
        else
            tm = gw_gmtime(t);

        sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        *p = '\0';
    }

    if (strlen(prefix) + strlen(fmt) > FORMAT_SIZE / 2) {
        sprintf(buf, "%s <OUTPUT message too long>\n", prefix);
        return;
    }
    sprintf(buf, "%s%s\n", prefix, fmt);
}


/* XXX should we also log automatically into main log, too? */

void alog(const char *fmt, ...)
{
    char buf[FORMAT_SIZE + 1];
    va_list args;

    if (file == NULL)
        return;

    format(buf, fmt);
    va_start(args, fmt);

    gwlist_lock(writers);
    gwlist_add_producer(writers);
    gwlist_unlock(writers);

    vfprintf(file, buf, args);
    fflush(file);

    gwlist_remove_producer(writers);

    va_end(args);
}

