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
 * smsbox.c - main program of the smsbox
 */

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

/* libxml & xpath things */
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "gwlib/gwlib.h"
#include "gwlib/regex.h"
#include "gwlib/gw-timer.h"

#include "msg.h"
#include "sms.h"
#include "dlr.h"
#include "bb.h"
#include "shared.h"
#include "heartbeat.h"
#include "html.h"
#include "urltrans.h"
#include "ota_prov_attr.h"
#include "ota_prov.h"
#include "ota_compiler.h"
#include "xml_shared.h"

#ifdef HAVE_SECURITY_PAM_APPL_H
#include <security/pam_appl.h>
#endif


#define SENDSMS_DEFAULT_CHARS "0123456789 +-"

#define O_DESTROY(a) { if(a) octstr_destroy(a); a = NULL; }

#define ACCOUNT_MAX_LEN 64

/* Defaults for the HTTP request queueing inside http_queue_thread */
#define HTTP_MAX_RETRIES    0
#define HTTP_RETRY_DELAY    10 /* in sec. */
#define HTTP_MAX_PENDING    512 /* max requests handled in parallel */

/* Timer item structure for HTTP retrying */
typedef struct TimerItem {
    Timer *timer;
    void *id;
} TimerItem;

/* have we received restart cmd from bearerbox? */
volatile sig_atomic_t restart = 0;

static Cfg *cfg;
static long bb_port;
static int bb_ssl = 0;
static long sendsms_port = 0;
static Octstr *sendsms_interface = NULL;
static Octstr *smsbox_id = NULL;
static Octstr *sendsms_url = NULL;
static Octstr *sendota_url = NULL;
static Octstr *xmlrpc_url = NULL;
static Octstr *bb_host;
static Octstr *accepted_chars = NULL;
static int only_try_http = 0;
static URLTranslationList *translations = NULL;
static long sms_max_length = MAX_SMS_OCTETS;
static char *sendsms_number_chars;
static Octstr *global_sender = NULL;
static Octstr *reply_couldnotfetch = NULL;
static Octstr *reply_couldnotrepresent = NULL;
static Octstr *reply_requestfailed = NULL;
static Octstr *reply_emptymessage = NULL;
static int mo_recode = 0;
static Numhash *white_list;
static Numhash *black_list;
static regex_t *white_list_regex = NULL;
static regex_t *black_list_regex = NULL;
static long max_http_retries = HTTP_MAX_RETRIES;
static long http_queue_delay = HTTP_RETRY_DELAY;
static Octstr *ppg_service_name = NULL;

static List *smsbox_requests = NULL;      /* the inbound request queue */
static List *smsbox_http_requests = NULL; /* the outbound HTTP request queue */

/* Timerset for the HTTP retry mechanism. */
static Timerset *timerset = NULL;

/* Maximum requests that we handle in parallel */
static Semaphore *max_pending_requests;

int charset_processing (Octstr *charset, Octstr *text, int coding);

/* for delayed HTTP answers.
 * Dict key is uuid, value is HTTPClient pointer
 * of open transaction
 */

static int immediate_sendsms_reply = 0;
static Dict *client_dict = NULL;
static List *sendsms_reply_hdrs = NULL;

/***********************************************************************
 * Communication with the bearerbox.
 */


/*
 * Identify ourself to bearerbox for smsbox-specific routing inside bearerbox.
 * Do this even while no smsbox-id is given to unlock the sender thread in
 * bearerbox.
 */
static void identify_to_bearerbox(void)
{
    Msg *msg;

    msg = msg_create(admin);
    msg->admin.command = cmd_identify;
    msg->admin.boxc_id = octstr_duplicate(smsbox_id);
    write_to_bearerbox(msg);
}

/*
 * Handle delayed reply to HTTP sendsms client, if any
 */
static void delayed_http_reply(Msg *msg)
{
    HTTPClient *client;
    Octstr *os, *answer;
    char id[UUID_STR_LEN + 1];
    int status;
	  
    uuid_unparse(msg->ack.id, id);
    os = octstr_create(id);
    debug("sms.http", 0, "Got ACK (%ld) of %s", msg->ack.nack, octstr_get_cstr(os));
    client = dict_remove(client_dict, os);
    if (client == NULL) {
        debug("sms.http", 0, "No client - multi-send or ACK to pull-reply");
        octstr_destroy(os);
        return;
    }
    /* XXX  this should be fixed so that we really wait for DLR
     *      SMSC accept/deny before doing this - but that is far
     *      more slower, a bit more complex, and is done later on
     */

    switch (msg->ack.nack) {
      case ack_success:
        status = HTTP_ACCEPTED;
        answer = octstr_create("0: Accepted for delivery");
        break;
      case ack_buffered:
        status = HTTP_ACCEPTED;
        answer = octstr_create("3: Queued for later delivery");
        break;
      case ack_failed:
        status = HTTP_FORBIDDEN;
        answer = octstr_create("Not routable. Do not try again.");
        break;
      case ack_failed_tmp:
        status = HTTP_SERVICE_UNAVAILABLE;
        answer = octstr_create("Temporal failure, try again later.");
        break;
      default:
	error(0, "Strange reply from bearerbox!");
        status = HTTP_SERVICE_UNAVAILABLE;
        answer = octstr_create("Temporal failure, try again later.");
        break;
    }

    http_send_reply(client, status, sendsms_reply_hdrs, answer);

    octstr_destroy(answer);
    octstr_destroy(os);
}


/*
 * Read an Msg from the bearerbox and send it to the proper receiver
 * via a List. At the moment all messages are sent to the smsbox_requests
 * List.
 */
static void read_messages_from_bearerbox(void)
{
    time_t start, t;
    int secs;
    int total = 0;
    int ret;
    Msg *msg;

    start = t = time(NULL);
    while (program_status != shutting_down) {
        /* block infinite for reading messages */
        ret = read_from_bearerbox(&msg, INFINITE_TIME);
        if (ret == -1) {
            if (program_status != shutting_down) {
                error(0, "Bearerbox is gone, restarting");
                program_status = shutting_down;
                restart = 1;
            }
            break;
        } else if (ret == 1) /* timeout */
            continue;
        else if (msg == NULL) /* just to be sure, may not happens */
            break;

	if (msg_type(msg) == admin) {
	    if (msg->admin.command == cmd_shutdown) {
		info(0, "Bearerbox told us to die");
		program_status = shutting_down;
	    } else if (msg->admin.command == cmd_restart) {
		info(0, "Bearerbox told us to restart");
		restart = 1;
		program_status = shutting_down;
	    }
	    /*
	     * XXXX here should be suspend/resume, add RSN
	     */
	    msg_destroy(msg);
	} else if (msg_type(msg) == sms) {
	    if (total == 0)
		start = time(NULL);
	    total++;
	    gwlist_produce(smsbox_requests, msg);
	} else if (msg_type(msg) == ack) {
	    if (!immediate_sendsms_reply)
		delayed_http_reply(msg);
	    msg_destroy(msg);
	} else {
	    warning(0, "Received other message than sms/admin, ignoring!");
	    msg_destroy(msg);
	}
    }
    secs = difftime(time(NULL), start);
    info(0, "Received (and handled?) %d requests in %d seconds "
    	 "(%.2f per second)", total, secs, (float)total / secs);
}


/***********************************************************************
 * Send Msg to bearerbox for delivery to phone, possibly split it first.
 */

/*
 * Counter for catenated SMS messages. The counter that can be put into
 * the catenated SMS message's UDH headers is actually the lowest 8 bits.
 */
static Counter *catenated_sms_counter;
 
/*
 * Send a message to the bearerbox for delivery to a phone. Use
 * configuration from `trans' to format the message before sending.
 * Return >= 0 for success & count of splitted sms messages, 
 * -1 for failure.  Does not destroy the msg.
 */
static int send_message(URLTranslation *trans, Msg *msg)
{
    int max_msgs;
    Octstr *header, *footer, *suffix, *split_chars;
    int catenate;
    unsigned long msg_sequence, msg_count;
    List *list;
    Msg *part;

    gw_assert(msg != NULL);
    gw_assert(msg_type(msg) == sms);

    if (trans != NULL)
        max_msgs = urltrans_max_messages(trans);
    else
        max_msgs = 1;

    if (max_msgs == 0) {
        info(0, "No reply sent, denied.");
        return 0;
    }

    /*
     * Encode our smsbox-id to the msg structure.
     * This will allow bearerbox to return specific answers to the
     * same smsbox, mainly for DLRs and SMS proxy modes.
     */
    if (smsbox_id != NULL) {
        msg->sms.boxc_id = octstr_duplicate(smsbox_id);
    }

    /*
     * Empty message? Two alternatives have to be handled:
     *  a) it's a HTTP sms-service reply: either ignore it or
     *     substitute the "empty" warning defined
     *  b) it's a sendsms HTTP interface call: leave the message empty
     */
    if (octstr_len(msg->sms.msgdata) == 0 && msg->sms.sms_type == mt_reply) {
        if (trans != NULL && urltrans_omit_empty(trans))
            return 0;
        else
            msg->sms.msgdata = octstr_duplicate(reply_emptymessage);
    }

    if (trans == NULL) {
        header = NULL;
        footer = NULL;
        suffix = NULL;
        split_chars = NULL;
        catenate = 0;
    } else {
        header = urltrans_header(trans);
        footer = urltrans_footer(trans);
        suffix = urltrans_split_suffix(trans);
        split_chars = urltrans_split_chars(trans);
        catenate = urltrans_concatenation(trans);

        /*
         * If there hasn't been yet any DLR-URL set in the message
         * and we have configured values from the URLTranslation,
         * hence the 'group = sms-service' context group, then use
         * them in the message.
         */
        if (msg->sms.dlr_url == NULL &&
                (msg->sms.dlr_url = octstr_duplicate(urltrans_dlr_url(trans))) != NULL)
            msg->sms.dlr_mask = urltrans_dlr_mask(trans);
    }

    if (catenate)
        msg_sequence = counter_increase(catenated_sms_counter) & 0xFF;
    else
        msg_sequence = 0;

    list = sms_split(msg, header, footer, suffix, split_chars, catenate,
                     msg_sequence, max_msgs, sms_max_length);
    msg_count = gwlist_len(list);
    
    debug("sms", 0, "message length %ld, sending %ld messages",
          octstr_len(msg->sms.msgdata), msg_count);
    
    /*
     * In order to get catenated msgs work properly, we
     * have moved catenation to bearerbox.
     * So here we just need to put splitted msgs into one again and send
     * to bearerbox that will care about catenation.
     */
    if (catenate) {
        Msg *new_msg = msg_duplicate(msg);
        octstr_delete(new_msg->sms.msgdata, 0, octstr_len(new_msg->sms.msgdata));
        while((part = gwlist_extract_first(list)) != NULL) {
            octstr_append(new_msg->sms.msgdata, part->sms.msgdata);
            msg_destroy(part);
        }
        write_to_bearerbox(new_msg);
    } else {
        /* msgs are the independent parts so sent those as is */
        while ((part = gwlist_extract_first(list)) != NULL)
            write_to_bearerbox(part);
    }
    
    gwlist_destroy(list, NULL);

    return msg_count;
}


/***********************************************************************
 * Stuff to remember which receiver belongs to which HTTP query.
 * This also includes HTTP request data to queue a failed HTTP request
 * into the smsbox_http_request queue which is then handled by the
 * http_queue_thread thread on a re-scheduled time basis.
 */


static HTTPCaller *caller;
static Counter *num_outstanding_requests;
     

struct receiver {
    Msg *msg;
    URLTranslation *trans;
    int method;  /* the HTTP method to use */
    Octstr *url; /* the after pattern URL */
    List *http_headers; 
    Octstr *body; /* body content of the request */
    unsigned long retries; /* number of performed retries */
};

/*
 * Again no urltranslation when we got an answer to wap push - it can only be dlr.
 */
static void *remember_receiver(Msg *msg, URLTranslation *trans, int method,
                               Octstr *url, List *headers, Octstr *body,
                               unsigned int retries)
{
    struct receiver *receiver;

    counter_increase(num_outstanding_requests);
    receiver = gw_malloc(sizeof(*receiver));

    receiver->msg = msg_create(sms);

    receiver->msg->sms.sender = octstr_duplicate(msg->sms.sender);
    receiver->msg->sms.receiver = octstr_duplicate(msg->sms.receiver);
    /* ppg_service_name should always be not NULL here */
    if (trans != NULL && (msg->sms.service == NULL || ppg_service_name == NULL ||
        octstr_compare(msg->sms.service, ppg_service_name) != 0)) {
        receiver->msg->sms.service = octstr_duplicate(urltrans_name(trans));
    } else {
        receiver->msg->sms.service = octstr_duplicate(msg->sms.service);
    }
    receiver->msg->sms.smsc_id = octstr_duplicate(msg->sms.smsc_id);
    /* to remember if it's a DLR http get */
    receiver->msg->sms.sms_type = msg->sms.sms_type;

    receiver->trans = trans;

    /* remember the HTTP request if we need to queue this */
    receiver->method = method;
    receiver->url = octstr_duplicate(url);
    receiver->http_headers = http_header_duplicate(headers);
    receiver->body = octstr_duplicate(body);
    receiver->retries = retries;

    return receiver;
}


static void get_receiver(void *id, Msg **msg, URLTranslation **trans, int *method,
                         Octstr **url, List **headers, Octstr **body,
                         unsigned long *retries)
{
    struct receiver *receiver;

    receiver = id;
    *msg = receiver->msg;
    *trans = receiver->trans;
    *method = receiver->method;
    *url = receiver->url;
    *headers = receiver->http_headers;
    *body = receiver->body;
    *retries = receiver->retries;
    gw_free(receiver);
    counter_decrease(num_outstanding_requests);
}


static long outstanding_requests(void)
{
    return counter_value(num_outstanding_requests);
}


/***********************************************************************
 * Thread for receiving reply from HTTP query and sending it to phone.
 */


static void strip_prefix_and_suffix(Octstr *html, Octstr *prefix,
    	    	    	    	    Octstr *suffix)
{
    long prefix_end, suffix_start;

    if (prefix == NULL || suffix == NULL)
    	return;
    prefix_end = octstr_case_search(html, prefix, 0);
    if (prefix_end == -1)
        return;
    prefix_end += octstr_len(prefix);
    suffix_start = octstr_case_search(html, suffix, prefix_end);
    if (suffix_start == -1)
        return;
    octstr_delete(html, 0, prefix_end);
    octstr_truncate(html, suffix_start - prefix_end);
}


static void get_x_kannel_from_headers(List *headers, Octstr **from,
				      Octstr **to, Octstr **udh,
				      Octstr **user, Octstr **pass,
				      Octstr **smsc, int *mclass, int *mwi,
				      int *coding, int *compress, 
				      int *validity, int *deferred,
				      int *dlr_mask, Octstr **dlr_url, 
				      Octstr **account, int *pid, int *alt_dcs, 
				      int *rpi, Octstr **binfo, int *priority, Octstr **meta_data)
{
    Octstr *name, *val;
    long l;

    for(l=0; l<gwlist_len(headers); l++) {
	http_header_get(headers, l, &name, &val);

	if (octstr_case_compare(name, octstr_imm("X-Kannel-From")) == 0) {
	    *from = octstr_duplicate(val);
	    octstr_strip_blanks(*from);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-To")) == 0) {
	    *to = octstr_duplicate(val);
	    octstr_strip_blanks(*to);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Username")) == 0) {
	    if (user != NULL) {
		*user = octstr_duplicate(val);
		octstr_strip_blanks(*user);
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Password")) == 0) {
	    if (pass != NULL) {
		*pass = octstr_duplicate(val);
		octstr_strip_blanks(*pass);
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-SMSC")) == 0) {
	    if (smsc != NULL) {
		*smsc = octstr_duplicate(val);
		octstr_strip_blanks(*smsc);
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-UDH")) == 0) {
	    *udh = octstr_duplicate(val);
	    octstr_strip_blanks(*udh);
	    if (octstr_hex_to_binary(*udh) == -1) {
		if (octstr_url_decode(*udh) == -1) {
		    warning(0, "Invalid UDH received in X-Kannel-UDH");
		    octstr_destroy(*udh);
		    *udh = NULL;
		}
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-DLR-URL")) == 0) {
	    *dlr_url = octstr_duplicate(val);
	    octstr_strip_blanks(*dlr_url);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Account")) == 0) {
	    *account = octstr_duplicate(val);
	    octstr_strip_blanks(*account);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-BInfo")) == 0) {
            *binfo = octstr_duplicate(val);
            octstr_strip_blanks(*binfo);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Coding")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", coding);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-PID")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", pid);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-MWI")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", mwi);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-MClass")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", mclass);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Alt-DCS")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", alt_dcs);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Compress")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", compress);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Validity")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", validity);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Deferred")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", deferred);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-DLR-Mask")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", dlr_mask);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-RPI")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", rpi);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Priority")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", priority);
	}
        else if (octstr_case_compare(name, octstr_imm("X-Kannel-Meta-Data")) == 0) {
            *meta_data = octstr_duplicate(val);
            octstr_strip_blanks(*meta_data);
        }
	octstr_destroy(name);
	octstr_destroy(val);
    }
}

/* requesttype = mt_reply or mt_push. for example, auth is only read on mt_push
 * parse body and populate fields, including replacing body for <ud> value and
 * type to text/plain */
static void get_x_kannel_from_xml(int requesttype , Octstr **type, Octstr **body, 
                                  List *headers, Octstr **from,
                                  Octstr **to, Octstr **udh,
                                  Octstr **user, Octstr **pass,
                                  Octstr **smsc, int *mclass, int *mwi,
                                  int *coding, int *compress,
                                  int *validity, int *deferred,
                                  int *dlr_mask, Octstr **dlr_url,
                                  Octstr **account, int *pid, int *alt_dcs,
                                  int *rpi, List **tolist, Octstr **charset,
                                  Octstr **binfo, int *priority, Octstr **meta_data)
{                                    
    xmlDocPtr doc = NULL;
    xmlXPathContextPtr xpathCtx = NULL;
    xmlXPathObjectPtr xpathObj = NULL;
    xmlChar *xml_string;
    Octstr *text = NULL, *tmp = NULL;
    
    if (*body == NULL)
        return;

    debug("sms", 0, "XMLParsing: XML: <%s>", octstr_get_cstr(*body));

    /* ok, start parsing */
    doc = xmlParseMemory(octstr_get_cstr(*body), octstr_len(*body));
    if (doc == NULL) {
        error(0, "XMLParsing: Could not parse xmldoc: <%s>", octstr_get_cstr(*body));
        return;
    }
    xpathCtx = xmlXPathNewContext(doc);
    if (xpathCtx == NULL) {
        error(0, "XMLParsing: Could not create xpath context.");
        xmlFreeDoc(doc);
        return;
    }

#define XPATH_SEARCH_OCTSTR(path, var, nostrip)                                         \
    do {                                                                                \
        xpathObj = xmlXPathEvalExpression(BAD_CAST path, xpathCtx);                     \
        if (xpathObj != NULL && !xmlXPathNodeSetIsEmpty(xpathObj->nodesetval)) {        \
            xml_string = xmlXPathCastToString(xpathObj);                       \
            O_DESTROY(var);                                                             \
            var = octstr_create((const char*) xml_string);                              \
            if(nostrip == 0)                                                            \
                octstr_strip_blanks(var);                                               \
            xmlFree(xml_string);                                                        \
        }                                                                               \
        if (xpathObj != NULL) xmlXPathFreeObject(xpathObj);                             \
    } while(0)

#define XPATH_SEARCH_NUMBER(path, var)                                                  \
    do {                                                                                \
        xpathObj = xmlXPathEvalExpression(BAD_CAST path, xpathCtx);                     \
        if (xpathObj != NULL && !xmlXPathNodeSetIsEmpty(xpathObj->nodesetval)) {        \
            var = xmlXPathCastToNumber(xpathObj);                                       \
        }                                                                               \
        if (xpathObj != NULL) xmlXPathFreeObject(xpathObj);                             \
    } while(0)

    /* auth */
    xpathObj = xmlXPathEvalExpression(BAD_CAST "/message/submit/from", xpathCtx);
    if (xpathObj != NULL && !xmlXPathNodeSetIsEmpty(xpathObj->nodesetval)) {
        xmlXPathFreeObject(xpathObj);
        if(requesttype == mt_push) {
            /* user */
            XPATH_SEARCH_OCTSTR("/message/submit/from/user", *user, 0);
            XPATH_SEARCH_OCTSTR("/message/submit/from/username", *user, 0);

            /* pass */
            XPATH_SEARCH_OCTSTR("/message/submit/from/pass", *pass, 0);
            XPATH_SEARCH_OCTSTR("/message/submit/from/password", *pass, 0);
        }

        /* account */
        XPATH_SEARCH_OCTSTR("/message/submit/from/account", *account, 0);

        /* binfo */
        XPATH_SEARCH_OCTSTR("/message/submit/from/binfo", *binfo, 0);
    }

    XPATH_SEARCH_OCTSTR("/message/submit/oa/number", *from, 0);

    /* to (da/number) Multiple tags */
    xpathObj = xmlXPathEvalExpression(BAD_CAST "/message/submit/da/number/text()", xpathCtx);
    if (xpathObj != NULL && !xmlXPathNodeSetIsEmpty(xpathObj->nodesetval)) {
        int i;

        *tolist = gwlist_create();
        for (i = 0; i < xpathObj->nodesetval->nodeNr; i++) {
            if (xpathObj->nodesetval->nodeTab[i]->type != XML_TEXT_NODE)
                continue;
            xml_string = xmlXPathCastNodeToString(xpathObj->nodesetval->nodeTab[i]);
            tmp = octstr_create((const char*) xpathObj->nodesetval->nodeTab[i]->content);
            xmlFree(xml_string);
            octstr_strip_blanks(tmp);
            gwlist_append(*tolist, tmp);
        }
    }
    if (xpathObj != NULL)
        xmlXPathFreeObject(xpathObj);

    /* udh */
    XPATH_SEARCH_OCTSTR("/message/submit/udh", *udh, 0);
    if(*udh != NULL && octstr_hex_to_binary(*udh) == -1)
        octstr_url_decode(*udh);

    /* smsc */
    XPATH_SEARCH_OCTSTR("/message/submit/smsc", *smsc, 0);
    if (smsc == NULL)
        XPATH_SEARCH_OCTSTR("/message/submit/to", *smsc, 0);

    /* pid */
    XPATH_SEARCH_NUMBER("/message/submit/pid", *pid);

    /* rpi */
    XPATH_SEARCH_NUMBER("/message/submit/rpi", *rpi);

    /* dcs* (dcs/ *) */
    /* mclass (dcs/mclass) */
    XPATH_SEARCH_NUMBER("/message/submit/dcs/mclass", *mclass);
    /* mwi (dcs/mwi) */
    XPATH_SEARCH_NUMBER("/message/submit/dcs/mwi", *mwi);
    /* coding (dcs/coding) */
    XPATH_SEARCH_NUMBER("/message/submit/dcs/coding", *coding);
    /* compress (dcs/compress) */
    XPATH_SEARCH_NUMBER("/message/submit/dcs/compress", *compress);
    /* alt-dcs (dcs/alt-dcs) */
    XPATH_SEARCH_NUMBER("/message/submit/dcs/alt-dcs", *alt_dcs);


    /* statusrequest* (statusrequest/ *) */
    /* dlr-mask (statusrequest/dlr-mask) */
    XPATH_SEARCH_NUMBER("/message/submit/statusrequest/dlr-mask", *dlr_mask);
    /* dlr-url */
    XPATH_SEARCH_OCTSTR("/message/submit/statusrequest/dlr-url", *dlr_url, 0);

    /* validity (vp/delay) */
    XPATH_SEARCH_NUMBER("/message/submit/vp/delay", *validity);

    /* deferred (timing/delay) */
    XPATH_SEARCH_NUMBER("/message/submit/timing/delay", *deferred);

    /* priority */
    XPATH_SEARCH_NUMBER("/message/submit/priority", *priority);

    /* meta_data */
    XPATH_SEARCH_OCTSTR("/message/submit/meta-data", *meta_data, 0);
    
    /* charset from <?xml...encoding=?> */
    O_DESTROY(*charset);
    if (doc->encoding != NULL)
        *charset = octstr_create((const char*) doc->encoding);
    else
	*charset = octstr_create("UTF-8");

    /* text */
    XPATH_SEARCH_OCTSTR("/message/submit/ud", text, 0);
    if (text != NULL && octstr_hex_to_binary(text) == -1)
        octstr_url_decode(text);

    octstr_truncate(*body, 0);
    if(text != NULL) {
        octstr_append(*body, text);
        octstr_destroy(text);
    }

    O_DESTROY(*type);
    *type = octstr_create("text/plain");

    if (xpathCtx != NULL)
        xmlXPathFreeContext(xpathCtx);
    if (doc != NULL)
        xmlFreeDoc(doc);
}


static void fill_message(Msg *msg, URLTranslation *trans,
			 Octstr *replytext, Octstr *from, Octstr *to, Octstr *udh,
			 int mclass, int mwi, int coding, int compress,
			 int validity, int deferred,
			 Octstr *dlr_url, int dlr_mask, int pid, int alt_dcs,
			 int rpi, Octstr *smsc, Octstr *account,
			 Octstr *charset, Octstr *binfo, int priority, Octstr *meta_data)
{
    msg->sms.msgdata = replytext;
    msg->sms.time = time(NULL);

    if (charset)
    	msg->sms.charset = charset;

    if (dlr_url != NULL) {
    	if (urltrans_accept_x_kannel_headers(trans)) {
    	    octstr_destroy(msg->sms.dlr_url);
    	    msg->sms.dlr_url = dlr_url;
    	} else {
    	    warning(0, "Tried to change dlr_url to '%s', denied.",
    		    octstr_get_cstr(dlr_url));
    	    octstr_destroy(dlr_url);
    	}
    }

    if (smsc != NULL) {
    	if (urltrans_accept_x_kannel_headers(trans)) {
    	    octstr_destroy(msg->sms.smsc_id);
    	    msg->sms.smsc_id = smsc;
    	} else {
    	    warning(0, "Tried to change SMSC to '%s', denied.",
    		    octstr_get_cstr(smsc));
    	    octstr_destroy(smsc);
    	}
    }

    if (from != NULL) {
    	if (urltrans_accept_x_kannel_headers(trans)) {
    	    octstr_destroy(msg->sms.sender);
    	    msg->sms.sender = from;
    	} else {
    	    warning(0, "Tried to change sender to '%s', denied.",
    		    octstr_get_cstr(from));
    	    octstr_destroy(from);
    	}
    }
    if (to != NULL) {
    	if (urltrans_accept_x_kannel_headers(trans)) {
    	    octstr_destroy(msg->sms.receiver);
    	    msg->sms.receiver = to;
    	} else {
    	    warning(0, "Tried to change receiver to '%s', denied.",
    		    octstr_get_cstr(to));
    	    octstr_destroy(to);
    	}
    }
    if (udh != NULL) {
    	if (urltrans_accept_x_kannel_headers(trans)) {
    	    octstr_destroy(msg->sms.udhdata);
    	    msg->sms.udhdata = udh;
    	} else {
    	    warning(0, "Tried to set UDH field, denied.");
    	    O_DESTROY(udh);
    	}
    }
    if (mclass != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
        	msg->sms.mclass = mclass;
        else
        	warning(0, "Tried to set MClass field, denied.");
    }
    if (pid != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
        	msg->sms.pid = pid;
        else
        	warning(0, "Tried to set PID field, denied.");
    }
    if (rpi != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
        	msg->sms.rpi = rpi;
        else
        	warning(0, "Tried to set RPI field, denied.");
    }
    if (alt_dcs != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
        	msg->sms.alt_dcs = alt_dcs;
        else
        	warning(0, "Tried to set Alt-DCS field, denied.");
    }
    if (mwi != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
        	msg->sms.mwi = mwi;
        else
        	warning(0, "Tried to set MWI field, denied.");
    }
    if (coding != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
        	msg->sms.coding = coding;
        else
        	warning(0, "Tried to set Coding field, denied.");
    }
    if (compress != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
        	msg->sms.compress = compress;
        else
        	warning(0, "Tried to set Compress field, denied.");
    }
    /* Compatibility Mode */
    if (msg->sms.coding == DC_UNDEF) {
    	if(octstr_len(udh))
    		msg->sms.coding = DC_8BIT;
    	else
    		msg->sms.coding = DC_7BIT;
    }
    if (validity != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
            msg->sms.validity = validity * 60 + time(NULL);
        else
            warning(0, "Tried to change validity to '%d', denied.", validity);
    }
    if (deferred != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
            msg->sms.deferred = deferred * 60 + time(NULL);
        else
            warning(0, "Tried to change deferred to '%d', denied.", deferred);
    }
    if (dlr_mask != SMS_PARAM_UNDEFINED) {
    	if (urltrans_accept_x_kannel_headers(trans)) {
    		msg->sms.dlr_mask = dlr_mask;
    	} else
    		warning(0, "Tried to change dlr_mask to '%d', denied.", dlr_mask);
    }
    if (account) {
        if (urltrans_accept_x_kannel_headers(trans)) {
            msg->sms.account = account;
        } else {
            warning(0, "Tried to change account to '%s', denied.",
                    octstr_get_cstr(account));
            octstr_destroy(account);
        }
    }
    if (binfo) {
        if (urltrans_accept_x_kannel_headers(trans)) {
            msg->sms.binfo = binfo;
        } else {
            warning(0, "Tried to change billing info to '%s', denied.",
                    octstr_get_cstr(binfo));
            octstr_destroy(binfo);
        }
    }
    if (priority != SMS_PARAM_UNDEFINED) {
        if (urltrans_accept_x_kannel_headers(trans))
            msg->sms.priority = priority;
        else
            warning(0, "Tried to change priority to '%d', denied.", priority);
    }
    if (meta_data != NULL) {
        if (urltrans_accept_x_kannel_headers(trans)) {
            octstr_destroy(msg->sms.meta_data);
            msg->sms.meta_data = meta_data;
        } else {
            warning(0, "Tried to set Meta-Data field, denied.");
            octstr_destroy(meta_data);
        }
    }
}


/***********************************************************************
 * Thread to handle failed HTTP requests and retries to deliver the
 * information to the HTTP server. The thread uses the smsbox_http_requests
 * queue that is spooled by url_result_thread in case the HTTP requests
 * fails.
 */

static void http_queue_thread(void *arg)
{
    void *id;
    Msg *msg;
    URLTranslation *trans;
    Octstr *req_url;
    List *req_headers;
    Octstr *req_body;
    unsigned long retries;
    int method;
    TimerItem *i;

    while ((i = gwlist_consume(smsbox_http_requests)) != NULL) {
        /*
         * The timer thread has injected the item to retry the
         * HTTP call again now.
         */

        debug("sms.http",0,"HTTP: Queue contains %ld outstanding requests",
              gwlist_len(smsbox_http_requests));

        /*
         * Get all required HTTP request data from the queue and reconstruct
         * the id pointer for later lookup in url_result_thread.
         */
        get_receiver(i->id, &msg, &trans, &method, &req_url, &req_headers, &req_body, &retries);

        gw_timer_elapsed_destroy(i->timer);
        gw_free(i);

        if (retries < max_http_retries) {
            id = remember_receiver(msg, trans, method, req_url, req_headers, req_body, ++retries);

            debug("sms.http",0,"HTTP: Retrying request <%s> (%ld/%ld)",
                  octstr_get_cstr(req_url), retries, max_http_retries);

            /* re-queue this request to the HTTPCaller list */
            http_start_request(caller, method, req_url, req_headers, req_body,
                               1, id, NULL);
        }

        msg_destroy(msg);
        octstr_destroy(req_url);
        http_destroy_headers(req_headers);
        octstr_destroy(req_body);
    }
}


static void url_result_thread(void *arg)
{
    Octstr *final_url, *req_body, *type, *replytext;
    List *reply_headers;
    int status, method;
    void *id;
    Msg *msg;
    URLTranslation *trans;
    Octstr *req_url;
    List *req_headers;
    Octstr *text_html, *text_plain, *text_wml, *text_xml;
    Octstr *octet_stream;
    unsigned long retries;
    unsigned int queued; /* indicate if processes reply is re-queued */
    TimerItem *item;

    Octstr *reply_body, *charset, *alt_charset;
    Octstr *udh, *from, *to, *dlr_url, *account, *smsc, *binfo, *meta_data;
    int dlr_mask, mclass, mwi, coding, compress, pid, alt_dcs, rpi;
    int validity, deferred, priority;

    text_html = octstr_imm("text/html");
    text_wml = octstr_imm("text/vnd.wap.wml");
    text_plain = octstr_imm("text/plain");
    text_xml = octstr_imm("text/xml");
    octet_stream = octstr_imm("application/octet-stream");

    for (;;) {
        queued = 0;
        id = http_receive_result(caller, &status, &final_url, &reply_headers, &reply_body);
        semaphore_up(max_pending_requests);
        if (id == NULL)
            break;

        from = to = udh = smsc = dlr_url = account = binfo = charset
        		= alt_charset = meta_data = NULL;
        mclass = mwi = compress = pid = alt_dcs = rpi = dlr_mask =
        		validity = deferred = priority = SMS_PARAM_UNDEFINED;
        coding = DC_7BIT;

        get_receiver(id, &msg, &trans, &method, &req_url, &req_headers, &req_body, &retries);

        if (status == HTTP_OK || status == HTTP_ACCEPTED) {

            if (msg->sms.sms_type == report_mo) {
                /* we are done */
                goto requeued;
            }

            http_header_get_content_type(reply_headers, &type, &charset);
            if (octstr_case_compare(type, text_html) == 0 ||
                octstr_case_compare(type, text_wml) == 0) {
                if (trans != NULL)
                    strip_prefix_and_suffix(reply_body, urltrans_prefix(trans),
                                        urltrans_suffix(trans));
                replytext = html_to_sms(reply_body);
                octstr_strip_blanks(replytext);
                get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
                						  NULL, NULL, &smsc, &mclass, &mwi,
                						  &coding, &compress, &validity,
                						  &deferred, &dlr_mask, &dlr_url,
                						  &account, &pid, &alt_dcs, &rpi,
                						  &binfo, &priority, &meta_data);
            } else if (octstr_case_compare(type, text_plain) == 0) {
                replytext = octstr_duplicate(reply_body);
                octstr_destroy(reply_body);
                reply_body = NULL;
                get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
                		                  NULL, NULL, &smsc, &mclass, &mwi,
                		                  &coding, &compress, &validity,
                						  &deferred, &dlr_mask, &dlr_url,
                						  &account, &pid, &alt_dcs, &rpi,
                						  &binfo, &priority, &meta_data);
            } else if (octstr_case_compare(type, text_xml) == 0) {
                replytext = octstr_duplicate(reply_body);
                octstr_destroy(reply_body);
                reply_body = NULL;
                get_x_kannel_from_xml(mt_reply, &type, &replytext, reply_headers,
                					  &from, &to, &udh, NULL, NULL, &smsc, &mclass, &mwi,
                                      &coding, &compress, &validity, &deferred, &dlr_mask,
                                      &dlr_url, &account, &pid, &alt_dcs, &rpi, NULL, &charset,
                                      &binfo, &priority, &meta_data);
            } else if (octstr_case_compare(type, octet_stream) == 0) {
                replytext = octstr_duplicate(reply_body);
                octstr_destroy(reply_body);
                coding = DC_8BIT;
                reply_body = NULL;
                get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
                		                  NULL, NULL, &smsc, &mclass, &mwi,
                						  &coding, &compress, &validity,
                						  &deferred, &dlr_mask, &dlr_url,
                						  &account, &pid, &alt_dcs, &rpi,
                						  &binfo, &priority, &meta_data);
            } else {
                replytext = octstr_duplicate(reply_couldnotrepresent);
            }

            /*
             * If there was a charset specified in the HTTP response,
             * we're not going to touch the encoding. Otherwise check if
             * we have a defined alt-charset for this sms-service.
             */
            if (octstr_len(charset) == 0 &&
            		(alt_charset = urltrans_alt_charset(trans)) != NULL) {
            	octstr_destroy(charset);
            	charset = octstr_duplicate(alt_charset);
            }

            /*
             * Ensure now that we transcode to our internal encoding.
             */
            if (charset_processing(charset, replytext, coding) == -1) {
                replytext = octstr_duplicate(reply_couldnotrepresent);
            }
            octstr_destroy(type);
        } else if (max_http_retries > retries) {
            item = gw_malloc(sizeof(TimerItem));
            item->timer = gw_timer_create(timerset, smsbox_http_requests, NULL);
            item->id = remember_receiver(msg, trans, method, req_url,
                                         req_headers, req_body, retries);
            gw_timer_elapsed_start(item->timer, http_queue_delay, item);
            queued++;
            goto requeued;
        } else
            replytext = octstr_duplicate(reply_couldnotfetch);

        if (final_url == NULL)
            final_url = octstr_imm("");
        if (reply_body == NULL)
            reply_body = octstr_imm("");

        if (msg->sms.sms_type != report_mo) {
            fill_message(msg, trans, replytext, from, to, udh, mclass,
                         mwi, coding, compress, validity, deferred, dlr_url,
                         dlr_mask, pid, alt_dcs, rpi, smsc, account, charset,
                         binfo, priority, meta_data);

            alog("SMS HTTP-request sender:%s request: '%s' url: '%s' reply: %d '%s'",
                 octstr_get_cstr(msg->sms.receiver),
                 (msg->sms.msgdata != NULL) ? octstr_get_cstr(msg->sms.msgdata) : "",
                 octstr_get_cstr(final_url), status,
                 (status == HTTP_OK) ? "<< successful >>" : octstr_get_cstr(reply_body));
        } else {
            octstr_destroy(replytext);
        }

requeued:
        octstr_destroy(final_url);
        http_destroy_headers(reply_headers);
        octstr_destroy(reply_body);
        octstr_destroy(req_url);
        http_destroy_headers(req_headers);
        octstr_destroy(req_body);

        if (msg->sms.sms_type != report_mo && !queued) {
            if (send_message(trans, msg) < 0)
                error(0, "failed to send message to phone");
        }
        msg_destroy(msg);
    }
}


/***********************************************************************
 * Thread to receive SMS messages from bearerbox and obeying the requests
 * in them. HTTP requests are started in the background (another thread
 * will deal with the replies) and other requests are fulfilled directly.
 */


/*
 * Perform the service requested by the user: translate the request into
 * a pattern, if it is an URL, start its fetch and return 0, otherwise
 * return the string in `*result' and return 1. Return -1 for errors.
 * If we are translating url for ppg dlr, we do not use trans data
 * structure defined for sms services. This is indicated by trans = NULL.
 */
static int obey_request(Octstr **result, URLTranslation *trans, Msg *msg)
{
    Octstr *pattern, *xml, *tmp;
    List *request_headers;
    void *id;
    struct tm tm;
    char p[22];
    int type;
    FILE *f;

    gw_assert(msg != NULL);
    gw_assert(msg_type(msg) == sms);

    if (msg->sms.sms_type == report_mo)
    	type = TRANSTYPE_GET_URL;
    else
    	type = urltrans_type(trans);

    pattern = urltrans_get_pattern(trans, msg);
    gw_assert(pattern != NULL);

    switch (type) {
    case TRANSTYPE_TEXT:
    	debug("sms", 0, "formatted text answer: <%s>",
    		  octstr_get_cstr(pattern));
    	*result = pattern;
    	alog("SMS request sender:%s request: '%s' fixed answer: '%s'",
    		 octstr_get_cstr(msg->sms.receiver),
    		 octstr_get_cstr(msg->sms.msgdata),
    		 octstr_get_cstr(pattern));
    	break;

    case TRANSTYPE_FILE:
    	*result = octstr_read_file(octstr_get_cstr(pattern));
    	octstr_destroy(pattern);
    	alog("SMS request sender:%s request: '%s' file answer: '%s'",
    	     octstr_get_cstr(msg->sms.receiver),
    	     octstr_get_cstr(msg->sms.msgdata),
    	     octstr_get_cstr(*result));
    	break;

    case TRANSTYPE_EXECUTE:
        semaphore_down(max_pending_requests);
        debug("sms.exec", 0, "executing sms-service '%s'",
              octstr_get_cstr(pattern));
        if ((f = popen(octstr_get_cstr(pattern), "r")) != NULL) {
            octstr_destroy(pattern);
            *result = octstr_read_pipe(f);
            pclose(f);
            semaphore_up(max_pending_requests);
            alog("SMS request sender:%s request: '%s' file answer: '%s'",
                octstr_get_cstr(msg->sms.receiver),
                octstr_get_cstr(msg->sms.msgdata),
                octstr_get_cstr(*result));
        } else {
            error(0, "popen failed for '%s': %d: %s",
                  octstr_get_cstr(pattern), errno, strerror(errno));
            *result = NULL;
            octstr_destroy(pattern);
            return -1;
        }
        break;

    /*
     * No Kannel headers when we are sending dlrs to wap push
     */
    case TRANSTYPE_GET_URL:
    	request_headers = http_create_empty_headers();
        http_header_add(request_headers, "User-Agent", GW_NAME "/" GW_VERSION);
        if (trans != NULL) {
        	if (urltrans_send_sender(trans)) {
        		http_header_add(request_headers, "X-Kannel-From",
        				        octstr_get_cstr(msg->sms.receiver));
        	}
        }

    	id = remember_receiver(msg, trans, HTTP_METHOD_GET, pattern, request_headers, NULL, 0);
    	semaphore_down(max_pending_requests);
    	http_start_request(caller, HTTP_METHOD_GET, pattern, request_headers,
                           NULL, 1, id, NULL);
    	octstr_destroy(pattern);
    	http_destroy_headers(request_headers);
    	*result = NULL;
    	return 0;
    	break;

    case TRANSTYPE_POST_URL:
    	request_headers = http_create_empty_headers();
    	http_header_add(request_headers, "User-Agent", GW_NAME "/" GW_VERSION);
    	if (msg->sms.coding == DC_8BIT)
    	    http_header_add(request_headers, "Content-Type", "application/octet-stream");
    	else if(msg->sms.coding == DC_UCS2)
    		http_header_add(request_headers, "Content-Type", "text/plain; charset=\"UTF-16BE\"");
    	else {
    	    Octstr *header;
    	    header = octstr_create("text/plain");
    	    if (msg->sms.charset) {
    	        octstr_append(header, octstr_imm("; charset=\""));
    	        octstr_append(header, msg->sms.charset);
    	        octstr_append(header, octstr_imm("\""));
    	    } else {
    	    	octstr_append(header, octstr_imm("; charset=\"UTF-8\""));
    	    }
    	    http_header_add(request_headers, "Content-Type", octstr_get_cstr(header));
    	    O_DESTROY(header);
    	}
    	if (urltrans_send_sender(trans))
    	    http_header_add(request_headers, "X-Kannel-From",
    	    		        octstr_get_cstr(msg->sms.receiver));
    	http_header_add(request_headers, "X-Kannel-To",
    			        octstr_get_cstr(msg->sms.sender));

    	tm = gw_gmtime(msg->sms.time);
    	sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d",
    			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
    			tm.tm_hour, tm.tm_min, tm.tm_sec);
    	http_header_add(request_headers, "X-Kannel-Time", p);

    	tm = gw_gmtime(time(NULL));
    	sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d",
    			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
    			tm.tm_hour, tm.tm_min, tm.tm_sec);
    	http_header_add(request_headers, "Date", p); /* HTTP RFC 14.18 */

    	if (octstr_len(msg->sms.udhdata)) {
    	    Octstr *os;
    	    os = octstr_duplicate(msg->sms.udhdata);
    	    octstr_url_encode(os);
    	    http_header_add(request_headers, "X-Kannel-UDH", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (octstr_len(msg->sms.smsc_id)) {
    	    Octstr *os;
    	    os = octstr_duplicate(msg->sms.smsc_id);
    	    http_header_add(request_headers, "X-Kannel-SMSC", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}

    	if (msg->sms.mclass != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d",msg->sms.mclass);
    	    http_header_add(request_headers, "X-Kannel-MClass", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (msg->sms.pid != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d",msg->sms.pid);
    	    http_header_add(request_headers, "X-Kannel-PID", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (msg->sms.rpi != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d",msg->sms.rpi);
    	    http_header_add(request_headers, "X-Kannel-RPI", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (msg->sms.alt_dcs != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d",msg->sms.alt_dcs);
    	    http_header_add(request_headers, "X-Kannel-Alt-DCS", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (msg->sms.mwi != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d",msg->sms.mwi);
    	    http_header_add(request_headers, "X-Kannel-MWI", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (msg->sms.coding != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d",msg->sms.coding);
    	    http_header_add(request_headers, "X-Kannel-Coding",	octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (msg->sms.compress != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d",msg->sms.compress);
    	    http_header_add(request_headers, "X-Kannel-Compress", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (msg->sms.validity != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d", (msg->sms.validity - time(NULL)) / 60);
    	    http_header_add(request_headers, "X-Kannel-Validity", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (msg->sms.deferred != SMS_PARAM_UNDEFINED) {
    	    Octstr *os;
    	    os = octstr_format("%d", (msg->sms.deferred - time(NULL)) / 60);
    	    http_header_add(request_headers, "X-Kannel-Deferred", octstr_get_cstr(os));
    	    octstr_destroy(os);
    	}
    	if (octstr_len(msg->sms.service)) {
    	    http_header_add(request_headers, "X-Kannel-Service", octstr_get_cstr(msg->sms.service));
    	}
    	if (octstr_len(msg->sms.binfo)) {
    	    http_header_add(request_headers, "X-Kannel-BInfo", octstr_get_cstr(msg->sms.binfo));
    	}
    	if (octstr_len(msg->sms.meta_data)) {
    		http_header_add(request_headers, "X-Kannel-Meta-Data", octstr_get_cstr(msg->sms.meta_data));
    	}

    	id = remember_receiver(msg, trans, HTTP_METHOD_POST, pattern,
    			               request_headers, msg->sms.msgdata, 0);
    	semaphore_down(max_pending_requests);
    	http_start_request(caller, HTTP_METHOD_POST, pattern, request_headers,
     			           msg->sms.msgdata, 1, id, NULL);
    	octstr_destroy(pattern);
    	http_destroy_headers(request_headers);
    	*result = NULL;
    	return 0;
    	break;

    case TRANSTYPE_POST_XML:

    	/* XXX The first two chars are beeing eaten somewhere and
    	 * only sometimes - something must be ungry */

#define OCTSTR_APPEND_XML(xml, tag, text) \
        octstr_format_append(xml, "  \t\t<" tag ">%s</" tag ">\n", (text?octstr_get_cstr(text):""))

#define OCTSTR_APPEND_XML_OCTSTR(xml, tag, text) \
        do { \
            xmlDocPtr tmp_doc = xmlNewDoc(BAD_CAST "1.0"); \
            xmlChar *xml_escaped = NULL; \
            if (text != NULL) xml_escaped = xmlEncodeEntitiesReentrant(tmp_doc, BAD_CAST octstr_get_cstr(text)); \
            octstr_format_append(xml, "  \t\t<" tag ">%s</" tag ">\n", (xml_escaped != NULL ? (char*)xml_escaped : "")); \
            if (xml_escaped != NULL) xmlFree(xml_escaped); \
            xmlFreeDoc(tmp_doc); \
        } while(0)

#define OCTSTR_APPEND_XML_NUMBER(xml, tag, value)          \
        octstr_format_append(xml, "  \t\t<" tag ">%ld</" tag ">\n", (long) value)

    	request_headers = http_create_empty_headers();
    	http_header_add(request_headers, "User-Agent", GW_NAME "/" GW_VERSION);
    	if (msg->sms.coding == DC_UCS2) {
    	    http_header_add(request_headers, "Content-Type",
    			            "text/xml; charset=\"ISO-8859-1\""); /* for account and other strings */
    	} else {
    	    Octstr *header;
    	    header = octstr_create("text/xml");
    	    if(msg->sms.charset) {
        		octstr_append(header, octstr_imm("; charset=\""));
        		octstr_append(header, msg->sms.charset);
        		octstr_append(header, octstr_imm("\""));
    	    }
    	    http_header_add(request_headers, "Content-Type", octstr_get_cstr(header));
    	    O_DESTROY(header);
    	}

    	tm = gw_gmtime(time(NULL));
    	sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d",
    			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
    			tm.tm_hour, tm.tm_min, tm.tm_sec);
    	http_header_add(request_headers, "Date", p); /* HTTP RFC 14.18 */

    	xml = octstr_create("");
    	octstr_append(xml, octstr_imm("<?xml version=\"1.0\" encoding=\""));
    	if (msg->sms.coding == DC_UCS2 || msg->sms.charset == NULL)
    		octstr_append(xml, octstr_imm("ISO-8859-1"));
    	else
    	    octstr_append(xml, msg->sms.charset);
    	octstr_append(xml, octstr_imm("\"?>\n"));

    	/*
    	 * XXX  damn windows that breaks with this :
    	 * octstr_append(xml, octstr_imm("<!DOCTYPE message SYSTEM \"SMSmessage.dtd\">\n"));
    	 */
    	octstr_append(xml, octstr_imm("<message cid=\"1\">\n"));
    	octstr_append(xml, octstr_imm("\t<submit>\n"));

    	/* oa */
    	if (urltrans_send_sender(trans)) {
    	    tmp = octstr_create("");
    	    OCTSTR_APPEND_XML_OCTSTR(tmp, "number", msg->sms.receiver);
    	    OCTSTR_APPEND_XML(xml, "oa", tmp);
    	    octstr_destroy(tmp);
    	}

    	/* da */
    	tmp = octstr_create("");
    	OCTSTR_APPEND_XML_OCTSTR(tmp, "number", msg->sms.sender);
    	OCTSTR_APPEND_XML(xml, "da", tmp);
    	octstr_destroy(tmp);

    	/* udh */
    	if (octstr_len(msg->sms.udhdata)) {
    	    Octstr *t;
    	    t = octstr_duplicate(msg->sms.udhdata);
    	    octstr_url_encode(t);
    	    OCTSTR_APPEND_XML_OCTSTR(xml, "udh", t);
    	    octstr_destroy(t);
    	}

    	/* ud */
    	if (octstr_len(msg->sms.msgdata)) {
            octstr_url_encode(msg->sms.msgdata);
            OCTSTR_APPEND_XML_OCTSTR(xml, "ud", msg->sms.msgdata);
        }

    	/* pid */
    	if (msg->sms.pid != SMS_PARAM_UNDEFINED)
    	    OCTSTR_APPEND_XML_NUMBER(xml, "pid", msg->sms.pid);

    	/* rpi */
    	if (msg->sms.rpi != SMS_PARAM_UNDEFINED)
    	    OCTSTR_APPEND_XML_NUMBER(xml, "rpi", msg->sms.rpi);

    	/* dcs */
    	tmp = octstr_create("");
    	if (msg->sms.coding != SMS_PARAM_UNDEFINED)
    	    OCTSTR_APPEND_XML_NUMBER(tmp, "coding", msg->sms.coding);
    	if (msg->sms.mclass != SMS_PARAM_UNDEFINED)
    	    OCTSTR_APPEND_XML_NUMBER(tmp, "mclass", msg->sms.mclass);
    	if (msg->sms.alt_dcs != SMS_PARAM_UNDEFINED)
    	    OCTSTR_APPEND_XML_NUMBER(tmp, "alt-dcs", msg->sms.alt_dcs);
    	if (msg->sms.mwi != SMS_PARAM_UNDEFINED)
    	    OCTSTR_APPEND_XML_NUMBER(tmp, "mwi", msg->sms.mwi);
    	if (msg->sms.compress != SMS_PARAM_UNDEFINED)
    	    OCTSTR_APPEND_XML_NUMBER(tmp, "compress", msg->sms.compress);
    	if (octstr_len(tmp))
    	    OCTSTR_APPEND_XML(xml, "dcs", tmp);
    	octstr_destroy(tmp);

	/* deferred (timing/delay) */
	tmp = octstr_create("");
	if(msg->sms.deferred != SMS_PARAM_UNDEFINED)
	    OCTSTR_APPEND_XML_NUMBER(tmp, "delay", (msg->sms.deferred - time(NULL)) / 60);
	if(octstr_len(tmp))
	    OCTSTR_APPEND_XML(xml, "timing", tmp);
	octstr_destroy(tmp);

	/* validity (vp/delay) */
	tmp = octstr_create("");
	if(msg->sms.validity != SMS_PARAM_UNDEFINED)
	    OCTSTR_APPEND_XML_NUMBER(tmp, "delay", (msg->sms.validity - time(NULL)) / 60);
	if(octstr_len(tmp))
	    OCTSTR_APPEND_XML(xml, "vp", tmp);
	octstr_destroy(tmp);

    	/* time (at) */
    	tm = gw_gmtime(msg->sms.time);
    	tmp = octstr_format("<year>%04d</year><month>%02d</month>"
    						"<day>%02d</day><hour>%02d</hour><minute>%02d</minute>"
    						"<second>%02d</second><timezone>0</timezone>",
    						tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
    						tm.tm_hour, tm.tm_min, tm.tm_sec);
    	OCTSTR_APPEND_XML(xml, "at", tmp);
    	octstr_destroy(tmp);

    	/* smsc */
    	if (octstr_len(msg->sms.smsc_id)) {
    	    tmp = octstr_create("");
    	    if (octstr_len(msg->sms.smsc_id))
    	    	OCTSTR_APPEND_XML_OCTSTR(tmp, "account", msg->sms.smsc_id);
    	    if (octstr_len(tmp))
                OCTSTR_APPEND_XML(xml, "from", tmp);
    	    O_DESTROY(tmp);
    	}

    	/* service = to/service */
    	if (octstr_len(msg->sms.service)) {
    	    tmp = octstr_create("");
    	    OCTSTR_APPEND_XML_OCTSTR(tmp, "service", msg->sms.service);
    	    if (octstr_len(tmp))
                OCTSTR_APPEND_XML(xml, "to", tmp);
    	    O_DESTROY(tmp);
    	}

    	/* meta_data */
    	if (octstr_len(msg->sms.meta_data)) {
    		OCTSTR_APPEND_XML_OCTSTR(xml, "meta-data", msg->sms.meta_data);
    	}

    	/* End XML */
    	octstr_append(xml, octstr_imm("\t</submit>\n"));
    	octstr_append(xml, octstr_imm("</message>\n"));

    	if (msg->sms.msgdata != NULL)
    	    octstr_destroy(msg->sms.msgdata);

    	msg->sms.msgdata = xml;

    	debug("sms", 0, "XMLBuild: XML: <%s>", octstr_get_cstr(msg->sms.msgdata));
    	id = remember_receiver(msg, trans, HTTP_METHOD_POST, pattern,
    			               request_headers, msg->sms.msgdata, 0);
    	semaphore_down(max_pending_requests);
    	http_start_request(caller, HTTP_METHOD_POST, pattern, request_headers,
    			           msg->sms.msgdata, 1, id, NULL);
    	octstr_destroy(pattern);
    	http_destroy_headers(request_headers);
    	*result = NULL;
    	return 0;
    	break;

    case TRANSTYPE_SENDSMS:
    	error(0, "Got URL translation type SENDSMS for incoming message.");
    	alog("SMS request sender:%s request: '%s' FAILED bad translation",
    	     octstr_get_cstr(msg->sms.receiver),
    	     octstr_get_cstr(msg->sms.msgdata));
    	octstr_destroy(pattern);
    	return -1;
    	break;

    default:
    	error(0, "Unknown URL translation type %d", urltrans_type(trans));
    	alog("SMS request sender:%s request: '%s' FAILED unknown translation",
    	     octstr_get_cstr(msg->sms.receiver),
    	     octstr_get_cstr(msg->sms.msgdata));
    	octstr_destroy(pattern);
    	return -1;
    	break;
    }

    return 1;
}

static void obey_request_thread(void *arg)
{
    Msg *msg, *mack, *reply_msg;
    Octstr *tmp, *reply;
    URLTranslation *trans;
    Octstr *p;
    int ret, dreport=0;

    while ((msg = gwlist_consume(smsbox_requests)) != NULL) {

    	if (msg->sms.sms_type == report_mo)
    	    dreport = 1;
    	else
    	    dreport = 0;

    	/* Recode to UTF-8 the MO message if possible */
    	if (mo_recode && msg->sms.coding == DC_UCS2) {
    	    Octstr *text;

    	    text = octstr_duplicate(msg->sms.msgdata);
    	    if (octstr_recode(octstr_imm("UTF-8"), octstr_imm("UTF-16BE"), text) == 0) {
                info(0, "MO message converted from UCS-2 to UTF-8");
                octstr_destroy(msg->sms.msgdata);
                msg->sms.msgdata = octstr_duplicate(text);
                msg->sms.charset = octstr_create("UTF-8");
                msg->sms.coding = DC_7BIT;
    	    }
    	    octstr_destroy(text);
    	}

    	if (octstr_len(msg->sms.sender) == 0 ||
    			octstr_len(msg->sms.receiver) == 0) {
    	    error(0, "smsbox_req_thread: no sender/receiver, dump follows:");
    	    msg_dump(msg, 0);
            /*
             * Send NACK to bearerbox, otherwise message remains in store file.
             */
            mack = msg_create(ack);
            mack->ack.nack = ack_failed;
            mack->ack.time = msg->sms.time;
            uuid_copy(mack->ack.id, msg->sms.id);
            write_to_bearerbox(mack);

    	    msg_destroy(msg);
    	    continue;
    	}

    	/* create ack message to be sent afterwards */
    	mack = msg_create(ack);
    	mack->ack.nack = ack_success;
    	mack->ack.time = msg->sms.time;
    	uuid_copy(mack->ack.id, msg->sms.id);

        /*
         * no smsbox services when we are doing ppg dlr - so trans would be
         * NULL in this case.
         */
        if (dreport) {
            if (msg->sms.service == NULL || (msg->sms.service != NULL &&
            		ppg_service_name != NULL &&
            		octstr_compare(msg->sms.service, ppg_service_name) == 0)) {
            	trans = NULL;
            } else {
                trans = urltrans_find_service(translations, msg);
            }

    	    info(0, "Starting delivery report <%s> from <%s>",
    		octstr_get_cstr(msg->sms.service),
    		octstr_get_cstr(msg->sms.sender));

        } else {
    	    trans = urltrans_find(translations, msg);
    	    if (trans == NULL) {
        		warning(0, "No translation found for <%s> from <%s> to <%s>",
            		    octstr_get_cstr(msg->sms.msgdata),
            		    octstr_get_cstr(msg->sms.sender),
            		    octstr_get_cstr(msg->sms.receiver));
        		sms_swap(msg);
        		goto error;
    	    }

    	    info(0, "Starting to service <%s> from <%s> to <%s>",
    	    	 octstr_get_cstr(msg->sms.msgdata),
    	    	 octstr_get_cstr(msg->sms.sender),
    	    	 octstr_get_cstr(msg->sms.receiver));

    	    /*
    	     * Transcode to an alt-charset encoding if requested for sms-service.
    	     * This ensures that legacy systems using Kannel 1.4.1 which used
    	     * latin1 as internal encoding can issue the same content to the
    	     * application servers.
    	     */
    	    tmp = urltrans_alt_charset(trans);
    	    if (tmp != NULL && msg->sms.coding == DC_7BIT) {
    	        if (charset_convert(msg->sms.msgdata, "UTF-8", octstr_get_cstr(tmp)) != 0) {
    	            error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
    	                  "UTF-8", octstr_get_cstr(tmp));
    	        }
    	    }

    	    /*
    	     * now, we change the sender (receiver now 'cause we swap them later)
    	     * if faked-sender or similar set. Note that we ignore if the
    	     * replacement fails.
    	     */
    	    tmp = octstr_duplicate(msg->sms.sender);

    	    p = urltrans_faked_sender(trans);
    	    if (p != NULL) {
    	    	octstr_destroy(msg->sms.sender);
    	    	msg->sms.sender = octstr_duplicate(p);
    	    } else if (global_sender != NULL) {
    	    	octstr_destroy(msg->sms.sender);
    	    	msg->sms.sender = octstr_duplicate(global_sender);
    	    } else {
    	    	octstr_destroy(msg->sms.sender);
    	    	msg->sms.sender = octstr_duplicate(msg->sms.receiver);
    	    }
    	    octstr_destroy(msg->sms.receiver);
    	    msg->sms.receiver = tmp;
    	    msg->sms.sms_type = mt_reply;
        }

        /* TODO: check if the sender is approved to use this service */

        if (msg->sms.service == NULL && trans != NULL)
        	msg->sms.service = octstr_duplicate(urltrans_name(trans));
        ret = obey_request(&reply, trans, msg);
        if (ret != 0) {
        	if (ret == -1) {
error:
				error(0, "request failed");
		        /* XXX this can be something different, according to
		           urltranslation */
		        reply = octstr_duplicate(reply_requestfailed);
		        trans = NULL;	/* do not use any special translation */
        	}
        	if (!dreport) {
                /* create reply message */
                reply_msg = msg_create(sms);
                reply_msg->sms.sms_type = mt_reply;
                reply_msg->sms.sender = msg->sms.sender;
                msg->sms.sender = NULL;
                reply_msg->sms.receiver = msg->sms.receiver;
                msg->sms.receiver = NULL;
                reply_msg->sms.smsc_id = msg->sms.smsc_id;
                msg->sms.smsc_id = NULL;
                reply_msg->sms.msgdata = reply;
                reply_msg->sms.time = time(NULL);	/* set current time */

                /* send message */
                if (send_message(trans, reply_msg) < 0)
                    error(0, "request_thread: failed");

                /* cleanup */
                msg_destroy(reply_msg);
        	}
        }

        write_to_bearerbox(mack); /* implicit msg_destroy */

        msg_destroy(msg);
    }
}


/***********************************************************************
 * HTTP sendsms interface.
 */


#ifdef HAVE_SECURITY_PAM_APPL_H /*Module for pam authentication */

/*
 * Use PAM (Pluggable Authentication Module) to check sendsms authentication.
 */

typedef const struct pam_message pam_message_type;

static const char *PAM_username;
static const char *PAM_password;

static int PAM_conv (int num_msg, pam_message_type **msg,
		     struct pam_response **resp,
		     void *appdata_ptr)
{
    int count = 0, replies = 0;
    struct pam_response *repl = NULL;
    int size = sizeof(struct pam_response);

#define GET_MEM \
	repl = gw_realloc(repl, size); \
	size += sizeof(struct pam_response)
#define COPY_STRING(s) (s) ? gw_strdup(s) : NULL

    for (count = 0; count < num_msg; count++) {
	switch (msg[count]->msg_style) {
	case PAM_PROMPT_ECHO_ON:
	    GET_MEM;
	    repl[replies].resp_retcode = PAM_SUCCESS;
	    repl[replies++].resp = COPY_STRING(PAM_username);
	    /* PAM frees resp */
	    break;

	case PAM_PROMPT_ECHO_OFF:
	    GET_MEM;
	    repl[replies].resp_retcode = PAM_SUCCESS;
	    repl[replies++].resp = COPY_STRING(PAM_password);
	    /* PAM frees resp */
	    break;

	case PAM_TEXT_INFO:
	    warning(0, "unexpected message from PAM: %s", msg[count]->msg);
	    break;

	case PAM_ERROR_MSG:
	default:
	    /* Must be an error of some sort... */
	    error(0, "unexpected error from PAM: %s", msg[count]->msg);
	    gw_free(repl);
	    return PAM_CONV_ERR;
	}
    }
    if (repl)
	*resp = repl;
    return PAM_SUCCESS;
}

static struct pam_conv PAM_conversation = {
    &PAM_conv,
    NULL
};


static int authenticate(const char *login, const char *passwd)
{
    pam_handle_t *pamh;
    int pam_error;
    
    PAM_username = login;
    PAM_password = passwd;
    
    pam_error = pam_start("kannel", login, &PAM_conversation, &pamh);
    if (pam_error != PAM_SUCCESS ||
        (pam_error = pam_authenticate(pamh, 0)) != PAM_SUCCESS) {
	pam_end(pamh, pam_error);
	return 0;
    }
    pam_end(pamh, PAM_SUCCESS);
    info(0, "sendsms used by <%s>", login);
    return 1;
}


/*
 * Check for matching username and password for requests.
 * Return an URLTranslation if successful NULL otherwise.
 */

static int pam_authorise_user(List *list) 
{
    Octstr *val, *user = NULL;
    char *pwd, *login;
    int result;

    if ((user = http_cgi_variable(list, "user")) == NULL &&
        (user = http_cgi_variable(list, "username"))==NULL)
	return 0;
    login = octstr_get_cstr(user);
    
    if ((val = http_cgi_variable(list, "password")) == NULL &&
        (val = http_cgi_variable(list, "pass")) == NULL)
	return 0;

    pwd = octstr_get_cstr(val);
    result = authenticate(login, pwd);
    
    return result;
}

#endif /* HAVE_SECURITY_PAM_APPL_H */




static Octstr* store_uuid(Msg *msg)
{
    char id[UUID_STR_LEN + 1];
    Octstr *stored_uuid;

    gw_assert(msg != NULL);
    gw_assert(!immediate_sendsms_reply);
    
    uuid_unparse(msg->sms.id, id);
    stored_uuid = octstr_create(id);

    debug("sms.http", 0, "Stored UUID %s", octstr_get_cstr(stored_uuid));

    /* this octstr is then used to store the HTTP client into 
     * client_dict, if need to, in sendsms_thread */

    return stored_uuid;
}



static Octstr *smsbox_req_handle(URLTranslation *t, Octstr *client_ip,
				 HTTPClient *client,
				 Octstr *from, Octstr *to, Octstr *text, 
				 Octstr *charset, Octstr *udh, Octstr *smsc,
				 int mclass, int mwi, int coding, int compress, 
				 int validity, int deferred,
				 int *status, int dlr_mask, Octstr *dlr_url, 
				 Octstr *account, int pid, int alt_dcs, int rpi,
				 List *receiver, Octstr *binfo, int priority, Octstr *meta_data)
{				     
    Msg *msg = NULL;
    Octstr *newfrom = NULL;
    Octstr *returnerror = NULL;
    Octstr *receiv;
    Octstr *stored_uuid = NULL;
    List *failed_id = NULL;
    List *allowed = NULL;
    List *denied = NULL;
    int no_recv, ret = 0, i;

    /*
     * Multi-cast messages with several receivers in 'to' are handled
     * in a loop. We only change sms.time and sms.receiver within the
     * loop below, because everything else is identical for all receivers.
     * If receiver is not null, to list is already present on it
     */
    if(receiver == NULL) {
        receiver = octstr_split_words(to);
    }
    no_recv = gwlist_len(receiver);

    /*
     * check if UDH length is legal, or otherwise discard the
     * message, to prevent intentional buffer overflow schemes
     */
    if (udh != NULL && (octstr_len(udh) != octstr_get_char(udh, 0) + 1)) {
        returnerror = octstr_create("UDH field misformed, rejected");
        goto field_error;
    }
    if (udh != NULL && octstr_len(udh) > MAX_SMS_OCTETS) {
        returnerror = octstr_create("UDH field is too long, rejected");
        goto field_error;
    }

    /*
     * Check for white and black lists, first for the URLTranlation
     * lists and then for the global lists.
     *
     * Set the 'allowed' and 'denied' lists accordingly to process at
     * least all allowed receiver messages. This is a constrain
     * walk through all disallowing rules within the lists.
     */
    allowed = gwlist_create();
    denied = gwlist_create();

    for (i = 0; i < no_recv; i++) {
        receiv = gwlist_get(receiver, i); 
            
	/*
	 * Check if there are any illegal characters in the 'to' scheme
	 */
	if (strspn(octstr_get_cstr(receiv), sendsms_number_chars) < octstr_len(receiv)) {
	    info(0,"Illegal characters in 'to' string ('%s') vs '%s'",
		octstr_get_cstr(receiv), sendsms_number_chars);
            gwlist_append_unique(denied, receiv, octstr_item_match);
	}

        /*
         * First of all fill the two lists systematicaly by the rules,
         * then we will revice the lists.
         */
        if (urltrans_white_list(t) &&
            numhash_find_number(urltrans_white_list(t), receiv) < 1) {
            info(0, "Number <%s> is not in white-list, message discarded",
                 octstr_get_cstr(receiv));
            gwlist_append_unique(denied, receiv, octstr_item_match);
        } else {
            gwlist_append_unique(allowed, receiv, octstr_item_match);
        }

        if (urltrans_white_list_regex(t) &&
                gw_regex_match_pre(urltrans_white_list_regex(t), receiv) == 0) {
            info(0, "Number <%s> is not in white-list-regex, message discarded",
                 octstr_get_cstr(receiv));
            gwlist_append_unique(denied, receiv, octstr_item_match);
        } else {
            gwlist_append_unique(allowed, receiv, octstr_item_match);
        }
        
        if (urltrans_black_list(t) &&
            numhash_find_number(urltrans_black_list(t), receiv) == 1) {
            info(0, "Number <%s> is in black-list, message discarded",
                 octstr_get_cstr(receiv));
            gwlist_append_unique(denied, receiv, octstr_item_match);
        } else {
            gwlist_append_unique(allowed, receiv, octstr_item_match);
        }

        if (urltrans_black_list_regex(t) &&
                gw_regex_match_pre(urltrans_black_list_regex(t), receiv) == 1) {
            info(0, "Number <%s> is in black-list-regex, message discarded",
                 octstr_get_cstr(receiv));
            gwlist_append_unique(denied, receiv, octstr_item_match);
        } else {
            gwlist_append_unique(allowed, receiv, octstr_item_match);
        }
        

        if (white_list &&
            numhash_find_number(white_list, receiv) < 1) {
            info(0, "Number <%s> is not in global white-list, message discarded",
                 octstr_get_cstr(receiv));
            gwlist_append_unique(denied, receiv, octstr_item_match);
        } else {
            gwlist_append_unique(allowed, receiv, octstr_item_match);
        }

        if (white_list_regex &&
            gw_regex_match_pre(white_list_regex, receiv) == 0) {
            info(0, "Number <%s> is not in global white-list-regex, message discarded",
                 octstr_get_cstr(receiv));
            gwlist_append_unique(denied, receiv, octstr_item_match);
        } else {
            gwlist_append_unique(allowed, receiv, octstr_item_match);
        }

        if (black_list &&
            numhash_find_number(black_list, receiv) == 1) {
            info(0, "Number <%s> is in global black-list, message discarded",
                 octstr_get_cstr(receiv));
            gwlist_append_unique(denied, receiv, octstr_item_match);
        } else {
            gwlist_append_unique(allowed, receiv, octstr_item_match);
        }

        if (black_list_regex &&
            gw_regex_match_pre(black_list_regex, receiv) == 1) {
            info(0, "Number <%s> is in global black-list-regex, message discarded",
                 octstr_get_cstr(receiv));
            gwlist_append_unique(denied, receiv, octstr_item_match);
        } else {
            gwlist_append_unique(allowed, receiv, octstr_item_match);
        }
    }
    
    /*
     * Now we have to revise the 'allowed' and 'denied' lists by walking
     * the 'denied' list and check if items are also present in 'allowed',
     * then we will discard them from 'allowed'.
     */
    for (i = 0; i < gwlist_len(denied); i++) {
        receiv = gwlist_get(denied, i);
        gwlist_delete_matching(allowed, receiv, octstr_item_match);
    }

    /* have all receivers been denied by list rules?! */
    if (gwlist_len(allowed) == 0) {
        returnerror = octstr_create("Number(s) has/have been denied by white- and/or black-lists.");
        goto field_error;
    }

    if (urltrans_faked_sender(t) != NULL) {
	/* discard previous from */
	newfrom = octstr_duplicate(urltrans_faked_sender(t));
    } else if (octstr_len(from) > 0) {
	newfrom = octstr_duplicate(from);
    } else if (urltrans_default_sender(t) != NULL) {
	newfrom = octstr_duplicate(urltrans_default_sender(t));
    } else if (global_sender != NULL) {
	newfrom = octstr_duplicate(global_sender);
    } else {
	returnerror = octstr_create("Sender missing and no global set, rejected");
	goto field_error;
    }

    info(0, "sendsms sender:<%s:%s> (%s) to:<%s> msg:<%s>",
         octstr_get_cstr(urltrans_username(t)),
         octstr_get_cstr(newfrom),
         octstr_get_cstr(client_ip),
         ( to == NULL ? "multi-cast" : octstr_get_cstr(to) ),
         ( text == NULL ? "" : octstr_get_cstr(text) ));
    
    /*
     * Create the msg structure and fill the types. Note that sms.receiver
     * and sms.time are set in the multi-cast support loop below.
     */
    msg = msg_create(sms);
    
    msg->sms.service = octstr_duplicate(urltrans_name(t));
    msg->sms.sms_type = mt_push;
    msg->sms.sender = octstr_duplicate(newfrom);
    if(octstr_len(account)) {
	if(octstr_len(account) <= ACCOUNT_MAX_LEN && 
	   octstr_search_chars(account, octstr_imm("[]\n\r"), 0) == -1) {
	    msg->sms.account = account ? octstr_duplicate(account) : NULL;
	} else {
	    returnerror = octstr_create("Account field misformed or too long, rejected");
	    goto field_error;
	}
    }
    msg->sms.msgdata = text ? octstr_duplicate(text) : octstr_create("");
    msg->sms.udhdata = udh ? octstr_duplicate(udh) : octstr_create("");
    
    if (octstr_len(binfo))
        msg->sms.binfo = octstr_duplicate(binfo);

    if(octstr_len(dlr_url)) {
	if(octstr_len(dlr_url) < 8) { /* http(s):// */
	    returnerror = octstr_create("DLR-URL field misformed, rejected");
	    goto field_error;
	} else {
	    Octstr *tmp;
	    tmp = octstr_copy(dlr_url, 0, 7);
	    if(octstr_case_compare(tmp, octstr_imm("http://")) == 0) {
		msg->sms.dlr_url = octstr_duplicate(dlr_url);
	    } else {
		O_DESTROY(tmp);
		tmp = octstr_copy(dlr_url, 0, 8);
		if(octstr_case_compare(tmp, octstr_imm("https://")) != 0) {
		    returnerror = octstr_create("DLR-URL field misformed, rejected");
		    O_DESTROY(tmp);
		    goto field_error;
		}
#ifdef HAVE_LIBSSL
		msg->sms.dlr_url = octstr_duplicate(dlr_url);
#else /* HAVE_LIBSSL */
		else {    
		    warning(0, "DLR-URL with https but SSL not supported, url is <%s>",
			    octstr_get_cstr(dlr_url));
		}
#endif /* HAVE_LIBSSL */
	    }
	    O_DESTROY(tmp);
	}
    } else {
	msg->sms.dlr_url = octstr_create("");
    }

    if ( dlr_mask < -1 || dlr_mask > 63 ) { /* 00111111 */
	returnerror = octstr_create("DLR-Mask field misformed, rejected");
	goto field_error;
    }
    msg->sms.dlr_mask = dlr_mask;
    
    if ( mclass < -1 || mclass > 3 ) {
	returnerror = octstr_create("MClass field misformed, rejected");
	goto field_error;
    }
    msg->sms.mclass = mclass;
    
    if ( pid < -1 || pid > 255 ) {
	returnerror = octstr_create("PID field misformed, rejected");
	goto field_error;
    }
    msg->sms.pid = pid;

    if ( rpi < -1 || rpi > 2) {
	returnerror = octstr_create("RPI field misformed, rejected");
	goto field_error;
    }
    msg->sms.rpi = rpi;
    
    if ( alt_dcs < -1 || alt_dcs > 1 ) {
	returnerror = octstr_create("Alt-DCS field misformed, rejected");
	goto field_error;
    }
    msg->sms.alt_dcs = alt_dcs;
    
    if ( mwi < -1 || mwi > 7 ) {
	returnerror = octstr_create("MWI field misformed, rejected");
	goto field_error;
    }
    msg->sms.mwi = mwi;

    if ( coding < -1 || coding > 2 ) {
	returnerror = octstr_create("Coding field misformed, rejected");
	goto field_error;
    }
    msg->sms.coding = coding;

    if ( compress < -1 || compress > 1 ) {
	returnerror = octstr_create("Compress field misformed, rejected");
	goto field_error;
    }
    msg->sms.compress = compress;

    /* Compatibility Mode */
    if ( msg->sms.coding == DC_UNDEF) {
	if(octstr_len(udh))
	  msg->sms.coding = DC_8BIT;
	else
	  msg->sms.coding = DC_7BIT;
    }
	

    if (validity < -1) {
        returnerror = octstr_create("Validity field misformed, rejected");
        goto field_error;
    } else if (validity != SMS_PARAM_UNDEFINED)
    	msg->sms.validity = validity * 60 + time(NULL);

    if (deferred < -1) {
        returnerror = octstr_create("Deferred field misformed, rejected");
        goto field_error;
    } else if (deferred != SMS_PARAM_UNDEFINED)
    	msg->sms.deferred = deferred * 60 + time(NULL);
    
    if (priority != SMS_PARAM_UNDEFINED && (priority < 0 || priority > 3)) {
        returnerror = octstr_create("Priority field misformed, rejected");
        goto field_error;
    }
    msg->sms.priority = priority;


    /* new smsc-id argument - we should check this one, if able,
       but that's advanced logics -- Kalle */
    
    if (urltrans_forced_smsc(t)) {
	msg->sms.smsc_id = octstr_duplicate(urltrans_forced_smsc(t));
	if (smsc)
	    info(0, "send-sms request smsc id ignored, "
	    	    "as smsc id forced to %s",
		    octstr_get_cstr(urltrans_forced_smsc(t)));
    } else if (smsc) {
	msg->sms.smsc_id = octstr_duplicate(smsc);
    } else if (urltrans_default_smsc(t)) {
	msg->sms.smsc_id = octstr_duplicate(urltrans_default_smsc(t));
    } else
	msg->sms.smsc_id = NULL;

    if (charset_processing(charset, msg->sms.msgdata, msg->sms.coding) == -1) {
	returnerror = octstr_create("Charset or body misformed, rejected");
	goto field_error;
    }

    msg->sms.meta_data = octstr_duplicate(meta_data);

    msg->sms.receiver = NULL;

    /* 
     * All checks are done, now add multi-cast request support by
     * looping through 'allowed'. This should work for any
     * number of receivers within 'to'. If the message fails append
     * it to 'failed_id'.
     */
    failed_id = gwlist_create();

    if (!immediate_sendsms_reply) {
        stored_uuid = store_uuid(msg);
        dict_put(client_dict, stored_uuid, client);
    }

    while ((receiv = gwlist_extract_first(allowed)) != NULL) {

	O_DESTROY(msg->sms.receiver);
        msg->sms.receiver = octstr_duplicate(receiv);

        msg->sms.time = time(NULL);
        /* send the message and return number of splits */
        ret = send_message(t, msg);

        if (ret == -1) {
            /* add the receiver to the failed list */
            gwlist_append(failed_id, receiv);
        } else {
            /* log the sending as successful for this particular message */
            alog("send-SMS request added - sender:%s:%s %s target:%s request: '%s'",
	             octstr_get_cstr(urltrans_username(t)),
                 octstr_get_cstr(newfrom), octstr_get_cstr(client_ip),
	             octstr_get_cstr(receiv),
	             udh == NULL ? ( text == NULL ? "" : octstr_get_cstr(text) ) : "<< UDH >>");
        }
    }

    if (gwlist_len(failed_id) > 0)
	goto transmit_error;
    
    *status = HTTP_ACCEPTED;
    returnerror = octstr_create("Sent.");

    /* 
     * Append all denied receivers to the returned body in case this is
     * a multi-cast send request
     */
    if (gwlist_len(denied) > 0) {
        octstr_format_append(returnerror, " Denied receivers are:");
        while ((receiv = gwlist_extract_first(denied)) != NULL) {
            octstr_format_append(returnerror, " %s", octstr_get_cstr(receiv));
        }
    }

    /*
     * Append number of splits to returned body. 
     * This may be used by the calling client.
     */
    if (ret > 1) 
        octstr_format_append(returnerror, " Message splits: %d", ret);

cleanup:
    octstr_destroy(stored_uuid);
    gwlist_destroy(failed_id, NULL);
    gwlist_destroy(allowed, NULL);
    gwlist_destroy(denied, NULL);
    gwlist_destroy(receiver, octstr_destroy_item);
    octstr_destroy(newfrom);
    msg_destroy(msg);

    return returnerror;
    

field_error:
    alog("send-SMS request failed - %s",
            octstr_get_cstr(returnerror));
    *status = HTTP_BAD_REQUEST;

    goto cleanup;

transmit_error:
    error(0, "sendsms_request: failed");
    *status = HTTP_INTERNAL_SERVER_ERROR;
    returnerror = octstr_create("Sending failed.");

    if (!immediate_sendsms_reply)
        dict_remove(client_dict, stored_uuid);

    /* 
     * Append all receivers to the returned body in case this is
     * a multi-cast send request
     */
    if (no_recv > 1) {
        octstr_format_append(returnerror, " Failed receivers are:");
        while ((receiv = gwlist_extract_first(failed_id)) != NULL) {
            octstr_format_append(returnerror, " %s", octstr_get_cstr(receiv));
        }
    }

    goto cleanup;
}


/*
 * new authorisation, usable by POST and GET
 */
static URLTranslation *authorise_username(Octstr *username, Octstr *password,
					  Octstr *client_ip) 
{
    URLTranslation *t = NULL;

    if (username == NULL || password == NULL)
	return NULL;

    if ((t = urltrans_find_username(translations, username))==NULL)
	return NULL;

    if (octstr_compare(password, urltrans_password(t))!=0)
	return NULL;
    else {
	Octstr *allow_ip = urltrans_allow_ip(t);
	Octstr *deny_ip = urltrans_deny_ip(t);
	
        if (is_allowed_ip(allow_ip, deny_ip, client_ip) == 0) {
	    warning(0, "Non-allowed connect tried by <%s> from <%s>, ignored",
		    octstr_get_cstr(username), octstr_get_cstr(client_ip));
	    return NULL;
        }
    }

    info(0, "sendsms used by <%s>", octstr_get_cstr(username));
    return t;
}

/*
 * Authentication whith the database of Kannel.
 * Check for matching username and password for requests.
 * Return an URLTranslation if successful NULL otherwise.
 */
static URLTranslation *default_authorise_user(List *list, Octstr *client_ip) 
{
    Octstr *pass, *user = NULL;

    if ((user = http_cgi_variable(list, "username")) == NULL)
        user = http_cgi_variable(list, "user");

    if ((pass = http_cgi_variable(list, "password")) == NULL)
	pass = http_cgi_variable(list, "pass");

    return authorise_username(user, pass, client_ip);
}


static URLTranslation *authorise_user(List *list, Octstr *client_ip) 
{
#ifdef HAVE_SECURITY_PAM_APPL_H
    URLTranslation *t;
    
    t = urltrans_find_username(translations, octstr_imm("pam"));
    if (t != NULL) {
	if (pam_authorise_user(list))
	    return t;
	else 
	    return NULL;
    } else
	return default_authorise_user(list, client_ip);
#else
    return default_authorise_user(list, client_ip);
#endif
}


/*
 * Create and send an SMS message from an HTTP request.
 * Args: args contains the CGI parameters
 */
static Octstr *smsbox_req_sendsms(List *args, Octstr *client_ip, int *status,
				  HTTPClient *client)
{
    URLTranslation *t = NULL;
    Octstr *tmp_string;
    Octstr *from, *to, *charset, *text, *udh, *smsc, *dlr_url, *account;
    Octstr *binfo, *meta_data;
    int	dlr_mask, mclass, mwi, coding, compress, validity, deferred, pid;
    int alt_dcs, rpi, priority;

    from = to = udh = text = smsc = account = dlr_url = charset = binfo = meta_data = NULL;
    mclass = mwi = coding = compress = validity = deferred = dlr_mask = 
        pid = alt_dcs = rpi = priority = SMS_PARAM_UNDEFINED;
 
    /* check the username and password */
    t = authorise_user(args, client_ip);
    if (t == NULL) {
	*status = HTTP_FORBIDDEN;
	return octstr_create("Authorization failed for sendsms");
    }
    
    udh = http_cgi_variable(args, "udh");
    text = http_cgi_variable(args, "text");
    charset = http_cgi_variable(args, "charset");
    smsc = http_cgi_variable(args, "smsc");
    from = http_cgi_variable(args, "from");
    to = http_cgi_variable(args, "to");
    account = http_cgi_variable(args, "account");
    binfo = http_cgi_variable(args, "binfo");
    dlr_url = http_cgi_variable(args, "dlr-url");
    if(dlr_url == NULL) { /* deprecated dlrurl without "-" */
	dlr_url = http_cgi_variable(args, "dlrurl");
	if(dlr_url != NULL)
	    warning(0, "<dlrurl> field used and deprecated. Please use dlr-url instead.");
    }
    tmp_string = http_cgi_variable(args, "dlr-mask");
    if(tmp_string == NULL) { /* deprecated dlrmask without "-" */
	tmp_string = http_cgi_variable(args, "dlrmask");
	if(tmp_string != NULL)
	    warning(0, "<dlrmask> field used and deprecated. Please use dlr-mask instead.");
    }
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &dlr_mask);

    tmp_string = http_cgi_variable(args, "mclass");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);

    tmp_string = http_cgi_variable(args, "pid");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &pid);

    tmp_string = http_cgi_variable(args, "rpi");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &rpi);

    tmp_string = http_cgi_variable(args, "alt-dcs");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &alt_dcs);

    tmp_string = http_cgi_variable(args, "mwi");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &mwi);

    tmp_string = http_cgi_variable(args, "coding");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &coding);

    tmp_string = http_cgi_variable(args, "compress");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &compress);

    tmp_string = http_cgi_variable(args, "validity");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &validity);

    tmp_string = http_cgi_variable(args, "deferred");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &deferred);

    tmp_string = http_cgi_variable(args, "priority");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &priority);
    
    meta_data = http_cgi_variable(args, "meta-data");

    /*
     * we required "to" to be defined
     */
    if (to == NULL) {
	error(0, "%s got insufficient headers (<to> is NULL)",
	      octstr_get_cstr(sendsms_url));
	*status = HTTP_BAD_REQUEST;
	return octstr_create("Missing receiver number, rejected");
    } 
    else if (octstr_len(to) == 0) {
	error(0, "%s got empty <to> cgi variable", octstr_get_cstr(sendsms_url));
	*status = HTTP_BAD_REQUEST;
	return octstr_create("Empty receiver number not allowed, rejected");
    }

    return smsbox_req_handle(t, client_ip, client, from, to, text, charset, udh,
			     smsc, mclass, mwi, coding, compress, validity, 
			     deferred, status, dlr_mask, dlr_url, account,
			     pid, alt_dcs, rpi, NULL, binfo, priority, meta_data);
    
}


/*
 * Create and send an SMS message from an HTTP request.
 * Args: args contains the CGI parameters
 */
static Octstr *smsbox_sendsms_post(List *headers, Octstr *body,
				   Octstr *client_ip, int *status,
				   HTTPClient *client)
{
    URLTranslation *t = NULL;
    Octstr *user, *pass, *ret, *type;
    List *tolist;
    Octstr *text_html, *text_plain, *text_wml, *text_xml, *octet_stream;
    Octstr *text;
    Octstr *from, *to, *udh, *smsc, *charset, *dlr_url, *account, *binfo, *meta_data;
    int dlr_mask, mclass, mwi, coding, compress, validity, deferred;
    int pid, alt_dcs, rpi, priority;
 
    text_html = octstr_imm("text/html");
    text_wml = octstr_imm("text/vnd.wap.wml");
    text_plain = octstr_imm("text/plain");
    text_xml = octstr_imm("text/xml");
    octet_stream = octstr_imm("application/octet-stream");

    user = pass = ret = type = NULL;
    tolist = NULL;
    from = to = udh = smsc = account = dlr_url = charset = binfo = meta_data = NULL;
    mclass = mwi = coding = compress = validity = deferred = dlr_mask = 
        pid = alt_dcs = rpi = priority = SMS_PARAM_UNDEFINED;
 
    http_header_get_content_type(headers, &type, &charset);
    if (octstr_case_compare(type, text_html) == 0 ||
	octstr_case_compare(type, text_wml) == 0) {
	text = html_to_sms(body);
	octstr_strip_blanks(text);
	body = text;
	get_x_kannel_from_headers(headers, &from, &to, &udh,
				  &user, &pass, &smsc, &mclass, &mwi, 
				  &coding, &compress, &validity, 
				  &deferred, &dlr_mask, &dlr_url, 
				  &account, &pid, &alt_dcs, &rpi,
				  &binfo, &priority, &meta_data);
    } else if (octstr_case_compare(type, text_plain) == 0 ||
               octstr_case_compare(type, octet_stream) == 0) {
	get_x_kannel_from_headers(headers, &from, &to, &udh,
				  &user, &pass, &smsc, &mclass, &mwi, 
				  &coding, &compress, &validity, 
				  &deferred, &dlr_mask, &dlr_url, 
				  &account, &pid, &alt_dcs, &rpi,
				  &binfo, &priority, &meta_data);
    } else if (octstr_case_compare(type, text_xml) == 0) {
	get_x_kannel_from_xml(mt_push, &type, &body, headers, 
                              &from, &to, &udh, &user, &pass, &smsc, &mclass, 
			      &mwi, &coding, &compress, &validity, &deferred,
			      &dlr_mask, &dlr_url, &account, &pid, &alt_dcs,
			      &rpi, &tolist, &charset, &binfo, &priority, &meta_data);
    } else {
	*status = HTTP_BAD_REQUEST;
	ret = octstr_create("Invalid content-type");
	goto error;
    }

    /* check the username and password */
    t = authorise_username(user, pass, client_ip);
    if (t == NULL) {
	*status = HTTP_FORBIDDEN;
	ret = octstr_create("Authorization failed for sendsms");
    }
    else if (to == NULL && tolist == NULL) {
	error(0, "%s got insufficient headers (<to> and <tolist> are NULL)",
	      octstr_get_cstr(sendsms_url));
	*status = HTTP_BAD_REQUEST;
	ret = octstr_create("Missing receiver(s) number(s), rejected");
    } 
    else if (to != NULL && octstr_len(to) == 0) {
	error(0, "%s got empty <to> cgi variable", octstr_get_cstr(sendsms_url));
	*status = HTTP_BAD_REQUEST;
	ret = octstr_create("Empty receiver number not allowed, rejected");
    } 
    else {
	if (octstr_case_compare(type,
				octstr_imm("application/octet-stream")) == 0) {
	    if (coding == DC_UNDEF)
		coding = DC_8BIT; /* XXX Force UCS-2 with DC Field */
	} else if (octstr_case_compare(type,
				       octstr_imm("text/plain")) == 0) {
	    if (coding == DC_UNDEF)
		coding = DC_7BIT;
	} else {
	    error(0, "%s got weird content type %s", octstr_get_cstr(sendsms_url),
		  octstr_get_cstr(type));
	    *status = HTTP_UNSUPPORTED_MEDIA_TYPE;
	    ret = octstr_create("Unsupported content-type, rejected");
	}

	if (ret == NULL)
	    ret = smsbox_req_handle(t, client_ip, client, from, to, body, charset,
				    udh, smsc, mclass, mwi, coding, compress, 
				    validity, deferred, status, dlr_mask, 
				    dlr_url, account, pid, alt_dcs, rpi, tolist,
				    binfo, priority, meta_data);

    }
    octstr_destroy(user);
    octstr_destroy(pass);
    octstr_destroy(from);
    octstr_destroy(to);
    octstr_destroy(udh);
    octstr_destroy(smsc);
    octstr_destroy(dlr_url);
    octstr_destroy(account);
    octstr_destroy(binfo);
    octstr_destroy(meta_data);
error:
    octstr_destroy(type);
    octstr_destroy(charset);
    return ret;
}


/*
 * Create and send an SMS message from a XML-RPC request.
 * Answer with a valid XML-RPC response for a successful request.
 * 
 * function signature: boolean sms.send(struct)
 *
 * The <struct> MUST contain at least <member>'s with name 'username',
 * 'password', 'to' and MAY contain additional <member>'s with name
 * 'from', 'account', 'smsc', 'udh', 'dlrmask', 'dlrurl'. All values
 * are of type string.
 */
static Octstr *smsbox_xmlrpc_post(List *headers, Octstr *body,
                                  Octstr *client_ip, int *status)
{
    Octstr *ret, *type;
    Octstr *charset;
    Octstr *output;
    Octstr *method_name;
    XMLRPCDocument *msg;

    charset = NULL;
    ret = NULL;

    /*
     * check if the content type is valid for this request
     */
    http_header_get_content_type(headers, &type, &charset);
    if (octstr_case_compare(type, octstr_imm("text/xml")) != 0) {
        error(0, "Unsupported content-type '%s'", octstr_get_cstr(type));
        *status = HTTP_BAD_REQUEST;
        ret = octstr_format("Unsupported content-type '%s'", octstr_get_cstr(type));
    } else {

        /*
         * parse the body of the request and check if it is a valid XML-RPC
         * structure
         */
        msg = xmlrpc_parse_call(body);

        if ((xmlrpc_parse_status(msg) != XMLRPC_COMPILE_OK) && 
            ((output = xmlrpc_parse_error(msg)) != NULL)) {
            /* parse failure */
            error(0, "%s", octstr_get_cstr(output));
            *status = HTTP_BAD_REQUEST;
            ret = octstr_format("%s", octstr_get_cstr(output));
            octstr_destroy(output);
        } else {

            /*
             * at least the structure has been valid, now check for the
             * required methodName and the required variables
             */
            if (octstr_case_compare((method_name = xmlrpc_get_call_name(msg)), 
                                    octstr_imm("sms.send")) != 0) {
                error(0, "Unknown method name '%s'", octstr_get_cstr(method_name));
                *status = HTTP_BAD_REQUEST;
                ret = octstr_format("Unkown method name '%s'", 
                                    octstr_get_cstr(method_name));
            } else {

                /*
                 * TODO: check for the required struct members
                 */

            }
        }

        xmlrpc_destroy_call(msg);
    }
    
    return ret;
}


/*
 * Create and send an SMS OTA (auto configuration) message from an HTTP 
 * request. If cgivar "text" is present, use it as a xml configuration source,
 * otherwise read the configuration from the configuration file.
 * Args: list contains the CGI parameters
 */
static Octstr *smsbox_req_sendota(List *list, Octstr *client_ip, int *status,
				  HTTPClient *client)
{
    Octstr *id, *from, *phonenumber, *smsc, *ota_doc, *doc_type, *account;
    CfgGroup *grp;
    Octstr *returnerror;
    Octstr *stored_uuid = NULL;
    List *grplist;
    Octstr *p;
    URLTranslation *t;
    Msg *msg;
    int ret, ota_type;
    
    id = phonenumber = smsc = account = NULL;

    /* check the username and password */
    t = authorise_user(list, client_ip);
    if (t == NULL) {
	*status = HTTP_FORBIDDEN;
	return octstr_create("Authorization failed for sendota");
    }
    
    if ((phonenumber = http_cgi_variable(list, "to")) == NULL) {
        if ((phonenumber = http_cgi_variable(list, "phonenumber")) == NULL) {
            error(0, "%s needs a valid phone number.", octstr_get_cstr(sendota_url));
            *status = HTTP_BAD_REQUEST;
            return octstr_create("Wrong sendota args.");
        }
    }

    if (urltrans_faked_sender(t) != NULL) {
	from = octstr_duplicate(urltrans_faked_sender(t));
    } else if ((from = http_cgi_variable(list, "from")) != NULL &&
	       octstr_len(from) > 0) {
	from = octstr_duplicate(from);
    } else if (urltrans_default_sender(t) != NULL) {
	from = octstr_duplicate(urltrans_default_sender(t));
    } else if (global_sender != NULL) {
	from = octstr_duplicate(global_sender);
    } else {
	*status = HTTP_BAD_REQUEST;
	return octstr_create("Sender missing and no global set, rejected");
    }
        
    /* check does we have an external XML source for configuration */
    if ((ota_doc = http_cgi_variable(list, "text")) != NULL) {
        Octstr *sec, *pin;
        
        /*
         * We are doing the XML OTA compiler mode for this request
         */
        debug("sms", 0, "OTA service with XML document");
        ota_doc = octstr_duplicate(ota_doc);
        if ((doc_type = http_cgi_variable(list, "type")) == NULL)
            doc_type = octstr_format("%s", "settings");
        else 
            doc_type = octstr_duplicate(doc_type);
        if ((sec = http_cgi_variable(list, "sec")) == NULL)
            sec = octstr_create("USERPIN");
        else 
            sec = octstr_duplicate(sec);
        if ((pin = http_cgi_variable(list, "pin")) == NULL)
            pin = octstr_create("12345");
        else
            pin = octstr_duplicate(pin);

        if ((ret = ota_pack_message(&msg, ota_doc, doc_type, from, 
                                phonenumber, sec, pin)) < 0) {
            *status = HTTP_BAD_REQUEST;
            msg_destroy(msg);
            if (ret == -2)
                return octstr_create("Erroneous document type, cannot"
                                     " compile\n");
            else if (ret == -1)
	        return octstr_create("Erroneous ota source, cannot compile\n");
        }

        goto send;

    } else {

        /* 
         * We are doing the ota-settings or ota-bookmark group mode
         * for this request.
         *
         * Check if a ota-setting ID has been given and decide which OTA
         * properties to be send to the client otherwise try to find a
         * ota-bookmark ID. If none is found then send the default 
         * ota-setting group, which is the first within the config file.
         */
        id = http_cgi_variable(list, "otaid");
    
        grplist = cfg_get_multi_group(cfg, octstr_imm("ota-setting"));
        while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
            p = cfg_get(grp, octstr_imm("ota-id"));
            if (id == NULL || (p != NULL && octstr_compare(p, id) == 0)) {
                ota_type = 1;
                goto found;
            }
            octstr_destroy(p);
        }
        gwlist_destroy(grplist, NULL);
        
        grplist = cfg_get_multi_group(cfg, octstr_imm("ota-bookmark"));
        while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
            p = cfg_get(grp, octstr_imm("ota-id"));
            if (id == NULL || (p != NULL && octstr_compare(p, id) == 0)) {
                ota_type = 0;             
                goto found;
            }
            octstr_destroy(p);
        }
        gwlist_destroy(grplist, NULL);
        
        if (id != NULL)
            error(0, "%s can't find any ota-setting or ota-bookmark group with ota-id '%s'.", 
                 octstr_get_cstr(sendota_url), octstr_get_cstr(id));
        else
	       error(0, "%s can't find any ota-setting group.", octstr_get_cstr(sendota_url));
        octstr_destroy(from);
        *status = HTTP_BAD_REQUEST;
        return octstr_create("Missing ota-setting or ota-bookmark group.");
    }
    
found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    /* tokenize the OTA settings or bookmarks group and return the message */
    if (ota_type)
        msg = ota_tokenize_settings(grp, from, phonenumber);
    else
        msg = ota_tokenize_bookmarks(grp, from, phonenumber);

send: 
    /* we still need to check if smsc is forced for this */
    smsc = http_cgi_variable(list, "smsc");
    if (urltrans_forced_smsc(t)) {
        msg->sms.smsc_id = octstr_duplicate(urltrans_forced_smsc(t));
        if (smsc)
            info(0, "send-sms request smsc id ignored, as smsc id forced to %s",
                 octstr_get_cstr(urltrans_forced_smsc(t)));
    } else if (smsc) {
        msg->sms.smsc_id = octstr_duplicate(smsc);
    } else if (urltrans_default_smsc(t)) {
        msg->sms.smsc_id = octstr_duplicate(urltrans_default_smsc(t));
    } else
        msg->sms.smsc_id = NULL;

    account = http_cgi_variable(list, "account");
    if (octstr_len(account) > 0)
        msg->sms.account = octstr_duplicate(account);

    octstr_dump(msg->sms.msgdata, 0);

    info(0, "%s <%s> <%s>", octstr_get_cstr(sendota_url), 
    	 id ? octstr_get_cstr(id) : "<default>", octstr_get_cstr(phonenumber));

    if (!immediate_sendsms_reply) {
        stored_uuid = store_uuid(msg);
        dict_put(client_dict, stored_uuid, client);
    }

    ret = send_message(t, msg); 

    if (ret == -1) {
        error(0, "sendota_request: failed");
        *status = HTTP_INTERNAL_SERVER_ERROR;
        returnerror = octstr_create("Sending failed.");
        dict_remove(client_dict, stored_uuid);
    } else {
        *status = HTTP_ACCEPTED;
        returnerror = octstr_create("Sent.");
    }

    msg_destroy(msg);
    octstr_destroy(stored_uuid);

    return returnerror;
}


/*
 * Create and send an SMS OTA (auto configuration) message from an HTTP POST 
 * request. Take the X-Kannel-foobar HTTP headers as parameter information.
 * Args: list contains the CGI parameters
 *
 * We still care about passed GET variable, in case the X-Kannel-foobar
 * parameters are not used but the POST contains the XML body itself.
 */
static Octstr *smsbox_sendota_post(List *headers, Octstr *body,
                                   Octstr *client_ip, int *status,
				   HTTPClient *client)
{
    Octstr *name, *val, *ret;
    Octstr *from, *to, *id, *user, *pass, *smsc;
    Octstr *type, *charset, *doc_type, *ota_doc, *sec, *pin;
    Octstr *stored_uuid = NULL;
    URLTranslation *t;
    Msg *msg;
    long l;
    int r;

    id = from = to = user = pass = smsc = NULL;
    doc_type = ota_doc = sec = pin = NULL;

    /* 
     * process all special HTTP headers 
     * 
     * XXX can't we do this better? 
     * Obviously http_header_find_first() does this
     */
    for (l = 0; l < gwlist_len(headers); l++) {
        http_header_get(headers, l, &name, &val);

        if (octstr_case_compare(name, octstr_imm("X-Kannel-OTA-ID")) == 0) {
            id = octstr_duplicate(val);
            octstr_strip_blanks(id);
        }
        else if (octstr_case_compare(name, octstr_imm("X-Kannel-From")) == 0) {
            from = octstr_duplicate(val);
            octstr_strip_blanks(from);
        }
        else if (octstr_case_compare(name, octstr_imm("X-Kannel-To")) == 0) {
            to = octstr_duplicate(val);
            octstr_strip_blanks(to);
        }
        else if (octstr_case_compare(name, octstr_imm("X-Kannel-Username")) == 0) {
            user = octstr_duplicate(val);
            octstr_strip_blanks(user);
        }
        else if (octstr_case_compare(name, octstr_imm("X-Kannel-Password")) == 0) {
            pass = octstr_duplicate(val);
            octstr_strip_blanks(pass);
        }
        else if (octstr_case_compare(name, octstr_imm("X-Kannel-SMSC")) == 0) {
            smsc = octstr_duplicate(val);
            octstr_strip_blanks(smsc);
        }
        else if (octstr_case_compare(name, octstr_imm("X-Kannel-SEC")) == 0) {
            sec = octstr_duplicate(val);
            octstr_strip_blanks(sec);
        }
        else if (octstr_case_compare(name, octstr_imm("X-Kannel-PIN")) == 0) {
            pin = octstr_duplicate(val);
            octstr_strip_blanks(pin);
        }
    }

    /* apply defaults */
    if (!sec)
        sec =  octstr_imm("USERPIN");
    if (!pin)
        pin = octstr_imm("1234");

    /* check the username and password */
    t = authorise_username(user, pass, client_ip);
    if (t == NULL) {
	   *status = HTTP_FORBIDDEN;
	   ret = octstr_create("Authorization failed for sendota");
    }
    /* let's see if we have at least a target msisdn */
    else if (to == NULL) {
	   error(0, "%s needs a valid phone number.", octstr_get_cstr(sendota_url));
	   *status = HTTP_BAD_REQUEST;
       ret = octstr_create("Wrong sendota args.");
    } else {

    if (urltrans_faked_sender(t) != NULL) {
        from = octstr_duplicate(urltrans_faked_sender(t));
    } 
    else if (from != NULL && octstr_len(from) > 0) {
    } 
    else if (urltrans_default_sender(t) != NULL) {
        from = octstr_duplicate(urltrans_default_sender(t));
    } 
    else if (global_sender != NULL) {
        from = octstr_duplicate(global_sender);
    } 
    else {
        *status = HTTP_BAD_REQUEST;
        ret = octstr_create("Sender missing and no global set, rejected");
        goto error;
    }

    /*
     * get the content-type of the body document 
     */
    http_header_get_content_type(headers, &type, &charset);

	if (octstr_case_compare(type, 
        octstr_imm("application/x-wap-prov.browser-settings")) == 0) {
        doc_type = octstr_format("%s", "settings");
    } 
    else if (octstr_case_compare(type, 
             octstr_imm("application/x-wap-prov.browser-bookmarks")) == 0) {
	    doc_type = octstr_format("%s", "bookmarks");
    }
    else if (octstr_case_compare(type, 
             octstr_imm("text/vnd.wap.connectivity-xml")) == 0) {
        doc_type = octstr_format("%s", "oma-settings");
    }

    if (doc_type == NULL) {
	    error(0, "%s got weird content type %s", octstr_get_cstr(sendota_url),
              octstr_get_cstr(type));
	    *status = HTTP_UNSUPPORTED_MEDIA_TYPE;
	    ret = octstr_create("Unsupported content-type, rejected");
	} else {

	    /* 
	     * ok, this is want we expect
	     * now lets compile the whole thing 
	     */
	    ota_doc = octstr_duplicate(body);

        if ((r = ota_pack_message(&msg, ota_doc, doc_type, from, to, sec, pin)) < 0) {
		*status = HTTP_BAD_REQUEST;
		msg_destroy(msg);
		if (r == -2) {
		    ret = octstr_create("Erroneous document type, cannot"
					" compile\n");
		    goto error;
		}
		else if (r == -1) {
		    ret = octstr_create("Erroneous ota source, cannot compile\n");
		    goto error;
		}
	    }

	    /* we still need to check if smsc is forced for this */
	    if (urltrans_forced_smsc(t)) {
		msg->sms.smsc_id = octstr_duplicate(urltrans_forced_smsc(t));
		if (smsc)
		    info(0, "send-sms request smsc id ignored, as smsc id forced to %s",
			 octstr_get_cstr(urltrans_forced_smsc(t)));
	    } else if (smsc) {
		msg->sms.smsc_id = octstr_duplicate(smsc);
	    } else if (urltrans_default_smsc(t)) {
		msg->sms.smsc_id = octstr_duplicate(urltrans_default_smsc(t));
	    } else
		msg->sms.smsc_id = NULL;

	    info(0, "%s <%s> <%s>", octstr_get_cstr(sendota_url), 
		 id ? octstr_get_cstr(id) : "XML", octstr_get_cstr(to));
    

        if (!immediate_sendsms_reply) {
            stored_uuid = store_uuid(msg);
            dict_put(client_dict, stored_uuid, client);
        }

	    r = send_message(t, msg); 

	    if (r == -1) {
            error(0, "sendota_request: failed");
            *status = HTTP_INTERNAL_SERVER_ERROR;
            ret = octstr_create("Sending failed.");
            if (!immediate_sendsms_reply) 
                dict_remove(client_dict, stored_uuid);
       } else  {
            *status = HTTP_ACCEPTED;
            ret = octstr_create("Sent.");
	    }

       msg_destroy(msg);
       octstr_destroy(stored_uuid);

	}
    }    
    
error:
    octstr_destroy(user);
    octstr_destroy(pass);
    octstr_destroy(smsc);

    return ret;
}


static void sendsms_thread(void *arg)
 {
    HTTPClient *client;
    Octstr *ip, *url, *body, *answer;
    List *hdrs, *args;
    int status;
    
    for (;;) {
    	/* reset request wars */
    	ip = url = body = answer = NULL;
    	hdrs = args = NULL;

        client = http_accept_request(sendsms_port, &ip, &url, &hdrs, &body, &args);
        if (client == NULL)
            break;

        info(0, "smsbox: Got HTTP request <%s> from <%s>",
                octstr_get_cstr(url), octstr_get_cstr(ip));

        /*
         * determine which kind of HTTP request this is any
         * call the necessary routine for it
         */

        /* sendsms */
        if (octstr_compare(url, sendsms_url) == 0) {
            /*
             * decide if this is a GET or POST request and let the
             * related routine handle the checking
             */
            if (body == NULL)
                answer = smsbox_req_sendsms(args, ip, &status, client);
            else
                answer = smsbox_sendsms_post(hdrs, body, ip, &status, client);
        }
        /* XML-RPC */
        else if (octstr_compare(url, xmlrpc_url) == 0) {
            /*
             * XML-RPC request needs to have a POST body
             */
            if (body == NULL) {
                answer = octstr_create("Incomplete request.");
                status = HTTP_BAD_REQUEST;
            } else
                answer = smsbox_xmlrpc_post(hdrs, body, ip, &status);
        }
        /* sendota */
        else if (octstr_compare(url, sendota_url) == 0) {
            if (body == NULL)
                answer = smsbox_req_sendota(args, ip, &status, client);
            else
                answer = smsbox_sendota_post(hdrs, body, ip, &status, client);
        }
        /* add aditional URI compares here */
        else {
            answer = octstr_create("Unknown request.");
            status = HTTP_NOT_FOUND;
        }

        debug("sms.http", 0, "Status: %d Answer: <%s>", status,
                octstr_get_cstr(answer));

        octstr_destroy(ip);
        octstr_destroy(url);
        http_destroy_headers(hdrs);
        octstr_destroy(body);
        http_destroy_cgiargs(args);

        if (immediate_sendsms_reply || status != HTTP_ACCEPTED)
            http_send_reply(client, status, sendsms_reply_hdrs, answer);
        else {
            debug("sms.http", 0, "Delayed reply - wait for bearerbox");
        }
        octstr_destroy(answer);
    }

}


/***********************************************************************
 * Main program. Configuration, signal handling, etc.
 */

static void signal_handler(int signum) {
    /* On some implementations (i.e. linuxthreads), signals are delivered
     * to all threads.  We only want to handle each signal once for the
     * entire box, and we let the gwthread wrapper take care of choosing
     * one.
     */
    if (!gwthread_shouldhandlesignal(signum))
        return;

    switch (signum) {
        case SIGINT:
        case SIGTERM:
       	    if (program_status != shutting_down) {
                error(0, "SIGINT received, aborting program...");
                program_status = shutting_down;
            }
            break;

        case SIGHUP:
            warning(0, "SIGHUP received, catching and re-opening logs");
            log_reopen();
            alog_reopen();
            break;

        /* 
         * It would be more proper to use SIGUSR1 for this, but on some
         * platforms that's reserved by the pthread support. 
         */
        case SIGQUIT:
	       warning(0, "SIGQUIT received, reporting memory usage.");
	       gw_check_leaks();
	       break;
    }
}


static void setup_signal_handlers(void) {
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGPIPE, &act, NULL);
}



static Cfg *init_smsbox(Cfg *cfg)
{
    CfgGroup *grp;
    Octstr *logfile;
    Octstr *p;
    long lvl, value;
    Octstr *http_proxy_host = NULL;
    long http_proxy_port = -1;
    int http_proxy_ssl = 0;
    List *http_proxy_exceptions = NULL;
    Octstr *http_proxy_username = NULL;
    Octstr *http_proxy_password = NULL;
    Octstr *http_proxy_exceptions_regex = NULL;
    int ssl = 0;
    int lf, m;
    long max_req;

    bb_port = BB_DEFAULT_SMSBOX_PORT;
    bb_ssl = 0;
    bb_host = octstr_create(BB_DEFAULT_HOST);
    logfile = NULL;
    lvl = 0;
    lf = m = 1;

    /*
     * first we take the port number in bearerbox and other values from the
     * core group in configuration file
     */

    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    
    cfg_get_integer(&bb_port, grp, octstr_imm("smsbox-port"));
#ifdef HAVE_LIBSSL
    cfg_get_bool(&bb_ssl, grp, octstr_imm("smsbox-port-ssl"));
#endif /* HAVE_LIBSSL */

    cfg_get_integer(&http_proxy_port, grp, octstr_imm("http-proxy-port"));
#ifdef HAVE_LIBSSL
    cfg_get_bool(&http_proxy_ssl, grp, octstr_imm("http-proxy-ssl"));
#endif /* HAVE_LIBSSL */

    http_proxy_host = cfg_get(grp, 
    	    	    	octstr_imm("http-proxy-host"));
    http_proxy_username = cfg_get(grp, 
    	    	    	    octstr_imm("http-proxy-username"));
    http_proxy_password = cfg_get(grp, 
    	    	    	    octstr_imm("http-proxy-password"));
    http_proxy_exceptions = cfg_get_list(grp,
    	    	    	    octstr_imm("http-proxy-exceptions"));
    http_proxy_exceptions_regex = cfg_get(grp,
    	    	    	    octstr_imm("http-proxy-exceptions-regex"));

#ifdef HAVE_LIBSSL
    conn_config_ssl(grp);
#endif 
    
    /*
     * get the remaining values from the smsbox group
     */
    grp = cfg_get_single_group(cfg, octstr_imm("smsbox"));
    if (grp == NULL)
	panic(0, "No 'smsbox' group in configuration");

    smsbox_id = cfg_get(grp, octstr_imm("smsbox-id"));

    p = cfg_get(grp, octstr_imm("bearerbox-host"));
    if (p != NULL) {
	octstr_destroy(bb_host);
	bb_host = p;
    }
    cfg_get_integer(&bb_port, grp, octstr_imm("bearerbox-port"));
#ifdef HAVE_LIBSSL
    if (cfg_get_bool(&ssl, grp, octstr_imm("bearerbox-port-ssl")) != -1)
        bb_ssl = ssl;
#endif /* HAVE_LIBSSL */

    cfg_get_bool(&mo_recode, grp, octstr_imm("mo-recode"));
    if(mo_recode < 0)
	mo_recode = 0;

    reply_couldnotfetch= cfg_get(grp, octstr_imm("reply-couldnotfetch"));
    if (reply_couldnotfetch == NULL)
	reply_couldnotfetch = octstr_create("Could not fetch content, sorry.");

    reply_couldnotrepresent= cfg_get(grp, octstr_imm("reply-couldnotfetch"));
    if (reply_couldnotrepresent == NULL)
	reply_couldnotrepresent = octstr_create("Result could not be represented "
					        "as an SMS message.");
    reply_requestfailed= cfg_get(grp, octstr_imm("reply-requestfailed"));
    if (reply_requestfailed == NULL)
	reply_requestfailed = octstr_create("Request Failed");

    reply_emptymessage= cfg_get(grp, octstr_imm("reply-emptymessage"));
    if (reply_emptymessage == NULL)
	reply_emptymessage = octstr_create("<Empty reply from service provider>");

    {   
	Octstr *os;
	os = cfg_get(grp, octstr_imm("white-list"));
	if (os != NULL) {
	    white_list = numhash_create(octstr_get_cstr(os));
	    octstr_destroy(os);
	}

	os = cfg_get(grp, octstr_imm("white-list-regex"));
	if (os != NULL) {
        if ((white_list_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
                        panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
	}

	os = cfg_get(grp, octstr_imm("black-list"));
	if (os != NULL) {
	    black_list = numhash_create(octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	os = cfg_get(grp, octstr_imm("black-list-regex"));
	if (os != NULL) {
        if ((black_list_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
                        panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
	}
    }

    cfg_get_integer(&sendsms_port, grp, octstr_imm("sendsms-port"));
    
    /* check if want to bind to a specific interface */
    sendsms_interface = cfg_get(grp, octstr_imm("sendsms-interface"));    

    cfg_get_integer(&sms_max_length, grp, octstr_imm("sms-length"));

#ifdef HAVE_LIBSSL
    cfg_get_bool(&ssl, grp, octstr_imm("sendsms-port-ssl"));
#endif /* HAVE_LIBSSL */

    /*
     * load the configuration settings for the sendsms and sendota URIs
     * else assume the default URIs, st.
     */
    if ((sendsms_url = cfg_get(grp, octstr_imm("sendsms-url"))) == NULL)
        sendsms_url = octstr_imm("/cgi-bin/sendsms");
    if ((xmlrpc_url = cfg_get(grp, octstr_imm("xmlrpc-url"))) == NULL)
        xmlrpc_url = octstr_imm("/cgi-bin/xmlrpc");
    if ((sendota_url = cfg_get(grp, octstr_imm("sendota-url"))) == NULL)
        sendota_url = octstr_imm("/cgi-bin/sendota");

    global_sender = cfg_get(grp, octstr_imm("global-sender"));
    accepted_chars = cfg_get(grp, octstr_imm("sendsms-chars"));
    sendsms_number_chars = accepted_chars ? 
        octstr_get_cstr(accepted_chars) : SENDSMS_DEFAULT_CHARS;
    logfile = cfg_get(grp, octstr_imm("log-file"));

    cfg_get_integer(&lvl, grp, octstr_imm("log-level"));

    if (logfile != NULL) {
	info(0, "Starting to log to file %s level %ld", 
	     octstr_get_cstr(logfile), lvl);
	log_open(octstr_get_cstr(logfile), lvl, GW_NON_EXCL);
	octstr_destroy(logfile);
    }
    if ((p = cfg_get(grp, octstr_imm("syslog-level"))) != NULL) {
        long level;
        Octstr *facility;
        if ((facility = cfg_get(grp, octstr_imm("syslog-facility"))) != NULL) {
            log_set_syslog_facility(octstr_get_cstr(facility));
            octstr_destroy(facility);
        }
        if (octstr_compare(p, octstr_imm("none")) == 0) {
            log_set_syslog(NULL, 0);
        } else if (octstr_parse_long(&level, p, 0, 10) > 0) {
            log_set_syslog("smsbox", level);
        }
        octstr_destroy(p);
    } else {
        log_set_syslog(NULL, 0);
    }
    if (global_sender != NULL) {
	info(0, "Service global sender set as '%s'", 
	     octstr_get_cstr(global_sender));
    }
    
    /* should smsbox reply to sendsms immediate or wait for bearerbox ack */
    cfg_get_bool(&immediate_sendsms_reply, grp, octstr_imm("immediate-sendsms-reply"));

    /* determine which timezone we use for access logging */
    if ((p = cfg_get(grp, octstr_imm("access-log-time"))) != NULL) {
        lf = (octstr_case_compare(p, octstr_imm("gmt")) == 0) ? 0 : 1;
        octstr_destroy(p);
    }

    /* should predefined markers be used, ie. prefixing timestamp */
    cfg_get_bool(&m, grp, octstr_imm("access-log-clean"));

    /* open access-log file */
    if ((p = cfg_get(grp, octstr_imm("access-log"))) != NULL) {
        info(0, "Logging accesses to '%s'.", octstr_get_cstr(p));
        alog_open(octstr_get_cstr(p), lf, m ? 0 : 1);
        octstr_destroy(p);
    }

    /* HTTP queueing values */
    cfg_get_integer(&max_http_retries, grp, octstr_imm("http-request-retry"));
    cfg_get_integer(&http_queue_delay, grp, octstr_imm("http-queue-delay"));

    if (sendsms_port > 0) {
        if (http_open_port_if(sendsms_port, ssl, sendsms_interface) == -1) {	
            if (only_try_http)
                error(0, "Failed to open HTTP socket, ignoring it");
            else
                panic(0, "Failed to open HTTP socket");
        } else {
            info(0, "Set up send sms service at port %ld", sendsms_port);
            gwthread_create(sendsms_thread, NULL);
        }
    }

    /* set maximum allowed MO/DLR requests in parallel */
    if (cfg_get_integer(&max_req, grp, octstr_imm("max-pending-requests")) == -1)
        max_req = HTTP_MAX_PENDING; 
    max_pending_requests = semaphore_create(max_req);

    if (cfg_get_integer(&value, grp, octstr_imm("http-timeout")) == 0)
       http_set_client_timeout(value);

    /*
     * Reading the name we are using for ppg services from ppg core group
     */
    if ((grp = cfg_get_single_group(cfg, octstr_imm("ppg"))) != NULL) {
        if ((ppg_service_name = cfg_get(grp, octstr_imm("service-name"))) == NULL)
            ppg_service_name = octstr_create("ppg");
    }

    if (http_proxy_host != NULL && http_proxy_port > 0) {
    	http_use_proxy(http_proxy_host, http_proxy_port, http_proxy_ssl,
		       http_proxy_exceptions, http_proxy_username,
                       http_proxy_password, http_proxy_exceptions_regex);
    }

    octstr_destroy(http_proxy_host);
    octstr_destroy(http_proxy_username);
    octstr_destroy(http_proxy_password);
    octstr_destroy(http_proxy_exceptions_regex);
    gwlist_destroy(http_proxy_exceptions, octstr_destroy_item);

    return cfg;
}


static int check_args(int i, int argc, char **argv) {
    if (strcmp(argv[i], "-H")==0 || strcmp(argv[i], "--tryhttp")==0) {
	only_try_http = 1;
    } else
	return -1;

    return 0;
} 


int main(int argc, char **argv)
{
    int cf_index;
    Octstr *filename;
    double heartbeat_freq = DEFAULT_HEARTBEAT;

    gwlib_init();
    cf_index = get_and_set_debugs(argc, argv, check_args);
    
    setup_signal_handlers();
    
    if (argv[cf_index] == NULL)
	filename = octstr_create("kannel.conf");
    else
	filename = octstr_create(argv[cf_index]);
    cfg = cfg_create(filename);

    if (cfg_read(cfg) == -1)
	panic(0, "Couldn't read configuration from `%s'.", octstr_get_cstr(filename));

    octstr_destroy(filename);

    report_versions("smsbox");

    init_smsbox(cfg);

    if (max_http_retries > 0) {
        info(0, "Using HTTP request queueing with %ld retries, %lds delay.", 
                max_http_retries, http_queue_delay);
    }

    debug("sms", 0, "----------------------------------------------");
    debug("sms", 0, GW_NAME " smsbox version %s starting", GW_VERSION);

    translations = urltrans_create();
    if (translations == NULL)
	panic(0, "urltrans_create failed");
    if (urltrans_add_cfg(translations, cfg) == -1)
	panic(0, "urltrans_add_cfg failed");

    client_dict = dict_create(32, NULL);
    sendsms_reply_hdrs = http_create_empty_headers();
    http_header_add(sendsms_reply_hdrs, "Content-type", "text/html");
    http_header_add(sendsms_reply_hdrs, "Pragma", "no-cache");
    http_header_add(sendsms_reply_hdrs, "Cache-Control", "no-cache");


    caller = http_caller_create();
    smsbox_requests = gwlist_create();
    smsbox_http_requests = gwlist_create();
    timerset = gw_timerset_create();
    gwlist_add_producer(smsbox_requests);
    gwlist_add_producer(smsbox_http_requests);
    num_outstanding_requests = counter_create();
    catenated_sms_counter = counter_create();
    gwthread_create(obey_request_thread, NULL);
    gwthread_create(url_result_thread, NULL);
    gwthread_create(http_queue_thread, NULL);

    connect_to_bearerbox(bb_host, bb_port, bb_ssl, NULL /* bb_our_host */);
	/* XXX add our_host if required */

    if (0 > heartbeat_start(write_to_bearerbox, heartbeat_freq,
				       outstanding_requests)) {
        info(0, GW_NAME "Could not start heartbeat.");
    }

    identify_to_bearerbox();
    read_messages_from_bearerbox();

    info(0, GW_NAME " smsbox terminating.");

    heartbeat_stop(ALL_HEARTBEATS);
    http_close_all_ports();
    gwthread_join_every(sendsms_thread);
    gwlist_remove_producer(smsbox_requests);
    gwlist_remove_producer(smsbox_http_requests);
    gwthread_join_every(obey_request_thread);
    http_caller_signal_shutdown(caller);
    gwthread_join_every(url_result_thread);
    gwthread_join_every(http_queue_thread);

    close_connection_to_bearerbox();
    alog_close();
    urltrans_destroy(translations);
    gw_assert(gwlist_len(smsbox_requests) == 0);
    gw_assert(gwlist_len(smsbox_http_requests) == 0);
    gwlist_destroy(smsbox_requests, NULL);
    gwlist_destroy(smsbox_http_requests, NULL);
    http_caller_destroy(caller);
    gw_timerset_destroy(timerset);
    counter_destroy(num_outstanding_requests);
    counter_destroy(catenated_sms_counter);
    octstr_destroy(bb_host);
    octstr_destroy(global_sender);
    octstr_destroy(accepted_chars);
    octstr_destroy(smsbox_id);
    octstr_destroy(sendsms_url);
    octstr_destroy(sendota_url);
    octstr_destroy(xmlrpc_url);
    octstr_destroy(reply_emptymessage);
    octstr_destroy(reply_requestfailed);
    octstr_destroy(reply_couldnotfetch);
    octstr_destroy(reply_couldnotrepresent);
    octstr_destroy(sendsms_interface);    
    octstr_destroy(ppg_service_name);    
    numhash_destroy(black_list);
    numhash_destroy(white_list);
    if (white_list_regex != NULL) gw_regex_destroy(white_list_regex);
    if (black_list_regex != NULL) gw_regex_destroy(black_list_regex);
    semaphore_destroy(max_pending_requests);
    cfg_destroy(cfg);

    dict_destroy(client_dict); 
    http_destroy_headers(sendsms_reply_hdrs);

    /* 
     * Just sleep for a while to get bearerbox chance to restart.
     * Otherwise we will fail while trying to connect to bearerbox!
     */
    if (restart) {
        gwthread_sleep(10.0);
        /* now really restart */
        restart_box(argv);
    }

    log_close_all();
    gwlib_shutdown();

    return 0;
}

int charset_processing(Octstr *charset, Octstr *body, int coding)
{
    int resultcode = 0;

	/*
	debug("sms.http", 0, "%s: enter, charset=%s, coding=%d, msgdata is:",
	      __func__, octstr_get_cstr(charset), coding);
	octstr_dump(body, 0);
	*/

    if (octstr_len(charset)) {
    	if (coding == DC_7BIT) {
    		/*
    		 * For 7 bit, convert to UTF-8
    		 */
    		if (charset_convert(body, octstr_get_cstr(charset), "UTF-8") < 0) {
                error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
                	  octstr_get_cstr(charset), "UTF-8");
    			resultcode = -1;
    		}
    	} else if (coding == DC_UCS2) {
    		/*
    		 * For UCS-2, convert to UTF-16BE
    		 */
    		if (charset_convert(body, octstr_get_cstr(charset), "UTF-16BE") < 0) {
                error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
                	  octstr_get_cstr(charset), "UTF-16BE");
    			resultcode = -1;
    		}
    	}
    }

	/*
	debug("sms.http", 0, "%s: exit, charset=%s, coding=%d, msgdata is:",
	      __func__, octstr_get_cstr(charset), coding);
	octstr_dump(body, 0);
	*/

    return resultcode;
}
