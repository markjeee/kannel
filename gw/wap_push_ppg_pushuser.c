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
 * wap_push_ppg_pushuser.c: Implementation of wap_push_ppg_pushuser.h header.
 *
 * By Aarno Syvänen for Wiral Ltd and Global Networks Inc.
 */

#include "wap_push_ppg_pushuser.h"
#include "numhash.h"
#include "gwlib/regex.h"

/***************************************************************************
 *
 * Global data structures
 *
 * Hold user specific  data for one ppg user
 */

struct WAPPushUser {
    Octstr *name;                      /* the name of the user */
    Octstr *username;                  /* the username of this ppg user */
    Octstr *password;                  /* and password */
    Octstr *country_prefix;
    Octstr *allowed_prefix;            /* phone number prefixes allowed by 
                                          this user when pushing*/
    regex_t *allowed_prefix_regex;
    
    Octstr *denied_prefix;             /* and denied ones */
    regex_t *denied_prefix_regex;

    Numhash *white_list;               /* phone numbers of this user, used for 
                                          push*/
    regex_t *white_list_regex;
    Numhash *black_list;               /* numbers should not be used for push*/
    regex_t *black_list_regex;

    Octstr *user_deny_ip;              /* this user allows pushes from these 
                                          IPs*/
    Octstr *user_allow_ip;             /* and denies them from these*/
    Octstr *smsc_id;                   /* force push SMs to this smsc */
    Octstr *default_smsc_id;           /* use this smsc as a default for push SMs */
    Octstr *dlr_url;                   /* default dlr url from this user */
    Octstr *smsbox_id;                 /* use this smsbox for sending dlrs back*/
};

typedef struct WAPPushUser WAPPushUser;

/*
 * Hold user specific  data of all ppg users
 */

struct WAPPushUserList {
    List *list;
    Dict *names;
}; 

typedef struct WAPPushUserList WAPPushUserList; 

static WAPPushUserList *users = NULL;

/*
 * This hash table stores time when a specific ip is allowed to try next time.
 */
static Dict *next_try = NULL;

/***********************************************************************************
 *
 * Prototypes of internal functions
 */

static void destroy_users_list(void *l);
static WAPPushUserList *pushusers_create(long number_of_users);
static WAPPushUser *create_oneuser(CfgGroup *grp);
static void destroy_oneuser(void *p);
static int oneuser_add(CfgGroup *cfg);
static void oneuser_dump(WAPPushUser *u);
static WAPPushUser *user_find_by_username(Octstr *username);
static int password_matches(WAPPushUser *u, Octstr *password);
static int ip_allowed_by_user(WAPPushUser *u, Octstr *ip);
static int prefix_allowed(WAPPushUser *u, Octstr *number);
static int whitelisted(WAPPushUser *u, Octstr *number);
static int blacklisted(WAPPushUser *u, Octstr *number);
static int wildcarded_ip_found(Octstr *ip, Octstr *needle, Octstr *ip_sep);
static int response(List *push_headers, Octstr **username, Octstr **password);
static void challenge(HTTPClient *c, List *push_headers);
static void reply(HTTPClient *c, List *push_headers);
static int parse_cgivars_for_username(List *cgivars, Octstr **username);
static int parse_cgivars_for_password(List *cgivars, Octstr **password);
static int compare_octstr_sequence(Octstr *os1, Octstr *os2, long start);
static Octstr *forced_smsc(WAPPushUser *u);
static Octstr *default_smsc(WAPPushUser *u);

/****************************************************************************
 *
 * Implementation of external functions
 */

/*
 * Initialize the whole module and fill the push users list.
 */
int wap_push_ppg_pushuser_list_add(List *list, long number_of_pushes, 
                                   long number_of_users)
{
    CfgGroup *grp;

    next_try = dict_create(number_of_pushes, octstr_destroy_item);
    users = pushusers_create(number_of_users);
    gw_assert(list);
    while (list && (grp = gwlist_extract_first(list))) {
        if (oneuser_add(grp) == -1) {
	        gwlist_destroy(list, NULL);
            return 0;
        }
    }
    gwlist_destroy(list, NULL);

    return 1;
}

void wap_push_ppg_pushuser_list_destroy(void)
{
    dict_destroy(next_try);
    if (users == NULL)
        return;

    gwlist_destroy(users->list, destroy_oneuser);
    dict_destroy(users->names);
    gw_free(users);
}

enum {
    NO_USERNAME = -1,
    NO_PASSWORD = 0,
    HEADER_AUTHENTICATION = 1
};

#define ADDITION  0.1

/*
 * This function does authentication possible before compiling the control 
 * document. This means:
 *           a) password authentication by url or by headers (it is, by basic
 *              authentication response, see rfc 2617, chapter 2) 
 *           b) if this does not work, basic authentication by challenge - 
 *              response 
 *           c) enforcing various ip lists
 *
 * Check does ppg allows a connection from this at all, then try to find username 
 * and password from headers, then from url. If both fails, try basic authentica-
 * tion. Then check does this user allow a push from this ip, then check the pass-
 * word.
 *
 * For protection against brute force and partial protection for denial of serv-
 * ice attacks, an exponential backup algorithm is used. Time when a specific ip  
 * is allowed to reconnect, is stored in Dict next_try. If an ip tries to recon-
 * nect before this (three attemps are allowed, then exponential seconds are add-
 * ed to the limit) we make a new challenge. We do the corresponding check before
 * testing passwords; after all, it is an authorization failure that causes a new
 * challenge. 
 *
 * Rfc 2617, chapter 1 states that if we do not accept credentials of an user's, 
 * we must send a new challenge to the user.
 *
 * Output an authenticated username.
 * This function should be called only when there are a push users list; the 
 * caller is responsible for this.
 */
int wap_push_ppg_pushuser_authenticate(HTTPClient *c, List *cgivars, Octstr *ip, 
                                       List *push_headers, Octstr **username) {
        time_t now;
        static long next = 0L;            /* used only in this thread (and this 
                                             function) */
        long next_time;
        Octstr *next_time_os;
        static long multiplier = 1L;      /* ditto */
        WAPPushUser *u;
        Octstr *copy,
               *password;
        int ret;
        
        copy = octstr_duplicate(ip);
        time(&now);
        next_time_os = NULL;

        if ((ret = response(push_headers, username, &password)) == NO_USERNAME) { 
            if (!parse_cgivars_for_username(cgivars, username)) {
                error(0, "no user specified, challenging regardless");
	            goto listed;
            }
        }

        if (password == NULL)
            parse_cgivars_for_password(cgivars, &password);

        u = user_find_by_username(*username);
        if (!ip_allowed_by_user(u, ip)) {
	        goto not_listed;
        }

        next = 0;       

        if ((next_time_os = dict_get(next_try, ip)) != NULL) {
	        octstr_parse_long(&next_time, next_time_os, 0, 10);
            if (difftime(now, (time_t) next_time) < 0) {
	            error(0, "another try from %s, not much time used", 
                      octstr_get_cstr(copy));
	            goto listed;
            }
        }

        if (u == NULL) {
	        error(0, "user %s is not allowed by users list, challenging",
                  octstr_get_cstr(*username));
	        goto listed;
        }

        if (!password_matches(u, password)) {
	        error(0, "wrong or missing password in request from %s, challenging" , 
                  octstr_get_cstr(copy));
            goto listed;
        }

        dict_remove(next_try, ip);       /* no restrictions after authentica-
                                            tion */
        octstr_destroy(password);
        octstr_destroy(copy);
        octstr_destroy(next_time_os);
        return 1;

not_listed:
        octstr_destroy(password);
        octstr_destroy(copy); 
        reply(c, push_headers);
        octstr_destroy(next_time_os);
        return 0;

listed:
        challenge(c, push_headers);

        multiplier <<= 1;
        next = next + multiplier * ADDITION;
        next += now;
        next_time_os = octstr_format("%ld", next);
        dict_put(next_try, ip, next_time_os);
        
        octstr_destroy(copy);
        octstr_destroy(password);
        
        return 0;
}

/*
 * This function checks phone number for allowed prefixes, black lists and white
 * lists. Note that the phone number necessarily follows the international format 
 * (a requirement by our pap compiler).
 */
int wap_push_ppg_pushuser_client_phone_number_acceptable(Octstr *username, 
        Octstr *number)
{
    WAPPushUser *u;

    u = user_find_by_username(username);
    if (!prefix_allowed(u, number)) {
        error(0, "Number %s not allowed by user %s (wrong prefix)", 
              octstr_get_cstr(number), octstr_get_cstr(username));
        return 0;
    }

    if (blacklisted(u, number)) {
        error(0, "Number %s not allowed by user %s (blacklisted)", 
              octstr_get_cstr(number), octstr_get_cstr(username) );
        return 0;
    }

    if (!whitelisted(u, number)) {
        error(0, "Number %s not allowed by user %s (not whitelisted)", 
              octstr_get_cstr(number), octstr_get_cstr(username) );
        return 0;
    }

    return 1;
}

int wap_push_ppg_pushuser_search_ip_from_wildcarded_list(Octstr *haystack, 
        Octstr *needle, Octstr *gwlist_sep, Octstr *ip_sep)
{
    List *ips;
    long i;
    Octstr *configured_ip;

    gw_assert(haystack);
    gw_assert(gwlist_sep);
    gw_assert(ip_sep);

    /*There are no wildcards in the list*/    
    if (octstr_search_char(haystack, '*', 0) < 0) {
        if (octstr_search(haystack, needle, 0) >= 0) {
	        return 1;
        } else { 
	        return 0;
        }
    }
    
    /*There are wildcards in the list*/
    configured_ip = NULL;
    ips = octstr_split(haystack, gwlist_sep);
    for (i = 0; i < gwlist_len(ips); ++i) {
        configured_ip = gwlist_get(ips, i);
        if (wildcarded_ip_found(configured_ip, needle, ip_sep))
	        goto found;
    }

    gwlist_destroy(ips, octstr_destroy_item);
    return 0;

found:
    gwlist_destroy(ips, octstr_destroy_item);
    return 1;
}

/*
 * Returns smsc-id that pushes by this user must use, 
 * NULL when there was an error.
 */
Octstr *wap_push_ppg_pushuser_smsc_id_get(Octstr *username)
{
    WAPPushUser *u;
    Octstr *smsc_id;

    if ((u = user_find_by_username(username)) == NULL) {
        /* no user found with this username */
        return NULL;
    }

    if ((smsc_id = forced_smsc(u)) != NULL)
        return octstr_duplicate(smsc_id);

    smsc_id = default_smsc(u);
    return octstr_duplicate(smsc_id);
}

/*
 * Returns default dlr url for this user.
 */
Octstr *wap_push_ppg_pushuser_dlr_url_get(Octstr *username)
{
    WAPPushUser *u;
    Octstr *dlr_url;

    u = user_find_by_username(username);
    dlr_url = u->dlr_url;     

    return octstr_duplicate(dlr_url);
}

/*
 * Returns default dlr smsbox id for this user.
 */
Octstr *wap_push_ppg_pushuser_smsbox_id_get(Octstr *username)
{
    WAPPushUser *u;
    Octstr *smsbox_id;

    u = user_find_by_username(username);
    smsbox_id = u->smsbox_id;

    return octstr_duplicate(smsbox_id);
}


/***************************************************************************
 *
 * Implementation of internal functions
 */

static void destroy_users_list(void *l)
{
    gwlist_destroy(l, NULL);
}

static WAPPushUserList *pushusers_create(long number_of_users) 
{
    users = gw_malloc(sizeof(WAPPushUserList));
    users->list = gwlist_create();
    users->names = dict_create(number_of_users, destroy_users_list);

    return users;
}

/*
 * Allocate memory for one push user and read configuration data to it. We initial-
 * ize all fields to NULL, because the value NULL means that the configuration did
 * not have this variable. 
 * Return NULL when failure, a pointer to the data structure otherwise.
 */
static WAPPushUser *create_oneuser(CfgGroup *grp)
{
    WAPPushUser *u;
    Octstr *grpname,
           *os;

    grpname = cfg_get(grp, octstr_imm("wap-push-user"));
    if (grpname == NULL) {
        error(0, "all users group (wap-push-user) are missing");
        goto no_grpname;
    }
   
    u = gw_malloc(sizeof(WAPPushUser));
    u->name = NULL;
    u->username = NULL;                  
    u->allowed_prefix = NULL;           
    u->allowed_prefix_regex = NULL;           
    u->denied_prefix = NULL;             
    u->denied_prefix_regex = NULL;             
    u->white_list = NULL;               
    u->white_list_regex = NULL;               
    u->black_list = NULL;              
    u->black_list_regex = NULL;              
    u->user_deny_ip = NULL;              
    u->user_allow_ip = NULL;
    u->smsc_id = NULL;
    u->default_smsc_id = NULL;

    u->name = cfg_get(grp, octstr_imm("wap-push-user"));

    if (u->name == NULL) {
        warning(0, "user name missing, dump follows");
        oneuser_dump(u);
        goto error;
    }

    u->username = cfg_get(grp, octstr_imm("ppg-username"));
    u->password = cfg_get(grp, octstr_imm("ppg-password"));

    if (u->username == NULL) {
        warning(0, "login name for user %s missing, dump follows", 
              octstr_get_cstr(u->name));
        oneuser_dump(u);
        goto error;
    }

    if (u->password == NULL) {
        warning(0, "password for user %s missing, dump follows", 
              octstr_get_cstr(u->name));
        oneuser_dump(u);
        goto error;
    }

    u->user_deny_ip = cfg_get(grp, octstr_imm("deny-ip"));
    u->user_allow_ip = cfg_get(grp, octstr_imm("allow-ip"));
    u->country_prefix = cfg_get(grp, octstr_imm("country-prefix"));
    u->allowed_prefix = cfg_get(grp, octstr_imm("allowed-prefix"));
    u->denied_prefix = cfg_get(grp, octstr_imm("denied-prefix"));
    u->smsc_id = cfg_get(grp, octstr_imm("forced-smsc"));
    u->default_smsc_id = cfg_get(grp, octstr_imm("default-smsc"));
    u->dlr_url = cfg_get(grp, octstr_imm("dlr-url"));
    u->smsbox_id = cfg_get(grp, octstr_imm("smsbox-id"));

    os = cfg_get(grp, octstr_imm("white-list"));
    if (os != NULL) {
	    u->white_list = numhash_create(octstr_get_cstr(os));
	    octstr_destroy(os);
    }
    os = cfg_get(grp, octstr_imm("black-list"));
    if (os != NULL) {
	    u->black_list = numhash_create(octstr_get_cstr(os));
	    octstr_destroy(os);
    }

    if ((os = cfg_get(grp, octstr_imm("allowed-prefix-regex"))) != NULL) {
        if ((u->allowed_prefix_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
    };
    if ((os = cfg_get(grp, octstr_imm("denied-prefix-regex"))) != NULL) {
        if ((u->denied_prefix_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
    };
    if ((os = cfg_get(grp, octstr_imm("white-list-regex"))) != NULL) {
        if ((u->white_list_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
    };
    if ((os = cfg_get(grp, octstr_imm("black-list-regex"))) != NULL) {
        if ((u->black_list_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
    };

    octstr_destroy(grpname);
    return u;

no_grpname:
    octstr_destroy(grpname);
    return NULL;

error:
    octstr_destroy(grpname);
    destroy_oneuser(u);
    return NULL;
}

static void destroy_oneuser(void *p) 
{
     WAPPushUser *u;

     u = p;
     if (u == NULL)
         return;

     octstr_destroy(u->name);
     octstr_destroy(u->username);  
     octstr_destroy(u->password);  
     octstr_destroy(u->country_prefix);              
     octstr_destroy(u->allowed_prefix);           
     octstr_destroy(u->denied_prefix);             
     numhash_destroy(u->white_list);               
     numhash_destroy(u->black_list);              
     octstr_destroy(u->user_deny_ip);              
     octstr_destroy(u->user_allow_ip);
     octstr_destroy(u->smsc_id);
     octstr_destroy(u->default_smsc_id);

     if (u->black_list_regex != NULL) gw_regex_destroy(u->black_list_regex);
     if (u->white_list_regex != NULL) gw_regex_destroy(u->white_list_regex);
     if (u->denied_prefix_regex != NULL) gw_regex_destroy(u->denied_prefix_regex);
     if (u->allowed_prefix_regex != NULL) gw_regex_destroy(u->allowed_prefix_regex);
     gw_free(u);             
}

static void oneuser_dump(WAPPushUser *u)
{
    if (u == NULL) {
        debug("wap.push.ppg.pushuser", 0, "no user found");
        return;
    }

    debug("wap.push.ppg.pushuser", 0, "Dumping user data: Name of the user:");
    octstr_dump(u->name, 0);
    debug("wap.push.ppg.pushuser", 0, "username:");
    octstr_dump(u->username, 0);  
    debug("wap.push.ppg.pushuser", 0, "omitting password");
    debug("wap-push.ppg.pushuser", 0, "country prefix");
    octstr_dump(u->country_prefix, 0);   
    debug("wap.push.ppg.pushuser", 0, "allowed prefix list:");            
    octstr_dump(u->allowed_prefix, 0);  
    debug("wap.push.ppg.pushuser", 0, "denied prefix list:");         
    octstr_dump(u->denied_prefix, 0);   
    debug("wap.push.ppg.pushuser", 0, "denied ip list:");                         
    octstr_dump(u->user_deny_ip, 0);    
    debug("wap.push.ppg.pushuser", 0, "allowed ip list:");                   
    octstr_dump(u->user_allow_ip, 0);
    debug("wap.push.ppg.pushuser", 0, "send via smsc-id:");                   
    octstr_dump(u->smsc_id, 0);
    debug("wap.push.ppg.pushuser", 0, "use default smsc:");
    octstr_dump(u->default_smsc_id, 0);
    debug("wap.push.ppg.pushuser", 0, "end of the dump");
}

/*
 * Add an user to the push users list
 */
static int oneuser_add(CfgGroup *grp)
{
    WAPPushUser *u;
    List *list;

    u = create_oneuser(grp);
    if (u == NULL)
        return -1;

    gwlist_append(users->list, u);

    list = dict_get(users->names, u->username);
    if (list == NULL) {
        list = gwlist_create();
        dict_put(users->names, u->username, list);
    }

    return 0;
}

static WAPPushUser *user_find_by_username(Octstr *username)
{
    WAPPushUser *u;
    long i;
    List *list;

    if (username == NULL)
        return NULL;

    if ((list = dict_get(users->names, username)) == NULL)
         return NULL;

    for (i = 0; i < gwlist_len(users->list); ++i) {
         u = gwlist_get(users->list, i);
         if (octstr_compare(u->username, username) == 0)
	         return u;
    }

    return NULL;
}

static int password_matches(WAPPushUser *u, Octstr *password)
{
    if (password == NULL)
        return 0;    

    return octstr_compare(u->password, password) == 0;
}

static int wildcarded_ip_found(Octstr *ip, Octstr *needle, Octstr *ip_sep)
{
    List *ip_fragments,
         *needle_fragments;
    long i;
    Octstr *ip_fragment,
           *needle_fragment;

    ip_fragments = octstr_split(ip, ip_sep);
    needle_fragments = octstr_split(needle, ip_sep);

    gw_assert(gwlist_len(ip_fragments) == gwlist_len(needle_fragments));
    for (i = 0; i < gwlist_len(ip_fragments); ++i) {
        ip_fragment = gwlist_get(ip_fragments, i);
        needle_fragment = gwlist_get(needle_fragments, i);
        if (octstr_compare(ip_fragment, needle_fragment) != 0 && 
                octstr_compare(ip_fragment, octstr_imm("*")) != 0)
 	        goto not_found;
    }

    gwlist_destroy(ip_fragments, octstr_destroy_item);
    gwlist_destroy(needle_fragments, octstr_destroy_item);   
    return 1;

not_found:
    gwlist_destroy(ip_fragments, octstr_destroy_item);
    gwlist_destroy(needle_fragments, octstr_destroy_item);
    return 0;
}

/*
 * Deny_ip = '*.*.*.*' is here taken literally: no ips allowed by this user 
 * (definitely strange, but not a fatal error). 
 */
static int ip_allowed_by_user(WAPPushUser *u, Octstr *ip)
{
    Octstr *copy,
           *ip_copy;
    
    if (u == NULL) {
        warning(0, "user not found from the users list");
        goto no_user;
    }

    copy = octstr_duplicate(u->username);

    if (u->user_deny_ip == NULL && u->user_allow_ip == NULL)
        goto allowed;

    if (u->user_deny_ip) {
        if (octstr_compare(u->user_deny_ip, octstr_imm("*.*.*.*")) == 0) {
            warning(0, "no ips allowed for %s", octstr_get_cstr(copy));
            goto denied;
        }
    }

    if (u->user_allow_ip)
        if (octstr_compare(u->user_allow_ip, octstr_imm("*.*.*.*")) == 0)
            goto allowed;

    if (u->user_deny_ip) {
        if (wap_push_ppg_pushuser_search_ip_from_wildcarded_list(u->user_deny_ip, 
	            ip, octstr_imm(";"), octstr_imm("."))) {
            goto denied;
        }
    }

    if (u->user_allow_ip) {
        if (wap_push_ppg_pushuser_search_ip_from_wildcarded_list(u->user_allow_ip, 
	            ip, octstr_imm(";"), octstr_imm("."))) {
            goto allowed;
        }
    }

    octstr_destroy(copy);
    warning(0, "ip not found from either ip list, deny it");
    return 0;

allowed:
    octstr_destroy(copy);
    return 1;

denied:
    ip_copy = octstr_duplicate(ip);
    warning(0, "%s denied by user %s", octstr_get_cstr(ip_copy), 
            octstr_get_cstr(copy));
    octstr_destroy(copy);
    octstr_destroy(ip_copy);
    return 0;

no_user:
    return 0;
}

/*
 * HTTP basic authentication server response is defined in rfc 2617, chapter 2.
 * Return 1, when we found username and password from headers, 0, when there were 
 * no password and -1 when there were no username (or no Authorization header at 
 * all, or an unparsable one). Username and password value 'NULL' means no user-
 * name or password supplied.
 */
static int response(List *push_headers, Octstr **username, Octstr **password)
{
    Octstr *header_value,
           *basic;
    size_t basic_len;
    List *auth_list;

    *username = NULL;
    *password = NULL;

    if ((header_value = http_header_find_first(push_headers, 
            "Authorization")) == NULL)
        goto no_response3; 

    octstr_strip_blanks(header_value);
    basic = octstr_imm("Basic");
    basic_len = octstr_len(basic);

    if (octstr_ncompare(header_value, basic, basic_len) != 0)
        goto no_response1;

    octstr_delete(header_value, 0, basic_len);
    octstr_strip_blanks(header_value);
    octstr_base64_to_binary(header_value);
    auth_list = octstr_split(header_value, octstr_imm(":"));

    if (gwlist_len(auth_list) != 2)
        goto no_response2;
    
    *username = octstr_duplicate(gwlist_get(auth_list, 0));
    *password = octstr_duplicate(gwlist_get(auth_list, 1));

    if (username == NULL) {
        goto no_response2;
    }

    if (password == NULL) {
        goto no_response4;
    }

    debug("wap.push.ppg.pushuser", 0, "we have an username and a password in" 
          " authorization header");
    gwlist_destroy(auth_list, octstr_destroy_item);
    octstr_destroy(header_value);
    http_header_remove_all(push_headers, "Authorization");
    return HEADER_AUTHENTICATION;

no_response1:
    octstr_destroy(header_value);
    return NO_USERNAME;

no_response2:   
    gwlist_destroy(auth_list, octstr_destroy_item);
    octstr_destroy(header_value);
    return NO_USERNAME;

no_response3:
    return NO_USERNAME;

no_response4:   
    gwlist_destroy(auth_list, octstr_destroy_item);
    octstr_destroy(header_value);
    return NO_PASSWORD;
}

/*
 * HTTP basic authentication server challenge is defined in rfc 2617, chapter 2. 
 * Only WWW-Authenticate header is required here by specs. This function does not
 * release memory used by push headers, the caller must do this.
 */
static void challenge(HTTPClient *c, List *push_headers)
{
    Octstr *challenge,
           *realm;
    int http_status;
    List *reply_headers;

    realm = octstr_format("%s", "Basic realm=");
    octstr_append(realm, get_official_name());
    octstr_format_append(realm, "%s", "\"wappush\"");
    reply_headers = http_create_empty_headers();
    http_header_add(reply_headers, "WWW-Authenticate", octstr_get_cstr(realm));
    http_status = HTTP_UNAUTHORIZED;
    challenge = octstr_imm("You must show your credentials.\n");
    
    http_send_reply(c, http_status, reply_headers, challenge);

    octstr_destroy(realm);
    http_destroy_headers(reply_headers);
}

/*
 * This function does not release memory used by push headers, the caller must do this.
 */
static void reply(HTTPClient *c, List *push_headers)
{
    int http_status;
    Octstr *denied;
    List *reply_headers;

    reply_headers = http_create_empty_headers();
    http_status = HTTP_FORBIDDEN;
    denied = octstr_imm("You are not allowed to use this service. Do not retry.\n");
 
    http_send_reply(c, http_status, push_headers, denied);

    http_destroy_headers(reply_headers);
}

/*
 * Note that the phone number necessarily follows the international format (a requi-
 * rement by our pap compiler). So we add country prefix to listed prefixes, if one
 * is configured.
 */
static int prefix_allowed(WAPPushUser *u, Octstr *number)
{
    List *allowed,
         *denied;
    long i;
    Octstr *listed_prefix;

    allowed = NULL;
    denied = NULL;

    if (u == NULL)
        goto no_user;

    if (        u->allowed_prefix == NULL && u->denied_prefix == NULL 
        && u->allowed_prefix_regex == NULL && u->denied_prefix_regex == NULL)
        goto no_configuration;

    if (u->denied_prefix != NULL) {
        denied = octstr_split(u->denied_prefix, octstr_imm(";"));
        for (i = 0; i < gwlist_len(denied); ++i) {
             listed_prefix = gwlist_get(denied, i);
             if (u->country_prefix != NULL)
                 octstr_insert(listed_prefix, u->country_prefix, 0);
             if (compare_octstr_sequence(number, listed_prefix, 
                     0) == 0) {
      	         goto denied;
             }
        }
    }

    /* note: country-prefix _must_be included in the pattern */
    if (u->denied_prefix_regex != NULL) 
        if (gw_regex_match_pre(u->denied_prefix_regex, number) == 1)
            goto denied;

    if (u->allowed_prefix_regex == NULL && u->allowed_prefix == NULL) 
        goto no_allowed_config;

    if (u->allowed_prefix != NULL) {
    allowed = octstr_split(u->allowed_prefix, octstr_imm(";"));
    for (i = 0; i < gwlist_len(allowed); ++i) {
         listed_prefix = gwlist_get(allowed, i);
         if (u->country_prefix != NULL)
             octstr_insert(listed_prefix, u->country_prefix, 0);
         if (compare_octstr_sequence(number, listed_prefix, 
                 0) == 0) {
	         goto allowed;
         }
    }
    }

    /* note: country-prefix _must_ be included in the pattern */
    if (u->allowed_prefix_regex != NULL) 
        if (gw_regex_match_pre(u->allowed_prefix_regex, number) == 1)
            goto allowed;

/*
 * Here we have an intentional fall-through. It will removed when memory cleaning
 * functions are implemented.
 */
denied:         
    gwlist_destroy(allowed, octstr_destroy_item);
    gwlist_destroy(denied, octstr_destroy_item);
    return 0;

allowed:      
    gwlist_destroy(allowed, octstr_destroy_item);
    gwlist_destroy(denied, octstr_destroy_item);
    return 1;

no_configuration:
    return 1;

no_user:
    return 0;

no_allowed_config:
    gwlist_destroy(denied, octstr_destroy_item);
    return 1;
}

static int whitelisted(WAPPushUser *u, Octstr *number)
{
    int result = 1;

    if (u->white_list != NULL)
        result = numhash_find_number(u->white_list, number);

    if ((result == 0) && (u->white_list_regex != NULL))
        result = gw_regex_match_pre(u->white_list_regex, number);

    return result;
}

static int blacklisted(WAPPushUser *u, Octstr *number)
{
    int result = 0;
    
    if (u->black_list != NULL)
        result = numhash_find_number(u->black_list, number);

    if ((result == 0) && (u->black_list_regex != NULL))
        result = gw_regex_match_pre(u->black_list_regex, number);

    return result;
}

/* 
 * 'NULL' means here 'no value found'.
 * Return 1 when we found username, 0 when we did not.
 */
static int parse_cgivars_for_username(List *cgivars, Octstr **username)
{
    *username = NULL;
    *username = octstr_duplicate(http_cgi_variable(cgivars, "username"));

    if (*username == NULL) {
        return 0;
    }

    return 1;
}

static int parse_cgivars_for_password(List *cgivars, Octstr **password)
{
    *password = NULL;
    *password = octstr_duplicate(http_cgi_variable(cgivars, "password"));

    if (*password == NULL) {
        return 0;
    }

    return 1;
}

/*
 * Compare an octet string os2 with a sequence of an octet string os1. The sequence
 * starts with a position start. 
 */
static int compare_octstr_sequence(Octstr *os1, Octstr *os2, long start)
{
    int ret;
    unsigned char *prefix;
    long end;

    if (octstr_len(os2) == 0)
        return 1;

    if (octstr_len(os1) == 0)
        return -1;

    prefix = NULL;
    if (start != 0) {
        prefix = gw_malloc(start);
        octstr_get_many_chars((char *)prefix, os1, 0, start);
        octstr_delete(os1, 0, start);
    }
    
    end = start + octstr_len(os2);
    ret = octstr_ncompare(os1, os2, end - start);
    
    if (start != 0) {
        octstr_insert_data(os1, 0, (char *)prefix, start);
        gw_free(prefix);
    }

    return ret;
}

static Octstr *forced_smsc(WAPPushUser *u)
{
    return u->smsc_id;
}

static Octstr *default_smsc(WAPPushUser *u)
{
    return u->default_smsc_id;
}












