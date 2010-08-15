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
 * wtp_tid.c - Implementation of WTP tid validation tests. Note that only 
 * WTP responder uses tid validation.
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#include "gwlib/gwlib.h"
#include "wtp_tid.h"

/*
 * Constants used for defining the tid cache status
 */
enum {
    no_cache = -1,
    iniatilised = -2,
    not_iniatilised = -3,
    cached = 0
};

/*
 * Global data structure:
 *
 * Tid cache is implemented by using a library object List
 */
 static List *tid_cache = NULL;   

/*****************************************************************************
 * Prototypes of internal functions
 */
static WTPCached_tid *cache_item_create_empty(void);
static void cache_item_destroy(void *item);
/*
static void cache_item_dump(WTPCached_tid *item);
*/
static void add_tid(WTPRespMachine *resp_machine, long tid);
static void set_tid_by_item(WTPCached_tid *item, long tid);
static int tid_in_window(long rcv_tid, long last_tid);
static WTPCached_tid *tid_cached(WTPRespMachine *resp_machine);

/******************************************************************************
 *
 * External functions:
 */

void wtp_tid_cache_init(void) 
{
    tid_cache = gwlist_create();
}

void wtp_tid_cache_shutdown(void) 
{
    debug("wap.wtp_tid", 0, "%ld items left in the tid cache", 
          gwlist_len(tid_cache));
    gwlist_destroy(tid_cache, cache_item_destroy);
}

/*
 * Tid verification is invoked, when tid_new flag of the incoming message is 
 * on. It is not, if the initiator is not yet cached. If initiator is cached, 
 * the received tid is stored.
 */
int wtp_tid_is_valid(WAPEvent *event, WTPRespMachine *resp_machine)
{
    long rcv_tid = -1,
         last_tid = -1;

    WTPCached_tid *item = NULL;

#if 0
    debug("wap.wtp.tid", 0, "starting validation");
#endif
    rcv_tid = resp_machine->tid;
   
    if (!event->u.RcvInvoke.tid_new) {
/*
 * First we check whether the current initiator has a cache item for it.
 */      
        if ((item = tid_cached(resp_machine)) == NULL) {
            if (event->u.RcvInvoke.no_cache_supported)
                return no_cached_tid;
            else {
#if 0
             debug("wap.wtp.tid", 0, "empty cache");    
#endif
	        add_tid(resp_machine, rcv_tid);
                return ok;
            }
        }
/*
 * If it has, we check if the message is a duplicate or has tid wrapped up 
 * confusingly.
 */      
        last_tid = item->tid; 
      
        if (tid_in_window(rcv_tid, last_tid) == 0){
            info(0, "WTP_TID: tid out of the window");
            return fail;
        } else {
#if 0
            debug("wap.wtp.tid", 0, "tid in the window");
#endif
            set_tid_by_item(item, rcv_tid);
            return ok;
        }

    } else {
        info(0, "WTP_TID: tid_new flag on");
        rcv_tid = 0;

        if (item == NULL) {
            add_tid(resp_machine, rcv_tid);
        } else {
            set_tid_by_item(item, rcv_tid);
        }
     
        return fail;
   }
/*
 * This return is unnecessary but the compiler demands it
 */
   return fail;
}

/*
 * Changes tid value used by an existing initiator. Input responder machine 
 * and the new tid.
 */
void wtp_tid_set_by_machine(WTPRespMachine *resp_machine, long tid)
{
    WTPCached_tid *item = NULL;
       
    item = tid_cached(resp_machine);
    set_tid_by_item(item, tid);
}

/*****************************************************************************
 *
 * Internal functions:
 *
 * Checks whether the received tid is inside the window of acceptable ones. 
 * The size of the window is set by the constant WTP_TID_WINDOW_SIZE (half of 
 * the tid space is the recommended value). 
 *
 * Inputs: stored tid, received tid. Output 0, if received tid is outside the 
 * window, 1, if it is inside.
 */
static int tid_in_window(long rcv_tid, long last_tid)
{
#if 0
    debug("wap.wtp.tid", 0, "tids were rcv_tid, %ld and last_tid, %ld"
          " and test window %ld", rcv_tid, last_tid, WTP_TID_WINDOW_SIZE); 
#endif
    if (last_tid == rcv_tid) {
	return 0;
    } 

    if (rcv_tid > last_tid) {
	if (abs(rcv_tid - last_tid) <= WTP_TID_WINDOW_SIZE) {
            return 1;
        } else {
            return 0;
        }
    }
       
    if (rcv_tid < last_tid) {
        if (abs(rcv_tid - last_tid) >= WTP_TID_WINDOW_SIZE){
            return 1;
        } else {
           return 0;
        }
    }

/*
 * Following return is unnecessary but our compiler demands it
 */
       return 0;
}

static WTPCached_tid *cache_item_create_empty(void)
{
    WTPCached_tid *item = NULL;

    item = gw_malloc(sizeof(*item));
    item->addr_tuple = NULL;
    item->tid = 0;

    return item;
}

static void cache_item_destroy(void *p)
{
    WTPCached_tid *item;
	
    item = p;
    wap_addr_tuple_destroy(item->addr_tuple);
    gw_free(item);
}

/*
 * Checking whether there is an item stored for a specific initiator. Receives 
 * address quadruplet - the identifier it uses - from object WTPRespMachine. 
 * Ditto tid. Returns the item or NULL, if there is not one. Initiator is 
 * identified by the address four-tuple.
 */

static int tid_is_cached(void *a, void *b)
{
    WAPAddrTuple *initiator_profile;
    WTPCached_tid *item;

    item = a;
    initiator_profile = b;

    return wap_addr_tuple_same(item->addr_tuple, initiator_profile);
}

static WTPCached_tid *tid_cached(WTPRespMachine *resp_machine)
{
    WTPCached_tid *item = NULL;

    item = gwlist_search(tid_cache, resp_machine->addr_tuple, tid_is_cached);

    return item;
}

/*
 * Adds an item to the tid cache, one item per every initiator. Initiator is 
 * identified by the address four-tuple, fetched from a wtp responder machine.
 */ 
static void add_tid(WTPRespMachine *resp_machine, long tid)
{
    WTPCached_tid *new_item = NULL;
       
    new_item = cache_item_create_empty(); 
    new_item->addr_tuple = wap_addr_tuple_duplicate(resp_machine->addr_tuple);
    new_item->tid = tid; 

    gwlist_append(tid_cache, new_item);
}

/*
 * Set tid for an existing initiator. Input a cache item and the new tid.
 */
static void set_tid_by_item(WTPCached_tid *item, long tid)
{
    gwlist_lock(tid_cache);
    item->tid = tid;
    gwlist_unlock(tid_cache);
}
