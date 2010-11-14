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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "gwlib/gwlib.h"


#ifdef MUTEX_STATS
Mutex *mutex_create_measured(Mutex *mutex, char *filename, int lineno)
{
    mutex->filename = filename;
    mutex->lineno = lineno;
    mutex->locks = 0;
    mutex->collisions = 0;
    return mutex;
}
#endif

Mutex *mutex_create_real(void)
{
    Mutex *mutex;

    mutex = gw_malloc(sizeof(Mutex));
    pthread_mutex_init(&mutex->mutex, NULL);
    mutex->owner = -1;
    mutex->dynamic = 1;
    return mutex;
}

Mutex *mutex_init_static_real(Mutex *mutex)
{
    pthread_mutex_init(&mutex->mutex, NULL);
    mutex->owner = -1;
    mutex->dynamic = 0;
    return mutex;
}

void mutex_destroy(Mutex *mutex)
{
    int ret;

    if (mutex == NULL)
        return;

#ifdef MUTEX_STATS
    if (mutex->locks > 0 || mutex->collisions > 0) {
        info(0, "Mutex %s:%d: %ld locks, %ld collisions.",
             mutex->filename, mutex->lineno,
             mutex->locks, mutex->collisions);
    }
#endif

    if ((ret = pthread_mutex_destroy(&mutex->mutex)) != 0)
        panic(ret, "Attempt to destroy locked mutex!");

    if (mutex->dynamic == 0)
        return;
    gw_free(mutex);
}


void mutex_lock_real(Mutex *mutex, char *file, int line, const char *func)
{
    int ret;

    gw_assert(mutex != NULL);

#ifdef MUTEX_STATS
    ret = pthread_mutex_trylock(&mutex->mutex);
    if (ret != 0) {
        ret = pthread_mutex_lock(&mutex->mutex);
        mutex->collisions++;
    }
    mutex->locks++;
#else
    ret = pthread_mutex_lock(&mutex->mutex);
#endif
    if (ret != 0)
        panic(0, "%s:%ld: %s: Mutex failure! (Called from %s:%ld:%s.)", \
		         __FILE__, (long) __LINE__, __func__, file, (long) line, func);
    if (mutex->owner == gwthread_self())
        panic(0, "%s:%ld: %s: Managed to lock the mutex twice! (Called from %s:%ld:%s.)", \
		         __FILE__, (long) __LINE__, __func__, file, (long) line, func);
    mutex->owner = gwthread_self();
}

int mutex_unlock_real(Mutex *mutex, char *file, int line, const char *func)
{
     int ret;
    
    if (mutex == NULL) {
        error(0, "%s:%ld: %s: Trying to unlock a NULL mutex! (Called from %s:%ld:%s.)", \
		         __FILE__, (long) __LINE__, __func__, file, (long) line, func);
       return -1;
    }
    gw_assert(mutex != NULL);
    mutex->owner = -1;
    ret = pthread_mutex_unlock(&mutex->mutex);
    if (ret != 0)
        panic(0, "%s:%ld: %s: Mutex failure! (Called from %s:%ld:%s.)", \
		         __FILE__, (long) __LINE__, __func__, file, (long) line, func);

    return ret;
}

int mutex_trylock_real(Mutex *mutex, const char *file, int line, const char *func)
{
    int ret;

    if (mutex == NULL) {
        error(0, "%s:%ld: %s: Trying to lock a NULL mutex! (Called from %s:%ld:%s.)", \
		         __FILE__, (long) __LINE__, __func__, file, (long) line, func);
        return -1;
    }

    ret = pthread_mutex_trylock(&mutex->mutex);
    if (ret == 0) {
        if (mutex->owner == gwthread_self())
            panic(0, "%s:%ld: %s: Managed to lock the mutex twice! (Called from %s:%ld:%s.)", \
		         __FILE__, (long) __LINE__, __func__, file, (long) line, func);

        mutex->owner = gwthread_self();
    }
    else if (ret == EINVAL)
        panic(0, "%s:%ld: %s: Mutex failure! (Called from %s:%ld:%s.)", \
		         __FILE__, (long) __LINE__, __func__, file, (long) line, func);

    return ret;
}


