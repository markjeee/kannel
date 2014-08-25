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
 * 3united.com (formerly Xidris) - An austrian (AT) SMS aggregator
 * Implementing version 1.3, 2003-05-06
 * Updating to version 1.9.1, 2004-09-28
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include "gwlib/gwlib.h"

/*
 * Parse for an parameter of an given XML tag and return it as Octstr
 */
static Octstr *parse_xml_tag(Octstr *body, Octstr *tag)
{
    Octstr *stag, *etag, *ret;
    int spos, epos;

    stag = octstr_format("<%s>", octstr_get_cstr(tag));
    if ((spos = octstr_search(body, stag, 0)) == -1) {
        octstr_destroy(stag);
        return NULL;
    }
    etag = octstr_format("</%s>", octstr_get_cstr(tag));
    if ((epos = octstr_search(body, etag, spos+octstr_len(stag))) == -1) {
        octstr_destroy(stag);
        octstr_destroy(etag);
        return NULL;
    }

    ret = octstr_copy(body, spos+octstr_len(stag), epos+1 - (spos+octstr_len(etag)));
    octstr_strip_blanks(ret);
    octstr_strip_crlfs(ret);

    octstr_destroy(stag);
    octstr_destroy(etag);

    return ret;
}

/* MT related function */
static int xidris_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url, *new_msg;
    List *headers;
    int dcs, esm_class;

    url = new_msg = NULL;
    dcs = esm_class = 0;

    /* format the URL for call */
    url = octstr_format("%S?"
        "app_id=%E&key=%E&dest_addr=%E&source_addr=%E",
        conndata->send_url, conndata->username,
        conndata->password, sms->sms.receiver, sms->sms.sender);

    if (octstr_len(sms->sms.udhdata)) {
        /* RAW additions for binary (8bit) msgs  */

        /* set the data coding scheme (DCS) and ESM class fields */
        dcs = fields_to_dcs(sms, sms->sms.alt_dcs);
        /* ESM_CLASS_SUBMIT_STORE_AND_FORWARD_MODE |
           ESM_CLASS_SUBMIT_UDH_INDICATOR */
        esm_class = 0x03 | 0x40;

        /* prepend UDH header to message block */
        new_msg = octstr_duplicate(sms->sms.udhdata);
        octstr_append(new_msg, sms->sms.msgdata);

        octstr_format_append(url, "&type=200&dcs=%d&esm=%d&message=%H",
                             dcs, esm_class, new_msg);
    }  else {
        /* additions for text (7bit) msgs */

        octstr_format_append(url, "&type=%E&message=%E",
                            (sms->sms.mclass ? octstr_imm("1") : octstr_imm("0")),
                            sms->sms.msgdata);
    }

    /*
     * We use &account=<foobar> from sendsms interface to encode any additionaly
     * proxied parameters, ie. billing information.
     */
    if (octstr_len(sms->sms.account)) {
        octstr_url_decode(sms->sms.account);
        octstr_format_append(url, "&%s", octstr_get_cstr(sms->sms.account));
    }

    headers = gwlist_create();
    debug("smsc.http.xidris", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));

    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers,
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    octstr_destroy(new_msg);
    http_destroy_headers(headers);

    return 0;
}


static void xidris_parse_reply(SMSCConn *conn, Msg *msg, int status,
                               List *headers, Octstr *body)
{
    Octstr *code, *desc, *mid;

    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        /* now parse the XML document for error code */
        code = parse_xml_tag(body, octstr_imm("status"));
        desc = parse_xml_tag(body, octstr_imm("description"));

        /* The following parsing assumes we get only *one* message id in the
         * response XML. Which is ok, since we garantee via previous concat
         * splitting, that we only pass PDUs of 1 SMS size to SMSC. */
        mid = parse_xml_tag(body, octstr_imm("message_id"));

        if (octstr_case_compare(code, octstr_imm("0")) == 0 && mid != NULL) {
            /* ensure the message id gets logged */
            msg->sms.binfo = octstr_duplicate(mid);

            /* SMSC ACK.. now we have the message id. */
            if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask))
                dlr_add(conn->id, mid, msg, 0);

            octstr_destroy(mid);
            bb_smscconn_sent(conn, msg, NULL);

        } else {
            error(0, "HTTP[%s]: Message not accepted. Status code <%s> "
                  "description `%s'.", octstr_get_cstr(conn->id),
                  octstr_get_cstr(code), octstr_get_cstr(desc));
            bb_smscconn_send_failed(conn, msg,
                    SMSCCONN_FAILED_MALFORMED, octstr_duplicate(desc));
        }
    } else {
        error(0, "HTTP[%s]: Message was rejected. SMSC reponse was:",
              octstr_get_cstr(conn->id));
        octstr_dump(body, 0);
        bb_smscconn_send_failed(conn, msg,
                SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));
    }
}

/* MO related function */
static void xidris_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    Octstr *user, *pass, *from, *to, *text, *account, *binfo;
    Octstr *state, *mid, *dest;
    Octstr *retmsg;
    int mclass, mwi, coding, validity, deferred;
    List *reply_headers;
    int ret, status;

    mclass = mwi = coding = validity = deferred = 0;
    retmsg = NULL;

    /* generic values */
    user = http_cgi_variable(cgivars, "app_id");
    pass = http_cgi_variable(cgivars, "key");

    /* MO specific values */
    from = http_cgi_variable(cgivars, "source_addr");
    to = http_cgi_variable(cgivars, "dest_addr");
    text = http_cgi_variable(cgivars, "message");
    account = http_cgi_variable(cgivars, "operator");
    binfo = http_cgi_variable(cgivars, "tariff");

    /* DLR (callback) specific values */
    state = http_cgi_variable(cgivars, "state");
    mid = http_cgi_variable(cgivars, "message_id");
    dest = http_cgi_variable(cgivars, "dest_addr");

    debug("smsc.http.xidris", 0, "HTTP[%s]: Received a request",
          octstr_get_cstr(conn->id));

    if (user == NULL || pass == NULL ||
        octstr_compare(user, conndata->username) != 0 ||
        octstr_compare(pass, conndata->password) != 0) {
        error(0, "HTTP[%s]: Authorization failure. username was <%s>.",
              octstr_get_cstr(conn->id), octstr_get_cstr(user));
        retmsg = octstr_create("Authorization failed for MO submission.");
        status = HTTP_UNAUTHORIZED;
    }
    else if (state != NULL && mid != NULL && dest != NULL) {    /* a DLR message */
        Msg *dlrmsg;
        int dlrstat = -1;

        if (octstr_compare(state, octstr_imm("DELIVRD")) == 0)
            dlrstat = DLR_SUCCESS;
        else if (octstr_compare(state, octstr_imm("ACCEPTD")) == 0)
            dlrstat = DLR_BUFFERED;
        else
            dlrstat = DLR_FAIL;

        dlrmsg = dlr_find(conn->id,
            mid, /* smsc message id */
            dest, /* destination */
            dlrstat, 0);

        if (dlrmsg != NULL) {
            dlrmsg->sms.msgdata = octstr_duplicate(mid);
            dlrmsg->sms.sms_type = report_mo;

            ret = bb_smscconn_receive(conn, dlrmsg);
            status = (ret == 0 ? HTTP_OK : HTTP_FORBIDDEN);
        } else {
            error(0,"HTTP[%s]: got DLR but could not find message or was not interested "
                    "in it id<%s> dst<%s>, type<%d>",
                octstr_get_cstr(conn->id), octstr_get_cstr(mid),
                octstr_get_cstr(dest), dlrstat);
            status = HTTP_OK;
        }

    }
    else if (from == NULL || to == NULL || text == NULL) {
        error(0, "HTTP[%s]: Insufficient args.",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient arguments, rejected.");
        status = HTTP_BAD_REQUEST;
    }
    else {
        Msg *msg;
        msg = msg_create(sms);

        debug("smsc.http.xidris", 0, "HTTP[%s]: Received new MO SMS.",
              octstr_get_cstr(conn->id));

        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.msgdata = octstr_duplicate(text);
        msg->sms.account = octstr_duplicate(account);
        msg->sms.binfo = octstr_duplicate(binfo);

        msg->sms.smsc_id = octstr_duplicate(conn->id);
        msg->sms.time = time(NULL);
        msg->sms.mclass = mclass;
        msg->sms.mwi = mwi;
        msg->sms.coding = coding;
        msg->sms.validity = time(NULL) + validity * 60;
        msg->sms.deferred = time(NULL) + deferred * 60;

        ret = bb_smscconn_receive(conn, msg);
        status = (ret == 0 ? HTTP_OK : HTTP_FORBIDDEN);
    }

    reply_headers = gwlist_create();
    debug("smsc.http.xidris", 0, "HTTP[%s]: Sending reply with HTTP status <%d>.",
          octstr_get_cstr(conn->id), status);

    http_send_reply(client, status, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}

static int xidris_init(SMSCConn *conn, CfgGroup *cfg)
{
    ConnData *conndata = conn->data;

    if (conndata->username == NULL || conndata->password == NULL) {
        error(0, "HTTP[%s]: 'username' and 'password' required for Xidris http smsc",
              octstr_get_cstr(conn->id));
        return -1;
    }

    return 0;
}

struct smsc_http_fn_callbacks smsc_http_xidris_callback = {
        .init = xidris_init,
        .send_sms = xidris_send_sms,
        .parse_reply = xidris_parse_reply,
        .receive_sms = xidris_receive_sms,
};
