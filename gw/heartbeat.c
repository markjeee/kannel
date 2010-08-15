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
 * heartbeat.c - thread for sending heartbeat Msgs to bearerbox
 */

#include <signal.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "heartbeat.h"

/*
 * Each running heartbeat gets one of these.  They are collected in
 * the heartbeats List.
 */
struct hb_info {
    hb_send_func_t *send_func;
    double freq;
    hb_load_func_t *load_func;
    long thread;
    volatile sig_atomic_t running;
};

/* List of struct hb_info. */
static List *heartbeats = NULL;

/*
 * Look for a hb_info in a list, by thread number.
 */
static int find_hb(void *item, void *pattern)
{
    long *threadnrp;
    struct hb_info *info;

    info = item;
    threadnrp = pattern;

    return info->thread == *threadnrp;
}

static void heartbeat_thread(void *arg)
{
    struct hb_info *info;
    time_t last_hb;

    info = arg;
    last_hb = 0;

    while (info->running) {
        Msg *msg;

        gwthread_sleep(info->freq);

        /*
         * Because the sleep can be interrupted, we might end up sending
         * heartbeats faster than the configured heartbeat frequency.
         * This is not bad unless we send them way too fast.  Make sure
         * our frequency is not more than twice the configured one.
         */
        if (difftime(time(NULL), last_hb) < info->freq / 2)
            continue;

        msg = msg_create(heartbeat);
        if (NULL != info->load_func)
            msg->heartbeat.load = info->load_func();
        info->send_func(msg);
        last_hb = time(NULL);
    }
}

long heartbeat_start(hb_send_func_t *send_func, double freq,
                     hb_load_func_t *load_func)
{
    struct hb_info *info;

    /* can't start with send_funct NULL */
    if (send_func == NULL)
        return -1;

    info = gw_malloc(sizeof(*info));
    info->send_func = send_func;
    info->freq = (freq <= 0 ? DEFAULT_HEARTBEAT : freq);
    info->load_func = load_func;
    info->running = 1;
    info->thread = gwthread_create(heartbeat_thread, info);
    if (info->thread >= 0) {
	if (heartbeats == NULL)
	    heartbeats = gwlist_create();
	gwlist_append(heartbeats, info);
        return info->thread;
    } else {
        gw_free(info);
        return -1;
    }
}

/*
 * function : heartbeat_stop
 * arguments: long hb_thread, the thread number of the heartbeat
 *            that is wished to be stopped.
 *            if hb_thread == ALL_HEARTBEATS then all heartbeats
 *            are stopped.
 * returns  : -
 */
void heartbeat_stop(long hb_thread)
{
    List *matching_info;
    struct hb_info *info;

    /*
     * First, check if there are heartbeats to stop.
     * If not, do not continue, otherwise this function will crash
     */
    if (heartbeats == NULL)
        return;

    if (hb_thread == ALL_HEARTBEATS) {
        while (NULL != (info = gwlist_extract_first(heartbeats))) {
            gw_assert(info);
            info->running = 0;
            gwthread_wakeup(info->thread);
            gwthread_join(info->thread);
            gw_free(info);
        }
    } else {
        matching_info = gwlist_extract_matching(heartbeats, &hb_thread, find_hb);
        if (matching_info == NULL) {
            warning(0, "Could not stop heartbeat %ld: not found.", hb_thread);
	    return;
        }
        gw_assert(gwlist_len(matching_info) == 1);
        info = gwlist_extract_first(matching_info);
        gwlist_destroy(matching_info, NULL);
     
        info->running = 0;
        gwthread_wakeup(hb_thread);
        gwthread_join(hb_thread);
        gw_free(info);
    }
    if (gwlist_len(heartbeats) == 0) {
        gwlist_destroy(heartbeats, NULL);
        heartbeats = NULL;
    }
}
