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
 * list.h - generic dynamic list
 *
 * This is a generic, dynamic list. Generic means that it stores void pointers
 * (void *), instead of a more specific data type. This allows storage of
 * any type of items in the list. The pointers may not be NULL pointers.
 *
 * A number of operations are defined for the list: create, destroy,
 * query of length, inserting and deleting items, getting items, and so on.
 * See below for a detailed list.
 *
 * The list is also thread safe: each single operation is atomic. For
 * list manipulation that needs to be atomic but uses several single
 * operations, the list supports locking and unlocking. It is up to the
 * caller to make sure the list lock is used properly; the implementation
 * only guarantees the atomicity of single operations.
 *
 * The API also has functions for solving typical producer-consumer problems:
 * the list counts the number of producers it has (they need to register
 * _and_ unregister explicitly) and has functions for adding a produced
 * item to the list and removing an item so that it can be consumed. The
 * consumption function (`gwlist_consume') sleeps, without using processor
 * time, until there is an item to be consumed or there are no more
 * producers. Thus, a typical producer would look like this:
 *
 *	gwlist_add_producer(list);
 *	while ((item = foo()) != NULL)
 *		gwlist_produce(list, item);
 *	gwlist_remove_producer(list);
 *
 * and the corresponding consumer would look like this:
 *
 *	while ((item = gwlist_consume(list)) != NULL)
 *		bar(item);
 *
 * There can be any number of producers and consumers at the same time.
 *
 * List items are numbered starting with `0'.
 *
 * Most list functions can do memory allocations. If these allocations
 * fail, they will kill the program (they use gwlib/gwmem.c for
 * memory allocations, and those do the killing). This is not mentioned
 * explicitly for each function.
 *
 * The module prefix is `list' (in any combination of upper and lower case
 * characters). All externally visible symbols (i.e., those defined by
 * this header file) start with the prefix.
 */

#ifndef LIST_H
#define LIST_H


/*
 * The list type. It is opaque: do not touch it except via the functions
 * defined in this header.
 */
typedef struct List List;


/*
 * A comparison function for list items. Returns true (non-zero) for
 * equal, false for non-equal. Gets an item from the list as the first
 * argument, the pattern as a second argument.
 */
typedef int gwlist_item_matches_t(void *item, void *pattern);


/*
 * A destructor function for list items.  Must free all memory associated
 * with the list item.
 */
typedef void gwlist_item_destructor_t(void *item);


/*
 * Create a list and return a pointer to the list object.
 */
List *gwlist_create_real(void);
#define gwlist_create() gw_claim_area(gwlist_create_real())

/*
 * Destroy the list. If `destructor' is not NULL, first destroy all items
 * by calling it for each item. If it is NULL, the caller is responsible
 * for destroying items. The caller is also responsible for making sure
 * that nothing else tries to touch the list from the time the call to
 * gwlist_destroy starts - this includes the item destructor function.
 */
void gwlist_destroy(List *list, gwlist_item_destructor_t *destructor);


/*
 * Return the number of items in the list.  Return 0 if list is NULL.
 */
long gwlist_len(List *list);


/*
 * Add a new item to the end of the list.
 */
void gwlist_append(List *list, void *item);


/*
 * This is similar to gwlist_append(). If the item is *not* present in the 
 * list it is added to the end of the list, otherwise the item is 
 * discarded and *not* added to the list. Hence you can assume that using
 * this append function you result in a unique item inside the list.
 */
void gwlist_append_unique(List *list, void *item, gwlist_item_matches_t *cmp);


/*
 * Insert an item into the list so that it becomes item number `pos'.
 */
void gwlist_insert(List *list, long pos, void *item);


/*
 * Delete items from the list. Note that this does _not_ free the memory
 * for the items, they are just dropped from the list.
 */
void gwlist_delete(List *list, long pos, long count);


/*
 * Delete all items from the list that match `pattern'. Like gwlist_delete,
 * the items are removed from the list, but are not destroyed themselves.
 * Return the number of items deleted.
 */
long gwlist_delete_matching(List *list, void *pat, gwlist_item_matches_t *cmp);


/*
 * Delete all items from the list whose pointer value is exactly `item'.
 * Return the number of items deleted.
 */
long gwlist_delete_equal(List *list, void *item);


/*
 * Return the item at position `pos'.
 */
void *gwlist_get(List *list, long pos);


/*
 * Remove and return the first item in the list. Return NULL if list is
 * empty. Note that unlike gwlist_consume, this won't sleep until there is
 * something in the list.
 */
void *gwlist_extract_first(List *list);


/*
 * Create a new list with items from `list' that match a pattern. The items
 * are removed from `list'. Return NULL if no matching items are found.
 * Note that unlike gwlist_consume, this won't sleep until there is
 * something in the list.
 */
List *gwlist_extract_matching(List *list, void *pat, gwlist_item_matches_t *cmp);


/*
 * Lock the list. This protects the list from other threads that also
 * lock the list with gwlist_lock, but not from threads that do not.
 * (This is intentional.)
 */
void gwlist_lock(List *list);


/*
 * Unlock the list lock locked by gwlist_lock. Only the owner of the lock
 * may unlock it (although this might not be checked).
 */
void gwlist_unlock(List *list);


/*
 * Sleep until the list is non-empty. Note that after the thread awakes
 * another thread may already have emptied the list again. Those who wish
 * to use this function need to be very careful with gwlist_lock and
 * gwlist_unlock.
 */
int gwlist_wait_until_nonempty(List *list);


/*
 * Register a new producer to the list.
 */
void gwlist_add_producer(List *list);

/*
 * Return the current number of producers for the list
 */
int gwlist_producer_count(List *list);

/*
 * Remove a producer from the list. If the number of producers drops to
 * zero, all threads sleeping in gwlist_consume will awake and return NULL.
 */
void gwlist_remove_producer(List *list);


/*
 * Add an item to the list. This equivalent to gwlist_append, but may be
 * easier to remember.
 */
void gwlist_produce(List *list, void *item);


/*
 * Remove an item from the list, or return NULL if the list was empty
 * and there were no producers. If the list is empty but there are
 * producers, sleep until there is something to return.
 */
void *gwlist_consume(List *list);


/*
 * Remove an item from the list, or return NULL if the list was empty
 * and there were no producers. If the list is empty but there are
 * producers, sleep until there is something to return or timeout occur.
 */
void *gwlist_timed_consume(List *list, long sec);


/*
 * Search the list for a particular item. If not found, return NULL. If found,
 * return the list element. Compare items to search pattern with 
 * `cmp(item, pattern)'. If the function returns non-zero, the items are 
 * equal.
 */
void *gwlist_search(List *list, void *pattern, gwlist_item_matches_t *cmp);


/*
 * Search the list for all items matching a pattern. If not found, return 
 * NULL. If found, return a list with the matching elements. Compare items
 * to search pattern with `cmp(item, pattern)'. If the function returns 
 * non-zero, the items are equal.
 */
List *gwlist_search_all(List *list, void *pattern, gwlist_item_matches_t *cmp);


/*
 * Sort the list with qsort.
 * if you have a list that you feed like that: 
 * Msg *message;
 * gwlist_add(mylist, message); 
 * a function that could sort messages by their data length would look like that:
 * int sort_by_messagelength(void* first_msg_pp, void* second_msg_pp)
 * {
 *     Msg *first_msg=*(Msg**)first_msg_pp;
 *     Msg *second_msg=*(Msg**)second_msg_pp;
 *     return octstr_len(first_msg->sms.msgdata) - octstr_len(second_msg->sms.msgdata);
 * }
 */
void gwlist_sort(List *list, int(*cmp)(const void *, const void *));

#endif
