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
 *
 * wshash.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * A mapping from null-terminated strings to `void *' pointers.
 *
 */

#include "wsint.h"
#include "wshash.h"

/********************* Types and definitions ****************************/

/* The size of the hash table. */
#define WS_HASH_TABLE_SIZE	256

/* A hash item. */
struct WsHashItemRec
{
    struct WsHashItemRec *next;
    char *name;
    void *data;
};

typedef struct WsHashItemRec WsHashItem;

/* The hash object. */
struct WsHashRec
{
    WsHashItem *items[WS_HASH_TABLE_SIZE];
    WsHashItemDestructor destructor;
    void *destructor_context;
};

/********************* Prototypes for static functions ******************/

/* Hash function to count the hash value of string `string'.  */
static size_t count_hash(const char *string);

/********************* Global functions *********************************/

WsHashPtr ws_hash_create(WsHashItemDestructor destructor, void *context)
{
    WsHashPtr hash = ws_calloc(1, sizeof(*hash));

    if (hash) {
        hash->destructor = destructor;
        hash->destructor_context = context;
    }

    return hash;
}


void ws_hash_destroy(WsHashPtr hash)
{
    if (hash == NULL)
        return;

    ws_hash_clear(hash);
    ws_free(hash);
}


WsBool ws_hash_put(WsHashPtr hash, const char *name, void *data)
{
    WsHashItem *i;
    size_t h = count_hash(name);

    for (i = hash->items[h]; i; i = i->next) {
        if (strcmp(i->name, name) == 0) {
            /* Found it. */

            /* Destroy the old item */
            if (hash->destructor)
                (*hash->destructor)(i->data, hash->destructor_context);

            i->data = data;

            return WS_FALSE;
        }
    }

    /* Must create a new mapping. */
    i = ws_calloc(1, sizeof(*i));

    if (i == NULL)
        return WS_FALSE;

    i->name = ws_strdup(name);
    if (i->name == NULL) {
        ws_free(i);
        return WS_FALSE;
    }

    i->data = data;

    /* Link it to our hash. */
    i->next = hash->items[h];
    hash->items[h] = i;

    return WS_TRUE;
}


void *ws_hash_get(WsHashPtr hash, const char *name)
{
    WsHashItem *i;
    size_t h = count_hash(name);

    for (i = hash->items[h]; i; i = i->next)
        if (strcmp(i->name, name) == 0)
            return i->data;

    return NULL;
}


void ws_hash_clear(WsHashPtr hash)
{
    WsHashItem *i, *n;
    size_t j;

    for (j = 0; j < WS_HASH_TABLE_SIZE; j++) {
        for (i = hash->items[j]; i; i = n) {
            n = i->next;
            if (hash->destructor)
                (*hash->destructor)(i->data, hash->destructor_context);

            ws_free(i->name);
            ws_free(i);
        }
        hash->items[j] = NULL;
    }
}

/********************* Static functions *********************************/

static size_t count_hash(const char *string)
{
    size_t val = 0;
    int i;

    for (i = 0; string[i]; i++) {
        val <<= 3;
        val ^= string[i];
        val ^= (val & 0xff00) >> 5;
        val ^= (val & 0xff0000) >> 16;
    }

    return val % WS_HASH_TABLE_SIZE;
}
