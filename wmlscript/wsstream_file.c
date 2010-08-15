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
 * wsstream_file.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Implementation of the file stream.
 *
 */

#include "wsint.h"

/********************* Types and definitions ****************************/

struct WsStreamFileCtxRec
{
    FILE *fp;

    /* Should the `fp' be closed when the stream is closed. */
    WsBool close_fp;

    /* A temporary buffer for the raw file data. */
    unsigned char buf[WS_STREAM_BUFFER_SIZE];

    /* For file output streams, this variable holds the number of data
       in `buf'. */
    size_t data_in_buf;

    /* Other fields (like character set conversion information) might be
       defined later. */
};

typedef struct WsStreamFileCtxRec WsStreamFileCtx;

/********************* Static method functions **************************/

static size_t file_input(void *context, WsUInt32 *buf, size_t buflen)
{
    WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;
    size_t read = 0;

    while (buflen > 0) {
        size_t toread = buflen < sizeof(ctx->buf) ? buflen : sizeof(ctx->buf);
        size_t got, i;

        got = fread(ctx->buf, 1, toread, ctx->fp);

        /* Convert the data to the stream's IO buffer. */
        for (i = 0; i < got; i++)
            buf[i] = ctx->buf[i];

        buflen -= got;
        buf += got;
        read += got;

        if (got < toread)
            /* EOF seen. */
            break;
    }

    return read;
}


static size_t file_output(void *context, WsUInt32 *buf, size_t buflen)
{
    WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;
    size_t wrote = 0;
    unsigned char ch;

    while (buflen) {
        /* Do we have any space in the stream's internal IO buffer? */
        if (ctx->data_in_buf >= WS_STREAM_BUFFER_SIZE) {
            size_t w;

            /* No, flush something to our file stream. */
            w = fwrite(ctx->buf, 1, ctx->data_in_buf, ctx->fp);
            if (w < ctx->data_in_buf) {
                /* Write failed.  As a result code we return the number
                          of characters written from our current write
                          request. */
                ctx->data_in_buf = 0;
                return wrote;
            }

            ctx->data_in_buf = 0;
        }
        /* Now we have space in the internal buffer. */

        /* Here we could perform some sort of conversions from ISO 10646
           to the output character set.  Currently we just support
           ISO-8859/1 and all unknown characters are replaced with
           '?'. */

        if (*buf > 0xff)
            ch = '?';
        else
            ch = (unsigned char) * buf;

        ctx->buf[ctx->data_in_buf++] = ch;

        /* Move forward. */
        buf++;
        buflen--;
        wrote++;
    }

    return wrote;
}


static WsBool file_flush(void *context)
{
    WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;

    /* If the internal buffer has any data, then this stream must be an
       output stream.  The variable `data_in_buf' is not updated on
       input streams. */
    if (ctx->data_in_buf) {
        if (fwrite(ctx->buf, 1, ctx->data_in_buf, ctx->fp) != ctx->data_in_buf) {
            /* The write failed. */
            ctx->data_in_buf = 0;
            return WS_FALSE;
        }

        /* The temporary buffer is not empty. */
        ctx->data_in_buf = 0;
    }

    /* Flush the underlying file stream. */
    return fflush(ctx->fp) == 0;
}


static void file_close(void *context)
{
    WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;

    if (ctx->close_fp)
        fclose(ctx->fp);

    ws_free(ctx);
}

/********************* Global functions *********************************/

WsStream *ws_stream_new_file(FILE *fp, WsBool output, WsBool close)
{
    WsStreamFileCtx *ctx = ws_calloc(1, sizeof(*ctx));
    WsStream *stream;

    if (ctx == NULL)
        return NULL;

    ctx->fp = fp;
    ctx->close_fp = close;

    if (output)
        stream = ws_stream_new(ctx, file_output, file_flush, file_close);
    else
        stream = ws_stream_new(ctx, file_input, file_flush, file_close);

    if (stream == NULL)
        /* The stream creation failed.  Close the stream context. */
        file_close(ctx);

    return stream;
}
