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
 * urltrans.c - URL translations
 *
 * Lars Wirzenius
 */


#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "urltrans.h"
#include "gwlib/gwlib.h"
#include "gw/sms.h"
#include "gw/dlr.h"
#include "gw/meta_data.h"


/***********************************************************************
 * Definitions of data structures. These are not visible to the external
 * world -- they may be accessed only via the functions declared in
 * urltrans.h.
 */


/*
 * Hold one keyword/options entity
 */
struct URLTranslation {
    int type;		/* see enumeration in header file */
    Octstr *pattern;	/* url, text or file-name pattern */
    Octstr *prefix;	/* for prefix-cut */
    Octstr *suffix;	/* for suffix-cut */
    Octstr *faked_sender;/* works only with certain services */
    Octstr *default_sender;/* Default sender to sendsms-user */
    long max_messages;	/* absolute limit of reply messages */
    int concatenation;	/* send long messages as concatenated SMS's if true */
    Octstr *split_chars;/* allowed chars to be used to split message */
    Octstr *split_suffix;/* chars added to end after each split (not last) */
    int omit_empty;	/* if the reply is empty, is notification send */
    Octstr *header;	/* string to be inserted to each SMS */
    Octstr *footer;	/* string to be appended to each SMS */
    Octstr *alt_charset; /* alternative charset to use towards service */
    List *accepted_smsc; /* smsc id's allowed to use this service. If not set,
			    all messages can use this service */
    List *accepted_account; /* account id's allowed to use this service. If not set,
			    all messages can use this service */
    
    Octstr *name;	/* Translation name */
    Octstr *username;	/* send sms username */
    Octstr *password;	/* password associated */
    Octstr *forced_smsc;/* if smsc id is forcet to certain for this user */
    Octstr *default_smsc; /* smsc id if none given in http send-sms request */
    Octstr *allow_ip;	/* allowed IPs to request send-sms with this 
    	    	    	   account */
    Octstr *deny_ip;	/* denied IPs to request send-sms with this account */
    Octstr *allowed_prefix;	/* Prefixes (of sender) allowed in this translation, or... */
    Octstr *denied_prefix;	/* ...denied prefixes */
    Octstr *allowed_recv_prefix; /* Prefixes (of receiver) allowed in this translation, or... */
    Octstr *denied_recv_prefix;	/* ...denied prefixes */
    Numhash *white_list;	/* To numbers allowed, or ... */
    Numhash *black_list; /* ...denied numbers */

    int assume_plain_text; /* for type: octet-stream */
    int accept_x_kannel_headers; /* do we accept special headers in reply */
    int strip_keyword;	/* POST body */
    int send_sender;	/* POST headers */
    
    int args;
    int has_catchall_arg;
    int catch_all;
    Octstr *dlr_url;    /* URL to call for delivery reports */
    long dlr_mask;       /* DLR event mask */

    regex_t *keyword_regex;       /* the compiled regular expression for the keyword*/
    regex_t *accepted_smsc_regex;
    regex_t *accepted_account_regex;
    regex_t *allowed_prefix_regex;
    regex_t *denied_prefix_regex;
    regex_t *allowed_receiver_prefix_regex;
    regex_t *denied_receiver_prefix_regex;
    regex_t *white_list_regex;
    regex_t *black_list_regex;
};


/*
 * Hold the list of all translations.
 */
struct URLTranslationList {
    List *list;
    List *defaults; /* List of default sms-services */
    Dict *names;	/* Dict of lowercase Octstr names */
};


/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */

static long count_occurences(Octstr *str, Octstr *pat);
static URLTranslation *create_onetrans(CfgGroup *grp);
static void destroy_onetrans(void *ot);
static URLTranslation *find_translation(URLTranslationList *trans, Msg *msg);
static URLTranslation *find_default_translation(URLTranslationList *trans,
						Octstr *smsc, Octstr *sender, Octstr *receiver,
						Octstr *account);


/***********************************************************************
 * Implementations of the functions declared in urltrans.h. See the
 * header for explanations of what they should do.
 */


static void destroy_keyword_list(void *list)
{
    gwlist_destroy(list, NULL);
}


URLTranslationList *urltrans_create(void) 
{
    URLTranslationList *trans;
    
    trans = gw_malloc(sizeof(URLTranslationList));
    trans->list = gwlist_create();
    trans->defaults = gwlist_create();
    trans->names = dict_create(1024, destroy_keyword_list);
    return trans;
}


void urltrans_destroy(URLTranslationList *trans) 
{
    gwlist_destroy(trans->list, destroy_onetrans);
    gwlist_destroy(trans->defaults, destroy_onetrans);
    dict_destroy(trans->names);
    gw_free(trans);
}


int urltrans_add_one(URLTranslationList *trans, CfgGroup *grp)
{
    URLTranslation *ot;
    List *list2;
    
    ot = create_onetrans(grp);
    if (ot == NULL)
	return -1;

    if (ot->type != TRANSTYPE_SENDSMS && ot->keyword_regex == NULL)
        gwlist_append(trans->defaults, ot);
    else 
        gwlist_append(trans->list, ot);
    
    list2 = dict_get(trans->names, ot->name);
    if (list2 == NULL) {
    	list2 = gwlist_create();
	dict_put(trans->names, ot->name, list2);
    }
    gwlist_append(list2, ot);

    return 0;
}


int urltrans_add_cfg(URLTranslationList *trans, Cfg *cfg) 
{
    CfgGroup *grp;
    List *list;
    
    list = cfg_get_multi_group(cfg, octstr_imm("sms-service"));
    while (list && (grp = gwlist_extract_first(list)) != NULL) {
	if (urltrans_add_one(trans, grp) == -1) {
	    gwlist_destroy(list, NULL);
	    return -1;
	}
    }
    gwlist_destroy(list, NULL);
    
    list = cfg_get_multi_group(cfg, octstr_imm("sendsms-user"));
    while (list && (grp = gwlist_extract_first(list)) != NULL) {
	if (urltrans_add_one(trans, grp) == -1) {
	    gwlist_destroy(list, NULL);
	    return -1;
	}
    }
    gwlist_destroy(list, NULL);

    return 0;
}


URLTranslation *urltrans_find(URLTranslationList *trans, Msg *msg) 
{
    URLTranslation *t = NULL;
    
    t = find_translation(trans, msg);
    if (t == NULL) {
        t = find_default_translation(trans, msg->sms.smsc_id, msg->sms.sender, msg->sms.receiver, msg->sms.account);
    }

    return t;
}


URLTranslation *urltrans_find_service(URLTranslationList *trans, Msg *msg)
{
    URLTranslation *t;
    List *list;
    
    list = dict_get(trans->names, msg->sms.service);
    if (list != NULL) {
       t = gwlist_get(list, 0);
    } else  {
       t = NULL;
    }
    return t;
}



URLTranslation *urltrans_find_username(URLTranslationList *trans, Octstr *name)
{
    URLTranslation *t;
    int i;

    gw_assert(name != NULL);
    for (i = 0; i < gwlist_len(trans->list); ++i) {
	t = gwlist_get(trans->list, i);
	if (t->type == TRANSTYPE_SENDSMS) {
	    if (octstr_compare(name, t->username) == 0)
		return t;
	}
    }
    return NULL;
}

/*
 * Remove the first word and the whitespace that follows it from
 * the start of the message data.
 */
static void strip_keyword(Msg *request)
{          
    int ch;
    long pos;

    pos = 0;

    for (; (ch = octstr_get_char(request->sms.msgdata, pos)) >= 0; pos++)
        if (isspace(ch))
            break;

    for (; (ch = octstr_get_char(request->sms.msgdata, pos)) >= 0; pos++)
        if (!isspace(ch))
            break;

    octstr_delete(request->sms.msgdata, 0, pos);
}


Octstr *urltrans_fill_escape_codes(Octstr *pattern, Msg *request)
{
    Octstr *enc;
    Octstr *meta_group, *meta_param;
    int nextarg, j;
    struct tm tm;
    int num_words;
    List *word_list;
    Octstr *result;
    long pattern_len;
    long pos;
    int c;
    long i, k;
    Octstr *temp;

    result = octstr_create("");

    if (request->sms.msgdata) {
        word_list = octstr_split_words(request->sms.msgdata);
        num_words = gwlist_len(word_list);
    } else {
        word_list = gwlist_create();
        num_words = 0;
    }
    
    pattern_len = octstr_len(pattern);
    nextarg = 1;
    pos = 0;
    for (;;) {
        while (pos < pattern_len) {
            c = octstr_get_char(pattern, pos);
            if (c == '%' && pos + 1 < pattern_len)
                break;
            octstr_append_char(result, c);
            ++pos;
        }

        if (pos == pattern_len)
            break;

    switch (octstr_get_char(pattern, pos + 1)) {
    case 'a':
        for (j = 0; j < num_words; ++j) {
        enc = octstr_duplicate(gwlist_get(word_list, j));
        octstr_url_encode(enc);
        if (j > 0)
            octstr_append_char(result, '+');
        octstr_append(result, enc);
        octstr_destroy(enc);
        }
        break;

    case 'A':
        if (request->sms.msgdata) {
        enc = octstr_duplicate(request->sms.msgdata);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        }
        break;

    case 'b':
        enc = octstr_duplicate(request->sms.msgdata);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'B':  /* billing identifier/information */
        if (octstr_len(request->sms.binfo)) {
            enc = octstr_duplicate(request->sms.binfo);
            octstr_url_encode(enc);
            octstr_append(result, enc);
            octstr_destroy(enc);
        }
        break;

    case 'c':
        octstr_append_decimal(result, request->sms.coding);
        break;

    case 'C':
        if (octstr_len(request->sms.charset)) {
            octstr_append(result, request->sms.charset);
        } else {
            switch (request->sms.coding) {
            case DC_UNDEF:
            case DC_7BIT:
                octstr_append(result, octstr_imm("UTF-8"));
                break;
            case DC_8BIT:
                octstr_append(result, octstr_imm("8-BIT"));
                break;
            case DC_UCS2:
                octstr_append(result, octstr_imm("UTF-16BE"));
                break;
            }
        }
        break;

    case 'd':
        enc = octstr_create("");
        octstr_append_decimal(enc, request->sms.dlr_mask);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'D': /* meta_data */
        if (octstr_len(request->sms.meta_data)) {
            enc = octstr_duplicate(request->sms.meta_data);
            octstr_url_encode(enc);
            octstr_append(result, enc);
            octstr_destroy(enc);
        }
        break;

    case 'f':  /* smsc number*/
        if (octstr_len(request->sms.smsc_number)) {
            enc = octstr_duplicate(request->sms.smsc_number);
            octstr_url_encode(enc);
            octstr_append(result, enc);
            octstr_destroy(enc);
        }
        break;

    case 'F':
        if (request->sms.foreign_id == NULL)
            break;
        enc = octstr_duplicate(request->sms.foreign_id);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'i':
        if (request->sms.smsc_id == NULL)
        break;
        enc = octstr_duplicate(request->sms.smsc_id);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'I':
        if (!uuid_is_null(request->sms.id)) {
                char id[UUID_STR_LEN + 1];
                uuid_unparse(request->sms.id, id);
            octstr_append_cstr(result, id);
            }
        break;

    case 'k':
        if (num_words <= 0)
        break;
        enc = octstr_duplicate(gwlist_get(word_list, 0));
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'm':  /* mclass - message class */
        enc = octstr_create("");
        octstr_append_decimal(enc, request->sms.mclass);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'M':  /* mwi - message waiting indicator */
        enc = octstr_create("");
        octstr_append_decimal(enc, request->sms.mwi);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'n':
        if (request->sms.service == NULL)
        break;
        enc = octstr_duplicate(request->sms.service);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'o':  /* account information (may be operator id for aggregators */
        if (octstr_len(request->sms.account)) {
            enc = octstr_duplicate(request->sms.account);
            octstr_url_encode(enc);
            octstr_append(result, enc);
            octstr_destroy(enc);
        }
        break;

    case 'O':  /* DCS */
    {
        int dcs;
        dcs = fields_to_dcs(request, request->sms.alt_dcs);
        octstr_format_append(result, "%02d", dcs);
        break;
    }

    /* NOTE: the sender and receiver is already switched in
     *    message, so that's why we must use 'sender' when
     *    we want original receiver and vice versa
     */
    case 'P':
        enc = octstr_duplicate(request->sms.sender);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'p':
        enc = octstr_duplicate(request->sms.receiver);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        break;

    case 'q':
        if (strncmp(octstr_get_cstr(request->sms.receiver),"00",2)==0) {
        enc = octstr_copy(request->sms.receiver, 2, 
                          octstr_len(request->sms.receiver));
        octstr_url_encode(enc);
        octstr_format_append(result, "%%2B%S", enc);
        octstr_destroy(enc);
        } else {
        enc = octstr_duplicate(request->sms.receiver);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        }
        break;

    case 'Q':
        if (strncmp(octstr_get_cstr(request->sms.sender), "00", 2) == 0) {
        enc = octstr_copy(request->sms.sender, 2, 
                          octstr_len(request->sms.sender));
        octstr_url_encode(enc);
        octstr_format_append(result, "%%2B%S", enc);
        octstr_destroy(enc);
        } else {
        enc = octstr_duplicate(request->sms.sender);
                octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        }
        break;

    case 'r':
        for (j = nextarg; j < num_words; ++j) {
        enc = octstr_duplicate(gwlist_get(word_list, j));
        octstr_url_encode(enc);
        if (j != nextarg)
            octstr_append_char(result, '+');
        octstr_append(result, enc);
        octstr_destroy(enc);
        }
        break;

    case 'R': /* dlr_url */
        if (octstr_len(request->sms.dlr_url)) {
            enc = octstr_duplicate(request->sms.dlr_url);
            octstr_url_encode(enc);
            octstr_append(result, enc);
            octstr_destroy(enc);
        }
        break;

    case 's':
        if (nextarg >= num_words)
        	break;
        enc = octstr_duplicate(gwlist_get(word_list, nextarg));
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
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

    case 't':
        tm = gw_gmtime(request->sms.time);
        octstr_format_append(result, "%04d-%02d-%02d+%02d:%02d:%02d",
                 tm.tm_year + 1900,
                 tm.tm_mon + 1,
                 tm.tm_mday,
                 tm.tm_hour,
                 tm.tm_min,
                 tm.tm_sec);
        break;

    case 'T':
        if (request->sms.time == MSG_PARAM_UNDEFINED)
        break;
        octstr_format_append(result, "%ld", request->sms.time);
        break;

    case 'u':
        if(octstr_len(request->sms.udhdata)) {
        enc = octstr_duplicate(request->sms.udhdata);
        octstr_url_encode(enc);
        octstr_append(result, enc);
        octstr_destroy(enc);
        }
        break;

    case 'v':
        if (request->sms.validity != MSG_PARAM_UNDEFINED) {
            octstr_format_append(result, "%ld", (request->sms.validity - time(NULL)) / 60);
        }
        break;

    case 'V':
        if (request->sms.deferred != MSG_PARAM_UNDEFINED) {
            octstr_format_append(result, "%ld", (request->sms.deferred - time(NULL)) / 60);
        }
        break;
    
    /*
     * This allows to pass meta-data individual parameters into urls.
     * The syntax is as follows: %#group#parameter#
     * For example: %#smpp#my_param# would be replaced with the value
     * 'my_param' from the group 'smpp' coming inside the meta_data field.
     */
    case '#':
        /* ASCII 0x23 == '#' */
        k = octstr_search_char(pattern, 0x23, pos + 2);
        if (k >= 0) {
            pos += 2;
            meta_group = octstr_copy(pattern, pos, (k-pos));
            pos = k + 1;
            k = octstr_search_char(pattern, 0x23, pos);
            if (k >= 0) {
                meta_param = octstr_copy(pattern, pos, (k-pos));
                pos = k - 1;
                if (request->sms.meta_data != NULL) {
                    enc = meta_data_get_value(request->sms.meta_data,
                            octstr_get_cstr(meta_group), meta_param);
                    octstr_url_encode(enc);
                    octstr_append(result, enc);
                    octstr_destroy(enc);
                }
                octstr_destroy(meta_param);
            } else {
                pos++;
            }
            octstr_destroy(meta_group);
        }
        break;

    /* XXX sms.parameters not present in here:
     *   * pid - will we receive this ? 
     *   * alt-dcs - shouldn't be required unless we want to inform 
     *               which alt-dcs external server should use back
     *   * compress - if we use compression, probably kannel would 
     *                decompress and reset this to 0. not required
     *   * rpi - we don't receive these from smsc
     *   * username, password, dlr-url, account - nonsense to send
     */
    case '%':
        octstr_format_append(result, "%%");
        break;

    default:
        octstr_format_append(result, "%%%c",
                             octstr_get_char(pattern, pos + 1));
        break;
    }

    pos += 2;
    }
    
    gwlist_destroy(word_list, octstr_destroy_item);    

    return result;    
}


/*
 * Trans being NULL means that we are servicing ppg (doing dlr, but this does not
 * concern us here).
 */
Octstr *urltrans_get_pattern(URLTranslation *t, Msg *request)
{
    Octstr *result, *pattern;
    
    if (request->sms.sms_type != report_mo && t->type == TRANSTYPE_SENDSMS)
        return octstr_create("");

    /* check if this is a delivery report message or not */
    if (request->sms.sms_type != report_mo) {
        pattern = t->pattern;
    } else {
        /* this is a DLR message */
        pattern = request->sms.dlr_url;
        if (octstr_len(pattern) == 0) {
            if (t && octstr_len(t->dlr_url)) {
                pattern = t->dlr_url;
            } else {
                return octstr_create("");
            }
        }
    }

    /* We have pulled this out into an own exported function. This 
     * gives other modules the chance to use the same escape code
     * semantics for Msgs. */
    result = urltrans_fill_escape_codes(pattern, request);

    /*
     * this SHOULD be done in smsbox, not here, but well,
     * much easier to do here
     */
    if (t && (t->type == TRANSTYPE_POST_URL || t->type == TRANSTYPE_POST_XML)
		    && t->strip_keyword)
	strip_keyword(request);

    return result;
}


int urltrans_type(URLTranslation *t) 
{
    return t->type;
}

Octstr *urltrans_prefix(URLTranslation *t) 
{
    return t->prefix;
}

Octstr *urltrans_suffix(URLTranslation *t) 
{
    return t->suffix;
}

Octstr *urltrans_default_sender(URLTranslation *t) 
{
    return t->default_sender;
}

Octstr *urltrans_faked_sender(URLTranslation *t) 
{
    return t->faked_sender;
}

int urltrans_max_messages(URLTranslation *t) 
{
    return t->max_messages;
}

int urltrans_concatenation(URLTranslation *t) 
{
    return t->concatenation;
}

Octstr *urltrans_split_chars(URLTranslation *t) 
{
    return t->split_chars;
}

Octstr *urltrans_split_suffix(URLTranslation *t) 
{
    return t->split_suffix;
}

int urltrans_omit_empty(URLTranslation *t) 
{
    return t->omit_empty;
}

Octstr *urltrans_header(URLTranslation *t) 
{
    return t->header;
}

Octstr *urltrans_footer(URLTranslation *t) 
{
    return t->footer;
}

Octstr *urltrans_alt_charset(URLTranslation *t)
{
    return t->alt_charset;
}

Octstr *urltrans_name(URLTranslation *t) 
{
    return t->name;
}

Octstr *urltrans_username(URLTranslation *t) 
{
    return t->username;
}

Octstr *urltrans_password(URLTranslation *t) 
{
    return t->password;
}

Octstr *urltrans_forced_smsc(URLTranslation *t) 
{
    return t->forced_smsc;
}

Octstr *urltrans_default_smsc(URLTranslation *t) 
{
    return t->default_smsc;
}

Octstr *urltrans_allow_ip(URLTranslation *t) 
{
    return t->allow_ip;
}

Octstr *urltrans_deny_ip(URLTranslation *t) 
{
    return t->deny_ip;
}

Octstr *urltrans_allowed_prefix(URLTranslation *t) 
{
    return t->allowed_prefix;
}

Octstr *urltrans_denied_prefix(URLTranslation *t) 
{
    return t->denied_prefix;
}

Octstr *urltrans_allowed_recv_prefix(URLTranslation *t) 
{
    return t->allowed_recv_prefix;
}

Octstr *urltrans_denied_recv_prefix(URLTranslation *t) 
{
    return t->denied_recv_prefix;
}

Numhash *urltrans_white_list(URLTranslation *t)
{
    return t->white_list;
}

regex_t *urltrans_white_list_regex(URLTranslation *t)
{
    return t->white_list_regex;
}

Numhash *urltrans_black_list(URLTranslation *t)
{
    return t->black_list;
}

regex_t *urltrans_black_list_regex(URLTranslation *t)
{
    return t->black_list_regex;
}

int urltrans_assume_plain_text(URLTranslation *t) 
{
    return t->assume_plain_text;
}

int urltrans_accept_x_kannel_headers(URLTranslation *t) 
{
    return t->accept_x_kannel_headers;
}

int urltrans_strip_keyword(URLTranslation *t) 
{
    return t->strip_keyword;
}

int urltrans_send_sender(URLTranslation *t) 
{
    return t->send_sender;
}

Octstr *urltrans_dlr_url(URLTranslation *t)
{
    return t->dlr_url;
}

int urltrans_dlr_mask(URLTranslation *t)
{
    return t->dlr_mask;
}


/***********************************************************************
 * Internal functions.
 */


/*
 * Create one URLTranslation. Return NULL for failure, pointer to it for OK.
 */
static URLTranslation *create_onetrans(CfgGroup *grp)
{
    URLTranslation *ot;
    Octstr *url, *post_url, *post_xml, *text, *file, *exec;
    Octstr *accepted_smsc, *accepted_account, *forced_smsc, *default_smsc;
    Octstr *grpname;
    int is_sms_service, regex_flag = REG_EXTENDED;
    Octstr *accepted_smsc_regex;
    Octstr *accepted_account_regex;
    Octstr *allowed_prefix_regex;
    Octstr *denied_prefix_regex;
    Octstr *allowed_receiver_prefix_regex;
    Octstr *denied_receiver_prefix_regex;
    Octstr *white_list_regex;
    Octstr *black_list_regex;
    Octstr *keyword_regex;
    Octstr *os, *tmp;
    
    grpname = cfg_get_group_name(grp);
    if (grpname == NULL)
    	return NULL;

    if (octstr_str_compare(grpname, "sms-service") == 0)
        is_sms_service = 1;
    else if (octstr_str_compare(grpname, "sendsms-user") == 0)
        is_sms_service = 0;
    else {
        octstr_destroy(grpname);
        return NULL;
    }
    octstr_destroy(grpname);

    ot = gw_malloc(sizeof(URLTranslation));
    memset(ot, 0, sizeof(*ot));
    
    if (is_sms_service) {
        cfg_get_bool(&ot->catch_all, grp, octstr_imm("catch-all"));

        ot->dlr_url = cfg_get(grp, octstr_imm("dlr-url"));
        if (cfg_get_integer(&ot->dlr_mask, grp, octstr_imm("dlr-mask")) == -1)
            ot->dlr_mask = DLR_UNDEFINED;
        ot->alt_charset = cfg_get(grp, octstr_imm("alt-charset"));
	    
        url = cfg_get(grp, octstr_imm("get-url"));
        if (url == NULL)
            url = cfg_get(grp, octstr_imm("url"));
	    
	post_url = cfg_get(grp, octstr_imm("post-url"));
	post_xml = cfg_get(grp, octstr_imm("post-xml"));
	file = cfg_get(grp, octstr_imm("file"));
	text = cfg_get(grp, octstr_imm("text"));
	exec = cfg_get(grp, octstr_imm("exec"));
	if (url != NULL) {
	    ot->type = TRANSTYPE_GET_URL;
	    ot->pattern = octstr_duplicate(url);
	} else if (post_url != NULL) {
	    ot->type = TRANSTYPE_POST_URL;
	    ot->pattern = octstr_duplicate(post_url);
	    ot->catch_all = 1;
	} else if (post_xml != NULL) {
	    ot->type = TRANSTYPE_POST_XML;
	    ot->pattern = octstr_duplicate(post_xml);
	    ot->catch_all = 1;
	} else if (file != NULL) {
	    ot->type = TRANSTYPE_FILE;
	    ot->pattern = octstr_duplicate(file);
	} else if (text != NULL) {
	    ot->type = TRANSTYPE_TEXT;
	    ot->pattern = octstr_duplicate(text);
	} else if (exec != NULL) {
	    ot->type = TRANSTYPE_EXECUTE;
	    ot->pattern = octstr_duplicate(exec);
	} else {
	    octstr_destroy(url);
	    octstr_destroy(post_url);
	    octstr_destroy(post_xml);
	    octstr_destroy(file);
	    octstr_destroy(text);
	    octstr_destroy(exec);
	    error(0, "Configuration group `sms-service' "
	    	     "did not specify get-url, post-url, post-xml, file or text.");
    	    goto error;
	}
	octstr_destroy(url);
	octstr_destroy(post_url);
	octstr_destroy(post_xml);
	octstr_destroy(file);
	octstr_destroy(text);
	octstr_destroy(exec);

	tmp = cfg_get(grp, octstr_imm("keyword"));
        keyword_regex = cfg_get(grp, octstr_imm("keyword-regex"));
	if (tmp == NULL && keyword_regex == NULL) {
	    error(0, "Group 'sms-service' must include either 'keyword' or 'keyword-regex'.");
	    goto error;
	}
	if (tmp != NULL && keyword_regex != NULL) {
	    error(0, "Group 'sms-service' may inlcude either 'keyword' or 'keyword-regex'.");
	    octstr_destroy(tmp);
	    octstr_destroy(keyword_regex);
	    goto error;
	}
	
	if (tmp != NULL && octstr_str_compare(tmp, "default") == 0) {
	    /* default sms-service */
	    ot->keyword_regex = NULL;
	    octstr_destroy(tmp);
	} else if (tmp != NULL) {
	    Octstr *aliases;
	    
	    /* convert to regex */
	    regex_flag |= REG_ICASE;
	    keyword_regex = octstr_format("^[ ]*(%S", tmp);
	    octstr_destroy(tmp);

	    aliases = cfg_get(grp, octstr_imm("aliases"));
	    if (aliases != NULL) {
	        long i;
	        List *l;

	        l = octstr_split(aliases, octstr_imm(";"));
	        octstr_destroy(aliases);
	        
	        for (i = 0; i < gwlist_len(l); ++i) {
	            os = gwlist_get(l, i);
	            octstr_format_append(keyword_regex, "|%S", os);
	        }
	        gwlist_destroy(l, octstr_destroy_item);
	    }
	    
	    octstr_append_cstr(keyword_regex, ")[ ]*");
	}

        if (keyword_regex != NULL && (ot->keyword_regex = gw_regex_comp(keyword_regex, regex_flag)) == NULL) {
            error(0, "Could not compile pattern '%s'", octstr_get_cstr(keyword_regex));
            octstr_destroy(keyword_regex);
            goto error;
        }

	ot->name = cfg_get(grp, octstr_imm("name"));
	if (ot->name == NULL)
	    ot->name = keyword_regex ? octstr_duplicate(keyword_regex) : octstr_create("default");
	octstr_destroy(keyword_regex);

	accepted_smsc = cfg_get(grp, octstr_imm("accepted-smsc"));
	if (accepted_smsc != NULL) {
	    ot->accepted_smsc = octstr_split(accepted_smsc, octstr_imm(";"));
	    octstr_destroy(accepted_smsc);
	}
	accepted_account = cfg_get(grp, octstr_imm("accepted-account"));
	if (accepted_account != NULL) {
	    ot->accepted_account = octstr_split(accepted_account, octstr_imm(";"));
	    octstr_destroy(accepted_account);
	}
        accepted_smsc_regex = cfg_get(grp, octstr_imm("accepted-smsc-regex"));
        if (accepted_smsc_regex != NULL) { 
            if ( (ot->accepted_smsc_regex = gw_regex_comp(accepted_smsc_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(accepted_smsc_regex));
            octstr_destroy(accepted_smsc_regex);
        }
        accepted_account_regex = cfg_get(grp, octstr_imm("accepted-account-regex"));
        if (accepted_account_regex != NULL) { 
            if ( (ot->accepted_account_regex = gw_regex_comp(accepted_account_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(accepted_account_regex));
            octstr_destroy(accepted_account_regex);
        }

	cfg_get_bool(&ot->assume_plain_text, grp, 
		     octstr_imm("assume-plain-text"));
	cfg_get_bool(&ot->accept_x_kannel_headers, grp, 
		     octstr_imm("accept-x-kannel-headers"));
	cfg_get_bool(&ot->strip_keyword, grp, octstr_imm("strip-keyword"));
	cfg_get_bool(&ot->send_sender, grp, octstr_imm("send-sender"));
	
	ot->prefix = cfg_get(grp, octstr_imm("prefix"));
	ot->suffix = cfg_get(grp, octstr_imm("suffix"));
        ot->allowed_recv_prefix = cfg_get(grp, octstr_imm("allowed-receiver-prefix"));
        allowed_receiver_prefix_regex = cfg_get(grp, octstr_imm("allowed-receiver-prefix-regex"));
        if (allowed_receiver_prefix_regex != NULL) {
            if ((ot->allowed_receiver_prefix_regex = gw_regex_comp(allowed_receiver_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(allowed_receiver_prefix_regex));
            octstr_destroy(allowed_receiver_prefix_regex);
        }

	ot->allowed_recv_prefix = cfg_get(grp, octstr_imm("allowed-receiver-prefix"));
	ot->denied_recv_prefix = cfg_get(grp, octstr_imm("denied-receiver-prefix"));
        denied_receiver_prefix_regex = cfg_get(grp, octstr_imm("denied-receiver-prefix-regex"));
        if (denied_receiver_prefix_regex != NULL) {
            if ((ot->denied_receiver_prefix_regex = gw_regex_comp(denied_receiver_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'",octstr_get_cstr(denied_receiver_prefix_regex));
            octstr_destroy(denied_receiver_prefix_regex);
        }

	ot->args = count_occurences(ot->pattern, octstr_imm("%s"));
	ot->args += count_occurences(ot->pattern, octstr_imm("%S"));
	ot->has_catchall_arg = 
	    (count_occurences(ot->pattern, octstr_imm("%r")) > 0) ||
	    (count_occurences(ot->pattern, octstr_imm("%a")) > 0);

    } else {
	ot->type = TRANSTYPE_SENDSMS;
	ot->pattern = octstr_create("");
	ot->args = 0;
	ot->has_catchall_arg = 0;
	ot->catch_all = 1;
	ot->username = cfg_get(grp, octstr_imm("username"));
	ot->password = cfg_get(grp, octstr_imm("password"));
	ot->dlr_url = cfg_get(grp, octstr_imm("dlr-url"));
	grp_dump(grp);
	if (ot->password == NULL) {
	    error(0, "Password required for send-sms user");
	    goto error;
	}
	ot->name = cfg_get(grp, octstr_imm("name"));
	if (ot->name == NULL)
	    ot->name = octstr_duplicate(ot->username);

	forced_smsc = cfg_get(grp, octstr_imm("forced-smsc"));
	default_smsc = cfg_get(grp, octstr_imm("default-smsc"));
	if (forced_smsc != NULL) {
	    if (default_smsc != NULL) {
		info(0, "Redundant default-smsc for send-sms user %s", 
		     octstr_get_cstr(ot->username));
	    }
	    ot->forced_smsc = forced_smsc;
	    octstr_destroy(default_smsc);
	} else  if (default_smsc != NULL)
	    ot->default_smsc = default_smsc;

	ot->deny_ip = cfg_get(grp, octstr_imm("user-deny-ip"));
	ot->allow_ip = cfg_get(grp, octstr_imm("user-allow-ip"));
	ot->default_sender = cfg_get(grp, octstr_imm("default-sender"));
    }
    
    ot->allowed_prefix = cfg_get(grp, octstr_imm("allowed-prefix"));
    allowed_prefix_regex = cfg_get(grp, octstr_imm("allowed-prefix-regex"));
    if (allowed_prefix_regex != NULL) {
        if ((ot->allowed_prefix_regex = gw_regex_comp(allowed_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(allowed_prefix_regex));
        octstr_destroy(allowed_prefix_regex);
    }
    ot->denied_prefix = cfg_get(grp, octstr_imm("denied-prefix"));
    denied_prefix_regex = cfg_get(grp, octstr_imm("denied-prefix-regex"));
    if (denied_prefix_regex != NULL) {
        if ((ot->denied_prefix_regex = gw_regex_comp(denied_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(denied_prefix_regex));
        octstr_destroy(denied_prefix_regex);
    }
    
    os = cfg_get(grp, octstr_imm("white-list"));
    if (os != NULL) {
        ot->white_list = numhash_create(octstr_get_cstr(os));
        octstr_destroy(os);
    }
    white_list_regex = cfg_get(grp, octstr_imm("white-list-regex"));
    if (white_list_regex != NULL) {
        if ((ot->white_list_regex = gw_regex_comp(white_list_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(white_list_regex));
        octstr_destroy(white_list_regex);
    }

    os = cfg_get(grp, octstr_imm("black-list"));
    if (os != NULL) {
        ot->black_list = numhash_create(octstr_get_cstr(os));
        octstr_destroy(os);
    }
    black_list_regex = cfg_get(grp, octstr_imm("black-list-regex"));
    if (black_list_regex != NULL) {
        if ((ot->black_list_regex = gw_regex_comp(black_list_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(black_list_regex));
        octstr_destroy(black_list_regex);
    }

    if (cfg_get_integer(&ot->max_messages, grp, octstr_imm("max-messages")) == -1)
	ot->max_messages = 1;
    cfg_get_bool(&ot->concatenation, grp, octstr_imm("concatenation"));
    cfg_get_bool(&ot->omit_empty, grp, octstr_imm("omit-empty"));
    
    ot->header = cfg_get(grp, octstr_imm("header"));
    ot->footer = cfg_get(grp, octstr_imm("footer"));
    ot->faked_sender = cfg_get(grp, octstr_imm("faked-sender"));
    ot->split_chars = cfg_get(grp, octstr_imm("split-chars"));
    ot->split_suffix = cfg_get(grp, octstr_imm("split-suffix"));

    if ( (ot->prefix == NULL && ot->suffix != NULL) ||
	 (ot->prefix != NULL && ot->suffix == NULL) ) {
	warning(0, "Service : suffix and prefix are only used"
		   " if both are set.");
    }
    if ((ot->prefix != NULL || ot->suffix != NULL) &&
        ot->type != TRANSTYPE_GET_URL) {
	warning(0, "Service : suffix and prefix are only used"
                   " if type is 'get-url'.");
    }
    
    return ot;

error:
    error(0, "Couldn't create a URLTranslation.");
    destroy_onetrans(ot);
    return NULL;
}


/*
 * Free one URLTranslation.
 */
static void destroy_onetrans(void *p) 
{
    URLTranslation *ot;
    
    ot = p;
    if (ot != NULL) {
	octstr_destroy(ot->dlr_url);
	octstr_destroy(ot->pattern);
	octstr_destroy(ot->prefix);
	octstr_destroy(ot->suffix);
	octstr_destroy(ot->default_sender);
	octstr_destroy(ot->faked_sender);
	octstr_destroy(ot->split_chars);
	octstr_destroy(ot->split_suffix);
	octstr_destroy(ot->header);
	octstr_destroy(ot->footer);
	octstr_destroy(ot->alt_charset);
	gwlist_destroy(ot->accepted_smsc, octstr_destroy_item);
	gwlist_destroy(ot->accepted_account, octstr_destroy_item);
	octstr_destroy(ot->name);
	octstr_destroy(ot->username);
	octstr_destroy(ot->password);
	octstr_destroy(ot->forced_smsc);
	octstr_destroy(ot->default_smsc);
	octstr_destroy(ot->allow_ip);
	octstr_destroy(ot->deny_ip);
	octstr_destroy(ot->allowed_prefix);
	octstr_destroy(ot->denied_prefix);
	octstr_destroy(ot->allowed_recv_prefix);
	octstr_destroy(ot->denied_recv_prefix);
	numhash_destroy(ot->white_list);
	numhash_destroy(ot->black_list);
        if (ot->keyword_regex != NULL) gw_regex_destroy(ot->keyword_regex);
        if (ot->accepted_smsc_regex != NULL) gw_regex_destroy(ot->accepted_smsc_regex);
        if (ot->accepted_account_regex != NULL) gw_regex_destroy(ot->accepted_account_regex);
        if (ot->allowed_prefix_regex != NULL) gw_regex_destroy(ot->allowed_prefix_regex);
        if (ot->denied_prefix_regex != NULL) gw_regex_destroy(ot->denied_prefix_regex);
        if (ot->allowed_receiver_prefix_regex != NULL) gw_regex_destroy(ot->allowed_receiver_prefix_regex);
        if (ot->denied_receiver_prefix_regex != NULL) gw_regex_destroy(ot->denied_receiver_prefix_regex);
        if (ot->white_list_regex != NULL) gw_regex_destroy(ot->white_list_regex);
        if (ot->black_list_regex != NULL) gw_regex_destroy(ot->black_list_regex);
	gw_free(ot);
    }
}


/*
 * checks if the number of passed words matches the service-pattern defined in the
 * translation. returns 0 if arguments are okay, -1 otherwise.
 */
static int check_num_args(URLTranslation *t, List *words)
{
    const int IS_OKAY = 0;
    const int NOT_OKAY = -1;
    int n;

    
    n = gwlist_len(words);
    /* check number of arguments */
    if (t->catch_all)
        return IS_OKAY;
    
    if (n - 1 == t->args)
        return IS_OKAY;

    if (t->has_catchall_arg && n - 1 >= t->args)
        return IS_OKAY;

    return NOT_OKAY;
}

/*
 * checks if a request matches the parameters of a URL-Translation, e.g. whether or not 
 * a user is allowed to use certain services. returns 0 if allowed, -1 if not.
 */
static int check_allowed_translation(URLTranslation *t,
                  Octstr *smsc, Octstr *sender, Octstr *receiver, Octstr *account)
{
    const int IS_ALLOWED = 0;
    const int NOT_ALLOWED = -1;

    /* if smsc_id set and accepted_smsc exist, accept
     * translation only if smsc id is in accept string
     */
    if (smsc && t->accepted_smsc && !gwlist_search(t->accepted_smsc, smsc, octstr_item_match))
        return NOT_ALLOWED;

    if (smsc && t->accepted_smsc_regex && gw_regex_match_pre( t->accepted_smsc_regex, smsc) == 0)
        return NOT_ALLOWED;

    /* if account_id set and accepted_account exist, accept
     * translation only if smsc id is in accept string
     */
    if (account && t->accepted_account && !gwlist_search(t->accepted_account, account, octstr_item_match))
        return NOT_ALLOWED;

    if (account && t->accepted_account_regex && gw_regex_match_pre( t->accepted_account_regex, account) == 0)
        return NOT_ALLOWED;

    /* Have allowed for sender */
    if (t->allowed_prefix && !t->denied_prefix && does_prefix_match(t->allowed_prefix, sender) != 1)
        return NOT_ALLOWED;

    if (t->allowed_prefix_regex && !t->denied_prefix_regex && gw_regex_match_pre(t->allowed_prefix_regex, sender) == 0)
        return NOT_ALLOWED;

    /* Have denied for sender */
    if (t->denied_prefix && !t->allowed_prefix && does_prefix_match(t->denied_prefix, sender) == 1)
        return NOT_ALLOWED;

    if (t->denied_prefix_regex && !t->allowed_prefix_regex && gw_regex_match_pre(t->denied_prefix_regex, sender) == 1)
        return NOT_ALLOWED;

    /* Have allowed for receiver */
    if (t->allowed_recv_prefix && !t->denied_recv_prefix && does_prefix_match(t->allowed_recv_prefix, receiver) != 1)
        return NOT_ALLOWED;

    if (t->allowed_receiver_prefix_regex && !t->denied_receiver_prefix_regex &&
        gw_regex_match_pre(t->allowed_receiver_prefix_regex, receiver) == 0)
        return NOT_ALLOWED;

    /* Have denied for receiver */
    if (t->denied_recv_prefix && !t->allowed_recv_prefix && does_prefix_match(t->denied_recv_prefix, receiver) == 1)
        return NOT_ALLOWED;

    if (t->denied_receiver_prefix_regex && !t->allowed_receiver_prefix_regex &&
        gw_regex_match_pre(t->denied_receiver_prefix_regex, receiver) == 0)
        return NOT_ALLOWED;

    if (t->white_list && numhash_find_number(t->white_list, sender) < 1) {
        return NOT_ALLOWED;
    }

    if (t->white_list_regex && gw_regex_match_pre(t->white_list_regex, sender) == 0) {
        return NOT_ALLOWED;
    }   

    if (t->black_list && numhash_find_number(t->black_list, sender) == 1) {
        return NOT_ALLOWED;
    }

    if (t->black_list_regex && gw_regex_match_pre(t->black_list_regex, sender) == 1) {
        return NOT_ALLOWED;
    }   

    /* Have allowed and denied */
    if (t->denied_prefix && t->allowed_prefix && does_prefix_match(t->allowed_prefix, sender) != 1 &&
        does_prefix_match(t->denied_prefix, sender) == 1)
        return NOT_ALLOWED;

    if (t->denied_prefix_regex && t->allowed_prefix_regex &&
        gw_regex_match_pre(t->allowed_prefix_regex, sender) == 0 &&
        gw_regex_match_pre(t->denied_prefix_regex, sender) == 1)
        return NOT_ALLOWED;

    return IS_ALLOWED;
};

    
/* get_matching_translations - iterate over all translations in trans. 
 * for each translation check whether 
 * the translation's keyword has already been interpreted as a regexp. 
 * if not, compile it now,
 * otherwise retrieve compilation result from dictionary.
 *
 * the translations where the word matches the translation's pattern 
 * are returned in a list
 * 
 */
static List *get_matching_translations(URLTranslationList *trans, Octstr *msg) 
{
    List *list;
    long i;
    URLTranslation *t;

    gw_assert(trans != NULL && msg != NULL);

    list = gwlist_create();
    for (i = 0; i < gwlist_len(trans->list); ++i) {
        t = gwlist_get(trans->list, i);
        
        if (t->keyword_regex == NULL)
            continue;

        if (gw_regex_match_pre(t->keyword_regex, msg) == 1) {
            debug("", 0, "match found: %s", octstr_get_cstr(t->name));
            gwlist_append(list, t);
        } else {
            debug("", 0, "no match found: %s", octstr_get_cstr(t->name));
        }
    }

    return list;
}

/*
 * Find the appropriate translation 
 */
static URLTranslation *find_translation(URLTranslationList *trans, Msg *msg)
{
    Octstr *data;
    int i;
    URLTranslation *t = NULL;
    List *list, *words;

    /* convert tolower and try to match */
    data = octstr_duplicate(msg->sms.msgdata);
    i = 0;
    while((i = octstr_search_char(data, 0, i)) != -1 && i < octstr_len(data) - 1) {
        octstr_delete(data, i, 1);
    }
    
    list = get_matching_translations(trans, data);
    words = octstr_split_words(data);

    /**
     * List now contains all translations where the keyword of the sms 
     * matches the pattern defined by the tranlsation's keyword.
     */
    for (i = 0; i < gwlist_len(list); ++i) {
        t = gwlist_get(list, i);

        /* TODO check_num_args, do we really need this??? */
        if (check_allowed_translation(t, msg->sms.smsc_id, msg->sms.sender, msg->sms.receiver, msg->sms.account) == 0
            && check_num_args(t, words) == 0)
            break;

        t = NULL;
    }

    octstr_destroy(data);
    gwlist_destroy(words, octstr_destroy_item);
    gwlist_destroy(list, NULL);
    
    return t;
}


static URLTranslation *find_default_translation(URLTranslationList *trans,
						Octstr *smsc, Octstr *sender, Octstr *receiver,
						Octstr *account)
{
    URLTranslation *t;
    int i;
    List *list;

    list = trans->defaults;
    t = NULL;
    for (i = 0; i < gwlist_len(list); ++i) {
	t = gwlist_get(list, i);

        if (check_allowed_translation(t, smsc, sender, receiver, account) == 0)
            break;

	t = NULL;
    }

    return t;
}

/*
 * Count the number of times `pat' occurs in `str'.
 */
static long count_occurences(Octstr *str, Octstr *pat)
{
    long count;
    long pos;
    long len;
    
    count = 0;
    pos = 0;
    len = octstr_len(pat);
    while ((pos = octstr_search(str, pat, pos)) != -1) {
    	++count;
	pos += len;
    }
    return count;
}

