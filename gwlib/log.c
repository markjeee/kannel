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
 * log.c - implement logging functions
 */

#include "gwlib.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#if HAVE_SYSLOG_H
#include <syslog.h>
#else

/*
 * If we don't have syslog.h, then we'll use the following dummy definitions
 * to avoid writing #if HAVE_SYSLOG_H everywhere.
 */

enum {
    LOG_PID, LOG_DAEMON, LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR, LOG_ALERT
};

static void openlog(const char *ident, int option, int facility)
{
}

static void syslog(int translog, const char *buf)
{
}

static void closelog(void)
{
}
#endif


/*
 * List of currently open log files.
 */
#define MAX_LOGFILES 128
static struct {
    FILE *file;
    int minimum_output_level;
    char filename[FILENAME_MAX + 1]; /* to allow re-open */
    enum excl_state exclusive;
} logfiles[MAX_LOGFILES];
static int num_logfiles = 0;


/*
 * Mapping array between thread id and logfiles[] index.
 * This is used for smsc specific logging.
 */
#define THREADTABLE_SIZE 1024
static unsigned int thread_to[(long)THREADTABLE_SIZE];


/*
 * Ensure we use the real threadtable slot number to map the thread id
 * instead of the thread id reported by gwthread_self()
 */
#define thread_slot() \
    (gwthread_self() % THREADTABLE_SIZE)


/*
 * List of places that should be logged at debug-level.
 */
#define MAX_LOGGABLE_PLACES (10*1000)
static char *loggable_places[MAX_LOGGABLE_PLACES];
static int num_places = 0;


/*
 * Reopen/rotate locking things.
 */
static RWLock rwlock;

/*
 * Syslog support.
 */
static int sysloglevel;
static int dosyslog = 0;


/*
 * Make sure stderr is included in the list.
 */
static void add_stderr(void)
{
    int i;

    for (i = 0; i < num_logfiles; ++i)
	if (logfiles[i].file == stderr)
	    return;
    logfiles[num_logfiles].file = stderr;
    logfiles[num_logfiles].minimum_output_level = GW_DEBUG;
    logfiles[num_logfiles].exclusive = GW_NON_EXCL;
    ++num_logfiles;
}


void log_init(void)
{
    unsigned long i;

    /* Initialize rwlock */
    gw_rwlock_init_static(&rwlock);

    /* default all possible thread to logging index 0, stderr */
    for (i = 0; i < THREADTABLE_SIZE; i++) {
        thread_to[i] = 0;
    }

    add_stderr();
}

void log_shutdown(void)
{
    log_close_all();
    /* destroy rwlock */
    gw_rwlock_destroy(&rwlock);
}


void log_set_output_level(enum output_level level)
{
    int i;

    for (i = 0; i < num_logfiles; ++i) {
	if (logfiles[i].file == stderr) {
	    logfiles[i].minimum_output_level = level;
	    break;
	}
    }
}

void log_set_log_level(enum output_level level)
{
    int i;

    /* change everything but stderr */
    for (i = 0; i < num_logfiles; ++i) {
        if (logfiles[i].file != stderr) {
            logfiles[i].minimum_output_level = level;
            info(0, "Changed logfile `%s' to level `%d'.", logfiles[i].filename, level);
        }
    }
}


void log_set_syslog(const char *ident, int syslog_level)
{
    if (ident == NULL)
	dosyslog = 0;
    else {
	dosyslog = 1;
	sysloglevel = syslog_level;
	openlog(ident, LOG_PID, LOG_DAEMON);
	debug("gwlib.log", 0, "Syslog logging enabled.");
    }
}


void log_reopen(void)
{
    int i, j, found;

    /*
     * Writer lock.
     */
    gw_rwlock_wrlock(&rwlock);

    for (i = 0; i < num_logfiles; ++i) {
        if (logfiles[i].file != stderr) {
            found = 0;

            /*
             * Reverse seek for allready reopened logfile.
             * If we find a previous file descriptor for the same file
             * name, then don't reopen that duplicate, but assign the
             * file pointer to it.
             */
            for (j = i-1; j >= 0 && found == 0; j--) {
                if (strcmp(logfiles[i].filename, logfiles[j].filename) == 0) {
                    logfiles[i].file = logfiles[j].file;
                    found = 1;
                }
            }
            if (found)
                continue;
            if (logfiles[i].file != NULL)
                fclose(logfiles[i].file);
            logfiles[i].file = fopen(logfiles[i].filename, "a");
            if (logfiles[i].file == NULL) {
                error(errno, "Couldn't re-open logfile `%s'.",
                      logfiles[i].filename);
            }
        }
    }

    /*
     * Unlock writer.
     */
    gw_rwlock_unlock(&rwlock);
}


void log_close_all(void)
{
    /*
     * Writer lock.
     */
    gw_rwlock_wrlock(&rwlock);

    while (num_logfiles > 0) {
        --num_logfiles;
        if (logfiles[num_logfiles].file != stderr && logfiles[num_logfiles].file != NULL) {
            int i;
            /* look for the same filename and set file to NULL */
            for (i = num_logfiles - 1; i >= 0; i--) {
                if (strcmp(logfiles[num_logfiles].filename, logfiles[i].filename) == 0)
                    logfiles[i].file = NULL;
            }
            fclose(logfiles[num_logfiles].file);
            logfiles[num_logfiles].file = NULL;
        }
    }

    /*
     * Unlock writer.
     */
    gw_rwlock_unlock(&rwlock);

    /* close syslog if used */
    if (dosyslog) {
        closelog();
        dosyslog = 0;
    }
}


int log_open(char *filename, int level, enum excl_state excl)
{
    FILE *f = NULL;
    int i;
    
    gw_rwlock_wrlock(&rwlock);

    if (num_logfiles == MAX_LOGFILES) {
        gw_rwlock_unlock(&rwlock);
        error(0, "Too many log files already open, not adding `%s'",
              filename);
        return -1;
    }

    if (strlen(filename) > FILENAME_MAX) {
        gw_rwlock_unlock(&rwlock);
        error(0, "Log filename too long: `%s'.", filename);
        return -1;
    }

    /*
     * Check if the file is already opened for logging.
     * If there is an open file, then assign the file descriptor
     * that is already existing for this log file.
     */
    for (i = 0; i < num_logfiles && f == NULL; ++i) {
        if (strcmp(logfiles[i].filename, filename) == 0)
            f = logfiles[i].file;
    }

    /* if not previously opened, then open it now */
    if (f == NULL) {
        f = fopen(filename, "a");
        if (f == NULL) {
            gw_rwlock_unlock(&rwlock);
            error(errno, "Couldn't open logfile `%s'.", filename);
            return -1;
        }
    }
    
    logfiles[num_logfiles].file = f;
    logfiles[num_logfiles].minimum_output_level = level;
    logfiles[num_logfiles].exclusive = excl;
    strcpy(logfiles[num_logfiles].filename, filename);
    ++num_logfiles;
    i = num_logfiles - 1;
    gw_rwlock_unlock(&rwlock);

    info(0, "Added logfile `%s' with level `%d'.", filename, level);

    return i;
}


#define FORMAT_SIZE (1024)
static void format(char *buf, int level, const char *place, int e,
		   const char *fmt, int with_timestamp)
{
    static char *tab[] = {
	"DEBUG: ",
	"INFO: ",
	"WARNING: ",
	"ERROR: ",
	"PANIC: ",
	"LOG: "
    };
    static int tab_size = sizeof(tab) / sizeof(tab[0]);
    time_t t;
    struct tm tm;
    char *p, prefix[1024];
    long tid, pid;
    
    p = prefix;

    if (with_timestamp) {
        time(&t);
#if LOG_TIMESTAMP_LOCALTIME
        tm = gw_localtime(t);
#else
        tm = gw_gmtime(t);
#endif
        sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    
        p = strchr(p, '\0');
    }

    gwthread_self_ids(&tid, &pid);
    sprintf(p, "[%ld] [%ld] ", pid, tid);
    
    p = strchr(p, '\0');
    if (level < 0 || level >= tab_size)
	sprintf(p, "UNKNOWN: ");
    else
	sprintf(p, "%s", tab[level]);

    p = strchr(p, '\0');
    if (place != NULL && *place != '\0')
	sprintf(p, "%s: ", place);
    
    if (strlen(prefix) + strlen(fmt) > FORMAT_SIZE / 2) {
	sprintf(buf, "%s <OUTPUT message too long>\n", prefix);
	return;
    }
    
    if (e == 0)
	sprintf(buf, "%s%s\n", prefix, fmt);
    else
	sprintf(buf, "%s%s\n%sSystem error %d: %s\n",
		prefix, fmt, prefix, e, strerror(e));
}


static void PRINTFLIKE(2,0) output(FILE *f, char *buf, va_list args) 
{
    vfprintf(f, buf, args);
    fflush(f);
}


static void PRINTFLIKE(1,0) kannel_syslog(char *format, va_list args, int level)
{
    char buf[4096]; /* Trying to syslog more than 4K could be bad */
    int translog;
    
    if (level >= sysloglevel && dosyslog) {
	if (args == NULL) {
	    strncpy(buf, format, sizeof(buf));
	    buf[sizeof(buf) - 1] = '\0';
	} else {
	    vsnprintf(buf, sizeof(buf), format, args);
	    /* XXX vsnprint not 100% portable */
	}

	switch(level) {
	case GW_DEBUG:
	    translog = LOG_DEBUG;
	    break;
	case GW_INFO:
	    translog = LOG_INFO;
	    break;
	case GW_WARNING:
	    translog = LOG_WARNING;
	    break;
	case GW_ERROR:
	    translog = LOG_ERR;
	    break;
	case GW_PANIC:
	    translog = LOG_ALERT;
	    break;
	default:
	    translog = LOG_INFO;
	    break;
	}
	syslog(translog, "%s", buf);
    }
}


/*
 * Almost all of the message printing functions are identical, except for
 * the output level they use. This macro contains the identical parts of
 * the functions so that the code needs to exist only once. It's a bit
 * more awkward to edit, but that can't be helped. The "do {} while (0)"
 * construct is a gimmick to be more like a function call in all syntactic
 * situation.
 */

#define FUNCTION_GUTS(level, place) \
	do { \
	    int i; \
	    char buf[FORMAT_SIZE]; \
	    va_list args; \
	    \
	    format(buf, level, place, err, fmt, 1); \
            gw_rwlock_rdlock(&rwlock); \
	    for (i = 0; i < num_logfiles; ++i) { \
		if (logfiles[i].exclusive == GW_NON_EXCL && \
                    level >= logfiles[i].minimum_output_level && \
                    logfiles[i].file != NULL) { \
		        va_start(args, fmt); \
		        output(logfiles[i].file, buf, args); \
		        va_end(args); \
		} \
	    } \
            gw_rwlock_unlock(&rwlock); \
	    if (dosyslog) { \
	        format(buf, level, place, err, fmt, 0); \
		va_start(args, fmt); \
		kannel_syslog(buf,args,level); \
		va_end(args); \
	    } \
	} while (0)

#define FUNCTION_GUTS_EXCL(level, place) \
	do { \
	    char buf[FORMAT_SIZE]; \
	    va_list args; \
	    \
	    format(buf, level, place, err, fmt, 1); \
            gw_rwlock_rdlock(&rwlock); \
            if (logfiles[e].exclusive == GW_EXCL && \
                level >= logfiles[e].minimum_output_level && \
                logfiles[e].file != NULL) { \
                va_start(args, fmt); \
                output(logfiles[e].file, buf, args); \
                va_end(args); \
            } \
            gw_rwlock_unlock(&rwlock); \
	} while (0)


#ifdef HAVE_BACKTRACE
static void PRINTFLIKE(2,3) gw_panic_output(int err, const char *fmt, ...)
{
    FUNCTION_GUTS(GW_PANIC, "");
}
#endif

void gw_panic(int err, const char *fmt, ...)
{
    /*
     * we don't want PANICs to spread accross smsc logs, so
     * this will be always within the main core log.
     */
    FUNCTION_GUTS(GW_PANIC, "");

#ifdef HAVE_BACKTRACE
    {
        void *stack_frames[50];
        size_t size, i;
        char **strings;

        size = backtrace(stack_frames, sizeof(stack_frames) / sizeof(void*));
        strings = backtrace_symbols(stack_frames, size);

        if (strings) {
            for (i = 0; i < size; i++)
                gw_panic_output(0, "%s", strings[i]);
        }
        else { /* hmm, no memory available */
            for (i = 0; i < size; i++)
                gw_panic_output(0, "%p", stack_frames[i]);
        }

        /*
         * Note: we don't free 'strings' array because gw_free could panic's and we
         *       have endless loop with SEGFAULT at the end. And this doesn't care
         *       us in any case, because we are panic's and exiting immediately. (alex)
         */
    }
#endif

#ifdef SEGFAULT_PANIC
    *((char*)0) = 0;
#endif

    exit(EXIT_FAILURE);
}


void error(int err, const char *fmt, ...) 
{
    int e;
    
    if ((e = thread_to[thread_slot()])) {
        FUNCTION_GUTS_EXCL(GW_ERROR, "");
    } else {
        FUNCTION_GUTS(GW_ERROR, "");
    }
}


void warning(int err, const char *fmt, ...) 
{
    int e;
    
    if ((e = thread_to[thread_slot()])) {
        FUNCTION_GUTS_EXCL(GW_WARNING, "");
    } else {
        FUNCTION_GUTS(GW_WARNING, "");
    }
}


void info(int err, const char *fmt, ...) 
{
    int e;
    
    if ((e = thread_to[thread_slot()])) {
        FUNCTION_GUTS_EXCL(GW_INFO, "");
    } else {
        FUNCTION_GUTS(GW_INFO, "");
    }
}


static int place_matches(const char *place, const char *pat) 
{
    size_t len;
    
    len = strlen(pat);
    if (pat[len-1] == '*')
	return (strncasecmp(place, pat, len - 1) == 0);
    return (strcasecmp(place, pat) == 0);
}


static int place_should_be_logged(const char *place) 
{
    int i;
    
    if (num_places == 0)
	return 1;
    for (i = 0; i < num_places; ++i) {
	if (*loggable_places[i] != '-' && 
	    place_matches(place, loggable_places[i]))
		return 1;
    }
    return 0;
}


static int place_is_not_logged(const char *place) 
{
    int i;
    
    if (num_places == 0)
	return 0;
    for (i = 0; i < num_places; ++i) {
	if (*loggable_places[i] == '-' &&
	    place_matches(place, loggable_places[i]+1))
		return 1;
    }
    return 0;
}


void debug(const char *place, int err, const char *fmt, ...) 
{
    int e;
    
    if (place_should_be_logged(place) && place_is_not_logged(place) == 0) {
	/*
	 * Note: giving `place' to FUNCTION_GUTS makes log lines
    	 * too long and hard to follow. We'll rely on an external
    	 * list of what places are used instead of reading them
    	 * from the log file.
	 */
        if ((e = thread_to[thread_slot()])) {
            FUNCTION_GUTS_EXCL(GW_DEBUG, "");
        } else {
            FUNCTION_GUTS(GW_DEBUG, "");
        }
    }
}


void log_set_debug_places(const char *places) 
{
    char *p;
    
    p = strtok(gw_strdup(places), " ,");
    num_places = 0;
    while (p != NULL && num_places < MAX_LOGGABLE_PLACES) {
	loggable_places[num_places++] = p;
	p = strtok(NULL, " ,");
    }
}


void log_thread_to(unsigned int idx)
{
    long thread_id = thread_slot();

    if (idx > 0) 
        info(0, "Logging thread `%ld' to logfile `%s' with level `%d'.", 
             thread_id, logfiles[idx].filename, logfiles[idx].minimum_output_level);
    thread_to[thread_id] = idx;
}

