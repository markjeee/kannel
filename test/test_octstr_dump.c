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
 * test_octstr_dump.c - reads a file and performs dumping.
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

int main(int argc, char **argv)
{
    Octstr *data, *filename, *hex;

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    if (argc < 2)
        panic(0, "Syntax: %s <file>\n", argv[0]);

    filename = octstr_create(argv[1]);
    data = octstr_read_file(octstr_get_cstr(filename));

    if (data == NULL)
        panic(0, "Cannot read file.");

    /* 
     * We test if this is a text/plain file with hex values in it.
     * Therefore copy the data and trail off any CR and LF from 
     * beginning and end and test if the result is only hex chars.
     * If yes, then convert to binary before dumping.
     */
    hex = octstr_duplicate(data);
    octstr_strip_crlfs(hex);
    if (octstr_is_all_hex(hex)) {
        debug("",0,"Trying to converting from hex to binary.");
        if (octstr_hex_to_binary(hex) == 0) {
            FILE *f = fopen(argv[2], "w");
            debug("",0,"Convertion was successfull. Writing binary content to file `%s'",
                  argv[2]);
            octstr_destroy(data);
            data = octstr_duplicate(hex);
            octstr_print(f, data);
            fclose(f);
        } else {
            debug("",0,"Failed to convert from hex?!");
        }
    }                                      

    debug("",0,"Dumping file `%s':", octstr_get_cstr(filename));
    octstr_dump(data, 0);

    octstr_destroy(data);
    octstr_destroy(hex);
    gwlib_shutdown();
    return 0;
}
