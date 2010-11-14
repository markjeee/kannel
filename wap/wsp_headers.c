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
 * wsp_headers.c - Implement WSP PDU headers
 * 
 * References:
 *   WSP specification version 1.1
 *   RFC 2068, Hypertext Transfer Protocol HTTP/1.1
 *   RFC 2616, Hypertext Transfer Protocol HTTP/1.1
 *
 *   For push headers, WSP specification, June 2000 conformance release
 *
 * This file has two parts.  The first part decodes the request's headers
 * from WSP to HTTP.  The second part encodes the response's headers from
 * HTTP to WSP.
 *
 * Note that push header encoding and decoding are divided two parts:
 * first decoding and encoding numeric values and then packing these values
 * into WSP format and unpacking them from WSP format. This module contains
 * only packing and unpacking parts.
 *
 * Some functions are declared non-static to provide them for external use,
 * ie. the MMS encapsulation encoding and decoding routines implemented in 
 * other files.
 *
 * Richard Braakman
 * Stipe Tolj <stolj@wapme.de>
 */

#include <string.h>
#include <limits.h>
#include <ctype.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_headers.h"
#include "wsp_strings.h"

/*
 * get field value and return its type as predefined data types
 * There are three kinds of field encodings:
 *   WSP_FIELD_VALUE_NUL_STRING: 0-terminated string
 *   WSP_FIELD_VALUE_ENCODED: short integer, range 0-127
 *   WSP_FIELD_VALUE_DATA: octet string defined by length
 * The function will return one of those values, and modify the parse context
 * to make it easy to get the field data.
 *   WSP_FIELD_VALUE_NUL_STRING: Leave parsing position at start of string
 *   WSP_FIELD_VALUE_ENCODED: Put value in *well_known_value, leave
 *        parsing position after field value.
 *   WSP_FIELD_VALUE_DATA: Leave parsing position at start of data, and set
 *        a parse limit at the end of data.
 */
int wsp_field_value(ParseContext *context, int *well_known_value)
{
    int val;
    unsigned long len;

    val = parse_get_char(context);
    if (val > 0 && val < 31) {
        *well_known_value = -1;
        parse_limit(context, val);
        return WSP_FIELD_VALUE_DATA;
    } else if (val == 31) {
        *well_known_value = -1;
        len = parse_get_uintvar(context);
        parse_limit(context, len);
        return WSP_FIELD_VALUE_DATA;
    } else if (val > 127) {
        *well_known_value = val - 128;
        return WSP_FIELD_VALUE_ENCODED;
    } else if (val == WSP_QUOTE || val == '"') {  /* 127 */
        *well_known_value = -1;
        /* We already consumed the Quote */
        return WSP_FIELD_VALUE_NUL_STRING;
    } else {    /* implicite val == 0 */ 
        *well_known_value = -1;
        /* Un-parse the character we just read */
        parse_skip(context, -1);
        return WSP_FIELD_VALUE_NUL_STRING;
    }
}

/* Skip over a field_value as defined above. */
void wsp_skip_field_value(ParseContext *context)
{
    int val;
    int ret;

    ret = wsp_field_value(context, &val);
    if (ret == WSP_FIELD_VALUE_DATA) {
        parse_skip_to_limit(context);
        parse_pop_limit(context);
    }
}

/* Multi-octet-integer is defined in 8.4.2.1 */
static long unpack_multi_octet_integer(ParseContext *context, long len)
{
    long val = 0;

    if (len > (long) sizeof(val) || len < 0)
        return -1;

    while (len > 0) {
        val = val * 256 + parse_get_char(context);
        len--;
    }

    if (parse_error(context))
        return -1;

    return val;
}

/* This function is similar to field_value, but it is used at various
 * places in the grammar where we expect either an Integer-value
 * or some kind of NUL-terminated text.
 * 
 * Return values are just like field_value except that WSP_FIELD_VALUE_DATA
 * will not be returned.
 *
 * As a special case, we parse a 0-length Long-integer as an
 * WSP_FIELD_VALUE_NONE, so that we can distinguish between No-value
 * and an Integer-value of 0.  (A real integer 0 would be encoded as
 * a Short-integer; the definition of Long-integer seems to allow
 * 0-length integers, but the definition of Multi-octet-integer does
 * not, so this is an unclear area of the specification.)
 */
int wsp_secondary_field_value(ParseContext *context, long *result)
{
    int val;
    long length;

    val = parse_get_char(context);
    if (val == 0) {
        *result = 0;
        return WSP_FIELD_VALUE_NONE;
    } else if (val > 0 && val < 31) {
        *result = unpack_multi_octet_integer(context, val);
        return WSP_FIELD_VALUE_ENCODED;
    } else if (val == 31) {
        length = parse_get_uintvar(context);
        *result = unpack_multi_octet_integer(context, length);
        return WSP_FIELD_VALUE_ENCODED;
    } else if (val > 127) {
        *result = val - 128;
        return WSP_FIELD_VALUE_ENCODED;
    } else if (val == WSP_QUOTE) {  /* 127 */
        *result = -1;
        return WSP_FIELD_VALUE_NUL_STRING;
    } else {
        *result = -1;
        /* Un-parse the character we just read */
        parse_skip(context, -1);
        return WSP_FIELD_VALUE_NUL_STRING;
    }
}

/* Integer-value is defined in 8.4.2.3 */
Octstr *wsp_unpack_integer_value(ParseContext *context)
{
    Octstr *decoded;
    unsigned long value;
    int val;

    val = parse_get_char(context);
    if (val < 31) {
        value = unpack_multi_octet_integer(context, val);
    } else if (val > 127) {
        value = val - 128;
    } else {
        warning(0, "WSP headers: bad integer-value.");
        return NULL;
    }

    decoded = octstr_create("");
    octstr_append_decimal(decoded, value);
    return decoded;
}

/* Q-value is defined in 8.4.2.3 */
static Octstr *convert_q_value(int q)
{
    Octstr *result = NULL;

    /* When quality factor 0 and quality factors with one or two
     * decimal digits are encoded, they shall be multiplied by 100
     * and incremented by one, so that they encode as a one-octet
     * value in range 1-100. */
    if (q >= 1 && q <= 100) {
        q = q - 1;
        result = octstr_create("0.");
        octstr_append_char(result, (q / 10) + '0');
        if (q % 10 > 0)
            octstr_append_char(result, (q % 10) + '0');
        return result;
    }

    /* Three decimal quality factors shall be multiplied with 1000
     * and incremented by 100. */
    if (q > 100 && q <= 1000) {
        q = q - 100;
        result = octstr_create("0.");
        octstr_append_char(result, (q / 100) + '0');
        if (q % 100 > 0)
            octstr_append_char(result, (q / 10 % 10) + '0');
        if (q % 10 > 0)
            octstr_append_char(result, (q % 10) + '0');
        return result;
    }

    return NULL;
}

/* Q-value is defined in 8.4.2.3 */
static Octstr *unpack_q_value(ParseContext *context)
{
    int c, c2;

    c = parse_get_char(context);
    if (c < 0)
        return NULL;

    if (c & 0x80) {
        c2 = parse_get_char(context);
        if (c2 < 0 || (c2 & 0x80))
            return NULL;
        c = ((c & 0x7f) << 8) + c2;
    }

    return convert_q_value(c);
}


/* Version-value is defined in 8.4.2.3. Encoding-Version uses coding
 * defined in this chapter, see 8.4.2.70.  */
Octstr *wsp_unpack_version_value(long value)
{
    Octstr *result;
    int major, minor;

    major = ((value >> 4) & 0x7);
    minor = (value & 0xf);

    result = octstr_create("");
    octstr_append_char(result, major + '0');
    if (minor != 15) {
        octstr_append_char(result, '.');
        octstr_append_decimal(result, minor);
    }

    return result;
}

static Octstr *unpack_encoding_version(ParseContext *context)
{
    int ch;

    ch = parse_get_char(context);
    if (ch < 128) {
        warning(0, "WSP: bad Encoding-Version value");
        return NULL;
    }

    return wsp_unpack_version_value(((long) ch) - 128);
}


/* Called with the parse limit set to the end of the parameter data,
 * and decoded containing the unpacked header line so far.
 * Parameter is defined in 8.4.2.4. */
static int unpack_parameter(ParseContext *context, Octstr *decoded)
{
    Octstr *parm = NULL;
    Octstr *value = NULL;
    int ret;
    long type;
    long val;

    ret = wsp_secondary_field_value(context, &type);
    if (parse_error(context) || ret == WSP_FIELD_VALUE_NONE) {
        warning(0, "bad parameter");
        goto error;
    }

    if (ret == WSP_FIELD_VALUE_ENCODED) {
        /* Typed-parameter */
        parm = wsp_parameter_to_string(type);
        if (!parm)
            warning(0, "Unknown parameter %02lx.", type);
    } else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
        /* Untyped-parameter */
        parm = parse_get_nul_string(context);
        if (!parm)
            warning(0, "Format error in parameter.");
        type = -1;
        /* We treat Untyped-value as a special type.  Its format
         * Integer-value | Text-value is pretty similar to most
         * typed formats. */
    } else {
        panic(0, "Unknown secondary field value type %d.", ret);
    }

    if (type == 0x00) /* q */
        value = unpack_q_value(context);
    else {
        ret = wsp_secondary_field_value(context, &val);
        if (parse_error(context)) {
            warning(0, "bad parameter value");
            goto error;
        }

        if (ret == WSP_FIELD_VALUE_ENCODED) {
            switch (type) {
            case -1:  /* untyped: Integer-value */
            case 3:  /* type: Integer-value */
            case 8:  /* padding: Short-integer */
                value = octstr_create("");
                octstr_append_decimal(value, val);
                break;
            case 0:  /* q, already handled above */
                gw_assert(0);
                break;
            case 1:  /* charset: Well-known-charset */
                value = wsp_charset_to_string(val);
                if (!value)
                    warning(0, "Unknown charset %04lx.", val);
                break;
            case 2:  /* level: Version-value */
                value = wsp_unpack_version_value(val);
                break;
            case 5:  /* name: Text-string */
            case 6:  /* filename: Text-string */
                warning(0, "Text-string parameter with integer encoding");
                break;
            case 7:  /* differences: Field-name */
                value = wsp_header_to_string(val);
                if (!value)
                    warning(0, "Unknown differences header %02lx.", val);
                break;
            default:
                warning(0, "Unknown parameter encoding %02lx.",
                        type);
                break;
            }
        } else if (ret == WSP_FIELD_VALUE_NONE) {
            value = octstr_create("");
        } else {
            gw_assert(ret == WSP_FIELD_VALUE_NUL_STRING);
            /* Text-value = No-value | Token-text | Quoted-string */
            value = parse_get_nul_string(context);
            if (!value)
                warning(0, "Format error in parameter value.");
            else {
                if (octstr_get_char(value, 0) == '"') {
                    /* Quoted-string */
                    octstr_append_char(value, '"');
                } else { /* DAVI! */
                    octstr_insert(value, octstr_imm("\""), 0);
                    octstr_append_char(value, '"');
                }
            }
        }
    }

    if (!parm || !value) {
        warning(0, "Skipping parameters");
        goto error;
    }

    octstr_append(decoded, octstr_imm("; "));
    octstr_append(decoded, parm);
    if (octstr_len(value) > 0) {
        octstr_append_char(decoded, '=');
        octstr_append(decoded, value);
    }
    octstr_destroy(parm);
    octstr_destroy(value);
    return 0;

error:
    parse_skip_to_limit(context);
    octstr_destroy(parm);
    octstr_destroy(value);
    parse_set_error(context);
    return -1;
}

void wsp_unpack_all_parameters(ParseContext *context, Octstr *decoded)
{
    int ret = 0;

    while (ret >= 0 && !parse_error(context) &&
           parse_octets_left(context) > 0) {
        ret = unpack_parameter(context, decoded);
    }
}

/* Unpack parameters in the format used by credentials and challenge,
 * which differs from the format used by all other HTTP headers. */
static void unpack_broken_parameters(ParseContext *context, Octstr *decoded)
{
    int ret = 0;
    int first = 1;
    long pos;

    while (ret >= 0 && !parse_error(context) &&
	   parse_octets_left(context) > 0) {
        pos = octstr_len(decoded);
        ret = unpack_parameter(context, decoded);
        if (ret >= 0) {
            if (first) {
                /* Zap ';' */
                octstr_delete(decoded, pos, 1);
                first = 0;
            } else {
                /* Replace ';' with ',' */
                octstr_set_char(decoded, pos, first ? ' ' : ',');
            }
        }
    }
}

static void unpack_optional_q_value(ParseContext *context, Octstr *decoded)
{
    if (parse_octets_left(context) > 0) {
        Octstr *qval = unpack_q_value(context);
        if (qval) {
            octstr_append(decoded, octstr_imm("; q="));
            octstr_append(decoded, qval);
            octstr_destroy(qval);
        } else
            warning(0, "Bad q-value");
    }
}

/* Date-value is defined in 8.4.2.3. */
Octstr *wsp_unpack_date_value(ParseContext *context)
{
    unsigned long timeval;
    int length;

    length = parse_get_char(context);
    if (length > 30) {
        warning(0, "WSP headers: bad date-value.");
        return NULL;
    }

    timeval = unpack_multi_octet_integer(context, length);
    if (timeval < 0) {
        warning(0, "WSP headers: cannot unpack date-value.");
        return NULL;
    }

    return date_format_http(timeval);
}

/* Accept-general-form is defined in 8.4.2.7 */
Octstr *wsp_unpack_accept_general_form(ParseContext *context)
{
    Octstr *decoded = NULL;
    int ret;
    long val;

    /* The definition for Accept-general-form looks quite complicated,
     * but the "Q-token Q-value" part fits the normal expansion of
     * Parameter, so it simplifies to:
     *  Value-length Media-range *(Parameter)
     * and we've already parsed Value-length.
     */

    /* We use this function to parse content-general-form too,
     * because its definition of Media-type is identical to Media-range.
     */

    ret = wsp_secondary_field_value(context, &val);
    if (parse_error(context) || ret == WSP_FIELD_VALUE_NONE) {
        warning(0, "bad media-range or media-type");
        return NULL;
    }

    if (ret == WSP_FIELD_VALUE_ENCODED) {
        decoded = wsp_content_type_to_string(val);
        if (!decoded) {
            warning(0, "Unknown content type 0x%02lx.", val);
            return NULL;
        }
    } else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
        decoded = parse_get_nul_string(context);
        if (!decoded) {
            warning(0, "Format error in content type");
            return NULL;
        }
    } else {
        panic(0, "Unknown secondary field value type %d.", ret);
    }

    wsp_unpack_all_parameters(context, decoded);
    return decoded;
}

/* Accept-charset-general-form is defined in 8.4.2.8 */
Octstr *wsp_unpack_accept_charset_general_form(ParseContext *context)
{
    Octstr *decoded = NULL;
    int ret;
    long val;

    ret = wsp_secondary_field_value(context, &val);
    if (parse_error(context) || ret == WSP_FIELD_VALUE_NONE) {
        warning(0, "Bad accept-charset-general-form");
        return NULL;
    }

    if (ret == WSP_FIELD_VALUE_ENCODED) {
        decoded = wsp_charset_to_string(val);
        if (!decoded) {
            warning(0, "Unknown character set %04lx.", val);
            return NULL;
        }
    } else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
        decoded = parse_get_nul_string(context);
        if (!decoded) {
            warning(0, "Format error in accept-charset");
            return NULL;
        }
    } else {
        panic(0, "Unknown secondary field value type %d.", ret);
    }

    unpack_optional_q_value(context, decoded);
    return decoded;
}

/* Accept-language-general-form is defined in 8.4.2.10 */
static Octstr *unpack_accept_language_general_form(ParseContext *context)
{
    Octstr *decoded = NULL;
    int ret;
    long val;

    ret = wsp_secondary_field_value(context, &val);
    if (parse_error(context) || ret == WSP_FIELD_VALUE_NONE) {
        warning(0, "Bad accept-language-general-form");
        return NULL;
    }

    if (ret == WSP_FIELD_VALUE_ENCODED) {
        /* Any-language is handled by a special entry in the
         * language table. */
        decoded = wsp_language_to_string(val);
        if (!decoded) {
            warning(0, "Unknown language %02lx.", val);
            return NULL;
        }
    } else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
        decoded = parse_get_nul_string(context);
        if (!decoded) {
            warning(0, "Format error in accept-language");
            return NULL;
        }
    } else {
        panic(0, "Unknown secondary field value type %d.", ret);
    }

    unpack_optional_q_value(context, decoded);
    return decoded;
}

/* Credentials is defined in 8.4.2.5 */
static Octstr *unpack_credentials(ParseContext *context)
{
    Octstr *decoded = NULL;
    int val;

    val = parse_peek_char(context);

    if (val == BASIC_AUTHENTICATION) {
        Octstr *userid, *password;

        parse_skip(context, 1);

        userid = parse_get_nul_string(context);
        password = parse_get_nul_string(context);

        if (parse_error(context)) {
            octstr_destroy(userid);
            octstr_destroy(password);
        } else {
            /* Create the user-pass cookie */
            decoded = octstr_duplicate(userid);
            octstr_append_char(decoded, ':');
            octstr_append(decoded, password);

            /* XXX Deal with cookies that overflow the 76-per-line
             * limit of base64.  Either go through and zap all
             * CR LF sequences, or give the conversion function
             * a flag or something to leave them out. */
            octstr_binary_to_base64(decoded);

            /* Zap the CR LF at the end */
            octstr_delete(decoded, octstr_len(decoded) - 2, 2);

            octstr_insert_data(decoded, 0, "Basic ", 6);

            octstr_destroy(userid);
            octstr_destroy(password);
        }
    } else if (val >= 32 && val < 128) {
        /* Generic authentication scheme */
        decoded = parse_get_nul_string(context);
        if (decoded)
            unpack_broken_parameters(context, decoded);
    }

    if (!decoded)
        warning(0, "Cannot parse credentials.");

    return decoded;
}

/* Credentials is defined in 8.4.2.5 
 * but as Proxy-Authentication is to be used by kannel, 
 * a simplier to parse version is used here */
static Octstr *proxy_unpack_credentials(ParseContext *context)
{
    Octstr *decoded = NULL;
    int val;

    val = parse_peek_char(context);

    if (val == BASIC_AUTHENTICATION) {
        Octstr *userid, *password;

        parse_skip(context, 1);

        userid = parse_get_nul_string(context);
        password = parse_get_nul_string(context);

        if (parse_error(context)) {
            octstr_destroy(userid);
            octstr_destroy(password);
        } else {
            /* Create the user-pass cookie */
            decoded = octstr_duplicate(userid);
            octstr_append_char(decoded, ':');
            octstr_append(decoded, password);
            octstr_destroy(userid);
            octstr_destroy(password);
        }
    } else if (val >= 32 && val < 128) {
        /* Generic authentication scheme */
        decoded = parse_get_nul_string(context);
        if (decoded)
            unpack_broken_parameters(context, decoded);
    }

    if (!decoded)
        warning(0, "Cannot parse credentials.");

    return decoded;
}

/* Challenge is defined in 8.4.2.5 */
static Octstr *unpack_challenge(ParseContext *context)
{
    Octstr *decoded = NULL;
    Octstr *realm_value = NULL;
    int val;

    val = parse_peek_char(context);
    if (val == BASIC_AUTHENTICATION) {
        parse_skip(context, 1);
        realm_value = parse_get_nul_string(context);
        if (realm_value) {
            decoded = octstr_create("Basic realm=\"");
            octstr_append(decoded, realm_value);
            octstr_append_char(decoded, '"');
        }
    } else if (val >= 32 && val < 128) {
        /* Generic authentication scheme */
        decoded = parse_get_nul_string(context);
        realm_value = parse_get_nul_string(context);
        if (decoded && realm_value) {
            octstr_append(decoded,
                          octstr_imm(" realm=\""));
            octstr_append(decoded, realm_value);
            octstr_append_char(decoded, '"');
            if (parse_octets_left(context) > 0) {
                /* Prepare for following parameter list */
                octstr_append_char(decoded, ',');
            }
            unpack_broken_parameters(context, decoded);
        }
    }

    if (!decoded)
        warning(0, "Cannot parse challenge.");

    octstr_destroy(realm_value);
    return decoded;
}

/* Content-range is defined in 8.4.2.23 */
static Octstr *unpack_content_range(ParseContext *context)
{
    /* We'd have to figure out how to access the content range
     * length (i.e. user_data size) from here to parse this,
     * and I don't see why the _client_ would send this in any case. */
    warning(0, "Decoding of content-range not supported");
    return NULL;

    /*
    	Octstr *decoded = NULL;
    	unsigned long first_byte_pos, entity_length;
    	unsigned long last_byte_pos;
     
    	first_byte_pos = parse_get_uintvar(context);
    	entity_length = parse_get_uintvar(context);
     
    	if (parse_error(context)) {
    		warning(0, "Cannot parse content-range header");
    		return NULL;
    	}
     
    	decoded = octstr_create("bytes ");
    	octstr_append_decimal(decoded, first_byte_pos);
    	octstr_append_char(decoded, '-');
    	octstr_append_decimal(decoded, last_byte_pos);
    	octstr_append_char(decoded, '/');
    	octstr_append_decimal(decoded, entity_length);
     
    	return decoded;
    */
}

/* Field-name is defined in 8.4.2.6 */
static Octstr *unpack_field_name(ParseContext *context)
{
    Octstr *decoded = NULL;
    int ret;
    int val;

    ret = wsp_field_value(context, &val);
    if (parse_error(context) || ret == WSP_FIELD_VALUE_DATA) {
        warning(0, "Bad field-name encoding");
        return NULL;
    }

    if (ret == WSP_FIELD_VALUE_ENCODED) {
        decoded = wsp_header_to_string(val);
        if (!decoded) {
            warning(0, "Unknown field-name 0x%02x.", val);
            return NULL;
        }
    } else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
        decoded = parse_get_nul_string(context);
        if (!decoded) {
            warning(0, "Bad field-name encoding");
            return NULL;
        }
    } else {
        panic(0, "Unknown field value type %d.", ret);
    }

    return decoded;
}

/* Cache-directive is defined in 8.4.2.15 */
static Octstr *unpack_cache_directive(ParseContext *context)
{
    Octstr *decoded = NULL;
    int ret;
    int val;

    ret = wsp_field_value(context, &val);
    if (parse_error(context) || ret == WSP_FIELD_VALUE_DATA) {
        warning(0, "Bad cache-directive");
        goto error;
    }

    if (ret == WSP_FIELD_VALUE_ENCODED) {
        decoded = wsp_cache_control_to_string(val);
        if (!decoded) {
            warning(0, "Bad cache-directive 0x%02x.", val);
            goto error;
        }
        octstr_append_char(decoded, '=');
        switch (val) {
        case WSP_CACHE_CONTROL_NO_CACHE:
        case WSP_CACHE_CONTROL_PRIVATE:
            if (parse_octets_left(context) == 0) {
                warning(0, "Too short cache-directive");
                goto error;
            }
            octstr_append_char(decoded, '"');
            do {
                Octstr *fieldname = unpack_field_name(context);
                if (!fieldname) {
                    warning(0, "Bad field name in cache directive");
                    goto error;
                }
                octstr_append(decoded, fieldname);
                octstr_destroy(fieldname);
                if (parse_octets_left(context) > 0) {
                    octstr_append_char(decoded, ',');
                    octstr_append_char(decoded, ' ');
                }
            } while (parse_octets_left(context) > 0 &&
                     !parse_error(context));
            octstr_append_char(decoded, '"');
            break;
        case WSP_CACHE_CONTROL_MAX_AGE:
        case WSP_CACHE_CONTROL_MAX_STALE:
        case WSP_CACHE_CONTROL_MIN_FRESH:
            {
                Octstr *seconds;
                seconds = wsp_unpack_integer_value(context);
                if (!seconds) {
                    warning(0, "Bad integer value in cache directive");
                    goto error;
                }
                octstr_append(decoded, seconds);
                octstr_destroy(seconds);
            }
            break;
        default:
            warning(0, "Unexpected value 0x%02x in cache directive.", val);
            break;
        }
    } else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
        /* XXX: WSP grammar seems wrong here.  It works out
         * to Token-text followed by Parameter.  But the
         * grammar in RFC2616 works out to a key = value
         * pair, i.e. only a Parameter. */
        decoded = parse_get_nul_string(context);
        if (!decoded) {
            warning(0, "Format error in cache-control.");
            return NULL;
        }
        /* Yes, the grammar allows only one */
        unpack_parameter(context, decoded);
    } else {
        panic(0, "Unknown field value type %d.", ret);
    }

    return decoded;

error:
    octstr_destroy(decoded);
    return NULL;
}

/* Retry-after is defined in 8.4.2.44 */
static Octstr *unpack_retry_after(ParseContext *context)
{
    int selector;

    selector = parse_get_char(context);
    if (selector == ABSOLUTE_TIME) {
        return wsp_unpack_date_value(context);
    } else if (selector == RELATIVE_TIME) {
        return wsp_unpack_integer_value(context);
    } else {
        warning(0, "Cannot parse retry-after value.");
        return NULL;
    }
}

/* Disposition is defined in 8.4.2.53 */
static Octstr *unpack_disposition(ParseContext *context)
{
    Octstr *decoded = NULL;
    int selector;

    selector = parse_get_char(context) - 128;
    decoded = wsp_disposition_to_string(selector);
    if (!decoded) {
        warning(0, "Cannot parse content-disposition value.");
        return NULL;
    }
    wsp_unpack_all_parameters(context, decoded);
    return decoded;
}

/* Range-value is defined in 8.4.2.42 */
static Octstr *unpack_range_value(ParseContext *context)
{
    Octstr *decoded = NULL;
    int selector;
    unsigned long first_byte_pos, last_byte_pos, suffix_length;

    selector = parse_get_char(context);
    if (selector == BYTE_RANGE) {
        first_byte_pos = parse_get_uintvar(context);
        if (parse_error(context))
            goto error;

        decoded = octstr_create("bytes = ");
        octstr_append_decimal(decoded, first_byte_pos);
        octstr_append_char(decoded, '-');

        last_byte_pos = parse_get_uintvar(context);
        if (parse_error(context)) {
            /* last_byte_pos is optional */
            parse_clear_error(context);
        } else {
            octstr_append_decimal(decoded, last_byte_pos);
        }
    } else if (selector == SUFFIX_BYTE_RANGE) {
        suffix_length = parse_get_uintvar(context);
        if (parse_error(context))
            goto error;

        decoded = octstr_create("bytes = -");
        octstr_append_decimal(decoded, suffix_length);
    } else {
        goto error;
    }

    return decoded;

error:
    warning(0, "Bad format for range-value.");
    octstr_destroy(decoded);
    return NULL;
}

/* Warning-value is defined in 8.4.2.51 */
static Octstr *unpack_warning_value(ParseContext *context)
{
    Octstr *decoded = NULL;
    Octstr *warn_code = NULL;
    Octstr *warn_agent = NULL;
    Octstr *warn_text = NULL;
    unsigned char quote = '"';

    warn_code = wsp_unpack_integer_value(context);

    warn_agent = parse_get_nul_string(context);
    if (warn_agent && octstr_get_char(warn_agent, 0) == WSP_QUOTE)
        octstr_delete(warn_agent, 0, 1);

    warn_text = parse_get_nul_string(context);
    if (warn_text && octstr_get_char(warn_text, 0) == WSP_QUOTE)
        octstr_delete(warn_text, 0, 1);
    if (octstr_get_char(warn_text, 0) != quote)
        octstr_insert_data(warn_text, 0, (char *)&quote, 1);
    if (octstr_get_char(warn_text, octstr_len(warn_text) - 1) != quote)
        octstr_append_char(warn_text, quote);

    if (parse_error(context) || !warn_agent || !warn_text)
        goto error;

    decoded = octstr_create("");
    octstr_append(decoded, warn_code);
    octstr_append_char(decoded, ' ');
    octstr_append(decoded, warn_agent);
    octstr_append_char(decoded, ' ');
    octstr_append(decoded, warn_text);

    octstr_destroy(warn_agent);
    octstr_destroy(warn_code);
    octstr_destroy(warn_text);
    return decoded;

error:
    warning(0, "Bad format for warning-value.");
    octstr_destroy(warn_agent);
    octstr_destroy(warn_code);
    octstr_destroy(warn_text);
    octstr_destroy(decoded);
    return NULL;
}

void wsp_unpack_well_known_field(List *unpacked, int field_type,
                                 ParseContext *context)
{
    int val, ret;
    unsigned char *headername = NULL;
    unsigned char *ch = NULL;
    Octstr *decoded = NULL;

    ret = wsp_field_value(context, &val);
    if (parse_error(context)) {
        warning(0, "Faulty header, skipping remaining headers.");
        parse_skip_to_limit(context);
        return;
    }

    headername = wsp_header_to_cstr(field_type);
    /* headername can still be NULL.  This is checked after parsing
     * the field value.  We want to parse the value before exiting,
     * so that we are ready for the next header. */

    /* The following code must set "ch" or "decoded" to a non-NULL
     * value if the header is valid. */

    if (ret == WSP_FIELD_VALUE_NUL_STRING) {
        /* We allow any header to have a text value, even if that
         * is not defined in the grammar.  Be generous in what
         * you accept, etc. */
        /* This covers Text-string, Token-Text, and Uri-value rules */
        decoded = parse_get_nul_string(context);
    } else if (ret == WSP_FIELD_VALUE_ENCODED) {
        switch (field_type) {
        case WSP_HEADER_ACCEPT:
        case WSP_HEADER_CONTENT_TYPE:
            ch = wsp_content_type_to_cstr(val);
            if (!ch)
                warning(0, "Unknown content type 0x%02x.", val);
            break;

        case WSP_HEADER_ACCEPT_CHARSET:
            ch = wsp_charset_to_cstr(val);
            if (!ch)
                warning(0, "Unknown charset 0x%02x.", val);
            break;

        case WSP_HEADER_ACCEPT_ENCODING:
        case WSP_HEADER_CONTENT_ENCODING:
            ch = wsp_encoding_to_cstr(val);
            if (!ch)
                warning(0, "Unknown encoding 0x%02x.", val);
            break;

        case WSP_HEADER_ACCEPT_LANGUAGE:
        case WSP_HEADER_CONTENT_LANGUAGE:
            ch = wsp_language_to_cstr(val);
            if (!ch)
                warning(0, "Unknown language 0x%02x.", val);
            break;

        case WSP_HEADER_ACCEPT_RANGES:
            ch = wsp_ranges_to_cstr(val);
            if (!ch)
                warning(0, "Unknown ranges value 0x%02x.", val);
            break;

        case WSP_HEADER_AGE:
        case WSP_HEADER_CONTENT_LENGTH:
        case WSP_HEADER_MAX_FORWARDS:
            /* Short-integer version of Integer-value */
            decoded = octstr_create("");
            octstr_append_decimal(decoded, val);
            break;

        case WSP_HEADER_ALLOW:
        case WSP_HEADER_PUBLIC:
            ch = wsp_method_to_cstr(val);
            if (!ch) {
                /* FIXME Support extended methods */
                warning(0, "Unknown method 0x%02x.", val);
            }
            break;

        case WSP_HEADER_CACHE_CONTROL:
        case WSP_HEADER_CACHE_CONTROL_V13:
        case WSP_HEADER_CACHE_CONTROL_V14:
            ch = wsp_cache_control_to_cstr(val);
            if (!ch)
                warning(0, "Unknown cache-control value 0x%02x.", val);
            break;

        case WSP_HEADER_CONNECTION:
            ch = wsp_connection_to_cstr(val);
            if (!ch)
                warning(0, "Unknown connection value 0x%02x.", val);
            break;


        case WSP_HEADER_PRAGMA:
            if (val == 0)
                ch = (unsigned char *)"no-cache";
            else
                warning(0, "Unknown pragma value 0x%02x.", val);
            break;

        case WSP_HEADER_TRANSFER_ENCODING:
            ch = wsp_transfer_encoding_to_cstr(val);
            if (!ch)
                warning(0, "Unknown transfer encoding value 0x%02x.", val);
            break;

        case WSP_HEADER_VARY:
            ch = wsp_header_to_cstr(val);
            if (!ch)
                warning(0, "Unknown Vary field name 0x%02x.", val);
            break;

        case WSP_HEADER_WARNING:
            decoded = octstr_create("");
            octstr_append_decimal(decoded, val);
            break;

        case WSP_HEADER_BEARER_INDICATION:
             ch = wsp_bearer_indication_to_cstr(val);
             if (!ch)
                 warning(0, "Unknown Bearer-Indication field name 0x%02x.", val);
             break;

        case WSP_HEADER_ACCEPT_APPLICATION:
             ch = wsp_application_id_to_cstr(val);
             if (!ch)
                 warning(0, "Unknown Accept-Application field name 0x%02x.", val);
             break;

        default:
            if (headername) {
                warning(0, "Did not expect short-integer with "
                        "'%s' header, skipping.", headername);
            }
            break;
        }
    } else if (ret == WSP_FIELD_VALUE_DATA) {
        switch (field_type) {
        case WSP_HEADER_ACCEPT:
        case WSP_HEADER_CONTENT_TYPE:
            /* Content-general-form and Accept-general-form
             * are defined separately in WSP, but their
             * definitions are equivalent. */
            decoded = wsp_unpack_accept_general_form(context);
            break;

        case WSP_HEADER_ACCEPT_CHARSET:
            decoded = wsp_unpack_accept_charset_general_form(context);
            break;

        case WSP_HEADER_ACCEPT_LANGUAGE:
            decoded = unpack_accept_language_general_form(context);
            break;

        case WSP_HEADER_AGE:
        case WSP_HEADER_CONTENT_LENGTH:
        case WSP_HEADER_MAX_FORWARDS:
        case WSP_HEADER_BEARER_INDICATION:
        case WSP_HEADER_ACCEPT_APPLICATION:
            /* Long-integer version of Integer-value */
            {
                long l = unpack_multi_octet_integer(context,
                                                    parse_octets_left(context));
                decoded = octstr_create("");
                octstr_append_decimal(decoded, l);
            }
            break;

        case WSP_HEADER_AUTHORIZATION:
            decoded = unpack_credentials(context);
            break;

        case WSP_HEADER_PROXY_AUTHORIZATION:
            decoded = proxy_unpack_credentials(context);
            break;

        case WSP_HEADER_CACHE_CONTROL:
            decoded = unpack_cache_directive(context);
            break;

        case WSP_HEADER_CONTENT_MD5:
            decoded = parse_get_octets(context,
                                       parse_octets_left(context));
            octstr_binary_to_base64(decoded);
            /* Zap the CR LF sequence at the end */
            octstr_delete(decoded, octstr_len(decoded) - 2, 2);
            break;

        case WSP_HEADER_CONTENT_RANGE:
            decoded = unpack_content_range(context);
            break;

        case WSP_HEADER_DATE:
        case WSP_HEADER_EXPIRES:
        case WSP_HEADER_IF_MODIFIED_SINCE:
        case WSP_HEADER_IF_RANGE:
        case WSP_HEADER_IF_UNMODIFIED_SINCE:
        case WSP_HEADER_LAST_MODIFIED:
            /* Back up to get the length byte again */
            parse_skip(context, -1);
            decoded = wsp_unpack_date_value(context);
            break;

        case WSP_HEADER_PRAGMA:
            /* The value is a bare Parameter, without a preceding
             * header body.  unpack_parameter wasn't really
             * designed for this.  We work around it here. */
            decoded = octstr_create("");
            if (unpack_parameter(context, decoded) < 0) {
                octstr_destroy(decoded);
                decoded = NULL;
            } else {
                /* Remove the leading "; " */
                octstr_delete(decoded, 0, 2);
            }
            break;

        case WSP_HEADER_PROXY_AUTHENTICATE:
        case WSP_HEADER_WWW_AUTHENTICATE:
            decoded = unpack_challenge(context);
            break;

        case WSP_HEADER_RANGE:
            decoded = unpack_range_value(context);
            break;

        case WSP_HEADER_RETRY_AFTER:
            decoded = unpack_retry_after(context);
            break;

        case WSP_HEADER_WARNING:
            decoded = unpack_warning_value(context);
            break;

        case WSP_HEADER_CONTENT_DISPOSITION:
            decoded = unpack_disposition(context);
            break;

        case WSP_HEADER_ENCODING_VERSION:
            decoded = unpack_encoding_version(context);
            break;

        default:
            if (headername) {
                warning(0, "Did not expect value-length with "
                        "'%s' header, skipping.", headername);
            }
            break;
        }
        if (headername && parse_octets_left(context) > 0) {
            warning(0, "WSP: %s: skipping %ld trailing octets.",
                    headername, parse_octets_left(context));
        }
        parse_skip_to_limit(context);
        parse_pop_limit(context);
    } else {
        panic(0, "Unknown field-value type %d.", ret);
    }

    if (ch == NULL && decoded != NULL)
        ch = (unsigned char *)octstr_get_cstr(decoded);
    if (ch == NULL)
        goto value_error;

    if (!headername) {
        warning(0, "Unknown header number 0x%02x.", field_type);
        goto value_error;
    }

    http_header_add(unpacked, (char *)headername,(char *) ch);
    octstr_destroy(decoded);
    return;

value_error:
    warning(0, "Skipping faulty header.");
    octstr_destroy(decoded);
}

void wsp_unpack_app_header(List *unpacked, ParseContext *context)
{
    Octstr *header = NULL;
    Octstr *value = NULL;

    header = parse_get_nul_string(context);
    value = parse_get_nul_string(context);

    if (header && value) {
        http_header_add(unpacked, octstr_get_cstr(header),
                        octstr_get_cstr(value));
    }

    if (parse_error(context))
        warning(0, "Error parsing application-header.");

    octstr_destroy(header);
    octstr_destroy(value);
}

List *wsp_headers_unpack(Octstr *headers, int content_type_present)
{
    ParseContext *context;
    int byte;
    List *unpacked;
    int code_page;

    unpacked = http_create_empty_headers();
    context = parse_context_create(headers);

    if (octstr_len(headers) > 0) {
        debug("wsp", 0, "WSP: decoding headers:");
        octstr_dump(headers, 0);
    }

    if (content_type_present)
        wsp_unpack_well_known_field(unpacked,
                                    WSP_HEADER_CONTENT_TYPE, context);

    code_page = 1;   /* default */

    while (parse_octets_left(context) > 0 && !parse_error(context)) {
        byte = parse_get_char(context);

        if (byte == 127 || (byte >= 1 && byte <= 31)) {
            if (byte == 127)
                code_page = parse_get_char(context);
            else
                code_page = byte;
            if (code_page == 1)
                info(0, "Returning to code page 1 (default).");
            else {
                warning(0, "Shift to unknown code page %d.",
                        code_page);
                warning(0, "Will try to skip headers until "
                        "next known code page.");
            }
        } else if (byte >= 128) {  /* well-known-header */
            if (code_page == 1)
                wsp_unpack_well_known_field(unpacked, byte - 128, context);
            else {
                debug("wsp", 0, "Skipping field 0x%02x.", byte);
                wsp_skip_field_value(context);
            }
        } else if (byte > 31 && byte < 127) {
            /* Un-parse the character we just read */
            parse_skip(context, -1);
            wsp_unpack_app_header(unpacked, context);
        } else {
            warning(0, "Unsupported token or header (start 0x%x)", byte);
            break;
        }
    }

    if (gwlist_len(unpacked) > 0) {
        long i;

        debug("wsp", 0, "WSP: decoded headers:");
        for (i = 0; i < gwlist_len(unpacked); i++) {
            Octstr *header = gwlist_get(unpacked, i);
            debug("wsp", 0, "%s", octstr_get_cstr(header));
        }
        debug("wsp", 0, "WSP: End of decoded headers.");
    }

    parse_context_destroy(context);
    return unpacked;
}


/**********************************************************************/
/* Start of header packing code (HTTP to WSP)                         */
/**********************************************************************/

static int pack_accept(Octstr *packet, Octstr *value);
static int pack_accept_charset(Octstr *packet, Octstr *value);
static int pack_accept_encoding(Octstr *packet, Octstr *value);
static int pack_accept_language(Octstr *packet, Octstr *value);
static int pack_cache_control(Octstr *packet, Octstr *value);
static int pack_challenge(Octstr *packet, Octstr *value);
static int pack_connection(Octstr *packet, Octstr *value);
static int pack_content_disposition(Octstr *packet, Octstr *value);
static int pack_content_range(Octstr *packet, Octstr *value);
static int pack_credentials(Octstr *packet, Octstr *value);
static int pack_encoding(Octstr *packet, Octstr *value);
static int pack_expires(Octstr *packet, Octstr *value);
static int pack_field_name(Octstr *packet, Octstr *value);
static int pack_if_range(Octstr *packet, Octstr *value);
static int pack_language(Octstr *packet, Octstr *value);
static int pack_md5(Octstr *packet, Octstr *value);
static int pack_method(Octstr *packet, Octstr *value);
static int pack_pragma(Octstr *packet, Octstr *value);
static int pack_range(Octstr *packet, Octstr *value);
static int pack_range_unit(Octstr *packet, Octstr *value);
static int pack_transfer_encoding(Octstr *packet, Octstr *value);
static int pack_uri(Octstr *packet, Octstr *value);
static int pack_warning(Octstr *packet, Octstr *value);



/* these are used in MMS encapsulation code too */

struct headerinfo headerinfo[] =
    {
        { WSP_HEADER_ACCEPT, pack_accept, LIST },
        { WSP_HEADER_ACCEPT_CHARSET, pack_accept_charset, LIST },
        { WSP_HEADER_ACCEPT_ENCODING, pack_accept_encoding, LIST },
        { WSP_HEADER_ACCEPT_LANGUAGE, pack_accept_language, LIST },
        { WSP_HEADER_ACCEPT_RANGES, pack_range_unit, LIST },
        { WSP_HEADER_AGE, wsp_pack_integer_string, 0 },
        /* pack_method is slightly too general because Allow is only
         * supposed to encode well-known-methods. */
        { WSP_HEADER_ALLOW, pack_method, LIST },
        { WSP_HEADER_AUTHORIZATION, pack_credentials, BROKEN_LIST },
        { WSP_HEADER_CACHE_CONTROL, pack_cache_control, LIST },
        { WSP_HEADER_CACHE_CONTROL_V13, pack_cache_control, LIST },
        { WSP_HEADER_CACHE_CONTROL_V14, pack_cache_control, LIST },
        { WSP_HEADER_CONNECTION, pack_connection, LIST },
        { WSP_HEADER_CONTENT_BASE, pack_uri, 0 },
        { WSP_HEADER_CONTENT_ENCODING, pack_encoding, LIST },
        { WSP_HEADER_CONTENT_LANGUAGE, pack_language, LIST },
        { WSP_HEADER_CONTENT_LENGTH, wsp_pack_integer_string, 0 },
        { WSP_HEADER_CONTENT_LOCATION, pack_uri, 0 },
        { WSP_HEADER_CONTENT_MD5, pack_md5, 0 },
        { WSP_HEADER_CONTENT_RANGE, pack_content_range, 0 },
        { WSP_HEADER_CONTENT_TYPE, wsp_pack_content_type, 0 },
        { WSP_HEADER_DATE, wsp_pack_date, 0 },
        { WSP_HEADER_ETAG, wsp_pack_quoted_text, 0 },
        { WSP_HEADER_EXPIRES, pack_expires, 0 },
        { WSP_HEADER_FROM, wsp_pack_text, 0 },
        { WSP_HEADER_HOST, wsp_pack_text, 0 },
        { WSP_HEADER_IF_MODIFIED_SINCE, wsp_pack_date, 0 },
        { WSP_HEADER_IF_MATCH, wsp_pack_quoted_text, 0 },
        { WSP_HEADER_IF_NONE_MATCH, wsp_pack_quoted_text, 0 },
        { WSP_HEADER_IF_RANGE, pack_if_range, 0 },
        { WSP_HEADER_IF_UNMODIFIED_SINCE, wsp_pack_date, 0 },
        { WSP_HEADER_LAST_MODIFIED, wsp_pack_date, 0 },
        { WSP_HEADER_LOCATION, pack_uri, 0 },
        { WSP_HEADER_MAX_FORWARDS, wsp_pack_integer_string, 0 },
        { WSP_HEADER_PRAGMA, pack_pragma, LIST },
        { WSP_HEADER_PROXY_AUTHENTICATE, pack_challenge, BROKEN_LIST },
        { WSP_HEADER_PROXY_AUTHORIZATION, pack_credentials, BROKEN_LIST },
        { WSP_HEADER_PUBLIC, pack_method, LIST },
        { WSP_HEADER_RANGE, pack_range, 0 },
        { WSP_HEADER_REFERER, pack_uri, 0 },
        { WSP_HEADER_RETRY_AFTER, wsp_pack_retry_after, 0 },
        { WSP_HEADER_SERVER, wsp_pack_text, 0 },
        { WSP_HEADER_TRANSFER_ENCODING, pack_transfer_encoding, LIST },
        { WSP_HEADER_UPGRADE, wsp_pack_text, LIST },
        { WSP_HEADER_USER_AGENT, wsp_pack_text, 0 },
        { WSP_HEADER_VARY, pack_field_name, LIST },
        { WSP_HEADER_VIA, wsp_pack_text, LIST },
        { WSP_HEADER_WARNING, pack_warning, LIST },
        { WSP_HEADER_WWW_AUTHENTICATE, pack_challenge, BROKEN_LIST },
        { WSP_HEADER_CONTENT_DISPOSITION, pack_content_disposition, 0 },
        { WSP_HEADER_PUSH_FLAG, wsp_pack_integer_string, 0},
        { WSP_HEADER_X_WAP_CONTENT_URI, pack_uri, 0},
        { WSP_HEADER_X_WAP_INITIATOR_URI, pack_uri, 0},
        { WSP_HEADER_X_WAP_APPLICATION_ID, wsp_pack_integer_string, 0},
        { WSP_HEADER_CONTENT_ID, wsp_pack_quoted_text, 0},
        { WSP_HEADER_ENCODING_VERSION, wsp_pack_version_value, 0 }
        // DAVI { WSP_HEADER_SET_COOKIE, pack_version_value, 0 }
    };

static Parameter *parm_create(Octstr *key, Octstr *value)
{
    Parameter *parm;

    parm = gw_malloc(sizeof(*parm));
    parm->key = key;
    parm->value = value;
    return parm;
}

static void parm_destroy(Parameter *parm)
{
    if (parm == NULL)
        return;

    octstr_destroy(parm->key);
    octstr_destroy(parm->value);
    gw_free(parm);
}

void parm_destroy_item(void *parm)
{
    parm_destroy(parm);
}

static Parameter *parm_parse(Octstr *value)
{
    long pos;
    Octstr *key, *val;

    pos = octstr_search_char(value, '=', 0);
    if (pos > 0) {
        key = octstr_copy(value, 0, pos);
        val = octstr_copy(value, pos + 1, octstr_len(value) - pos);
        octstr_strip_blanks(key);
        octstr_strip_blanks(val);
    } else {
        key = octstr_duplicate(value);
        val = NULL;
    }

    return parm_create(key, val);
}

/* Many HTTP field elements can take parameters in a standardized
 * form: parameters appear after the main value, each is introduced
 * by a semicolon (;), and consists of a key=value pair or just
 * a key, where the key is a token and the value is either a token
 * or a quoted-string.
 * The main value itself is a series of tokens, separators, and
 * quoted-strings.
 *
 * This function will take such a field element, and remove all
 * parameters from it.  The parameters are returned as a List
 * of Parameter, where the value field is left NULL
 * if the parameter was just a key.
 * It returns NULL if there were no parameters.
 */
List *wsp_strip_parameters(Octstr *value)
{
    long pos;
    long len;
    int c;
    long end;
    List *parms;
    long firstparm;

    len = octstr_len(value);
    /* Find the start of the first parameter. */
    for (pos = 0; pos < len; pos++) {
        c = octstr_get_char(value, pos);
        if (c == ';')
            break;
        else if (c == '"')
            pos += http_header_quoted_string_len(value, pos) - 1;
    }

    if (pos >= len)
        return NULL;   /* no parameters */

    parms = gwlist_create();
    firstparm = pos;

    for (pos++; pos > 0 && pos < len; pos++) {
        Octstr *key = NULL;
        Octstr *val = NULL;

        end = octstr_search_char(value, '=', pos);
        if (end < 0)
            end = octstr_search_char(value, ';', pos);
        if (end < 0)
            end = octstr_len(value);
        key = octstr_copy(value, pos, end - pos);
        octstr_strip_blanks(key);
        pos = end;

        if (octstr_get_char(value, pos) == '=') {
            pos++;
            while (isspace(octstr_get_char(value, pos)))
                pos++;
            if (octstr_get_char(value, pos) == '"')
                end = pos + http_header_quoted_string_len(value, pos);
            else
                end = octstr_search_char(value, ';', pos);
            if (end < 0)
                end = octstr_len(value);
            val = octstr_copy(value, pos, end - pos);
            octstr_strip_blanks(val);
            pos = end;
            pos = octstr_search_char(value, ';', pos);
        }

        gwlist_append(parms, parm_create(key, val));
    }

    octstr_delete(value, firstparm, octstr_len(value) - firstparm);
    octstr_strip_blanks(value);
    return parms;
}

int wsp_pack_text(Octstr *packed, Octstr *text)
{
    /* This check catches 0-length strings as well, because
     * octstr_get_char will return -1. */
    if (octstr_get_char(text, 0) >= 128 || octstr_get_char(text, 0) < 32)
        octstr_append_char(packed, WSP_QUOTE);
    octstr_append(packed, text);
    octstr_append_char(packed, 0);
    return 0;
}

/* Pack a string as quoted-text WAP WSP 203, Section 8.4.2.1 */
int wsp_pack_quoted_text(Octstr *packed, Octstr *text)
{
     octstr_append_char(packed, '"');
     octstr_append(packed,text);
     octstr_append_char(packed,0);
     return 0;
}

/* Pack text as Quoted-string if it starts with a " character.
 * Pack it as Text-string otherwise. */
static void pack_quoted_string(Octstr *packed, Octstr *text)
{
    octstr_append(packed, text);
    if (octstr_get_char(text, octstr_len(text) - 1) == '"' &&
        octstr_get_char(text, 0) == '"')
        octstr_delete(packed, octstr_len(packed) - 1, 1);
    octstr_append_char(packed, 0);
}

/* Is this char in the 'separators' set defined by HTTP? */
static int is_separator_char(int c)
{
    switch (c) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '@':
    case ',':
    case ';':
    case ':':
    case '\\':
    case '"':
    case '/':
    case '[':
    case ']':
    case '?':
    case '=':
    case '{':
    case '}':
    case 32:  /* SP */
    case 9:   /* HT */
        return 1;
    default:
        return 0;
    }
}

/* Is this char part of a 'token' as defined by HTTP? */
static int is_token_char(int c)
{
    return c >= 32 && c < 127 && !is_separator_char(c);
}

/* Is this string a 'token' as defined by HTTP? */
static int is_token(Octstr *token)
{
    return octstr_len(token) > 0 &&
           octstr_check_range(token, 0, octstr_len(token), is_token_char);
}

/* We represent qvalues as integers from 0 through 1000, rather than
 * as floating values. */
static int parse_qvalue(Octstr *value)
{
    int qvalue;

    if (value == NULL)
        return -1;

    if (!isdigit(octstr_get_char(value, 0)))
        return -1;

    qvalue = (octstr_get_char(value, 0) - '0') * 1000;

    if (octstr_get_char(value, 1) != '.')
        goto gotvalue;

    if (!isdigit(octstr_get_char(value, 2)))
        goto gotvalue;
    qvalue += (octstr_get_char(value, 2) - '0') * 100;


    if (!isdigit(octstr_get_char(value, 3)))
        goto gotvalue;
    qvalue += (octstr_get_char(value, 3) - '0') * 10;

    if (!isdigit(octstr_get_char(value, 4)))
        goto gotvalue;
    qvalue += (octstr_get_char(value, 4) - '0');

gotvalue:
    if (qvalue < 0 || qvalue > 1000)
        return -1;

    return qvalue;
}

static int get_qvalue(List *parms, int default_qvalue)
{
    long i;
    Parameter *parm;
    int qvalue;

    for (i = 0; i < gwlist_len(parms); i++) {
        parm = gwlist_get(parms, i);
        if (octstr_str_compare(parm->key, "q") == 0 ||
            octstr_str_compare(parm->key, "Q") == 0) {
            qvalue = parse_qvalue(parm->value);
            if (qvalue >= 0)
                return qvalue;
        }
    }

    return default_qvalue;
}

static int pack_qvalue(Octstr *packed, int qvalue)
{
    /* "Quality factor 1 is the default value and shall never
     * be sent." */
    if (qvalue == 1000)
        return -1;

    /* Remember that our qvalues are already multiplied by 1000. */
    if (qvalue % 10 == 0)
        qvalue = qvalue / 10 + 1;
    else
        qvalue = qvalue + 100;
    octstr_append_uintvar(packed, qvalue);
    return 0;
}

/* Pack value as a Value-length followed by the encoded value. */
void wsp_pack_value(Octstr *packed, Octstr *encoded)
{
    long len;

    len = octstr_len(encoded);
    if (len <= 30)
        octstr_append_char(packed, len);
    else {
        octstr_append_char(packed, 31);
        octstr_append_uintvar(packed, len);
    }

    octstr_append(packed, encoded);
}

void wsp_pack_long_integer(Octstr *packed, unsigned long integer)
{
    long oldlen = octstr_len(packed);
    unsigned char octet;
    long len;

    if (integer == 0) {
	/* The Multi-octet-integer has to be at least 1 octet long. */
	octstr_append_char(packed, 1); /* length */
	octstr_append_char(packed, 0); /* value */
	return;
    }

    /* Encode it back-to-front, by repeatedly inserting
     * at the same position, because that's easier. */
    for (len = 0; integer != 0; integer >>= 8, len++) {
        octet = integer & 0xff;
        octstr_insert_data(packed, oldlen, (char *)&octet, 1);
    }

    octet = len;
    octstr_insert_data(packed, oldlen, (char *)&octet, 1);
}

void wsp_pack_short_integer(Octstr *packed, unsigned long integer)
{
    gw_assert(integer <= MAX_SHORT_INTEGER);

    octstr_append_char(packed, integer + 0x80);
}

void wsp_pack_integer_value(Octstr *packed, unsigned long integer)
{
    if (integer <= MAX_SHORT_INTEGER)
        wsp_pack_short_integer(packed, integer);
    else
        wsp_pack_long_integer(packed, integer);
}

int wsp_pack_integer_string(Octstr *packed, Octstr *value)
{
    unsigned long integer;
    long pos;
    int c;
    int digit;

    integer = 0;
    for (pos = 0; pos < octstr_len(value); pos++) {
        c = octstr_get_char(value, pos);
        if (!isdigit(c))
            break;
        digit = c - '0';
        if (integer > ULONG_MAX / 10)
            goto overflow;
        integer *= 10;
        if (integer > ULONG_MAX - digit)
            goto overflow;
        integer += digit;
    }

    wsp_pack_integer_value(packed, integer);
    return 0;

overflow:
    warning(0, "WSP: Number too large to handle: '%s'.",
            octstr_get_cstr(value));
    return -1;
}


int wsp_pack_version_value(Octstr *packed, Octstr *version)
{
    long major, minor;
    long pos;

    pos = octstr_parse_long(&major, version, 0, 10);
    if (pos < 0 || major < 1 || major > 7)
        goto usetext;

    if (pos == octstr_len(version))
        minor = 15;
    else {
        if (octstr_get_char(version, pos) != '.')
            goto usetext;
        pos = octstr_parse_long(&minor, version, pos + 1, 10);
        if (pos != octstr_len(version) || minor < 0 || minor > 14)
            goto usetext;
    }

    wsp_pack_short_integer(packed, major << 4 | minor);
    return 0;

usetext:
    wsp_pack_text(packed, version);
    return 0;
}

int wsp_pack_constrained_value(Octstr *packed, Octstr *text, long value)
{
    if (value >= 0)
        wsp_pack_short_integer(packed, value);
    else
        wsp_pack_text(packed, text);
    return 0;
}

static void pack_parameter(Octstr *packed, Parameter *parm)
{
    long keytoken;
    long tmp;
    long start;

    start = octstr_len(packed);

    /* Parameter = Typed-parameter | Untyped-parameter */
    /* keytoken = wsp_string_to_parameter(parm->key); */
    /* XXX this should obey what kind of WSP Encoding-Version the client is using */
    keytoken = wsp_string_to_versioned_parameter(parm->key, WSP_1_2);
   
    if (keytoken >= 0) {
        /* Typed-parameter = Well-known-parameter-token Typed-value */
        /* Well-known-parameter-token = Integer-value */
        wsp_pack_integer_value(packed, keytoken);
        /* Typed-value = Compact-value | Text-value */
        /* First try to pack as Compact-value or No-value.
         * If that fails, pack as Text-value. */
        if (parm->value == NULL) {
            octstr_append_char(packed, 0);  /* No-value */
            return;
        } else switch (keytoken) {
            case 0:   /* q */
                tmp = parse_qvalue(parm->value);
                if (tmp >= 0) {
                    if (pack_qvalue(packed, tmp) < 0)
                        octstr_delete(packed, start,
                                      octstr_len(packed) - start);
                    return;
                }
                break;
            case 1:  /* charset */
                tmp = wsp_string_to_charset(parm->value);
                if (tmp >= 0) {
                    wsp_pack_integer_value(packed, tmp);
                    return;
                }
                break;
            case 2:  /* level */
                wsp_pack_version_value(packed, parm->value);
                return;
            case 3:  /* type */
                if (octstr_check_range(parm->value, 0,
                                       octstr_len(parm->value), 
				       gw_isdigit) &&
                    wsp_pack_integer_string(packed, parm->value) >= 0)
                    return;
                break;
            case 5:  /* name */
            case 6:  /* filename */
                break;
            case 7:  /* differences */
                if (pack_field_name(packed, parm->value) >= 0)
                    return;
                break;
            case 8:  /* padding */
                if (octstr_parse_long(&tmp, parm->value, 0, 10)
                    == octstr_len(parm->value) &&
                    tmp >= 0 && tmp <= MAX_SHORT_INTEGER) {
                    wsp_pack_short_integer(packed, tmp);
                    return;
                }
                break;
            }
        pack_quoted_string(packed, parm->value);
    } else {
        /* Untyped-parameter = Token-text Untyped-value */
        wsp_pack_text(packed, parm->key);
        /* Untyped-value = Integer-value | Text-value */
        if (parm->value == NULL) {
            octstr_append_char(packed, 0);  /* No-value */
            return;
        }
        /* If we can pack as integer, do so. */
        if (octstr_parse_long(&tmp, parm->value, 0, 10)
            == octstr_len(parm->value)) {
            wsp_pack_integer_value(packed, tmp);
        } else {
            pack_quoted_string(packed, parm->value);
        }
    }
}

void wsp_pack_parameters(Octstr *packed, List *parms)
{
    long i;
    Parameter *parm;

    for (i = 0; i < gwlist_len(parms); i++) {
        parm = gwlist_get(parms, i);
        pack_parameter(packed, parm);
    }
}

static int pack_uri(Octstr *packed, Octstr *value)
{
    wsp_pack_text(packed, value);
    return 0;
}

static int pack_md5(Octstr *packed, Octstr *value)
{
    Octstr *binary;

    binary = octstr_duplicate(value);
    octstr_base64_to_binary(binary);

    if (octstr_len(binary) != 16) {
        error(0, "WSP: MD5 value not 128 bits.");
        return -1;
    }

    octstr_append_char(packed, 16);
    octstr_append(packed, binary);

    octstr_destroy(binary);

    return 0;
}

/* Actually packs a "Value-length Challenge" */
/* Relies on http_split_auth_value to have converted the entry to
 * the normal HTTP parameter format rather than the comma-separated
 * one used by challenge and credentials. */
static int pack_challenge(Octstr *packed, Octstr *value)
{
    Octstr *encoding = NULL;
    Octstr *scheme = NULL;
    Octstr *basic = octstr_imm("Basic");
    Octstr *realm = octstr_imm("realm");
    Octstr *parmstring = NULL;
    List *parms = NULL;
    Parameter *realmparm = NULL;
    long realmpos = -1;
    Octstr *realmval = NULL;
    long pos;

    encoding = octstr_create("");

    /* Get authentication scheme */
    for (pos = 0; pos < octstr_len(value); pos++) {
        if (!is_token_char(octstr_get_char(value, pos)))
            break;
    }
    scheme = octstr_copy(value, 0, pos);
    octstr_strip_blanks(scheme);

    /* Skip whitespace */
    while (isspace(octstr_get_char(value, pos)))
        pos++;

    if (octstr_case_compare(scheme, basic) == 0) {
        parmstring = octstr_copy(value, pos, octstr_len(value) - pos);
        realmparm = parm_parse(parmstring);

        octstr_append_char(encoding, BASIC_AUTHENTICATION);
        realmpos = octstr_len(encoding);
    } else {
        long i;

        wsp_pack_text(encoding, scheme);
        realmpos = octstr_len(encoding);

        /* Find the realm parameter and exclude it */
        parms = wsp_strip_parameters(value);
        for (i = 0; i < gwlist_len(parms); i++) {
            Parameter *parm = gwlist_get(parms, i);
            if (octstr_case_compare(realm, parm->key) == 0) {
                realmparm = parm;
                gwlist_delete(parms, i, 1);
                break;
            }
        }

        wsp_pack_parameters(encoding, parms);
    }

    /*
     * In the WSP encoding we have to put the realm value first, but
     * with non-Basic challenges we don't know if it will come first
     * in the HTTP header.  So we just start parsing parameters, and
     * go back and insert the realm value later.  The same technique
     * is used for Basic authentication to simplify the code.
     */

    if (realmparm == NULL ||
        octstr_case_compare(realmparm->key, realm) != 0 ||
        realmparm->value == NULL)
        goto error;

    /* Zap quote marks */
    if (octstr_get_char(realmparm->value, 0) == '"' &&
        octstr_get_char(realmparm->value, octstr_len(realmparm->value) - 1) == '"') {
        octstr_delete(realmparm->value, 0, 1);
        octstr_delete(realmparm->value, octstr_len(realmparm->value) - 1, 1);
    }

    gw_assert(realmpos >= 0);

    realmval = octstr_create("");
    wsp_pack_text(realmval, realmparm->value);
    octstr_insert(encoding, realmval, realmpos);

    wsp_pack_value(packed, encoding);

    octstr_destroy(encoding);
    octstr_destroy(scheme);
    octstr_destroy(parmstring);
    parm_destroy(realmparm);
    gwlist_destroy(parms, parm_destroy_item);
    octstr_destroy(realmval);
    return 0;

error:
    warning(0, "WSP: Cannot parse challenge.");
    octstr_destroy(encoding);
    octstr_destroy(scheme);
    octstr_destroy(parmstring);
    parm_destroy(realmparm);
    gwlist_destroy(parms, parm_destroy_item);
    octstr_destroy(realmval);
    return -1;
}

/* Actually packs a "Value-length Credentials" */
/* Relies on http_split_auth_value to have converted the entry to
 * the normal HTTP parameter format rather than the comma-separated
 * one used by challenge and credentials. */
static int pack_credentials(Octstr *packed, Octstr *value)
{
    Octstr *encoding = NULL;
    Octstr *scheme = NULL;
    Octstr *basic = NULL;
    long pos;

    encoding = octstr_create("");

    /* Get authentication scheme */
    for (pos = 0; pos < octstr_len(value); pos++) {
        if (!is_token_char(octstr_get_char(value, pos)))
            break;
    }
    scheme = octstr_copy(value, 0, pos);
    octstr_strip_blanks(scheme);

    /* Skip whitespace */
    while (isspace(octstr_get_char(value, pos)))
        pos++;

    basic = octstr_imm("Basic");
    if (octstr_case_compare(scheme, basic) == 0) {
        Octstr *cookie;
        Octstr *userid;
        Octstr *password;

        octstr_append_char(encoding, BASIC_AUTHENTICATION);

        cookie = octstr_copy(value, pos, octstr_len(value) - pos);
        octstr_base64_to_binary(cookie);
        pos = octstr_search_char(cookie, ':', 0);
        if (pos < 0) {
            warning(0, "WSP: bad cookie in credentials '%s'.",
                    octstr_get_cstr(value));
            octstr_destroy(cookie);
            goto error;
        }

        userid = octstr_copy(cookie, 0, pos);
        password = octstr_copy(cookie, pos + 1, octstr_len(cookie) - pos);
        wsp_pack_text(encoding, userid);
        wsp_pack_text(encoding, password);

        octstr_destroy(cookie);
        octstr_destroy(userid);
        octstr_destroy(password);
    } else {
        List *parms;

        wsp_pack_text(encoding, scheme);
        parms = wsp_strip_parameters(value);
        wsp_pack_parameters(encoding, parms);
        gwlist_destroy(parms, parm_destroy_item);
    }

    wsp_pack_value(packed, encoding);
    octstr_destroy(encoding);
    octstr_destroy(scheme);

    return 0;

error:
    octstr_destroy(encoding);
    octstr_destroy(scheme);
    return -1;
}

int wsp_pack_date(Octstr *packed, Octstr *value)
{
    long timeval;

    /* If we get a negative timeval here, this means either
     * we're beyond the time_t 32 bit int positive border for the 
     * timestamp or we're really handling time before epoch. */
    timeval = date_parse_http(value);
    if (timeval == -1) {
        warning(0, "WSP headers: cannot decode date '%s'",
                octstr_get_cstr(value));
        return -1;
    }

    /* We'll assume that we don't package time before epoch. */
    wsp_pack_long_integer(packed, (unsigned long) timeval);
    return 0;
}

static int pack_connection(Octstr *packed, Octstr *value)
{
    return wsp_pack_constrained_value(packed, value,
                                      wsp_string_to_connection(value));
}

static int pack_encoding(Octstr *packed, Octstr *value)
{
    return wsp_pack_constrained_value(packed, value,
                                      wsp_string_to_encoding(value));
}

static int pack_field_name(Octstr *packed, Octstr *value)
{
    /* XXX we need to obey which WSP encoding-version to use */
    /* return pack_constrained_value(packed, value,
                                  wsp_string_to_header(value)); */
    return wsp_pack_constrained_value(packed, value,
                                      wsp_string_to_versioned_header(value, WSP_1_2));
}

static int pack_language(Octstr *packed, Octstr *value)
{
    long language;

    /* Can't use pack_constrained_value here because
     * language does not necessarily fit in a Short-integer. */
    language = wsp_string_to_language(value);
    if (language >= 0)
        wsp_pack_integer_value(packed, language);
    else
        wsp_pack_text(packed, value);
    return 0;
}

/* Encode value as Well-known-method | Token-text */
static int pack_method(Octstr *packed, Octstr *value)
{
    /* In the future, we will need some way to refer to extended
     * method names negotiated for this session. */ 
    return wsp_pack_constrained_value(packed, value,
                                      wsp_string_to_method(value));
}

/* Encode value as Accept-ranges-value */
static int pack_range_unit(Octstr *packed, Octstr *value)
{
    return wsp_pack_constrained_value(packed, value,
                                      wsp_string_to_ranges(value));
}

/* Encode byte-range-spec | suffix-byte-range-spec as Range-value.
 * Return position after the parsed spec. */
static int pack_range_value(Octstr *packed, Octstr *value, long pos)
{
    long first_byte_pos;
    long last_byte_pos;
    long suffix_length;
    Octstr *encoding;

    while (isspace(octstr_get_char(value, pos)))
        pos++;

    if (isdigit(octstr_get_char(value, pos))) {
        /* byte-range-spec */
        pos = octstr_parse_long(&first_byte_pos, value, pos, 10);
        if (pos < 0 || first_byte_pos < 0)
            return -1;

        while (isspace(octstr_get_char(value, pos)))
            pos++;

        if (octstr_get_char(value, pos) != '-')
            return -1;
        pos++;

        while (isspace(octstr_get_char(value, pos)))
            pos++;

        if (isdigit(octstr_get_char(value, pos))) {
            pos = octstr_parse_long(&last_byte_pos, value, pos, 10);
            if (pos < 0 || last_byte_pos < 0)
                return -1;
        } else {
            last_byte_pos = -1;
        }

        encoding = octstr_create("");
        octstr_append_char(encoding, BYTE_RANGE);
        octstr_append_uintvar(encoding, first_byte_pos);
        if (last_byte_pos >= 0)
            octstr_append_uintvar(encoding, last_byte_pos);
        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
    } else if (octstr_get_char(value, pos) == '-') {
        /* suffix-byte-range-spec */
        pos++;

        pos = octstr_parse_long(&suffix_length, value, pos, 10);
        if (pos < 0 || suffix_length < 0)
            return -1;

        encoding = octstr_create("");
        octstr_append_char(encoding, SUFFIX_BYTE_RANGE);
        octstr_append_uintvar(encoding, suffix_length);
        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
    } else
        return -1;

    return pos;
}

static int pack_transfer_encoding(Octstr *packed, Octstr *value)
{
    return wsp_pack_constrained_value(packed, value,
                                      wsp_string_to_transfer_encoding(value));
}

/* Also used by pack_content_type  */
static int pack_accept(Octstr *packed, Octstr *value)
{
    List *parms;
    long media;

    parms = wsp_strip_parameters(value);
    /* XXX we need to obey which WSP encoding-version to use */
    /* media = wsp_string_to_content_type(value); */
    media = wsp_string_to_versioned_content_type(value, WSP_1_2);

    /* See if we can fit this in a Constrained-media encoding */
    if (parms == NULL && media <= MAX_SHORT_INTEGER) {
        wsp_pack_constrained_value(packed, value, media);
    } else {
        Octstr *encoding = octstr_create("");

        if (media >= 0)
            wsp_pack_integer_value(encoding, media);
        else
            wsp_pack_text(encoding, value);

        wsp_pack_parameters(encoding, parms);
        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
    }

    gwlist_destroy(parms, parm_destroy_item);
    return 0;
}

static int pack_accept_charset(Octstr *packed, Octstr *value)
{
    List *parms;
    long charset;
    long qvalue;

    parms = wsp_strip_parameters(value);
    charset = wsp_string_to_charset(value);
    qvalue = 1000;
    if (parms)
        qvalue = get_qvalue(parms, qvalue);
    gwlist_destroy(parms, parm_destroy_item);

    /* See if we can fit this in a Constrained-charset encoding */
    if (qvalue == 1000 && charset <= MAX_SHORT_INTEGER) {
        wsp_pack_constrained_value(packed, value, charset);
    } else {
        Octstr *encoding = octstr_create("");

        if (charset >= 0)
            wsp_pack_integer_value(encoding, charset);
        else
            wsp_pack_text(encoding, value);

        if (qvalue != 1000)
            pack_qvalue(encoding, qvalue);
        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
    }

    return 0;
}

/* WSP 8.4.2.9 does not specify any way to encode parameters for
 * an Accept-encoding field, but RFC2616 allows q parameters.
 * We'll have to simplify: qvalue > 0 means encode it, qvalue == 0
 * means don't encode it.
 */
static int pack_accept_encoding(Octstr *packed, Octstr *value)
{
    List *parms;
    int qvalue;

    qvalue = 1000;   /* default */

    parms = wsp_strip_parameters(value);
    if (parms)
        qvalue = get_qvalue(parms, qvalue);
    gwlist_destroy(parms, parm_destroy_item);

    if (qvalue > 0) {
        if (qvalue < 1000)
            warning(0, "Cannot encode q-value in Accept-Encoding.");
        pack_encoding(packed, value);
    } else {
        warning(0, "Cannot encode q=0 in Accept-Encoding; skipping this encoding.");
        return -1;
    }

    return 0;
}

static int pack_accept_language(Octstr *packed, Octstr *value)
{
    List *parms;
    long language;
    long qvalue;

    parms = wsp_strip_parameters(value);
    language = wsp_string_to_language(value);
    qvalue = 1000;
    if (parms)
        qvalue = get_qvalue(parms, qvalue);
    gwlist_destroy(parms, parm_destroy_item);

    /* See if we can fit this in a Constrained-language encoding. */
    /* Note that our language table already includes Any-language */
    if (qvalue == 1000 && language <= MAX_SHORT_INTEGER) {
        wsp_pack_constrained_value(packed, value, language);
    } else {
        Octstr *encoding = octstr_create("");

        if (language >= 0)
            wsp_pack_integer_value(encoding, language);
        else
            wsp_pack_text(encoding, value);

        if (qvalue != 1000)
            pack_qvalue(encoding, qvalue);
        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
    }

    return 0;
}

static int pack_cache_control(Octstr *packed, Octstr *value)
{
    Parameter *parm;
    long tmp;

    parm = parm_parse(value);

    if (parm->value == NULL) {
        wsp_pack_constrained_value(packed, value,
                                   wsp_string_to_cache_control(parm->key));
    } else {
        Octstr *encoding = octstr_create("");

        tmp = wsp_string_to_cache_control(parm->key);
        if (tmp < 0) {
            /* According to WSP 8.4.2.15, the format
             * is "Cache-extension Parameter", and
             * Cache-extension is a Token-text.
             * But in HTTP a cache-extension is of
             * the form token=value, which maps
             * nicely to Parameter.  So what is
             * this extra Token-text?  I decided to leave it blank.
             *  - Richard Braakman
             */
            wsp_pack_text(encoding, octstr_imm(""));
            pack_parameter(encoding, parm);
        } else {
            int done = 0;
            Octstr *value_encoding;
            List *names;
            Octstr *element;

            value_encoding = octstr_create("");
            switch (tmp) {
            case WSP_CACHE_CONTROL_NO_CACHE:
            case WSP_CACHE_CONTROL_PRIVATE:
                if (octstr_get_char(parm->value, 0) == '"')
                    octstr_delete(parm->value, 0, 1);
                if (octstr_get_char(parm->value, octstr_len(parm->value) - 1) == '"')
                    octstr_delete(parm->value, octstr_len(parm->value) - 1, 1);
                names = http_header_split_value(parm->value);
                while ((element = gwlist_consume(names))) {
                    pack_field_name(value_encoding, element);
                    octstr_destroy(element);
                }
                gwlist_destroy(names, octstr_destroy_item);
                done = 1;
                break;

            case WSP_CACHE_CONTROL_MAX_AGE:
            case WSP_CACHE_CONTROL_MAX_STALE:
            case WSP_CACHE_CONTROL_MIN_FRESH:
                if (wsp_pack_integer_string(value_encoding, parm->value) >= 0)
                    done = 1;
                break;
            }

            if (done) {
                wsp_pack_short_integer(encoding, tmp);
                octstr_append(encoding, value_encoding);
            } else {
                /* See note above */
                pack_parameter(encoding, parm);
            }
            octstr_destroy(value_encoding);
        }

        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
    }

    parm_destroy(parm);
    return 1;
}

static int pack_content_disposition(Octstr *packed, Octstr *value)
{
    List *parms;
    long disposition;

    parms = wsp_strip_parameters(value);
    disposition = wsp_string_to_disposition(value);

    if (disposition >= 0) {
        Octstr *encoding = octstr_create("");

        wsp_pack_short_integer(encoding, disposition);
        wsp_pack_parameters(encoding, parms);
        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
    } else {
        warning(0, "WSP: Cannot encode Content-Disposition '%s'.",
                octstr_get_cstr(value));
        goto error;
    }

    gwlist_destroy(parms, parm_destroy_item);
    return 0;

error:
    gwlist_destroy(parms, parm_destroy_item);
    return -1;
}

static int pack_content_range(Octstr *packed, Octstr *value)
{
    Octstr *bytes;
    long pos;
    long firstbyte, lastbyte, instancelen;
    Octstr *encoding;

    bytes = octstr_imm("bytes ");
    if (octstr_ncompare(value, bytes, octstr_len(bytes)) != 0)
        goto error;

    pos = octstr_len(bytes);
    while (isspace(octstr_get_char(value, pos)))
        pos++;
    if (octstr_get_char(value, pos) == '*')
        goto warning;
    pos = octstr_parse_long(&firstbyte, value, pos, 10);
    if (pos < 0)
        goto error;

    while (isspace(octstr_get_char(value, pos)))
        pos++;
    if (octstr_get_char(value, pos++) != '-')
        goto error;

    pos = octstr_parse_long(&lastbyte, value, pos, 10);
    if (pos < 0)
        goto error;

    while (isspace(octstr_get_char(value, pos)))
        pos++;
    if (octstr_get_char(value, pos++) != '/')
        goto error;

    while (isspace(octstr_get_char(value, pos)))
        pos++;
    if (octstr_get_char(value, pos) == '*')
        goto warning;
    pos = octstr_parse_long(&instancelen, value, pos, 10);
    if (pos < 0)
        goto error;

    /* XXX: If the range is valid but not representable,
     * or if it's invalid, then should we refrain from sending
     * anything at all?  It might pollute the client's cache. */

    if (lastbyte < firstbyte || instancelen < lastbyte) {
        warning(0, "WSP: Content-Range '%s' is invalid.",
                octstr_get_cstr(value));
        return -1;
    }

    encoding = octstr_create("");
    octstr_append_uintvar(encoding, firstbyte);
    octstr_append_uintvar(encoding, instancelen);
    wsp_pack_value(packed, encoding);
    octstr_destroy(encoding);

    return 0;

error:
    warning(0, "WSP: Cannot parse Content-Range '%s'.",
            octstr_get_cstr(value));
    return -1;

warning:
    warning(0, "WSP: Cannot encode Content-Range '%s'.",
            octstr_get_cstr(value));
    return -1;
}

int wsp_pack_content_type(Octstr *packed, Octstr *value)
{
    /* The expansion of Content-type-value works out to be
     * equivalent to Accept-value. */ 
    return pack_accept(packed, value);
}

static int pack_expires(Octstr *packed, Octstr *value)
{
    int ret;

    ret = wsp_pack_date(packed, value);

    if (ret < 0) {
	/* Responses with an invalid Expires header should be treated
	as already expired.  If we just skip this header, then the client
	won't know that.  So we encode one with a date far in the past. */
	wsp_pack_long_integer(packed, LONG_AGO_VALUE);
	ret = 0;
    }

    return ret;
}

static int pack_if_range(Octstr *packed, Octstr *value)
{
    if (octstr_get_char(value, 0) == '"' ||
        (octstr_get_char(value, 0) == 'W' &&
         octstr_get_char(value, 1) == '/')) {
        return wsp_pack_quoted_text(packed, value);   /* It's an etag */
    } else {
        return wsp_pack_date(packed, value);
    }
}

static int pack_pragma(Octstr *packed, Octstr *value)
{
    Octstr *nocache;

    nocache = octstr_imm("no-cache");
    if (octstr_case_compare(value, nocache) == 0)
        wsp_pack_short_integer(packed, WSP_CACHE_CONTROL_NO_CACHE);
    else {
        Parameter *parm;
        Octstr *encoding;

        encoding = octstr_create("");
        parm = parm_parse(value);
        pack_parameter(encoding, parm);
        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
        parm_destroy(parm);
    }

    return 0;
}

static int pack_range(Octstr *packed, Octstr *value)
{
    Octstr *bytes = octstr_imm("bytes");
    long pos;

    if (octstr_ncompare(value, bytes, octstr_len(bytes)) != 0
        || is_token_char(octstr_get_char(value, octstr_len(bytes))))
        goto error;

    pos = octstr_len(bytes);
    while (isspace(octstr_get_char(value, pos)))
        pos++;

    if (octstr_get_char(value, pos) != '=')
        goto error;
    pos++;

    for (;;) {
        /* Discard the whole header if any part of it can't be
         * parsed.  Probably a partial Range header is worse
         * than none at all. */
        pos = pack_range_value(packed, value, pos);
        if (pos < 0)
            goto error;

        while (isspace(octstr_get_char(value, pos)))
            pos++;

        if (octstr_get_char(value, pos) != ',')
            break;
        pos++;

        wsp_pack_short_integer(packed, WSP_HEADER_RANGE);
    }

    return 0;

error:
    warning(0, "WSP: Cannot parse 'Range: %s'.",
            octstr_get_cstr(value));
    return -1;
}

/* The value is either a HTTP-date or a delta-seconds (integer). */
int wsp_pack_retry_after(Octstr *packed, Octstr *value)
{
    Octstr *encoded = NULL;

    encoded = octstr_create("");
    if (isdigit(octstr_get_char(value, 0))) {
        octstr_append_char(encoded, RELATIVE_TIME);
        if (wsp_pack_integer_string(encoded, value) < 0)
            goto error;
    } else {
        octstr_append_char(encoded, ABSOLUTE_TIME);
        if (wsp_pack_date(encoded, value) < 0)
            goto error;
    }
    wsp_pack_value(packed, encoded);

    octstr_destroy(encoded);
    return 0;

error:
    octstr_destroy(encoded);
    return -1;
}


static int convert_rfc2616_warning_to_rfc2068(int warn_code)
{
    int i;
    struct {
	int rfc2616code;
	int rfc2068code;
    } code_transform[] = {
	{ 110, 10 },  /* Response is stale */
	{ 111, 11 },  /* Revalidation failed */
	{ 112, 12 },  /* Disconnected operation */
	{ 113, 13 },  /* Heuristic expiration */
	{ 199, 99 },  /* Miscellaneous warning */
	{ 214, 14 },  /* Transformation applied */
	{ 299, 99 },  /* Miscellaneous (persistent) warning */
	{ -1, -1 }
    };
    
    for (i = 0; code_transform[i].rfc2616code >= 0; i++) {
	if (code_transform[i].rfc2616code == warn_code)
	    return code_transform[i].rfc2068code;
    }

    return warn_code; /* conversion failed */
}

static int pack_warning(Octstr *packed, Octstr *value)
{
    long warn_code = -1;
    Octstr *warn_agent = NULL;
    Octstr *warn_text = NULL;
    long pos;
    long start;

    pos = octstr_parse_long(&warn_code, value, 0, 10);
    if (pos < 0 || warn_code < 0)
        goto error;

    if (warn_code > 99) {
	/* RFC2068 uses 2-digit codes, and RFC2616 uses 3-digit codes.
	 * This must be an RFC2616 code.  We don't have room in the
	 * encoding for such codes, so we try to convert it back to
	 * an RFC2068 code. */
	warn_code = convert_rfc2616_warning_to_rfc2068(warn_code);
    }

    if (warn_code > MAX_SHORT_INTEGER) {
	warning(0, "WSP: Cannot encode warning code %ld.", warn_code);
	return -1;
    }

    while (isspace(octstr_get_char(value, pos)))
        pos++;

    start = pos;
    while (pos < octstr_len(value) && !isspace(octstr_get_char(value, pos)))
        pos++;

    if (pos > start)
        warn_agent = octstr_copy(value, start, pos - start);

    while (isspace(octstr_get_char(value, pos)))
        pos++;

    start = pos;
    pos += http_header_quoted_string_len(value, pos);
    if (pos > start)
        warn_text = octstr_copy(value, start, pos - start);

    if (warn_agent == NULL && warn_text == NULL) {
        /* Simple encoding */
        wsp_pack_short_integer(packed, warn_code);
    } else {
        /* General encoding */
        Octstr *encoding = octstr_create("");

        if (warn_agent == NULL)
            warn_agent = octstr_create("");
        if (warn_text == NULL)
            warn_text = octstr_create("");

        wsp_pack_short_integer(encoding, warn_code);
        wsp_pack_text(encoding, warn_agent);
        wsp_pack_text(encoding, warn_text);
        wsp_pack_value(packed, encoding);
        octstr_destroy(encoding);
    }

    octstr_destroy(warn_agent);
    octstr_destroy(warn_text);
    return 0;

error:
    warning(0, "WSP: Cannot parse 'Warning: %s'.",
            octstr_get_cstr(value));
    octstr_destroy(warn_agent);
    octstr_destroy(warn_text);
    return -1;
}

void wsp_pack_separate_content_type(Octstr *packed, List *headers)
{
    Octstr *content_type;

    /* Can't use http_header_get_content_type because it
     * does not parse all possible parameters. */
    content_type = http_header_find_first(headers, "Content-Type");

    if (content_type == NULL) {
        warning(0, "WSP: Missing Content-Type header in "
                "response, guessing application/octet-stream");
        content_type = octstr_create("application/octet-stream");
    }
    octstr_strip_blanks(content_type);
    wsp_pack_content_type(packed, content_type);
    octstr_destroy(content_type);
}

int wsp_pack_list(Octstr *packed, long fieldnum, List *elements, int i)
{
    long startpos;
    Octstr *element;

    while ((element = gwlist_consume(elements))) {
        startpos = octstr_len(packed);

        wsp_pack_short_integer(packed, fieldnum);
        if (headerinfo[i].func(packed, element) < 0) {
            /* Remove whatever we added */
            octstr_delete(packed, startpos,
                          octstr_len(packed) - startpos);
            /* But continue processing elements */
        }
        octstr_destroy(element);
    }
    return 0;
}

static int pack_known_header(Octstr *packed, long fieldnum, Octstr *value)
{
    List *elements = NULL;
    long startpos;
    long i;

    octstr_strip_blanks(value);

    startpos = octstr_len(packed);

    for (i = 0; i < TABLE_SIZE(headerinfo); i++) {
        if (headerinfo[i].header == fieldnum)
            break;
    }

    if (i == TABLE_SIZE(headerinfo)) {
        error(0, "WSP: Do not know how to encode header type %ld",
              fieldnum);
        goto error;
    }

    if (headerinfo[i].allows_list == LIST)
        elements = http_header_split_value(value);
    else if (headerinfo[i].allows_list == BROKEN_LIST)
        elements = http_header_split_auth_value(value);
    else
        elements = NULL;

    if (elements != NULL) {
        if (wsp_pack_list(packed, fieldnum, elements, i) < 0)
            goto error;
    } else {
        wsp_pack_short_integer(packed, fieldnum);
        if (headerinfo[i].func(packed, value) < 0)
            goto error;
    }

    gwlist_destroy(elements, octstr_destroy_item);
    return 0;

error:
    /* Remove whatever we added */
    octstr_delete(packed, startpos, octstr_len(packed) - startpos);
    gwlist_destroy(elements, octstr_destroy_item);
    return -1;
}

int wsp_pack_application_header(Octstr *packed,
                                   Octstr *fieldname, Octstr *value)
{
    if (!is_token(fieldname)) {
        warning(0, "WSP headers: `%s' is not a valid HTTP token.",
                octstr_get_cstr(fieldname));
        return -1;
    }

    /* We have to deal specially with the X-WAP.TOD header, because it
     * is the only case of a text-format header defined with a non-text
     * field value. */
    /* Normally this should be a case-insensitive comparison, but this
     * header will only be present if we generated it ourselves in the
     * application layer. */
    if (octstr_str_compare(fieldname, "X-WAP.TOD") == 0) {
        wsp_pack_text(packed, fieldname);
        return wsp_pack_date(packed, value);
    }

    wsp_pack_text(packed, fieldname);
    wsp_pack_text(packed, value);
    return 0;
}

Octstr *wsp_headers_pack(List *headers, int separate_content_type, int wsp_version)
{
    Octstr *packed;
    long i, len;
    int errors;

    packed = octstr_create("");
    if (separate_content_type)
        wsp_pack_separate_content_type(packed, headers);

    len = gwlist_len(headers);
    for (i = 0; i < len; i++) {
        Octstr *fieldname;
        Octstr *value;
        long fieldnum;

        http_header_get(headers, i, &fieldname, &value);
        /* XXX we need to obey which WSP encoding-version to use */
        /* fieldnum = wsp_string_to_header(fieldname); */
        fieldnum = wsp_string_to_versioned_header(fieldname, wsp_version);

        errors = 0;

        if (separate_content_type && fieldnum == WSP_HEADER_CONTENT_TYPE) {
	    /* already handled */
        } else if (fieldnum < 0) {
            if (wsp_pack_application_header(packed, fieldname, value) < 0)
                errors = 1;
        } else {
            if (pack_known_header(packed, fieldnum, value) < 0)
                errors = 1;
        }

        if (errors)
            warning(0, "Skipping header: %s: %s",
                    octstr_get_cstr(fieldname),
                    octstr_get_cstr(value));

        octstr_destroy(fieldname);
        octstr_destroy(value);
    }

    /*
    http_header_dump(headers);
    octstr_dump(packed, 0);
    */

    return packed;
}

