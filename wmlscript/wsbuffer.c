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
 * wsbuffer.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * A multipurpose buffer.
 *
 */

#include "wsint.h"

/********************* Global functions *********************************/

void ws_buffer_init(WsBuffer *buffer)
{
    buffer->len = 0;
    buffer->data = NULL;
}


void ws_buffer_uninit(WsBuffer *buffer)
{
    ws_free(buffer->data);
    buffer->len = 0;
    buffer->data = NULL;
}


WsBuffer *ws_buffer_alloc()
{
    return ws_calloc(1, sizeof(WsBuffer));
}


void ws_buffer_free(WsBuffer *buffer)
{
    ws_free(buffer->data);
    ws_free(buffer);
}


WsBool ws_buffer_append(WsBuffer *buffer, unsigned char *data, size_t len)
{
    unsigned char *p;

    if (!ws_buffer_append_space(buffer, &p, len))
        return WS_FALSE;

    memcpy(p, data, len);

    return WS_TRUE;
}


WsBool ws_buffer_append_space(WsBuffer *buffer, unsigned char **p, size_t size)
{
    unsigned char *ndata = ws_realloc(buffer->data, buffer->len + size);

    if (ndata == NULL)
        return WS_FALSE;

    buffer->data = ndata;

    if (p)
        *p = buffer->data + buffer->len;

    buffer->len += size;

    return WS_TRUE;
}


unsigned char *ws_buffer_ptr(WsBuffer *buffer)
{
    return buffer->data;
}


size_t ws_buffer_len(WsBuffer *buffer)
{
    return buffer->len;
}


unsigned char *ws_buffer_steal(WsBuffer *buffer)
{
    unsigned char *p = buffer->data;

    buffer->data = NULL;
    buffer->len = 0;

    return p;
}
