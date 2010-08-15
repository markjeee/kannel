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
 * gw-prioqueue.c - generic priority queue with guaranteed order.
 *
 * Algorithm ala Robert Sedgewick.
 *
 * Alexander Malysh <amalysh at kannel.org>, 2004, 2008
 */

#include "gw-config.h"
#include <pthread.h>
#include "thread.h"
#include "gwmem.h"
#include "gwassert.h"
#include "gwthread.h"
#include "gw-prioqueue.h"


struct element {
    void *item;
    long long seq;
};

struct gw_prioqueue {
    Mutex *mutex;
    struct element **tab;
    size_t size;
    long len;
    long producers;
    long long seq;
    pthread_cond_t nonempty;
    int (*cmp)(const void*, const void *);
};


static void inline queue_lock(gw_prioqueue_t *queue)
{
    mutex_lock(queue->mutex);
}


static void inline queue_unlock(gw_prioqueue_t *queue)
{
    mutex_unlock(queue->mutex);
}


static void make_bigger(gw_prioqueue_t *queue, long items)
{
    size_t size = queue->size;
    size_t new_size = sizeof(*queue->tab) * (queue->len + items);
    
    if (size >= new_size)
        return;
    
    queue->tab = gw_realloc(queue->tab, new_size);
    queue->size = new_size;
}


static int compare(struct element *a, struct element *b, int(*cmp)(const void*, const void *))
{
    int rc;

    rc = cmp(a->item, b->item);
    if (rc == 0) {
        /* check sequence to guarantee order */
        if (a->seq < b->seq)
            rc = 1;
        else if (a->seq > b->seq)
            rc = -1;
    }

    return rc;
}


/**
 * Heapize up
 * @queue - our prioqueue
 + @index - start index
 */
static void upheap(gw_prioqueue_t *queue, register long index)
{
    struct element *v = queue->tab[index];
    while (queue->tab[index / 2]->item != NULL && compare(queue->tab[index / 2], v, queue->cmp) < 0) {
        queue->tab[index] = queue->tab[index / 2];
        index /= 2;
    }
    queue->tab[index] = v;
}


/**
 * Heapize down
 * @queue - our prioqueue
 * @index - start index
 */
static void downheap(gw_prioqueue_t *queue, register long index)
{
    struct element *v = queue->tab[index];
    register long j;
    
    while (index <= queue->len / 2) {
        j = 2 * index;
        /* take the biggest child item */
        if (j < queue->len && compare(queue->tab[j], queue->tab[j + 1], queue->cmp) < 0)
            j++;
        /* break if our item bigger */
        if (compare(v, queue->tab[j], queue->cmp) >= 0)
            break;
        queue->tab[index] = queue->tab[j];
        index = j;
    }
    queue->tab[index] = v;
}


gw_prioqueue_t *gw_prioqueue_create(int(*cmp)(const void*, const void *))
{
    gw_prioqueue_t *ret;
     
    gw_assert(cmp != NULL);
    
    ret = gw_malloc(sizeof(*ret));
    ret->producers = 0;
    pthread_cond_init(&ret->nonempty, NULL);
    ret->mutex = mutex_create();
    ret->tab = NULL;
    ret->size = 0;
    ret->len = 0;
    ret->seq = 0;
    ret->cmp = cmp;
    
    /* put NULL item at pos 0 that is our stop marker */
    make_bigger(ret, 1);
    ret->tab[0] = gw_malloc(sizeof(**ret->tab));
    ret->tab[0]->item = NULL;
    ret->tab[0]->seq = ret->seq++;
    ret->len++;
    
    return ret;
}


void gw_prioqueue_destroy(gw_prioqueue_t *queue, void(*item_destroy)(void*))
{
    long i;

    if (queue == NULL)
        return;
    
    for (i = 0; i < queue->len; i++) {
        if (item_destroy != NULL && queue->tab[i]->item != NULL)
            item_destroy(queue->tab[i]->item);
        gw_free(queue->tab[i]);
    }
    mutex_destroy(queue->mutex);
    pthread_cond_destroy(&queue->nonempty);
    gw_free(queue->tab);
    gw_free(queue);
}


long gw_prioqueue_len(gw_prioqueue_t *queue)
{
    long len;

    if (queue == NULL)
        return 0;
     
    queue_lock(queue);
    len = queue->len - 1;
    queue_unlock(queue);
    
    return len;
}


void gw_prioqueue_insert(gw_prioqueue_t *queue, void *item)
{
    gw_assert(queue != NULL);
    gw_assert(item != NULL);
    
    queue_lock(queue);
    make_bigger(queue, 1);
    queue->tab[queue->len] = gw_malloc(sizeof(**queue->tab));
    queue->tab[queue->len]->item = item;
    queue->tab[queue->len]->seq = queue->seq++;
    upheap(queue, queue->len);
    queue->len++;
    pthread_cond_signal(&queue->nonempty);
    queue_unlock(queue);
}


void gw_prioqueue_foreach(gw_prioqueue_t *queue, void(*fn)(const void *, long))
{
    register long i;

    gw_assert(queue != NULL && fn != NULL);
    
    queue_lock(queue);
    for (i = 1; i < queue->len; i++)
        fn(queue->tab[i]->item, i - 1);
    queue_unlock(queue);
}


void *gw_prioqueue_remove(gw_prioqueue_t *queue)
{
    void *ret;
    
    gw_assert(queue != NULL);
    
    queue_lock(queue);
    if (queue->len <= 1) {
        queue_unlock(queue);
        return NULL;
    }
    ret = queue->tab[1]->item;
    gw_free(queue->tab[1]);
    queue->tab[1] = queue->tab[--queue->len];
    downheap(queue, 1);
    queue_unlock(queue);
    
    return ret;
}


void *gw_prioqueue_get(gw_prioqueue_t *queue)
{
    void *ret;
    
    gw_assert(queue != NULL);
    
    queue_lock(queue);
    if (queue->len > 1)
        ret = queue->tab[1]->item;
    else
        ret = NULL;
    queue_unlock(queue);
    
    return ret;
}


void *gw_prioqueue_consume(gw_prioqueue_t *queue)
{
    void *ret;
    
    gw_assert(queue != NULL);

    queue_lock(queue);
    while (queue->len == 1 && queue->producers > 0) {
        queue->mutex->owner = -1;
        pthread_cond_wait(&queue->nonempty, &queue->mutex->mutex);
        queue->mutex->owner = gwthread_self();
    }
    if (queue->len > 1) {
        ret = queue->tab[1]->item;
        gw_free(queue->tab[1]);
        queue->tab[1] = queue->tab[--queue->len];
        downheap(queue, 1);
    } else {
        ret = NULL;
    }
    queue_unlock(queue);
    
    return ret;
}


void gw_prioqueue_add_producer(gw_prioqueue_t *queue)
{
    gw_assert(queue != NULL);

    queue_lock(queue);
    queue->producers++;
    queue_unlock(queue);
}


void gw_prioqueue_remove_producer(gw_prioqueue_t *queue)
{
    gw_assert(queue != NULL);

    queue_lock(queue);
    gw_assert(queue->producers > 0);
    queue->producers--;
    pthread_cond_broadcast(&queue->nonempty);
    queue_unlock(queue);
}


long gw_prioqueue_producer_count(gw_prioqueue_t *queue)
{
    long ret;

    gw_assert(queue != NULL);
    
    queue_lock(queue);
    ret = queue->producers;
    queue_unlock(queue);
    
    return ret;
}

