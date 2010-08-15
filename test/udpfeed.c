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

/* udpfeed.c - blindly send UDP packets to a certain port
 *
 * This little tool reads a bunch of files and sends each of them
 * to a given port as a single UDP packets.  It's useful for running
 * sets of test packets to see if any of them will crash the gateway.
 * By default, it sends them at one-second intervals.
 */

#include <unistd.h>

#include "gwlib/gwlib.h"

#define UDP_MAXIMUM (65535 - 40)

static unsigned char usage[] = "\
Usage: udpfeed [options] files...\n\
\n\
where options are:\n\
\n\
-h		help\n\
-g hostname	name of IP number of host to send to (default: localhost)\n\
-p port		port number to send to (default: 9200)\n\
-i interval	delay between packers (default: 1.0 seconds)\n\
\n\
Each file will be sent as a single packet.\n\
";

static Octstr *hostname;
static int port = 9200;  /* By default, the sessionless WSP port */
static double interval = 1.0;  /* Default interval (seconds) between packets */
static long maxsize = UDP_MAXIMUM;  /* Maximum packet size in octets */

static void help(void) {
	info(0, "\n%s", usage);
}

static void send_file(int udpsock, char *filename, Octstr *address) {
	Octstr *contents;

	contents = octstr_read_file(filename);
	if (contents == NULL) {
		info(0, "Skipping \"%s\".", filename);
		return;
	}

	info(0, "Sending \"%s\", %ld octets.", filename, octstr_len(contents));

	if (octstr_len(contents) > maxsize) {
		octstr_truncate(contents, maxsize);
		warning(0, "Truncating to %ld octets.", maxsize);
	}

	udp_sendto(udpsock, contents, address);

	octstr_destroy(contents);
}

int main(int argc, char **argv) {
	int opt;
	Octstr *address;
	int udpsock;

	gwlib_init();

	/* Set defaults that can't be set statically */
	hostname = octstr_create("localhost");

	while ((opt = getopt(argc, argv, "hg:p:i:m:")) != EOF) {
		switch(opt) {
		case 'g':
			octstr_destroy(hostname);
			hostname = octstr_create(optarg);
			break;

		case 'p':
			port = atoi(optarg);
			break;

		case 'i':
			interval = atof(optarg);
			break;

		case 'm':
			maxsize = atol(optarg);
			if (maxsize > UDP_MAXIMUM) {
				maxsize = UDP_MAXIMUM;
				warning(0, "-m: truncated to UDP maximum of"
					"%ld bytes.", maxsize);
			}
			break;

		case 'h':
			help();
			exit(0);
			break;

		case '?':
		default:
			error(0, "Unknown option '%c'", opt);
			help();
			exit(1);
			break;
		}
	}

	address = udp_create_address(hostname, port);
	udpsock = udp_client_socket();
	if (udpsock < 0)
		exit(1);

	for ( ; optind < argc; optind++) {
		send_file(udpsock, argv[optind], address);
		if (interval > 0 && optind + 1 < argc)
			gwthread_sleep(interval);
	}

	octstr_destroy(address);
	octstr_destroy(hostname);
	gwlib_shutdown();
    	return 0;
}
