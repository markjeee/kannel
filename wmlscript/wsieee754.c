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

#include "wsint.h"

/********************* Types and definitions ****************************/

#define WS_IEEE754_SINGLE_EXP_SIZE	8
#define WS_IEEE754_SINGLE_MANT_SIZE	23
#define WS_IEEE754_SINGLE_BIAS		127

#define WS_IEEE754_SINGLE_EXP_MIN	-126
#define WS_IEEE754_SINGLE_EXP_MAX	127

#define WS_IEEE754_POSITIVE_INFINITY	0x7f800000

/********************* Special values ***********************************/

unsigned char ws_ieee754_nan[4] = {0xff, 0xff, 0xff, 0xff};

unsigned char ws_ieee754_positive_inf[4] = {0x7f, 0x80, 0x00, 0x00};

unsigned char ws_ieee754_negative_inf[4] = {0xff, 0x80, 0x00, 0x00};

/********************* Global functions *********************************/

WsIeee754Result ws_ieee754_encode_single(double value, unsigned char *buf)
{
    int sign = 0;
    WsInt32 exp = 0;
    WsUInt32 mant = 0;
    int i;
    WsIeee754Result result = WS_IEEE754_OK;

    /* The sign bit. */
    if (value < 0.0) {
        sign = 1;
        value = -value;
    }

    /* Scale the value so that: 1 <= mantissa < 2. */
    if (value > 1.0) {
        /* The exponent is positive. */
        while (value >= 2.0 && exp <= WS_IEEE754_SINGLE_EXP_MAX) {
            value /= 2.0;
            exp++;
        }
        if (exp > WS_IEEE754_SINGLE_EXP_MAX) {
            /* Overflow => infinity. */
            exp = 0xff;

            if (sign)
                result = WS_IEEE754_NEGATIVE_INF;
            else
                result = WS_IEEE754_POSITIVE_INF;

            goto done;
        }

        /* The 1 is implicit. */
        value -= 1;
    } else {
        /* The exponent is negative. */
        while (value < 1.0 && exp > WS_IEEE754_SINGLE_EXP_MIN) {
            value *= 2.0;
            exp--;
        }
        if (value >= 1.0) {
            /* We managed to get the number to the normal form.  Let's
                      remote the implicit 1 from the value. */
            gw_assert(value >= 1.0);
            value -= 1.0;
        } else {
            /* The number is still smaller than 1.  We just try to
                      present the remaining stuff in our mantissa.  If that
                      fails, we fall back to 0.0.  We mark exp to -127 (after
                      bias it is 0) to mark this unnormalized form. */
            exp--;
            gw_assert(exp == -127);
        }
    }

    for (i = 0; i < WS_IEEE754_SINGLE_MANT_SIZE; i++) {
        value *= 2.0;
        mant <<= 1;

        if (value >= 1.0) {
            mant |= 1;
            value -= 1.0;
        }
    }

    /* Handle rounding.  Intel seems to round 0.5 down so to be
       compatible, our check is > instead of >=. */
    if (value * 2.0 > 1.0) {
        mant++;
        if (mant == 0x800000) {
            /* This we the really worst case.  The rounding rounds the
               mant up to 2.0.  So we must increase the exponent by one.
               This may then result an overflow in the exponent which
               converts our number to infinity. */
            mant = 0;
            exp++;

            if (exp > WS_IEEE754_SINGLE_EXP_MAX) {
                /* Overflow => infinity. */
                exp = 0xff;
                goto done;
            }
        }
    }

    /* Handle biased exponent. */
    exp += WS_IEEE754_SINGLE_BIAS;

done:

    /* Encode the value to the buffer. */

    mant |= exp << 23;
    mant |= sign << 31;

    buf[3] = (mant & 0x000000ff);
    buf[2] = (mant & 0x0000ff00) >> 8;
    buf[1] = (mant & 0x00ff0000) >> 16;
    buf[0] = (mant & 0xff000000) >> 24;

    return result;
}


WsIeee754Result ws_ieee754_decode_single(unsigned char *buf,
                                         double *value_return)
{
    WsUInt32 sign = ws_ieee754_single_get_sign(buf);
    WsInt32 exp = (WsInt32) ws_ieee754_single_get_exp(buf);
    WsUInt32 mant = ws_ieee754_single_get_mant(buf);
    double value;
    int i;

    /* Check the special cases where exponent is all 1. */
    if (exp == 0xff) {
        if (mant == 0)
            return sign ? WS_IEEE754_NEGATIVE_INF : WS_IEEE754_POSITIVE_INF;

        return WS_IEEE754_NAN;
    }

    /* Expand the mantissa. */
    value = 0.0;
    for (i = 0; i < WS_IEEE754_SINGLE_MANT_SIZE; i++) {
        if (mant & 0x1)
            value += 1.0;

        value /= 2.0;
        mant >>= 1;
    }

    /* Check the `unnormalized' vs. `normal form'. */
    if (exp == 0)
        /* This is a `unnormalized' number. */
        exp = -126;
    else {
        /* This is a standard case. */
        value += 1.0;
        exp -= WS_IEEE754_SINGLE_BIAS;
    }

    /* Handle exponents. */
    while (exp > 0) {
        value *= 2;
        exp--;
    }
    while (exp < 0) {
        value /= 2;
        exp++;
    }

    /* Finally notify sign. */
    if (sign)
        value = -value;

    *value_return = value;

    return WS_IEEE754_OK;
}


WsUInt32 ws_ieee754_single_get_sign(unsigned char *buf)
{
    return (buf[0] & 0x80) >> 7;
}


WsUInt32 ws_ieee754_single_get_exp(unsigned char *buf)
{
    WsUInt32 value = buf[0] & 0x7f;

    value <<= 1;
    value |= (buf[1] & 0x80) >> 7;

    return value;
}


WsUInt32 ws_ieee754_single_get_mant(unsigned char *buf)
{
    WsUInt32 value = buf[1] & 0x7f;

    value <<= 8;
    value |= buf[2];

    value <<= 8;
    value |= buf[3];

    return value;
}

#if 0
/********************* Tests for IEEE754 functions **********************/

void ws_ieee754_print(unsigned char *buf)
{
    int i, j;

    for (i = 0; i < 4; i++) {
        unsigned char mask = 0x80;
        unsigned char ch = buf[i];

        for (j = 0; j < 8; j++) {
            if (ch & mask)
                printf("1");
            else
                printf("0");

            if ((i == 0 && j == 0)
                || (i == 1 && j == 0))
                printf(" ");

            mask >>= 1;
        }
    }
    printf("\n");
}

#include <math.h>
#include <stdlib.h>
#include <machine/ieee.h>

void check_value(double num)
{
    float native = num;
    unsigned char buf[4];
    struct ieee_single *s = (struct ieee_single *) & native;
    unsigned int *uip = (unsigned int *) s;
    unsigned int n = ntohl(*uip);
    double d;

    ws_ieee754_encode_single(num, buf);
    if (memcmp(buf, &n, 4) != 0) {
        printf("\n");
        printf("%f failed:\n", num);
        printf("ws:     ");
        ws_ieee754_print(buf);
        printf("native: ");
        ws_ieee754_print((unsigned char *) &n);
        abort();
    }

    if (ws_ieee754_decode_single(buf, &d) != WS_IEEE754_OK
        || d != native) {
        printf("\ndecode of %f failed: got %f\n", num, d);
        abort();
    }
}


int main(int argc, char *argv[])
{
    unsigned char buf[4];
    unsigned int rounds = 0;

    if (argc > 1) {
        int i;

        for (i = 1; i < argc; i++)
            check_value(strtod(argv[1], NULL));

        return 0;
    }

    ws_ieee754_encode_single(5.75, buf);
    ws_ieee754_print(buf);
    check_value(5.75);

    ws_ieee754_encode_single(340282346638528859811704183484516925440.0, buf);
    ws_ieee754_print(buf);
    check_value(340282346638528859811704183484516925440.0);

    ws_ieee754_encode_single( -340282346638528859811704183484516925440.0, buf);
    ws_ieee754_print(buf);
    check_value( -340282346638528859811704183484516925440.0);

    ws_ieee754_encode_single(3.0 * pow(2, -129), buf);
    ws_ieee754_print(buf);
    check_value(3.0 * pow(2, -129));

    ws_ieee754_encode_single(pow(2, -149), buf);
    ws_ieee754_print(buf);
    check_value(pow(2, -149));

    ws_ieee754_encode_single(pow(2, -149) * .1, buf);
    ws_ieee754_print(buf);
    check_value(pow(2, -149) * .1);

    ws_ieee754_encode_single( -pow(2, -149), buf);
    ws_ieee754_print(buf);
    check_value( -pow(2, -149));

    ws_ieee754_encode_single( -pow(2, -149) * .1, buf);
    ws_ieee754_print(buf);

    while (1) {
        double a = random();
        double b = random();

        if (b == 0.0)
            continue;

        check_value(a / b);
        check_value(a * b);

        if ((++rounds % 100000) == 0) {
            printf("%d ", rounds);
            fflush(stdout);
        }

    }

    return 0;
}
#endif
