/* ====================================================================
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2016 Kannel Group  
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
 * bb_store.c : wrapper for the different bearerbox box SMS storage/retrieval module
 *
 * Author: Alexander Malysh, 2006
 */

#include "gw-config.h"

#include "gwlib/gwlib.h"
#include "msg.h"
#include "bearerbox.h"
#include "bb_store.h"


long (*store_messages)(void);
int (*store_save)(Msg *msg);
int (*store_save_ack)(Msg *msg, ack_status_t status);
int (*store_load)(void(*receive_msg)(Msg*));
int (*store_dump)(void);
void (*store_shutdown)(void);
Octstr* (*store_msg_pack)(Msg *msg);
Msg* (*store_msg_unpack)(Octstr *os);
void (*store_for_each_message)(void(*callback_fn)(Msg* msg, void *data), void *data);


int store_init(Cfg *cfg, const Octstr *type, const Octstr *fname, long dump_freq,
               void *pack_func, void *unpack_func)
{
    int ret;
    
    store_msg_pack = pack_func;
    store_msg_unpack = unpack_func;

    if (type == NULL || octstr_str_compare(type, "file") == 0) {
        ret = store_file_init(fname, dump_freq);
    } else if (octstr_str_compare(type, "spool") == 0) {
        ret = store_spool_init(fname);
#ifdef HAVE_REDIS
    } else if (octstr_str_compare(type, "redis") == 0) {
        ret = store_redis_init(cfg);
#endif
    } else {
        error(0, "Unknown 'store-type' defined.");
        ret = -1;
    }

    return ret;
}

struct status {
    const char *format;
    Octstr *status;
};

static void status_cb(Msg *msg, void *d)
{
    struct status *data = d;
    struct tm tm;
    char id[UUID_STR_LEN + 1];

    if (msg == NULL)
        return;

    /* transform the time value */
#if LOG_TIMESTAMP_LOCALTIME
    tm = gw_localtime(msg->sms.time);
#else
    tm = gw_gmtime(msg->sms.time);
#endif

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
        msg->sms.mclass, msg->sms.coding, msg->sms.mwi, msg->sms.compress,
        msg->sms.dlr_mask,
        (msg->sms.udhdata ? msg->sms.udhdata : octstr_imm("")),
        (msg->sms.msgdata ? msg->sms.msgdata : octstr_imm("")));
}

Octstr *store_status(int status_type)
{
    Octstr *ret = octstr_create("");
    const char *format;
    struct status data;

    /* check if we are active */
    if (store_for_each_message == NULL)
        return ret;

    /* set the type based header */
    if (status_type == BBSTATUS_HTML) {
        octstr_append_cstr(ret, "<table border=1>\n"
            "<tr><td>SMS ID</td><td>Type</td><td>Time</td><td>Sender</td><td>Receiver</td>"
            "<td>SMSC ID</td><td>BOX ID</td><td>Flags</td>"
            "<td>UDH</td><td>Message</td>"
            "</tr>\n");

        format = "<tr><td>%s</td><td>%s</td>"
                "<td>%04d-%02d-%02d %02d:%02d:%02d</td>"
                "<td>%s</td><td>%s</td><td>%s</td>"
                "<td>%s</td><td>%ld:%ld:%ld:%ld:%ld</td><td>%E</td><td>%E</td></tr>\n";
    } else if (status_type == BBSTATUS_XML) {
        format = "\t<message>\n\t<id>%s</id>\n\t<type>%s</type>\n\t"
                "<time>%04d-%02d-%02d %02d:%02d:%02d</time>\n\t"
                "<sender>%s</sender>\n\t"
                "<receiver>%s</receiver>\n\t<smsc-id>%s</smsc-id>\n\t"
                "<box-id>%s</box-id>\n\t"
                "<flags>%ld:%ld:%ld:%ld:%ld</flags>\n\t"
                "<udh-data>%E</udh-data>\n\t<msg-data>%E</msg-data>\n\t"
                "</message>\n";
    } else {
        octstr_append_cstr(ret, "[SMS ID] [Type] [Time] [Sender] [Receiver] [SMSC ID] [BOX ID] [Flags] [UDH] [Message]\n");
        format = "[%s] [%s] [%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s] [%s] [%s] [%ld:%ld:%ld:%ld:%ld] [%E] [%E]\n";
    }

    data.format = format;
    data.status = ret;

    store_for_each_message(status_cb, &data);

    /* set the type based footer */
    if (status_type == BBSTATUS_HTML) {
        octstr_append_cstr(ret,"</table>");
    }

    return ret;
}
