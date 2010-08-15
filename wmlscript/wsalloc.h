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
 * wsalloc.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Memory allocation routines.
 *
 */

#ifndef WSALLOC_H
#define WSALLOC_H

#if WS_DEBUG
#define WS_MEM_DEBUG 1
#endif /* WS_DEBUG */

#if !WS_MEM_DEBUG

/********************* Prototypes for global functions ******************/

/* Allocate `size' bytes of memory.  The function returns NULL if the
 * allocation fails. */
void *ws_malloc(size_t size);

/* Allocate `num' items of size `size'.  The returned memory block is
 * initialied with zero.  The function returns NULL if the allocation
 * fails .*/
void *ws_calloc(size_t num, size_t size);

/* Reallocate the memory block `ptr' to size `size'.  The old data is
 * preserved in the new memory block.  The function returns NULL if
 * the allocation fails.  It is permissible to call the function with
 * NULL as the `ptr' argument of 0 as the `size' argument.  In these
 * cases, the function acts the "Right Way".  If the `ptr' is NULL,
 * the function allocates a fresh block of size `size'.  If the `size'
 * is NULL, the memory block `ptr' is freed. */
void *ws_realloc(void *ptr, size_t size);

/* Take a copy of the memory buffer `ptr' which has `size' bytes of
 * data.  The function returns NULL if the allocation fails.  The
 * returned buffer is null-terminated. */
void *ws_memdup(const void *ptr, size_t size);

/* Take a copy of the C-string `str'.  The function returns NULL if
 * the allocation fails. */
void *ws_strdup(const char *str);

/* Free the memory block `ptr' that was previously allocated with one
 * of the ws_{m,c,re}alloc() functions.  It is allowed to call the
 * function with NULL as the `ptr' argument. */
void ws_free(void *ptr);

#else /* WS_MEM_DEBUG */

/********************* Memory debugging routines ************************/

/* These macros and functions are used in debugging memory usage of
 * the compiler and to find out memory leaks.  When these functions
 * are used, each dynamically allocated block is recorded in a list of
 * active blocks, and allocated blocks are tagged with information
 * about their allocation location.  When the block is freed, it is
 * removed from the list and its contents is marked freed.  Typically
 * these functions detect memory leaks and freeing same memory block
 * multiple times.
 *  
 * These functions can also be used to test error recovery code of
 * memory allocation failures.  The function ws_clear_leaks() clears
 * the current information about used blocks and it sets the limit of
 * successful memory allocations.  When more than the limit number of
 * memory allocations have been performed, all memory allocations
 * fail.  When the tested function has returned, you can see if you
 * cleanup code did not free all blocks by using the functions
 * ws_hash_leaks() and ws_dump_blocks().
 *  
 * These functions are not thread safe.  They use shared static list
 * to record the active blocks and they do not use any sorts of
 * locking.
 */
    
/* Macros to tag the allocation source file location to the allocated
memory block. */

#define ws_malloc(_s) ws_malloc_i((_s), __FILE__, __LINE__)
#define ws_calloc(_n, _s) ws_calloc_i((_n), (_s), __FILE__, __LINE__)
#define ws_realloc(_p, _s) ws_realloc_i((_p), (_s), __FILE__, __LINE__)
#define ws_memdup(_p, _s) ws_memdup_i((_p), (_s), __FILE__, __LINE__)
#define ws_strdup(_s) ws_strdup_i((_s), __FILE__, __LINE__)
#define ws_free(_p) ws_free_i((_p))

/* The allocation and freeing functions. */

void *ws_malloc_i(size_t size, const char *file, int line);
void *ws_calloc_i(size_t num, size_t size, const char *file, int line);
void *ws_realloc_i(void *ptr, size_t size, const char *file, int line);
void *ws_memdup_i(const void *ptr, size_t size, const char *file, int line);
void *ws_strdup_i(const char *str, const char *file, int line);
void ws_free_i(void *ptr);

/* A predicate to check if the system currently has any allocated
 * blocks.  The function returns 1 if it has any blocks and 0
 * otherwise. */
int ws_has_leaks(void);

/* Dumps all currently allocated blocks, including their allocation
 * location, to standard error (stderr).  The function also prints
 * statistics about maximum memory usage. */
void ws_dump_blocks(void);

/* Clear all statistics and the list containing the currently
 * allocated leaks.  The argument `num_successful_allocs' sets the
 * limit how many memory allocations (assuming that the system has
 * enought memory) are successful.  If more than
 * `num_successful_allocs' are performed, the allocations routines
 * will fail and return the value NULL. */
void ws_clear_leaks(unsigned int num_successful_allocs);

#endif /* WS_MEM_DEBUG */

#endif /* not WSALLOC_H */
