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
 * wsfalloc.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Fast memory allocation routines with easy cleanup.
 *
 */

#ifndef WSFALLOC_H
#define WSFALLOC_H

/********************* Types and definitions ****************************/

struct WsFastMallocBlockRec
{
    struct WsFastMallocBlockRec *next;
    /* The data follows immediately here. */
};

typedef struct WsFastMallocBlockRec WsFastMallocBlock;

struct WsFastMallocRec
{
    WsFastMallocBlock *blocks;

    /* The default block size of this pool. */
    size_t block_size;

    /* The number of bytes allocates for user blocks. */
    size_t user_bytes_allocated;

    /* The next allocation can be done from this position. */
    unsigned char *ptr;

    /* And it has this much space. */
    size_t size;
};

typedef struct WsFastMallocRec WsFastMalloc;

/********************* Prototypes for global functions ******************/

/* Create a new fast memory allocator with internal block size of
   `block_size' bytes.  The function returns NULL if the creation
   failed. */
WsFastMalloc *ws_f_create(size_t block_size);

/* Destroy the fast allocator `pool' and free all resources it has
   allocated.  All memory chunks, allocated from this pool will be
   invalidated with this call. */
void ws_f_destroy(WsFastMalloc *pool);

/* Allocate `size' bytes of memory from the pool `pool'.  The function
   returns NULL if the allocation fails. */
void *ws_f_malloc(WsFastMalloc *pool, size_t size);

/* Allocate `num' items of size `size' from the pool `pool'.  The
   returned memory block is initialized with zero.  The function
   returns NULL if the allocation fails. */
void *ws_f_calloc(WsFastMalloc *pool, size_t num, size_t size);

/* Take a copy of the memory buffer `ptr' which has `size' bytes of
   data.  The copy is allocated from the pool `pool'.  The function
   returns NULL if the allocation fails. */
void *ws_f_memdup(WsFastMalloc *pool, const void *ptr, size_t size);

/* Take a copy of the C-string `str'.  The copy is allocated from the
   pool `pool'.  The function returns NULL if the allocation fails. */
void *ws_f_strdup(WsFastMalloc *pool, const char *str);

#endif /* not WSFALLOC_H */
