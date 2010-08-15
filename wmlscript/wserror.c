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
 * wserror.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Error and information reporting functions.
 *
 */

#include "wsint.h"

/********************* High-level functions *****************************/

void ws_info(WsCompilerPtr compiler, char *message, ...)
{
    va_list ap;

    if (!compiler->params.verbose)
        return;

    ws_puts(WS_STDOUT, "wsc: ");

    va_start(ap, message);
    ws_vfprintf(WS_STDOUT, message, ap);
    va_end(ap);

    ws_puts(WS_STDOUT, WS_LINE_TERMINATOR);
}


void ws_fatal(char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "wsc: fatal: ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");

    abort();
}


void ws_error_memory(WsCompilerPtr compiler)
{
    gw_assert(compiler->magic == COMPILER_MAGIC);

    if (compiler->errors & WS_ERROR_B_MEMORY)
        /* We have already reported this error. */
        return;

    compiler->errors |= WS_ERROR_B_MEMORY;
    ws_puts(WS_STDERR, "wsc: error: out of memory" WS_LINE_TERMINATOR);
}


void ws_error_syntax(WsCompilerPtr compiler, WsUInt32 line)
{
    gw_assert(compiler->magic == COMPILER_MAGIC);

    if (compiler->errors & WS_ERROR_B_MEMORY)
        /* It makes no sense to report syntax errors when we have run out
           of memory.  This information is not too valid. */ 
        return;

    if (line == 0)
        line = compiler->linenum;

    if (compiler->last_syntax_error_line == line)
        /* It makes no sense to report multiple syntax errors from the
           same line. */ 
        return;

    compiler->last_syntax_error_line = line;
    compiler->errors |= WS_ERROR_B_SYNTAX;

    ws_fprintf(WS_STDERR, "%s:%u: syntax error" WS_LINE_TERMINATOR,
               compiler->input_name, line);
}


void ws_src_error(WsCompilerPtr compiler, WsUInt32 line, char *message, ...)
{
    va_list ap;

    gw_assert(compiler->magic == COMPILER_MAGIC);

    if (line == 0)
        line = compiler->linenum;

    compiler->errors |= WS_ERROR_B_SEMANTIC;

    ws_fprintf(WS_STDERR, "%s:%u: ", compiler->input_name, line);

    va_start(ap, message);
    ws_vfprintf(WS_STDERR, message, ap);
    va_end(ap);

    ws_puts(WS_STDERR, WS_LINE_TERMINATOR);

    compiler->num_errors++;
}


void ws_src_warning(WsCompilerPtr compiler, WsUInt32 line, char *message, ...)
{
    va_list ap;

    gw_assert(compiler->magic == COMPILER_MAGIC);

    if (line == 0)
        line = compiler->linenum;

    ws_fprintf(WS_STDERR, "%s:%u: warning: ", compiler->input_name, line);

    va_start(ap, message);
    ws_vfprintf(WS_STDERR, message, ap);
    va_end(ap);

    ws_puts(WS_STDERR, WS_LINE_TERMINATOR);

    compiler->num_errors++;
}

/********************* Low-level functions ******************************/

void ws_fprintf(WsIOProc io, void *context, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    ws_vfprintf(io, context, fmt, ap);
    va_end(ap);
}


void ws_vfprintf(WsIOProc io, void *context, const char *fmt, va_list ap)
{
    int start, i;

    for (start = 0, i = 0; fmt[i]; i++)
        if (fmt[i] == '%' && fmt[i + 1]) {
            char buf[256];
            char *cp;
            int ival;
            unsigned int uival;
            int padder = ' ';
            int left = 0;
            unsigned int width = 0;

            if (fmt[i + 1] == '%') {
                /* An escaped `%'.  Print leading data including the `%'
                          character. */
                i++;
                (*io)(fmt + start, i - start, context);
                start = i + 1;
                continue;
            }

            /* An escape sequence. */

            /* Print leading data if any. */
            if (i > start)
                (*io)(fmt + start, i - start, context);

            /* We support a minor sub-set of the printf()'s formatting
                      capabilities.  Let's see what we got. */
            i++;

            /* Alignment? */
            if (fmt[i] == '-') {
                left = 1;
                i++;
            }

            /* Padding? */
            if (fmt[i] == '0') {
                padder = '0';
                i++;
            }

            /* Width? */
            while ('0' <= fmt[i] && fmt[i] <= '9') {
                width *= 10;
                width += fmt[i++] - '0';
            }

            /* Check the format. */
            cp = buf;
            switch (fmt[i]) {
            case 'c': 		/* character */
                ival = (int) va_arg(ap, int);

                snprintf(buf, sizeof(buf), "%c", (char) ival);
                cp = buf;
                break;

            case 's': 		/* string */
                cp = va_arg(ap, char *);
                break;

            case 'd': 		/* integer */
                ival = va_arg(ap, int);

                snprintf(buf, sizeof(buf), "%d", ival);
                cp = buf;
                break;

            case 'u': 		/* unsigned integer */
                uival = va_arg(ap, unsigned int);

                snprintf(buf, sizeof(buf), "%u", uival);
                cp = buf;
                break;

            case 'x': 		/* unsigned integer in hexadecimal format */
                uival = va_arg(ap, unsigned int);

                snprintf(buf, sizeof(buf), "%x", uival);
                cp = buf;
                break;

            default:
                ws_fatal("ws_vfprintf(): format %%%c not implemented", fmt[i]);
                break;
            }

            if (left)
                /* Output the value left-justified. */
                (*io)(cp, strlen(cp), context);

            /* Need padding? */
            if (width > strlen(cp)) {
                /* Yes we need. */
                int amount = width - strlen(cp);

                while (amount-- > 0)
                    ws_fputc(padder, io, context);
            }

            if (!left)
                /* Output the value right-justified. */
                (*io)(cp, strlen(cp), context);

            /* Process more. */
            start = i + 1;
        }

    /* Print trailing data if any. */
    if (i > start)
        (*io)(fmt + start, i - start, context);
}


void ws_puts(WsIOProc io, void *context, const char *str)
{
    (*io)(str, strlen(str), context);
}


void ws_fputc(int ch, WsIOProc io, void *context)
{
    char c = (char) ch;

    (*io)(&c, 1, context);
}
