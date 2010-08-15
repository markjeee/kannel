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
 * gw-prioqueue.h - generic priority queue.
 *
 * Algorithm ala Robert Sedgewick.
 *
 * Alexander Malysh <olek2002 at hotmail.com>, 2004
 */

#ifndef GW_PRIOQUEUE_H
#define GW_PRIOQUEUE_H 1

typedef struct gw_prioqueue gw_prioqueue_t;

/**
 * Create priority queue
 * @cmp - compare function
 * @return newly created priority queue
 */
gw_prioqueue_t *gw_prioqueue_create(int(*cmp)(const void*, const void *));

/**
 * Destroy priority queue
 * @queue - queue to destroy
 * @item_destroy - item destructor
 */
void gw_prioqueue_destroy(gw_prioqueue_t *queue, void(*item_destroy)(void*));

/**
 * Return priority queue length
 * @queue - priority queue
 * @return length of this priority queue
 */
long gw_prioqueue_len(gw_prioqueue_t *queue);

/**
 * Insert item into the priority queue
 * @queue - priority queue
 * @item - to be inserted item
 */
void gw_prioqueue_insert(gw_prioqueue_t *queue, void *item);

#define gw_prioqueue_produce(queue, item) gw_prioqueue_insert(queue, item)

void gw_prioqueue_foreach(gw_prioqueue_t *queue, void(*fn)(const void *, long));

/**
 * Remove biggest item from the priority queue, but not block if producers
 * available and none items in the queue
 * @queue - priority queue
 * @return - biggest item or NULL if none items in the queue
 */
void *gw_prioqueue_remove(gw_prioqueue_t *queue);

/*
 * Same as gw_prioqueue_remove, except that item is not removed from the
 * priority queue
 */
void *gw_prioqueue_get(gw_prioqueue_t *queue);

/**
 * Remove biggest item from the priority queue, but block if producers
 * available and none items in the queue
 * @queue - priority queue
 * @return biggest item or NULL if none items and none producers in the queue
 */
void *gw_prioqueue_consume(gw_prioqueue_t *queue);

/**
 * Add producer to the priority queue
 * @queue - priority queue
 */
void gw_prioqueue_add_producer(gw_prioqueue_t *queue);

/**
 * Remove producer from the priority queue
 * @queue - priority queue
 */
void gw_prioqueue_remove_producer(gw_prioqueue_t *queue);

/**
 * Return producer count for the priority queue
 * @queue - priority queue
 * @return producer count
 */
long gw_prioqueue_producer_count(gw_prioqueue_t *queue);

#endif
