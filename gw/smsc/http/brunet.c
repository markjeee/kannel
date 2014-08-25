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
 * Brunet - A german aggregator (mainly doing T-Mobil D1 connections)
 *
 *  o bruHTT v1.3L (for MO traffic)
 *  o bruHTP v2.1 (date 22.04.2003) (for MT traffic)
 *
 * Stipe Tolj <stolj@wapme.de>
 * Tobias Weber <weber@wapme.de>
 */

#include "gwlib/gwlib.h"


/* MT related function */
static int brunet_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url, *tid, *xser;
    List *headers;
    char id[UUID_STR_LEN + 1];
    int dcs;

    /*
     * Construct TransactionId.
     * Beware that brunet needs an "clean" octstr representation,
     * without the dashes in the string. So remove them.
     */
    uuid_unparse(sms->sms.id, id);
    tid = octstr_create(id);
    octstr_replace(tid, octstr_imm("-"), octstr_imm(""));

    /* form the basic URL */
    url = octstr_format("%S?MsIsdn=%E&Originator=%E",
        conndata->send_url, sms->sms.receiver, sms->sms.sender);

    /*
     * We use &binfo=<foobar> from sendsms interface to encode
     * additional paramters. If a mandatory value is not set,
     * a default value is applied
     */
    if (octstr_len(sms->sms.binfo)) {
        octstr_url_decode(sms->sms.binfo);
        octstr_format_append(url, "&%S", sms->sms.binfo);
    }
    /* CustomerId */
    if (octstr_search(url, octstr_create("CustomerId="), 0) == -1) {
        octstr_format_append(url, "&CustomerId=%S", conndata->username);
    }
    /* TransactionId */
    if (octstr_search(url, octstr_create("TransactionId="), 0) == -1) {
        octstr_format_append(url, "&TransactionId=%S", tid);
    }
    /* SMSCount */
    if (octstr_search(url, octstr_create("SMSCount="), 0) == -1) {
        octstr_format_append(url, "&%s", "SMSCount=1");
    }
    /* ActionType */
    if (octstr_search(url, octstr_create("ActionType="), 0) == -1) {
        octstr_format_append(url, "&%s", "ActionType=A");
    }
    /* ServiceDeliveryType */
    if (octstr_search(url, octstr_create("ServiceDeliveryType="), 0) == -1) {
        octstr_format_append(url, "&%s", "ServiceDeliveryType=P");
    }

    /* if coding is not set and UDH exists, assume DC_8BIT
     * else default to DC_7BIT */
    if (sms->sms.coding == DC_UNDEF)
        sms->sms.coding = octstr_len(sms->sms.udhdata) > 0 ? DC_8BIT : DC_7BIT;

    if (sms->sms.coding == DC_8BIT)
        octstr_format_append(url, "&MessageType=B&Text=%H", sms->sms.msgdata);
    else
        octstr_format_append(url, "&MessageType=S&Text=%E", sms->sms.msgdata);

    dcs = fields_to_dcs(sms,
        (sms->sms.alt_dcs != SMS_PARAM_UNDEFINED ? sms->sms.alt_dcs : 0));

    /* XSer processing */
    xser = octstr_create("");
    /* XSer DCS values */
    if (dcs != 0 && dcs != 4)
        octstr_format_append(xser, "0201%02x", dcs & 0xff);
    /* add UDH header */
    if (octstr_len(sms->sms.udhdata)) {
        octstr_format_append(xser, "01%02x%H", octstr_len(sms->sms.udhdata),
                             sms->sms.udhdata);
    }
    if (octstr_len(xser) > 0)
        octstr_format_append(url, "&XSer=%S", xser);
    octstr_destroy(xser);


    headers = http_create_empty_headers();
    debug("smsc.http.brunet", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));

    /*
     * Brunet requires an SSL-enabled HTTP client call, this is handled
     * transparently by the Kannel HTTP layer module.
     */
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers,
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    octstr_destroy(tid);
    http_destroy_headers(headers);

    return 0;
}


/*
 * Parse a line in the format: <name=value name=value ...>
 * and return a Dict with the name as key and the value as value,
 * otherwise return NULL if a parsing error occures.
 */
static Dict *brunet_parse_body(Octstr *body)
{
    Dict *param = NULL;
    List *words = NULL;
    long len;
    Octstr *word;

    words = octstr_split_words(body);
    if ((len = gwlist_len(words)) > 0) {
        param = dict_create(4, (void(*)(void *)) octstr_destroy);
        while ((word = gwlist_extract_first(words)) != NULL) {
            List *l = octstr_split(word, octstr_imm("="));
            Octstr *key = gwlist_extract_first(l);
            Octstr *value = gwlist_extract_first(l);
            if (octstr_len(key))
                dict_put(param, key, value);
            octstr_destroy(key);
            octstr_destroy(word);
            gwlist_destroy(l, (void(*)(void *)) octstr_destroy);
        }
    }
    gwlist_destroy(words, (void(*)(void *)) octstr_destroy);

    return param;
}


static void brunet_parse_reply(SMSCConn *conn, Msg *msg, int status,
                               List *headers, Octstr *body)
{
    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        Dict *param;
        Octstr *status;

        if ((param = brunet_parse_body(body)) != NULL &&
            (status = dict_get(param, octstr_imm("Status"))) != NULL &&
            octstr_case_compare(status, octstr_imm("0")) == 0) {
            Octstr *msg_id;

            /* pass the MessageId for this MT to the logging facility */
            if ((msg_id = dict_get(param, octstr_imm("MessageId"))) != NULL)
                msg->sms.binfo = octstr_duplicate(msg_id);

            bb_smscconn_sent(conn, msg, NULL);

        } else {
            error(0, "HTTP[%s]: Message was malformed. SMSC response `%s'.",
                  octstr_get_cstr(conn->id), octstr_get_cstr(body));
            bb_smscconn_send_failed(conn, msg,
                    SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
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
static void brunet_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    Octstr *user, *from, *to, *text, *udh;
    Octstr *retmsg;
    int mclass, mwi, coding, validity, deferred;
    List *reply_headers;
    int ret;

    mclass = mwi = coding = validity = deferred = 0;

    user = http_cgi_variable(cgivars, "CustomerId");
    from = http_cgi_variable(cgivars, "MsIsdn");
    to = http_cgi_variable(cgivars, "Recipient");
    text = http_cgi_variable(cgivars, "SMMO");
    udh = http_cgi_variable(cgivars, "XSer");

    debug("smsc.http.brunet", 0, "HTTP[%s]: Received a request",
          octstr_get_cstr(conn->id));

    if (user == NULL || octstr_compare(user, conndata->username) != 0) {
        error(0, "HTTP[%s]: Authorization failure. CustomerId was <%s>.",
              octstr_get_cstr(conn->id), octstr_get_cstr(user));
        retmsg = octstr_create("Authorization failed for MO submission.");
    }
    else if (from == NULL || to == NULL || text == NULL) {
        error(0, "HTTP[%s]: Insufficient args.",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient arguments, rejected.");
    }
    else {
        Msg *msg;
        msg = msg_create(sms);

        debug("smsc.http.brunet", 0, "HTTP[%s]: Received new MO SMS.",
              octstr_get_cstr(conn->id));

        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.msgdata = octstr_duplicate(text);
        msg->sms.udhdata = octstr_duplicate(udh);

        msg->sms.smsc_id = octstr_duplicate(conn->id);
        msg->sms.time = time(NULL); /* XXX maybe extract from DateReceived */
        msg->sms.mclass = mclass;
        msg->sms.mwi = mwi;
        msg->sms.coding = coding;
        msg->sms.validity = time(NULL) + validity * 60;
        msg->sms.deferred = time(NULL) + deferred * 60;

        ret = bb_smscconn_receive(conn, msg);
        if (ret == -1)
            retmsg = octstr_create("Status=1");
        else
            retmsg = octstr_create("Status=0");
    }

    reply_headers = gwlist_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.brunet", 0, "HTTP[%s]: Sending reply `%s'.",
          octstr_get_cstr(conn->id), octstr_get_cstr(retmsg));
    http_send_reply(client, HTTP_OK, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}

static int brunet_init(SMSCConn *conn, CfgGroup *cfg)
{
    ConnData *conndata = conn->data;

    if (conndata->username == NULL) {
        error(0, "HTTP[%s]: 'username' (=CustomerId) required for Brunet http smsc",
              octstr_get_cstr(conn->id));
        return -1;
    }

    return 0;
}

struct smsc_http_fn_callbacks smsc_http_brunet_callback = {
        .init = brunet_init,
        .send_sms = brunet_send_sms,
        .parse_reply = brunet_parse_reply,
        .receive_sms = brunet_receive_sms,
};


