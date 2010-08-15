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
 * semaphore.c - implementation of semaphores
 *
 * Lars Wirzenius
 * Alexander Malysh <a.malysh@centrium.de>, 2004
 */


#include "gwlib/gwlib.h"

#ifdef HAVE_SEMAPHORE
#include <semaphore.h>
#include <errno.h>
#endif

struct Semaphore {
#ifdef HAVE_SEMAPHORE
    sem_t sem;
#else
    List *list;
#endif
};


Semaphore *semaphore_create(long n)
{
    Semaphore *semaphore;
#ifndef HAVE_SEMAPHORE
    static char item;
#endif
    
    semaphore = gw_malloc(sizeof(*semaphore));

#ifdef HAVE_SEMAPHORE
    if (sem_init(&semaphore->sem, 0, (unsigned int) n) != 0)
        panic(errno, "Could not initialize semaphore.");
#else
    semaphore->list = gwlist_create();
    gwlist_add_producer(semaphore->list);
    while (n-- > 0)
	gwlist_produce(semaphore->list, &item);
#endif

    return semaphore;
}


void semaphore_destroy(Semaphore *semaphore)
{
    if (semaphore != NULL) {
#ifdef HAVE_SEMAPHORE
        if (sem_destroy(&semaphore->sem) != 0)
            panic(errno, "Destroying semaphore while some threads are waiting.");
#else
	gwlist_destroy(semaphore->list, NULL);
#endif
	gw_free(semaphore);
    }
}


void semaphore_up(Semaphore *semaphore)
{
#ifndef HAVE_SEMAPHORE
    static char item;
    gw_assert(semaphore != NULL);
    gwlist_produce(semaphore->list, &item);
#else
    gw_assert(semaphore != NULL);
    if (sem_post(&semaphore->sem) != 0)
        error(errno, "Value for semaphore is out of range.");
#endif
}


void semaphore_down(Semaphore *semaphore)
{
    gw_assert(semaphore != NULL);
#ifdef HAVE_SEMAPHORE
    sem_wait(&semaphore->sem);
#else
    gwlist_consume(semaphore->list);
#endif
}


long semaphore_getvalue(Semaphore *semaphore)
{
    gw_assert(semaphore != NULL);
#ifdef HAVE_SEMAPHORE
    {
        int val;
        if (sem_getvalue(&semaphore->sem, &val) != 0)
            panic(errno, "Could not get semaphore value.");
        return val;
    }
#else
    return gwlist_len(semaphore->list);
#endif
}

