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
 * SMSC Connection wrapper
 *
 * Interface to old SMS center implementations
 *
 * Kalle Marjola 2000
 */

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"

#include "smsc.h"
#include "smsc_p.h"


typedef struct smsc_wrapper {
    SMSCenter	*smsc;
    List	*outgoing_queue;
    List	*stopped;	/* list-trick for suspend/isolate */ 
    long     	receiver_thread;
    long	sender_thread;
    Mutex	*reconnect_mutex;
} SmscWrapper;


static void smscwrapper_destroy(SmscWrapper *wrap)
{
    if (wrap == NULL)
	return;
    gwlist_destroy(wrap->outgoing_queue, NULL);
    gwlist_destroy(wrap->stopped, NULL);
    mutex_destroy(wrap->reconnect_mutex);
    if (wrap->smsc != NULL)
	smsc_close(wrap->smsc);
    gw_free(wrap);
}


static int reconnect(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;
    Msg *msg;
    int ret;
    int wait = 1;

    /* disable double-reconnect
     * NOTE: it is still possible that we do double-connect if
     *   first thread gets through this if-statement and then
     *   execution switches to another thread.. this can be avoided
     *   via double-mutex system, but I do not feel it is worth it,
     *   maybe later --rpr
     */
    if (conn->status == SMSCCONN_RECONNECTING) {
	mutex_lock(wrap->reconnect_mutex);	/* wait here */
	mutex_unlock(wrap->reconnect_mutex);
	return 0;
    }
    mutex_lock(wrap->reconnect_mutex);

    debug("bb.sms", 0, "smsc_wrapper <%s>: reconnect started",
	  octstr_get_cstr(conn->name));

    while((msg = gwlist_extract_first(wrap->outgoing_queue))!=NULL) {
	bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_TEMPORARILY, NULL);
    }
    conn->status = SMSCCONN_RECONNECTING;
    

    while(conn->why_killed == SMSCCONN_ALIVE) {
	ret = smsc_reopen(wrap->smsc);
	if (ret == 0) {
	    info(0, "Re-open of %s succeeded.", octstr_get_cstr(conn->name));
	    mutex_lock(conn->flow_mutex);
	    conn->status = SMSCCONN_ACTIVE;
	    conn->connect_time = time(NULL);
	    mutex_unlock(conn->flow_mutex);
	    bb_smscconn_connected(conn);
	    break;
	}
	else if (ret == -2) {
	    error(0, "Re-open of %s failed permanently",
		  octstr_get_cstr(conn->name));
	    mutex_lock(conn->flow_mutex);
	    conn->status = SMSCCONN_DISCONNECTED;
	    mutex_unlock(wrap->reconnect_mutex);
	    mutex_unlock(conn->flow_mutex);
	    return -1;	/* permanent failure */
	}
	else {
	    error(0, "Re-open to <%s> failed, retrying after %d minutes...",
		  octstr_get_cstr(conn->name), wait);
	    gwthread_sleep(wait*60.0);

	    wait = wait > 10 ? 10 : wait * 2 + 1;
	}
    }
    mutex_unlock(wrap->reconnect_mutex);
    return 0;
}


static Msg *sms_receive(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;
    int ret;
    Msg *newmsg = NULL;

    if (smscenter_pending_smsmessage(wrap->smsc) == 1) {

        ret = smscenter_receive_msg(wrap->smsc, &newmsg);
        if (ret == 1) {

            /* if any smsc_id available, use it */
            newmsg->sms.smsc_id = octstr_duplicate(conn->id);

	    return newmsg;
        } else if (ret == 0) { /* "NEVER" happens */
            warning(0, "SMSC %s: Pending message returned '1', "
                    "but nothing to receive!", octstr_get_cstr(conn->name));
            msg_destroy(newmsg);
            return NULL;
        } else {
            msg_destroy(newmsg);
	    if (reconnect(conn) == -1)
		smscconn_shutdown(conn, 0);
	    return NULL;
        }
    }
    return NULL;
}


static void wrapper_receiver(void *arg)
{
    Msg 	*msg;
    SMSCConn 	*conn = arg;
    SmscWrapper *wrap = conn->data;
    /* SmscWrapper *wrap = conn->data; ** non-used */
    double 	sleep = 0.0001;
    
    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);
    
    /* remove messages from SMSC until we are killed */
    while(conn->why_killed == SMSCCONN_ALIVE) {

        gwlist_consume(wrap->stopped); /* block here if suspended/isolated */

	msg = sms_receive(conn);
	if (msg) {
            debug("bb.sms", 0, "smscconn (%s): new message received",
		  octstr_get_cstr(conn->name));
            sleep = 0.0001;
	    bb_smscconn_receive(conn, msg);
        }
        else {
	    /* note that this implementations means that we sleep even
	     * when we fail connection.. but time is very short, anyway
	     */
            gwthread_sleep(sleep);
            /* gradually sleep longer and longer times until something starts to
             * happen - this of course reduces response time, but that's better than
             * extensive CPU usage when it is not used
             */
            sleep *= 2;
            if (sleep >= 2.0)
                sleep = 1.999999;
        }
    }
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

    /* this thread is joined at sender */
}



static int sms_send(SMSCConn *conn, Msg *msg)
{
    SmscWrapper *wrap = conn->data;
    int ret;

    debug("bb.sms", 0, "smscconn_sender (%s): sending message",
	  octstr_get_cstr(conn->name));

    ret = smscenter_submit_msg(wrap->smsc, msg);
    if (ret == -1) {
	bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));

	if (reconnect(conn) == -1)
	    smscconn_shutdown(conn, 0);
        return -1;
    } else {
	bb_smscconn_sent(conn, msg, NULL);
        return 0;
    }
}


static void wrapper_sender(void *arg)
{
    Msg 	*msg;
    SMSCConn 	*conn = arg;
    SmscWrapper *wrap = conn->data;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    /* send messages to SMSC until our outgoing_list is empty and
     * no producer anymore (we are set to shutdown) */
    while(conn->status != SMSCCONN_DEAD) {

	if ((msg = gwlist_consume(wrap->outgoing_queue)) == NULL)
            break;

        if (octstr_search_char(msg->sms.receiver, ' ', 0) != -1) {
            /*
             * multi-send: this should be implemented in corresponding
             *  SMSC protocol, but while we are waiting for that...
             */
            int i;
	    Msg *newmsg;
            /* split from spaces: in future, split with something more sensible,
             * this is dangerous... (as space is url-encoded as '+')
             */
            List *nlist = octstr_split_words(msg->sms.receiver);

	    debug("bb.sms", 0, "Handling multi-receiver message");

            for(i=0; i < gwlist_len(nlist); i++) {

		newmsg = msg_duplicate(msg);
                octstr_destroy(newmsg->sms.receiver);

                newmsg->sms.receiver = gwlist_get(nlist, i);
                sms_send(conn, newmsg);
            }
            gwlist_destroy(nlist, NULL);
            msg_destroy(msg);
        }
        else
	    sms_send(conn,msg);

    }
    /* cleanup, we are now dying */

    debug("bb.sms", 0, "SMSCConn %s sender died, waiting for receiver",
	  octstr_get_cstr(conn->name));
    
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

    if (conn->is_stopped) {
	gwlist_remove_producer(wrap->stopped);
	conn->is_stopped = 0;
    }

    gwthread_wakeup(wrap->receiver_thread);
    gwthread_join(wrap->receiver_thread);

    /* call 'failed' to all messages still in queue */
    
    mutex_lock(conn->flow_mutex);

    conn->status = SMSCCONN_DEAD;

    while((msg = gwlist_extract_first(wrap->outgoing_queue))!=NULL) {
	bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
    }
    smscwrapper_destroy(wrap);
    conn->data = NULL;
    
    mutex_unlock(conn->flow_mutex);

    bb_smscconn_killed();
}



static int wrapper_add_msg(SMSCConn *conn, Msg *sms)
{
    SmscWrapper *wrap = conn->data;
    Msg *copy;

    copy = msg_duplicate(sms);
    gwlist_produce(wrap->outgoing_queue, copy);

    return 0;
}


static int wrapper_shutdown(SMSCConn *conn, int finish_sending)
{
    SmscWrapper *wrap = conn->data;

    debug("bb.sms", 0, "Shutting down SMSCConn %s, %s",
	  octstr_get_cstr(conn->name), finish_sending ? "slow" : "instant");
    
    if (finish_sending == 0) {
	Msg *msg; 
	while((msg = gwlist_extract_first(wrap->outgoing_queue))!=NULL) {
	    bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
	}
    }
    gwlist_remove_producer(wrap->outgoing_queue);
    gwthread_wakeup(wrap->sender_thread);
    gwthread_wakeup(wrap->receiver_thread);
    return 0;
}

static void wrapper_stop(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;

    debug("smscconn", 0, "Stopping wrapper");
    gwlist_add_producer(wrap->stopped);

}

static void wrapper_start(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;

    debug("smscconn", 0, "Starting wrapper");
    gwlist_remove_producer(wrap->stopped);
}


static long wrapper_queued(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;
    long ret = gwlist_len(wrap->outgoing_queue);

    /* use internal queue as load, maybe something else later */
    
    conn->load = ret;
    return ret;
}

int smsc_wrapper_create(SMSCConn *conn, CfgGroup *cfg)
{
    /* 1. Call smsc_open()
     * 2. create sender/receiver threads
     * 3. fill up the conn
     *
     * XXX open() SHOULD be done in distinct thread, not here!
     */

    SmscWrapper *wrap;

    wrap = gw_malloc(sizeof(SmscWrapper));
    wrap->smsc = NULL;
    conn->data = wrap;
    conn->send_msg = wrapper_add_msg;
    
    
    wrap->outgoing_queue = gwlist_create();
    wrap->stopped = gwlist_create();
    wrap->reconnect_mutex = mutex_create();
    gwlist_add_producer(wrap->outgoing_queue);
    
    if ((wrap->smsc = smsc_open(cfg)) == NULL)
	goto error;

    conn->name = octstr_create(smsc_name(wrap->smsc));
    conn->status = SMSCCONN_ACTIVE;
    conn->connect_time = time(NULL);

    if (conn->is_stopped)
	gwlist_add_producer(wrap->stopped);
	

    /* XXX here we could fail things... specially if the second one
     *     fails.. so fix this ASAP
     *
     * moreover, open should be in sender/receiver, so that we can continue
     * while trying to open... maybe move this, or just wait for new
     * implementations of various SMSC protocols
     */
    
    if ((wrap->receiver_thread = gwthread_create(wrapper_receiver, conn))==-1)
	goto error;

    if ((wrap->sender_thread = gwthread_create(wrapper_sender, conn))==-1)
	goto error;

    conn->shutdown = wrapper_shutdown;
    conn->queued = wrapper_queued;
    conn->stop_conn = wrapper_stop;
    conn->start_conn = wrapper_start;
    
    return 0;

error:
    error(0, "Failed to create Smsc wrapper");
    conn->data = NULL;
    smscwrapper_destroy(wrap);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    return -1;
}



