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
 * gwlib/counter.c - a counter object
 *
 * This file implements the Counter objects declared in counter.h.
 *
 * Lars Wirzenius.
 *
 * Changed the counter type 'long' into 'unsigned long' so it wraps 
 * by itself. Just keep increasing it.
 * Also added a counter_increase_with function.
 * harrie@lisanza.net
 */

#include <limits.h>

#include "gwlib.h"

struct Counter
{
#ifdef HAVE_PTHREAD_SPINLOCK_T
    pthread_spinlock_t lock;
#else
    Mutex *lock;
#endif
    unsigned long n;
};


#ifdef HAVE_PTHREAD_SPINLOCK_T
#define lock(c) pthread_spin_lock(&c->lock)
#define unlock(c) pthread_spin_unlock(&c->lock)
#else
#define lock(c) mutex_lock(c->lock)
#define unlock(c) mutex_unlock(c->lock)
#endif


Counter *counter_create(void)
{
    Counter *counter;

    counter = gw_malloc(sizeof(Counter));
#ifdef HAVE_PTHREAD_SPINLOCK_T
    pthread_spin_init(&counter->lock, 0);
#else
    counter->lock = mutex_create();
#endif

    counter->n = 0;
    return counter;
}


void counter_destroy(Counter *counter)
{
    if (counter == NULL)
        return;

#ifdef HAVE_PTHREAD_SPINLOCK_T
    pthread_spin_destroy(&counter->lock);
#else
    mutex_destroy(counter->lock);
#endif
    gw_free(counter);
}

unsigned long counter_increase(Counter *counter)
{
    unsigned long ret;

    lock(counter);
    ret = counter->n;
    ++counter->n;
    unlock(counter);
    return ret;
}

unsigned long counter_increase_with(Counter *counter, unsigned long value)
{
    unsigned long ret;

    lock(counter);
    ret = counter->n;
    counter->n += value;
    unlock(counter);
    return ret;
}

unsigned long counter_value(Counter *counter)
{
    unsigned long ret;

    lock(counter);
    ret = counter->n;
    unlock(counter);
    return ret;
}

unsigned long counter_decrease(Counter *counter)
{
    unsigned long ret;

    lock(counter);
    ret = counter->n;
    if (counter->n > 0)
        --counter->n;
    unlock(counter);
    return ret;
}

unsigned long counter_set(Counter *counter, unsigned long n)
{
    unsigned long ret;

    lock(counter);
    ret = counter->n;
    counter->n = n;
    unlock(counter);
    return ret;
}
