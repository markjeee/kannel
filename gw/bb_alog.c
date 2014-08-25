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
 * gw/bb_alog.c -- encapsulate custom access log logic and escape code parsing
 *
 * Stipe Tolj <stolj at kannel dot org>
 * Alexander Malysh <amalysh at kannel dot org>
 */

#include "gwlib/gwlib.h"
#include "msg.h"
#include "sms.h"
#include "bearerbox.h"
#include "smscconn.h"

static Octstr *custom_log_format = NULL;


/********************************************************************
 * Routine to escape the values into the custom log format.
 *
 * The following escape code values are acceptable within the 
 * 'access-log-format' config directive of bearerbox:
 *
 *   %l - log message
 *   %i - smsc-id
 *   %n - service-name (for MO) or sendsms-user (for MT)
 *   %A - account
 *   %B - billing identifier/information
 *   %p - sender (from) 
 *   %P - receiver (to)
 *   %m - message class (mclass)
 *   %c - coding
 *   %M - message waiting indicator (mwi)
 *   %C - compress indicator
 *   %d - dlr_mask
 *   %R - dlr_url
 *   %D - meta-data
 *   %a - the original SMS message, spaces squeezed
 *   %u - UDH data (in escaped form)
 *   %U - length of UDH data
 *   %k - the keyword in the SMS request (the first word in the SMS message) 
 *   %s - next word from the SMS message, starting with the second one
 *   %S - same as %s, but '*' is converted to '~' 
 *   %r - words not yet used by %s
 *   %b - the original SMS message
 *   %L - length of SMS message
 *   %t - the time of the message, formatted as "YYYY-MM-DD HH:MM:SS"
 *   %T - the time of the message, in UNIX epoch timestamp format
 *   %I - the internal message ID
 *   %F - the foreign (smsc-provided) message ID
 *   %x - smsbox-id, identifying the smsbox connection
 *
 * Most escape codes should be compatible with escape codes used in
 * sms-service groups.
 *
 * The default access-log-format would look like this (if access-log-clean is true):
 *   "%t %l [SMSC:%i] [SVC:%n] [ACT:%A] [BINF:%B] [FID:%F] [META:%D] [from:%p] [to:%P] \
 *    [flags:%m:%c:%M:%C:%d] [msg:%L:%b] [udh:%U:%u]"
 */
  
static Octstr *get_pattern(SMSCConn *conn, Msg *msg, const char *message)
{
    int nextarg, j;
    struct tm tm;
    int num_words;
    List *word_list;
    Octstr *result;
    const char *pattern;
    Octstr *temp, *text, *udh;
    size_t n;
    long i;
 
    text = msg->sms.msgdata ? octstr_duplicate(msg->sms.msgdata) : octstr_create("");
    udh = msg->sms.udhdata ? octstr_duplicate(msg->sms.udhdata) : octstr_create("");
    if ((msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2))
        octstr_binary_to_hex(text, 1);
    else
        octstr_convert_printable(text);
    octstr_binary_to_hex(udh, 1);

    if (octstr_len(text)) {
        word_list = octstr_split_words(text);
        num_words = gwlist_len(word_list);
    } else {
    	word_list = gwlist_create();
        num_words = 0;
    }

    result = octstr_create("");
    pattern = octstr_get_cstr(custom_log_format);

    nextarg = 1;

    while (*pattern != '\0') {
        n = strcspn(pattern, "%");
        octstr_append_data(result, pattern, n);
        pattern += n;
        gw_assert(*pattern == '%' || *pattern == '\0');
        if (*pattern == '\0')
            break;

        pattern++;
        
        switch (*pattern) {
            case 'k':
                if (num_words <= 0)
                    break;
                octstr_append(result, gwlist_get(word_list, 0));
                break;

            case 's':
                if (nextarg >= num_words)
                    break;
                octstr_append(result, gwlist_get(word_list, nextarg));
                ++nextarg;
                break;

            case 'S':
                if (nextarg >= num_words)
                    break;
                temp = gwlist_get(word_list, nextarg);
                for (i = 0; i < octstr_len(temp); ++i) {
                    if (octstr_get_char(temp, i) == '*')
                        octstr_append_char(result, '~');
                    else
                        octstr_append_char(result, octstr_get_char(temp, i));
                }
                ++nextarg;
                break;

            case 'r':
                for (j = nextarg; j < num_words; ++j) {
                    if (j != nextarg)
                        octstr_append_char(result, '+');
                    octstr_append(result, gwlist_get(word_list, j));
                }
                break;
    
            case 'l':
                if (message)
                    octstr_append_cstr(result, message);
                break;

            case 'P':
                if (msg->sms.receiver)
                    octstr_append(result, msg->sms.receiver);
                break;

            case 'p':
                if (msg->sms.sender)
                    octstr_append(result, msg->sms.sender);
                break;

            case 'a':
                for (j = 0; j < num_words; ++j) {
                    if (j > 0)
                        octstr_append_char(result, ' ');
                    octstr_append(result, gwlist_get(word_list, j));
                }
                break;

            case 'b':
                if (text)
                    octstr_append(result, text);
                break;

            case 'L':
                octstr_append_decimal(result, octstr_len(msg->sms.msgdata));
                break;

            case 't':
                tm = gw_gmtime(msg->sms.time);
                octstr_format_append(result, "%04d-%02d-%02d %02d:%02d:%02d",
                        tm.tm_year + 1900,
                        tm.tm_mon + 1,
                        tm.tm_mday,
                        tm.tm_hour,
                        tm.tm_min,
                        tm.tm_sec);
                break;

            case 'T':
                if (msg->sms.time != MSG_PARAM_UNDEFINED)
                    octstr_format_append(result, "%ld", msg->sms.time);
                break;

            case 'i':
                if (conn && smscconn_id(conn))
                    octstr_append(result, smscconn_id(conn));
                else if (conn && smscconn_name(conn))
                    octstr_append(result, smscconn_name(conn));
                else if (msg->sms.smsc_id)
                    octstr_append(result, msg->sms.smsc_id);
                break;

            case 'I':
                if (!uuid_is_null(msg->sms.id)) {
                    char id[UUID_STR_LEN + 1];
                    uuid_unparse(msg->sms.id, id);
                    octstr_append_cstr(result, id);
                }
                break;

            case 'n':
                if (msg->sms.service != NULL)
                    octstr_append(result, msg->sms.service);
                break;

            case 'd':
                octstr_append_decimal(result, msg->sms.dlr_mask);
                break;

            case 'R':
                if (msg->sms.dlr_url != NULL)
                    octstr_append(result, msg->sms.dlr_url);
                break;

            case 'D': /* meta_data */
                if (msg->sms.meta_data != NULL)
                    octstr_append(result, msg->sms.meta_data);
                break;

            case 'c':
                octstr_append_decimal(result, msg->sms.coding);
                break;

            case 'm':
                octstr_append_decimal(result, msg->sms.mclass);
                break;

            case 'C':
                octstr_append_decimal(result, msg->sms.compress);
                break;

            case 'M':
                octstr_append_decimal(result, msg->sms.mwi);
                break;

            case 'u':
                if (octstr_len(udh)) {
                    octstr_append(result, udh);
                }
                break;

            case 'U':
                octstr_append_decimal(result, octstr_len(msg->sms.udhdata));
                break;

            case 'B':  /* billing identifier/information */
                if (octstr_len(msg->sms.binfo)) {
                    octstr_append(result, msg->sms.binfo);
                }
                break;

            case 'A':  /* account */
                if (octstr_len(msg->sms.account)) {
                    octstr_append(result, msg->sms.account);
                }
                break;

            case 'F': /* the foreign (smsc-provided) message ID */
                if (msg->sms.foreign_id != NULL)
                    octstr_append(result, msg->sms.foreign_id);
                break;

            case 'x': /* the boxc_id, hence the smsbox-id of the message */
                if (msg->sms.boxc_id != NULL)
                    octstr_append(result, msg->sms.boxc_id);
                break;

                /* XXX add more here if needed */

            case '%':
                octstr_format_append(result, "%%");
                break;

            default:
                warning(0, "Unknown escape code (%%%c) within custom-log-format, skipping!", *pattern);
                octstr_format_append(result, "%%%c", *pattern);
                break;
        } /* switch(...) */
    
        pattern++;
    } /* for ... */

    gwlist_destroy(word_list, octstr_destroy_item);
    octstr_destroy(text);
    octstr_destroy(udh);

    return result;
}


/********************************************************************
 * 
 */

void bb_alog_init(const Octstr *format)
{
    gw_assert(format != NULL);

    custom_log_format = octstr_duplicate(format);
}


void bb_alog_shutdown(void)
{
    octstr_destroy(custom_log_format);
    custom_log_format = NULL;
}


void bb_alog_sms(SMSCConn *conn, Msg *msg, const char *message)
{
    Octstr *text = NULL;
    
    gw_assert(msg_type(msg) == sms);

    /* if we don't have any custom log, then use our "default" one */
    
    if (custom_log_format == NULL) {
        Octstr *udh;
        const Octstr *cid;

        text = msg->sms.msgdata ? octstr_duplicate(msg->sms.msgdata) : octstr_create("");
        udh = msg->sms.udhdata ? octstr_duplicate(msg->sms.udhdata) : octstr_create("");

        if (conn && smscconn_id(conn))
            cid = smscconn_id(conn);
        else if (conn && smscconn_name(conn))
            cid = smscconn_name(conn);
        else if (msg->sms.smsc_id)
            cid = msg->sms.smsc_id;
        else
            cid = octstr_imm("");

        if ((msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2))
            octstr_binary_to_hex(text, 1);
        else
            octstr_convert_printable(text);
        octstr_binary_to_hex(udh, 1);

        alog("%s [SMSC:%s] [SVC:%s] [ACT:%s] [BINF:%s] [FID:%s] [META:%s] [from:%s] [to:%s] [flags:%ld:%ld:%ld:%ld:%ld] "
             "[msg:%ld:%s] [udh:%ld:%s]",
             message,
             octstr_get_cstr(cid),
             msg->sms.service ? octstr_get_cstr(msg->sms.service) : "",
             msg->sms.account ? octstr_get_cstr(msg->sms.account) : "",
             msg->sms.binfo ? octstr_get_cstr(msg->sms.binfo) : "",
             msg->sms.foreign_id ? octstr_get_cstr(msg->sms.foreign_id) : "",
             msg->sms.meta_data ? octstr_get_cstr(msg->sms.meta_data) : "",
             msg->sms.sender ? octstr_get_cstr(msg->sms.sender) : "",
             msg->sms.receiver ? octstr_get_cstr(msg->sms.receiver) : "",
             msg->sms.mclass, msg->sms.coding, msg->sms.mwi, msg->sms.compress,
             msg->sms.dlr_mask, 
             octstr_len(msg->sms.msgdata), octstr_get_cstr(text),
             octstr_len(msg->sms.udhdata), octstr_get_cstr(udh)
        );

        octstr_destroy(udh);
    } else {
        text = get_pattern(conn, msg, message);
        alog("%s", octstr_get_cstr(text));
    }

    octstr_destroy(text);
}


