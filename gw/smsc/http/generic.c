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
 * Generic HTTP interface
 *
 * This 'generic' type will handle the 'send-url' directive in the
 * group the same way the 'sms-service' for smsbox does, via
 * URLTranslation. Response interpretation is based on the three
 * regex value that match against the reponse body. The HTTP reponse
 * code is not obeyed.
 *
 * Example config group:
 *
 *  group = smsc
 *  smsc = http
 *  system-type = generic
 *  send-url = "http://<foobar>/<uri>?from=%P&to=%p&text=%b"
 *  status-success-regex = "ok"
 *  status-permfail-regex = "failure"
 *  status-tempfail-regex = "retry later"
 *  generic-foreign-id-regex = "<id>(.+)</id>"
 *  generic-param-from = "phoneNumber"
 *  generic-param-to = "shortCode"
 *  generic-param-text = "message"
 *  generic-message-sent = "OK"
 *  generic-status-sent = 200
 *  generic-status-error = 400
 *
 * Note that neither 'smsc-username' nor 'smsc-password' is required,
 * since they are coded into the the 'send-url' value directly.
 *
 * Stipe Tolj <st@tolj.org>
 *
 * MO processing by Alejandro Guerrieri <aguerrieri at kannel dot org>
 */

#include "gwlib/gwlib.h"

/*
 * This maps fields to values for MO parameters
 */
struct fieldmap {
    Octstr *username;
    Octstr *password;
    Octstr *from;
    Octstr *to;
    Octstr *text;
    Octstr *udh;
    Octstr *service;
    Octstr *account;
    Octstr *binfo;
    Octstr *meta_data;
    Octstr *dlr_mask;
    Octstr *dlr_err;
    Octstr *dlr_url;
    Octstr *dlr_mid;
    Octstr *flash;
    Octstr *mclass;
    Octstr *mwi;
    Octstr *coding;
    Octstr *validity;
    Octstr *deferred;
    Octstr *foreign_id;
    Octstr *message_sent;
    long status_sent;
    long status_error;
};

struct generic_values {
    struct fieldmap *map;

    /* The following are compiled regex for the 'generic' type for handling
     * success, permanent failure and temporary failure. For types that use
     * simple HTTP body parsing, these may be used also for other types,
     * ie. for our own Kannel reply parsing. */
    regex_t *success_regex;
    regex_t *permfail_regex;
    regex_t *tempfail_regex;

    /* Compiled regex for the 'generic' type to get the foreign message id
    * from the HTTP response body */
    regex_t *generic_foreign_id_regex;
};

/*
 * Destroys the FieldMap structure
 */
static void fieldmap_destroy(struct fieldmap *fieldmap)
{
    if (fieldmap == NULL)
        return;
    octstr_destroy(fieldmap->username);
    octstr_destroy(fieldmap->password);
    octstr_destroy(fieldmap->from);
    octstr_destroy(fieldmap->to);
    octstr_destroy(fieldmap->text);
    octstr_destroy(fieldmap->udh);
    octstr_destroy(fieldmap->service);
    octstr_destroy(fieldmap->account);
    octstr_destroy(fieldmap->binfo);
    octstr_destroy(fieldmap->meta_data);
    octstr_destroy(fieldmap->dlr_mask);
    octstr_destroy(fieldmap->dlr_err);
    octstr_destroy(fieldmap->dlr_url);
    octstr_destroy(fieldmap->dlr_mid);
    octstr_destroy(fieldmap->flash);
    octstr_destroy(fieldmap->mclass);
    octstr_destroy(fieldmap->mwi);
    octstr_destroy(fieldmap->coding);
    octstr_destroy(fieldmap->validity);
    octstr_destroy(fieldmap->deferred);
    octstr_destroy(fieldmap->foreign_id);
    octstr_destroy(fieldmap->message_sent);
    gw_free(fieldmap);
}


/*
 * Get the FieldMap struct to map MO parameters
 */
static struct fieldmap *generic_get_field_map(CfgGroup *grp)
{
    struct fieldmap *fm = NULL;

    fm = gw_malloc(sizeof(*fm));
    gw_assert(fm != NULL);

    fm->username = cfg_get(grp, octstr_imm("generic-param-username"));
    if (fm->username == NULL)
        fm->username = octstr_create("username");
    fm->password = cfg_get(grp, octstr_imm("generic-param-password"));
    if (fm->password == NULL)
        fm->password = octstr_create("password");
    fm->from = cfg_get(grp, octstr_imm("generic-param-from"));
    if (fm->from == NULL)
        fm->from = octstr_create("from");
    fm->to = cfg_get(grp, octstr_imm("generic-param-to"));
    if (fm->to == NULL)
        fm->to = octstr_create("to");
    fm->text = cfg_get(grp, octstr_imm("generic-param-text"));
    if (fm->text == NULL)
        fm->text = octstr_create("text");
    fm->udh = cfg_get(grp, octstr_imm("generic-param-udh"));
    if (fm->udh == NULL)
        fm->udh = octstr_create("udh");
    /* "service" preloads the "username" parameter to mimic former behaviour */
    fm->service = cfg_get(grp, octstr_imm("generic-param-service"));
    if (fm->service == NULL)
        fm->service = octstr_create("username");
    fm->account = cfg_get(grp, octstr_imm("generic-param-account"));
    if (fm->account == NULL)
        fm->account = octstr_create("account");
    fm->binfo = cfg_get(grp, octstr_imm("generic-param-binfo"));
    if (fm->binfo == NULL)
        fm->binfo = octstr_create("binfo");
    fm->dlr_mask = cfg_get(grp, octstr_imm("generic-param-dlr-mask"));
    if (fm->dlr_mask == NULL)
        fm->dlr_mask = octstr_create("dlr-mask");
    fm->dlr_err = cfg_get(grp, octstr_imm("generic-param-dlr-err"));
    if (fm->dlr_err == NULL)
        fm->dlr_err = octstr_create("dlr-err");
    fm->dlr_url = cfg_get(grp, octstr_imm("generic-param-dlr-url"));
    if (fm->dlr_url == NULL)
        fm->dlr_url = octstr_create("dlr-url");
    fm->dlr_mid = cfg_get(grp, octstr_imm("generic-param-dlr-mid"));
    if (fm->dlr_mid == NULL)
        fm->dlr_mid = octstr_create("dlr-mid");
    fm->flash = cfg_get(grp, octstr_imm("generic-param-flash"));
    if (fm->flash == NULL)
        fm->flash = octstr_create("flash");
    fm->mclass = cfg_get(grp, octstr_imm("generic-param-mclass"));
    if (fm->mclass == NULL)
        fm->mclass = octstr_create("mclass");
    fm->mwi = cfg_get(grp, octstr_imm("generic-param-mwi"));
    if (fm->mwi == NULL)
        fm->mwi = octstr_create("mwi");
    fm->coding = cfg_get(grp, octstr_imm("generic-param-coding"));
    if (fm->coding == NULL)
        fm->coding = octstr_create("coding");
    fm->validity = cfg_get(grp, octstr_imm("generic-param-validity"));
    if (fm->validity == NULL)
        fm->validity = octstr_create("validity");
    fm->deferred = cfg_get(grp, octstr_imm("generic-param-deferred"));
    if (fm->deferred == NULL)
        fm->deferred = octstr_create("deferred");
    fm->foreign_id = cfg_get(grp, octstr_imm("generic-param-foreign-id"));
    if (fm->foreign_id == NULL)
        fm->foreign_id = octstr_create("foreign-id");
    fm->meta_data = cfg_get(grp, octstr_imm("generic-param-meta-data"));
    if (fm->meta_data == NULL)
        fm->meta_data = octstr_create("meta-data");
    fm->message_sent = cfg_get(grp, octstr_imm("generic-message-sent"));
    if (fm->message_sent == NULL)
        fm->message_sent = octstr_create("Sent");
    /* both success and error uses HTTP_ACCEPTED to mimic former behaviour */
    if (cfg_get_integer(&fm->status_sent, grp, octstr_imm("generic-status-sent")) == -1) {
        fm->status_sent = HTTP_ACCEPTED;
    }
    if (cfg_get_integer(&fm->status_error, grp, octstr_imm("generic-status-error")) == -1) {
        fm->status_error = HTTP_ACCEPTED;
    }

    return fm;
}

static void generic_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    struct generic_values *values = conndata->data;
    struct fieldmap *fm = values->map;
    Octstr *user, *pass, *from, *to, *text, *udh, *account, *binfo, *meta_data;
    Octstr *dlrmid, *dlrerr;
    Octstr *tmp_string, *retmsg;
    int dlrmask;
    List *reply_headers;
    int ret, retstatus;

    dlrmask = SMS_PARAM_UNDEFINED;

    /* Parse enough parameters to validate the request */
    user = http_cgi_variable(cgivars, octstr_get_cstr(fm->username));
    pass = http_cgi_variable(cgivars, octstr_get_cstr(fm->password));
    from = http_cgi_variable(cgivars, octstr_get_cstr(fm->from));
    to = http_cgi_variable(cgivars, octstr_get_cstr(fm->to));
    text = http_cgi_variable(cgivars, octstr_get_cstr(fm->text));
    udh = http_cgi_variable(cgivars, octstr_get_cstr(fm->udh));
    dlrmid = http_cgi_variable(cgivars, octstr_get_cstr(fm->dlr_mid));
    tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->dlr_mask));
    if (tmp_string) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &dlrmask);
    }
    dlrerr = http_cgi_variable(cgivars, octstr_get_cstr(fm->dlr_err));

    debug("smsc.http.generic", 0, "HTTP[%s]: Received an HTTP request",
          octstr_get_cstr(conn->id));

    if ((conndata->username != NULL && conndata->password != NULL) &&
        (user == NULL || pass == NULL ||
        octstr_compare(user, conndata->username) != 0 ||
        octstr_compare(pass, conndata->password) != 0)) {
        error(0, "HTTP[%s]: Authorization failure",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Authorization failed for sendsms");
        retstatus = fm->status_error;
    } else if (dlrmask != DLR_UNDEFINED && dlrmid != NULL) {
        /* we got a DLR, and we don't require additional values */
        Msg *dlrmsg;

        dlrmsg = dlr_find(conn->id,
            dlrmid, /* message id */
            to, /* destination */
            dlrmask, 0);

        if (dlrmsg != NULL) {
            dlrmsg->sms.sms_type = report_mo;
            dlrmsg->sms.msgdata = octstr_duplicate(text);
            dlrmsg->sms.account = octstr_duplicate(conndata->username);

            debug("smsc.http.generic", 0, "HTTP[%s]: Received DLR for DLR-URL <%s>",
                  octstr_get_cstr(conn->id), octstr_get_cstr(dlrmsg->sms.dlr_url));

            if (dlrerr != NULL) {
                /* pass errorcode as is */
                if (dlrmsg->sms.meta_data == NULL)
                    dlrmsg->sms.meta_data = octstr_create("");

                meta_data_set_value(dlrmsg->sms.meta_data, METADATA_DLR_GROUP,
                                    octstr_imm(METADATA_DLR_GROUP_ERRORCODE), dlrerr, 1);
            }

            Msg *resp = msg_duplicate(dlrmsg);
            ret = bb_smscconn_receive(conn, dlrmsg);
            if (ret == -1) {
                retmsg = octstr_create("Not accepted");
                retstatus = fm->status_error;
            } else {
                retmsg = urltrans_fill_escape_codes(fm->message_sent, resp);
                retstatus = fm->status_sent;
            }
            msg_destroy(resp);
        } else {
            error(0,"HTTP[%s]: Got DLR but could not find message or was not interested "
                  "in it id<%s> dst<%s>, type<%d>",
                  octstr_get_cstr(conn->id), octstr_get_cstr(dlrmid),
                  octstr_get_cstr(to), dlrmask);
            retmsg = octstr_create("Unknown DLR, not accepted");
            retstatus = fm->status_error;
        }
    } else if (from == NULL || to == NULL || text == NULL) {
        error(0, "HTTP[%s]: Insufficient args",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient args, rejected");
        retstatus = fm->status_error;
    } else if (udh != NULL && (octstr_len(udh) != octstr_get_char(udh, 0) + 1)) {
        error(0, "HTTP[%s]: UDH field misformed, rejected",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("UDH field misformed, rejected");
        retstatus = fm->status_error;
    } else if (udh != NULL && octstr_len(udh) > MAX_SMS_OCTETS) {
        error(0, "HTTP[%s]: UDH field is too long, rejected",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("UDH field is too long, rejected");
        retstatus = fm->status_error;
    }
    else {
        /* we got a normal MO SMS */
        Msg *msg;
        msg = msg_create(sms);

        /* Parse the rest of the parameters */
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->flash));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.mclass);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->mclass));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.mclass);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->mwi));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.mwi);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->coding));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.coding);
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->validity));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.validity);
            msg->sms.validity = time(NULL) + msg->sms.validity * 60;
        }
        tmp_string = http_cgi_variable(cgivars, octstr_get_cstr(fm->deferred));
        if (tmp_string) {
            sscanf(octstr_get_cstr(tmp_string),"%ld", &msg->sms.deferred);
            msg->sms.deferred = time(NULL) + msg->sms.deferred * 60;
        }
        account = http_cgi_variable(cgivars, octstr_get_cstr(fm->account));
        binfo = http_cgi_variable(cgivars, octstr_get_cstr(fm->binfo));
        meta_data = http_cgi_variable(cgivars, octstr_get_cstr(fm->meta_data));

        debug("smsc.http.generic", 0, "HTTP[%s]: Constructing new SMS",
              octstr_get_cstr(conn->id));

        /* convert character encoding if required */
        if (conndata->alt_charset &&
            charset_convert(text, octstr_get_cstr(conndata->alt_charset),
                    DEFAULT_CHARSET) != 0)
            error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave it as it is.",
                    octstr_get_cstr(conndata->alt_charset), DEFAULT_CHARSET);

        msg->sms.service = octstr_duplicate(user);
        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.msgdata = octstr_duplicate(text);
        msg->sms.udhdata = octstr_duplicate(udh);
        msg->sms.smsc_id = octstr_duplicate(conn->id);
        msg->sms.time = time(NULL);
        msg->sms.account = octstr_duplicate(account);
        msg->sms.binfo = octstr_duplicate(binfo);
        msg->sms.meta_data = octstr_duplicate(meta_data);
        Msg *resp = msg_duplicate(msg);
        ret = bb_smscconn_receive(conn, msg);
        if (ret == -1) {
            retmsg = octstr_create("Not accepted");
            retstatus = fm->status_error;
        } else {
            retmsg = urltrans_fill_escape_codes(fm->message_sent, resp);
            retstatus = fm->status_sent;
        }
        msg_destroy(resp);
    }

    reply_headers = gwlist_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.generic", 0, "HTTP[%s]: Sending reply",
          octstr_get_cstr(conn->id));
    http_send_reply(client, retstatus, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}


static int generic_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url = NULL;
    List *headers;

    /* We use the escape code population function from our
     * URLTranslation module to fill in the appropriate values
     * into the URL scheme. */
    url = urltrans_fill_escape_codes(conndata->send_url, sms);

    headers = gwlist_create();
    debug("smsc.http.generic", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers,
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);

    return 0;
}

static void generic_parse_reply(SMSCConn *conn, Msg *msg, int status,
                                List *headers, Octstr *body)
{
    ConnData *conndata = conn->data;
    struct generic_values *values = conndata->data;
    regmatch_t pmatch[2];
    Octstr *msgid = NULL;

    /*
     * Our generic type checks only content on the HTTP response body.
     * We use the pre-compiled regex to match against the states.
     * This is the most generic criteria (at the moment).
     */
    if ((values->success_regex != NULL) &&
        (gw_regex_exec(values->success_regex, body, 0, NULL, 0) == 0)) {
        /* SMSC ACK... the message id should be in the body */

        /* add to our own DLR storage */
        if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask)) {
            /* directive 'generic-foreign-id-regex' is present, fetch the foreign ID */
            if ((values->generic_foreign_id_regex != NULL)) {
                if (gw_regex_exec(values->generic_foreign_id_regex, body, sizeof(pmatch) / sizeof(regmatch_t), pmatch, 0) == 0) {
                    if (pmatch[1].rm_so != -1 && pmatch[1].rm_eo != -1) {
                        msgid = octstr_copy(body, pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
                        debug("smsc.http.generic", 0, "HTTP[%s]: Found foreign message id <%s> in body.",
                              octstr_get_cstr(conn->id), octstr_get_cstr(msgid));
                        dlr_add(conn->id, msgid, msg, 0);
                        octstr_destroy(msgid);
                    }
                }
                if (msgid == NULL)
                    warning(0, "HTTP[%s]: Can't get the foreign message id from the HTTP body.",
                            octstr_get_cstr(conn->id));
            } else {
                char id[UUID_STR_LEN + 1];
                /* use own own UUID as msg ID in the DLR storage */
                uuid_unparse(msg->sms.id, id);
                msgid = octstr_create(id);
                dlr_add(conn->id, msgid, msg, 0);
                octstr_destroy(msgid);
            }
        }
        bb_smscconn_sent(conn, msg, NULL);
    }
    else if ((values->permfail_regex != NULL) &&
        (gw_regex_exec(values->permfail_regex, body, 0, NULL, 0) == 0)) {
        error(0, "HTTP[%s]: Message not accepted.", octstr_get_cstr(conn->id));
        bb_smscconn_send_failed(conn, msg,
            SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
    }
    else if ((values->tempfail_regex != NULL) &&
        (gw_regex_exec(values->tempfail_regex, body, 0, NULL, 0) == 0)) {
        warning(0, "HTTP[%s]: Message temporary not accepted, will retry.",
                octstr_get_cstr(conn->id));
        bb_smscconn_send_failed(conn, msg,
            SMSCCONN_FAILED_TEMPORARILY, octstr_duplicate(body));
    }
    else {
        error(0, "HTTP[%s]: Message was rejected. SMSC reponse was:",
              octstr_get_cstr(conn->id));
        octstr_dump(body, 0);
        bb_smscconn_send_failed(conn, msg,
            SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));
    }
}

static int generic_init(SMSCConn *conn, CfgGroup *cfg)
{
    Octstr *os;
    ConnData *conndata = conn->data;
    struct generic_values *values;

    /* we need at least the criteria for a successful sent */
    if ((os = cfg_get(cfg, octstr_imm("status-success-regex"))) == NULL) {
        error(0, "HTTP[%s]: 'status-success-regex' required for generic http smsc",
              octstr_get_cstr(conn->id));
        return -1;
    }
    conndata->data = values = gw_malloc(sizeof(*values));
    /* reset */
    memset(conndata->data, 0, sizeof(*values));

    values->success_regex = values->permfail_regex = values->tempfail_regex = NULL;
    values->generic_foreign_id_regex = NULL;

    values->map = generic_get_field_map(cfg);

    /* pre-compile regex expressions */
    if (os != NULL) {   /* this is implicit due to the above if check */
        if ((values->success_regex = gw_regex_comp(os, REG_EXTENDED|REG_NOSUB)) == NULL)
            error(0, "HTTP[%s]: Could not compile pattern '%s' defined for variable 'status-success-regex'",
                  octstr_get_cstr(conn->id), octstr_get_cstr(os));
        octstr_destroy(os);
    }
    if ((os = cfg_get(cfg, octstr_imm("status-permfail-regex"))) != NULL) {
        if ((values->permfail_regex = gw_regex_comp(os, REG_EXTENDED|REG_NOSUB)) == NULL)
            panic(0, "Could not compile pattern '%s' defined for variable 'status-permfail-regex'", octstr_get_cstr(os));
        octstr_destroy(os);
    }
    if ((os = cfg_get(cfg, octstr_imm("status-tempfail-regex"))) != NULL) {
        if ((values->tempfail_regex = gw_regex_comp(os, REG_EXTENDED|REG_NOSUB)) == NULL)
            panic(0, "Could not compile pattern '%s' defined for variable 'status-tempfail-regex'", octstr_get_cstr(os));
        octstr_destroy(os);
    }
    if ((os = cfg_get(cfg, octstr_imm("generic-foreign-id-regex"))) != NULL) {
        if ((values->generic_foreign_id_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s' defined for variable 'generic-foreign-id-regex'", octstr_get_cstr(os));
        else {
            /* check quickly that at least 1 group seems to be defined in the regex */
            if (octstr_search_char(os, '(', 0) == -1 || octstr_search_char(os, ')', 0) == -1)
                warning(0, "HTTP[%s]: No group defined in pattern '%s' for variable 'generic-foreign-id-regex'", octstr_get_cstr(conn->id), octstr_get_cstr(os));
        }
        octstr_destroy(os);
    }

    debug("", 0, "generic init completed");

    return 0;
}

static void generic_destroy(SMSCConn *conn)
{
    ConnData *conndata;
    struct generic_values *values;

    if (conn == NULL || conn->data == NULL)
        return;

    conndata = conn->data;
    values = conndata->data;

    fieldmap_destroy(values->map);
    if (values->success_regex)
        gw_regex_destroy(values->success_regex);
    if (values->permfail_regex)
        gw_regex_destroy(values->permfail_regex);
    if (values->tempfail_regex)
        gw_regex_destroy(values->tempfail_regex);
    if (values->generic_foreign_id_regex)
        gw_regex_destroy(values->generic_foreign_id_regex);

    gw_free(values);
    conndata->data = NULL;

    debug("", 0, "generic destroy completed");
}

struct smsc_http_fn_callbacks smsc_http_generic_callback = {
        .init = generic_init,
        .destroy = generic_destroy,
        .send_sms = generic_send_sms,
        .parse_reply = generic_parse_reply,
        .receive_sms = generic_receive_sms,
};
