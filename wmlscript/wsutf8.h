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
 * wsutf8.h
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

#ifndef WSUTF8_H
#define WSUTF8_H

/********************* Types and defintions *****************************/

/* UTF-8 string handle. */
struct WsUtf8StringRec
{
    /* The length of the UTF-8 encoded `data'. */
    size_t len;

    /* The UTF-8 encoded data. */
    unsigned char *data;

    /* The number of characters in the string. */
    size_t num_chars;
};

typedef struct WsUtf8StringRec WsUtf8String;

/********************* Global functions *********************************/

/* Allocate an empty UTF-8 string.  The function returns NULL if the
   allocation failed (out of memory). */
WsUtf8String *ws_utf8_alloc(void);

/* Free an UTF-8 encoded string. */
void ws_utf8_free(WsUtf8String *string);

/* Append the character `ch' to the string `string'.  The function
   returns 1 if the operation was successful or 0 otherwise (out of
   memory). */
int ws_utf8_append_char(WsUtf8String *string, unsigned long ch);

/* Verify the UTF-8 encoded string `data' containing `len' bytes of
   data.  The function returns 1 if the `data' is correctly encoded
   and 0 otherwise.  If the argument `strlen_return' is not NULL, it
   is set to the number of characters in the string. */
int ws_utf8_verify(const unsigned char *data, size_t len,
                   size_t *strlen_return);

/* Set UTF-8 encoded data `data', `len' to the string `string'.  The
   function returns 1 if the data was UTF-8 encoded and 0 otherwise
   (malformed data or out of memory).  The function frees the possible
   old data from `string'. */
int ws_utf8_set_data(WsUtf8String *string, const unsigned char *data,
                     size_t len);

/* Get a character from the UTF-8 string `string'.  The argument
   `posp' gives the index of the character in the UTF-8 encoded data.
   It is not the sequence number of the character.  It is its starting
   position within the UTF-8 encoded data.  The argument `posp' is
   updated to point to the beginning of the next character within the
   data.  The character is returned in `ch_return'.  The function
   returns 1 if the operation was successful or 0 otherwise (index
   `posp' was invalid or there were no more characters in the
   string). */
int ws_utf8_get_char(const WsUtf8String *string, unsigned long *ch_return,
                     size_t *posp);

/* Convert the UTF-8 encoded string `string' to null-terminated ISO
   8859/1 (ISO latin1) string.  Those characters of `string' which can
   not be presented in latin1 are replaced with the character
   `unknown_char'.  If the argument `len_return' is not NULL, it is
   set to contain the length of the returned string (excluding the
   trailing null-character).  The function returns a pointer to the
   string or NULL if the operation failed (out of memory).  The
   returned string must be freed with the ws_utf8_free_data()
   function. */
unsigned char *ws_utf8_to_latin1(const WsUtf8String *string,
                                 unsigned char unknown_char,
                                 size_t *len_return);

/* Free a string, returned by the ws_utf8_to_latin1_cstr()
   function. */
void ws_utf8_free_data(unsigned char *data);

#endif /* not WSUTF8_H */
