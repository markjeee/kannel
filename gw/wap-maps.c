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
 * gw/wap-maps.c - URL mapping 
 * 
 * Bruno Rodrigues  <bruno.rodrigues@litux.org>
 */

#include "gwlib/gwlib.h"
#include "wap-maps.h"

struct url_map_struct {
    Octstr *name;
    Octstr *url;
    Octstr *map_url;
    Octstr *send_msisdn_query;
    Octstr *send_msisdn_header;
    Octstr *send_msisdn_format;
    int accept_cookies;
};

struct user_map_struct {
    Octstr *name;
    Octstr *user;
    Octstr *pass;
    Octstr *msisdn;
};


/*
 * XXX All mapping functions should be implemented with Dicts instead of
 * Lists! Linear scans in lists are pretty slow against hash table lookups,
 * espacially when you have *lot* of entries, which is the case in URL re-
 * writting in general.
 * TODO: identify a hash key that can be used and use that as lookup.
 */

/* mapping storrage */
static List *url_map = NULL;
static Dict *user_map = NULL;


/********************************************************************
 * Creation and destruction of mapping entries
 */

void wap_map_add_url(Octstr *name, Octstr *url, Octstr *map_url,
                     Octstr *send_msisdn_query,
                     Octstr *send_msisdn_header,
                     Octstr *send_msisdn_format,
                     int accept_cookies) {
    struct url_map_struct *entry;

    if (url_map == NULL) 
        url_map = gwlist_create();

    entry = gw_malloc(sizeof(*entry));
    entry->name = name;
    entry->url = url;
    entry->map_url = map_url;
    entry->send_msisdn_query = send_msisdn_query;
    entry->send_msisdn_header = send_msisdn_header;
    entry->send_msisdn_format = send_msisdn_format;
    entry->accept_cookies = accept_cookies;
    
    gwlist_append(url_map, entry);
}


static void wap_user_map_destroy(void *i) 
{
    struct user_map_struct *entry = i;

    octstr_destroy(entry->name);
    octstr_destroy(entry->user);
    octstr_destroy(entry->pass);
    octstr_destroy(entry->msisdn);
    gw_free(entry);
}


void wap_map_add_user(Octstr *name, Octstr *user, Octstr *pass,
                      Octstr *msisdn) {
    struct user_map_struct *entry;

    if (user_map == NULL) 
        user_map = dict_create(32, wap_user_map_destroy);

    entry = gw_malloc(sizeof(*entry));
    entry->name = name;
    entry->user = user;
    entry->pass = pass;
    entry->msisdn = msisdn;
    dict_put(user_map, entry->user, entry);
}


void wap_map_destroy(void) 
{
    long i;
    struct url_map_struct *entry;

    if (url_map != NULL) {
        for (i = 0; i < gwlist_len(url_map); i++) {
            entry = gwlist_get(url_map, i);
            octstr_destroy(entry->name);
            octstr_destroy(entry->url);
            octstr_destroy(entry->map_url);
            octstr_destroy(entry->send_msisdn_query);
            octstr_destroy(entry->send_msisdn_header);
            octstr_destroy(entry->send_msisdn_format);
            gw_free(entry);
        }
        gwlist_destroy(url_map, NULL);
    }
    url_map = NULL;
}


void wap_map_user_destroy(void)
{
    dict_destroy(user_map);
    user_map = NULL;
}


/********************************************************************
 * Public functions
 */

void wap_map_url_config(char *s)
{
    char *in, *out;
    
    s = gw_strdup(s);
    in = strtok(s, " \t");
    if (!in) 
        return;
    out = strtok(NULL, " \t");
    if (!out) 
        return;
    wap_map_add_url(octstr_imm("unknown"), octstr_create(in), 
                     octstr_create(out), NULL, NULL, NULL, 0);
    gw_free(s);
}

void wap_map_url_config_device_home(char *to)
{
    wap_map_add_url(octstr_imm("Device Home"), octstr_imm("DEVICE:home*"),
                     octstr_create(to), NULL, NULL, NULL, -1);
}


void wap_map_url(Octstr **osp, Octstr **send_msisdn_query, 
                 Octstr **send_msisdn_header, 
                 Octstr **send_msisdn_format, int *accept_cookies)
{
    long i;
    Octstr *newurl, *tmp1, *tmp2;

    newurl = tmp1 = tmp2 = NULL;
    *send_msisdn_query = *send_msisdn_header = *send_msisdn_format = NULL;
    *accept_cookies = -1;

    debug("wsp",0,"WSP: Mapping url <%s>", octstr_get_cstr(*osp));
    for (i = 0; url_map && i < gwlist_len(url_map); i++) {
        struct url_map_struct *entry;
        entry = gwlist_get(url_map, i);

        /* 
        debug("wsp",0,"WSP: matching <%s> with <%s>", 
	          octstr_get_cstr(entry->url), octstr_get_cstr(entry->map_url)); 
        */

        /* DAVI: I only have '*' terminated entry->url implementation for now */
        tmp1 = octstr_duplicate(entry->url);
        octstr_delete(tmp1, octstr_len(tmp1)-1, 1); /* remove last '*' */
        tmp2 = octstr_copy(*osp, 0, octstr_len(tmp1));

        debug("wsp",0,"WSP: Matching <%s> with <%s>", 
              octstr_get_cstr(tmp1), octstr_get_cstr(tmp2));

        if (octstr_case_compare(tmp2, tmp1) == 0) {
            /* rewrite url if configured to do so */
            if (entry->map_url != NULL) {
                if (octstr_get_char(entry->map_url, 
                                    octstr_len(entry->map_url)-1) == '*') {
                    newurl = octstr_duplicate(entry->map_url);
                    octstr_delete(newurl, octstr_len(newurl)-1, 1);
                    octstr_append(newurl, octstr_copy(*osp, 
                    octstr_len(entry->url)-1, 
                    octstr_len(*osp)-octstr_len(entry->url)+1));
                } else {
                    newurl = octstr_duplicate(entry->map_url);
                }
                debug("wsp",0,"WSP: URL Rewriten from <%s> to <%s>", 
                      octstr_get_cstr(*osp), octstr_get_cstr(newurl));
                octstr_destroy(*osp);
                *osp = newurl;
            }
            *accept_cookies = entry->accept_cookies;
            *send_msisdn_query = octstr_duplicate(entry->send_msisdn_query);
            *send_msisdn_header = octstr_duplicate(entry->send_msisdn_header);
            *send_msisdn_format = octstr_duplicate(entry->send_msisdn_format);
            octstr_destroy(tmp1);
            octstr_destroy(tmp2);
            break;
        }
        octstr_destroy(tmp1);
        octstr_destroy(tmp2);
    }
}

int wap_map_user(Octstr **msisdn, Octstr *user, Octstr *pass)
{
    struct user_map_struct *entry;

    entry = dict_get(user_map, user);
    if (entry != NULL && octstr_compare(pass, entry->pass) == 0) {
        *msisdn = octstr_duplicate(entry->msisdn);
        return 1;
    }
    return 0;
}

