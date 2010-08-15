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
 * gw-rwlock.c: Implements Reader/Writer Lock.
 * If pthread_rwlock_XXX functions are present then those will be used;
 * otherwise emulation with mutexes/condition should be done.
 *
 * Alexander Malysh <amalysh@kannel.org>, initial version 2004
 */

#include "gw-config.h"
#include "gwlib.h"
#include "gw-rwlock.h"

#define DEBUG 0
#if DEBUG
  #define RWDEBUG(str,lev,frm,args...) debug(str, lev, frm, ## args)
#else
  #define RWDEBUG(str,lev,frm,args...) do{}while(0)
#endif


RWLock *gw_rwlock_create(void)
{
    RWLock *ret = gw_malloc(sizeof(*ret));
#ifdef HAVE_PTHREAD_RWLOCK
    int rc = pthread_rwlock_init(&ret->rwlock, NULL);
    if (rc != 0)
        panic(rc, "Initialization of RWLock failed.");
#else
    ret->writer = -1;
    ret->rwlock = gwlist_create();
    if (ret->rwlock == NULL)
        panic(0, "Initialization of RWLock failed.");
#endif
    ret->dynamic = 1;

    return ret;
}


void gw_rwlock_init_static(RWLock *lock)
{
#ifdef HAVE_PTHREAD_RWLOCK
    int rc = pthread_rwlock_init(&lock->rwlock, NULL);
    if (rc != 0)
        panic(rc, "Initialization of RWLock failed.");
#else
    lock->writer = -1;
    lock->rwlock = gwlist_create();
    if (lock->rwlock == NULL)
        panic(0, "Initialization of RWLock failed.");
#endif
    lock->dynamic = 0;
}


void gw_rwlock_destroy(RWLock *lock)
{
#ifdef HAVE_PTHREAD_RWLOCK
    int ret;
#endif

    if (!lock)
        return;

#ifdef HAVE_PTHREAD_RWLOCK
    ret = pthread_rwlock_destroy(&lock->rwlock);
    if (ret != 0)
        panic(ret, "Attempt to destroy locked rwlock.");
#else
    gwlist_destroy(lock->rwlock, NULL);
#endif

    if (lock->dynamic)
        gw_free(lock);
}


int gw_rwlock_rdlock(RWLock *lock)
{
    int ret = 0;
    gw_assert(lock != NULL);

#ifdef HAVE_PTHREAD_RWLOCK
    ret = pthread_rwlock_rdlock(&lock->rwlock);
    if (ret != 0) {
        panic(ret, "Error while pthread_rwlock_rdlock.");
    }
#else
    gwlist_lock(lock->rwlock);
    gwlist_add_producer(lock->rwlock);
    gwlist_unlock(lock->rwlock);
    RWDEBUG("", 0, "------------ gw_rwlock_rdlock(%p) ----------", lock);
#endif

    return ret;
}


int gw_rwlock_unlock(RWLock *lock)
{
    int ret = 0;
    gw_assert(lock != NULL);

#ifdef HAVE_PTHREAD_RWLOCK
    ret = pthread_rwlock_unlock(&lock->rwlock);
    if (ret != 0)
        panic(ret, "Error while gw_rwlock_unlock.");
#else
    RWDEBUG("", 0, "------------ gw_rwlock_unlock(%p) ----------", lock);
    if (lock->writer == gwthread_self()) {
        lock->writer = -1;
        gwlist_unlock(lock->rwlock);
    } else 
        gwlist_remove_producer(lock->rwlock);
#endif

    return ret;
}


int gw_rwlock_wrlock(RWLock *lock)
{
    int ret = 0;
    gw_assert(lock != NULL);

#ifdef HAVE_PTHREAD_RWLOCK
    ret = pthread_rwlock_wrlock(&lock->rwlock);
    if (ret != 0)
        panic(ret, "Error while pthread_rwlock_wrlock.");
#else
    RWDEBUG("", 0, "------------ gw_rwlock_wrlock(%p) ----------", lock);
    gwlist_lock(lock->rwlock);
    RWDEBUG("", 0, "------------ gw_rwlock_wrlock(%p) producers=%d", lock, gwlist_producer_count(lock->rwlock));
    /* wait for reader */
    gwlist_consume(lock->rwlock);
    lock->writer = gwthread_self();
#endif

    return ret;
}
