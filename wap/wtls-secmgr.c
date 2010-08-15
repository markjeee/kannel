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
 * gw/wtls-secmgr.c - wapbox wtls security manager
 *
 * The security manager's interface consists of two functions:
 *
 *      wtls_secmgr_start()
 *              This starts the security manager thread.
 *
 *      wtls_secmgr_dispatch(event)
 *              This adds a new event to the security manager's event
 *              queue.
 *
 * The wtls security manager is a thread that reads events from its event
 * queue, and feeds back events to the WTLS layer. Here is where various
 * approvals or rejections are made to requested security settings.
 *
 */

#include <string.h>

#include "gwlib/gwlib.h"

#if (HAVE_WTLS_OPENSSL)

#include "wtls.h"

/*
 * Give the status the module:
 *
 *      limbo
 *              not running at all
 *      running
 *              operating normally
 *      terminating
 *              waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } run_status = limbo;


/*
 * The queue of incoming events.
 */
static List *secmgr_queue = NULL;

/*
 * Private functions.
 */

static void main_thread(void *);

/***********************************************************************
 * The public interface to the application layer.
 */

void wtls_secmgr_init(void) {
        gw_assert(run_status == limbo);
        secmgr_queue = gwlist_create();
        gwlist_add_producer(secmgr_queue);
        run_status = running;
        gwthread_create(main_thread, NULL);
}


void wtls_secmgr_shutdown(void) {
        gw_assert(run_status == running);
        gwlist_remove_producer(secmgr_queue);
        run_status = terminating;
        
        gwthread_join_every(main_thread);
        
        gwlist_destroy(secmgr_queue, wap_event_destroy_item);
}


void wtls_secmgr_dispatch(WAPEvent *event) {
        gw_assert(run_status == running);
        gwlist_produce(secmgr_queue, event);
}


long wtls_secmgr_get_load(void) {
        gw_assert(run_status == running);
        return gwlist_len(secmgr_queue);
}


/***********************************************************************
 * Private functions.
 */


static void main_thread(void *arg) {
        WAPEvent *ind, *res, *req, *term;
        
        while (run_status == running && (ind = gwlist_consume(secmgr_queue)) != NULL) {
                switch (ind->type) {
                case SEC_Create_Ind:
                        /* Process the cipherlist */
                        /* Process the MAClist */
                        /* Process the PKIlist */
                        /* Dispatch a SEC_Create_Res */
                        res = wap_event_create(SEC_Create_Res);
                        res->u.SEC_Create_Res.addr_tuple =
                                wap_addr_tuple_duplicate(ind->u.SEC_Create_Ind.addr_tuple);
                        wtls_dispatch_event(res);
                        debug("wtls_secmgr : main_thread", 0,"Dispatching SEC_Create_Res event");
                        /* Dispatch a SEC_Exchange_Req or maybe a SEC_Commit_Req */
                        req = wap_event_create(SEC_Exchange_Req);
                        req->u.SEC_Exchange_Req.addr_tuple =
                                wap_addr_tuple_duplicate(ind->u.SEC_Create_Ind.addr_tuple);
                        wtls_dispatch_event(req);
                        debug("wtls_secmgr : main_thread", 0,"Dispatching SEC_Exchange_Req event");
                        wap_event_destroy(ind);
                        break;
                case SEC_Terminate_Req:
                        /* Dispatch a SEC_Terminate_Req */
                        term = wap_event_create(SEC_Terminate_Req);
                        term->u.SEC_Terminate_Req.addr_tuple =
                                wap_addr_tuple_duplicate(ind->u.SEC_Create_Ind.addr_tuple);
                        term->u.SEC_Terminate_Req.alert_desc = 0;
                        term->u.SEC_Terminate_Req.alert_level = 3;
                       wtls_dispatch_event(term);
                default:
                        panic(0, "WTLS-secmgr: Can't handle %s event",
                              wap_event_name(ind->type));
                        break;
                }
        }
}

#endif
