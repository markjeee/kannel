/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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
 * gw/dlr_mem.c
 *
 * Implementation of handling delivery reports (DLRs)
 * in memory
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <stolj@wapme.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
 */

#include "gwlib/gwlib.h"
#include "dlr_p.h"

/*
 * This is the global list where all messages being sent out are being kept track
 * of his list is looked up once a delivery report comes in
 */
static List *dlr_waiting_list;
static RWLock rwlock;

/*
 * Destroy dlr_waiting_list.
 */
static void dlr_mem_shutdown()
{
    gw_rwlock_wrlock(&rwlock);
    gwlist_destroy(dlr_waiting_list, (gwlist_item_destructor_t *)dlr_entry_destroy);
    gw_rwlock_unlock(&rwlock);
    gw_rwlock_destroy(&rwlock);
}

/*
 * Get count of dlr messages waiting.
 */
static long dlr_mem_messages(void)
{
    return gwlist_len(dlr_waiting_list);
}

static void dlr_mem_flush(void)
{
    long i;
    long len;

    gw_rwlock_wrlock(&rwlock);
    len = gwlist_len(dlr_waiting_list);
    for (i = 0; i < len; i++) {
        struct dlr_entry *dlr = gwlist_get(dlr_waiting_list, 0);
        gwlist_delete(dlr_waiting_list, 0, 1);
        dlr_entry_destroy(dlr);
    }
    gw_rwlock_unlock(&rwlock);
}

/*
 * add struct dlr_entry to list
 */
static void dlr_mem_add(struct dlr_entry *dlr)
{
    gw_rwlock_wrlock(&rwlock);
    gwlist_append(dlr_waiting_list,dlr);
    gw_rwlock_unlock(&rwlock);
}

/*
 * Private compare function
 * Return 0 if entry match and 1 if not.
 */
static int dlr_mem_entry_match(struct dlr_entry *dlr, const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    /* XXX: check destination addr too, because e.g. for UCP is not enough to check only
     *          smsc and timestamp (timestamp is even without milliseconds)
     */
    if (dst){
        long pos = octstr_len(dlr->destination) - octstr_len(dst);

        if (pos < 0)
            return 1;

        if (octstr_compare(dlr->smsc, smsc) == 0 && octstr_compare(dlr->timestamp, ts) == 0 && octstr_search(dlr->destination, dst, pos) != -1)
            return 0;
    } else if (octstr_compare(dlr->smsc, smsc) == 0 && octstr_compare(dlr->timestamp, ts) == 0)
        return 0;

    return 1;
}

/*
 * Find matching entry and return copy of it, otherwise NULL
 */
static struct dlr_entry *dlr_mem_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    long i;
    long len;
    struct dlr_entry *dlr = NULL, *ret = NULL;

    gw_rwlock_rdlock(&rwlock);
    len = gwlist_len(dlr_waiting_list);
    for (i=0; i < len; i++) {
        dlr = gwlist_get(dlr_waiting_list, i);

        if (dlr_mem_entry_match(dlr, smsc, ts, dst) == 0) {
            ret = dlr_entry_duplicate(dlr);
            break;
        }
    }
    gw_rwlock_unlock(&rwlock);

    /* we couldnt find a matching entry */
    return ret;
}

/*
 * Remove matching entry
 */
static void dlr_mem_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    long i;
    long len;
    struct dlr_entry *dlr = NULL;

    gw_rwlock_wrlock(&rwlock);
    len = gwlist_len(dlr_waiting_list);
    for (i=0; i < len; i++) {
        dlr = gwlist_get(dlr_waiting_list, i);

        if (dlr_mem_entry_match(dlr, smsc, ts, dst) == 0) {
            gwlist_delete(dlr_waiting_list, i, 1);
            dlr_entry_destroy(dlr);
            break;
        }
    }
    gw_rwlock_unlock(&rwlock);
}

static struct dlr_storage  handles = {
    .type = "internal",
    .dlr_add = dlr_mem_add,
    .dlr_get = dlr_mem_get,
    .dlr_remove = dlr_mem_remove,
    .dlr_shutdown = dlr_mem_shutdown,
    .dlr_messages = dlr_mem_messages,
    .dlr_flush = dlr_mem_flush
};

/*
 * Initialize dlr_waiting_list and return out storage handles.
 */
struct dlr_storage *dlr_init_mem(Cfg *cfg)
{
    dlr_waiting_list = gwlist_create();
    gw_rwlock_init_static(&rwlock);

    return &handles;
}
