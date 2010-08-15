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

/*
 * urltrans.h - URL translations
 *
 * The SMS gateway receives service requests sent as SMS messages and uses
 * a web server to actually perform the requests. The first word of the
 * SMS message usually specifies the service, and for each service there is
 * a URL that specifies the web page or cgi-bin that performs the service. 
 * Thus, in effect, the gateway `translates' SMS messages to URLs.
 *
 * urltrans.h and urltrans.c implement a data structure for holding a list
 * of translations and formatting a SMS request into a URL. It is used as
 * follows:
 *
 * 1. Create a URLTranslation object with urltrans_create.
 * 2. Add translations into it with urltrans_add_one or urltrans_add_cfg.
 * 3. Receive SMS messages, and translate them into URLs with 
 *    urltrans_get_url.
 * 4. When you are done, free the object with urltrans_destroy.
 *
 * See below for more detailed instructions for using the functions.
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef URLTRANS_H
#define URLTRANS_H

#include "gwlib/gwlib.h"
#include "msg.h"
#include "numhash.h"
#include "gwlib/regex.h"

/*
 * This is the data structure that holds the list of translations. It is
 * opaque and is defined in and usable only within urltrans.c.
 */
typedef struct URLTranslationList URLTranslationList;


/*
 * This is the data structure that holds one translation. It is also
 * opaque, and is accessed via some of the functions below.
 */
typedef struct URLTranslation URLTranslation;

enum {
    TRANSTYPE_GET_URL = 0,
    TRANSTYPE_POST_URL,
    TRANSTYPE_POST_XML,
    TRANSTYPE_TEXT,
    TRANSTYPE_FILE,
    TRANSTYPE_EXECUTE,
    TRANSTYPE_SENDSMS
};


/*
 * Create a new URLTranslationList object. Return NULL if the creation failed,
 * or a pointer to the object if it succeded.
 *
 * The object is empty: it contains no translations.
 */
URLTranslationList *urltrans_create(void);


/*
 * Destroy a URLTranslationList object.
 */
void urltrans_destroy(URLTranslationList *list);


/*
 * Add a translation to the object. The group is parsed internally.
 *
 * There can be several patterns for the same keyword, but with different
 * patterns. urltrans_get_url will pick the pattern that best matches the
 * actual SMS message. (See urltrans_get_pattern for a description of the
 * algorithm.)
 *
 * There can only be one pattern with keyword "default", however.
 *
 * Sendsms-translations do not use keyword. Instead they use username and
 * password
 *
 * Return -1 for error, or 0 for OK.
 */
int urltrans_add_one(URLTranslationList *trans, CfgGroup *grp);


/*
 * Add translations to a URLTranslation object from a Config object
 * (see config.h). Translations are added from groups in `cfg' that
 * contain variables called "keyword" and "url". For each such group,
 * urltrans_add_one is called.
 *
 * Return -1 for error, 0 for OK. If -1 is returned, the URLTranslation
 * object may have been partially modified.
 */
int urltrans_add_cfg(URLTranslationList *trans, Cfg *cfg);


/*
 * Find the translation that corresponds to a given text string
 *
 * Use the translation with pattern whose keyword is the same as the first
 * word of the text and that has the number of `%s' fields as the text
 * has words after the first one. If no such pattern exists, use the
 * pattern whose keyword is "default". If there is no such pattern, either,
 * return NULL.
 *
 * If 'smsc' is set, only accept translation with no 'accepted-smsc' set or
 * with matching smsc in that list.
 *
 * If 'account' is set, only accept translation with no 'accepted-account' set or
 * with matching account in that list.
 */
URLTranslation *urltrans_find(URLTranslationList *trans, Msg *msg);

/*
 * Find the translation that corresponds to a given name
 *
 * Use the translation with service whose name is the same as the first
 * word of the text. If no such pattern exists, return NULL.
 */
URLTranslation *urltrans_find_service(URLTranslationList *trans, Msg *msg); 


/*
 * find matching URLTranslation for the given 'username', or NULL
 * if not found. Password must be checked afterwards
 */
URLTranslation *urltrans_find_username(URLTranslationList *trans, 
    	    	    	    	       Octstr *name);


/* 
 * Return the populated URL octstr from the given pattern containing
 * the escape codes with values from the Msg.
 * urtrans_get_pattern() uses this internally, but we want to provide
 * this function also to the external calling space for use of the
 * defined escape codes for Msg values.
 */
Octstr *urltrans_fill_escape_codes(Octstr *pattern, Msg *request);


/*
 * Return a pattern given contents of an SMS message. Find the appropriate
 * translation pattern and fill in the missing parts from the contents of
 * the SMS message.
 *
 * `sms' is the SMS message that is being translated.
 *
 * Return NULL if there is a failure. Otherwise, return a pointer to the
 * pattern, which is stored in dynamically allocated memory that the
 * caller should free when the pattern is no longer needed.
 *
 * The pattern is URL, fixed text or file name according to type of urltrans
 */
Octstr *urltrans_get_pattern(URLTranslation *t, Msg *sms);


/*
 * Return the type of the translation, see enumeration above
 */
int urltrans_type(URLTranslation *t);


/*
 * Return prefix and suffix of translations, if they have been set.
 */
Octstr *urltrans_prefix(URLTranslation *t);
Octstr *urltrans_suffix(URLTranslation *t);


/*
 * Return default sender number, or NULL if not set.
 */
Octstr *urltrans_default_sender(URLTranslation *t);


/*
 * Return (a recommended) faked sender number, or NULL if not set.
 */
Octstr *urltrans_faked_sender(URLTranslation *t);


/*
 * Return maximum number of SMS messages that should be generated from
 * the web page directed by the URL translation.
 */
int urltrans_max_messages(URLTranslation *t);


/*
 * Return the concatenation status for SMS messages that should be generated
 * from the web page directed by the URL translation. (1=enabled)
 */
int urltrans_concatenation(URLTranslation *t);


/*
 * Return (recommended) delimiter characters when splitting long
 * replies into several messages
 */
Octstr *urltrans_split_chars(URLTranslation *t);


/*
 * return a string that should be added after each sms message if it is
 * except for the last one.
 */
Octstr *urltrans_split_suffix(URLTranslation *t);


/*
 * Return if set that should not send 'empty reply' messages
 */
int urltrans_omit_empty(URLTranslation *t);


/*
 * return a string that should be inserted to each SMS, if any
 */
Octstr *urltrans_header(URLTranslation *t);


/*
 * return a string that should be appended to each SMS, if any
 */
Octstr *urltrans_footer(URLTranslation *t);


/*
 * return the name, username or password string, or NULL if not set
 * (used only with TRANSTYPE_SENDSMS)
 */
Octstr *urltrans_name(URLTranslation *t);
Octstr *urltrans_username(URLTranslation *t);
Octstr *urltrans_password(URLTranslation *t);


/* Return forced smsc ID for send-sms user, if set */
Octstr *urltrans_forced_smsc(URLTranslation *t);


/* Return default smsc ID for send-sms user, if set */
Octstr *urltrans_default_smsc(URLTranslation *t);


/* Return allow and deny IP strings, if set. */
Octstr *urltrans_allow_ip(URLTranslation *t);
Octstr *urltrans_deny_ip(URLTranslation *t);

/* Return allowed and denied prefixes */
Octstr *urltrans_allowed_prefix(URLTranslation *t);
Octstr *urltrans_denied_prefix(URLTranslation *t);
Octstr *urltrans_allowed_recv_prefix(URLTranslation *t);
Octstr *urltrans_denied_recv_prefix(URLTranslation *t);

/* Return white and black to number list */
Numhash *urltrans_white_list(URLTranslation *t);
Numhash *urltrans_black_list(URLTranslation *t);
regex_t *urltrans_white_list_regex(URLTranslation *t);
regex_t *urltrans_black_list_regex(URLTranslation *t);

/* Return value of true (!0) or false (0) variables */
int urltrans_assume_plain_text(URLTranslation *t);
int urltrans_accept_x_kannel_headers(URLTranslation *t);

int urltrans_strip_keyword(URLTranslation *t);
int urltrans_send_sender(URLTranslation *t);

#endif
