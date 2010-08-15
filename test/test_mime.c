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
 * test_mime.c: A simple program for testing the mime parser
 *
 * We add carriage returs to the source, so that output of some editors are 
 * acceptable and remove them from component files, in case someone likes to 
 * compile them (come on, this is only a test program).
 * We panic on errors, so that this function can be used by a shell script
 * for testing purposes.
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#include <stdio.h>

#include "gwlib/gwlib.h"
#include "gw/wap_push_pap_mime.h"

static void prepend_crlf(Octstr **os)
{
    octstr_insert(*os, octstr_imm("\r\n"), 0);
}

static void append_crlf(Octstr *os)
{
    octstr_append(os, octstr_imm("\r\n"));
}

static void add_crs(Octstr *os)
{
    long i;
    Octstr *nos;

    if (os == NULL)
        return;

    nos = octstr_format("%c", '\r');
    i = 0;
    while (i < octstr_len(os)) {
        if (octstr_get_char(os, i) == '\n') {
	    octstr_insert(os, nos, i);
            ++i;
        }
        ++i;
    }

    octstr_destroy(nos);
}

static void remove_crs(Octstr *os)
{
    long i;

    if (os == NULL)
        return;

    i = 0;
    while (i < octstr_len(os)) {
        if (octstr_get_char(os, i) == '\r') {
	    octstr_delete(os, i, 1);
            --i;
        }
        ++i;
    }
}

static int skip_tail(Octstr **os, int delimiter)
{
    long delimiter_pos;

    if ((delimiter_pos = octstr_search_char(*os, delimiter, 0)) == -1)
        return 0;

    octstr_delete(*os, delimiter_pos, octstr_len(*os) - delimiter_pos);

    return 1;
}

static void help(void)
{
    info(0, "Usage: test_mime [options] source_file");
    info(0, "Parse source file into component parts.");
    info(0, "Source file has the following format:");
    info(0, "    boundary=<mime boundary>;");
    info(0, "    content=<mime content>;");
    info(0, "Content headers are added into the content_file. This file has");
    info(0, "following format:");
    info(0, "    headers=<content_headers>;");
    info(0, "    content=<push-content>;");
    info(0, "And options are");
    info(0, "    -h");
    info(0, "print this info");
    info(0, "    -d filename");
    info(0, "store push data to file filename. Default test/data.txt");
    info(0, "    -c filename");
    info(0, "store push control message to file filename. Default");
    info(0, " test/pap.txt");
    info(0, "    -s");
    info(0, "write push control message and push data to standard output");
    info(0, "Default write it to the file.");
}

int main(int argc, char **argv)
{
    Octstr *mime_content,
           *pap_content,
           *push_data,
           *rdf_content,
           *boundary,
           *push_content_file = NULL,
           *this_header,
           *pap_osname,
           *data_osname;
    List *content_headers,
         *source_parts;
    char *pap_content_file,
         *push_data_file,
         *rdf_content_file;
    int ret,
        std_out,
        opt,
        d_file,
        c_file;
    FILE *fp1,
         *fp2,
         *fp3;

    gwlib_init();
    std_out = 0;
    d_file = 0;
    c_file = 0;
    data_osname = NULL;
    pap_osname = NULL;

    while ((opt = getopt(argc, argv, "hd:sc:")) != EOF) {
        switch(opt) {
            case 'h':
                help();
                exit(1);
            break;

            case 'd':
	        d_file = 1;
                data_osname = octstr_create(optarg);
            break;

            case 'c':
	        c_file = 1;
                pap_osname = octstr_create(optarg);
            break;

            case 's':
                std_out = 1;
            break;

            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping");
            break;
        }
    }    

    if (optind >= argc) {
        help();
        panic(0, "missing arguments, stopping");
    }

    if (!c_file)
        pap_content_file = "test/pap.txt";
    else
        pap_content_file = octstr_get_cstr(pap_osname);
    if (!d_file)
        push_data_file = "test/data.txt";
    else
        push_data_file = octstr_get_cstr(data_osname);
    rdf_content_file = "test/rdf.txt";

    mime_content = octstr_read_file(argv[optind]);
    if (mime_content == NULL) {
        octstr_destroy(mime_content);
        error(0, "No MIME source");
        panic(0, "Stopping");
    }

    source_parts = octstr_split(mime_content, octstr_imm("content="));
    if (gwlist_len(source_parts) == 1) {     /* a hack to circumvent a bug */
        error(0, "Badly formatted source:");
        octstr_destroy(mime_content);
        gwlist_destroy(source_parts, octstr_destroy_item);
        panic(0, "Stopping");
    }

    boundary = gwlist_extract_first(source_parts);
    octstr_delete(boundary, 0, octstr_len(octstr_imm("boundary=")));
    if (skip_tail(&boundary, ';') == 0) {
        error(0, "Cannot determine boundary, no delimiter; possible");
        octstr_dump(boundary, 0);
        goto no_parse;
    }
    
    octstr_destroy(mime_content);
    mime_content = gwlist_extract_first(source_parts);
    if (skip_tail(&mime_content, ';') == 0){
        error(0, "Cannot determine mime content, no delimiter");
        octstr_dump(mime_content, 0);
        goto no_parse;
    }
    prepend_crlf(&mime_content);
    add_crs(mime_content);
    append_crlf(mime_content);
    
    ret = mime_parse(boundary, mime_content, &pap_content, &push_data, 
                     &content_headers, &rdf_content);
    if (ret == 0) {
        error(0, "Mime_parse returned 0, cannot continue");
        goto error;
    }

    remove_crs(pap_content);
    if (!std_out) {
        fp1 = fopen(pap_content_file, "a");
        if (fp1 == NULL) {
            error(0, "Cannot open the file for pap control message");
            goto error;
        }
        octstr_print(fp1, pap_content);
        debug("test.mime", 0, "pap control message appended to the file");
        fclose(fp1);
    } else {
        debug("test.mime", 0, "pap control message was");
        octstr_dump(pap_content, 0);
    }

    remove_crs(push_data);
    if (!std_out) {
        fp2 = fopen(push_data_file, "a");
        if (fp2 == NULL) {
            error(0, "Cannot open the push data file");
            goto error;
        }
        push_content_file = octstr_create("");
        octstr_append(push_content_file, octstr_imm("headers="));
        while (gwlist_len(content_headers) > 0) {
            octstr_append(push_content_file, 
                          this_header = gwlist_extract_first(content_headers));
            octstr_format_append(push_content_file, "%c", ' ');
            octstr_destroy(this_header);
        }
        octstr_append(push_content_file, octstr_imm(";\n"));
        octstr_append(push_content_file, octstr_imm("content="));
        octstr_append(push_content_file, push_data);
        octstr_append(push_content_file, octstr_imm(";\n"));
        octstr_print(fp2, push_content_file);
        debug("test.mime", 0, "push content appended to the file");
        fclose(fp2);
    } else {
        debug("test.mime", 0, "Content headers were");
        http_header_dump(content_headers);
        debug("test.mime", 0, "And push content itself");
        octstr_dump(push_data, 0);
    }

    if (rdf_content != NULL)
        remove_crs(rdf_content);
    if (!std_out && rdf_content != NULL) {
        fp3 = NULL;
        if (rdf_content != NULL) {
            fp3 = fopen(rdf_content_file, "a");
            if (fp3 == NULL) {
                error(0, "Cannot open the rdf file");
                goto cerror;
             }
            octstr_print(fp3, rdf_content);
            debug("test.mime", 0, "push caps message appended to the file");
            fclose(fp3);
        }
    } else {
        if (rdf_content != NULL) {
            debug("test.mime", 0, "push caps message was");
            octstr_dump(rdf_content, 0);
        }
    }
    
    octstr_destroy(boundary);
    octstr_destroy(mime_content);
    octstr_destroy(pap_content);
    octstr_destroy(push_data);
    octstr_destroy(rdf_content);
    octstr_destroy(pap_osname);
    octstr_destroy(data_osname);
    http_destroy_headers(content_headers);
    gwlist_destroy(source_parts, octstr_destroy_item);
    octstr_destroy(push_content_file);
    gwlib_shutdown();

    info(0, "MIME data parsed successfully");
    return 0;

no_parse:
    octstr_destroy(mime_content);
    octstr_destroy(pap_osname);
    octstr_destroy(data_osname);
    gwlist_destroy(source_parts, octstr_destroy_item);
    octstr_destroy(boundary);
    gwlib_shutdown();
    panic(0, "Stopping");

error:
    octstr_destroy(mime_content);
    gwlist_destroy(source_parts, octstr_destroy_item);
    octstr_destroy(boundary);
    octstr_destroy(pap_content);
    octstr_destroy(push_data);
    octstr_destroy(pap_osname);
    octstr_destroy(data_osname);
    http_destroy_headers(content_headers);
    octstr_destroy(rdf_content);
    gwlib_shutdown();
    panic(0, "Stopping");

cerror:
    octstr_destroy(mime_content);
    gwlist_destroy(source_parts, octstr_destroy_item);
    octstr_destroy(boundary);
    octstr_destroy(pap_content);
    octstr_destroy(push_data);
    octstr_destroy(push_content_file);
    octstr_destroy(pap_osname);
    octstr_destroy(data_osname);
    http_destroy_headers(content_headers);
    octstr_destroy(rdf_content);
    gwlib_shutdown();
    panic(0, "Stopping");
/* return after panic always required by gcc */
    return 1;
}













