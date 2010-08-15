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
 * wsencode.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Encoding and decoding routines to store different types of data to
 * the format, specified by the WMLScript specification.
 *
 */

#ifndef WSENCODE_H
#define WSENCODE_H

/********************* Types and defintions *****************************/

/* Macros to store and restore integers from data buffers. */

#define WS_PUT_UINT8(buf, val)				\
    do {						\
        unsigned char *_p = (buf);			\
        _p[0] = ((val) & 0xff);				\
    } while (0)


#define WS_PUT_UINT16(buf, val)				\
    do {						\
        unsigned char *_p = (buf);			\
        _p[0] = (((val) & 0xff00) >> 8);		\
        _p[1] = ((val) & 0xff);				\
    } while (0)

#define WS_PUT_UINT32(buf, val)				\
    do {						\
        unsigned char *_p = (buf);			\
        _p[0] = (((val) & 0xff000000) >> 24);		\
        _p[1] = (((val) & 0x00ff0000) >> 16);		\
        _p[2] = (((val) & 0x0000ff00) >> 8);		\
        _p[3] = ((val) & 0x000000ff);			\
    } while (0)

#define WS_GET_UINT8(buf, var)				\
    do {						\
        const unsigned char *_p = (buf);		\
        (var) = _p[0];					\
    } while (0);

#define WS_GET_UINT16(buf, var)				\
    do {						\
        const unsigned char *_p = (buf);		\
        WsUInt16 _val;					\
        _val = _p[0];					\
        _val <<= 8;					\
        _val |= _p[1];					\
        (var) = _val;					\
    } while (0);

#define WS_GET_UINT32(buf, var)				\
    do {						\
        const unsigned char *_p = (buf);		\
        WsUInt32 _val;					\
        _val = _p[0];					\
        _val <<= 8;					\
        _val |= _p[1];					\
        _val <<= 8;					\
        _val |= _p[2];					\
        _val <<= 8;					\
        _val |= _p[3];					\
        (var) = _val;					\
    } while (0);

/* The maximum length of a multi-byte encoded WsUInt32 integer (in
   bytes). */
#define WS_MB_UINT32_MAX_ENCODED_LEN	5

/* Type specifiers for the ws_{encode,decode}_buffer() functions. */
typedef enum
{
    /* The terminator of the encoding list.  This must be the last item
       in all encoding and decoding function calls. */
    WS_ENC_END,

    /* 8 bits of data.  The value must be given as `WsByte'. */
    WS_ENC_BYTE,

    /* A signed 8 bit integer.  The value must be given as `WsInt8'. */
    WS_ENC_INT8,

    /* An unsigned 8 bit integer.  The value must be given as `WsUInt8'. */
    WS_ENC_UINT8,

    /* A signed 16 bit integer.  The value must be given as `WsInt16'. */
    WS_ENC_INT16,

    /* An unsigned 16 bit integer.  The value must be given as `WsUInt16'. */
    WS_ENC_UINT16,

    /* A signed 32 bit integer.  The value must be given as `WsInt32'. */
    WS_ENC_INT32,

    /* An unsigned 32 bit integer.  The value must be given as `WsUInt32'. */
    WS_ENC_UINT32,

    /* An unsigned 16 bit integer in the multi-byte format.  The value
       must be given as `WsUInt16'. */
    WS_ENC_MB_UINT16,

    /* An unsigned 32 bit integer in the multi-byte format.  The value
       must be given as `WsUInt32'. */
    WS_ENC_MB_UINT32,

    /* Binary data specified with two arguments: unsigned char *, size_t */
    WS_ENC_DATA
} WsEncodingSpec;

/********************* Global functions *********************************/

/* Encode the unsigned 32 bit integer `value' to the multi-byte format
   to the buffer `buffer'.  The buffer `buffer' must have at least
   WS_MB_UINT32_MAX_ENCODED_LEN bytes of data.  The function returns a
   pointer, pointing to the beginning of the encoded data.  Note that
   the returned pointer does not necessarily point to the beginning of
   the buffer `buffer'.  The size of the encoded multi-byte value is
   returned in `len_return'. */
unsigned char *ws_encode_mb_uint32(WsUInt32 value, unsigned char *buffer,
                                   size_t *len_return);

/* Decode a multi-byte encoded unsigned integer from the buffer
   `buffer'.  The function returns the decoded value.  The argument
   `len' must contain the length of the buffer `buffer'.  It is set to
   contain the length of the encoded value in the buffer.  The value,
   stored in `len', can be used to skip the multi-byte encoded value
   from the buffer `buffer'. */
WsUInt32 ws_decode_mb_uint32(const unsigned char *buffer, size_t *len);

/* Encode data as specified in the WsEncodingSpec encoded argument
   list `...' into the buffer `buffer'.  The function returns WS_TRUE
   if the encoding was successful or WS_FALSE otherwise (out of
   memory). */
WsBool ws_encode_buffer(WsBuffer *buffer, ...);

/* Decode data from the buffer `buffer', `buffer_len' according to the
   WsEncodingSpec encoded argument list `...'.  The argument list
   `...' must be encoded as in ws_encode_buffer() but the values must
   be replaced with pointers to variables of the type.  The function
   returns the number of bytes decoded from the buffer or 0 if the
   decoding failed. */
size_t ws_decode_buffer(const unsigned char *buffer, size_t buffer_len, ...);

#endif /* not WSENCODE_H */
