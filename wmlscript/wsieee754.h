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
 * wsieee754.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Functions to manipulate ANSI/IEEE Std 754-1985 binary floating-point
 * numbers.
 *
 */

#ifndef WSIEEE754_H
#define WSIEEE754_H

/********************* Types and definitions ****************************/

/* Return codes for encoding and decoding functions. */
typedef enum
{
    /* The operation was successful. */
    WS_IEEE754_OK,

    /* The value is `Not a Number' NaN. */
    WS_IEEE754_NAN,

    /* The valueis positive infinity. */
    WS_IEEE754_POSITIVE_INF,

    /* The value is negative infinity. */
    WS_IEEE754_NEGATIVE_INF
} WsIeee754Result;

/********************* Special values ***********************************/

/* `Not a Number' NaN */
extern unsigned char ws_ieee754_nan[4];

/* Positive infinity. */
extern unsigned char ws_ieee754_positive_inf[4];

/* Positive infinity. */
extern unsigned char ws_ieee754_negative_inf[4];

/********************* Global functions *********************************/

/* Encode the floating point number `value' to the IEEE-754 single
   precision format.  The function stores the encoded value to the
   buffer `buf'.  The buffer `buf' must have 32 bits (4 bytes) of
   space.  The function returns WsIeee754Result return value.  It
   describes the format of the encoded value in `buf'.  In all cases,
   the function generates the corresponding encoded value to the
   buffer `buf'. */
WsIeee754Result ws_ieee754_encode_single(double value, unsigned char *buf);

/* Decode the IEEE-754 encoded number `buf' into a floating point
   number.  The argument `buf' must have 32 bits of data.  The
   function returns a result code which describes the success of the
   decode operation.  If the result is WS_IEEE754_OK, the resulting
   floating point number is returned in `value_return'. */
WsIeee754Result ws_ieee754_decode_single(unsigned char *buf,
        double *value_return);

/* Get the sign bit from the IEEE-754 single format encoded number
   `buf'.  The buffer `buf' must have 32 bits of data. */
WsUInt32 ws_ieee754_single_get_sign(unsigned char *buf);

/* Get the exponent from the IEEE-754 single format encoded number
   `buf'.  The buffer `buf' must have 32 bits of data.  The returned
   value is the biased exponent. */
WsUInt32 ws_ieee754_single_get_exp(unsigned char *buf);

/* Get the mantissa from the IEEE-754 single format encoded number
   `buf'.  The buffer `buf' must have 32 bits of data. */
WsUInt32 ws_ieee754_single_get_mant(unsigned char *buf);

#endif /* not WSIEEE754_H */
