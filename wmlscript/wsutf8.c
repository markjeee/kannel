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
 * wsutf8.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Functions to manipulate UTF-8 encoded strings.
 *
 * Specification: RFC-2279
 *
 */

#include "wsint.h"

/********************* Types and definitions ****************************/

/* Masks to determine the UTF-8 encoding of an ISO 10646 character. */
#define WS_UTF8_ENC_1_M	0xffffff80
#define WS_UTF8_ENC_2_M	0xfffff800
#define WS_UTF8_ENC_3_M	0xffff0000
#define WS_UTF8_ENC_4_M	0xffe00000
#define WS_UTF8_ENC_5_M	0xfc000000
#define WS_UTF8_ENC_6_M	0x80000000

/* The high-order bits.  This array can be indexed with the number of
   bytes in the encoding to get the initialization mask for the
   high-order bits. */
static unsigned char utf8_hibits[7] =
    {
        0x00, 				/* unused */
        0x00, 				/* 1 byte */
        0xc0, 				/* 2 bytes */
        0xe0, 				/* 3 bytes */
        0xf0, 				/* 4 bytes */
        0xf8, 				/* 5 bytes */
        0xfc, 				/* 6 bytes */
    };

/* The high-order bits for continuation bytes (10xxxxxx). */
#define WS_UTF8_ENC_C_BITS	0x80

/* Mask to get the continuation bytes from the character (00111111). */
#define WS_UTF8_CONT_DATA_MASK	0x3f

/* Determine the encoding type of the ISO 10646 character `ch'.  The
   argument `ch' must be given as `unsigned long'.  The macro returns
   0 if the value `ch' can not be encoded as UTF-8 and the number of
   bytes in the encoded value otherwise. */
#define WS_UTF8_ENC_TYPE(ch)			\
    (((ch) & WS_UTF8_ENC_1_M) == 0		\
     ? 1					\
     : (((ch) & WS_UTF8_ENC_2_M) == 0		\
       ? 2					\
       : (((ch) & WS_UTF8_ENC_3_M) == 0		\
         ? 3					\
         : (((ch) & WS_UTF8_ENC_4_M) == 0	\
           ? 4					\
           : (((ch) & WS_UTF8_ENC_5_M) == 0	\
             ? 5				\
             : (((ch) & WS_UTF8_ENC_6_M) == 0   \
               ? 6				\
               : 0))))))

/* Masks and values to determine the length of an UTF-8 encoded
   character. */
#define WS_UTF8_DEC_1_M	0x80
#define WS_UTF8_DEC_2_M	0xe0
#define WS_UTF8_DEC_3_M	0xf0
#define WS_UTF8_DEC_4_M	0xf8
#define WS_UTF8_DEC_5_M	0xfc
#define WS_UTF8_DEC_6_M	0xfe

#define WS_UTF8_DEC_1_V	0x00
#define WS_UTF8_DEC_2_V	0xc0
#define WS_UTF8_DEC_3_V	0xe0
#define WS_UTF8_DEC_4_V	0xf0
#define WS_UTF8_DEC_5_V	0xf8
#define WS_UTF8_DEC_6_V	0xfc

/* Masks to get the data bits from the first byte of an UTF-8 encoded
   character.  This array can be indexed with the number of bytes in
   the encoding. */
static unsigned char utf8_hidata_masks[7] =
    {
        0x00, 				/* unused */
        0x7f, 				/* 1 byte */
        0x1f, 				/* 2 bytes */
        0x0f, 				/* 3 bytes */
        0x07, 				/* 4 bytes */
        0x03, 				/* 5 bytes */
        0x01, 				/* 6 bytes */
    };

/* The mask and the value of the continuation bytes. */
#define WS_UTF8_DEC_C_M	0xc0
#define WS_UTF8_DEC_C_V 0x80

/* Determine how many bytes the UTF-8 encoding uses by investigating
   the first byte `b'.  The argument `b' must be given as `unsigned
   char'.  The macro returns 0 if the byte `b' is not a valid UTF-8
   first byte. */
#define WS_UTF8_DEC_TYPE(b)					\
    (((b) & WS_UTF8_DEC_1_M) == WS_UTF8_DEC_1_V			\
     ? 1							\
     : (((b) & WS_UTF8_DEC_2_M) == WS_UTF8_DEC_2_V		\
       ? 2							\
       : (((b) & WS_UTF8_DEC_3_M) == WS_UTF8_DEC_3_V		\
         ? 3							\
         : (((b) & WS_UTF8_DEC_4_M) == WS_UTF8_DEC_4_V		\
           ? 4							\
           : (((b) & WS_UTF8_DEC_5_M) == WS_UTF8_DEC_5_V	\
             ? 5						\
             : (((b) & WS_UTF8_DEC_6_M) == WS_UTF8_DEC_6_V	\
               ? 6						\
               : 0))))))

/* Predicate to check whether the `unsigned char' byte `b' is a
   continuation byte. */
#define WS_UTF8_DEC_C_P(b) (((b) & WS_UTF8_DEC_C_M) == WS_UTF8_DEC_C_V)

/********************* Global functions *********************************/

WsUtf8String *ws_utf8_alloc()
{
    return ws_calloc(1, sizeof(WsUtf8String));
}


void ws_utf8_free(WsUtf8String *string)
{
    if (string == NULL)
        return;

    ws_free(string->data);
    ws_free(string);
}


int ws_utf8_append_char(WsUtf8String *string, unsigned long ch)
{
    unsigned char *d;
    unsigned int num_bytes = WS_UTF8_ENC_TYPE(ch);
    unsigned int len, i;

    if (num_bytes == 0)
        ws_fatal("ws_utf8_append_char(): 0x%lx is not a valid UTF-8 character",
                 ch);

    d = ws_realloc(string->data, string->len + num_bytes);
    if (d == NULL)
        return 0;

    len = string->len;

    /* Encode the continuation bytes (n > 1). */
    for (i = num_bytes - 1; i > 0; i--) {
        d[len + i] = WS_UTF8_ENC_C_BITS;
        d[len + i] |= ch & WS_UTF8_CONT_DATA_MASK;
        ch >>= 6;
    }

    /* And continue the first byte. */
    d[len] = utf8_hibits[num_bytes];
    d[len] |= ch;

    string->data = d;
    string->len += num_bytes;
    string->num_chars++;

    return 1;
}


int ws_utf8_verify(const unsigned char *data, size_t len,
                   size_t *strlen_return)
{
    unsigned int num_bytes, i;
    size_t strlen = 0;

    while (len > 0) {
        num_bytes = WS_UTF8_DEC_TYPE(*data);
        if (num_bytes == 0)
            /* Not a valid beginning. */
            return 0;

        if (len < num_bytes)
            /* The data is truncated. */
            return 0;

        for (i = 1; i < num_bytes; i++)
            if (!WS_UTF8_DEC_C_P(data[i]))
                /* Not a valid continuation byte. */
                return 0;

        len -= num_bytes;
        data += num_bytes;
        strlen++;
    }

    if (strlen_return)
        *strlen_return = strlen;

    return 1;
}


int ws_utf8_set_data(WsUtf8String *string, const unsigned char *data,
                     size_t len)
{
    size_t num_chars;

    if (!ws_utf8_verify(data, len, &num_chars))
        /* Malformed data. */
        return 0;

    /* Init `string' to empty. */
    ws_free(string->data);
    string->data = NULL;
    string->len = 0;
    string->num_chars = 0;

    /* Set the new data. */
    string->data = ws_memdup(data, len);
    if (string->data == NULL)
        return 0;

    string->len = len;
    string->num_chars = num_chars;

    return 1;
}


int ws_utf8_get_char(const WsUtf8String *string, unsigned long *ch_return,
                     size_t *posp)
{
    size_t pos = *posp;
    unsigned int num_bytes, i;
    unsigned char *data;
    unsigned long ch;

    if (pos < 0 || pos >= string->len)
        /* Index out range. */
        return 0;

    data = string->data + pos;

    num_bytes = WS_UTF8_DEC_TYPE(*data);
    if (num_bytes == 0)
        /* Invalid position. */
        return 0;

    if (pos + num_bytes > string->len)
        /* Truncated data. */
        return 0;

    /* Get the first byte. */
    ch = data[0] & utf8_hidata_masks[num_bytes];

    /* Add the continuation bytes. */
    for (i = 1; i < num_bytes; i++) {
        ch <<= 6;
        ch |= data[i] & WS_UTF8_CONT_DATA_MASK;
    }

    *ch_return = ch;
    *posp = pos + num_bytes;

    return 1;
}


unsigned char *ws_utf8_to_latin1(const WsUtf8String *string,
                                 unsigned char unknown_char,
                                 size_t *len_return)
{
    unsigned char *cstr;
    size_t i;
    size_t pos = 0;

    if (string == NULL)
        return NULL;

    cstr = ws_malloc(string->num_chars + 1);
    if (cstr == NULL)
        return NULL;

    for (i = 0; i < string->num_chars; i++) {
        unsigned long ch;

        if (!ws_utf8_get_char(string, &ch, &pos))
            ws_fatal("ws_utf8_to_latin1_cstr(): internal inconsistency");

        if (ch > 0xff)
            cstr[i] = unknown_char;
        else
            cstr[i] = (unsigned char) ch;
    }

    cstr[i] = '\0';

    if (len_return)
        *len_return = string->num_chars;

    return cstr;
}


void ws_utf8_free_data(unsigned char *data)
{
    if (data)
        ws_free(data);
}
