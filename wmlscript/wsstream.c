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
 * wsstream.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Generic input / output stream.
 *
 */

#include "wsint.h"

/********************* Global functions *********************************/

WsBool ws_stream_getc(WsStream *stream, WsUInt32 *ch_return)
{
    if (stream->ungetch_valid) {
        *ch_return = stream->ungetch;
        stream->ungetch_valid = WS_FALSE;

        return WS_TRUE;
    }

    if (stream->buffer_pos >= stream->data_in_buffer) {
        /* Read more data to the buffer. */
        stream->buffer_pos = 0;
        stream->data_in_buffer = (*stream->io)(stream->context,
                                               stream->buffer,
                                               WS_STREAM_BUFFER_SIZE);
        if (stream->data_in_buffer == 0)
            /* EOF reached. */
            return WS_FALSE;
    }

    /* Return the next character. */
    *ch_return = stream->buffer[stream->buffer_pos++];

    return WS_TRUE;
}


void ws_stream_ungetc(WsStream *stream, WsUInt32 ch)
{
    stream->ungetch = ch;
    stream->ungetch_valid = WS_TRUE;
}


WsBool ws_stream_flush(WsStream *stream)
{
    if (stream->flush)
        return (*stream->flush)(stream->context);

    return WS_TRUE;
}


void ws_stream_close(WsStream *stream)
{
    if (stream->close)
        (*stream->close)(stream->context);

    ws_free(stream);
}


WsStream *ws_stream_new(void *context, WsStreamIOProc io,
                        WsStreamFlushProc flush, WsStreamCloseProc close)
{
    WsStream *stream = ws_calloc(1, sizeof(*stream));

    if (stream == NULL)
        return NULL;

    stream->io = io;
    stream->flush = flush;
    stream->close = close;
    stream->context = context;

    return stream;
}
