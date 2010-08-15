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
 * test_boxc.c - test boxc connection module of bearerbox
 *
 * Stipe Tolj <stolj@wapme.de>
 */
             
#include "gwlib/gwlib.h"
#include "gw/msg.h"
#include "gw/shared.h"

static void help(void)
{
    info(0, "Usage: test_boxc [options] ...");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-h hostname");
    info(0, "    hostname where bearerbox is running (default: localhost)");
    info(0, "-p number");
    info(0, "    port for smsbox connections on bearerbox host (default: 13001)");
    info(0, "-c number");
    info(0, "    numer of sequential connections that are made and closed (default: 1)");
}

/* global variables */
static unsigned long port = 13001;
static  unsigned int no_conn = 1;
static Octstr *host;

static void run_connects(void)
{
    unsigned int i;
    Msg *msg;

    for (i = 1; i <= no_conn; i++) {

        /* connect to Kannel's bearerbox */
        connect_to_bearerbox(host, port, 0, NULL);

        /* identify ourself to bearerbox */
        msg = msg_create(admin);
        msg->admin.command = cmd_identify;
        msg->admin.boxc_id = octstr_create("test-smsbox");
        write_to_bearerbox(msg);

        /* do something, like passing MT messages */

        /* close connection and shutdown */
        close_connection_to_bearerbox();
    }
}

int main(int argc, char **argv)
{
    int opt;

    gwlib_init();

    host = octstr_create("localhost");

    while ((opt = getopt(argc, argv, "v:h:p:c:")) != EOF) {
        switch (opt) {
            case 'v':
                log_set_output_level(atoi(optarg));
                break;

            case 'h':
                octstr_destroy(host);
                host = octstr_create(optarg);
                break;

            case 'p':
                port = atoi(optarg);
                break;

            case 'c':
                no_conn = atoi(optarg);
                break;

            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping.");
        }
    }

    if (!optind) {
        help();
        exit(0);
    }

    run_connects();

    octstr_destroy(host);

    gwlib_shutdown();

    return 0;
}

