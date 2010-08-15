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
 * bb_store_spool.c - bearerbox box SMS storage/retrieval module using spool directory
 *
 * Author: Alexander Malysh, 2006
 */

#include "gw-config.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "sms.h"
#include "bearerbox.h"
#include "bb_store.h"


/* how much subdirs allowed ? */
#define MAX_DIRS 100

static Octstr *spool;
static Counter *counter;
static List *loaded;


static int store_spool_dump()
{
    /* nothing todo */
    return 0;
}


static long store_spool_messages()
{
    return counter ? counter_value(counter) : -1;
}


static int for_each_file(const Octstr *dir_s, int ignore_err, void(*cb)(const Octstr*, void*), void *data)
{
    DIR *dir;
    struct dirent *ent;
    struct stat stat;
    int ret = 0;

    if ((dir = opendir(octstr_get_cstr(dir_s))) == NULL) {
        error(errno, "Could not open directory `%s'", octstr_get_cstr(dir_s));
        return -1;
    }
    while((ent = readdir(dir)) != NULL) {
        Octstr *filename;
        if (*(ent->d_name) == '.') /* skip hidden files */
            continue;
        filename = octstr_format("%S/%s", dir_s, ent->d_name);
        if (lstat(octstr_get_cstr(filename), &stat) == -1) {
            if (!ignore_err)
                error(errno, "Could not get stat for `%s'", octstr_get_cstr(filename));
            ret = -1;
        } else if (S_ISDIR(stat.st_mode) && for_each_file(filename, ignore_err, cb, data) == -1) {
            ret = -1;
        } else if (S_ISREG(stat.st_mode) && cb != NULL)
            cb(filename, data);
        octstr_destroy(filename);
        if (ret == -1 && ignore_err)
            ret = 0;
        else if (ret == -1)
            break;
    }
    closedir(dir);

    return ret;
}


struct status {
    const char *format;
    Octstr *status;
};


static void status_cb(const Octstr *filename, void *d)
{
    struct status *data = d;
    struct tm tm;
    char id[UUID_STR_LEN + 1];
    Octstr *msg_s;
    Msg *msg;

    msg_s = octstr_read_file(octstr_get_cstr(filename));
    msg = store_msg_unpack(msg_s);
    octstr_destroy(msg_s);
    if (msg == NULL)
        return;

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

    octstr_format_append(data->status, data->format,
        id,
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

    msg_destroy(msg);
}


static Octstr *store_spool_status(int status_type)
{
    Octstr *ret = octstr_create("");
    const char *format;
    struct status data;

    /* check if we are active */
    if (spool == NULL)
        return ret;

    /* set the type based header */
    if (status_type == BBSTATUS_HTML) {
        octstr_append_cstr(ret, "<table border=1>\n"
            "<tr><td>SMS ID</td><td>Type</td><td>Time</td><td>Sender</td><td>Receiver</td>"
            "<td>SMSC ID</td><td>BOX ID</td><td>UDH</td><td>Message</td>"
            "</tr>\n");

        format = "<tr><td>%s</td><td>%s</td>"
                "<td>%04d-%02d-%02d %02d:%02d:%02d</td>"
                "<td>%s</td><td>%s</td><td>%s</td>"
                "<td>%s</td><td>%s</td><td>%s</td></tr>\n";
    } else if (status_type == BBSTATUS_XML) {
        format = "<message>\n\t<id>%s</id>\n\t<type>%s</type>\n\t"
                "<time>%04d-%02d-%02d %02d:%02d:%02d</time>\n\t"
                "<sender>%s</sender>\n\t"
                "<receiver>%s</receiver>\n\t<smsc-id>%s</smsc-id>\n\t"
                "<box-id>%s</box-id>\n\t"
                "<udh-data>%s</udh-data>\n\t<msg-data>%s</msg-data>\n\t"
                "</message>\n";
    } else {
        octstr_append_cstr(ret, "[SMS ID] [Type] [Time] [Sender] [Receiver] [SMSC ID] [BOX ID] [UDH] [Message]\n");
        format = "[%s] [%s] [%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s] [%s] [%s] [%s] [%s]\n";
    }

    data.format = format;
    data.status = ret;
    /* ignore error because files may disappear */
    for_each_file(spool, 1, status_cb, &data);

    /* set the type based footer */
    if (status_type == BBSTATUS_HTML) {
        octstr_append_cstr(ret,"</table>");
    }

    return ret;
}


static void dispatch(const Octstr *filename, void *data)
{
    Octstr *msg_s;
    Msg *msg;
    void(*receive_msg)(Msg*) = data;

    /* debug("", 0, "dispatch(%s,...) called", octstr_get_cstr(filename)); */

    msg_s = octstr_read_file(octstr_get_cstr(filename));
    if (msg_s == NULL)
        return;
    msg = store_msg_unpack(msg_s);
    octstr_destroy(msg_s);
    if (msg != NULL) {
        receive_msg(msg);
        counter_increase(counter);
    } else {
        error(0, "Could not unpack message `%s'", octstr_get_cstr(filename));
    }
}


static int store_spool_load(void(*receive_msg)(Msg*))
{
    int rc;

    /* check if we are active */
    if (spool == NULL)
        return 0;
        
    /* sanity check */
    if (receive_msg == NULL)
        return -1;

    rc = for_each_file(spool, 0, dispatch, receive_msg);

    info(0, "Loaded %ld messages from store.", counter_value(counter));

    /* allow using of storage */
    gwlist_remove_producer(loaded);

    return rc;
}


static int store_spool_save(Msg *msg)
{
    char id[UUID_STR_LEN + 1];
    Octstr *id_s;

    /* always set msg id and timestamp */
    if (msg_type(msg) == sms && uuid_is_null(msg->sms.id))
        uuid_generate(msg->sms.id);

    if (msg_type(msg) == sms && msg->sms.time == MSG_PARAM_UNDEFINED)
        time(&msg->sms.time);

    if (spool == NULL)
        return 0;

    /* blocke here if store still not loaded */
    gwlist_consume(loaded);

    switch(msg_type(msg)) {
        case sms:
        {
            Octstr *os = store_msg_pack(msg);
            Octstr *filename, *dir;
            int fd;
            size_t wrc;

            if (os == NULL) {
                error(0, "Could not pack message.");
                return -1;
            }
            uuid_unparse(msg->sms.id, id);
            id_s = octstr_create(id);
            dir = octstr_format("%S/%ld", spool, octstr_hash_key(id_s) % MAX_DIRS);
            octstr_destroy(id_s);
            if (mkdir(octstr_get_cstr(dir), S_IRUSR|S_IWUSR|S_IXUSR) == -1 && errno != EEXIST) {
                error(errno, "Could not create directory `%s'.", octstr_get_cstr(dir));
                octstr_destroy(dir);
                octstr_destroy(os);
                return -1;
            }
            filename = octstr_format("%S/%s", dir, id);
            octstr_destroy(dir);
            if ((fd = open(octstr_get_cstr(filename), O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR)) == -1) {
                error(errno, "Could not open file `%s'.", octstr_get_cstr(filename));
                octstr_destroy(filename);
                octstr_destroy(os);
                return -1;
            }
            for (wrc = 0; wrc < octstr_len(os); ) {
                size_t rc = write(fd, octstr_get_cstr(os) + wrc, octstr_len(os) - wrc);
                if (rc == -1) {
                    /* remove file */
                    error(errno, "Could not write message to `%s'.", octstr_get_cstr(filename));
                    close(fd);
                    if (unlink(octstr_get_cstr(filename)) == -1)
                        error(errno, "Oops, Could not remove failed file `%s'.", octstr_get_cstr(filename));
                    octstr_destroy(os);
                    octstr_destroy(filename);
                    return -1;
                }
                wrc += rc;
            }
            close(fd);
            counter_increase(counter);
            octstr_destroy(filename);
            octstr_destroy(os);
            break;
        }
        case ack:
        {
            Octstr *filename;
            uuid_unparse(msg->ack.id, id);
            id_s = octstr_create(id);
            filename = octstr_format("%S/%ld/%s", spool, octstr_hash_key(id_s) % MAX_DIRS, id);
            octstr_destroy(id_s);
            if (unlink(octstr_get_cstr(filename)) == -1) {
                error(errno, "Could not unlink file `%s'.", octstr_get_cstr(filename));
                octstr_destroy(filename);
                return -1;
            }
            counter_decrease(counter);
            octstr_destroy(filename);
            break;
        }
        default:
            return -1;
    }

    return 0;
}


static int store_spool_save_ack(Msg *msg, ack_status_t status)
{
    int ret;
    Msg *nack = msg_create(ack);

    nack->ack.nack = status;
    uuid_copy(nack->ack.id, msg->sms.id);
    nack->ack.time = msg->sms.time;
    ret = store_spool_save(nack);
    msg_destroy(nack);

    return ret;
}


static void store_spool_shutdown()
{
    if (spool == NULL)
        return;
        
    counter_destroy(counter);
    octstr_destroy(spool);
    gwlist_destroy(loaded, NULL);
}


int store_spool_init(const Octstr *store_dir)
{
    DIR *dir;

    store_messages = store_spool_messages;
    store_save = store_spool_save;
    store_save_ack = store_spool_save_ack;
    store_load = store_spool_load;
    store_dump = store_spool_dump;
    store_shutdown = store_spool_shutdown;
    store_status = store_spool_status;

    if (store_dir == NULL)
        return 0;

    /* check if we can open directory */
    if ((dir = opendir(octstr_get_cstr(store_dir))) == NULL) {
        error(errno, "Could not open directory `%s'", octstr_get_cstr(store_dir));
        return -1;
    }
    closedir(dir);

    loaded = gwlist_create();
    gwlist_add_producer(loaded);
    spool = octstr_duplicate(store_dir);
    counter = counter_create();

    return 0;
}

