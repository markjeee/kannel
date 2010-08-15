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
 * fdset.c - module for managing a large collection of file descriptors
 */

#include "gw-config.h"
 
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "gwlib/gwlib.h"


struct FDSet
{
    /* Thread ID of the set's internal thread, which will spend most
     * of its time blocking on poll().  This is set when the thread
     * is created, and not changed after that.  It's not protected
     * by any lock. */
    long poll_thread;

    /* The following fields are for use by the polling thread only.
     * No-one else may touch them.  It's not protected by any lock. */

    /* Array for use with poll().  Elements 0 through size-1 are allocated.
     * Elements 0 through entries-1 are in use. */
    struct pollfd *pollinfo;
    int size;
    int entries;
    
    /* Array of times when appropriate fd got any event or events bitmask changed */
    time_t *times;

    /* timeout for this fdset */
    long timeout;

    /* Arrays of callback and data fields.  They are kept in sync with
     * the pollinfo array, and are basically extra fields that we couldn't
     * put in struct pollfd because that structure is defined externally. */
    fdset_callback_t **callbacks;
    void **datafields;

    /* The poller function loops over the table after poll() returns,
     * and calls callback functions that may modify the table that is
     * being scanned.  We can't just copy the table to avoid interference,
     * because fdset_unregister and fdset_listen guarantee that their
     * operations are complete when they return -- that does not work
     * if poller() is scanning an outdated copy of the table.
     * To solve this, we have a field that marks when the table is
     * being scanned.  If this field is true, fdset_unregister merely
     * sets the fd to -1 instead of deleting the whole entry.
     * fdset_listen will takes care to modify revents as well as
     * events. fdset_register always adds to the end of the table,
     * so it does not have to do anything special.
     */
    int scanning;

    /* This field keeps track of how many fds were set to -1 by
     * fdset_unregister while "scanning" is true.  That way we can
     * efficiently check if we need to scan the table to really 
     * delete those entries. */
    int deleted_entries;

    
    /* The following fields are for general use, and are of types that
     * have internal locks. */

    /* List of struct action.  Used by other threads to make requests
     * of the polling thread. */
    List *actions;
};

/* Datatype to describe changes to the fdset fields that only the polling
 * thread may touch.  Other threads use this type to submit requests to
 * change those fields. */
/* Action life cycle: Created, then pushed on set->actions list by
 * action_submit.  Poller thread wakes up and takes it from the list,
 * then calls handle_action, which performs the action and pushes it
 * on the action's done list.  action_submit then takes it back and
 * destroys it. */
/* If no synchronization is needed, action_submit_nosync can be used.
 * In that case handle_action will destroy the action itself instead
 * of putting it on any list. */
struct action
{
    enum { REGISTER, LISTEN, UNREGISTER, DESTROY, SET_TIMEOUT } type;
    int fd;                     /* Used by REGISTER, LISTEN, and UNREGISTER */
    int mask;                   /* Used by LISTEN */
    int events;                 /* Used by REGISTER and LISTEN */
    fdset_callback_t *callback; /* Used by REGISTER */
    void *data;                 /* Used by REGISTER */
    long timeout;               /* Used by SET_TIMEOUT */
    /* When the request has been handled, an element is produced on this
     * list, so that the submitter can synchronize.  Can be left NULL. */
    List *done;                 /* Used by LISTEN, UNREGISTER, and DESTROY */
};

/* Return a new action structure of the given type, with all fields empty. */
static struct action *action_create(int type)
{
    struct action *new;

    new = gw_malloc(sizeof(*new));
    new->type = type;
    new->fd = -1;
    new->mask = 0;
    new->events = 0;
    new->callback = NULL;
    new->data = NULL;
    new->done = NULL;

    return new;
}

static void action_destroy(struct action *action)
{
    if (action == NULL)
        return;

    gwlist_destroy(action->done, NULL);
    gw_free(action);
}

/* For use with gwlist_destroy */
static void action_destroy_item(void *action)
{
    action_destroy(action);
}


/*
 * Submit an action for this set, and wait for the polling thread to
 * confirm that it's been done, by pushing the action on its done list.
 */
static void submit_action(FDSet *set, struct action *action)
{
    List *done;
    void *sync;

    gw_assert(set != NULL);
    gw_assert(action != NULL);

    done = gwlist_create();
    gwlist_add_producer(done);

    action->done = done;

    gwlist_append(set->actions, action);
    gwthread_wakeup(set->poll_thread);

    sync = gwlist_consume(done);
    gw_assert(sync == action);

    action_destroy(action);
}

/* 
 * As above, but don't wait for confirmation.
 */
static void submit_action_nosync(FDSet *set, struct action *action)
{
    gwlist_append(set->actions, action);
    gwthread_wakeup(set->poll_thread);
}

/* Do one action for this thread and confirm that it's been done by
 * appending the action to its done list.  May only be called by
 * the polling thread.  Returns 0 normally, and returns -1 if the
 * action destroyed the set. */
static int handle_action(FDSet *set, struct action *action)
{
    int result;

    gw_assert(set != NULL);
    gw_assert(set->poll_thread == gwthread_self());
    gw_assert(action != NULL);

    result = 0;

    switch (action->type) {
    case REGISTER:
        fdset_register(set, action->fd, action->events,
                       action->callback, action->data);
        break;
    case LISTEN:
        fdset_listen(set, action->fd, action->mask, action->events);
        break;
    case UNREGISTER:
        fdset_unregister(set, action->fd);
        break;
    case DESTROY:
        fdset_destroy(set);
        result = -1;
        break;
    case SET_TIMEOUT:
        set->timeout = action->timeout;
        break;
    default:
        panic(0, "fdset: handle_action got unknown action type %d.",
              action->type);
    }

    if (action->done == NULL)
	action_destroy(action);
    else
        gwlist_produce(action->done, action);

    return result;
}

/* Look up the entry number in the pollinfo array for this fd.
 * Right now it's a linear search, this may have to be improved. */
static int find_entry(FDSet *set, int fd)
{
    int i;

    gw_assert(set != NULL);
    gw_assert(gwthread_self() == set->poll_thread);

    for (i = 0; i < set->entries; i++) {
        if (set->pollinfo[i].fd == fd)
            return i;
    }

    return -1;
}

static void remove_entry(FDSet *set, int entry)
{
    if (entry != set->entries - 1) {
        /* We need to keep the array contiguous, so move the last element
         * to fill in the hole. */
        set->pollinfo[entry] = set->pollinfo[set->entries - 1];
        set->callbacks[entry] = set->callbacks[set->entries - 1];
        set->datafields[entry] = set->datafields[set->entries - 1];
        set->times[entry] = set->times[set->entries - 1];
    }
    set->entries--;
}

static void remove_deleted_entries(FDSet *set)
{
    int i;

    i = 0;
    while (i < set->entries && set->deleted_entries > 0) {
        if (set->pollinfo[i].fd < 0) {
            remove_entry(set, i);
	    set->deleted_entries--;
	} else {
	    i++;
        }
    }
}

/* Main function for polling thread.  Most its time is spent blocking
 * in poll().  No-one else is allowed to change the fields it uses,
 * so other threads just put something on the actions list and wake
 * up this thread.  That's why it checks the actions list every time
 * it goes through the loop.
 */
static void poller(void *arg)
{
    FDSet *set = arg;
    struct action *action;
    int ret;
    int i;
    time_t now;

    gw_assert(set != NULL);

    for (;;) {
        while ((action = gwlist_extract_first(set->actions)) != NULL) {
            /* handle_action returns -1 if the set was destroyed. */
            if (handle_action(set, action) < 0)
                return;
        }

        /* Block for defined timeout, waiting for activity */
        ret = gwthread_poll(set->pollinfo, set->entries, set->timeout);

        if (ret < 0) {
            if (errno != EINTR) {
                error(errno, "Poller: can't handle error; sleeping 1 second.");
                gwthread_sleep(1.0);
            }
            continue;
        }
        time(&now);
        /* Callbacks may modify the table while we scan it, so be careful. */
        set->scanning = 1;
        for (i = 0; i < set->entries; i++) {
            if (set->pollinfo[i].revents != 0) {
                set->callbacks[i](set->pollinfo[i].fd,
                                set->pollinfo[i].revents,
                                set->datafields[i]);
                /* update event time */
                time(&set->times[i]);
            } else if (set->timeout > 0 && difftime(set->times[i] + set->timeout, now) <= 0) {
                debug("gwlib.fdset", 0, "Timeout for fd:%d appeares.", set->pollinfo[i].fd);
                set->callbacks[i](set->pollinfo[i].fd, POLLERR, set->datafields[i]);
            }
        }
        set->scanning = 0;

    if (set->deleted_entries > 0)
        remove_deleted_entries(set);
    }
}


FDSet *fdset_create_real(long timeout)
{
    FDSet *new;

    new = gw_malloc(sizeof(*new));

    /* Start off with space for one element because we can't malloc 0 bytes
     * and we don't want to worry about these pointers being NULL. */
    new->size = 1;
    new->entries = 0;
    new->pollinfo = gw_malloc(sizeof(new->pollinfo[0]) * new->size);
    new->callbacks = gw_malloc(sizeof(new->callbacks[0]) * new->size);
    new->datafields = gw_malloc(sizeof(new->datafields[0]) * new->size);
    new->times = gw_malloc(sizeof(new->times[0]) * new->size);
    new->timeout = timeout > 0 ? timeout : -1;
    new->scanning = 0;
    new->deleted_entries = 0;

    new->actions = gwlist_create();

    new->poll_thread = gwthread_create(poller, new);
    if (new->poll_thread < 0) {
        error(0, "Could not start internal thread for fdset.");
        fdset_destroy(new);
        return NULL;
    }

    return new;
}

void fdset_destroy(FDSet *set)
{
    if (set == NULL)
        return;

    if (set->poll_thread < 0 || gwthread_self() == set->poll_thread) {
        if (set->entries > 0) {
            warning(0, "Destroying fdset with %d active entries.",
                    set->entries);
        }
        gw_free(set->pollinfo);
        gw_free(set->callbacks);
        gw_free(set->datafields);
        gw_free(set->times);
        if (gwlist_len(set->actions) > 0) {
            error(0, "Destroying fdset with %ld pending actions.",
                  gwlist_len(set->actions));
        }
        gwlist_destroy(set->actions, action_destroy_item);
        gw_free(set);
    } else {
        long thread = set->poll_thread;
        submit_action(set, action_create(DESTROY));
	gwthread_join(thread);
    }
}

void fdset_register(FDSet *set, int fd, int events,
                    fdset_callback_t callback, void *data)
{
    int new;

    gw_assert(set != NULL);

    if (gwthread_self() != set->poll_thread) {
        struct action *action;

        action = action_create(REGISTER);
        action->fd = fd;
        action->events = events;
        action->callback = callback;
        action->data = data;
	submit_action_nosync(set, action);
        return;
    }

    gw_assert(set->entries <= set->size);

    if (set->entries >= set->size) {
        int newsize = set->entries + 1;
        set->pollinfo = gw_realloc(set->pollinfo,
                                   sizeof(set->pollinfo[0]) * newsize);
        set->callbacks = gw_realloc(set->callbacks,
                                   sizeof(set->callbacks[0]) * newsize);
        set->datafields = gw_realloc(set->datafields,
                                   sizeof(set->datafields[0]) * newsize);
        set->times = gw_realloc(set->times, sizeof(set->times[0]) * newsize);
        set->size = newsize;
    }

    /* We don't check set->scanning.  Adding new entries is not harmful
     * because their revents fields are 0. */

    new = set->entries++;
    set->pollinfo[new].fd = fd;
    set->pollinfo[new].events = events;
    set->pollinfo[new].revents = 0;
    set->callbacks[new] = callback;
    set->datafields[new] = data;
    time(&set->times[new]);
}

void fdset_listen(FDSet *set, int fd, int mask, int events)
{
    int entry;

    gw_assert(set != NULL);

    if (gwthread_self() != set->poll_thread) {
        struct action *action;

        action = action_create(LISTEN);
        action->fd = fd;
	action->mask = mask;
        action->events = events;
        submit_action(set, action);
        return;
    }

    entry = find_entry(set, fd);   
    if (entry < 0) {
        warning(0, "fdset_listen called on unregistered fd %d.", fd);
        return;
    }

    /* Copy the bits from events specified by the mask, and preserve the
     * bits not specified by the mask. */
    set->pollinfo[entry].events =
	(set->pollinfo[entry].events & ~mask) | (events & mask);

    /* If poller is currently scanning the array, then change the
     * revents field so that the callback function will not be called
     * for events we should no longer listen for.  The idea is the
     * same as for the events field, except that we only turn bits off. */
    if (set->scanning) {
        set->pollinfo[entry].revents =
            set->pollinfo[entry].revents & (events | ~mask);
    }
    
    time(&set->times[entry]);
}

void fdset_unregister(FDSet *set, int fd)
{
    int entry;

    gw_assert(set != NULL);

    if (gwthread_self() != set->poll_thread) {
        struct action *action;

        action = action_create(UNREGISTER);
        action->fd = fd;
        submit_action(set, action);
        return;
    }

    /* Remove the entry from the pollinfo array */

    entry = find_entry(set, fd);
    if (entry < 0) {
        warning(0, "fdset_listen called on unregistered fd %d.", fd);
        return;
    }

    if (entry == set->entries - 1) {
        /* It's the last entry.  We can safely remove it even while
         * the array is being scanned, because the scan checks set->entries. */
        set->entries--;
    } else if (set->scanning) {
        /* We can't remove entries because the array is being
         * scanned.  Mark it as deleted.  */
        set->pollinfo[entry].fd = -1;
        set->deleted_entries++;
    } else {
        remove_entry(set, entry);
    }
}

void fdset_set_timeout(FDSet *set, long timeout)
{
    gw_assert(set != NULL);

    if (gwthread_self() != set->poll_thread) {
        struct action *action;

        action = action_create(SET_TIMEOUT);
        action->timeout = timeout;
        submit_action(set, action);
        return;
    }
    set->timeout = timeout;
}
