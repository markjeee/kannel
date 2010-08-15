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
 * log.h - logging functions
 *
 * Please note that opening and closing of log files are not thread safe.
 * Don't do it unless you're in single-thread mode.
 */

#ifndef GWLOG_H
#define GWLOG_H

/* Symbolic levels for output levels. */
enum output_level {
	GW_DEBUG, GW_INFO, GW_WARNING, GW_ERROR, GW_PANIC
};

/* defines if a log-file is exclusive or not */
enum excl_state {
    GW_NON_EXCL, GW_EXCL
};

/* Initialize the log file module */
void log_init(void);

/* Shutdown the log file module */
void log_shutdown(void);

/* Print a panicky error message and terminate the program with a failure.
 * So, this function is called when there is no other choice than to exit
 * immediately, with given reason
 */
#define	panic	gw_panic

void gw_panic(int, const char *, ...) PRINTFLIKE(2,3);

/* Print a normal error message. Used when something which should be
 * investigated and possibly fixed, happens. The error might be fatal, too,
 * but we have time to put system down peacefully.
 */
void error(int, const char *, ...) PRINTFLIKE(2,3);

/* Print a warning message. 'Warning' is a message that should be told and
 * distinguished from normal information (info), but does not necessary 
 * require any further investigations. Like 'warning, no sender number set'
 */
void warning(int, const char *, ...) PRINTFLIKE(2,3);

/* Print an informational message. This information should be limited to
 * one or two rows per request, if real debugging information is needed,
 * use debug
 */
void info(int, const char *, ...) PRINTFLIKE(2,3);

/*
 * Print a debug message. Most of the log messages should be of this level 
 * when the system is under development. The first argument gives the `place'
 * where the function is called from; see function set_debug_places.
 */
void debug(const char *, int, const char *, ...) PRINTFLIKE(3,4);


/*
 * Set the places from which debug messages are actually printed. This
 * allows run-time configuration of what is and is not logged when debug
 * is called. `places' is a string of tokens, separated by whitespace and/or
 * commas, with trailing asterisks (`*') matching anything. For instance,
 * if `places' is "wap.wsp.* wap.wtp.* wapbox", then all places that begin 
 * with "wap.wsp." or "wap.wtp." (including the dots) are logged, and so 
 * is the place called "wapbox". Nothing else is logged at debug level, 
 * however. The 'places' string can also have negations, marked with '-' at 
 * the start, so that nothing in that place is outputted. So if the string is
 * "wap.wsp.* -wap.wap.http", only wap.wsp is logged, but not http-parts on 
 * it
 */
void log_set_debug_places(const char *places);

/* Set minimum level for output messages to stderr. Messages with a lower 
   level are not printed to standard error, but may be printed to files
   (see below). */
void log_set_output_level(enum output_level level);

/* Set minimum level for output messages to logfiles */
void log_set_log_level(enum output_level level);

/*
 * Set syslog usage. If `ident' is NULL, syslog is not used.
 */
void log_set_syslog(const char *ident, int syslog_level);

/* Start logging to a file as well. The file will get messages at least of
   level `level'. There is no need and no way to close the log file;
   it will be closed automatically when the program finishes. Failures
   when opening to the log file are printed to stderr. 
   Where `excl' defines if the log file will be exclusive or not.
   Returns the index within the global logfiles[] array where this
   log file entry has been added. */
int log_open(char *filename, int level, enum excl_state excl);

/* Close and re-open all logfiles */
void log_reopen(void);

/*
 * Close all log files.
 */
void log_close_all(void);

/* 
 * Register a thread to a specific logfiles[] index and hence 
 * to a specific exclusive log file.
 */
void log_thread_to(unsigned int idx);

#endif
