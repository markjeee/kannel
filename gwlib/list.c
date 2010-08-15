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
 * list.c - generic dynamic list
 *
 * This module implements the generic list. See list.h for an explanation
 * of how to use the list.
 *
 * The list is implemented as an array, a starting index into the array,
 * and an integer giving the length of the list. The list's element i is
 * not necessarily at array element i, but instead it is found at element
 *
 *	(start + i) % len
 *
 * This is because we need to make it fast to use the list as a queue,
 * meaning that adding elements to the end and removing them from the
 * beginning must be very fast. Insertions into the middle of the list
 * need not be fast, however. It would be possible to implement the list
 * with a linked list, of course, but this would cause many more memory
 * allocations: every time an item is added to the list, a new node would
 * need to be allocated, and when it is removed, it would need to be freed.
 * Using an array lets us reduce the number of allocations. It also lets
 * us access an arbitrary element in constant time, which is specially
 * useful since it lets us simplify the list API by not adding iterators
 * or an explicit list item type.
 *
 * If insertions and deletions into the middle of the list become common,
 * it would be more efficient to use a buffer gap implementation, but
 * there's no point in doing that until the need arises.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#include "gw-config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "gwassert.h"
#include "list.h"
#include "log.h"
#include "thread.h"
#include "gwmem.h"


struct List
{
    void **tab;
    long tab_size;
    long start;
    long len;
    Mutex *single_operation_lock;
    Mutex *permanent_lock;
    pthread_cond_t nonempty;
    long num_producers;
};

#define INDEX(list, i)	(((list)->start + i) % (list)->tab_size)
#define GET(list, i)	((list)->tab[INDEX(list, i)])


long gwthread_self(void);

static void lock(List *list);
static void unlock(List *list);
static void make_bigger(List *list, long items);
static void delete_items_from_list(List *list, long pos, long count);


List *gwlist_create_real(void)
{
    List *list;

    list = gw_malloc(sizeof(List));
    list->tab = NULL;
    list->tab_size = 0;
    list->start = 0;
    list->len = 0;
    list->single_operation_lock = mutex_create();
    list->permanent_lock = mutex_create();
    pthread_cond_init(&list->nonempty, NULL);
    list->num_producers = 0;
    return list;
}


void gwlist_destroy(List *list, gwlist_item_destructor_t *destructor)
{
    void *item;

    if (list == NULL)
        return;

    if (destructor != NULL) {
        while ((item = gwlist_extract_first(list)) != NULL)
            destructor(item);
    }

    mutex_destroy(list->permanent_lock);
    mutex_destroy(list->single_operation_lock);
    pthread_cond_destroy(&list->nonempty);
    gw_free(list->tab);
    gw_free(list);
}


long gwlist_len(List *list)
{
    long len;

    if (list == NULL)
        return 0;
    lock(list);
    len = list->len;
    unlock(list);
    return len;
}


void gwlist_append(List *list, void *item)
{
    lock(list);
    make_bigger(list, 1);
    list->tab[INDEX(list, list->len)] = item;
    ++list->len;
    pthread_cond_signal(&list->nonempty);
    unlock(list);
}


void gwlist_append_unique(List *list, void *item, int (*cmp)(void *, void *))
{
    void *it;
    long i;

    lock(list);
    it = NULL;
    for (i = 0; i < list->len; ++i) {
        it = GET(list, i);
        if (cmp(it, item)) {
            break;
        }
    }
    if (i == list->len) {
        /* not yet in list, so add it */
        make_bigger(list, 1);
        list->tab[INDEX(list, list->len)] = item;
        ++list->len;
        pthread_cond_signal(&list->nonempty);
    }
    unlock(list);
}
        

void gwlist_insert(List *list, long pos, void *item)
{
    long i;

    lock(list);
    gw_assert(pos >= 0);
    gw_assert(pos <= list->len);

    make_bigger(list, 1);
    for (i = list->len; i > pos; --i)
        list->tab[INDEX(list, i)] = GET(list, i - 1);
    list->tab[INDEX(list, pos)] = item;
    ++list->len;
    pthread_cond_signal(&list->nonempty);
    unlock(list);
}


void gwlist_delete(List *list, long pos, long count)
{
    lock(list);
    delete_items_from_list(list, pos, count);
    unlock(list);
}


long gwlist_delete_matching(List *list, void *pat, gwlist_item_matches_t *matches)
{
    long i;
    long count;

    lock(list);

    /* XXX this could be made more efficient by noticing
       consecutive items to be removed, but leave that for later.
       --liw */
    i = 0;
    count = 0;
    while (i < list->len) {
        if (matches(GET(list, i), pat)) {
            delete_items_from_list(list, i, 1);
            count++;
        } else {
            ++i;
        }
    }
    unlock(list);

    return count;
}


long gwlist_delete_equal(List *list, void *item)
{
    long i;
    long count;

    lock(list);

    /* XXX this could be made more efficient by noticing
       consecutive items to be removed, but leave that for later.
       --liw */
    i = 0;
    count = 0;
    while (i < list->len) {
        if (GET(list, i) == item) {
            delete_items_from_list(list, i, 1);
            count++;
        } else {
            ++i;
        }
    }
    unlock(list);

    return count;
}


void *gwlist_get(List *list, long pos)
{
    void *item;

    lock(list);
    gw_assert(pos >= 0);
    gw_assert(pos < list->len);
    item = GET(list, pos);
    unlock(list);
    return item;
}


void *gwlist_extract_first(List *list)
{
    void *item;

    gw_assert(list != NULL);
    lock(list);
    if (list->len == 0)
        item = NULL;
    else {
        item = GET(list, 0);
        delete_items_from_list(list, 0, 1);
    }
    unlock(list);
    return item;
}


List *gwlist_extract_matching(List *list, void *pat, gwlist_item_matches_t *cmp)
{
    List *new_list;
    long i;

    new_list = gwlist_create();
    lock(list);
    i = 0;
    while (i < list->len) {
        if (cmp(GET(list, i), pat)) {
            gwlist_append(new_list, GET(list, i));
            delete_items_from_list(list, i, 1);
        } else
            ++i;
    }
    unlock(list);

    if (gwlist_len(new_list) == 0) {
        gwlist_destroy(new_list, NULL);
        return NULL;
    }
    return new_list;
}


void gwlist_lock(List *list)
{
    gw_assert(list != NULL);
    mutex_lock(list->permanent_lock);
}


void gwlist_unlock(List *list)
{
    gw_assert(list != NULL);
    mutex_unlock(list->permanent_lock);
}


int gwlist_wait_until_nonempty(List *list)
{
    int ret;

    lock(list);
    while (list->len == 0 && list->num_producers > 0) {
        list->single_operation_lock->owner = -1;
        pthread_cond_wait(&list->nonempty,
                          &list->single_operation_lock->mutex);
        list->single_operation_lock->owner = gwthread_self();
    }
    if (list->len > 0)
        ret = 1;
    else
        ret = -1;
    unlock(list);
    return ret;
}


void gwlist_add_producer(List *list)
{
    lock(list);
    ++list->num_producers;
    unlock(list);
}


int gwlist_producer_count(List *list)
{
    int ret;
    lock(list);
    ret = list->num_producers;
    unlock(list);
    return ret;
}


void gwlist_remove_producer(List *list)
{
    lock(list);
    gw_assert(list->num_producers > 0);
    --list->num_producers;
    pthread_cond_broadcast(&list->nonempty);
    unlock(list);
}


void gwlist_produce(List *list, void *item)
{
    gwlist_append(list, item);
}


void *gwlist_consume(List *list)
{
    void *item;

    lock(list);
    while (list->len == 0 && list->num_producers > 0) {
        list->single_operation_lock->owner = -1;
        pthread_cond_wait(&list->nonempty,
                          &list->single_operation_lock->mutex);
        list->single_operation_lock->owner = gwthread_self();
    }
    if (list->len > 0) {
        item = GET(list, 0);
        delete_items_from_list(list, 0, 1);
    } else {
        item = NULL;
    }
    unlock(list);
    return item;
}


void *gwlist_timed_consume(List *list, long sec)
{
    void *item;
    struct timespec abstime;
    int rc;

    abstime.tv_sec = time(NULL) + sec;
    abstime.tv_nsec = 0;

    lock(list);
    while (list->len == 0 && list->num_producers > 0) {
        list->single_operation_lock->owner = -1;
        rc = pthread_cond_timedwait(&list->nonempty,
                          &list->single_operation_lock->mutex, &abstime);
        list->single_operation_lock->owner = gwthread_self();
        if (rc == ETIMEDOUT)
            break;
    }
    if (list->len > 0) {
        item = GET(list, 0);
        delete_items_from_list(list, 0, 1);
    } else {
        item = NULL;
    }
    unlock(list);
    return item;
}


void *gwlist_search(List *list, void *pattern, int (*cmp)(void *, void *))
{
    void *item;
    long i;

    lock(list);
    item = NULL;
    for (i = 0; i < list->len; ++i) {
        item = GET(list, i);
        if (cmp(item, pattern)) {
            break;
        }
    }
    if (i == list->len) {
        item = NULL;
    }
    unlock(list);

    return item;
}



List *gwlist_search_all(List *list, void *pattern, int (*cmp)(void *, void *))
{
    List *new_list;
    void *item;
    long i;

    new_list = gwlist_create();

    lock(list);
    item = NULL;
    for (i = 0; i < list->len; ++i) {
        item = GET(list, i);
        if (cmp(item, pattern))
            gwlist_append(new_list, item);
    }
    unlock(list);

    if (gwlist_len(new_list) == 0) {
        gwlist_destroy(new_list, NULL);
        new_list = NULL;
    }

    return new_list;
}


void gwlist_sort(List *list, int(*cmp)(const void *, const void *))
{
    gw_assert(list != NULL && cmp != NULL);

    lock(list);
    if (list->len == 0) {
        /* nothing to sort */
        unlock(list);
        return;
    }
    qsort(&GET(list, 0), list->len, sizeof(void*), cmp);
    unlock(list);
}


/*************************************************************************/

static void lock(List *list)
{
    gw_assert(list != NULL);
    mutex_lock(list->single_operation_lock);
}

static void unlock(List *list)
{
    gw_assert(list != NULL);
    mutex_unlock(list->single_operation_lock);
}


/*
 * Make the array bigger. It might be more efficient to make the size
 * bigger than what is explicitly requested.
 *
 * Assume list has been locked for a single operation already.
 */
static void make_bigger(List *list, long items)
{
    long old_size, new_size;
    long len_at_beginning, len_at_end;

    if (list->len + items <= list->tab_size)
        return;

    old_size = list->tab_size;
    new_size = old_size + items;
    list->tab = gw_realloc(list->tab, new_size * sizeof(void *));
    list->tab_size = new_size;

    /*
     * Now, one of the following situations is in effect
     * (* is used, empty is unused element):
     *
     * Case 1: Used area did not wrap. No action is necessary.
     * 
     *			   old_size              new_size
     *			   v                     v
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * | |*|*|*|*|*|*| | | | | | | | | | | | | | | | |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *   ^           ^
     *   start       start+len
     * 
     * Case 2: Used area wrapped, but the part at the beginning
     * of the array fits into the new area. Action: move part
     * from beginning to new area.
     * 
     *			   old_size              new_size
     *			   v                     v
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |*|*| | | | | | | |*|*|*| | | | | | | | | | | |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *     ^             ^
     *     start+len     start
     * 
     * Case 3: Used area wrapped, and the part at the beginning
     * of the array does not fit into the new area. Action: move
     * as much as will fit from beginning to new area and move
     * the rest to the beginning.
     * 
     *				      old_size   new_size
     *					     v   v
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |*|*|*|*|*|*|*|*|*| | | | | | | | |*|*|*|*| | |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *		     ^               ^
     *		     start+len       start
     */

    gw_assert(list->start < old_size ||
              (list->start == 0 && old_size == 0));
    if (list->start + list->len > old_size) {
        len_at_end = old_size - list->start;
        len_at_beginning = list->len - len_at_end;
        if (len_at_beginning <= new_size - old_size) {
            /* This is Case 2. */
            memmove(list->tab + old_size,
                    list->tab,
                    len_at_beginning * sizeof(void *));
        } else {
            /* This is Case 3. */
            memmove(list->tab + old_size,
                    list->tab,
                    (new_size - old_size) * sizeof(void *));
            memmove(list->tab,
                    list->tab + (new_size - old_size),
                    (len_at_beginning - (new_size - old_size))
                    * sizeof(void *));
        }
    }
}


/*
 * Remove items `pos' through `pos+count-1' from list. Assume list has
 * been locked by caller already.
 */
static void delete_items_from_list(List *list, long pos, long count)
{
    long i, from, to;

    gw_assert(pos >= 0);
    gw_assert(pos < list->len);
    gw_assert(count >= 0);
    gw_assert(pos + count <= list->len);

    /*
     * There are four cases:
     *
     * Case 1: Deletion at beginning of list. Just move start
     * marker forwards (wrapping it at end of array). No need
     * to move any items.
     *
     * Case 2: Deletion at end of list. Just shorten the length
     * of the list. No need to move any items.
     *
     * Case 3: Deletion in the middle so that the list does not
     * wrap in the array. Move remaining items at end of list
     * to the place of the deletion.
     *
     * Case 4: Deletion in the middle so that the list does indeed
     * wrap in the array. Move as many remaining items at the end
     * of the list as will fit to the end of the array, then move
     * the rest to the beginning of the array.
     */
    if (pos == 0) {
        list->start = (list->start + count) % list->tab_size;
        list->len -= count;
    } else if (pos + count == list->len) {
        list->len -= count;
    } else if (list->start + list->len < list->tab_size) {
        memmove(list->tab + list->start + pos,
                list->tab + list->start + pos + count,
                (list->len - pos - count) * sizeof(void *));
        list->len -= count;
    } else {
        /*
         * This is not specially efficient, but it's simple and
         * works. Faster methods would have to take more special
         * cases into account. 
         */
        for (i = 0; i < list->len - count - pos; ++i) {
            from = INDEX(list, pos + i + count);
            to = INDEX(list, pos + i);
            list->tab[to] = list->tab[from];
        }
        list->len -= count;
    }
}
