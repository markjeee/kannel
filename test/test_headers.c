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
 * test_headers.c - test wsp header packing and unpacking.
 *
 * Richard Braakman <dark@wapit.com>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "wap/wsp_headers.h"
#include "wap/wsp_strings.h"


/* Test the http_header_combine function. */
static void test_header_combine(void)
{
    List *old;
    List *new;
    List *tmp;

    old = http_create_empty_headers();
    new = http_create_empty_headers();
    tmp = http_create_empty_headers();

    http_header_add(old, "Accept", "text/html");
    http_header_add(old, "Accept", "text/plain");
    http_header_add(old, "Accept-Language", "en");
    http_header_add(old, "Accept", "image/jpeg");

    http_header_combine(tmp, old);
    if (gwlist_len(tmp) != 4) {
        error(0, "http_combine_header with an empty 'old' did not just append.");
    }

    http_header_combine(old, new);
    if (gwlist_len(old) != 4) {
        error(0, "http_combine_header with an empty 'new' changed 'old'.");
    }

    http_header_add(new, "Accept", "text/html");
    http_header_add(new, "Accept", "text/plain");
    
    http_header_combine(old, new);
    if (gwlist_len(old) != 3 ||
        octstr_compare(gwlist_get(old, 0),
                       octstr_imm("Accept-Language: en")) != 0 ||
        octstr_compare(gwlist_get(old, 1),
                       octstr_imm("Accept: text/html")) != 0 ||
        octstr_compare(gwlist_get(old, 2),
                       octstr_imm("Accept: text/plain")) != 0) {
        error(0, "http_header_combine failed.");
    }

    http_destroy_headers(old);
    http_destroy_headers(new);
    http_destroy_headers(tmp);
}
 

static void split_headers(Octstr *headers, List **split, List **expected)
{
    long start;
    long pos;

    *split = gwlist_create();
    *expected = gwlist_create();
    start = 0;
    for (pos = 0; pos < octstr_len(headers); pos++) {
        if (octstr_get_char(headers, pos) == '\n') {
            int c;
            Octstr *line;

            if (pos == start) {
                /* Skip empty lines */
                start = pos + 1;
                continue;
            }

            line = octstr_copy(headers, start, pos - start);
            start = pos + 1;

            c = octstr_get_char(line, 0);
            octstr_delete(line, 0, 2);
            if (c == '|') {
                gwlist_append(*split, line);
                gwlist_append(*expected, octstr_duplicate(line));
            } else if (c == '<') {
                gwlist_append(*split, line);
            } else if (c == '>') {
                gwlist_append(*expected, line);
            } else if (c == '#') {
                /* comment */
                octstr_destroy(line);
            } else {
                warning(0, "Bad line in test headers file");
                octstr_destroy(line);
            }
        }
    }
}

int main(int argc, char **argv)
{
    Octstr *headers;
    List *expected;
    List *split;
    Octstr *packed;
    List *unpacked;
    Octstr *filename;
    long i;
    int mptr;

    gwlib_init();
    wsp_strings_init();

    mptr = get_and_set_debugs(argc, argv, NULL);
    if (argc - mptr <= 0)
        panic(0, "Usage: test_headers [options] header-file");

    filename = octstr_create(argv[mptr]);
    headers = octstr_read_file(octstr_get_cstr(filename));
    split_headers(headers, &split, &expected);
    packed = wsp_headers_pack(split, 0, WSP_1_2);
    unpacked = wsp_headers_unpack(packed, 0);

    if (gwlist_len(unpacked) != gwlist_len(expected)) {
        error(0, "Expected %ld headers, generated %ld.\n",
              gwlist_len(expected), gwlist_len(unpacked));
    } else {
        for (i = 0; i < gwlist_len(unpacked); i++) {
            Octstr *got, *exp;
            got = gwlist_get(unpacked, i);
            exp = gwlist_get(expected, i);
            if (octstr_compare(got, exp) != 0) {
                error(0, "Exp: %s", octstr_get_cstr(exp));
                error(0, "Got: %s", octstr_get_cstr(got));
            }
        }
    }

    test_header_combine();

    octstr_destroy(headers);
    octstr_destroy(filename);
    gwlist_destroy(split, octstr_destroy_item);
    gwlist_destroy(expected, octstr_destroy_item);
    octstr_destroy(packed);
    gwlist_destroy(unpacked, octstr_destroy_item);

    wsp_strings_shutdown();
    gwlib_shutdown();
    return 0;
}
