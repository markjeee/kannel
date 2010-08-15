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
 * General useful socket functions
 */

#ifndef GW_SOCKET_H
#define GW_SOCKET_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "gw-config.h"

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#include "octstr.h"

/* Return the official and fully qualified domain name of the host. 
   Caller should treat this as read-only. Caller MUST NOT destroy it. */
Octstr *get_official_name(void);

/* Return an official IP number for the host. Caller should treat this 
   as read-only. Caller MUST NOT destroy it. Note that there can be
   multiple official IP numbers for the host.
   */
Octstr *get_official_ip(void);

/* Open a server socket. Return -1 for error, >= 0 socket number for OK.*/
int make_server_socket(int port, const char *interface_name);

/* Open a client socket. */
int tcpip_connect_to_server(char *hostname, int port, const char *interface_name);

/* As above, but binds our end to 'our_port' */
int tcpip_connect_to_server_with_port(char *hostname, int port, int our_port,
		const char *interface_name);

/* Open a client socket in nonblocking mode, done is 0 if socket 
   connected  immediatly, overwise done is 1 */
int tcpip_connect_nb_to_server(char *hostname, int port, const char *interface_name,
                               int *done);

/* As above, but binds our end to 'our_port' */
int tcpip_connect_nb_to_server_with_port(char *hostname, int port, int our_port,
                                         const char *interface_name, int *done);

/* Write string to socket. */
int write_to_socket(int socket, char *str);

/* Set socket to blocking or non-blocking mode.  Return -1 for error,
 * 0 for success. */
int socket_set_blocking(int socket, int blocking);

/* Check if there is something to be read in 'fd'. Return 1 if there
 * is data, 0 otherwise, -1 on error */
int read_available(int fd, long wait_usec);


/*
 * Create a UDP socket for receiving from clients. Return -1 for failure,
 * a socket file descriptor >= 0 for OK.
 */
int udp_bind(int port, const char *interface_name);


/*
 * Create the client end of a UDP socket (i.e., a UDP socket that can
 * be on any port). Return -1 for failure, a socket file descriptor >= 0 
 * for OK.
 */
int udp_client_socket(void);


/*
 * Encode a hostname or IP number and port number into a binary address,
 * and return that as an Octstr. Return NULL if the host doesn't exist
 * or the IP number is syntactically invalid, or the port is bad.
 */
Octstr *udp_create_address(Octstr *host_or_ip, int port);


/*
 * Return the IP number of an encoded binary address, as a cleartext string.
 */
Octstr *udp_get_ip(Octstr *addr);


/*
 * Return the port number of an encoded binary address, as a cleartext string.
 */
int udp_get_port(Octstr *addr);


/*
 * Send a UDP message to a given server.
 */
int udp_sendto(int s, Octstr *datagram, Octstr *addr);


/*
 * Receive a UDP message from a client.
 */
int udp_recvfrom(int s, Octstr **datagram, Octstr **addr);


/*
 * Create an Octstr of character representation of an IP
 */
Octstr *host_ip(struct sockaddr_in addr);


/*
 * Return the port number of an IP connection.
 */
int host_port(struct sockaddr_in addr);


/*
 * This must be called before sockets are used. gwlib_init does that
 */
void socket_init(void);


/*
 * Likewise, shutdown, called by gwlib_shutdown
 */
void socket_shutdown(void);

/*
 *  Converts an address of various types to an Octstr representation.
 *  Similar to host_ip, but works with more than IPv4
 */
Octstr *gw_netaddr_to_octstr(int af, void* src);


/*
 * Do an accept() system call for the given file descriptor. Return -1
 * for error (from accept or gwthread_poll, or gwthread_poll was 
 * interrupted by gwthread_wakeup) or the new file descriptor for success. 
 * Return IP number (as formatted by host_ip) via *client_addr.
 */
int gw_accept(int fd, Octstr **client_addr);



#endif
