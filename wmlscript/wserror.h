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
 * wserror.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Error and information reporting functions.
 *
 */

#ifndef WSERROR_H
#define WSERROR_H

/********************* High-level functions *****************************/

/* Report an informative message `message'. */
void ws_info(WsCompilerPtr compiler, char *message, ...);

/* Report a fatal (non-recovable) error and terminate the program
   brutally.  This is only used to report internal inconsistencies and
   bugs.  This will never return. */
void ws_fatal(char *fmt, ...) PRINTFLIKE(1,2);

/* Report an out-of-memory error. */
void ws_error_memory(WsCompilerPtr compiler);

/* Report a syntax error from the line `line' of the current input
   stream.  If the argument `line' is 0, the error line number is the
   current line number of the input stream. */
void ws_error_syntax(WsCompilerPtr compiler, WsUInt32 line);

/* Report a source stream specific (WMLScript language specific) error
   `message' from the source stream line number `line'.  If the
   argument `line' is 0, the line number information is taken from the
   input stream's current position. */
void ws_src_error(WsCompilerPtr compiler, WsUInt32 line, char *message, ...);

/* Report a source stream specific warning `message' from the source
   stram line number `line'.  If the argument `line' is 0, the line
   number information is taken from the input stream's current
   position. */
void ws_src_warning(WsCompilerPtr compiler, WsUInt32 line, char *message, ...);

/********************* Low-level functions ******************************/

/* Standard output and error streams.  These are handy macros to fetch
   the I/O function and its context corresponding to the stream from
   the compiler. */

#define WS_STREAM(_stream)		\
compiler->params._stream ## _cb,	\
compiler->params._stream ## _cb_context

#define WS_STDOUT WS_STREAM(stdout)
#define WS_STDERR WS_STREAM(stderr)

/* Print the message `fmt', `...' to the stream `io', `context'.  Note
   that not all format and format specifiers of the normal printf()
   are supported. */
void ws_fprintf(WsIOProc io, void *context, const char *fmt, ...);

/* Print the message `fmt', `ap' to the stream `io', `context'. */
void ws_vfprintf(WsIOProc io, void *context, const char *fmt, va_list ap);

/* Print the string `str' to the stream `io', `context'.  The function
   will not print newline after the string. */
void ws_puts(WsIOProc io, void *context, const char *str);

/* Print the character `ch' to the stream `io', `context'. */
void ws_fputc(int ch, WsIOProc io, void *context);

#endif /* not WSERROR_H */
