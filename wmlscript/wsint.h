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
 * wsint.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Operating system specific environment and general helper utilities
 * for the WMLScript tools.
 *
 */

#ifndef WSINT_H
#define WSINT_H

#include "gw-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include "gwlib/gwassert.h"

/********************* Types and definitions ****************************/

/* Platform dependent line terminator.  This is used in diagnostic and
   error messages to terminate output lines. */

#ifdef WIN32
#define WS_LINE_TERMINATOR "\r\n"
#else /* not WIN32 */
#define WS_LINE_TERMINATOR "\n"
#endif /* not WIN32 */

/* Data types. */

#define WS_INT8_MIN	-128
#define WS_INT8_MAX	 127

#define WS_INT16_MIN	-32768
#define WS_INT16_MAX	 32767

#define WS_INT32_MIN	-2147483648
#define WS_INT32_MAX	 2147483647

/* Integer types. */

typedef unsigned char WsByte;

typedef signed char WsInt8;
typedef unsigned char WsUInt8;

typedef signed short WsInt16;
typedef unsigned short WsUInt16;

typedef signed long WsInt32;
typedef unsigned long WsUInt32;

/* Internally we use as good floating point numbers as possible.  This
   way we avoid losing data in constant folding, etc. */
typedef double WsFloat;

typedef enum
{
    WS_FALSE,
    WS_TRUE
} WsBool;


/* Error flags. */

/* Out of memory. */
#define WS_ERROR_B_MEMORY	0x01

/* The input program was syntactically incorrect. */
#define WS_ERROR_B_SYNTAX	0x02

/* The input program was semantically incorrect.  We managed to parse
   it, but it contained some semantical errors.  For example, a local
   variable was defined twice. */
#define WS_ERROR_B_SEMANTIC	0x04

/********************* Include sub-module headers ***********************/

#include "ws.h"
#include "wserror.h"
#include "wsutf8.h"
#include "wsieee754.h"
#include "wsbuffer.h"
#include "wsencode.h"
#include "wsalloc.h"
#include "wsfalloc.h"
#include "wsstream.h"
#include "wshash.h"
#include "wsbc.h"

#include "wsstree.h"
#include "wsasm.h"
#include "wsopt.h"
#include "wsstdlib.h"

/********************* The compiler handle ******************************/

#if WS_DEBUG
/* The currently active compiler.  Just for debugging purposes. */
extern WsCompilerPtr global_compiler;
#endif /* WS_DEBUG */

/* A structure to register the currently active `continue-break'
   labels.  These are allocated from the syntax-tree pool. */
struct WsContBreakRec
{
    struct WsContBreakRec *next;
    WsAsmIns *l_cont;
    WsAsmIns *l_break;
};

typedef struct WsContBreakRec WsContBreak;

#define COMPILER_MAGIC (0xfefe0101)
struct WsCompilerRec
{
    /* A magic number of assure that a correct compiler handle is passed
       to the parser and lexer functions. */
    WsUInt32 magic;

    /* User-specifiable parameters. */
    WsCompilerParams params;

    /* Current input stream. */
    WsStream *input;

    /* The source file name and line number of the current input
       stream. */
    const char *input_name;
    WsUInt32 linenum;

    /* Fast-malloc pool for the syntax tree items. */
    WsFastMalloc *pool_stree;

    /* Fast-malloc pool for the symbolic assembler instructions. */
    WsFastMalloc *pool_asm;

    /* List of active memory blocks, allocated by the lexer.  When lexer
       allocates string or symbol tokens, their dynamically allocated
       data is registered to this list.  The parser removes the items
       when needed, but if the parsing fails, the items can be freed
       from this list during the cleanup. */
    void **lexer_active_list;
    size_t lexer_active_list_size;

    /* The byte-code object. */
    WsBc *bc;

    /* The next label for the assembler generation. */
    WsUInt32 next_label;

    /* The assembler code, currently begin constructed on this compiler. */
    WsAsmIns *asm_head;
    WsAsmIns *asm_tail;

    /* Buffer holding the linearized byte-code for the current symbolic
       assembler. */
    WsBuffer byte_code;

    /* The syntax tree items, found from the source stream. */

    /* External compilation unit pragmas. */
    WsHashPtr pragma_use_hash;

    /* Functions. */
    WsUInt32 num_functions;
    WsFunction *functions;

    /* A mapping from function names to their declarations in
       `functions'. */
    WsHashPtr functions_hash;

    /* A namespace for function arguments and local variables. */
    WsUInt32 next_vindex;
    WsHashPtr variables_hash;

    /* Registry for the currently active `continue-break' labels. */
    WsContBreak *cont_break;

    /* Statistics about the compilation. */

    WsUInt32 num_errors;
    WsUInt32 num_warnings;

    WsUInt32 num_extern_functions;
    WsUInt32 num_local_functions;

    /* Bitmask to record occurred errors.  This is used in error
       generation and reporting to make sane error messages. */
    WsUInt32 errors;

    /* The latest line where a syntax error occurred.  The compiler do
       not print multiple syntax errors from the same line. */
    WsUInt32 last_syntax_error_line;
};

typedef struct WsCompilerRec WsCompiler;

/********************* Lexer and parser *********************************/

/* The lexer. */
extern int yylex();

/* Register the lexer allocated block `ptr' to the compiler's list of
   active blocks. */
WsBool ws_lexer_register_block(WsCompiler *compiler, void *ptr);

/* Register the lexer allocated UTF-8 string `string' to the
   compiler's list of active blocks. */
WsBool ws_lexer_register_utf8(WsCompiler *compiler, WsUtf8String *string);

/* Unregister the block `ptr' from the compiler's list of active
   blocks and free it.  It is a fatal error if the block `ptr' does
   not exist on the list. */
void ws_lexer_free_block(WsCompiler *compiler, void *ptr);

/* Unregister an UTF-8 string `string' from the compiler's list of
   active blocks and free it. */
void ws_lexer_free_utf8(WsCompiler *compiler, WsUtf8String *string);

/* The parser. */
int ws_yy_parse(void *context);

#endif /* not WSINT_H */
