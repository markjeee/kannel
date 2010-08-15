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
 * wsbuffer.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * A multipurpose buffer.
 *
 */

#ifndef WSBUFFER_H
#define WSBUFFER_H

/********************* Types and defintions *****************************/

/* A multipurpose buffer.  The contents of the buffer handle is
   visible but its internals should not be modified directly. */
struct WsBufferRec
{
    size_t len;
    unsigned char *data;
};

typedef struct WsBufferRec WsBuffer;

/********************* Global functions *********************************/

/* Initialize the buffer `buffer'.  The buffer is not allocated; the
   argument `buffer' must point to allocated buffer. */
void ws_buffer_init(WsBuffer *buffer);

/* Uninitialize buffer `buffer'.  The actual buffer structure is not
   freed; only its internally allocated buffer is freed. */
void ws_buffer_uninit(WsBuffer *buffer);

/* Allocate and initialize a new buffer.  The function returns NULL if
   the allocation failed. */
WsBuffer *ws_buffer_alloc(void);

/* Free the buffer `buffer' and all its resources. */
void ws_buffer_free(WsBuffer *buffer);

/* Append `size' bytes of data from `data' to the buffer `buffer'.
   The function returns WS_TRUE if the operation was successful or
   WS_FALSE otherwise. */
WsBool ws_buffer_append(WsBuffer *buffer, unsigned char *data, size_t len);

/* Append `size' bytes of space to the buffer `buffer'.  If the
   argument `p' is not NULL, it is set to point to the beginning of
   the appended space.  The function returns WS_TRUE if the operation
   was successful of WS_FALSE otherwise.  */
WsBool ws_buffer_append_space(WsBuffer *buffer, unsigned char **p, size_t size);

/* Return a pointer to the beginning of the buffer's data. */
unsigned char *ws_buffer_ptr(WsBuffer *buffer);

/* Return the length of the buffer `buffer'. */
size_t ws_buffer_len(WsBuffer *buffer);

/* Steal the buffer's data.  The function returns a pointer to the
   beginning of the buffer's data and re-initializes the buffer to
   empty data.  The returned data must be with the ws_free() function
   by the caller. */
unsigned char *ws_buffer_steal(WsBuffer *buffer);

#endif /* not WSBUFFER_H */
