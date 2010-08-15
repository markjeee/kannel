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
 * wsencode.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Encoding and decoding routines.
 *
 */

#include "wsint.h"

/********************* Types and definitions ****************************/

#define WS_MB_CONT_BIT	0x80
#define WS_MB_DATA_MASK	0x7f

/********************* Global functions *********************************/

unsigned char *ws_encode_mb_uint32(WsUInt32 value, unsigned char *buffer,
                                   size_t *len_return)
{
    unsigned char *p = buffer + WS_MB_UINT32_MAX_ENCODED_LEN;
    size_t len = 1;

    /* Set the terminator byte. */
    *p = value & WS_MB_DATA_MASK;
    value >>= 7;
    p--;

    /* And add the data. */
    while (value > 0) {
        *p = (value & WS_MB_DATA_MASK) | WS_MB_CONT_BIT;
        value >>= 7;
        p--;
        len++;
    }

    *len_return = len;

    return p + 1;
}


WsUInt32 ws_decode_mb_uint32(const unsigned char *buffer, size_t *len)
{
    WsUInt32 value = 0;
    size_t i;

    for (i = 0; i < *len; i++) {
        value <<= 7;
        value |= buffer[i] & WS_MB_DATA_MASK;

        if ((buffer[i] & WS_MB_CONT_BIT) == 0)
            break;
    }

    *len = i + 1;

    return value;
}


WsBool ws_encode_buffer(WsBuffer *buffer, ...)
{
    va_list ap;
    WsEncodingSpec spec;
    int ival;
    unsigned char *p;
    unsigned char *cp;
    size_t len;
    WsUInt32 ui32;
    unsigned char data[64];

    va_start(ap, buffer);

    while ((spec = va_arg(ap, int)) != WS_ENC_END)
    {
        switch (spec)
        {
        case WS_ENC_BYTE:
        case WS_ENC_INT8:
        case WS_ENC_UINT8:
            ival = va_arg(ap, int);

            if (!ws_buffer_append_space(buffer, &p, 1))
                goto error;

            WS_PUT_UINT8(p, ival);
            break;

        case WS_ENC_INT16:
        case WS_ENC_UINT16:
            ival = va_arg(ap, int);

            if (!ws_buffer_append_space(buffer, &p, 2))
                goto error;

            WS_PUT_UINT16(p, ival);
            break;

        case WS_ENC_INT32:
        case WS_ENC_UINT32:
            ival = va_arg(ap, long);

            if (!ws_buffer_append_space(buffer, &p, 4))
                goto error;

            WS_PUT_UINT32(p, ival);
            break;

        case WS_ENC_MB_UINT16:
        case WS_ENC_MB_UINT32:
            if (spec == WS_ENC_MB_UINT16)
                ui32 = va_arg(ap, int);
            else
                ui32 = va_arg(ap, long);

            len = sizeof(data);
            cp = ws_encode_mb_uint32(ui32, data, &len);

            if (!ws_buffer_append_space(buffer, &p, len))
                goto error;

            memcpy(p, cp, len);
            break;

        case WS_ENC_DATA:
            cp = va_arg(ap, unsigned char *);
            len = va_arg(ap, unsigned int);

            if (!ws_buffer_append_space(buffer, &p, len))
                goto error;

            memcpy(p, cp, len);
            break;

        default:
            ws_fatal("ws_encode_buffer(): unknown type %d: probably a missing "
                     "WS_ENC_END",
                     spec);
            break;
        }
    }

    va_end(ap);

    return WS_TRUE;


    /*
     * Error handling.
     */

error:

    va_end(ap);

    return WS_FALSE;
}


size_t ws_decode_buffer(const unsigned char *buffer, size_t buffer_len, ...)
{
    va_list ap;
    WsEncodingSpec spec;
    WsUInt8 *i8p;
    WsUInt16 *i16p;
    WsUInt32 *i32p;
    unsigned char **cpp;
    size_t len;
    size_t orig_buffer_len = buffer_len;

    va_start(ap, buffer_len);

    while ((spec = va_arg(ap, int)) != WS_ENC_END) {
        switch (spec) {
        case WS_ENC_BYTE:
        case WS_ENC_INT8:
        case WS_ENC_UINT8:
            if (buffer_len < 1)
                goto too_short_buffer;

            i8p = va_arg(ap, WsUInt8 *);
            WS_GET_UINT8(buffer, *i8p);

            buffer++;
            buffer_len--;
            break;

        case WS_ENC_INT16:
        case WS_ENC_UINT16:
            if (buffer_len < 2)
                goto too_short_buffer;

            i16p = va_arg(ap, WsUInt16 *);
            WS_GET_UINT16(buffer, *i16p);

            buffer += 2;
            buffer_len -= 2;
            break;

        case WS_ENC_INT32:
        case WS_ENC_UINT32:
            if (buffer_len < 4)
                goto too_short_buffer;

            i32p = va_arg(ap, WsUInt32 *);
            WS_GET_UINT32(buffer, *i32p);

            buffer += 4;
            buffer_len -= 4;
            break;

        case WS_ENC_MB_UINT16:
        case WS_ENC_MB_UINT32:
            {
                size_t len = buffer_len;
                WsUInt32 i32;

                if (buffer_len < 1)
                    goto too_short_buffer;

                i32 = ws_decode_mb_uint32(buffer, &len);

                if (spec == WS_ENC_MB_UINT16) {
                    i16p = va_arg(ap, WsUInt16 *);
                    *i16p = i32;
                } else {
                    i32p = va_arg(ap, WsUInt32 *);
                    *i32p = i32;
                }

                buffer += len;
                buffer_len -= len;
            }
            break;

        case WS_ENC_DATA:
            cpp = va_arg(ap, unsigned char **);
            len = va_arg(ap, size_t);

            if (buffer_len < len)
                goto too_short_buffer;

            *cpp = (unsigned char *) buffer;
            buffer += len;
            buffer_len -= len;
            break;

        default:
            ws_fatal("ws_encode_buffer(): unknown type %d: probably a missing "
                     "WS_ENC_END",
                     spec);
            break;
        }
    }
    va_end(ap);

    return orig_buffer_len - buffer_len;

too_short_buffer:

    va_end(ap);

    return 0;
}
