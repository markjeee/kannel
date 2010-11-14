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
 * gwmem.h
 *
 * This is a simple malloc()-wrapper. It does not return NULLs but
 * instead panics.
 *
 * We have two wrappers. One that just checks for allocation failures and
 * panics if they happen and one that tries to find allocation problems,
 * such as using an area after it has been freed.
 *
 * Kalle Marjola
 * Lars Wirzenius
 */

#ifndef GWMEM_H
#define GWMEM_H


void *gw_native_noop(void *ptr);
void gw_native_init(void);
void gw_native_check_leaks(void);
void *gw_native_malloc(size_t size);
void *gw_native_calloc(int nmemb, size_t size);
void *gw_native_realloc(void *ptr, size_t size);
void gw_native_free(void *ptr);
char *gw_native_strdup(const char *str);
void gw_native_shutdown(void);


void gw_check_init_mem(int slow_flag);
void gw_check_check_leaks(void);
void *gw_check_malloc(size_t size, 
	const char *filename, long line, const char *function);
void *gw_check_calloc(int nmemb, size_t size, 
	const char *filename, long line, const char *function);
void *gw_check_realloc(void *p, size_t size, 
	const char *filename, long line, const char *function);
void  gw_check_free(void *p, 
	const char *filename, long line, const char *function);
char *gw_check_strdup(const char *str, 
	const char *filename, long line, const char *function);
int gw_check_is_allocated(void *p);
long gw_check_area_size(void *p);
void *gw_check_claim_area(void *p,
	const char *filename, long line, const char *function);
void gw_check_shutdown(void);



/*
 * "slow" == "checking" with a small variation.
 */
#if USE_GWMEM_SLOW
#define USE_GWMEM_CHECK 1
#endif


#if USE_GWMEM_NATIVE

/*
 * The `native' wrapper.
 */

#define gw_init_mem()
#define gw_check_leaks()
#define gw_malloc(size) (gw_native_malloc(size))
#define gw_malloc_trace(size, file, line, func) (gw_native_malloc(size))
#define gw_calloc(nmemb, size) (gw_native_calloc(nmemb, size))
#define gw_realloc(ptr, size) (gw_native_realloc(ptr, size))
#define gw_free(ptr) (gw_native_free(ptr))
#define gw_strdup(str) (gw_native_strdup(str))
#define gw_assert_allocated(ptr, file, line, function)
#define gw_claim_area(ptr) (gw_native_noop(ptr))
#define gw_claim_area_for(ptr, file, line, func) (gw_native_noop(ptr))
#define gwmem_shutdown()
#define gwmem_type() (octstr_imm("native"))

#elif USE_GWMEM_CHECK

/*
 * The `check' wrapper.
 */

#ifdef USE_GWMEM_SLOW
#define gw_init_mem() (gw_check_init_mem(1))
#define gwmem_type() (octstr_imm("slow"))
#else
#define gw_init_mem() (gw_check_init_mem(0))
#define gwmem_type() (octstr_imm("checking"))
#endif

#define gw_check_leaks() (gw_check_check_leaks())
#define gw_malloc_trace(size, file, line, func) \
	(gw_check_malloc(size, file, line, func))
#define gw_malloc(size) \
	(gw_check_malloc(size, __FILE__, __LINE__, __func__))
#define gw_calloc(nmemb, size) \
	(gw_check_calloc(nmemb, size, __FILE__, __LINE__, __func__))
#define gw_realloc(ptr, size) \
	(gw_check_realloc(ptr, size, __FILE__, __LINE__, __func__))
#define gw_free(ptr) \
	(gw_check_free(ptr, __FILE__, __LINE__, __func__))
#define gw_strdup(str) \
	(gw_check_strdup(str, __FILE__, __LINE__, __func__))
#define gw_assert_allocated(ptr, file, line, function) \
	(gw_assert_place(gw_check_is_allocated(ptr), file, line, function))
#define gw_claim_area(ptr) \
	(gw_check_claim_area(ptr, __FILE__, __LINE__, __func__))
#define gw_claim_area_for(ptr, file, line, func) \
	(gw_check_claim_area(ptr, file, line, func))
#define gwmem_shutdown() (gw_check_shutdown())

#else

/*
 * Unknown wrapper. Oops.
 */
#error "Unknown malloc wrapper."


#endif


/*
 * Make sure no-one uses the unwrapped functions by mistake.
 */

/* undefine first to avoid compiler warnings about redefines */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup

#define malloc(n)	do_not_call_malloc_directly
#define calloc(a, b)	do_not_use_calloc
#define realloc(p, n)	do_not_call_realloc_directly
#define free(p)	    	do_not_call_free_directly
#define strdup(p)    	do_not_call_strdup_directly


#endif
