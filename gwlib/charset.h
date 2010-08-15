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
 * gwlib/charset.h - character set conversions
 *
 * This header defines some utility functions for converting between
 * character sets.  Approximations are made when necessary, so avoid
 * needless conversions.
 *
 * Currently only GSM and Latin-1 are supported with Kannel specific
 * functions. This module contains also wrappers for libxml2 character
 * set conversion functions that work either from or to UTF-8. More
 * about libxml2's character set support on the header file
 * <libxml/encoding.h> or the implementation file encoding.c. Short
 * version: it has a few basic character set supports built in; for
 * the rest iconv is used.
 *
 * Richard Braakman
 * Tuomas Luttinen
 */

#ifndef CHARSET_H
#define CHARSET_H

#include <libxml/encoding.h>
#include <libxml/tree.h>

/*
 * Initialize the charset subsystem.
 */
void charset_init(void);

/*
 * Shutdown the charset subsystem.
 */
void charset_shutdown(void);

/**
 * Convert octet string in GSM format to UTF-8.
 * Every GSM character can be represented with unicode, hence nothing will
 * be lost. Escaped charaters will be translated into appropriate UTF-8 character.
 */
void charset_gsm_to_utf8(Octstr *ostr);

/**
 * Convert octet string in UTF-8 format to GSM 03.38.
 * Because not all UTF-8 charater can be converted to GSM 03.38 non
 * convertable character replaces with NRP character (see define above).
 * Special characters will be formed into escape sequences.
 * Incomplete UTF-8 characters at the end of the string will be skipped.
 */
void charset_utf8_to_gsm(Octstr *ostr);

/*
 * Convert from GSM default character set to NRC ISO 21 (German)
 * and vise versa.
 */
void charset_gsm_to_nrc_iso_21_german(Octstr *ostr);
void charset_nrc_iso_21_german_to_gsm(Octstr *ostr);

/* Trunctate a string of GSM characters to a maximum length.
 * Make sure the last remaining character is a whole character,
 * and not half of an escape sequence.
 * Return 1 if any characters were removed, otherwise 0.
 */
int charset_gsm_truncate(Octstr *gsm, long max);

/* Convert a string in the GSM default character set (GSM 03.38)
 * to ISO-8859-1.  A series of Greek characters (codes 16, 18-26)
 * are not representable and are converted to '?' characters.
 * GSM default is a 7-bit alphabet.  Characters with the 8th bit
 * set are left unchanged. */
void charset_gsm_to_latin1(Octstr *gsm);

/* Convert a string in the ISO-8859-1 character set to the GSM 
 * default character set (GSM 03.38).  A large number of characters
 * are not representable.  Approximations are made in some cases
 * (accented characters to their unaccented versions, for example),
 * and the rest are converted to '?' characters. */
void charset_latin1_to_gsm(Octstr *latin1);

/* Convert a string from  character set specified by charset_from into
 * UTF-8 character set. The result is stored in the octet string *to that 
 * is allocated by the function. The function returns the number of bytes 
 * written for success, -1 for general error, -2 for an transcoding error 
 * (the input string wasn't valid string in the character set it was said 
 * to be or there was no converter found for the character set).
 */
int charset_to_utf8(Octstr *from, Octstr **to, Octstr *charset_from);

/* Convert a string from UTF-8 character set into another character set 
 * specified by charset_from. The result is stored in the octet string *to
 * that is allocated by the function. The function returns the number of 
 * bytes written for success, -1 for general error, -2 for an transcoding 
 * error (the input string wasn't valid string in the character set it 
 * was said to be or there was no converter found for the character set).
 */
int charset_from_utf8(Octstr *utf8, Octstr **to, Octstr *charset_to);

/* use iconv library to convert an Octstr in place, from source character set to
 * destination character set
 */
int charset_convert(Octstr* string, char* charset_from, char* charset_to);

#endif
