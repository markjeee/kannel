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
 * wsbc.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Byte-code handling functions.
 *
 */

#include "wsint.h"
#include "wsbc.h"

/********************* Prototypes for static functions ******************/

/* Add a new pragma of type `type' to the byte-code `bc'.  The
 * function returns a pointer to an internal pragma structure that
 * must not be freed by the caller.  It is freed when the byte-code
 * `bc' is freed.  The function returns NULL if the pragma structure
 * could not be allocated. */
static WsBcPragma *add_pragma(WsBc *bc, WsBcPragmaType type);

/********************* Manipulating byte-code structure *****************/

WsBc *ws_bc_alloc(WsBcStringEncoding string_encoding)
{
    WsBc *bc = ws_calloc(1, sizeof(WsBc));

    if (bc == NULL)
        return NULL;

    bc->string_encoding = string_encoding;

    return bc;
}


void ws_bc_free(WsBc *bc)
{
    WsUInt16 i;
    WsUInt8 j;

    if (bc == NULL)
        return;

    /* Free constants. */
    for (i = 0; i < bc->num_constants; i++) {
        WsBcConstant *c = &bc->constants[i];

        if (c->type == WS_BC_CONST_TYPE_UTF8_STRING)
            ws_free(c->u.v_string.data);
    }
    ws_free(bc->constants);

    /* Free pragmas. */
    ws_free(bc->pragmas);

    /* Free function names. */
    for (j = 0; j < bc->num_function_names; j++)
        ws_free(bc->function_names[j].name);
    ws_free(bc->function_names);

    /* Free functions. */
    for (j = 0; j < bc->num_functions; j++)
        ws_free(bc->functions[j].code);
    ws_free(bc->functions);

    /* Free the byte-code structure. */
    ws_free(bc);
}


WsBool ws_bc_encode(WsBc *bc, unsigned char **data_return,
                    size_t *data_len_return)
{
    WsBuffer buffer;
    WsUInt32 ui;
    unsigned char data[64];
    unsigned char *p, *mb;
    size_t len;

    ws_buffer_init(&buffer);

    /* Append space for the header.  We do not know yet the size of the
       resulting byte-code. */
    if (!ws_buffer_append_space(&buffer, NULL, WS_BC_MAX_HEADER_LEN))
        goto error;


    /* Constants. */

    if (!ws_encode_buffer(&buffer,
                          WS_ENC_MB_UINT16, bc->num_constants,
                          WS_ENC_MB_UINT16, (WsUInt16) bc->string_encoding,
                          WS_ENC_END))
        goto error;

    for (ui = 0 ; ui < bc->num_constants; ui++) {
        switch (bc->constants[ui].type) {
        case WS_BC_CONST_TYPE_INT:
            if (WS_INT8_MIN <= bc->constants[ui].u.v_int
                && bc->constants[ui].u.v_int <= WS_INT8_MAX) {
                if (!ws_encode_buffer(&buffer,
                                      WS_ENC_UINT8, (WsUInt8) WS_BC_CONST_INT8,
                                      WS_ENC_INT8,
                                      (WsInt8) bc->constants[ui].u.v_int,
                                      WS_ENC_END))
                    goto error;
            } else if (WS_INT16_MIN <= bc->constants[ui].u.v_int
                       && bc->constants[ui].u.v_int <= WS_INT16_MAX) {
                if (!ws_encode_buffer(&buffer,
                                      WS_ENC_UINT8, (WsUInt8) WS_BC_CONST_INT16,
                                      WS_ENC_INT16,
                                      (WsInt16) bc->constants[ui].u.v_int,
                                      WS_ENC_END))
                    goto error;
            } else {
                if (!ws_encode_buffer(&buffer,
                                      WS_ENC_UINT8, (WsUInt8) WS_BC_CONST_INT32,
                                      WS_ENC_INT32, bc->constants[ui].u.v_int,
                                      WS_ENC_END))
                    goto error;
            }
            break;

        case WS_BC_CONST_TYPE_FLOAT32:
        case WS_BC_CONST_TYPE_FLOAT32_NAN:
        case WS_BC_CONST_TYPE_FLOAT32_POSITIVE_INF:
        case WS_BC_CONST_TYPE_FLOAT32_NEGATIVE_INF:
            switch (bc->constants[ui].type) {
            case WS_BC_CONST_TYPE_FLOAT32:
                ws_ieee754_encode_single(bc->constants[ui].u.v_float, data);
                p = data;
                break;

            case WS_BC_CONST_TYPE_FLOAT32_NAN:
                p = ws_ieee754_nan;
                break;

            case WS_BC_CONST_TYPE_FLOAT32_POSITIVE_INF:
                p = ws_ieee754_positive_inf;
                break;

            case WS_BC_CONST_TYPE_FLOAT32_NEGATIVE_INF:
                p = ws_ieee754_negative_inf;
                break;

            default:
                ws_fatal("ws_bc_encode(): internal inconsistency");
                /* NOTREACHED */
                p = NULL; 		/* Initialized to keep compiler quiet. */
                break;
            }

            if (!ws_encode_buffer(&buffer,
                                  WS_ENC_UINT8, (WsUInt8) WS_BC_CONST_FLOAT32,
                                  WS_ENC_DATA, p, 4,
                                  WS_ENC_END))
                goto error;
            break;

            break;

        case WS_BC_CONST_TYPE_UTF8_STRING:
            /* Encode the strings as requested. */
            switch (bc->string_encoding) {
            case WS_BC_STRING_ENC_ISO_8859_1:
                {
                    WsUtf8String *string = ws_utf8_alloc();
                    unsigned char *latin1;
                    size_t latin1_len;
                    WsBool success;

                    if (string == NULL)
                        goto error;

                    /* Create an UTF-8 string. */
                    if (!ws_utf8_set_data(string,
                                          bc->constants[ui].u.v_string.data,
                                          bc->constants[ui].u.v_string.len)) {
                        ws_utf8_free(string);
                        goto error;
                    }

                    /* Convert it to latin1. */
                    latin1 = ws_utf8_to_latin1(string, '?', &latin1_len);

                    /* We'r done with the UTF-8 string. */
                    ws_utf8_free(string);

                    if (latin1 == NULL)
                        goto error;

                    /* Encode it. */
                    success = ws_encode_buffer(
                                  &buffer,
                                  WS_ENC_UINT8,
                                  (WsUInt8) WS_BC_CONST_EXT_ENC_STRING,

                                  WS_ENC_MB_UINT32, (WsUInt32) latin1_len,
                                  WS_ENC_DATA, latin1, latin1_len,

                                  WS_ENC_END);
                    ws_utf8_free_data(latin1);

                    if (!success)
                        goto error;
                }
                break;

            case WS_BC_STRING_ENC_UTF8:
                if (!ws_encode_buffer(
                        &buffer,
                        WS_ENC_UINT8,
                        (WsUInt8) WS_BC_CONST_UTF8_STRING,

                        WS_ENC_MB_UINT32,
                        (WsUInt32) bc->constants[ui].u.v_string.len,

                        WS_ENC_DATA,
                        bc->constants[ui].u.v_string.data,
                        bc->constants[ui].u.v_string.len,

                        WS_ENC_END))
                    goto error;
                break;
            }
            break;

        case WS_BC_CONST_TYPE_EMPTY_STRING:
            if (!ws_encode_buffer(&buffer,
                                  WS_ENC_UINT8,
                                  (WsUInt8) WS_BC_CONST_EMPTY_STRING,
                                  WS_ENC_END))
                goto error;
            break;
        }
    }


    /* Pragmas. */

    if (!ws_encode_buffer(&buffer,
                          WS_ENC_MB_UINT16, bc->num_pragmas,
                          WS_ENC_END))
        goto error;

    for (ui = 0; ui < bc->num_pragmas; ui++) {
        switch (bc->pragmas[ui].type) {
        case WS_BC_PRAGMA_TYPE_ACCESS_DOMAIN:
            if (!ws_encode_buffer(&buffer,
                                  WS_ENC_UINT8,
                                  (WsUInt8) WS_BC_PRAGMA_ACCESS_DOMAIN,

                                  WS_ENC_MB_UINT16, bc->pragmas[ui].index_1,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_BC_PRAGMA_TYPE_ACCESS_PATH:
            if (!ws_encode_buffer(&buffer,
                                  WS_ENC_UINT8,
                                  (WsUInt8) WS_BC_PRAGMA_ACCESS_PATH,
                                  WS_ENC_MB_UINT16, bc->pragmas[ui].index_1,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY:
            if (!ws_encode_buffer(&buffer,
                                  WS_ENC_UINT8,
                                  (WsUInt8) WS_BC_PRAGMA_USER_AGENT_PROPERTY,
                                  WS_ENC_MB_UINT16, bc->pragmas[ui].index_1,
                                  WS_ENC_MB_UINT16, bc->pragmas[ui].index_2,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY_AND_SCHEME:
            if (!ws_encode_buffer(
                    &buffer,
                    WS_ENC_UINT8,
                    (WsUInt8) WS_BC_PRAGMA_USER_AGENT_PROPERTY_AND_SCHEME,
                    WS_ENC_MB_UINT16, bc->pragmas[ui].index_1,
                    WS_ENC_MB_UINT16, bc->pragmas[ui].index_2,
                    WS_ENC_MB_UINT16, bc->pragmas[ui].index_3,
                    WS_ENC_END))
                goto error;
            break;
        }
    }


    /* Function pool. */

    if (!ws_encode_buffer(&buffer,
                          WS_ENC_UINT8, bc->num_functions,
                          WS_ENC_END))
        goto error;

    /* Function names. */

    if (!ws_encode_buffer(&buffer,
                          WS_ENC_UINT8, bc->num_function_names,
                          WS_ENC_END))
        goto error;

    for (ui = 0; ui < bc->num_function_names; ui++) {
        size_t name_len = strlen(bc->function_names[ui].name);

        if (!ws_encode_buffer(&buffer,
                              WS_ENC_UINT8, bc->function_names[ui].index,
                              WS_ENC_UINT8, (WsUInt8) name_len,
                              WS_ENC_DATA, bc->function_names[ui].name, name_len,
                              WS_ENC_END))
            goto error;
    }

    /* Functions. */

    for (ui = 0; ui < bc->num_functions; ui++) {
        if (!ws_encode_buffer(&buffer,
                              WS_ENC_UINT8, bc->functions[ui].num_arguments,
                              WS_ENC_UINT8, bc->functions[ui].num_locals,
                              WS_ENC_MB_UINT32, bc->functions[ui].code_size,
                              WS_ENC_DATA, bc->functions[ui].code,
                              (size_t) bc->functions[ui].code_size,
                              WS_ENC_END))
            goto error;
    }


    /* Fix the byte-code header. */

    p = ws_buffer_ptr(&buffer);

    /* Encode the size of the byte-code excluding the byte-code header. */
    mb = ws_encode_mb_uint32(ws_buffer_len(&buffer) - WS_BC_MAX_HEADER_LEN,
                             data, &len);
    memcpy(p + WS_BC_MAX_HEADER_LEN - len, mb, len);

    /* Set the byte-code file version information. */
    WS_PUT_UINT8(p + WS_BC_MAX_HEADER_LEN - len - 1, WS_BC_VERSION);

    /* Calculate the beginning of the bc-array and its size. */
    *data_return = p + WS_BC_MAX_HEADER_LEN - len - 1;
    *data_len_return = ws_buffer_len(&buffer) - WS_BC_MAX_HEADER_LEN + len + 1;

    /* All done. */
    return WS_TRUE;


    /*
     * Error handling.
     */

error:

    ws_buffer_uninit(&buffer);
    *data_return = NULL;
    *data_len_return = 0;

    return WS_FALSE;
}


void ws_bc_data_free(unsigned char *data)
{
    size_t len = WS_MB_UINT32_MAX_ENCODED_LEN;

    if (data == NULL)
        return;

    /* Decode the mb-encoded length so we know how much space it uses. */
    (void) ws_decode_mb_uint32(data + 1, &len);

    /* Now we can compute the beginning of the array `data'. */
    ws_free(data - (WS_MB_UINT32_MAX_ENCODED_LEN - len));
}


/* A helper macro to update the data pointers during the decoding of
   byte-code data. */
#define WS_UPDATE_DATA	\
    data += decoded;	\
    data_len -= decoded

/* A helper macro to check the validity of the constant string index
   `idx'. */
#define WS_CHECK_STRING(idx)				\
    if ((idx) >= bc->num_constants			\
        || ((bc->constants[(idx)].type			\
             != WS_BC_CONST_TYPE_UTF8_STRING)		\
            && (bc->constants[(idx)].type		\
                != WS_BC_CONST_TYPE_EMPTY_STRING)))	\
        goto error;

WsBc *ws_bc_decode(const unsigned char *data, size_t data_len)
{
    WsBc *bc = ws_bc_alloc(WS_BC_STRING_ENC_ISO_8859_1);
    WsByte b;
    WsUInt32 ui32;
    WsUInt16 ui16, j;
    WsUInt16 ui16b;
    WsUInt8 ui8, num_functions, k, l;
    WsInt8 i8;
    WsInt16 i16;
    WsInt32 i32;
    WsIeee754Result ieee754;
    unsigned char *ucp;
    size_t decoded;

    /* Decode the byte-code header. */
    decoded = ws_decode_buffer(data, data_len,
                               WS_ENC_BYTE, &b,
                               WS_ENC_MB_UINT32, &ui32,
                               WS_ENC_END);

    if (!decoded
        || b != WS_BC_VERSION
        || ui32 != data_len - decoded)
        /* This is not a valid (or supported) byte-code header. */
        goto error;

    WS_UPDATE_DATA;

    /* Constant pool. */

    decoded = ws_decode_buffer(data, data_len,
                               WS_ENC_MB_UINT16, &ui16,
                               WS_ENC_MB_UINT16, &ui16b,
                               WS_ENC_END);
    if (!decoded)
        goto error;

    bc->string_encoding = ui16b;

    bc->constants = ws_calloc(ui16, sizeof(WsBcConstant));
    if (bc->constants == NULL)
        goto error;
    bc->num_constants = ui16;

    WS_UPDATE_DATA;

    for (j = 0; j < bc->num_constants; j++) {
        WsBcConstant *c = &bc->constants[j];

        decoded = ws_decode_buffer(data, data_len,
                                   WS_ENC_UINT8, &ui8,
                                   WS_ENC_END);
        if (decoded != 1)
            goto error;

        WS_UPDATE_DATA;

        switch (ui8) {
        case WS_BC_CONST_INT8:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_INT8, &i8,
                                       WS_ENC_END);
            if (decoded != 1)
                goto error;

            WS_UPDATE_DATA;

            c->type = WS_BC_CONST_TYPE_INT;
            c->u.v_int = i8;
            break;

        case WS_BC_CONST_INT16:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_INT16, &i16,
                                       WS_ENC_END);
            if (decoded != 2)
                goto error;

            WS_UPDATE_DATA;

            c->type = WS_BC_CONST_TYPE_INT;
            c->u.v_int = i16;
            break;

        case WS_BC_CONST_INT32:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_INT32, &i32,
                                       WS_ENC_END);
            if (decoded != 4)
                goto error;

            WS_UPDATE_DATA;

            c->type = WS_BC_CONST_TYPE_INT;
            c->u.v_int = i32;
            break;

        case WS_BC_CONST_FLOAT32:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_DATA, &ucp, (size_t) 4,
                                       WS_ENC_END);
            if (decoded != 4)
                goto error;

            WS_UPDATE_DATA;

            ieee754 = ws_ieee754_decode_single(ucp, &c->u.v_float);

            switch (ieee754) {
            case WS_IEEE754_OK:
                c->type = WS_BC_CONST_TYPE_FLOAT32;
                break;

            case WS_IEEE754_NAN:
                c->type = WS_BC_CONST_TYPE_FLOAT32_NAN;
                break;

            case WS_IEEE754_POSITIVE_INF:
                c->type = WS_BC_CONST_TYPE_FLOAT32_POSITIVE_INF;
                break;

            case WS_IEEE754_NEGATIVE_INF:
                c->type = WS_BC_CONST_TYPE_FLOAT32_NEGATIVE_INF;
                break;
            }

            break;

        case WS_BC_CONST_UTF8_STRING:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_MB_UINT32, &ui32,
                                       WS_ENC_END);
            if (decoded == 0)
                goto error;

            WS_UPDATE_DATA;

            c->type = WS_BC_CONST_TYPE_UTF8_STRING;
            c->u.v_string.len = ui32;

            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_DATA, &ucp, c->u.v_string.len,
                                       WS_ENC_END);
            if (decoded != ui32)
                goto error;

            WS_UPDATE_DATA;

            c->u.v_string.data = ws_memdup(ucp, ui32);
            if (c->u.v_string.data == NULL)
                goto error;

            /* Check the validity of the data. */
            if (!ws_utf8_verify(c->u.v_string.data, c->u.v_string.len,
                                &c->u.v_string.num_chars))
                goto error;
            break;

        case WS_BC_CONST_EMPTY_STRING:
            c->type = WS_BC_CONST_TYPE_EMPTY_STRING;
            break;

        case WS_BC_CONST_EXT_ENC_STRING:
            ws_fatal("external character encoding not implemented yet");
            break;

        default:
            /* Reserved. */
            goto error;
            break;
        }
    }

    /* Pragma pool. */

    decoded = ws_decode_buffer(data, data_len,
                               WS_ENC_MB_UINT16, &ui16,
                               WS_ENC_END);
    if (!decoded)
        goto error;

    bc->pragmas = ws_calloc(ui16, sizeof(WsBcPragma));
    if (bc->pragmas == NULL)
        goto error;
    bc->num_pragmas = ui16;

    WS_UPDATE_DATA;

    for (j = 0; j < bc->num_pragmas; j++) {
        WsBcPragma *p = &bc->pragmas[j];

        decoded = ws_decode_buffer(data, data_len,
                                   WS_ENC_UINT8, &ui8,
                                   WS_ENC_END);
        if (decoded != 1)
            goto error;

        WS_UPDATE_DATA;

        p->type = ui8;

        switch (ui8) {
        case WS_BC_PRAGMA_ACCESS_DOMAIN:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_MB_UINT16, &p->index_1,
                                       WS_ENC_END);
            if (!decoded)
                goto error;

            WS_CHECK_STRING(p->index_1);
            break;

        case WS_BC_PRAGMA_ACCESS_PATH:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_MB_UINT16, &p->index_1,
                                       WS_ENC_END);
            if (!decoded)
                goto error;

            WS_CHECK_STRING(p->index_1);
            break;

        case WS_BC_PRAGMA_USER_AGENT_PROPERTY:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_MB_UINT16, &p->index_1,
                                       WS_ENC_MB_UINT16, &p->index_2,
                                       WS_ENC_END);
            if (!decoded)
                goto error;

            WS_CHECK_STRING(p->index_1);
            WS_CHECK_STRING(p->index_2);
            break;

        case WS_BC_PRAGMA_USER_AGENT_PROPERTY_AND_SCHEME:
            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_MB_UINT16, &p->index_1,
                                       WS_ENC_MB_UINT16, &p->index_2,
                                       WS_ENC_MB_UINT16, &p->index_3,
                                       WS_ENC_END);
            if (!decoded)
                goto error;

            WS_CHECK_STRING(p->index_1);
            WS_CHECK_STRING(p->index_2);
            WS_CHECK_STRING(p->index_3);
            break;

        default:
            goto error;
            break;
        }

        WS_UPDATE_DATA;
    }

    /* Function pool. */

    decoded = ws_decode_buffer(data, data_len,
                               WS_ENC_UINT8, &num_functions,
                               WS_ENC_END);
    if (decoded != 1)
        goto error;

    WS_UPDATE_DATA;

    /* Function names. */

    decoded = ws_decode_buffer(data, data_len,
                               WS_ENC_UINT8, &ui8,
                               WS_ENC_END);
    if (decoded != 1)
        goto error;

    WS_UPDATE_DATA;

    if (ui8) {
        /* We have function names. */
        bc->function_names = ws_calloc(ui8, sizeof(WsBcFunctionName));
        if (bc->function_names == NULL)
            goto error;
        bc->num_function_names = ui8;

        for (k = 0; k < bc->num_function_names; k++) {
            WsBcFunctionName *n = &bc->function_names[k];

            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_UINT8, &n->index,
                                       WS_ENC_UINT8, &ui8,
                                       WS_ENC_END);
            if (decoded != 2)
                goto error;

            WS_UPDATE_DATA;

            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_DATA, &ucp, (size_t) ui8,
                                       WS_ENC_END);
            if (decoded != ui8)
                goto error;

            WS_UPDATE_DATA;

            n->name = ws_memdup(ucp, ui8);
            if (n->name == NULL)
                goto error;

            /* Check the validity of the name. */

            if (!ws_utf8_verify((unsigned char *) n->name, ui8, NULL))
                goto error;

            /* Just check that the data contains only valid characters. */
            for (l = 0; l < ui8; l++) {
                unsigned int ch = (unsigned char) n->name[l];

                if (('a' <= ch && ch <= 'z')
                    || ('A' <= ch && ch <= 'Z')
                    || ch == '_'
                    || (l > 0 && ('0' <= ch && ch <= '9')))
                    /* Ok. */
                    continue;

                /* Invalid character in the function name. */
                goto error;
            }

            /* Is the index valid? */
            if (n->index >= num_functions)
                goto error;
        }
    }

    /* Functions. */

    if (num_functions) {
        /* We have functions. */
        bc->functions = ws_calloc(num_functions, sizeof(WsBcFunction));
        if (bc->functions == NULL)
            goto error;
        bc->num_functions = num_functions;

        for (k = 0; k < bc->num_functions; k++) {
            WsBcFunction *f = &bc->functions[k];

            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_UINT8, &f->num_arguments,
                                       WS_ENC_UINT8, &f->num_locals,
                                       WS_ENC_MB_UINT32, &f->code_size,
                                       WS_ENC_END);
            if (!decoded)
                goto error;

            WS_UPDATE_DATA;

            decoded = ws_decode_buffer(data, data_len,
                                       WS_ENC_DATA, &ucp, f->code_size,
                                       WS_ENC_END);
            if (decoded != f->code_size)
                goto error;

            WS_UPDATE_DATA;

            if (f->code_size) {
                /* It is not an empty function. */
                f->code = ws_memdup(ucp, f->code_size);
                if (f->code == NULL)
                    goto error;
            }
        }
    }

    /* Did we process it all? */
    if (data_len != 0)
        goto error;

    /* All done. */
    return bc;

    /*
     * Error handling.
     */

error:

    ws_bc_free(bc);

    return NULL;
}

/********************* Adding constant elements *************************/

WsBool ws_bc_add_const_int(WsBc *bc, WsUInt16 *index_return, WsInt32 value)
{
    WsUInt16 i;
    WsBcConstant *nc;

    /* Do we already have a suitable integer constant? */
    for (i = 0; i < bc->num_constants; i++) {
        if (bc->constants[i].type == WS_BC_CONST_TYPE_INT
            && bc->constants[i].u.v_int == value) {
            *index_return = i;
            return WS_TRUE;
        }
    }

    /* Must add a new constant. */

    nc = ws_realloc(bc->constants,
                    (bc->num_constants + 1) * sizeof(WsBcConstant));
    if (nc == NULL)
        return WS_FALSE;

    bc->constants = nc;
    bc->constants[bc->num_constants].type = WS_BC_CONST_TYPE_INT;
    bc->constants[bc->num_constants].u.v_int = value;

    *index_return = bc->num_constants++;

    return WS_TRUE;
}


WsBool ws_bc_add_const_float(WsBc *bc, WsUInt16 *index_return, WsFloat value)
{
    WsUInt16 i;
    WsBcConstant *nc;

    /* Do we already have a suitable float32 constant? */
    for (i = 0; i < bc->num_constants; i++) {
        if (bc->constants[i].type == WS_BC_CONST_TYPE_FLOAT32
            && bc->constants[i].u.v_float == value) {
            *index_return = i;
            return WS_TRUE;
        }
    }

    /* Must add a new constant. */

    nc = ws_realloc(bc->constants,
                    (bc->num_constants + 1) * sizeof(WsBcConstant));
    if (nc == NULL)
        return WS_FALSE;

    bc->constants = nc;
    bc->constants[bc->num_constants].type = WS_BC_CONST_TYPE_FLOAT32;
    bc->constants[bc->num_constants].u.v_float = value;

    *index_return = bc->num_constants++;

    return WS_TRUE;
}


WsBool ws_bc_add_const_utf8_string(WsBc *bc, WsUInt16 *index_return,
                                   const unsigned char *data, size_t len)
{
    WsUInt16 i;
    WsBcConstant *nc;

    /* Do we already have a suitable UFT-8 constant? */
    for (i = 0; i < bc->num_constants; i++) {
        if (bc->constants[i].type == WS_BC_CONST_TYPE_UTF8_STRING
            && bc->constants[i].u.v_string.len == len
            && memcmp(bc->constants[i].u.v_string.data,
                      data, len) == 0) {
            *index_return = i;
            return WS_TRUE;
        }
    }

    /* Must add a new constant. */

    nc = ws_realloc(bc->constants,
                    (bc->num_constants + 1) * sizeof(WsBcConstant));
    if (nc == NULL)
        return WS_FALSE;

    bc->constants = nc;
    bc->constants[bc->num_constants].type = WS_BC_CONST_TYPE_UTF8_STRING;
    bc->constants[bc->num_constants].u.v_string.len = len;
    bc->constants[bc->num_constants].u.v_string.data
    = ws_memdup(data, len);
    if (bc->constants[bc->num_constants].u.v_string.data == NULL)
        return WS_FALSE;

    *index_return = bc->num_constants++;

    return WS_TRUE;
}



WsBool ws_bc_add_const_empty_string(WsBc *bc, WsUInt16 *index_return)
{
    WsUInt16 i;
    WsBcConstant *nc;

    /* Do we already have a suitable empty string constant? */
    for (i = 0; i < bc->num_constants; i++) {
        if (bc->constants[i].type == WS_BC_CONST_TYPE_EMPTY_STRING) {
            *index_return = i;
            return WS_TRUE;
        }
    }

    /* Must add a new constant. */

    nc = ws_realloc(bc->constants,
                    (bc->num_constants + 1) * sizeof(WsBcConstant));
    if (nc == NULL)
        return WS_FALSE;

    bc->constants = nc;
    bc->constants[bc->num_constants].type = WS_BC_CONST_TYPE_EMPTY_STRING;

    *index_return = bc->num_constants++;

    return WS_TRUE;
}

/********************* Adding pragmas ***********************************/

WsBool ws_bc_add_pragma_access_domain(WsBc *bc, const unsigned char *domain,
                                      size_t domain_len)
{
    WsBcPragma *p = add_pragma(bc, WS_BC_PRAGMA_TYPE_ACCESS_DOMAIN);

    if (p == NULL)
        return WS_FALSE;

    if (!ws_bc_add_const_utf8_string(bc, &p->index_1, domain, domain_len))
        return WS_FALSE;

    return WS_TRUE;
}


WsBool ws_bc_add_pragma_access_path(WsBc *bc, const unsigned char *path,
                                    size_t path_len)
{
    WsBcPragma *p = add_pragma(bc, WS_BC_PRAGMA_TYPE_ACCESS_PATH);

    if (p == NULL)
        return WS_FALSE;

    if (!ws_bc_add_const_utf8_string(bc, &p->index_1, path, path_len))
        return WS_FALSE;

    return WS_TRUE;
}


WsBool ws_bc_add_pragma_user_agent_property(WsBc *bc,
                                            const unsigned char *name,
                                            size_t name_len,
                                            const unsigned char *property,
                                            size_t property_len)
{
    WsBcPragma *p = add_pragma(bc, WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY);

    if (p == NULL)
        return WS_FALSE;

    if (!ws_bc_add_const_utf8_string(bc, &p->index_1, name, name_len)
        || !ws_bc_add_const_utf8_string(bc, &p->index_2, property, property_len))
        return WS_FALSE;

    return WS_TRUE;
}


WsBool ws_bc_add_pragma_user_agent_property_and_scheme(
    WsBc *bc,
    const unsigned char *name,
    size_t name_len,
    const unsigned char *property,
    size_t property_len,
    const unsigned char *scheme,
    size_t scheme_len)
{
    WsBcPragma *p;

    p = add_pragma(bc, WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY_AND_SCHEME);

    if (p == NULL)
        return WS_FALSE;

    if (!ws_bc_add_const_utf8_string(bc, &p->index_1, name, name_len)
        || !ws_bc_add_const_utf8_string(bc, &p->index_2, property, property_len)
        || !ws_bc_add_const_utf8_string(bc, &p->index_3, scheme, scheme_len))
        return WS_FALSE;

    return WS_TRUE;
}

/********************* Adding functions *********************************/

WsBool ws_bc_add_function(WsBc *bc, WsUInt8 *index_return, char *name,
                          WsUInt8 num_arguments, WsUInt8 num_locals,
                          WsUInt32 code_size, unsigned char *code)
{
    WsBcFunction *nf;

    /* First, add the function to the function pool. */

    nf = ws_realloc(bc->functions,
                    (bc->num_functions + 1) * sizeof(WsBcFunction));
    if (nf == NULL)
        return WS_FALSE;

    bc->functions = nf;
    bc->functions[bc->num_functions].num_arguments = num_arguments;
    bc->functions[bc->num_functions].num_locals = num_locals;
    bc->functions[bc->num_functions].code_size = code_size;
    bc->functions[bc->num_functions].code = ws_memdup(code, code_size);

    if (bc->functions[bc->num_functions].code == NULL)
        return WS_FALSE;

    /* Save the index of the function. */
    *index_return = bc->num_functions++;

    /* For external functions (which have name), add a name entry to the
       function name pool. */
    if (name) {
        WsBcFunctionName *nfn;

        nfn = ws_realloc(bc->function_names,
                         ((bc->num_function_names + 1)
                          * sizeof(WsBcFunctionName)));
        if (nfn == NULL)
            return WS_FALSE;

        bc->function_names = nfn;
        bc->function_names[bc->num_function_names].index = *index_return;
        bc->function_names[bc->num_function_names].name = ws_strdup(name);

        if (bc->function_names[bc->num_function_names].name == NULL)
            return WS_FALSE;

        bc->num_function_names++;
    }

    /* All done. */
    return WS_TRUE;
}

/********************* Static functions *********************************/

static WsBcPragma *add_pragma(WsBc *bc, WsBcPragmaType type)
{
    WsBcPragma *np;

    /* Add a new pragma slot. */
    np = ws_realloc(bc->pragmas, (bc->num_pragmas + 1) * sizeof(WsBcPragma));
    if (np == NULL)
        return NULL;

    bc->pragmas = np;
    bc->pragmas[bc->num_pragmas].type = type;

    return &bc->pragmas[bc->num_pragmas++];
}
