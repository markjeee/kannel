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
 * dict.c - lookup data structure using octet strings as keys
 *
 * The Dict is implemented as a simple hash table. In the future, it 
 * might be interesting to use a trie instead.
 *
 * Lars Wirzenius, based on code by Tuomas Luttinen
 */


#include "gwlib.h"


/*
 * The hash table stores key/value -pairs in a List.
 */

typedef struct Item Item;
struct Item {
    Octstr *key;
    void *value;
};


static Item *item_create(Octstr *key, void *value)
{
    Item *item;
    
    item = gw_malloc(sizeof(*item));
    item->key = octstr_duplicate(key);
    item->value = value;
    return item;
}

static void item_destroy(void *item)
{
    Item *p;
    
    p = item;
    octstr_destroy(p->key);
    gw_free(p);
}


static int item_has_key(void *item, void *key)
{
    return octstr_compare(key, ((Item *) item)->key) == 0;
}


/*
 * The dictionary itself is a very simple hash table.
 * `tab' is an array of Lists of Items, in which empty Lists may be
 * represented as NULL.  `size' is the number of elements allocated
 * for the array, and `key_count' is the number of Items currently
 * in the table.  `key_count' is kept up to date by the put and remove
 * functions, and is used to make dict_key_count() faster.
 */

struct Dict {
    List **tab;
    long size;
    long key_count;
    void (*destroy_value)(void *);
    Mutex *lock;
};


static void lock(Dict *dict)
{
    mutex_lock(dict->lock);
}


static void unlock(Dict *dict)
{
    mutex_unlock(dict->lock);
}


static long key_to_index(Dict *dict, Octstr *key)
{
    return octstr_hash_key(key) % dict->size;
}

static int handle_null_value(Dict *dict, Octstr *key, void *value)
{
    if (value == NULL) {
        value = dict_remove(dict, key);
	if (dict->destroy_value != NULL)
	    dict->destroy_value(value);
        return 1;
    }

    return 0;
}

static int dict_put_true(Dict *dict, Octstr *key, void *value)
{
    Item *p;
    long i;
    int item_unique;

    item_unique = 0;
    lock(dict);
    i = key_to_index(dict, key);

    if (dict->tab[i] == NULL) {
	dict->tab[i] = gwlist_create();
	p = NULL;
    } else {
	p = gwlist_search(dict->tab[i], key, item_has_key);
    }

    if (p == NULL) {
    	p = item_create(key, value);
	gwlist_append(dict->tab[i], p);
        dict->key_count++;
        item_unique = 1;
    } else {
    	if (dict->destroy_value != NULL)
    	    dict->destroy_value(value);
        item_unique = 0;
    }

    unlock(dict);

    return item_unique;
}

/*
 * And finally, the public functions.
 */


Dict *dict_create(long size_hint, void (*destroy_value)(void *))
{
    Dict *dict;
    long i;
    
    dict = gw_malloc(sizeof(*dict));

    /*
     * Hash tables tend to work well until they are fill to about 50%.
     */
    dict->size = size_hint * 2;

    dict->tab = gw_malloc(sizeof(dict->tab[0]) * dict->size);
    for (i = 0; i < dict->size; ++i)
    	dict->tab[i] = NULL;
    dict->lock = mutex_create();
    dict->destroy_value = destroy_value;
    dict->key_count = 0;
    
    return dict;
}


void dict_destroy(Dict *dict)
{
    long i;
    Item *p;
    
    if (dict == NULL)
        return;

    for (i = 0; i < dict->size; ++i) {
        if (dict->tab[i] == NULL)
	    continue;

	while ((p = gwlist_extract_first(dict->tab[i])) != NULL) {
	    if (dict->destroy_value != NULL)
	    	dict->destroy_value(p->value);
	    item_destroy(p);
	}
	gwlist_destroy(dict->tab[i], NULL);
    }
    mutex_destroy(dict->lock);
    gw_free(dict->tab);
    gw_free(dict);
}


void dict_put(Dict *dict, Octstr *key, void *value)
{
    long i;
    Item *p;

    if (value == NULL) {
        value = dict_remove(dict, key);
	if (dict->destroy_value != NULL)
	    dict->destroy_value(value);
        return;
    }

    lock(dict);
    i = key_to_index(dict, key);
    if (dict->tab[i] == NULL) {
	dict->tab[i] = gwlist_create();
	p = NULL;
    } else
	p = gwlist_search(dict->tab[i], key, item_has_key);
    if (p == NULL) {
    	p = item_create(key, value);
	gwlist_append(dict->tab[i], p);
        dict->key_count++;
    } else {
	if (dict->destroy_value != NULL)
	    dict->destroy_value(p->value);
	p->value = value;
    }
    unlock(dict);
}

int dict_put_once(Dict *dict, Octstr *key, void *value)
{
    int ret;

    ret = 1;
    if (handle_null_value(dict, key, value))
        return 1;
    if (dict_put_true(dict, key, value)) {
        ret = 1;
    } else {
        ret = 0;
    }
    return ret;
}

void *dict_get(Dict *dict, Octstr *key)
{
    long i;
    Item *p;
    void *value;

    lock(dict);
    i = key_to_index(dict, key);
    if (dict->tab[i] == NULL)
	p = NULL;
    else
        p = gwlist_search(dict->tab[i], key, item_has_key);
    if (p == NULL)
    	value = NULL;
    else
    	value = p->value;
    unlock(dict);
    return value;
}


void *dict_remove(Dict *dict, Octstr *key)
{
    long i;
    Item *p;
    void *value;
    List *list;

    lock(dict);
    i = key_to_index(dict, key);
    if (dict->tab[i] == NULL)
        list = NULL;
    else
        list = gwlist_extract_matching(dict->tab[i], key, item_has_key);
    gw_assert(list == NULL || gwlist_len(list) == 1);
    if (list == NULL)
    	value = NULL;
    else {
	p = gwlist_get(list, 0);
	gwlist_destroy(list, NULL);
    	value = p->value;
	item_destroy(p);
	dict->key_count--;
    }
    unlock(dict);
    return value;
}


long dict_key_count(Dict *dict)
{
    long result;

    lock(dict);
    result = dict->key_count;
    unlock(dict);

    return result;
}


List *dict_keys(Dict *dict)
{
    List *list;
    Item *item;
    long i, j;
    
    list = gwlist_create();

    lock(dict);
    for (i = 0; i < dict->size; ++i) {
	if (dict->tab[i] == NULL)
	    continue;
	for (j = 0; j < gwlist_len(dict->tab[i]); ++j) {
	    item = gwlist_get(dict->tab[i], j);
	    gwlist_append(list, octstr_duplicate(item->key));
	}
    }
    unlock(dict);
    
    return list;
}







