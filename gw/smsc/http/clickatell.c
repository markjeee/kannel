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

/*----------------------------------------------------------------
 * Clickatell - http://api.clickatell.com/
 *
 * Rene Kluwen <rene.kluwen@chimit.nl>
 * Stipe Tolj <st@tolj.org>, <stolj@kannel.org>
 */

#include "gwlib/gwlib.h"


/* MT related function */
static int clickatell_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url, *os;
    char id[UUID_STR_LEN + 1];
    List *headers;

    /* form the basic URL */
    url = octstr_format("%S/sendmsg?to=%E&from=%E&api_id=%E&user=%E&password=%E",
        conndata->send_url, sms->sms.receiver, sms->sms.sender,
        conndata->system_id, conndata->username, conndata->password);

    /* append MD5 digest as msg ID from our UUID */
    uuid_unparse(sms->sms.id, id);
    os = octstr_create(id);
    octstr_replace(os, octstr_imm("-"), octstr_imm(""));
    octstr_format_append(url, "&cliMsgId=%E", os);
    octstr_destroy(os);

    /* add UDH header */
    if(octstr_len(sms->sms.udhdata)) {
    octstr_format_append(url, "&udh=%H", sms->sms.udhdata);
    }

    if(sms->sms.coding == DC_8BIT) {
        octstr_format_append(url, "&data=%H&mclass=%d", sms->sms.msgdata, sms->sms.mclass);
    } else if(sms->sms.coding == DC_UCS2) {
        octstr_format_append(url, "&unicode=1&text=%H", sms->sms.msgdata);
    } else {
        octstr_format_append(url, "&text=%E", sms->sms.msgdata);
        if (conndata->alt_charset) {
            octstr_format_append(url, "&charset=%E", conndata->alt_charset);
        } else {
            octstr_append_cstr(url, "&charset=UTF-8");
        }
    }

    if (DLR_IS_ENABLED_DEVICE(sms->sms.dlr_mask))
    octstr_format_append(url, "&callback=3&deliv_ack=1");

    headers = http_create_empty_headers();
    debug("smsc.http.clickatell", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));

    /*
     * Clickatell requires optionally an SSL-enabled HTTP client call, this is handled
     * transparently by the Kannel HTTP layer module.
     */
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);

    return 0;
}


/*
 * Parse a line in the format: ID: XXXXXXXXXXXXXXXXXX
 * and return a Dict with the 'ID' as key and the value as value,
 * otherwise return NULL if a parsing error occures.
 */
static Dict *clickatell_parse_body(Octstr *body)
{
    Dict *param = NULL;
    List *words = NULL;
    long len;
    Octstr *word, *value;

    words = octstr_split_words(body);
    if ((len = gwlist_len(words)) > 1) {
        word = gwlist_extract_first(words);
        if (octstr_compare(word, octstr_imm("ID:")) == 0) {
            value = gwlist_extract_first(words);
            param = dict_create(4, (void(*)(void *)) octstr_destroy);
            dict_put(param, octstr_imm("ID"), value);
        } else if (octstr_compare(word, octstr_imm("ERR:")) == 0) {
            value = gwlist_extract_first(words);
            param = dict_create(4, (void(*)(void *)) octstr_destroy);
            dict_put(param, octstr_imm("ERR"), value);
        }
        octstr_destroy(word);
    }
    gwlist_destroy(words, (void(*)(void *)) octstr_destroy);

    return param;
}


static void clickatell_parse_reply(SMSCConn *conn, Msg *msg, int status,
                               List *headers, Octstr *body)
{
    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        Dict *param;
        Octstr *msgid;

        if ((param = clickatell_parse_body(body)) != NULL &&
            (msgid = dict_get(param, octstr_imm("ID"))) != NULL &&
            msgid != NULL) {

            /* SMSC ACK.. now we have the message id. */
            if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask))
                dlr_add(conn->id, msgid, msg, 0);

            bb_smscconn_sent(conn, msg, NULL);

        } else {
            error(0, "HTTP[%s]: Message was malformed or error was returned. SMSC response `%s'.",
                  octstr_get_cstr(conn->id), octstr_get_cstr(body));
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
        }
        dict_destroy(param);

    } else {
        error(0, "HTTP[%s]: Message was rejected. SMSC reponse `%s'.",
              octstr_get_cstr(conn->id), octstr_get_cstr(body));
        bb_smscconn_send_failed(conn, msg,
                SMSCCONN_FAILED_REJECTED, octstr_duplicate(body));
    }
}

/* MO related function */
static void clickatell_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    List *reply_headers;
    int ret;
    Octstr *apimsgid, *status, *timestamp, *retmsg, *dest, *charge;
    Octstr *api_id, *from, *to, *text, *charset, *udh;
    int httpstatus = HTTP_UNAUTHORIZED, dlrstat;
    Msg *dlrmsg, *momsg;
    struct tm tm;

    /* dlr parameters */
    apimsgid = http_cgi_variable(cgivars, "apiMsgId");
    status = http_cgi_variable(cgivars, "status");
    /* timestamp is for both DLR & MO */
    timestamp = http_cgi_variable(cgivars, "timestamp");
    dest = http_cgi_variable(cgivars, "to");
    charge = http_cgi_variable(cgivars, "charge");
    /* MO parameters */
    api_id = http_cgi_variable(cgivars, "api_id");
    from = http_cgi_variable(cgivars, "from");
    to = http_cgi_variable(cgivars, "to");
    text = http_cgi_variable(cgivars, "text");
    charset = http_cgi_variable(cgivars, "charset");
    udh = http_cgi_variable(cgivars, "udh");

    debug("smsc.http.clickatell", 0, "HTTP[%s]: Received a request",
          octstr_get_cstr(conn->id));

    if (api_id != NULL && from != NULL && to != NULL && timestamp != NULL && text != NULL && charset != NULL && udh != NULL) {
    /* we received an MO message */
    debug("smsc.http.clickatell", 0, "HTTP[%s]: Received MO message from %s: <%s>",
            octstr_get_cstr(conn->id), octstr_get_cstr(from), octstr_get_cstr(text));
    momsg = msg_create(sms);
    momsg->sms.sms_type = mo;
    momsg->sms.sender = octstr_duplicate(from);
    momsg->sms.receiver = octstr_duplicate(to);
    momsg->sms.msgdata = octstr_duplicate(text);
    momsg->sms.charset = octstr_duplicate(charset);
    momsg->sms.service = octstr_duplicate(api_id);
    momsg->sms.binfo = octstr_duplicate(api_id);
        momsg->sms.smsc_id = octstr_duplicate(conn->id);
    if (octstr_len(udh) > 0) {
        momsg->sms.udhdata = octstr_duplicate(udh);
    }
        strptime(octstr_get_cstr(timestamp), "%Y-%m-%d %H:%M:%S", &tm);
        momsg->sms.time = gw_mktime(&tm);

    /* note: implicit msg_destroy */
    ret = bb_smscconn_receive(conn, momsg);
        httpstatus = HTTP_OK;
    retmsg = octstr_create("Thanks");
    } else if (apimsgid == NULL || status == NULL || timestamp == NULL || dest == NULL) {
        error(0, "HTTP[%s]: Insufficient args.",
              octstr_get_cstr(conn->id));
        httpstatus = HTTP_OK;
        retmsg = octstr_create("Insufficient arguments, rejected.");
    } else {
    switch (atoi(octstr_get_cstr(status))) {
    case  1: /* message unknown */
    case  5: /* error with message */
    case  6: /* user cancelled message */
    case  7: /* error delivering message */
    case  9: /* routing error */
    case 10: /* message expired */
        dlrstat = 2; /* delivery failure */
        break;
    case  2: /* message queued */
    case  3: /* delivered */
    case 11: /* message queued for later delivery */
        dlrstat = 4; /* message buffered */
        break;
    case  4: /* received by recipient */
    case  8: /* OK */
        dlrstat = 1; /* message received */
        break;
    default: /* unknown status code */
        dlrstat = 16; /* smsc reject */
        break;
    }
        dlrmsg = dlr_find(conn->id,
            apimsgid, /* smsc message id */
            dest, /* destination */
            dlrstat, 0);

        if (dlrmsg != NULL) {
            /* dlrmsg->sms.msgdata = octstr_duplicate(apimsgid); */
            dlrmsg->sms.sms_type = report_mo;
        dlrmsg->sms.time = atoi(octstr_get_cstr(timestamp));
        if (charge) {
        /* unsure if smsbox relays the binfo field to dlrs.
           But it is here in case they will start to do it. */
        dlrmsg->sms.binfo = octstr_duplicate(charge);
        }

            ret = bb_smscconn_receive(conn, dlrmsg);
            httpstatus = (ret == 0 ? HTTP_OK : HTTP_FORBIDDEN);
            retmsg = octstr_create("Sent");
        } else {
            error(0,"HTTP[%s]: got DLR but could not find message or was not interested "
                    "in it id<%s> dst<%s>, type<%d>",
            octstr_get_cstr(conn->id), octstr_get_cstr(apimsgid),
            octstr_get_cstr(dest), dlrstat);
            httpstatus = HTTP_OK;
            retmsg = octstr_create("Thanks");
        }
    }

    reply_headers = gwlist_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.clickatell", 0, "HTTP[%s]: Sending reply `%s'.",
          octstr_get_cstr(conn->id), octstr_get_cstr(retmsg));
    http_send_reply(client, httpstatus, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}

struct smsc_http_fn_callbacks smsc_http_clickatell_callback = {
        .send_sms = clickatell_send_sms,
        .parse_reply = clickatell_parse_reply,
        .receive_sms = clickatell_receive_sms,
};

