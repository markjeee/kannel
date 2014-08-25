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

/*
 * thread.h - thread manipulation
 */

#ifndef GW_THREAD_H
#define GW_THREAD_H

#include "gw-config.h"

#if !HAVE_PTHREAD_H
#error "You need POSIX.1 threads and <pthread.h> header file"
#endif

#include <pthread.h>

/*
 * Wrapper around pthread_mutex_t to avoid problems with recursive calls
 * to pthread_mutex_trylock on Linux (at least).
 */
typedef struct {
	pthread_mutex_t mutex;
	long owner;
	int dynamic;
#ifdef MUTEX_STATS
	unsigned char *filename;
	int lineno;
	long locks;
	long collisions;
#endif
} Mutex;


/*
 * Create a Mutex.
 */
#ifdef MUTEX_STATS
#define mutex_create() gw_claim_area(mutex_create_measured(mutex_create_real(), \
    	    	    	    	    	                 __FILE__, __LINE__))
#else
#define mutex_create() gw_claim_area(mutex_create_real())
#endif

/*
 * Create a Mutex.  Call these functions via the macro defined above.
 */
Mutex *mutex_create_measured(Mutex *mutex, char *filename, int lineno);
Mutex *mutex_create_real(void);


/*
 * Initialize a statically allocated Mutex.  We need those inside gwlib
 * modules that are in turn used by the mutex wrapper, such as "gwmem" and
 * "protected".
 */
#ifdef MUTEX_STATS
#define mutex_init_static(mutex) \
    mutex_create_measured(mutex_init_static_real(mutex), __FILE__, __LINE__)
#else
#define mutex_init_static(mutex) \
    mutex_init_static_real(mutex)
#endif

Mutex *mutex_init_static_real(Mutex *mutex);


/*
 * Destroy a Mutex.
 */
void mutex_destroy(Mutex *mutex);


/* lock given mutex. PANIC if fails (non-initialized mutex or other
 * coding error) */ 
#define mutex_lock(m) mutex_lock_real(m, __FILE__, __LINE__, __func__)
void mutex_lock_real(Mutex *mutex, char *file, int line, const char *func);


/* unlock given mutex, PANIC if fails (so do not call for non-locked) */
/* returns 0 if ok 1 if failure for debugging */
#define mutex_unlock(m) mutex_unlock_real(m, __FILE__, __LINE__, __func__)
int mutex_unlock_real(Mutex *mutex, char *file, int line, const char *func);


/*
 * Try to lock given mutex, returns -1 if mutex is NULL; 0 if mutex acquired; otherwise
 * EBUSY. PANIC if mutex was not properly initialized before.
 */
#define mutex_trylock(m) mutex_trylock_real(m, __FILE__, __LINE__, __func__)
int mutex_trylock_real(Mutex *mutex, const char *file, int line, const char *func);

#endif


