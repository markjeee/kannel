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
 * wsfalloc.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Fast memory allocation routines.
 *
 */

#include "wsint.h"

/********************* Global functions *********************************/

WsFastMalloc *ws_f_create(size_t block_size)
{
    WsFastMalloc *pool = ws_calloc(1, sizeof(WsFastMalloc));

    if (pool == NULL)
        return NULL;

    pool->block_size = block_size;

    return pool;
}


void ws_f_destroy(WsFastMalloc *pool)
{
    WsFastMallocBlock *b, *bnext;

    if (pool == NULL)
        return;

    for (b = pool->blocks; b; b = bnext) {
        bnext = b->next;
        ws_free(b);
    }
    ws_free(pool);
}


void *ws_f_malloc(WsFastMalloc *pool, size_t size)
{
    unsigned char *result;

    /* Keep the blocks aligned, because this function is used to allocate
     * space for structures containing longs and such. */

    if (size % sizeof(long) != 0) {
        size += sizeof(long) - (size % sizeof(long));
    }

    if (pool->size < size) {
        size_t alloc_size;
        WsFastMallocBlock *b;

        /* Must allocate a fresh block. */
        alloc_size = pool->block_size;
        if (alloc_size < size)
            alloc_size = size;

        /* Allocate the block and remember to add the header size. */
        b = ws_malloc(alloc_size + sizeof(WsFastMallocBlock));

        if (b == NULL)
            /* No memory available. */
            return NULL;

        /* Add this block to the memory pool. */
        b->next = pool->blocks;
        pool->blocks = b;

        pool->ptr = ((unsigned char *) b) + sizeof(WsFastMallocBlock);
        pool->size = alloc_size;
    }

    /* Now we can allocate `size' bytes of data from this pool. */

    result = pool->ptr;

    pool->ptr += size;
    pool->size -= size;

    pool->user_bytes_allocated += size;

    return result;
}


void *ws_f_calloc(WsFastMalloc *pool, size_t num, size_t size)
{
    void *p = ws_f_malloc(pool, num * size);

    if (p == NULL)
        return p;

    memset(p, 0, num * size);

    return p;
}


void *ws_f_memdup(WsFastMalloc *pool, const void *ptr, size_t size)
{
    unsigned char *d = ws_f_malloc(pool, size + 1);

    if (d == NULL)
        return NULL;

    memcpy(d, ptr, size);
    d[size] = '\0';

    return d;
}


void *ws_f_strdup(WsFastMalloc *pool, const char *str)
{
    size_t len;
    char *s;

    if (str == NULL)
        return NULL;

    len = strlen(str) + 1;
    s = ws_f_malloc(pool, len);

    if (s == NULL)
        return NULL;

    memcpy(s, str, len);

    return s;
}
