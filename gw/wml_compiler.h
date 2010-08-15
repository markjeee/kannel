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
 * wml_compiler.h - compiling WML to WML binary
 *
 * This is a header for WML compiler for compiling the WML text 
 * format to WML binary format, which is used for transmitting the 
 * decks to the mobile terminal to decrease the use of the bandwidth.
 *
 * See comments below for explanations on individual functions.
 *
 * Tuomas Luttinen for Wapit Ltd.
 */


#ifndef WML_COMPILER_H
#define WML_COMPILER_H

/*
 * wml_compile - the interface to wml_compiler
 * 
 * This function compiles the WML to WML binary. The arguments are 
 * the following: 
 *   wml_text: the WML text to be compiled
 *   charset: the character set as HTTP headers declare it
 *   wml_binary: buffer for the compiled WML binary
 *   version: max wbxml version supported by device, or NULL for default
 *
 * The wml_text and charset are allocated by the caller and should also be
 * freed by the caller. The function takes care for memory allocation for
 * the wml_binary. The caller is responsible for freeing this space. 
 * 
 * Return: 0 for ok, -1 for an error
 * 
 * Errors are logged with a little explanation and error number.
 */
int wml_compile(Octstr *wml_text,
		Octstr *charset,
		Octstr **wml_binary,
		Octstr *version);

/*
 * A function to initialize the wml compiler for use. Allocates memory 
 * for the tables wml compiler uses.
 * 
 * The passed boolean defines if our internal wml_compiler will be forcing
 * libxml2 parser to be strict, hence not to recover from XML parsing
 * errors, or if we let the parset be relax and recover from errors.
 * 
 * Beware that a related XML parsing mode is considered to be a vulnerability
 * to your wapbox if huge WML/XML bogus is injected and the wml_compiler
 * runs all his string sorting/matching things on this.
 */
void wml_init(int wml_xml_strict);

/*
 * A function for shutting down the wml_compiler. Frees the memory used 
 * by the wml compiler.
 */
void wml_shutdown(void);


#endif
