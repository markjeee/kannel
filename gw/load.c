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

/**
 * load.c
 *
 * Alexander Malysh <amalysh at kannel.org> 2008 for project Kannel
 */

#include "gwlib/gwlib.h"
#include "load.h"


struct load_entry {
    float prev;
    float curr;
    time_t last;
    int interval;
    int dirty;
};


struct load {
    struct load_entry **entries;
    int len;
    int heuristic;
    RWLock *lock;
};


Load* load_create_real(int heuristic)
{
    struct load *load;
    
    load = gw_malloc(sizeof(*load));
    load->len = 0;
    load->entries = NULL;
    load->heuristic = heuristic;
    load->lock = gw_rwlock_create();
    
    return load;
}


int load_add_interval(Load *load, int interval)
{
    int i;
    struct load_entry *entry;
    
    if (load == NULL)
        return -1;
    
    gw_rwlock_wrlock(load->lock);
    
    /* first look if we have equal interval added already */
    for (i = 0; i < load->len; i++) {
        if (load->entries[i]->interval == interval) {
            gw_rwlock_unlock(load->lock);
            return -1;
        }
    }
    /* so no equal interval there, add new one */
    entry = gw_malloc(sizeof(struct load_entry));
    entry->prev = entry->curr = 0.0;
    entry->interval = interval;
    entry->dirty = 1;
    time(&entry->last);
    
    load->entries = gw_realloc(load->entries, sizeof(struct load*) * (load->len + 1));
    load->entries[load->len] = entry;
    load->len++;
    
    gw_rwlock_unlock(load->lock);
    
    return 0;
}

    
void load_destroy(Load *load)
{
    int i;

    if (load == NULL)
        return;

    for (i = 0; i < load->len; i++) {
        gw_free(load->entries[i]);
    }
    gw_free(load->entries);
    gw_rwlock_destroy(load->lock);
    gw_free(load);
}


void load_increase_with(Load *load, unsigned long value)
{
    time_t now;
    int i;
    
    if (load == NULL)
        return;
    gw_rwlock_wrlock(load->lock);
    time(&now);
    for (i = 0; i < load->len; i++) {
        struct load_entry *entry = load->entries[i];
        /* check for special case, load over whole live time */
        if (entry->interval != -1 && now >= entry->last + entry->interval) {
            /* rotate */
            entry->curr /= entry->interval;
            if (entry->prev > 0)
                entry->prev = (2*entry->curr + entry->prev)/3;
            else
                entry->prev = entry->curr;
            entry->last = now;
            entry->curr = 0.0;
            entry->dirty = 0;
        }
        entry->curr += value;
    }
    gw_rwlock_unlock(load->lock);
}


float load_get(Load *load, int pos)
{
    float ret;
    time_t now;
    struct load_entry *entry;

    if (load == NULL || pos >= load->len) {
        return -1.0;
    }

    /* first maybe rotate load */
    load_increase_with(load, 0);
    
    time(&now);
    gw_rwlock_rdlock(load->lock);
    entry = load->entries[pos];
    if (load->heuristic && !entry->dirty) {
        ret = entry->prev;
    } else {
        time_t diff = (now - entry->last);
        if (diff == 0) diff = 1;
        ret = entry->curr/diff;
    }
    gw_rwlock_unlock(load->lock);

    return ret;
}


int load_len(Load *load)
{
    int ret;
    if (load == NULL)
        return 0;
    gw_rwlock_rdlock(load->lock);
    ret = load->len;
    gw_rwlock_unlock(load->lock);
    return ret;
}
