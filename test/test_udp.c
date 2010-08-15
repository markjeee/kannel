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
 * test_udp.c - program to test UDP packet functions
 *
 * This program implements a simple ping-pong server.
 *
 * Lars Wirzenius
 */

#include "gwlib/gwlib.h"
#include <string.h>

static char usage[] = "\
Usage: test_udp client server_port\n\
       test_udp server server_port\n\
";

#define PING "ping"
#define PONG "pong"
#define TIMES 10

static void client(int port) {
	int i, s;
	Octstr *ping, *pong, *addr, *from;
	
	s = udp_client_socket();
	ping = octstr_create(PING);
	addr = udp_create_address(octstr_create("localhost"), port);
	if (s == -1 || addr == NULL)
		panic(0, "Couldn't set up client socket.");

	for (i = 0; i < TIMES; ++i) {
		if (udp_sendto(s, ping, addr) == -1)
			panic(0, "Couldn't send ping.");
		if (udp_recvfrom(s, &pong, &from) == -1)
			panic(0, "Couldn't receive pong");
		info(0, "Got <%s> from <%s:%d>", octstr_get_cstr(pong),
			octstr_get_cstr(udp_get_ip(from)), 
			udp_get_port(from));
	}
}

static void server(int port) {
	int i, s;
	Octstr *ping, *pong, *from;
	
	s = udp_bind(port,"0.0.0.0");
	pong = octstr_create(PONG);
	if (s == -1)
		panic(0, "Couldn't set up client socket.");

	for (i = 0; i < TIMES; ++i) {
		if (udp_recvfrom(s, &ping, &from) == -1)
			panic(0, "Couldn't receive ping");
		info(0, "Got <%s> from <%s:%d>", octstr_get_cstr(ping),
			octstr_get_cstr(udp_get_ip(from)), 
			udp_get_port(from));
		if (udp_sendto(s, pong, from) == -1)
			panic(0, "Couldn't send pong.");
	}
}

int main(int argc, char **argv) {
	int port;
	
	gwlib_init();

	if (argc != 3)
		panic(0, "Bad argument list\n%s", usage);
	
	port = atoi(argv[2]);

	if (strcmp(argv[1], "client") == 0)
		client(port);
	else
		server(port);
	return 0;
}
