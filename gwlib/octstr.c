/* ====================================================================
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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
 * octstr.c - implementation of Octet strings
 *
 * See octstr.h for explanations of what public functions should do.
 *
 * Lars Wirzenius
 */


#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "gwlib.h"

/* 
 * Unfortunately some platforms base va_list an an array type
 * which makes passing of the &args a bit tricky 
 */
#if (defined(__linux__) && (defined(__powerpc__) || defined(__s390__) || defined(__x86_64))) || \
    (defined(__FreeBSD__) && defined(__amd64__)) || \
    (defined(DARWIN) && defined(__x86_64__))
#define VARGS(x)   (x)
#define VALPARM(y) va_list y
#define VALST(z)   (z)
#else
#define VARGS(x)   (&x)
#define VALPARM(y) va_list *y
#define VALST(z)   (*z)
#endif


/***********************************************************************
 * Definitions of data structures. These are not visible to the external
 * world -- they may be accessed only via the functions declared in
 * octstr.h. This ensures they really are abstract.
 */

/*
 * The octet string.
 *
 * `data' is a pointer to dynamically allocated memory are where the 
 * octets in the string. It may be bigger than the actual length of the
 * string.
 *
 * `len' is the length of the string.
 *
 * `size' is the size of the memory area `data' points at.
 *
 * When `size' is greater than zero, it is at least `len+1', and the
 * character at `len' is '\0'. This is so that octstr_get_cstr will
 * always work.
 *
 * `immutable' defines whether the octet string is immutable or not.
 */
struct Octstr
{
    unsigned char *data;
    long len;
    long size;
    int immutable;
};


/**********************************************************************
 * Hash table of immutable octet strings.
 */

#define MAX_IMMUTABLES 1024

static Octstr *immutables[MAX_IMMUTABLES];
static Mutex immutables_mutex;
static int immutables_init = 0;

static char is_safe[UCHAR_MAX + 1];

/*
 * Convert a pointer to a C string literal to a long that can be used
 * for hashing. This is done by converting the pointer into an integer
 * and discarding the lowest to bits to get rid of typical alignment
 * bits.
 */
#define CSTR_TO_LONG(ptr)	(((unsigned long) ptr) >> 2)


/*
 * HEX to ASCII preprocessor macro
 */
#define H2B(a) (a >= '0' && a <= '9' ? \
    a - '0' : (a >= 'a' && a <= 'f' ? \
        a - 'a' + 10 : (a >= 'A' && a <= 'F' ? a - 'A' + 10 : -1) \
        ) \
    )


/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */


static void seems_valid_real(const Octstr *ostr, const char *filename, long lineno,
                             const char *function);
#ifdef NO_GWASSERT
#define seems_valid(ostr)
#else
#define seems_valid(ostr) \
    (seems_valid_real(ostr, __FILE__, __LINE__, __func__))
#endif


/***********************************************************************
 * Implementations of the functions declared in octstr.h. See the
 * header for explanations of what they should do.
 */


/* Reserve space for at least 'size' octets */
static void octstr_grow(Octstr *ostr, long size)
{
    gw_assert(!ostr->immutable);
    seems_valid(ostr);
    gw_assert(size >= 0);

    size++;   /* make room for the invisible terminating NUL */

    if (size > ostr->size) {
        /* always reallocate in 1kB chunks */
        size += 1024 - (size % 1024);
        ostr->data = gw_realloc(ostr->data, size);
        ostr->size = size;
    }
}


/*
 * Fill is_safe table. is_safe[c] means that c can be left as such when
 * url-encoded.
 * RFC 2396 defines the list of characters that need to be encoded.
 * Space is treated as an exception by the encoding routine;
 * it's listed as safe here, but is actually changed to '+'.
 */
static void urlcode_init(void)
{
    int i;

    unsigned char *safe = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                          "abcdefghijklmnopqrstuvwxyz-_.!~*'()";
    for (i = 0; safe[i] != '\0'; ++i)
        is_safe[safe[i]] = 1;
}


void octstr_init(void)
{
    urlcode_init();
    mutex_init_static(&immutables_mutex);
    immutables_init = 1;
}


void octstr_shutdown(void)
{
    long i, n;

    n = 0;
    for (i = 0; i < MAX_IMMUTABLES; ++i) {
        if (immutables[i] != NULL) {
	    gw_free(immutables[i]);
            ++n;
        }
    }
    if(n>0)
        debug("gwlib.octstr", 0, "Immutable octet strings: %ld.", n);
    mutex_destroy(&immutables_mutex);
}


Octstr *octstr_create_real(const char *cstr, const char *file, long line,
                           const char *func)
{
    gw_assert(cstr != NULL);
    return octstr_create_from_data_trace(cstr, strlen(cstr), file, line, func);
}

Octstr *octstr_create_from_data_real(const char *data, long len, const char *file,
                                     long line, const char *func)
{
    Octstr *ostr;

    gw_assert(len >= 0);
    if (data == NULL)
        gw_assert(len == 0);

    /* if gw_assert is disabled just return NULL
     * and caller will check for NULL or just crash.
     */
    if (len < 0 || (data == NULL && len != 0))
        return NULL;

    ostr = gw_malloc_trace(sizeof(*ostr), file, line, func);
    if (len == 0) {
        ostr->len = 0;
        ostr->size = 0;
        ostr->data = NULL;
    } else {
        ostr->len = len;
        ostr->size = len + 1;
        ostr->data = gw_malloc_trace(ostr->size, file, line, func);
        memcpy(ostr->data, data, len);
        ostr->data[len] = '\0';
    }
    ostr->immutable = 0;
    seems_valid(ostr);
    return ostr;
}


Octstr *octstr_imm(const char *cstr)
{
    Octstr *os;
    long i, index;
    unsigned char *data;

    gw_assert(immutables_init);
    gw_assert(cstr != NULL);

    index = CSTR_TO_LONG(cstr) % MAX_IMMUTABLES;
    data = (unsigned char *) cstr;

    mutex_lock(&immutables_mutex);
    i = index;
    for (; ; ) {
	if (immutables[i] == NULL || immutables[i]->data == data)
            break;
        i = (i + 1) % MAX_IMMUTABLES;
        if (i == index)
            panic(0, "Too many immutable strings.");
    }
    os = immutables[i];
    if (os == NULL) {
	/*
	 * Can't use octstr_create() because it copies the string,
	 * which would break our hashing.
	 */
	os = gw_malloc(sizeof(*os));
        os->data = data;
        os->len = strlen(data);
        os->size = os->len + 1;
        os->immutable = 1;
	immutables[i] = os;
	seems_valid(os);
    }
    mutex_unlock(&immutables_mutex);

    return os;
}


void octstr_destroy(Octstr *ostr)
{
    if (ostr != NULL) {
        seems_valid(ostr);
	if (!ostr->immutable) {
            gw_free(ostr->data);
            gw_free(ostr);
        }
    }
}


void octstr_destroy_item(void *os)
{
    octstr_destroy(os);
}


long octstr_len(const Octstr *ostr)
{
    if (ostr == NULL)
        return 0;
    seems_valid(ostr);
    return ostr->len;
}


Octstr *octstr_copy_real(const Octstr *ostr, long from, long len, const char *file, long line,
                         const char *func)
{
    if (ostr == NULL)
        return octstr_create("");

    seems_valid_real(ostr, file, line, func);
    gw_assert(from >= 0);
    gw_assert(len >= 0);

    if (from >= ostr->len)
        return octstr_create("");

    if (len > ostr->len - from)
        len = ostr->len - from;

    return octstr_create_from_data_trace(ostr->data + from, len, file,
                                         line, func);
}



Octstr *octstr_duplicate_real(const Octstr *ostr, const char *file, long line,
                              const char *func)
{
    if (ostr == NULL)
        return NULL;
    seems_valid_real(ostr, file, line, func);
    return octstr_create_from_data_trace(ostr->data, ostr->len, file, line, func);
}


Octstr *octstr_cat(Octstr *ostr1, Octstr *ostr2)
{
    Octstr *ostr;

    seems_valid(ostr1);
    seems_valid(ostr2);
    gw_assert(!ostr1->immutable);

    ostr = octstr_create("");
    ostr->len = ostr1->len + ostr2->len;
    ostr->size = ostr->len + 1;
    ostr->data = gw_malloc(ostr->size);

    if (ostr1->len > 0)
        memcpy(ostr->data, ostr1->data, ostr1->len);
    if (ostr2->len > 0)
        memcpy(ostr->data + ostr1->len, ostr2->data, ostr2->len);
    ostr->data[ostr->len] = '\0';

    seems_valid(ostr);
    return ostr;
}


int octstr_get_char(const Octstr *ostr, long pos)
{
    seems_valid(ostr);
    if (pos >= ostr->len || pos < 0)
        return -1;
    return ostr->data[pos];
}


void octstr_set_char(Octstr *ostr, long pos, int ch)
{
    seems_valid(ostr);
    gw_assert(!ostr->immutable);
    if (pos < ostr->len)
        ostr->data[pos] = ch;
    seems_valid(ostr);
}


void octstr_get_many_chars(char *buf, Octstr *ostr, long pos, long len)
{
    gw_assert(buf != NULL);
    seems_valid(ostr);

    if (pos >= ostr->len)
        return;
    if (pos + len > ostr->len)
        len = ostr->len - pos;
    if (len > 0)
        memcpy(buf, ostr->data + pos, len);
}


char *octstr_get_cstr_real(const Octstr *ostr, const char *file, long line, 
    	    	    	   const char *func)
{
    if (!ostr)
        return "(null)";
    seems_valid_real(ostr, file, line, func);
    if (ostr->len == 0)
        return "";
    return ostr->data;
}


void octstr_append_from_hex(Octstr *ostr, char *hex)
{
    Octstr *output;
	
    seems_valid(ostr);
    gw_assert(!ostr->immutable);
	
    output = octstr_create(hex);
    octstr_hex_to_binary(output);
    octstr_append(ostr, output);
    octstr_destroy(output);
}


void octstr_binary_to_hex(Octstr *ostr, int uppercase)
{
    unsigned char *hexits;
    long i, tmp;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);
    if (ostr->len == 0)
        return;

    hexits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    octstr_grow(ostr, ostr->len * 2);

    /* In-place modification must be done back-to-front to avoid
     * overwriting the data while we read it.  Even the order of
     * the two assignments is important, to get i == 0 right. */
    for (i = ostr->len - 1; i >= 0; i--) {
        tmp = i << 1; /* tmp = i * 2; */
        ostr->data[tmp + 1] = hexits[ostr->data[i] & 0xf];
        ostr->data[tmp] = hexits[ostr->data[i] >> 4];
    }

    ostr->len = ostr->len * 2;
    ostr->data[ostr->len] = '\0';

    seems_valid(ostr);
}


int octstr_hex_to_binary(Octstr *ostr)
{
    long len, i;
    unsigned char *p;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);

    if (ostr->len == 0)
        return 0;

    /* Check if it's in the right format */
    if (!octstr_check_range(ostr, 0, ostr->len, gw_isxdigit))
        return -1;

    len = ostr->len;

    /* Convert ascii data to binary values */
    for (i = 0, p = ostr->data; i < len; i++, p++) {
        if (*p >= '0' && *p <= '9')
            *p -= '0';
        else if (*p >= 'a' && *p <= 'f')
            *p = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F')
            *p = *p - 'A' + 10;
        else {
            /* isxdigit checked the whole string, so we should
             * not be able to get here. */
            gw_assert(0);
            *p = 0;
        }
    }

    /* De-hexing will compress data by factor of 2 */
    len = ostr->len / 2;

    for (i = 0; i < len; i++) {
        ostr->data[i] = ostr->data[i * 2] * 16 | ostr->data[i * 2 + 1];
    }

    ostr->len = len;
    ostr->data[len] = '\0';

    seems_valid(ostr);
    return 0;
}


void octstr_binary_to_base64(Octstr *ostr)
{
    static const unsigned char base64[64] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    long triplets;
    long lines;
    long orig_len;
    unsigned char *data;
    long from, to;
    int left_on_line;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);

    if (ostr->len == 0) {
        /* Always terminate with CR LF */
        octstr_insert(ostr, octstr_imm("\015\012"), 0);
        return;
    }

    /* The lines must be 76 characters each (or less), and each
     * triplet will expand to 4 characters, so we can fit 19
     * triplets on one line.  We need a CR LF after each line,
     * which will add 2 octets per 19 triplets (rounded up). */
    triplets = (ostr->len + 2) / 3;   /* round up */
    lines = (triplets + 18) / 19;

    octstr_grow(ostr, triplets * 4 + lines * 2);
    orig_len = ostr->len;
    data = ostr->data;

    ostr->len = triplets * 4 + lines * 2;
    data[ostr->len] = '\0';

    /* This function works back-to-front, so that encoded data will
     * not overwrite source data.
     * from points to the start of the last triplet (which may be
     * an odd-sized one), and to points to the start of where the
     * last quad should go.  */
    from = (triplets - 1) * 3;
    to = (triplets - 1) * 4 + (lines - 1) * 2;

    /* First write the CR LF after the last quad */
    data[to + 5] = 10;   /* LF */
    data[to + 4] = 13;   /* CR */
    left_on_line = triplets - ((lines - 1) * 19);

    /* base64 encoding is in 3-octet units.  To handle leftover
     * octets, conceptually we have to zero-pad up to the next
     * 6-bit unit, and pad with '=' characters for missing 6-bit
     * units.
     * We do it by first completing the first triplet with 
     * zero-octets, and after the loop replacing some of the
     * result characters with '=' characters.
     * There is enough room for this, because even with a 1 or 2
     * octet source string, space for four octets of output
     * will be reserved.
     */
    switch (orig_len % 3) {
    case 0:
        break;
    case 1:
        data[orig_len] = 0;
        data[orig_len + 1] = 0;
        break;
    case 2:
        data[orig_len + 1] = 0;
        break;
    }

    /* Now we only have perfect triplets. */
    while (from >= 0) {
        long whole_triplet;

        /* Add a newline, if necessary */
        if (left_on_line == 0) {
            to -= 2;
            data[to + 5] = 10;  /* LF */
            data[to + 4] = 13;  /* CR */
            left_on_line = 19;
        }

        whole_triplet = (data[from] << 16) |
                        (data[from + 1] << 8) |
                        data[from + 2];
        data[to + 3] = base64[whole_triplet % 64];
        data[to + 2] = base64[(whole_triplet >> 6) % 64];
        data[to + 1] = base64[(whole_triplet >> 12) % 64];
        data[to] = base64[(whole_triplet >> 18) % 64];

        to -= 4;
        from -= 3;
        left_on_line--;
    }

    gw_assert(left_on_line == 0);
    gw_assert(from == -3);
    gw_assert(to == -4);

    /* Insert padding characters in the last quad.  Remember that
     * there is a CR LF between the last quad and the end of the
     * string. */
    switch (orig_len % 3) {
    case 0:
        break;
    case 1:
        gw_assert(data[ostr->len - 3] == 'A');
        gw_assert(data[ostr->len - 4] == 'A');
        data[ostr->len - 3] = '=';
        data[ostr->len - 4] = '=';
        break;
    case 2:
        gw_assert(data[ostr->len - 3] == 'A');
        data[ostr->len - 3] = '=';
        break;
    }

    seems_valid(ostr);
}


void octstr_base64_to_binary(Octstr *ostr)
{
    long triplet;
    long pos, len;
    long to;
    int quadpos = 0;
    int warned = 0;
    unsigned char *data;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);

    len = ostr->len;
    data = ostr->data;

    if (len == 0)
        return;

    to = 0;
    triplet = 0;
    quadpos = 0;
    for (pos = 0; pos < len; pos++) {
        int c = data[pos];
        int sixbits;

        if (c >= 'A' && c <= 'Z') {
            sixbits = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            sixbits = 26 + c - 'a';
        } else if (c >= '0' && c <= '9') {
            sixbits = 52 + c - '0';
        } else if (c == '+') {
            sixbits = 62;
        } else if (c == '/') {
            sixbits = 63;
        } else if (c == '=') {
            /* These can only occur at the end of encoded
             * text.  RFC 2045 says we can assume it really
             * is the end. */
            break;
        } else if (isspace(c)) {
            /* skip whitespace */
            continue;
        } else {
            if (!warned) {
                warning(0, "Unusual characters in base64 "
                        "encoded text.");
                warned = 1;
            }
            continue;
        }

        triplet = (triplet << 6) | sixbits;
        quadpos++;

        if (quadpos == 4) {
            data[to++] = (triplet >> 16) & 0xff;
            data[to++] = (triplet >> 8) & 0xff;
            data[to++] = triplet & 0xff;
            quadpos = 0;
        }
    }

    /* Deal with leftover octets */
    switch (quadpos) {
    case 0:
        break;
    case 3:  /* triplet has 18 bits, we want the first 16 */
        data[to++] = (triplet >> 10) & 0xff;
        data[to++] = (triplet >> 2) & 0xff;
        break;
    case 2:  /* triplet has 12 bits, we want the first 8 */
        data[to++] = (triplet >> 4) & 0xff;
        break;
    case 1:
        warning(0, "Bad padding in base64 encoded text.");
        break;
    }

    ostr->len = to;
    data[to] = '\0';

    seems_valid(ostr);
}


long octstr_parse_long(long *nump, Octstr *ostr, long pos, int base)
{
    /* strtol wants a char *, and we have to compare the result to
     * an unsigned char *.  The easiest way to avoid warnings without
     * introducing typecasts is to use two variables. */
    char *endptr;
    unsigned char *endpos;
    long number;

    seems_valid(ostr);
    gw_assert(nump != NULL);
    gw_assert(base == 0 || (base >= 2 && base <= 36));

    if (pos >= ostr->len) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    number = strtol(ostr->data + pos, &endptr, base);
    endpos = endptr;
    if (errno == ERANGE)
        return -1;
    if (endpos == ostr->data + pos) {
        errno = EINVAL;
        return -1;
    }

    *nump = number;
    return endpos - ostr->data;
}


long octstr_parse_double(double *nump, Octstr *ostr, long pos)
{
    /* strtod wants a char *, and we have to compare the result to
     * an unsigned char *.  The easiest way to avoid warnings without
     * introducing typecasts is to use two variables. */
    char *endptr;
    unsigned char *endpos;
    double number;

    seems_valid(ostr);
    gw_assert(nump != NULL);

    if (pos >= ostr->len) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    number = strtod(ostr->data + pos, &endptr);
    endpos = endptr;
    if (errno == ERANGE)
        return -1;
    if (endpos == ostr->data + pos) {
        errno = EINVAL;
        return -1;
    }

    *nump = number;
    return endpos - ostr->data;
}


int octstr_check_range(Octstr *ostr, long pos, long len,
                       octstr_func_t filter)
{
    long end = pos + len;

    seems_valid(ostr);
    gw_assert(len >= 0);

    if (pos >= ostr->len)
        return 1;
    if (end > ostr->len)
        end = ostr->len;

    for ( ; pos < end; pos++) {
        if (!filter(ostr->data[pos]))
            return 0;
    }

    return 1;
}


void octstr_convert_range(Octstr *ostr, long pos, long len,
                          octstr_func_t map)
{
    long end = pos + len;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);
    gw_assert(len >= 0);

    if (pos >= ostr->len)
        return;
    if (end > ostr->len)
        end = ostr->len;

    for ( ; pos < end; pos++) {
        ostr->data[pos] = map(ostr->data[pos]);
    }

    seems_valid(ostr);
}


static int inline make_printable(int c)
{
    return isprint(c) ? c : '.';    
}


void octstr_convert_printable(Octstr *ostr)
{
    octstr_convert_range(ostr, 0, ostr->len, make_printable);
}



int octstr_compare(const Octstr *ostr1, const Octstr *ostr2)
{
    int ret;
    long len;

    seems_valid(ostr1);
    seems_valid(ostr2);

    if (ostr1->len < ostr2->len)
        len = ostr1->len;
    else
        len = ostr2->len;

    if (len == 0) {
	if (ostr1->len == 0 && ostr2->len > 0)
	    return -1;
	if (ostr1->len > 0 && ostr2->len == 0)
	    return 1;
        return 0;
    }

    ret = memcmp(ostr1->data, ostr2->data, len);
    if (ret == 0) {
        if (ostr1->len < ostr2->len)
            ret = -1;
        else if (ostr1->len > ostr2->len)
            ret = 1;
    }
    return ret;
}


int octstr_case_compare(const Octstr *os1, const Octstr *os2)
{
    int c1, c2;
    long i, len;

    seems_valid(os1);
    seems_valid(os2);

    if (os1->len < os2->len)
        len = os1->len;
    else
        len = os2->len;

    if (len == 0) {
        if (os1->len == 0 && os2->len > 0)
            return -1;
        if (os1->len > 0 && os2->len == 0)
            return 1;
        return 0;
    }

    c1 = c2 = 0;
    for (i = 0; i < len; ++i) {
        c1 = toupper(os1->data[i]);
        c2 = toupper(os2->data[i]);
        if (c1 != c2)
            break;
    }

    if (i == len) {
        if (i == os1->len && i == os2->len)
            return 0;
        if (i == os1->len)
            return -1;
        return 1;
    } else {
        /*
        c1 = toupper(os1->data[i]);
        c2 = toupper(os2->data[i]);
        */
        if (c1 < c2)
            return -1;
        if (c1 == c2)
            return 0;
        return 1;
    }
}


int octstr_ncompare(const Octstr *ostr1, const Octstr *ostr2, long n)
{
    long len;

    seems_valid(ostr1);
    seems_valid(ostr2);

    if ((ostr1->len < ostr2->len) && (ostr1->len < n))
        len = ostr1->len;
    else if ((ostr2->len < ostr1->len) && (ostr2->len < n))
        len = ostr2->len;
    else
        len = n;

    if (len == 0)
        return 0;

    return memcmp(ostr1->data, ostr2->data, len);
}


int octstr_str_compare(const Octstr *ostr, const char *str)
{
    seems_valid(ostr);

    if (str == NULL)
        return -1;
    if (ostr->data == NULL)
	return strcmp("", str);

    return strcmp(ostr->data, str);
}


int octstr_str_case_compare(const Octstr *ostr, const char *str)
{
    seems_valid(ostr);

    if (str == NULL)
        return -1;
    if (ostr->data == NULL)
       return strcasecmp("", str);

    return strcasecmp(ostr->data, str);
}


int octstr_str_ncompare(const Octstr *ostr, const char *str, long n)
{
    seems_valid(ostr);

    if (str == NULL)
        return -1;
    if (ostr->data == NULL)
        return 1; /* str grater */

    return strncmp(ostr->data, str, n);
}


long octstr_search_char(const Octstr *ostr, int ch, long pos)
{
    unsigned char *p;

    seems_valid(ostr);
    gw_assert(ch >= 0);
    gw_assert(ch <= UCHAR_MAX);
    gw_assert(pos >= 0);

    if (pos >= ostr->len)
        return -1;

    p = memchr(ostr->data + pos, ch, ostr->len - pos);
    if (!p)
        return -1;
    return p - ostr->data;
}


long octstr_rsearch_char(const Octstr *ostr, int ch, long pos)
{
    long i;

    seems_valid(ostr);
    gw_assert(ch >= 0);
    gw_assert(ch <= UCHAR_MAX);
    gw_assert(pos >= 0);

    if (pos >= ostr->len)
        return -1;

    for (i = pos; i >= 0; i--) {
        if (ostr->data[i] == ch)
            return i;
    }

    return -1;
}


long octstr_search_chars(const Octstr *ostr, const Octstr *chars, long pos)
{
    long i, j;

    seems_valid(ostr);
    seems_valid(chars);
    gw_assert(pos >= 0);

    for (i = 0; i < octstr_len(chars); i++) {
	j = octstr_search_char(ostr, octstr_get_char(chars, i), pos);
	if (j != -1)
	    return j;
    }

    return -1;
}


long octstr_search(const Octstr *haystack, const Octstr *needle, long pos)
{
    int first;

    seems_valid(haystack);
    seems_valid(needle);
    gw_assert(pos >= 0);

    /* Always "find" an empty string */
    if (needle->len == 0)
        return 0;

    if (needle->len == 1)
        return octstr_search_char(haystack, needle->data[0], pos);

    /* For each occurrence of needle's first character in ostr,
     * check if the rest of needle follows.  Stop if there are no
     * more occurrences, or if the rest of needle can't possibly
     * fit in the haystack. */
    first = needle->data[0];
    pos = octstr_search_char(haystack, first, pos);
    while (pos >= 0 && haystack->len - pos >= needle->len) {
        if (memcmp(haystack->data + pos,
                   needle->data, needle->len) == 0)
            return pos;
        pos = octstr_search_char(haystack, first, pos + 1);
    }

    return -1;
}


long octstr_case_search(const Octstr *haystack, const Octstr *needle, long pos)
{
    long i, j;
    int c1, c2;

    seems_valid(haystack);
    seems_valid(needle);
    gw_assert(pos >= 0);

    /* Always "find" an empty string */
    if (needle->len == 0)
        return 0;

    for (i = pos; i <= haystack->len - needle->len; ++i) {
	for (j = 0; j < needle->len; ++j) {
	    c1 = toupper(haystack->data[i + j]);
	    c2 = toupper(needle->data[j]);
	    if (c1 != c2)
	    	break;
	}
	if (j == needle->len)
	    return i;
    }

    return -1;    
}

long octstr_case_nsearch(const Octstr *haystack, const Octstr *needle, long pos, long n)
{
    long i, j;
    int c1, c2;

    seems_valid(haystack);
    seems_valid(needle);
    gw_assert(pos >= 0);

    /* Always "find" an empty string */
    if (needle->len == 0)
        return 0;

    for (i = pos; i <= haystack->len - needle->len && i < n; ++i) {
        for (j = 0; j < needle->len && j < n; ++j) {
            c1 = toupper(haystack->data[i + j]);
            c2 = toupper(needle->data[j]);
            if (c1 != c2)
                break;
        }
        if (j == needle->len)
            return i;
    }

    return -1;
}


long octstr_str_search(const Octstr *haystack, const char *needle, long pos)
{
    int first;
    int needle_len;

    seems_valid(haystack);
    gw_assert(pos >= 0);

    /* Always "find" an empty string */
    if (needle == NULL || needle[0] == '\0')
        return 0;

    needle_len = strlen(needle);

    if (needle_len == 1)
        return octstr_search_char(haystack, needle[0], pos);

    /* For each occurrence of needle's first character in ostr,
     * check if the rest of needle follows.  Stop if there are no
     * more occurrences, or if the rest of needle can't possibly
     * fit in the haystack. */
    first = needle[0];
    pos = octstr_search_char(haystack, first, pos);
    while (pos >= 0 && haystack->len - pos >= needle_len) {
        if (memcmp(haystack->data + pos,
                   needle, needle_len) == 0)
            return pos;
        pos = octstr_search_char(haystack, first, pos + 1);
    }

    return -1;
}


int octstr_print(FILE *f, Octstr *ostr)
{
    gw_assert(f != NULL);
    seems_valid(ostr);

    if (ostr->len == 0)
        return 0;
    if (fwrite(ostr->data, ostr->len, 1, f) != 1) {
        error(errno, "Couldn't write all of octet string to file.");
        return -1;
    }
    return 0;
}


int octstr_pretty_print(FILE *f, Octstr *ostr)
{
    unsigned char *p;
    long i;

    gw_assert(f != NULL);
    seems_valid(ostr);

    p = ostr->data;
    for (i = 0; i < ostr->len; ++i, ++p) {
        if (isprint(*p))
            fprintf(f, "%c", *p);
        else
            fprintf(f, "\\x%02x", *p);
    }
    if (ferror(f))
        return -1;
    return 0;
}


int octstr_write_to_socket(int socket, Octstr *ostr)
{
    long len;
    unsigned char *data;
    int ret;

    gw_assert(socket >= 0);
    seems_valid(ostr);

    data = ostr->data;
    len = ostr->len;
    while (len > 0) {
        ret = write(socket, data, len);
        if (ret == -1) {
            if (errno != EINTR) {
                error(errno, "Writing to socket failed");
                return -1;
            }
        } else {
            /* ret may be less than len */
            len -= ret;
            data += ret;
        }
    }
    return 0;
}


long octstr_write_data(Octstr *ostr, int fd, long from)
{
    long ret;

    gw_assert(fd >= 0);
    gw_assert(from >= 0);
    seems_valid(ostr);

    if (from >= ostr->len)
        return 0;

    ret = write(fd, ostr->data + from, ostr->len - from);

    if (ret < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        error(errno, "Error writing %ld octets to fd %d:",
              ostr->len - from, fd);
        return -1;
    }

    return ret;
}


int octstr_append_from_socket(Octstr *ostr, int socket)
{
    unsigned char buf[4096];
    int len;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);

again:
    len = recv(socket, buf, sizeof(buf), 0);
    if (len < 0 && errno == EINTR)
        goto again;

    if (len < 0) {
        error(errno, "Could not read from socket %d", socket);
        return -1;
    }

    octstr_append_data(ostr, buf, len);
    return len;
}


void octstr_insert(Octstr *ostr1, const Octstr *ostr2, long pos)
{
    if (ostr2 == NULL)
        return;

    seems_valid(ostr1);
    seems_valid(ostr2);
    gw_assert(pos <= ostr1->len);
    gw_assert(!ostr1->immutable);

    if (ostr2->len == 0)
        return;

    octstr_grow(ostr1, ostr1->len + ostr2->len);
    memmove(ostr1->data + pos + ostr2->len, ostr1->data + pos,
            ostr1->len - pos);
    memcpy(ostr1->data + pos, ostr2->data, ostr2->len);
    ostr1->len += ostr2->len;
    ostr1->data[ostr1->len] = '\0';

    seems_valid(ostr1);
}


void octstr_truncate(Octstr *ostr, int new_len)
{
    if (ostr == NULL)
        return;
        
    seems_valid(ostr);
    gw_assert(!ostr->immutable);
    gw_assert(new_len >= 0);

    if (new_len >= ostr->len)
        return;

    ostr->len = new_len;
    ostr->data[new_len] = '\0';

    seems_valid(ostr);
}


void octstr_strip_blanks(Octstr *text)
{
    int start = 0, end, len = 0;

    seems_valid(text);
    gw_assert(!text->immutable);

    /* Remove white space from the beginning of the text */
    while (isspace(octstr_get_char(text, start)) && 
	   start <= octstr_len(text))
        start ++;

    if (start > 0)
        octstr_delete(text, 0, start);

    /* and from the end. */

    if ((len = octstr_len(text)) > 0) {
        end = len = len - 1;
        while (isspace(octstr_get_char(text, end)) && end >= 0)
            end--;
        octstr_delete(text, end + 1, len - end);
    }

    seems_valid(text);
}

static int iscrlf(unsigned char c)
{
    return c == '\n' || c == '\r';
}

void octstr_strip_crlfs(Octstr *text)
{
    int start = 0, end, len = 0;

    seems_valid(text);
    gw_assert(!text->immutable);

    /* Remove white space from the beginning of the text */
    while (iscrlf(octstr_get_char(text, start)) && 
	   start <= octstr_len(text))
        start ++;

    if (start > 0)
        octstr_delete(text, 0, start);

    /* and from the end. */

    if ((len = octstr_len(text)) > 0) {
        end = len = len - 1;
        while (iscrlf(octstr_get_char(text, end)) && end >= 0)
            end--;
        octstr_delete(text, end + 1, len - end);
    }

    seems_valid(text);
}

void octstr_strip_nonalphanums(Octstr *text)
{
    int start = 0, end, len = 0;

    seems_valid(text);
    gw_assert(!text->immutable);

    /* Remove white space from the beginning of the text */
    while (!isalnum(octstr_get_char(text, start)) && 
	   start <= octstr_len(text))
        start ++;

    if (start > 0)
        octstr_delete(text, 0, start);

    /* and from the end. */

    if ((len = octstr_len(text)) > 0) {
        end = len = len - 1;
        while (!isalnum(octstr_get_char(text, end)) && end >= 0)
            end--;
        octstr_delete(text, end + 1, len - end);
    }

    seems_valid(text);
}


void octstr_shrink_blanks(Octstr *text)
{
    int i, j, end;

    seems_valid(text);
    gw_assert(!text->immutable);

    end = octstr_len(text);

    /* Shrink white spaces to one  */
    for (i = 0; i < end; i++) {
        if (isspace(octstr_get_char(text, i))) {
            /* Change the remaining space into single space. */
            if (octstr_get_char(text, i) != ' ')
                octstr_set_char(text, i, ' ');

            j = i = i + 1;
            while (isspace(octstr_get_char(text, j)))
                j ++;
            if (j - i > 1)
                octstr_delete(text, i, j - i);
        }
    }

    seems_valid(text);
}


void octstr_insert_data(Octstr *ostr, long pos, const char *data, long len)
{
    seems_valid(ostr);
    gw_assert(!ostr->immutable);
    gw_assert(pos <= ostr->len);

    if (len == 0)
        return;

    octstr_grow(ostr, ostr->len + len);
    if (ostr->len > pos) {	/* only if neccessary*/
        memmove(ostr->data + pos + len, ostr->data + pos, ostr->len - pos);
    }
    memcpy(ostr->data + pos, data, len);
    ostr->len += len;
    ostr->data[ostr->len] = '\0';

    seems_valid(ostr);
}

void octstr_insert_char(Octstr *ostr, long pos, const char c)
{
    seems_valid(ostr);
    gw_assert(!ostr->immutable);
    gw_assert(pos <= ostr->len);
    
    octstr_grow(ostr, ostr->len + 1);
    if (ostr->len > pos)
        memmove(ostr->data + pos + 1, ostr->data + pos, ostr->len - pos);
    memcpy(ostr->data + pos, &c, 1);
    ostr->len += 1;
    ostr->data[ostr->len] = '\0';
    
    seems_valid(ostr);
}

void octstr_append_data(Octstr *ostr, const char *data, long len)
{
    gw_assert(ostr != NULL);
    octstr_insert_data(ostr, ostr->len, data, len);
}


void octstr_append(Octstr *ostr1, const Octstr *ostr2)
{
    gw_assert(ostr1 != NULL);
    octstr_insert(ostr1, ostr2, ostr1->len);
}


void octstr_append_cstr(Octstr *ostr, const char *cstr)
{
    octstr_insert_data(ostr, ostr->len, cstr, strlen(cstr));
}


void octstr_append_char(Octstr *ostr, int ch)
{
    unsigned char c = ch;

    gw_assert(ch >= 0);
    gw_assert(ch <= UCHAR_MAX);
    octstr_insert_data(ostr, ostr->len, &c, 1);
}


void octstr_delete(Octstr *ostr1, long pos, long len)
{
    seems_valid(ostr1);
    gw_assert(!ostr1->immutable);

    if (pos > ostr1->len)
        pos = ostr1->len;
    if (pos + len > ostr1->len)
        len = ostr1->len - pos;
    if (len > 0) {
        memmove(ostr1->data + pos, ostr1->data + pos + len,
                ostr1->len - pos - len);
        ostr1->len -= len;
        ostr1->data[ostr1->len] = '\0';
    }

    seems_valid(ostr1);
}



Octstr *octstr_read_file(const char *filename)
{
    FILE *f;
    Octstr *os;
    char buf[4096];
    long n;

    gw_assert(filename != NULL);

    f = fopen(filename, "r");
    if (f == NULL) {
        error(errno, "fopen failed: couldn't open `%s'", filename);
        return NULL;
    }

    os = octstr_create("");
    if (os == NULL)
        goto error;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        octstr_insert_data(os, octstr_len(os), buf, n);

    (void) fclose(f);
    return os;

error:
    (void) fclose(f);
    octstr_destroy(os);
    return NULL;
}


Octstr *octstr_read_pipe(FILE *f)
{
    Octstr *os;
    char buf[4096];

    gw_assert(f != NULL);

    os = octstr_create("");
    if (os == NULL)
        goto error;

    while (fgets(buf, sizeof(buf), f) != NULL)
        octstr_append_data(os, buf, strlen(buf));

    return os;

error:
    octstr_destroy(os);
    return NULL;
}


List *octstr_split_words(const Octstr *ostr)
{
    unsigned char *p;
    List *list;
    Octstr *word;
    long i, start, end;

    seems_valid(ostr);

    list = gwlist_create();

    p = ostr->data;
    i = 0;
    for (; ; ) {
        while (i < ostr->len && isspace(*p)) {
            ++p;
            ++i;
        }
        start = i;

        while (i < ostr->len && !isspace(*p)) {
            ++p;
            ++i;
        }
        end = i;

        if (start == end)
            break;

        word = octstr_create_from_data(ostr->data + start,
                                       end - start);
        gwlist_append(list, word);
    }

    return list;
}


List *octstr_split(const Octstr *os, const Octstr *sep)
{
    List *list;
    long next, pos, seplen;
    
    list = gwlist_create();
    pos = 0;
    seplen = octstr_len(sep);

    while ((next = octstr_search(os, sep, pos)) >= 0) {
        gwlist_append(list, octstr_copy(os, pos, next - pos));
        pos = next + seplen;
    }
    
    if (pos < octstr_len(os))
        gwlist_append(list, octstr_copy(os, pos, octstr_len(os)));
    
    return list;
}


int octstr_item_match(void *item, void *pattern)
{
    return octstr_compare(item, pattern) == 0;
}


int octstr_item_case_match(void *item, void *pattern)
{
    return octstr_case_compare(item, pattern) == 0;
}


void octstr_url_encode(Octstr *ostr)
{
    long i, n, len = 0;
    int all_safe;
    unsigned char c, *str, *str2, *res, *hexits;

    if (ostr == NULL)
        return;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);

    if (ostr->len == 0)
        return;

    /* calculate new length */
    for (i = n = 0, str = ostr->data, all_safe = 1; i < ostr->len; i++) {
        c = *str++;
	
        if (c == ' ') {
            all_safe = 0;
            continue;
        }

        if (!is_safe[c]) {
	    n++;
            all_safe = 0;
        }
     }

    if (all_safe) /* we are done, all chars are safe */
       return;

    hexits = "0123456789ABCDEF";

    /*
     * no need to reallocate if n == 0, so we make replace in place.
     * NOTE: we don't do if (xxx) ... else ... because conditional jump
     * is not so fast as just compare (alex).
     */
    res = str2 = (n ? gw_malloc((len = ostr->len + 2 * n + 1)) : ostr->data);

    for (i = 0, str = ostr->data; i < ostr->len; i++) {
        c = *str++;

        if (c == ' ') {
            *str2++ = '+';
            continue;
        }

        if (!is_safe[c]) {
            *str2++ = '%';
            *str2++ = hexits[c >> 4 & 0xf];
            *str2++ = hexits[c & 0xf];
            continue;
        }

        *str2++ = c;
    }
    *str2 = 0;
    
    /* we made replace in place */
    if (n) {
        gw_free(ostr->data);
        ostr->data = res;
        ostr->size = len;
        ostr->len = len - 1;
    }

    seems_valid(ostr);
}


int octstr_url_decode(Octstr *ostr)
{
    unsigned char *string;
    unsigned char *dptr;
    int code, code2, ret = 0;

    if (ostr == NULL)
        return 0;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);

    if (ostr->len == 0)
        return 0;

    string = ostr->data;
    dptr = ostr->data;

    do {
        if (*string == '%') {
            if (*(string + 1) == '\0' || *(string + 2) == '\0') {
                warning(0, "octstr_url_decode: corrupted end-of-string <%s>", string);
                ret = -1;
                break;
            }

            code = H2B(*(string + 1));
            code2 = H2B(*(string + 2));

            if (code == -1 || code2 == -1) {
                warning(0, "octstr_url_decode: garbage detected (%c%c%c) skipping.",
                            *string, *(string + 1), *(string + 2));
                *dptr++ = *string++;
                *dptr++ = *string++;
                *dptr++ = *string++;
                ret = -1;
                continue;
            }

            *dptr++ = code << 4 | code2;
            string += 3;
        }
        else if (*string == '+') {
            *dptr++ = ' ';
            string++;
        } else
            *dptr++ = *string++;
    } while (*string); 	/* we stop here because it terimates encoded string */

    *dptr = '\0';
    ostr->len = (dptr - ostr->data);

    seems_valid(ostr);
    return ret;
}


long octstr_get_bits(Octstr *ostr, long bitpos, int numbits)
{
    long pos;
    long result;
    int mask;
    int shiftwidth;

    seems_valid(ostr);
    gw_assert(bitpos >= 0);
    gw_assert(numbits <= 32);
    gw_assert(numbits >= 0);

    pos = bitpos / 8;
    bitpos = bitpos % 8;

    /* This also takes care of the len == 0 case */
    if (pos >= ostr->len)
        return 0;

    mask = (1 << numbits) - 1;

    /* It's easy if the range fits in one octet */
    if (bitpos + numbits <= 8) {
        /* shiftwidth is the number of bits to ignore on the right.
         * bitpos 0 is the leftmost bit. */
        shiftwidth = 8 - (bitpos + numbits);
        return (ostr->data[pos] >> shiftwidth) & mask;
    }

    /* Otherwise... */
    result = 0;
    while (bitpos + numbits > 8) {
        result = (result << 8) | ostr->data[pos];
        numbits -= (8 - bitpos);
        bitpos = 0;
        pos++;
        if (pos >= ostr->len)
            return (result << numbits) & mask;
    }

    gw_assert(bitpos == 0);
    result <<= numbits;
    result |= ostr->data[pos] >> (8 - numbits);
    return result & mask;
}

void octstr_set_bits(Octstr *ostr, long bitpos, int numbits,
                     unsigned long value)
{
    long pos;
    unsigned long mask;
    int shiftwidth;
    int bits;
    int maxlen;
    int c;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);
    gw_assert(bitpos >= 0);
    gw_assert(numbits <= 32);
    gw_assert(numbits >= 0);

    maxlen = (bitpos + numbits + 7) / 8;
    if (maxlen > ostr->len) {
        octstr_grow(ostr, maxlen);
        /* Make sure the new octets start out with value 0 */
        for (pos = ostr->len; pos < maxlen; pos++) {
            ostr->data[pos] = 0;
        }
        ostr->len = maxlen;
        ostr->data[maxlen] = 0;
    }

    mask = (1 << numbits) - 1;
    /* mask is also the largest value that fits */
    gw_assert(value <= mask);

    pos = bitpos / 8;
    bitpos = bitpos % 8;

    /* Does the range fit in one octet? */
    if (bitpos + numbits <= 8) {
        /* shiftwidth is the number of bits to ignore on the right.
         * bitpos 0 is the leftmost bit. */
        shiftwidth = 8 - (bitpos + numbits);
        /* Extract the bits we don't want to affect */
        c = ostr->data[pos] & ~(mask << shiftwidth);
        c |= value << shiftwidth;
        gw_assert(pos < ostr->len);
        ostr->data[pos] = c;
        return;
    }

    /* Otherwise... */
    /* If speed is a problem here, we could have separate cases for
     * the first octet (which may have bitpos > 0), and the rest,
     * which don't. */
    while (bitpos + numbits > 8) {
        /* We want this many bits from the value */
        bits = 8 - bitpos;
        /* There are this many bits to their right in the value */
        shiftwidth = numbits - bits;
        /* Construct a mask for "bits" bits on the far right */
        mask = (1 << bits) - 1;
        /* Get the bits we want */
        c = (value >> shiftwidth) & mask;
        /* Merge them with the bits that are already there */
        gw_assert(pos < ostr->len);
        ostr->data[pos] = (ostr->data[pos] & ~mask) | c;
        numbits -= (8 - bitpos);
        bitpos = 0;
        pos++;
    }

    gw_assert(bitpos == 0);
    gw_assert(pos < ostr->len);
    /* Set remaining bits.  This is just like the single-octet case
     * before the loop, except that we know bitpos is 0. */
    mask = (1 << numbits) - 1;
    shiftwidth = 8 - numbits;
    c = ostr->data[pos] & ~(mask << shiftwidth);
    c |= value << shiftwidth;
    ostr->data[pos] = c;

    seems_valid(ostr);
}


void octstr_append_uintvar(Octstr *ostr, unsigned long value)
{
    /* A uintvar is defined to be up to 32 bits large, so it will
     * fit in 5 octets. */
    unsigned char octets[5];
    int i;
    int start;

    /* Handle last byte separately; it has no continuation bit,
     * and must be encoded even if value is 0. */
    octets[4] = value & 0x7f;
    value >>= 7;

    for (i = 3; value > 0 && i >= 0; i--) {
        octets[i] = 0x80 | (value & 0x7f);
        value >>= 7;
    }
    start = i + 1;

    octstr_append_data(ostr, octets + start, 5 - start);
}


long octstr_extract_uintvar(Octstr *ostr, unsigned long *value, long pos)
{
    int c;
    int count;
    unsigned long ui;

    ui = 0;
    for (count = 0; count < 5; count++) {
        c = octstr_get_char(ostr, pos + count);
        if (c < 0)
            return -1;
        ui = (ui << 7) | (c & 0x7f);
        if (!(c & 0x80)) {
            *value = ui;
            return pos + count + 1;
        }
    }

    return -1;
}


void octstr_append_decimal(Octstr *ostr, long value)
{
    char tmp[128];

    sprintf(tmp, "%ld", value);
    octstr_append_cstr(ostr, tmp);
}



/**********************************************************************
 * octstr_dump... and related private functions
 */

static void octstr_dump_debug(const Octstr *ostr, int level)
{
    unsigned char *p, *d, buf[1024], charbuf[256];
    long pos;
    const int octets_per_line = 16;
    int c, this_line_begins_at;
    
    if (ostr == NULL)
        return;

    seems_valid(ostr);

    debug("gwlib.octstr", 0, "%*sOctet string at %p:", level, "",
          (void *) ostr);
    debug("gwlib.octstr", 0, "%*s  len:  %lu", level, "",
          (unsigned long) ostr->len);
    debug("gwlib.octstr", 0, "%*s  size: %lu", level, "",
          (unsigned long) ostr->size);
    debug("gwlib.octstr", 0, "%*s  immutable: %d", level, "",
          ostr->immutable);

    buf[0] = '\0';
    p = buf;
    d = charbuf;
    this_line_begins_at = 0;
    for (pos = 0; pos < octstr_len(ostr); ) {
        c = octstr_get_char(ostr, pos);
        sprintf(p, "%02x ", c);
        p = strchr(p, '\0');
        if (isprint(c))
            *d++ = c;
        else
            *d++ = '.';
        ++pos;
        if (pos - this_line_begins_at == octets_per_line) {
            *d = '\0';
            debug("gwlib.octstr", 0, "%*s  data: %s  %s", level, "",
                  buf, charbuf);
            buf[0] = '\0';
            charbuf[0] = '\0';
            p = buf;
            d = charbuf;
            this_line_begins_at = pos;
        }
    }
    if (pos - this_line_begins_at > 0) {
        *d = '\0';
        debug("gwlib.octstr", 0, "%*s  data: %-*.*s  %s", level, "",
              octets_per_line*3,
              octets_per_line*3, buf, charbuf);
    }

    debug("gwlib.octstr", 0, "%*sOctet string dump ends.", level, "");
}


/*
 * We do some pre-processor mangling here in order to reduce code for 
 * the 3 log levels info(), warning() and error() that have the same
 * argument list.
 * We need to map the function calls via ## concatenation and revert
 * to the original function call by a define.
 * The do-while loop emulates a function call.
 */

#define LLinfo info
#define LLwarning warning
#define LLerror error

#define octstr_dump_LOGLEVEL(loglevel, ostr, level) \
do { \
    unsigned char *p, *d, buf[1024], charbuf[256]; \
    long pos; \
    const int octets_per_line = 16; \
    int c, this_line_begins_at; \
    \
    if (ostr == NULL) \
        return; \
    \
    seems_valid(ostr); \
    \
    LL##loglevel(0, "%*sOctet string at %p:", level, "", \
          (void *) ostr); \
    LL##loglevel(0, "%*s  len:  %lu", level, "", \
          (unsigned long) ostr->len); \
    LL##loglevel(0, "%*s  size: %lu", level, "", \
          (unsigned long) ostr->size); \
    LL##loglevel(0, "%*s  immutable: %d", level, "", \
          ostr->immutable); \
    \
    buf[0] = '\0'; \
    p = buf; \
    d = charbuf; \
    this_line_begins_at = 0; \
    for (pos = 0; pos < octstr_len(ostr); ) { \
        c = octstr_get_char(ostr, pos); \
        sprintf(p, "%02x ", c); \
        p = strchr(p, '\0'); \
        if (isprint(c)) \
            *d++ = c; \
        else \
            *d++ = '.'; \
        ++pos; \
        if (pos - this_line_begins_at == octets_per_line) { \
            *d = '\0'; \
            LL##loglevel(0, "%*s  data: %s  %s", level, "", \
                  buf, charbuf); \
            buf[0] = '\0'; \
            charbuf[0] = '\0'; \
            p = buf; \
            d = charbuf; \
            this_line_begins_at = pos; \
        } \
    } \
    if (pos - this_line_begins_at > 0) { \
        *d = '\0'; \
        LL##loglevel(0, "%*s  data: %-*.*s  %s", level, "", \
              octets_per_line*3, \
              octets_per_line*3, buf, charbuf); \
    } \
    \
    LL##loglevel(0, "%*sOctet string dump ends.", level, ""); \
} while (0)


void octstr_dump_real(const Octstr *ostr, int level, ...)
{
    va_list p;
    unsigned int loglevel;
    
    va_start(p, level);
    loglevel = va_arg(p, unsigned int);
    va_end(p);
    
    switch (loglevel) {
        case GW_DEBUG:
            octstr_dump_debug(ostr, level);
            break;
        case GW_INFO:
            octstr_dump_LOGLEVEL(info, ostr, level);
            break;
        case GW_WARNING:
            octstr_dump_LOGLEVEL(warning, ostr, level);
            break;
        case GW_ERROR:
            octstr_dump_LOGLEVEL(error, ostr, level);
            break;
        default:
            octstr_dump_debug(ostr, level);
            break;
    }
}                             


void octstr_dump_short(Octstr *ostr, int level, const char *name)
{
    char buf[100];
    char *p;
    long i;
    int c;

    if (ostr == NULL) {
        debug("gwlib.octstr", 0, "%*s%s: NULL", level, "", name);
        return;
    }

    seems_valid(ostr);

    if (ostr->len < 20) {
        p = buf;
        for (i = 0; i < ostr->len; i++) {
            c = ostr->data[i];
            if (c == '\n') {
                *p++ = '\\';
                *p++ = 'n';
            } else if (!isprint(c)) {
                break;
            } else if (c == '"') {
                *p++ = '\\';
                *p++ = '"';
            } else if (c == '\\') {
                *p++ = '\\';
                *p++ = '\\';
            } else {
                *p++ = c;
            }
        }
        if (i == ostr->len) {
            *p++ = 0;
            /* We got through the loop without hitting nonprintable
             * characters. */
            debug("gwlib.octstr", 0, "%*s%s: \"%s\"", level, "", name, buf);
            return;
        }
    }

    debug("gwlib.octstr", 0, "%*s%s:", level, "", name);
    octstr_dump(ostr, level + 1);
}


/**********************************************************************
 * octstr_format and related private functions
 */


/*
 * A parsed form of the format string. This struct has been carefully
 * defined so that it can be initialized with {0} and it will have 
 * the correct defaults.
 */
struct format
{
    int minus;
    int zero;

    long min_width;

    int has_prec;
    long prec;

    long type;
};


static void format_flags(struct format *format, const char **fmt)
{
    int done;

    done = 0;
    do
    {
        switch (**fmt) {
        case '-':
            format->minus = 1;
            break;

        case '0':
            format->zero = 1;
            break;

        default:
            done = 1;
        }

        if (!done)
            ++(*fmt);
    } while (!done);
}


static void format_width(struct format *format, const char **fmt,
                         VALPARM(args))
{
    char *end;

    if (**fmt == '*')
    {
        format->min_width = va_arg(VALST(args), int);
        ++(*fmt);
    } else if (isdigit(**(const unsigned char **) fmt))
    {
        format->min_width = strtol(*fmt, &end, 10);
        *fmt = end;
        /* XXX error checking is missing from here */
    }
}


static void format_prec(struct format *format, const char **fmt,
                        VALPARM(args))
{
    char *end;

    if (**fmt != '.')
        return;
    ++(*fmt);
    if (**fmt == '*')
    {
        format->has_prec = 1;
        format->prec = va_arg(VALST(args), int);
        ++(*fmt);
    } else if (isdigit(**(const unsigned char **) fmt))
    {
        format->has_prec = 1;
        format->prec = strtol(*fmt, &end, 10);
        *fmt = end;
        /* XXX error checking is missing from here */
    }
}


static void format_type(struct format *format, const char **fmt)
{
    switch (**fmt) {
    case 'h':
        format->type = **fmt;
        ++(*fmt);
        break;
    case 'l':
        if (*(*fmt + 1) == 'l'){
           format->type = 'L';
           ++(*fmt);
        } else format->type = **fmt;
        ++(*fmt);
        break;
    }
}


static void convert(Octstr *os, struct format *format, const char **fmt,
                    VALPARM(args))
{
    Octstr *new;
    char *s, *pad;
    long long n;
    unsigned long long u;
    char tmpfmt[1024];
    char tmpbuf[1024];
    char c;
    void *p;

    new = NULL;

    switch (**fmt)
    {
    case 'c':
        c = va_arg(VALST(args), int);
        new = octstr_create_from_data(&c, 1);
        break;

    case 'd':
    case 'i':
        switch (format->type) {
        case 'L':
            n = va_arg(VALST(args), long long);
            break;
        case 'l':
            n = va_arg(VALST(args), long);
            break;
        case 'h':
            n = (short) va_arg(VALST(args), int);
            break;
        default:
            n = va_arg(VALST(args), int);
            break;
        }
        new = octstr_create("");
        octstr_append_decimal(new, n);
        break;

    case 'o':
    case 'u':
    case 'x':
    case 'X':
   switch (format->type) {
   case 'l':
      u = va_arg(VALST(args), unsigned long);
      break;
   case 'L':
      u = va_arg(VALST(args), unsigned long long);
      break;
   case 'h':
      u = (unsigned short) va_arg(VALST(args), unsigned int);
      break;
   default:
      u = va_arg(VALST(args), unsigned int);
      break;
   }
   tmpfmt[0] = '%';
	tmpfmt[1] = 'l';
	tmpfmt[2] = **fmt;
	tmpfmt[3] = '\0';
	sprintf(tmpbuf, tmpfmt, u);
        new = octstr_create(tmpbuf);
        break;

    case 'e':
    case 'f':
    case 'g':
        sprintf(tmpfmt, "%%");
        if (format->minus)
            strcat(tmpfmt, "-");
        if (format->zero)
            strcat(tmpfmt, "0");
        if (format->min_width > 0)
            sprintf(strchr(tmpfmt, '\0'),
                    "%ld", format->min_width);
        if (format->has_prec)
            sprintf(strchr(tmpfmt, '\0'),
                    ".%ld", format->prec);
        if (format->type != '\0')
            sprintf(strchr(tmpfmt, '\0'),
                    "%c", (int) format->type);
        sprintf(strchr(tmpfmt, '\0'), "%c", **fmt);
        snprintf(tmpbuf, sizeof(tmpbuf),
                 tmpfmt, va_arg(VALST(args), double));
        new = octstr_create(tmpbuf);
        break;

    case 's':
        s = va_arg(VALST(args), char *);
        if (format->has_prec && format->prec < (long) strlen(s))
            n = format->prec;
        else
            n = (long) strlen(s);
        new = octstr_create_from_data(s, n);
        break;

    case 'p':
    	p = va_arg(VALST(args), void *);
	sprintf(tmpfmt, "%p", p);
	new = octstr_create(tmpfmt);
	break;

    case 'S':
        new = octstr_duplicate(va_arg(VALST(args), Octstr *));
        if (!new)
            new = octstr_create("(null)");
        if (format->has_prec)
            octstr_truncate(new, format->prec);
        break;

    case 'E':
        new = octstr_duplicate(va_arg(VALST(args), Octstr *));
        if (!new)
            new = octstr_create("(null)");
        octstr_url_encode(new);
        /*
         * note: we use blind truncate - encoded character can get cut half-way.
         */
        if (format->has_prec)
            octstr_truncate(new, format->prec);
        break;

    case 'H':
        new = octstr_duplicate(va_arg(VALST(args), Octstr *));
        if (!new)
            new = octstr_create("(null)");
        /* upper case */
        octstr_binary_to_hex(new, 1);
        if (format->has_prec)
            octstr_truncate(new, (format->prec % 2 ? format->prec - 1 : format->prec));
        break;

    case '%':
    	new = octstr_create("%");
    	break;

    default:
        panic(0, "octstr_format format string syntax error.");
    }

    if (format->zero)
        pad = "0";
    else
        pad = " ";

    if (format->minus) {
        while (format->min_width > octstr_len(new))
            octstr_append_data(new, pad, 1);
    } else {
        while (format->min_width > octstr_len(new))
            octstr_insert_data(new, 0, pad, 1);
    }

    octstr_append(os, new);
    octstr_destroy(new);

    if (**fmt != '\0')
        ++(*fmt);
}


Octstr *octstr_format(const char *fmt, ...)
{
    Octstr *os;
    va_list args;

    va_start(args, fmt);
    os = octstr_format_valist(fmt, args);
    va_end(args);
    return os;
}


Octstr *octstr_format_valist_real(const char *fmt, va_list args)
{
    Octstr *os;
    size_t n;

    os = octstr_create("");

    while (*fmt != '\0') {
        struct format format = { 0, };

        n = strcspn(fmt, "%");
        octstr_append_data(os, fmt, n);
        fmt += n;

        gw_assert(*fmt == '%' || *fmt == '\0');
        if (*fmt == '\0')
            continue;

        ++fmt;
        format_flags(&format, &fmt);
        format_width(&format, &fmt, VARGS(args));
        format_prec(&format, &fmt, VARGS(args));
        format_type(&format, &fmt);
        convert(os, &format, &fmt, VARGS(args));
    }

    seems_valid(os);
    return os;
}


void octstr_format_append(Octstr *os, const char *fmt, ...)
{
    Octstr *temp;
    va_list args;

    va_start(args, fmt);
    temp = octstr_format_valist(fmt, args);
    va_end(args);
    octstr_append(os, temp);
    octstr_destroy(temp);
}


/*
 * Hash implementation ala Robert Sedgewick.
 */
unsigned long octstr_hash_key(Octstr *ostr)
{
    unsigned long b    = 378551;
    unsigned long a    = 63689;
    unsigned long hash = 0;
    unsigned long i    = 0;
    unsigned long len = octstr_len(ostr);
    const char *str = octstr_get_cstr(ostr);

    for(i = 0; i < len; str++, i++) {
        hash = hash*a+(*str);
        a = a*b;
    }

    return (hash & 0x7FFFFFFF);
}


/**********************************************************************
 * Local functions.
 */

static void seems_valid_real(const Octstr *ostr, const char *filename, long lineno,
                             const char *function)
{
    gw_assert(immutables_init);
    gw_assert_place(ostr != NULL,
                    filename, lineno, function);
    gw_assert_allocated(ostr,
                        filename, lineno, function);
    gw_assert_place(ostr->len >= 0,
                    filename, lineno, function);
    gw_assert_place(ostr->size >= 0,
                    filename, lineno, function);
    if (ostr->size == 0) {
        gw_assert_place(ostr->len == 0,
                        filename, lineno, function);
        gw_assert_place(ostr->data == NULL,
                        filename, lineno, function);
    } else {
        gw_assert_place(ostr->len + 1 <= ostr->size,
                        filename, lineno, function);
        gw_assert_place(ostr->data != NULL,
                        filename, lineno, function);
	if (!ostr->immutable)
            gw_assert_allocated(ostr->data,
                                filename, lineno, function);
        gw_assert_place(ostr->data[ostr->len] == '\0',
                        filename, lineno, function);
    }
}

int
octstr_recode (Octstr *tocode, Octstr *fromcode, Octstr *orig)
{
    Octstr *octstr_utf8 = NULL;
    Octstr *octstr_final = NULL;
    int resultcode = 0;
    
    if (octstr_case_compare(tocode, fromcode) == 0) {
	goto cleanup_and_exit;
    }

    if ((octstr_case_compare(fromcode, octstr_imm ("UTF-8")) != 0) &&
	(octstr_case_compare(fromcode, octstr_imm ("UTF8")) != 0)) {
	if (charset_to_utf8(orig, &octstr_utf8, fromcode) < 0) {
	    resultcode = -1;
	    goto cleanup_and_exit;
	}
    } else {
	octstr_utf8 = octstr_duplicate(orig);
    }

    if ((octstr_case_compare(tocode, octstr_imm ("UTF-8")) != 0) &&
	(octstr_case_compare(tocode, octstr_imm ("UTF8")) != 0)) {
	if (charset_from_utf8(octstr_utf8, &octstr_final, tocode) < 0) {
	    resultcode = -1;
	    goto cleanup_and_exit;
	}
    } else {
	octstr_final = octstr_duplicate(octstr_utf8);
    }

    octstr_truncate(orig, 0);
    octstr_append(orig, octstr_final);

 cleanup_and_exit:
    octstr_destroy (octstr_utf8);
    octstr_destroy (octstr_final);

    return resultcode;
}

void octstr_strip_char(Octstr *text, char ch)
{
    int start = 0;

    seems_valid(text);
    gw_assert(!text->immutable);

    /* Remove char from the beginning of the text */
    while ((ch == octstr_get_char(text, start)) &&
           start <= octstr_len(text))
        start ++;

    if (start > 0)
        octstr_delete(text, 0, start);

    seems_valid(text);
}

int octstr_isnum(Octstr *ostr1)
{
    int start = 0;
    char c;

    seems_valid(ostr1);
    while (start < octstr_len(ostr1)) {
        c = octstr_get_char(ostr1, start);
        if (!isdigit(c) && (c!='+'))
            return 0;
        start++;
    }
    return 1;
}

void octstr_replace(Octstr *haystack, Octstr *needle, Octstr *repl)
{
    int p = 0;
    long len, repl_len;

    len = octstr_len(needle);
    repl_len = octstr_len(repl);

    while ((p = octstr_search(haystack, needle, p)) != -1) {
        octstr_delete(haystack, p, len);
        octstr_insert(haystack, repl, p);
        p += repl_len;
    }
}

void octstr_replace_first(Octstr *haystack, Octstr *needle, Octstr *repl)
{
    int p = 0;
    long len, repl_len;

    len = octstr_len(needle);
    repl_len = octstr_len(repl);

    p = octstr_search(haystack, needle, p);
    if (p != -1) {
        octstr_delete(haystack, p, len);
        octstr_insert(haystack, repl, p);
    }
}

int octstr_symbolize(Octstr *ostr)
{
    long len, i;

    seems_valid(ostr);
    gw_assert(!ostr->immutable);

    if (ostr->len == 0)
        return 0;

    /* Check if it's in the right format */
    if (!octstr_check_range(ostr, 0, ostr->len, gw_isxdigit))
        return -1;

    len = ostr->len + (ostr->len/2);
    octstr_grow(ostr, ostr->len * 2);

    for (i = 0; i < len; i += 3)
        octstr_insert_data(ostr, i, "%", 1);

    return 1;
}

void octstr_delete_matching(Octstr *haystack, Octstr *needle)
{
    int p = 0;
    long len;

    seems_valid(haystack);
    seems_valid(needle);
    gw_assert(!haystack->immutable);
    len = octstr_len(needle);

    while ((p = octstr_search(haystack, needle, p)) != -1) {
        octstr_delete(haystack, p, len);
    }
}
 
int octstr_is_all_hex(Octstr *os)      
{
    long len, i;
    int ch;

    seems_valid(os);
    len = octstr_len(os);
    for (i = 0; i < len; ++i) {
        ch = octstr_get_char(os, i);
        if (!gw_isxdigit(ch))
            return 0;
    }

    return 1;
}

/*
 * function octstr_convert_to_html_entities()
 *      make data HTML safe by converting appropriate characters to HTML entities
 * Input: data to be inserted in HTML
 **/
void octstr_convert_to_html_entities(Octstr* input)
{
    int i;

    for (i = 0; i < octstr_len(input); ++i) {
        switch (octstr_get_char(input, i)) {
#define ENTITY(a,b) \
    case a: \
    octstr_delete(input, i, 1); \
    octstr_insert(input, octstr_imm("&" b ";"), i); \
    i += sizeof(b); break;
#include "gwlib/html-entities.def"
#undef ENTITY
        }
    }
}

/*
 * This function is meant to find html entities in an octstr.
 * The html-entities.def file must be sorted alphabetically for
 * this function to work (according to current Locale in use).
*/
static int octstr_find_entity(Octstr* input, int startfind, int endfind)
{
#define ENTITY(a,b) { a, b },
    struct entity_struct {
        int entity;
        char *entity_str;
    };
    const struct entity_struct entities[] = {
#include "html-entities.def"
        { -1, "" } /* pivot */
    };
#undef ENTITY
    int center;         /* position in table that we are about to compare */
    int matchresult;    /* result of match agains found entity name. indicates less, equal or greater */

    if (endfind == 0) {
        /* when calling this function we do not (nor even want to) know the
         * sizeof(entities). Hence this check. */
        endfind = (sizeof(entities) / sizeof(struct entity_struct)) - 1;
    }
    center = startfind + ((endfind - startfind) / 2);
    matchresult = octstr_str_compare(input, entities[center].entity_str);
    if (matchresult == 0) {
        return entities[center].entity;
    }
    if (endfind - startfind <= 1) {
        /* we are at the end of our results */
        return -1;
    }
    if  (matchresult < 0) {
        /* keep searching in first part of the table */
        return octstr_find_entity(input, startfind, center);
    } else {
        /* keep searching in last part of the table */
        return octstr_find_entity(input, center, endfind);
    }
}

/*
 * function octstr_convert_from_html_entities()
 *   convert HTML safe data back to binary data by replacing HTML entities with their
 *   respective character values
 * Input: data to be inserted in HTML
 **/
void octstr_convert_from_html_entities(Octstr* input)
{
    int startpos = 0, endpos;
    int entity;
    Octstr *match;

    while ((startpos = octstr_search_char(input, '&', startpos)) != -1) {
        endpos = octstr_search_char(input, ';', startpos + 1);
        if (endpos >= 0) {
            match = octstr_copy(input, startpos + 1, endpos - startpos - 1);
            entity = octstr_find_entity(match, 0, 0);
            if (entity >= 0) {
                octstr_delete(input, startpos, endpos - startpos + 1);
                octstr_insert_char(input, startpos, entity);
            }
            octstr_destroy(match);
        }
        startpos++;
    }
}
