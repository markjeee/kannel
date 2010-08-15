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
 * html.c - routines for manipulating HTML.
 *
 * Lars Wirzenius
 */


#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "html.h"
#include "gwlib/gwlib.h"

#define SMS_MAX 161


/* Is there a comment beginning at offset `pos'? */
static int html_comment_begins(Octstr *html, long pos)
{
    char buf[10];

    octstr_get_many_chars(buf, html, pos, 4);
    buf[5] = '\0';
    return strcmp(buf, "<!--") == 0;
}


/* Skip a comment in HTML. */
static void skip_html_comment(Octstr *html, long *pos)
{
    long i;

    *pos += 4; 	/* Skip "<!--" at beginning of comment. */
    i = octstr_search(html, octstr_imm("-->"), *pos);
    if (i == -1)
        *pos = octstr_len(html);
    else
        *pos = i;
}


/* Skip a beginning or ending tag in HTML, including any attributes. */
static void skip_html_tag(Octstr *html, long *pos)
{
    long i, len;
    int c;

    /* Skip leading '<'. */
    ++(*pos);

    /* Skip name of tag and attributes with values. */
    len = octstr_len(html);
    while (*pos < len && (c = octstr_get_char(html, *pos)) != '>') {
        if (c == '"' || c == '\'') {
            i = octstr_search_char(html, c, *pos + 1);
            if (i == -1)
                *pos = len;
            else
                *pos = i + 1;
        } else
            ++(*pos);
    }

    /* Skip trailing '>' if it is there. */
    if (octstr_get_char(html, *pos) == '>')
        ++(*pos);
}


/* Convert an HTML entity into a single character and advance `*html' past
   the entity. */
static void convert_html_entity(Octstr *sms, Octstr *html, long *pos)
{
    static struct {
        char *entity;
        int latin1;
    }
    tab[] = {
        { "&amp;", '&' },
        { "&lt;", '<' },
        { "&gt;", '>' },

        /* The following is copied from

        	http://www.hut.fi/~jkorpela/HTML3.2/latin1.html

           by Jukka Korpela. Hand and script edited to form this
           table. */

        { "&nbsp;", ' ' },
        { "&iexcl;", 161 },
        { "&cent;", 162 },
        { "&pound;", 163 },
        { "&curren;", 164 },
        { "&yen;", 165 },
        { "&brvbar;", 166 },
        { "&sect;", 167 },
        { "&uml;", 168 },
        { "&copy;", 169 },
        { "&ordf;", 170 },
        { "&laquo;", 171 },
        { "&not;", 172 },
        { "&shy;", 173 },
        { "&reg;", 174 },
        { "&macr;", 175 },
        { "&deg;", 176 },
        { "&plusmn;", 177 },
        { "&sup2;", 178 },
        { "&sup3;", 179 },
        { "&acute;", 180 },
        { "&micro;", 181 },
        { "&para;", 182 },
        { "&middot;", 183 },
        { "&cedil;", 184 },
        { "&sup1;", 185 },
        { "&ordm;", 186 },
        { "&raquo;", 187 },
        { "&frac14;", 188 },
        { "&frac12;", 189 },
        { "&frac34;", 190 },
        { "&iquest;", 191 },
        { "&Agrave;", 192 },
        { "&Aacute;", 193 },
        { "&Acirc;", 194 },
        { "&Atilde;", 195 },
        { "&Auml;", 196 },
        { "&Aring;", 197 },
        { "&AElig;", 198 },
        { "&Ccedil;", 199 },
        { "&Egrave;", 200 },
        { "&Eacute;", 201 },
        { "&Ecirc;", 202 },
        { "&Euml;", 203 },
        { "&Igrave;", 204 },
        { "&Iacute;", 205 },
        { "&Icirc;", 206 },
        { "&Iuml;", 207 },
        { "&ETH;", 208 },
        { "&Ntilde;", 209 },
        { "&Ograve;", 210 },
        { "&Oacute;", 211 },
        { "&Ocirc;", 212 },
        { "&Otilde;", 213 },
        { "&Ouml;", 214 },
        { "&times;", 215 },
        { "&Oslash;", 216 },
        { "&Ugrave;", 217 },
        { "&Uacute;", 218 },
        { "&Ucirc;", 219 },
        { "&Uuml;", 220 },
        { "&Yacute;", 221 },
        { "&THORN;", 222 },
        { "&szlig;", 223 },
        { "&agrave;", 224 },
        { "&aacute;", 225 },
        { "&acirc;", 226 },
        { "&atilde;", 227 },
        { "&auml;", 228 },
        { "&aring;", 229 },
        { "&aelig;", 230 },
        { "&ccedil;", 231 },
        { "&egrave;", 232 },
        { "&eacute;", 233 },
        { "&ecirc;", 234 },
        { "&euml;", 235 },
        { "&igrave;", 236 },
        { "&iacute;", 237 },
        { "&icirc;", 238 },
        { "&iuml;", 239 },
        { "&eth;", 240 },
        { "&ntilde;", 241 },
        { "&ograve;", 242 },
        { "&oacute;", 243 },
        { "&ocirc;", 244 },
        { "&otilde;", 245 },
        { "&ouml;", 246 },
        { "&divide;", 247 },
        { "&oslash;", 248 },
        { "&ugrave;", 249 },
        { "&uacute;", 250 },
        { "&ucirc;", 251 },
        { "&uuml;", 252 },
        { "&yacute;", 253 },
        { "&thorn;", 254 },
        { "&yuml;", 255 },
    };
    int num_tab = sizeof(tab) / sizeof(tab[0]);
    long i, code;
    size_t len;
    char buf[1024];

    if (octstr_get_char(html, *pos + 1) == '#') {
        if (octstr_get_char(html, *pos + 2) == 'x' || octstr_get_char(html, *pos + 2) == 'X')
            i = octstr_parse_long(&code, html, *pos + 3, 16); /* hex */
        else
            i = octstr_parse_long(&code, html, *pos + 2, 10); /* decimal */
        if (i > 0) {
            if (code < 256)
                octstr_append_char(sms, code);
            *pos = i + 1;
            if (octstr_get_char(html, *pos) == ';')
                ++(*pos);
        } else {
            ++(*pos);
            octstr_append_char(sms, '&');
        }
    } else {
        for (i = 0; i < num_tab; ++i) {
            len = strlen(tab[i].entity);
            octstr_get_many_chars(buf, html, *pos, len);
            buf[len] = '\0';
            if (strcmp(buf, tab[i].entity) == 0) {
                *pos += len;
                octstr_append_char(sms, tab[i].latin1);
                break;
            }
        }
        if (i == num_tab) {
            ++(*pos);
            octstr_append_char(sms, '&');
        }
    }
}


Octstr *html_to_sms(Octstr *html)
{
    long i, len;
    int c;
    Octstr *sms;

    sms = octstr_create("");
    len = octstr_len(html);
    i = 0;
    while (i < len) {
        c = octstr_get_char(html, i);
        switch (c) {
        case '<':
            if (html_comment_begins(html, i))
                skip_html_comment(html, &i);
            else
                skip_html_tag(html, &i);
            break;
        case '&':
            convert_html_entity(sms, html, &i);
            break;
        default:
            octstr_append_char(sms, c);
            ++i;
            break;
        }
    }
    octstr_shrink_blanks(sms);
    octstr_strip_blanks(sms);
    return sms;
}
