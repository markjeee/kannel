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
 * wsbc.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Byte-code handling.
 *
 */

#ifndef WSBC_H
#define WSBC_H

#include "wsint.h"

/********************* Types and defintions *****************************/

/* The byte-code version numbers. */
#define WS_BC_VERSION_MAJOR	1
#define WS_BC_VERSION_MINOR	1
#define WS_BC_VERSION (((WS_BC_VERSION_MAJOR - 1) << 4) | WS_BC_VERSION_MINOR)

/* The maximum length of the byte-code header: the multi-byte encoded
   length + one byte for the version information. */
#define WS_BC_MAX_HEADER_LEN	(WS_MB_UINT32_MAX_ENCODED_LEN + 1)

/* The string encoding, used in the byte-code data.  These are the
   MIBEnum values, assigned by IANA.  For a complete description of
   the character sets and values, please see the document
   `rfc/iana/assignments/character-sets'. */
typedef enum
{
    WS_BC_STRING_ENC_ISO_8859_1	= 4,
    WS_BC_STRING_ENC_UTF8	= 106
} WsBcStringEncoding;

/* Constant types in the BC constants pool. */
#define WS_BC_CONST_INT8		0
#define WS_BC_CONST_INT16		1
#define WS_BC_CONST_INT32		2
#define WS_BC_CONST_FLOAT32		3
#define WS_BC_CONST_UTF8_STRING		4
#define WS_BC_CONST_EMPTY_STRING	5
#define WS_BC_CONST_EXT_ENC_STRING	6
#define WS_BC_CONST_FIRST_RESERVED	7

/* An in-memory byte-code constant. */

typedef enum
{
    WS_BC_CONST_TYPE_INT,
    WS_BC_CONST_TYPE_FLOAT32,
    WS_BC_CONST_TYPE_FLOAT32_NAN,
    WS_BC_CONST_TYPE_FLOAT32_POSITIVE_INF,
    WS_BC_CONST_TYPE_FLOAT32_NEGATIVE_INF,
    WS_BC_CONST_TYPE_UTF8_STRING,
    WS_BC_CONST_TYPE_EMPTY_STRING
} WsBcConstantType;

struct WsBcConstantRec
{
    WsBcConstantType type;

    union
    {
        WsInt32 v_int;
        WsFloat v_float;
        WsUtf8String v_string;
    } u;
};

typedef struct WsBcConstantRec WsBcConstant;

/* Pragma types in the BC pragma pool. */
#define WS_BC_PRAGMA_ACCESS_DOMAIN			0
#define WS_BC_PRAGMA_ACCESS_PATH			1
#define WS_BC_PRAGMA_USER_AGENT_PROPERTY		2
#define WS_BC_PRAGMA_USER_AGENT_PROPERTY_AND_SCHEME	3
#define WS_BC_PRAGMA_FIRST_RESERVED			4

/* An in-memory byte-code pragma. */

typedef enum
{
    WS_BC_PRAGMA_TYPE_ACCESS_DOMAIN,
    WS_BC_PRAGMA_TYPE_ACCESS_PATH,
    WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY,
    WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY_AND_SCHEME
} WsBcPragmaType;

struct WsBcPragmaRec
{
    WsBcPragmaType type;

    WsUInt16 index_1;
    WsUInt16 index_2;
    WsUInt16 index_3;
};

typedef struct WsBcPragmaRec WsBcPragma;

/* An in-memory byte-code function name. */
struct WsBcFunctionNameRec
{
    /* Index to the function pool. */
    WsUInt8 index;

    /* The name of the function as a 7 bit ASCII.  This is as-is in the
       UTF-8 format. */
    char *name;
};

typedef struct WsBcFunctionNameRec WsBcFunctionName;

/* An in-memory byte-code function. */
struct WsBcFunctionRec
{
    WsUInt8 num_arguments;
    WsUInt8 num_locals;
    WsUInt32 code_size;
    unsigned char *code;
};

typedef struct WsBcFunctionRec WsBcFunction;

/* An in-memory byte-code file. */
struct WsBcRec
{
    /* How the strings are encoded in linearization. */
    WsBcStringEncoding string_encoding;

    /* Constant pool.  In this structure, all strings are in UTF-8
       format.  However, they can be converted to different formats - if
       requested - when linearizing the byte-code. */
    WsUInt16 num_constants;
    WsBcConstant *constants;

    /* Pragma pool. */
    WsUInt16 num_pragmas;
    WsBcPragma *pragmas;

    /* Function pool. */

    WsUInt8 num_function_names;
    WsBcFunctionName *function_names;

    WsUInt8 num_functions;
    WsBcFunction *functions;
};

typedef struct WsBcRec WsBc;

/********************* Manipulating byte-code structure *****************/

/* Allocate a new byte-code structure.  The argument `string_encoding'
   specifies the encoding that is used for strings.  The function
   returns NULL if the allocation failed. */
WsBc *ws_bc_alloc(WsBcStringEncoding string_encoding);

/* Free the byte-code structure `bc' and all its internally allocated
   data structures.  The byte-code handle `bc' should not be used
   after this function. */
void ws_bc_free(WsBc *bc);

/* Encode the byte-code structure `bc' into a linearized binary
   byte-code blob.  The function returns WS_TRUE if the encoding was
   successful of WS_FALSE otherwise.  The result blob is returned in
   `data_return' and its length is returned in `data_len_return'.  The
   returned byte-code block must be freed with the ws_bc_data_free()
   function.  You *must* not free it with ws_free() since it will
   corrupt the heap. */
WsBool ws_bc_encode(WsBc *bc, unsigned char **data_return,
                    size_t *data_len_return);

/* Free a byte-code data `data', returned by the ws_bc_encode()
   function. */
void ws_bc_data_free(unsigned char *data);

/* Decode the byte-code data `data' into an in-memory byte-code
   structure.  The function returns the byte-code structure or NULL if
   the decoding fails.  The argument `data_len' specfies the length of
   the byte-code data `data'.  The returned byte-code structure must
   be freed with the ws_bc_free() function when it is not needed
   anymore. */
WsBc *ws_bc_decode(const unsigned char *data, size_t data_len);

/********************* Adding constant elements *************************/

/* Add an integer constant `value' to the constant pool of the
   byte-code structure `bc'.  The index of the constant is returned in
   `index_return'.  The function returns WS_TRUE if the operation was
   successful or WS_FALSE otherwise (out of memory).  */
WsBool ws_bc_add_const_int(WsBc *bc, WsUInt16 *index_return,
                           WsInt32 value);

/* Add a floating point constant `value' to the constant pool of the
   byte-code structure `bc'. */
WsBool ws_bc_add_const_float(WsBc *bc, WsUInt16 *index_return, WsFloat value);

/* Add an UTF-8 encoded string to the constant pool of the byte-code
   structure `bc'. */
WsBool ws_bc_add_const_utf8_string(WsBc *bc, WsUInt16 *index_return,
                                   const unsigned char *data, size_t len);

/* Add an empty string to the constant pool of the byte-code structure
   `bc'. */
WsBool ws_bc_add_const_empty_string(WsBc *bc, WsUInt16 *index_return);

/********************* Adding pragmas ***********************************/

/* Add an access control specifier pragma to the constant and pragma
   pools of the byte-code structure `bc'.  The argument `domain' has
   `domain_len' bytes of UTF-8 data specifying the access domain. */
WsBool ws_bc_add_pragma_access_domain(WsBc *bc,
                                      const unsigned char *domain,
                                      size_t domain_len);

/* Add an access control specifier pragma to the constant and pragma
   pools of the byte-code structure `bc'.  The argument `path' has
   `path_len' bytes of UTF-8 data specifying the access path. */
WsBool ws_bc_add_pragma_access_path(WsBc *bc,
                                    const unsigned char *path,
                                    size_t path_len);

/* Add a use agent property pragma to the constant and pragma pools of
   the byte-code structure `bc'.  The arguments `name' and `property'
   are UTF-8 encoded use agent property. */
WsBool ws_bc_add_pragma_user_agent_property(WsBc *bc,
        const unsigned char *name,
        size_t name_len,
        const unsigned char *property,
        size_t property_len);

/* Add a use agent property pragma to the constant and pragma pools of
   the byte-code structure `bc'.  The arguments `name', `property',
   and `scheme' are UTF-8 encoded use agent property and scheme. */
WsBool ws_bc_add_pragma_user_agent_property_and_scheme(
    WsBc *bc,
    const unsigned char *name,
    size_t name_len,
    const unsigned char *property,
    size_t property_len,
    const unsigned char *scheme,
    size_t scheme_len);

/********************* Adding functions *********************************/

/* Add a new function to the function pool of the byte-code structure
   `bc'.  The argument `name' specifies the name of the function for
   external functions.  For internal functions, the `name' argument
   must be NULL.  The argument `num_arguments' specifies the number of
   arguments the function takes.  The argument `num_locals' specifies
   how many local variables the function needs.  The byte-code of the
   function is in `code' and it is `code_size' bytes long.  The
   function takes a copy of the byte-code array `code'.  The caller
   can free / modify the original array, pointed by the argument
   `code', after the function returns.  The function returns WS_TRUE
   if the adding was successful or WS_FALSE otherwise (out of
   memory). */
WsBool ws_bc_add_function(WsBc *bc, WsUInt8 *index_return,
                          char *name, WsUInt8 num_arguments,
                          WsUInt8 num_locals, WsUInt32 code_size,
                          unsigned char *code);

#endif /* not WSBC_H */
