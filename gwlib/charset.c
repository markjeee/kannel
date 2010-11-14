/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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
 * gwlib/charset.c - character set conversions
 *
 * This file implements the character set conversions declared in charset.h.
 *
 * Richard Braakman
 */

#include "gwlib/gwlib.h"

#if HAVE_ICONV
#include <errno.h>
#include <iconv.h>
#endif

/* Code used for non-representable characters */
#define NRP '?'

#include "gwlib/latin1_to_gsm.h"


/* This is the extension table defined in GSM 03.38.  It is the mapping
 * used for the character after a GSM 27 (Escape) character.  All characters
 * not in the table, as well as characters we can't represent, will map
 * to themselves.  We cannot represent the euro symbol, which is an escaped
 * 'e', so we left it out of this table. */
static const struct {
    int gsmesc;
    int latin1;
} gsm_esctolatin1[] = {
    {  10, 12 }, /* ASCII page break */
    {  20, '^' },
    {  40, '{' },
    {  41, '}' },
    {  47, '\\' },
    {  60, '[' },
    {  61, '~' },
    {  62, ']' },
    {  64, '|' },
    { 101, 128 },
    { -1, -1 }
};


/**
 * Struct maps escaped GSM chars to unicode codeposition.
 */
static const struct {
    int gsmesc;
    int unichar;
} gsm_esctouni[] = {
    { 10, 12 }, /* ASCII page break */
    { 20, '^' },
    { 40, '{' },
    { 41, '}' },
    { 47, '\\' },
    { 60, '[' },
    { 61, '~' },
    { 62, ']' },
    { 64, '|' },
    { 'e', 0x20AC },  /* euro symbol */
    { -1, -1 }
};


/* Map GSM default alphabet characters to ISO-Latin-1 characters.
 * The greek characters at positions 16 and 18 through 26 are not
 * mappable.  They are mapped to '?' characters.
 * The escape character, at position 27, is mapped to a space,
 * though normally the function that indexes into this table will
 * treat it specially. */
static const unsigned char gsm_to_latin1[128] = {
     '@', 0xa3,  '$', 0xa5, 0xe8, 0xe9, 0xf9, 0xec,   /* 0 - 7 */
    0xf2, 0xc7,   10, 0xd8, 0xf8,   13, 0xc5, 0xe5,   /* 8 - 15 */
     '?',  '_',  '?',  '?',  '?',  '?',  '?',  '?',   /* 16 - 23 */
         '?',  '?',  '?',  ' ', 0xc6, 0xe6, 0xdf, 0xc9,   /* 24 - 31 */
     ' ',  '!',  '"',  '#', 0xa4,  '%',  '&', '\'',   /* 32 - 39 */
     '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',   /* 40 - 47 */
     '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',   /* 48 - 55 */
     '8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',   /* 56 - 63 */
        0xa1,  'A',  'B',  'C',  'D',  'E',  'F',  'G',   /* 64 - 71 */
         'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',   /* 73 - 79 */
         'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',   /* 80 - 87 */
         'X',  'Y',  'Z', 0xc4, 0xd6, 0xd1, 0xdc, 0xa7,   /* 88 - 95 */
        0xbf,  'a',  'b',  'c',  'd',  'e',  'f',  'g',   /* 96 - 103 */
         'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',   /* 104 - 111 */
         'p',  'q',  'r',  's',  't',  'u',  'v',  'w',   /* 112 - 119 */
         'x',  'y',  'z', 0xe4, 0xf6, 0xf1, 0xfc, 0xe0    /* 120 - 127 */
};

/** 
 * Map GSM default alphabet characters to unicode codeposition.
 * The escape character, at position 27, is mapped to a NRP,
 * though normally the function that indexes into this table will
 * treat it specially.
 */
static const int gsm_to_unicode[128] = {
      '@',  0xA3,   '$',  0xA5,  0xE8,  0xE9,  0xF9,  0xEC,   /* 0 - 7 */
     0xF2,  0xC7,    10,  0xd8,  0xF8,    13,  0xC5,  0xE5,   /* 8 - 15 */
    0x394,   '_', 0x3A6, 0x393, 0x39B, 0x3A9, 0x3A0, 0x3A8,   /* 16 - 23 */
    0x3A3, 0x398, 0x39E,   NRP,  0xC6,  0xE6,  0xDF,  0xC9,   /* 24 - 31 */
      ' ',   '!',   '"',   '#',  0xA4,   '%',   '&',  '\'',   /* 32 - 39 */
      '(',   ')',   '*',   '+',   ',',   '-',   '.',   '/',   /* 40 - 47 */
      '0',   '1',   '2',   '3',   '4',   '5',   '6',   '7',   /* 48 - 55 */
      '8',   '9',   ':',   ';',   '<',   '=',   '>',   '?',   /* 56 - 63 */
      0xA1,  'A',   'B',   'C',   'D',   'E',   'F',   'G',   /* 64 - 71 */
      'H',   'I',   'J',   'K',   'L',   'M',   'N',   'O',   /* 73 - 79 */
      'P',   'Q',   'R',   'S',   'T',   'U',   'V',   'W',   /* 80 - 87 */
      'X',   'Y',   'Z',  0xC4,  0xD6,  0xD1,  0xDC,  0xA7,   /* 88 - 95 */
     0xBF,   'a',   'b',   'c',   'd',   'e',   'f',   'g',   /* 96 - 103 */
      'h',   'i',   'j',   'k',   'l',   'm',   'n',   'o',   /* 104 - 111 */
      'p',   'q',   'r',   's',   't',   'u',   'v',   'w',   /* 112 - 119 */
      'x',   'y',   'z',  0xE4,  0xF6,  0xF1,  0xFC,  0xE0    /* 120 - 127 */
};

/*
 * Register alises for Windows character sets that the libxml/libiconv can
 * recoqnise them.
 */

struct alias_t {
    char *real;
    char *alias;
};

typedef struct alias_t alias_t;

alias_t chars_aliases[] = {
    { "CP1250", "WIN-1250" },
    { "CP1250", "WINDOWS-1250" },
    { "CP1251", "WIN-1251" }, 
    { "CP1251", "WINDOWS-1251" },
    { "CP1252", "WIN-1252" }, 
    { "CP1252", "WINDOWS-1252" },
    { "CP1253", "WIN-1253" }, 
    { "CP1253", "WINDOWS-1253" },
    { "CP1254", "WIN-1254" }, 
    { "CP1254", "WINDOWS-1254" },
    { "CP1257", "WIN-1257" },
    { "CP1257", "WINDOWS-1257" },
    { NULL }
};

void charset_init()
{
    int i;

    for (i = 0; chars_aliases[i].real != NULL; i++) {
      xmlAddEncodingAlias(chars_aliases[i].real,chars_aliases[i].alias);
      /*debug("encoding",0,"Add encoding for %s",chars_aliases[i].alias);*/
    }
}

void charset_shutdown()
{
    xmlCleanupEncodingAliases();
}

/**
 * Convert octet string in GSM format to UTF-8.
 * Every GSM character can be represented with unicode, hence nothing will
 * be lost. Escaped charaters will be translated into appropriate UTF-8 character.
 */
void charset_gsm_to_utf8(Octstr *ostr)
{
    long pos, len;
    Octstr *newostr;

    if (ostr == NULL)
        return;

    newostr = octstr_create("");
    len = octstr_len(ostr);
    
    for (pos = 0; pos < len; pos++) {
        int c, i;
        
        c = octstr_get_char(ostr, pos);
        if (c > 127) {
            warning(0, "Could not convert GSM (0x%02x) to Unicode.", c);
            continue;
        }
        
        if(c == 27 && pos + 1 < len) {
            c = octstr_get_char(ostr, ++pos);
            for (i = 0; gsm_esctouni[i].gsmesc >= 0; i++) {
                if (gsm_esctouni[i].gsmesc == c)
                    break;
            }   
            if (gsm_esctouni[i].gsmesc == c) {
                /* found a value for escaped char */
                c = gsm_esctouni[i].unichar;
            } else {
	        /* nothing found, look esc in our table */
		c = gsm_to_unicode[27];
                pos--;
	    }
        } else if (c < 128) {
            c = gsm_to_unicode[c];
        }
        /* unicode to utf-8 */
        if(c < 128) {
            /* 0-127 are ASCII chars that need no conversion */
            octstr_append_char(newostr, c);
        } else { 
            /* test if it can be converterd into a two byte char */
            if(c < 0x0800) {
                octstr_append_char(newostr, ((c >> 6) | 0xC0) & 0xFF); /* add 110xxxxx */
                octstr_append_char(newostr, (c & 0x3F) | 0x80); /* add 10xxxxxx */
            } else {
                /* else we encode with 3 bytes. This only happens in case of euro symbol */
                octstr_append_char(newostr, ((c >> 12) | 0xE0) & 0xFF); /* add 1110xxxx */
                octstr_append_char(newostr, (((c >> 6) & 0x3F) | 0x80) & 0xFF); /* add 10xxxxxx */
                octstr_append_char(newostr, ((c  & 0x3F) | 0x80) & 0xFF); /* add 10xxxxxx */
            }
            /* There are no 4 bytes encoded characters in GSM charset */
        }
    }

    octstr_truncate(ostr, 0);
    octstr_append(ostr, newostr);
    octstr_destroy(newostr);
}

/**
 * Convert octet string in UTF-8 format to GSM 03.38.
 * Because not all UTF-8 charater can be converted to GSM 03.38 non
 * convertable character replaces with NRP character (see define above).
 * Special characters will be formed into escape sequences.
 * Incomplete UTF-8 characters at the end of the string will be skipped.
 */
void charset_utf8_to_gsm(Octstr *ostr)
{
    long pos, len;
    int val1, val2;
    Octstr *newostr;

    if (ostr == NULL)
        return;
    
    newostr = octstr_create("");
    len = octstr_len(ostr);
    
    for (pos = 0; pos < len; pos++) {
        val1 = octstr_get_char(ostr, pos);
        
        /* check range */
        if (val1 < 0 || val1 > 255) {
            warning(0, "Char (0x%02x) in UTF-8 string not in the range (0, 255). Skipped.", val1);
            continue;
        }
        
        /* Convert UTF-8 to unicode code */
        
        /* test if two byte utf8 char */
        if ((val1 & 0xE0) == 0xC0) {
            /* test if incomplete utf char */
            if(pos + 1 < len) {
                val2 = octstr_get_char(ostr, ++pos);
                val1 = (((val1 & ~0xC0) << 6) | (val2 & 0x3F));
            } else {
                /* incomplete, ignore it */
                warning(0, "Incomplete UTF-8 char discovered, skipped. 1");
                pos += 1;
                continue;
            }
        } else if ((val1 & 0xF0) == 0xE0) { /* test for three byte utf8 char */
            if(pos + 2 < len) {
                val2 = octstr_get_char(ostr, ++pos);
                val1 = (((val1 & ~0xE0) << 6) | (val2 & 0x3F));
                val2 = octstr_get_char(ostr, ++pos);
                val1 = (val1 << 6) | (val2 & 0x3F);
            } else {
                /* incomplete, ignore it */
                warning(0, "Incomplete UTF-8 char discovered, skipped. 2");
                pos += 2;
                continue;
            }
        }

        /* test Latin code page 1 char */
        if(val1 <= 255) {
            val1 = latin1_to_gsm[val1];
            /* needs to be escaped ? */
            if(val1 < 0) {
                octstr_append_char(newostr, 27);
                val1 *= -1;
            }
        } else {
            /* Its not a Latin1 char, test for allowed GSM chars */
            switch(val1) {
            case 0x394:
                val1 = 0x10; /* GREEK CAPITAL LETTER DELTA */
                break;
            case 0x3A6:
                val1 = 0x12; /* GREEK CAPITAL LETTER PHI */
                break;
            case 0x393:
                val1 = 0x13; /* GREEK CAPITAL LETTER GAMMA */
                break;
            case 0x39B:
                val1 = 0x14; /* GREEK CAPITAL LETTER LAMBDA */
                break;
            case 0x3A9:
                val1 = 0x15; /* GREEK CAPITAL LETTER OMEGA */
                break;
            case 0x3A0:
                val1 = 0x16; /* GREEK CAPITAL LETTER PI */
                break;
            case 0x3A8:
                val1 = 0x17; /* GREEK CAPITAL LETTER PSI */
                break;
            case 0x3A3:
                val1 = 0x18; /* GREEK CAPITAL LETTER SIGMA */
                break;
            case 0x398:
                val1 = 0x19; /* GREEK CAPITAL LETTER THETA */
                break;
            case 0x39E:
                val1 = 0x1A; /* GREEK CAPITAL LETTER XI */
                break;
            case 0x20AC:
                val1 = 'e'; /* EURO SIGN */
                octstr_append_char(newostr, 27);
                break;
            default: val1 = NRP; /* character cannot be represented in GSM 03.38 */
            }
        }
        octstr_append_char(newostr, val1);
    }

    octstr_truncate(ostr, 0);
    octstr_append(ostr, newostr);
    octstr_destroy(newostr);
}


void charset_gsm_to_latin1(Octstr *ostr)
{
    long pos, len;

    len = octstr_len(ostr);
    for (pos = 0; pos < len; pos++) {
    int c, new, i;

    c = octstr_get_char(ostr, pos);
    if (c == 27 && pos + 1 < len) {
        /* GSM escape code.  Delete it, then process the next
             * character specially. */
        octstr_delete(ostr, pos, 1);
        len--;
        c = octstr_get_char(ostr, pos);
        for (i = 0; gsm_esctolatin1[i].gsmesc >= 0; i++) {
        if (gsm_esctolatin1[i].gsmesc == c)
            break;
        }
        if (gsm_esctolatin1[i].gsmesc == c)
        new = gsm_esctolatin1[i].latin1;
        else if (c < 128)
        new = gsm_to_latin1[c];
        else
        continue;
    } else if (c < 128) {
            new = gsm_to_latin1[c];
    } else {
        continue;
    }
    if (new != c)
        octstr_set_char(ostr, pos, new);
    }
}


void charset_latin1_to_gsm(Octstr *ostr)
{
    long pos, len;
    int c, new;
    unsigned char esc = 27;

    len = octstr_len(ostr);
    for (pos = 0; pos < len; pos++) {
    c = octstr_get_char(ostr, pos);
    gw_assert(c >= 0);
    gw_assert(c <= 256);
    new = latin1_to_gsm[c];
    if (new < 0) {
         /* Escaped GSM code */
        octstr_insert_data(ostr, pos, (char*) &esc, 1);
        pos++;
        len++;
        new = -new;
    }
    if (new != c)
        octstr_set_char(ostr, pos, new);
    }
}


/*
 * This function is a wrapper arround charset_latin1_to_gsm()
 * which implements the mapping of a NRCs (national reprentation codes)
 * ISO 21 German.
 */
void charset_gsm_to_nrc_iso_21_german(Octstr *ostr)
{
    long pos, len;
    int c, new;

    len = octstr_len(ostr);
    
    for (pos = 0; pos < len; pos++) {
        c = octstr_get_char(ostr, pos);
        switch (c) {
            /* GSM value; NRC value */
            case 0x5b: new = 0x5b; break; /* Ä */
            case 0x5c: new = 0x5c; break; /* Ö */
            case 0x5e: new = 0x5d; break; /* Ü */
            case 0x7b: new = 0x7b; break; /* ä */
            case 0x7c: new = 0x7c; break; /* ö */
            case 0x7e: new = 0x7d; break; /* ü */
            case 0x1e: new = 0x7e; break; /* ß */
            case 0x5f: new = 0x5e; break; /* § */
            default: new = c;
        }
        if (new != c)
            octstr_set_char(ostr, pos, new);
    }
}

void charset_nrc_iso_21_german_to_gsm(Octstr *ostr)
{
    long pos, len;
    int c, new;

    len = octstr_len(ostr);

    for (pos = 0; pos < len; pos++) {
        c = octstr_get_char(ostr, pos);
        switch (c) {
            /* NRC value; GSM value */
            case 0x5b: new = 0x5b; break; /* Ä */
            case 0x5c: new = 0x5c; break; /* Ö */
            case 0x5d: new = 0x5e; break; /* Ü */
            case 0x7b: new = 0x7b; break; /* ä */
            case 0x7c: new = 0x7c; break; /* ö */
            case 0x7d: new = 0x7e; break; /* ü */
            case 0x7e: new = 0x1e; break; /* ß */
            case 0x5e: new = 0x5f; break; /* § */
            default: new = c;
        }
        if (new != c)
            octstr_set_char(ostr, pos, new);
    }
}

int charset_gsm_truncate(Octstr *gsm, long max)
{
    if (octstr_len(gsm) > max) {
	/* If the last GSM character was an escaped character,
	 * then chop off the escape as well as the character. */
	if (octstr_get_char(gsm, max - 1) == 27)
  	    octstr_truncate(gsm, max - 1);
	else
	    octstr_truncate(gsm, max);
	return 1;
    }
    return 0;
}

int charset_to_utf8(Octstr *from, Octstr **to, Octstr *charset_from)
{
    int ret;
    xmlCharEncodingHandlerPtr handler = NULL;
    xmlBufferPtr frombuffer = NULL;
    xmlBufferPtr tobuffer = NULL;

    if (octstr_compare(charset_from, octstr_imm("UTF-8")) == 0) {
        *to = octstr_duplicate(from);
        return 0;
    }

    handler = xmlFindCharEncodingHandler(octstr_get_cstr(charset_from));
    if (handler == NULL)
	return -2;

    /* Build the libxml buffers for the transcoding. */
    tobuffer = xmlBufferCreate();
    frombuffer = xmlBufferCreate();
    xmlBufferAdd(frombuffer, (unsigned char*)octstr_get_cstr(from), octstr_len(from));

    ret = xmlCharEncInFunc(handler, tobuffer, frombuffer);

    *to = octstr_create_from_data((char*)tobuffer->content, tobuffer->use);

    /* Memory cleanup. */
    xmlBufferFree(tobuffer);
    xmlBufferFree(frombuffer);

    return ret;
}

int charset_from_utf8(Octstr *utf8, Octstr **to, Octstr *charset_to)
{
    int ret;
    xmlCharEncodingHandlerPtr handler = NULL;
    xmlBufferPtr frombuffer = NULL;
    xmlBufferPtr tobuffer = NULL;

    handler = xmlFindCharEncodingHandler(octstr_get_cstr(charset_to));
    if (handler == NULL)
	return -2;

    /* Build the libxml buffers for the transcoding. */
    tobuffer = xmlBufferCreate();
    frombuffer = xmlBufferCreate();
    xmlBufferAdd(frombuffer, (unsigned char*)octstr_get_cstr(utf8), octstr_len(utf8));

    ret = xmlCharEncOutFunc(handler, tobuffer, frombuffer);
    if (ret < -2)
	/* Libxml seems to be here a little uncertain what would be the 
	 * return code -3, so let's make it -1. Ugly thing, indeed. --tuo */
	ret = -1; 

    *to = octstr_create_from_data((char*)tobuffer->content, tobuffer->use);

    /* Memory cleanup. */
    xmlBufferFree(tobuffer);
    xmlBufferFree(frombuffer);

    return ret;
}

int charset_convert(Octstr* string, char* charset_from, char* charset_to)
{
#if HAVE_ICONV
    char *from_buf, *to_buf, *pointer;
    size_t inbytesleft, outbytesleft, ret;
    iconv_t cd;
     
    if (!charset_from || !charset_to || !string) /* sanity check */
        return -1;

    if (octstr_len(string) < 1)
        return 0; /* we are done, nothing to convert */
        
    cd = iconv_open(charset_to, charset_from);
    /* Did I succeed in getting a conversion descriptor ? */
    if (cd == (iconv_t)(-1)) {
        /* I guess not */
        error(0,"Failed to convert string from <%s> to <%s> - probably broken type names.", 
              charset_from, charset_to);
        return -1; 
    }
    
    from_buf = octstr_get_cstr(string);
    inbytesleft = octstr_len(string);
    /* allocate max sized buffer, assuming target encoding may be 4 byte unicode */
    outbytesleft = inbytesleft * 4;
    pointer = to_buf = gw_malloc(outbytesleft);

    do {
        ret = iconv(cd, (ICONV_CONST char**) &from_buf, &inbytesleft, &pointer, &outbytesleft);
        if(ret == -1) {
            long tmp;
            /* the conversion failed somewhere */
            switch(errno) {
            case E2BIG: /* no space in output buffer */
                debug("charset", 0, "outbuf to small, realloc.");
                tmp = pointer - to_buf;
                to_buf = gw_realloc(to_buf, tmp + inbytesleft * 4);
                outbytesleft += inbytesleft * 4;
                pointer = to_buf + tmp;
                ret = 0;
                break;
            case EILSEQ: /* invalid multibyte sequence */
            case EINVAL: /* incomplete multibyte sequence */
                warning(0, "Invalid/Incomplete multibyte sequence at position %d, skeep it.",
                        (int)(from_buf - octstr_get_cstr(string)));
                /* skeep char and try next */
                if (outbytesleft == 0) {
                    /* buffer to small */
                    tmp = pointer - to_buf;
                    to_buf = gw_realloc(to_buf, tmp + inbytesleft * 4);
                    outbytesleft += inbytesleft * 4;
                    pointer = to_buf + tmp;
                }
                pointer[0] = from_buf[0];
                pointer++;
                from_buf++;
                inbytesleft--;
                outbytesleft--;
                ret = 0;
                break;
            }
        }
    } while(inbytesleft && ret == 0); /* stop if error occurs and not handled above */
    
    iconv_close(cd);
    
    if (ret != -1) {
        /* conversion succeeded */
        octstr_truncate(string, 0);
        octstr_append_data(string, to_buf, pointer - to_buf);
        if (ret)
            debug("charset", 0, "charset_convert did %ld non-reversible conversions", (long) ret);
        ret = 0;
    } else
        error(errno,"Failed to convert string from <%s> to <%s>.", charset_from, charset_to);

    if (errno == EILSEQ) {
        debug("charset_convert", 0, "Found an invalid multibyte sequence at position <%d>",
              (int)(from_buf - octstr_get_cstr(string)));
    }
    gw_free(to_buf);
    return ret;
#endif
    /* no convertion done due to not having iconv */
    return -1;
}
