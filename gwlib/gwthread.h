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
 * gwthread.h - threads wrapper with interruptible sleep and poll operations.
 *
 * This is a (partial) encapsulation of threads.  It provides functions
 * to create new threads and to manipulate threads.  It will eventually
 * be extended to encapsulate all pthread functions we use, so that
 * non-POSIX platforms can plug in their own versions.
 *
 * Richard Braakman
 */

#ifndef GWTHREAD_H
#define GWTHREAD_H

#include "gw-config.h"
#include <sys/poll.h>

/* gwthread_self() must return this value for the main thread. */
#define MAIN_THREAD_ID 0

typedef void gwthread_func_t(void *arg);

/* Called by the gwlib init code */
void gwthread_init(void);
void gwthread_shutdown(void);

/* Start a new thread, running func(arg).  Return the new thread ID
 * on success, or -1 on failure.  Thread IDs are unique during the lifetime
 * of the entire process, unless you use more than LONG_MAX threads. */
long gwthread_create_real(gwthread_func_t *func, const char *funcname,
			  void *arg);
#define gwthread_create(func, arg) \
	(gwthread_create_real(func, __FILE__ ":" #func, arg))

/* Wait for the other thread to terminate.  Return immediately if it
 * has already terminated. */
void gwthread_join(long thread);

/* Wait for all threads whose main function is `func' to terminate.
 * Return immediately if none are running. */
void gwthread_join_every(gwthread_func_t *func);

/* Wait for all threads to terminate.  Return immediately if none
 * are running.  This function is not intended to be called if new
 * threads are still being created, and it may not notice such threads. */
void gwthread_join_all(void);

/* Return the thread id of this thread.  Note that it may be called for
 * the main thread even before the gwthread library has been initialized
 * and after it had been shut down. */
long gwthread_self(void);

/* Same as above, but returns the process id (pid) of the thread. */
long gwthread_self_pid(void);

/* Same as gwthread_self() and gwthread_self_pid() combined to one void
 * call. Returns the internal thread id and the process id. */
void gwthread_self_ids(long *tid, long *pid);

/* If the other thread is currently in gwthread_pollfd or gwthread_sleep,
 * make it return immediately.  Otherwise, make it return immediately, the
 * next time it calls one of those functions. */
void gwthread_wakeup(long thread);

/* Wake up all threads */
void gwthread_wakeup_all(void);

/* Wrapper around the poll() system call, for one file descriptor.
 * "events" is a set of the flags defined in <sys/poll.h>, usually
 * POLLIN, POLLOUT, or (POLLIN|POLLOUT).  Return when one of the
 * events is true, or when another thread calls gwthread_wakeup on us, or
 * when the timeout expires.  The timeout is specified in seconds,
 * and a negative value means do not time out.  Return the revents
 * structure filled in by poll() for this fd.  Return -1 if something
 * went wrong. */
int gwthread_pollfd(int fd, int events, double timeout);

/* Wrapper around the poll() system call, for an array of file
 * descriptors.  The difference with normal poll is that the
 * thread can be woken up with gwthread_wakeup.  timeout is in seconds. */
/* NOTE: This interface will probably change in the future, because currently
 * it is hard to implement efficiently. */
int gwthread_poll(struct pollfd *fds, long numfds, double timeout);

/* Sleep until "seconds" seconds have elapsed, or until another thread
 * calls gwthread_wakeup on us.  Fractional seconds are allowed. */
void gwthread_sleep(double seconds);

/* Sleep until "seconds" seconds have elapsed, or until another thread
 * calls gwthread_wakeup on us.  Fractional seconds are allowed. */
void gwthread_sleep_micro(double dseconds);

/* Force a specific thread to terminate. Returns 0 on success, -1 if the
 * thread has been terminated while calling and non-zero for the pthread
 * specific error code.
 */
int gwthread_cancel(long thread); 
 
/*
 * Check wheather this thread should handle the given signal.
 * Since signals are thread specific, this needs to be handled
 * by the thread code here.  This is mostly to cope with
 * "interesting" implementations of "pthreads"
 */
int gwthread_shouldhandlesignal(int signal);

/* Dump the current signal mask for this thread. This will print out a
 * set of debug messages that state the signal handling status for the
 * first 32 signals on the current system. Return -1 if something goes
 * wrong.
 *
 * Debugging purposes mostly so it can be ignored on platforms
 * where this isn't applicable.
 */
int gwthread_dumpsigmask(void);


#endif
