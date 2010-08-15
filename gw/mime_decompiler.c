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
 * mime_decompiler.c - decompiling application/vnd.wap.multipart.* 
 *                     to multipart/ *
 *
 * This is a header for Mime decompiler for decompiling binary mime
 * format to text mime format, which is used for transmitting POST  
 * data from mobile terminal to decrease the use of the bandwidth.
 *
 * See comments below for explanations on individual functions.
 *
 * Bruno Rodrigues
 */

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "gwlib/gwlib.h"
#include "wap/wsp.h"
#include "wap/wsp_headers.h"
#include "wap/wsp_strings.h"
#include "mime_decompiler.h"

/***********************************************************************
 * Declarations of data types. 
 */

int mime_decompile(Octstr *binary_mime, Octstr **mime)
{ 
    char *boundary = "kannel_boundary";
    ParseContext *context;
    long mime_parts;
    long i, j;
    unsigned long headers_len, data_len;

    i = mime_parts = headers_len = data_len = 0;

    debug("wap.wsp.multipart.form.data", 0, "MIMEDEC: begining decoding");

    if(binary_mime == NULL || octstr_len(binary_mime) < 1) {
        warning(0, "MIMEDEC: invalid mime, ending");
        return -1;
    }
    *mime = octstr_create("");

    /* already dumped in deconvert_content
    debug("mime", 0, "MMSDEC: binary mime dump:");
    octstr_dump(binary_mime, 0);
    */

    context = parse_context_create(binary_mime);
    debug("mime", 0, "MIMEDEC: context created");

    mime_parts = parse_get_uintvar(context);
    debug("mime", 0, "MIMEDEC: mime has %ld multipart entities", mime_parts);
    if(mime_parts == 0) {
        debug("mime", 0, "MIMEDEC: mime has none multipart entities, ending");
        return 0;
    }

    while(parse_octets_left(context) > 0) {
        Octstr *headers, *data;
        List *gwlist_headers;
        i++;
    
        octstr_append(*mime, octstr_imm("--"));
        octstr_append(*mime, octstr_imm(boundary));
        octstr_append(*mime, octstr_imm("\n"));

        headers_len = parse_get_uintvar(context);
        data_len = parse_get_uintvar(context);
        debug("mime", 0, "MIMEDEC[%ld]: headers length <0x%02lx>, "
                         "data length <0x%02lx>", i, headers_len, data_len);

        if((headers = parse_get_octets(context, headers_len)) != NULL) {
            gwlist_headers = wsp_headers_unpack(headers, 1);
            for(j=0; j<gwlist_len(gwlist_headers);j++) {
                octstr_append(*mime, gwlist_get(gwlist_headers, j));
                octstr_append(*mime, octstr_imm("\n"));
            }
        } else {
            error(0, "MIMEDEC[%ld]: headers length is out of range, ending", i);
            return -1; 
        }

        if((data = parse_get_octets(context, data_len)) != NULL ||
           (i = mime_parts && /* XXX SE-T610 eats last byte, which is generally null */
	    (data = parse_get_octets(context, data_len - 1)) != NULL)) { 
            debug("mime", 0, "MMSDEC[%ld]: body [%s]", i, octstr_get_cstr(data));
            octstr_append(*mime, octstr_imm("\n"));
            octstr_append(*mime, data);
            octstr_append(*mime, octstr_imm("\n"));
        } else {
            error(0, "MIMEDEC[%ld]: data length is out of range, ending", i);
            return -1;
        }
    }
    octstr_append(*mime, octstr_imm("--"));
    octstr_append(*mime, octstr_imm(boundary));
    octstr_append(*mime, octstr_imm("--\n"));

    /* already dumped in deconvert_content
    debug("mime", 0, "MMSDEC: text mime dump:");
    octstr_dump(*mime, 0);
    */

    return 0;
}

