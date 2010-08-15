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
 * test_mime_multipart.c - test MIME multipart convertion routines.
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gwlib/mime.h"

static void help(void) 
{
    info(0, "Usage: test_mime_multipart [options] mime-encoded-file ...");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-n number");
    info(0, "    perform opertion n times");
}

int main(int argc, char **argv)
{
    Octstr *filename = NULL;
    unsigned long num = 1, j;
    int opt;
    Octstr *mime, *mime2;
    MIMEEntity *m;

    gwlib_init();
        
    while ((opt = getopt(argc, argv, "hv:n:")) != EOF) {
        switch (opt) {
            case 'v':
                log_set_output_level(atoi(optarg));
                break;
            case 'n':
                num = atoi(optarg);
                break;
            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping.");
        }
    }
    
    if (optind == argc) {
        help();
        exit(0);
    }

    filename = octstr_create(argv[argc-1]);
    mime = octstr_read_file(octstr_get_cstr(filename));

    for (j = 1; j <= num; j++) {

    info(0,"MIME Octstr from file `%s':", octstr_get_cstr(filename));
    octstr_dump(mime, 0);

    m = mime_octstr_to_entity(mime);

    mime_entity_dump(m);

    mime2 = mime_entity_to_octstr(m);

    info(0, "MIME Octstr after reconstruction:");
    octstr_dump(mime2, 0);

    if (octstr_compare(mime, mime2) != 0) {
        error(0, "MIME content from file `%s' and reconstruction differs!", 
              octstr_get_cstr(filename));
    } else {
        info(0, "MIME Octstr compare result has been successfull.");
    }

    octstr_destroy(mime2);
    mime_entity_destroy(m);

    } /* num times */

    octstr_destroy(filename);
 
    gwlib_shutdown();

    return 0;
}

