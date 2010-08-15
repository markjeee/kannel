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
 * test_pap.c: A simple program to test pap compiler
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#include <stdio.h>

#include "gwlib/gwlib.h"
#include "gw/wap_push_pap_compiler.h"
#include "wap/wap_events.h"

static void help (void)
{
    info(0, "Usage test_pap [option] pap_source");
    info(0, "where options are");
    info(0, "-h print this text");
    info(0, "-v level set log level for stderr logging");
    info(0, "-l log wap event to this file");
}

int main(int argc, char **argv)
{
    int opt,
        ret;
    Octstr *pap_doc,
           *log_file;
    WAPEvent *e;

    log_file = NULL;
    gwlib_init();
    
    while ((opt = getopt(argc, argv, "h:v:l:")) != EOF) {
        switch (opt) {
        case 'h':
	    help();
            exit(1);
	break;

        case 'v':
	    log_set_output_level(atoi(optarg));
	break;

        case 'l':
	    octstr_destroy(log_file);
	    log_file = octstr_create(optarg);
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
        error(0, "Missing arguments");
        help();
        panic(0, "Stopping");
    }

    if (log_file != NULL) {
    	log_open(octstr_get_cstr(log_file), GW_DEBUG, GW_NON_EXCL);
	octstr_destroy(log_file);
    }

    pap_doc = octstr_read_file(argv[optind]);
    if (pap_doc == NULL)
        panic(0, "Cannot read the pap document");

    e = NULL;
    ret = pap_compile(pap_doc, &e);
    
    if (ret < 0) {
        debug("test.pap", 0, "Unable to compile the pap document, rc %d", ret); 
        return 1;           
    } 

    debug("test.pap", 0, "Compiling successfull, wap event being:\n");
    wap_event_dump(e);

    wap_event_destroy(e);
    octstr_destroy(pap_doc);
    gwlib_shutdown();
    return 0;
}





