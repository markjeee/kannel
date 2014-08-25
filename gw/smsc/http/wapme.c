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
 * Wapme SMS Proxy
 *
 * Stipe Tolj <stolj@kannel.org>
 */

#include "gwlib/gwlib.h"


static int wapme_smsproxy_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url;
    List *headers;

    url = octstr_format("%S?command=forward&smsText=%E&phoneNumber=%E"
                        "&serviceNumber=%E&smsc=%E",
                        conndata->send_url,
                        sms->sms.msgdata, sms->sms.sender, sms->sms.receiver,
                        sms->sms.smsc_id);

    headers = gwlist_create();
    debug("smsc.http.wapme", 0, "HTTP[%s]: Start request",
          octstr_get_cstr(conn->id));
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers,
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);

    return 0;
}

static void wapme_smsproxy_parse_reply(SMSCConn *conn, Msg *msg, int status,
                   List *headers, Octstr *body)
{
    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        bb_smscconn_sent(conn, msg, NULL);
    } else {
        bb_smscconn_send_failed(conn, msg,
                SMSCCONN_FAILED_MALFORMED, octstr_duplicate(body));
    }
}

/*
 * static void wapme_smsproxy_receive_sms(SMSCConn *conn, HTTPClient *client,
 *                                List *headers, Octstr *body, List *cgivars)
 *
 * The HTTP server for MO messages will act with the same interface as smsbox's
 * sendsms interface, so that the logical difference is hidden and SMS Proxy
 * can act transparently. So there is no need for an explicite implementation
 * here.
 */

static int wapme_smsproxy_init(SMSCConn *conn, CfgGroup *cfg)
{
    ConnData *conndata = conn->data;

    if (conndata->username == NULL || conndata->password == NULL) {
        error(0, "HTTP[%s]: 'username' and 'password' required for Wapme http smsc",
              octstr_get_cstr(conn->id));
        return -1;
    }
    return 0;
}

struct smsc_http_fn_callbacks smsc_http_wapme_callback = {
        .init = wapme_smsproxy_init,
        .send_sms = wapme_smsproxy_send_sms,
        .parse_reply = wapme_smsproxy_parse_reply,
        .receive_sms = kannel_receive_sms,
};

