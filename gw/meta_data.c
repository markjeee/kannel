/* ====================================================================
 * The Kannel Software License, Version 1.0
 *
 * Copyright (c) 2001-2010 Kannel Group
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
 * meta_data.h
 *
 * Meta Data manupilation.
 * 
 * Alexander Malysh <amalysh kannel.org>, 2007-2009
 */

#include "gwlib/gwlib.h"
#include "meta_data.h"


struct meta_data {
    Octstr *group;
    Dict *values;
    struct meta_data *next;
};


static struct meta_data *meta_data_create(void)
{
    struct meta_data *ret;

    ret = gw_malloc(sizeof(*ret));
    ret->group = NULL;
    ret->values = NULL;
    ret->next = NULL;

    return ret;
}


static void meta_data_destroy(struct meta_data *meta)
{
    struct meta_data *next;

    if (meta == NULL)
        return;

    do {
        next = meta->next;
        octstr_destroy(meta->group);
        dict_destroy(meta->values);
        gw_free(meta);
        meta = next;
    } while(meta != NULL);
}


/* format: ?group-name?key=value&key=value?group?... group, key, value are urlencoded */
static struct meta_data *meta_data_unpack(const Octstr *data)
{
    struct meta_data *ret = NULL, *curr = NULL;
    const char *str;
    long pos;
    Octstr *key = NULL;
    int type, next_type;
    long start, end;

    start = end = -1;
    type = next_type = -1;
    for (pos = 0, str = octstr_get_cstr(data); pos <= octstr_len(data); str++, pos++) {
        switch(*str) {
        case '?':
            if (start == -1) { /* start of str */
                start = pos;
                type = 0;
            } else if (type == 0) { /* end of group */
                end = pos;
                next_type = 1;
            } else if (type == 2 && key != NULL) { /* end of value */
                end = pos;
                next_type = 0;
            } else if (key == NULL) { /* start of next group without key and value */
                start = pos;
                type = 0;
            } else {
                /* FAILED */
                error(0, "MDATA: Found '?' but not expected it end=%ld start=%ld type=%d.", end, start, type);
                meta_data_destroy(ret);
                octstr_destroy(key);
                return NULL;
            }
            break;
        case '=':
            if (type == 1 && curr != NULL && key == NULL) { /* end of key */
                end = pos;
                next_type = 2;
            } else {
                /* FAILED */
                error(0, "MDATA: Found '=' but not expected it end=%ld start=%ld type=%d.", end, start, type);
                meta_data_destroy(ret);
                octstr_destroy(key);
                return NULL;
            }
            break;
        case '&':
            if (type == 2 && curr != NULL && key != NULL) { /* end of value */
                end = pos;
                next_type = 1;
            } else if (type == 1 && key == NULL) { /* just & skip it */
                start = pos;
            } else {
                /* FAILED */
                error(0, "MDATA: Found '&' but not expected it end=%ld start=%ld type=%d.", end, start, type);
                meta_data_destroy(ret);
                octstr_destroy(key);
                return NULL;
            }
            break;
        case '\0':
            if (type == 2) /* end of value */
                end = pos;
            break;
        }
        if (start >= 0 && end >= 0) {
            Octstr *tmp;

            if (end - start - 1 == 0)
                tmp = octstr_create("");
            else
                tmp = octstr_create_from_data(str - end + start + 1, end - start - 1);

            octstr_url_decode(tmp);

            switch(type) {
            case 0: /* group */
                if (curr == NULL) {
                    curr = gw_malloc(sizeof(*curr));
                } else {
                    curr->next = gw_malloc(sizeof(*curr));
                    curr = curr->next;
                }
                curr->group = tmp;
                tmp = NULL;
                curr->values = dict_create(1024, octstr_destroy_item);
                curr->next = NULL;
                if (ret == NULL)
                    ret = curr;
                debug("meta_data", 0, "new group created `%s'", octstr_get_cstr(curr->group));
                break;
            case 1: /* key */
                key = tmp;
                tmp = NULL;
                break;
            case 2: /* value */
                debug("meta_data", 0, "group=`%s' key=`%s' value=`%s'", octstr_get_cstr(curr->group),
                      octstr_get_cstr(key), octstr_get_cstr(tmp));
                dict_put(curr->values, key, tmp);
                tmp = NULL;
                octstr_destroy(key);
                key = NULL;
                break;
            }
            octstr_destroy(tmp);
            type = next_type;
            next_type = -1;
            start = end;
            end = -1;
        }
    }
    octstr_destroy(key);

    return ret;
}


static int meta_data_pack(struct meta_data *mdata, Octstr *data)
{
    List *l;
    Octstr *tmp;

    if (mdata == NULL || data == NULL)
        return -1;
    /* clear data */
    octstr_delete(data, 0, octstr_len(data));
    do {
        octstr_format_append(data, "?%E?", mdata->group);
        l = dict_keys(mdata->values);
        while(l != NULL && (tmp = gwlist_extract_first(l)) != NULL) {
            octstr_format_append(data, "%E=%E&", tmp, dict_get(mdata->values, tmp));
            octstr_destroy(tmp);
        }
        gwlist_destroy(l, octstr_destroy_item);
        mdata = mdata->next;
    } while(mdata != NULL);

    return 0;
}


Dict *meta_data_get_values(const Octstr *data, const char *group)
{
    struct meta_data *mdata, *curr;
    Dict *ret = NULL;

    if (data == NULL || group == NULL)
        return NULL;

    mdata = meta_data_unpack(data);
    if (mdata == NULL)
        return NULL;
    for (curr = mdata; curr != NULL; curr = curr->next) {
        if (octstr_str_case_compare(curr->group, group) == 0) {
            ret = curr->values;
            curr->values = NULL;
            break;
        }
    }

    meta_data_destroy(mdata);

    return ret;
}


int meta_data_set_values(Octstr *data, const Dict *dict, const char *group, int replace)
{
    struct meta_data *mdata, *curr;
    int i;
    List *keys;
    Octstr *key;

    if (data == NULL || group == NULL)
        return -1;

    mdata = meta_data_unpack(data);
    for (curr = mdata; curr != NULL; curr = curr->next) {
        if (octstr_str_case_compare(curr->group, group) == 0) {
            /*
             * If we don't replace the values, copy the old Dict values to the new Dict
             */
            if (replace == 0) {
                keys = dict_keys(curr->values);
                while((key = gwlist_extract_first(keys)) != NULL) {
                    dict_put_once((Dict*)dict, key, octstr_duplicate(dict_get(curr->values, key)));
                    octstr_destroy(key);
                }
                gwlist_destroy(keys, octstr_destroy_item);
            }
            dict_destroy(curr->values);
            curr->values = (Dict*)dict;
            break;
        }
    }

    if (curr == NULL) {
        curr = meta_data_create();
        curr->group = octstr_create(group);
        curr->values = (Dict*)dict;
        curr->next = NULL;
        if (mdata == NULL) {
            mdata = curr;
        } else {
            curr->next = mdata->next;
            mdata->next = curr;
        }
    }
    i = meta_data_pack(mdata, data);
    curr->values = NULL;

    meta_data_destroy(mdata);

    return i;
}


int meta_data_set_value(Octstr *data, const char *group, const Octstr *key, const Octstr *value, int replace)
{
    struct meta_data *mdata, *curr;
    int ret = 0;

    if (data == NULL || group == NULL || value == NULL)
        return -1;

    mdata = meta_data_unpack(data);
    for (curr = mdata; curr != NULL; curr = curr->next) {
        if (octstr_str_case_compare(curr->group, group) == 0)
            break;
    }
    if (curr == NULL) {
        /* group doesn't exists */
        curr = meta_data_create();
        curr->group = octstr_create(group);
        curr->values = dict_create(10, octstr_destroy_item);
        if (mdata != NULL) {
            curr->next = mdata->next;
            mdata->next = curr;
        } else {
            mdata = curr;
        }
    }
    if (replace) {
        /* delete old value if any */
        dict_put(curr->values, (Octstr *) key, NULL);
        /* put new value */
        dict_put(curr->values, (Octstr *) key, octstr_duplicate(value));
    } else if (dict_get(curr->values, (Octstr *) key) == NULL) {
        /* put new value */
        dict_put(curr->values, (Octstr *) key, octstr_duplicate(value));
    }

    /* pack it */
    ret = meta_data_pack(mdata, data);

    meta_data_destroy(mdata);

    return ret;
}


Octstr *meta_data_get_value(Octstr *data, const char *group, const Octstr *key)
{
    struct meta_data *mdata, *curr;
    Octstr *ret = NULL;

    if (data == NULL || group == NULL || key == NULL)
        return NULL;

    mdata = meta_data_unpack(data);
    if (mdata == NULL)
        return NULL;
    for (curr = mdata; curr != NULL; curr = curr->next) {
        if (octstr_str_case_compare(curr->group, group) == 0) {
            ret = dict_remove(curr->values, (Octstr *) key);
            break;
        }
    }

    meta_data_destroy(mdata);

    return ret;
}
