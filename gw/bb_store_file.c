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
 * bb_store.c : bearerbox box SMS storage/retrieval module
 *
 * Kalle Marjola 2001 for project Kannel
 *
 * Updated Oct 2004
 *
 * New features:
 *  - uses dict to save messages, for faster retrieval
 *  - acks are no longer saved (to memory), they simply delete
 *    messages from dict
 *  - better choice when dump done; configurable frequency
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "bearerbox.h"
#include "sms.h"

static FILE *file = NULL;
static Octstr *filename = NULL;
static Octstr *newfile = NULL;
static Octstr *bakfile = NULL;
static Mutex *file_mutex = NULL;
static long cleanup_thread = -1;
static long dump_frequency = 0;

static Dict *sms_dict = NULL;

static int active = 1;
static time_t last_dict_mod = 0;
static List *loaded;


static void write_msg(Msg *msg)
{
    Octstr *pack;
    unsigned char buf[4];
    
    pack = store_msg_pack(msg);
    encode_network_long(buf, octstr_len(pack));
    octstr_insert_data(pack, 0, (char*)buf, 4);

    octstr_print(file, pack);
    fflush(file);

    octstr_destroy(pack);
}


static int read_msg(Msg **msg, Octstr *os, long *off)
{
    unsigned char buf[4];
    long i;
    Octstr *pack;

    gw_assert(*off >= 0);
    if (*off + 4 > octstr_len(os)) {
        error(0, "Packet too short while unpacking Msg.");
        return -1;
    }

    octstr_get_many_chars((char*)buf, os, *off, 4);
    i = decode_network_long(buf);
    *off  +=  4;
    
    pack = octstr_copy(os, *off, i);
    *off += octstr_len(pack);
    *msg = store_msg_unpack(pack);
    octstr_destroy(pack);
    
    if (!*msg)
        return -1;
    
    return 0;
}


static int open_file(Octstr *name)
{
    file = fopen(octstr_get_cstr(name), "w");
    if (file == NULL) {
        error(errno, "Failed to open '%s' for writing, cannot create store-file",
	      octstr_get_cstr(name));
        return -1;
    }
    return 0;
}


static int rename_store(void)
{
    if (rename(octstr_get_cstr(filename), octstr_get_cstr(bakfile)) == -1) {
        if (errno != ENOENT) {
            error(errno, "Failed to rename old store '%s' as '%s'",
            octstr_get_cstr(filename), octstr_get_cstr(bakfile));
            return -1;
        }
    }
    if (rename(octstr_get_cstr(newfile), octstr_get_cstr(filename)) == -1) {
        error(errno, "Failed to rename new store '%s' as '%s'",
              octstr_get_cstr(newfile), octstr_get_cstr(filename));
        return -1;
    }
    return 0;
}


static int do_dump(void)
{
    Octstr *key;
    Msg *msg;
    List *sms_list;
    long l;

    if (filename == NULL)
        return 0;

    /* create a new store-file and save all non-acknowledged
     * messages into it
     */
    if (open_file(newfile)==-1)
        return -1;

    sms_list = dict_keys(sms_dict);
    for (l=0; l < gwlist_len(sms_list); l++) {
        key = gwlist_get(sms_list, l);
        msg = dict_get(sms_dict, key);
        if (msg != NULL)
            write_msg(msg);
    }
    fflush(file);
    gwlist_destroy(sms_list, octstr_destroy_item);

    /* rename old storefile as .bak, and then new as regular file
     * without .new ending */

    return rename_store();
}


/*
 * thread to write current store to file now and then, to prevent
 * it from becoming far too big (slows startup)
 */
static void store_dumper(void *arg)
{
    time_t now;
    int busy = 0;

    while (active) {
        now = time(NULL);
        /*
         * write store to file up to each N. second, providing
         * that something happened or if we are constantly busy.
         */
        if (now - last_dict_mod > dump_frequency || busy) {
            store_dump();
            /* 
             * make sure that no new dump is done for a while unless
             * something happens. This moves the trigger in the future
             * and allows the if statement to pass if nothing happened
             * in the mean time while sleeping. The busy flag is needed
             * to garantee we do dump in case we are constantly busy
             * and hence the difference between now and last dict
             * operation is less then dump frequency, otherwise we
             * would never dump. This is for constant high load.
             */
            last_dict_mod = time(NULL) + 3600*24;
            busy = 0;
        } else {
            busy = (now - last_dict_mod) > 0;
        }
        gwthread_sleep(dump_frequency);
    }
    store_dump();
    if (file != NULL)
       fclose(file);
    octstr_destroy(filename);
    octstr_destroy(newfile);
    octstr_destroy(bakfile);
    mutex_destroy(file_mutex);

    dict_destroy(sms_dict);
    /* set all vars to NULL */
    filename = newfile = bakfile = NULL;
    file_mutex = NULL;
    sms_dict = NULL;
}


/*------------------------------------------------------*/

static Octstr *store_file_status(int status_type)
{
    char *frmt;
    Octstr *ret, *key;
    unsigned long l;
    struct tm tm;
    Msg *msg;
    List *keys;
    char id[UUID_STR_LEN + 1];

    ret = octstr_create("");

    /* set the type based header */
    if (status_type == BBSTATUS_HTML) {
        octstr_append_cstr(ret, "<table border=1>\n"
            "<tr><td>SMS ID</td><td>Type</td><td>Time</td><td>Sender</td><td>Receiver</td>"
            "<td>SMSC ID</td><td>BOX ID</td><td>UDH</td><td>Message</td>"
            "</tr>\n");
    } else if (status_type == BBSTATUS_TEXT) {
        octstr_append_cstr(ret, "[SMS ID] [Type] [Time] [Sender] [Receiver] [SMSC ID] [BOX ID] [UDH] [Message]\n");
    }
   
    /* if there is no store-file, then don't loop in sms_store */
    if (filename == NULL)
        goto finish;

    keys = dict_keys(sms_dict);

    for (l = 0; l < gwlist_len(keys); l++) {
        key = gwlist_get(keys, l);
        msg = dict_get(sms_dict, key);
        if (msg == NULL)
            continue;

        if (msg_type(msg) == sms) {

            if (status_type == BBSTATUS_HTML) {
                frmt = "<tr><td>%s</td><td>%s</td>"
                       "<td>%04d-%02d-%02d %02d:%02d:%02d</td>"
                       "<td>%s</td><td>%s</td><td>%s</td>"
                       "<td>%s</td><td>%s</td><td>%s</td></tr>\n";
            } else if (status_type == BBSTATUS_XML) {
                frmt = "<message>\n\t<id>%s</id>\n\t<type>%s</type>\n\t"
                       "<time>%04d-%02d-%02d %02d:%02d:%02d</time>\n\t"
                       "<sender>%s</sender>\n\t"
                       "<receiver>%s</receiver>\n\t<smsc-id>%s</smsc-id>\n\t"
                       "<box-id>%s</box-id>\n\t"
                       "<udh-data>%s</udh-data>\n\t<msg-data>%s</msg-data>\n\t"
                       "</message>\n";
            } else {
                frmt = "[%s] [%s] [%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s] [%s] [%s] [%s] [%s]\n";
            }

            /* transform the time value */
#if LOG_TIMESTAMP_LOCALTIME
            tm = gw_localtime(msg->sms.time);
#else
            tm = gw_gmtime(msg->sms.time);
#endif
            if (msg->sms.udhdata)
                octstr_binary_to_hex(msg->sms.udhdata, 1);
            if (msg->sms.msgdata &&
                (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2 ||
                (msg->sms.coding == DC_UNDEF && msg->sms.udhdata)))
                octstr_binary_to_hex(msg->sms.msgdata, 1);

            uuid_unparse(msg->sms.id, id);

            octstr_format_append(ret, frmt, id,
                (msg->sms.sms_type == mo ? "MO" :
                 msg->sms.sms_type == mt_push ? "MT-PUSH" :
                 msg->sms.sms_type == mt_reply ? "MT-REPLY" :
                 msg->sms.sms_type == report_mo ? "DLR-MO" :
                 msg->sms.sms_type == report_mt ? "DLR-MT" : ""),
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec,
                (msg->sms.sender ? octstr_get_cstr(msg->sms.sender) : ""),
                (msg->sms.receiver ? octstr_get_cstr(msg->sms.receiver) : ""),
                (msg->sms.smsc_id ? octstr_get_cstr(msg->sms.smsc_id) : ""),
                (msg->sms.boxc_id ? octstr_get_cstr(msg->sms.boxc_id) : ""),
                (msg->sms.udhdata ? octstr_get_cstr(msg->sms.udhdata) : ""),
                (msg->sms.msgdata ? octstr_get_cstr(msg->sms.msgdata) : ""));

            if (msg->sms.udhdata)
                octstr_hex_to_binary(msg->sms.udhdata);
            if (msg->sms.msgdata &&
                (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2 ||
                (msg->sms.coding == DC_UNDEF && msg->sms.udhdata)))
                octstr_hex_to_binary(msg->sms.msgdata);
        }
    }
    gwlist_destroy(keys, octstr_destroy_item);

finish:
    /* set the type based footer */
    if (status_type == BBSTATUS_HTML) {
        octstr_append_cstr(ret,"</table>");
    }

    return ret;
}


static long store_file_messages(void)
{
    return (sms_dict ? dict_key_count(sms_dict) : -1);
}


static int store_to_dict(Msg *msg)
{
    Msg *copy;
    Octstr *uuid_os;
    char id[UUID_STR_LEN + 1];
	
    /* always set msg id and timestamp */
    if (msg_type(msg) == sms && uuid_is_null(msg->sms.id))
        uuid_generate(msg->sms.id);

    if (msg_type(msg) == sms && msg->sms.time == MSG_PARAM_UNDEFINED)
        time(&msg->sms.time);

    if (msg_type(msg) == sms) {
        copy = msg_duplicate(msg);
        
        uuid_unparse(copy->sms.id, id);
        uuid_os = octstr_create(id);
        
        dict_put(sms_dict, uuid_os, copy);
        octstr_destroy(uuid_os);
        last_dict_mod = time(NULL);
    } else if (msg_type(msg) == ack) {
        uuid_unparse(msg->ack.id, id);
        uuid_os = octstr_create(id);
        copy = dict_remove(sms_dict, uuid_os);
        octstr_destroy(uuid_os);
        if (copy == NULL) {
            warning(0, "bb_store: get ACK of message not found "
        	       "from store, strange?");
        } else {
            msg_destroy(copy);
            last_dict_mod = time(NULL);
        }
    } else
        return -1;
    return 0;
}
    
static int store_file_save(Msg *msg)
{
    if (filename == NULL)
        return 0;

    /* block here until store not loaded */
    gwlist_consume(loaded);

    /* lock file_mutex in order to have dict and file in sync */
    mutex_lock(file_mutex);
    if (store_to_dict(msg) == -1) {
        mutex_unlock(file_mutex);
        return -1;
    }
    
    /* write to file, too */
    write_msg(msg);
    fflush(file);
    mutex_unlock(file_mutex);

    return 0;
}


static int store_file_save_ack(Msg *msg, ack_status_t status)
{
    Msg *mack;
    int ret;

    /* only sms are handled */
    if (!msg || msg_type(msg) != sms)
        return -1;

    if (filename == NULL)
        return 0;

    mack = msg_create(ack);
    if (!mack)
        return -1;

    mack->ack.time = msg->sms.time;
    uuid_copy(mack->ack.id, msg->sms.id);
    mack->ack.nack = status;

    ret = store_save(mack);
    msg_destroy(mack);

    return ret;
}


static int store_file_load(void(*receive_msg)(Msg*))
{
    List *keys;
    Octstr *store_file, *key;
    Msg *msg;
    int retval, msgs;
    long end, pos;

    if (filename == NULL)
        return 0;

    mutex_lock(file_mutex);
    if (file != NULL) {
        fclose(file);
        file = NULL;
    }

    store_file = octstr_read_file(octstr_get_cstr(filename));
    if (store_file != NULL)
        info(0, "Loading store file `%s'", octstr_get_cstr(filename));
    else {
        store_file = octstr_read_file(octstr_get_cstr(newfile));
        if (store_file != NULL)
            info(0, "Loading store file `%s'", octstr_get_cstr(newfile));
        else {
            store_file = octstr_read_file(octstr_get_cstr(bakfile));
            if (store_file != NULL)
        	       info(0, "Loading store file `%s'", octstr_get_cstr(bakfile));
            else {
                info(0, "Cannot open any store file, starting a new one");
                retval = open_file(filename);
                goto end;
            }
        }
    }

    info(0, "Store-file size %ld, starting to unpack%s", octstr_len(store_file),
        octstr_len(store_file) > 10000 ? " (may take awhile)" : "");


    pos = 0;
    msgs = 0;
    end = octstr_len(store_file);
    
    while (pos < end) {
        if (read_msg(&msg, store_file, &pos) == -1) {
            error(0, "Garbage at store-file, skipped.");
            continue;
        }
        if (msg_type(msg) == sms) {
            store_to_dict(msg);
            msgs++;
        } else if (msg_type(msg) == ack) {
            store_to_dict(msg);
        } else {
            warning(0, "Strange message in store-file, discarded, "
                "dump follows:");
            msg_dump(msg, 0);
        }
        msg_destroy(msg);
    }
    octstr_destroy(store_file);

    info(0, "Retrieved %d messages, non-acknowledged messages: %ld",
        msgs, dict_key_count(sms_dict));

    /* now create a new sms_store out of messages left */

    keys = dict_keys(sms_dict);
    while ((key = gwlist_extract_first(keys)) != NULL) {
        msg = dict_remove(sms_dict, key);
        if (store_to_dict(msg) != -1) {
            receive_msg(msg);
        } else {
            error(0, "Found unknown message type in store file.");
            msg_dump(msg, 0);
            msg_destroy(msg);
        }
        octstr_destroy(key);
    }
    gwlist_destroy(keys, octstr_destroy_item);

    /* Finally, generate new store file out of left messages */
    retval = do_dump();

end:
    mutex_unlock(file_mutex);

    /* allow using of store */
    gwlist_remove_producer(loaded);

    /* start dumper thread */
    if ((cleanup_thread = gwthread_create(store_dumper, NULL))==-1)
        panic(0, "Failed to create a cleanup thread!");

    return retval;
}


static int store_file_dump(void)
{
    int retval;

    debug("bb.store", 0, "Dumping %ld messages to store",
	  dict_key_count(sms_dict));
    mutex_lock(file_mutex);
    if (file != NULL) {
        fclose(file);
        file = NULL;
    }
    retval = do_dump();
    mutex_unlock(file_mutex);

    return retval;
}


static void store_file_shutdown(void)
{
    if (filename == NULL)
        return;

    active = 0;
    gwthread_wakeup(cleanup_thread);
    /* wait for cleanup thread */
    if (cleanup_thread != -1)
        gwthread_join(cleanup_thread);

    gwlist_destroy(loaded, NULL);
}


int store_file_init(const Octstr *fname, long dump_freq)
{
    /* Initialize function pointers */
    store_messages = store_file_messages;
    store_save = store_file_save;
    store_save_ack = store_file_save_ack;
    store_load = store_file_load;
    store_dump = store_file_dump;
    store_shutdown = store_file_shutdown;
    store_status = store_file_status;

    if (fname == NULL)
        return 0; /* we are done */

    if (octstr_len(fname) > (FILENAME_MAX-5))
        panic(0, "Store file filename too long: `%s', failed to init.",
	      octstr_get_cstr(fname));

    filename = octstr_duplicate(fname);
    newfile = octstr_format("%s.new", octstr_get_cstr(filename));
    bakfile = octstr_format("%s.bak", octstr_get_cstr(filename));

    sms_dict = dict_create(1024, msg_destroy_item);

    if (dump_freq > 0)
        dump_frequency = dump_freq;
    else
        dump_frequency = BB_STORE_DEFAULT_DUMP_FREQ;

    file_mutex = mutex_create();
    active = 1;

    loaded = gwlist_create();
    gwlist_add_producer(loaded);

    return 0;
}
