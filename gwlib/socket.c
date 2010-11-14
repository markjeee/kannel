/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/utsname.h>

#include "gwlib.h"


static Octstr *official_name = NULL;
static Octstr *official_ip = NULL;

/*
 * FreeBSD is not happy with our approach of allocating a sockaddr
 * and then filling in the fields.  It has private fields that need
 * to be initialized to 0.  This structure is used for that.
 */
static const struct sockaddr_in empty_sockaddr_in;

#ifndef UDP_PACKET_MAX_SIZE
#define UDP_PACKET_MAX_SIZE (64*1024)
#endif


int make_server_socket(int port, const char *interface_name )
{
    struct sockaddr_in addr;
    int s;
    int reuse;
    struct hostent hostinfo;
    char *buff = NULL;

    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        error(errno, "socket failed");
        goto error;
    }

    addr = empty_sockaddr_in;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (interface_name == NULL || strcmp(interface_name, "*") == 0)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else {
        if (gw_gethostbyname(&hostinfo, interface_name, &buff) == -1) {
            error(errno, "gethostbyname failed");
            goto error;
        }
        addr.sin_addr = *(struct in_addr *) hostinfo.h_addr;
    }

    reuse = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse,
                   sizeof(reuse)) == -1) {
        error(errno, "setsockopt failed for server address");
        goto error;
    }

    if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        error(errno, "bind failed");
        goto error;
    }

    if (listen(s, 10) == -1) {
        error(errno, "listen failed");
        goto error;
    }

    gw_free(buff);

    return s;

error:
    if (s >= 0)
        (void) close(s);
    gw_free(buff);
    return -1;
}


int tcpip_connect_to_server(char *hostname, int port, const char *interface_name)
{

    return tcpip_connect_to_server_with_port(hostname, port, 0, interface_name);
}


int tcpip_connect_to_server_with_port(char *hostname, int port, int our_port, const char *interface_name)
{
    struct sockaddr_in addr;
    struct sockaddr_in o_addr;
    struct hostent hostinfo;
    struct hostent o_hostinfo;
    int s, rc = -1, i;
    char *buff, *buff1;

    buff = buff1 = NULL;

    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        error(errno, "Couldn't create new socket.");
        goto error;
    }

    if (gw_gethostbyname(&hostinfo, hostname, &buff) == -1) {
        error(errno, "gethostbyname failed");
        goto error;
    }

    if (our_port > 0 || (interface_name != NULL && strcmp(interface_name, "*") != 0))  {
        int reuse;

        o_addr = empty_sockaddr_in;
        o_addr.sin_family = AF_INET;
        o_addr.sin_port = htons(our_port);
        if (interface_name == NULL || strcmp(interface_name, "*") == 0)
            o_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        else {
            if (gw_gethostbyname(&o_hostinfo, interface_name, &buff1) == -1) {
                error(errno, "gethostbyname failed");
                goto error;
            }
            o_addr.sin_addr = *(struct in_addr *) o_hostinfo.h_addr;
        }

        reuse = 1;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse)) == -1) {
            error(errno, "setsockopt failed before bind");
            goto error;
        }
        if (bind(s, (struct sockaddr *) &o_addr, sizeof(o_addr)) == -1) {
            error(errno, "bind to local port %d failed", our_port);
            goto error;
        }
    }

    i = 0;
    do {
        Octstr *ip2;

        addr = empty_sockaddr_in;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr = *(struct in_addr *) hostinfo.h_addr_list[i];

        ip2 = gw_netaddr_to_octstr(AF_INET, &addr.sin_addr);

        debug("gwlib.socket", 0, "Connecting to <%s>", octstr_get_cstr(ip2));

        rc = connect(s, (struct sockaddr *) &addr, sizeof(addr));
        if (rc == -1) {
            error(errno, "connect to <%s> failed", octstr_get_cstr(ip2));
        }
    } while (rc == -1 && hostinfo.h_addr_list[++i] != NULL);

    if (rc == -1)
        goto error;

    gw_free(buff);
    gw_free(buff1);
    return s;

error:
    error(0, "error connecting to server `%s' at port `%d'", hostname, port);
    if (s >= 0)
        close(s);
    gw_free(buff);
    gw_free(buff1);
    return -1;
}

int tcpip_connect_nb_to_server(char *hostname, int port, const char *interface_name, int *done)
{
    return tcpip_connect_nb_to_server_with_port(hostname, port, 0, interface_name, done);
}

int tcpip_connect_nb_to_server_with_port(char *hostname, int port, int our_port, const char *interface_name, int *done)
{
    struct sockaddr_in addr;
    struct sockaddr_in o_addr;
    struct hostent hostinfo;
    struct hostent o_hostinfo;
    int s, flags, rc = -1, i;
    char *buff, *buff1;

    *done = 1;
    buff = buff1 = NULL;

    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        error(errno, "Couldn't create new socket.");
        goto error;
    }

    if (gw_gethostbyname(&hostinfo, hostname, &buff) == -1) {
        error(errno, "gethostbyname failed");
        goto error;
    }

    if (our_port > 0 || (interface_name != NULL && strcmp(interface_name, "*") != 0)) {
        int reuse;

        o_addr = empty_sockaddr_in;
        o_addr.sin_family = AF_INET;
        o_addr.sin_port = htons(our_port);
        if (interface_name == NULL || strcmp(interface_name, "*") == 0)
            o_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        else {
            if (gw_gethostbyname(&o_hostinfo, interface_name, &buff1) == -1) {
                error(errno, "gethostbyname failed");
                goto error;
            }
            o_addr.sin_addr = *(struct in_addr *) o_hostinfo.h_addr;
        }

        reuse = 1;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse)) == -1) {
            error(errno, "setsockopt failed before bind");
            goto error;
        }
        if (bind(s, (struct sockaddr *) &o_addr, sizeof(o_addr)) == -1) {
            error(errno, "bind to local port %d failed", our_port);
            goto error;
        }
    }

    flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    i = 0;
    do {
        Octstr *ip2;

        addr = empty_sockaddr_in;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr = *(struct in_addr *) hostinfo.h_addr_list[i];

        ip2 = gw_netaddr_to_octstr(AF_INET, &addr.sin_addr);

        debug("gwlib.socket", 0, "Connecting nonblocking to <%s>", octstr_get_cstr(ip2));

        if ((rc = connect(s, (struct sockaddr *) &addr, sizeof(addr))) < 0) {
            if (errno != EINPROGRESS) {
                error(errno, "nonblocking connect to <%s> failed", octstr_get_cstr(ip2));
            }
        }
    } while (rc == -1 && errno != EINPROGRESS && hostinfo.h_addr_list[++i] != NULL);

    if (rc == -1 && errno != EINPROGRESS)
        goto error;

    /* May be connected immediatly
     * (if we connecting to localhost for example)
     */
    if (rc == 0) {
        *done = 0;
    }

    gw_free(buff);
    gw_free(buff1);

    return s;

error:
    error(0, "error connecting to server `%s' at port `%d'", hostname, port);
    if (s >= 0)
        close(s);
    gw_free(buff);
    gw_free(buff1);
    return -1;
}


int write_to_socket(int socket, char *str)
{
    size_t len;
    int ret;

    len = strlen(str);
    while (len > 0) {
        ret = write(socket, str, len);
        if (ret == -1) {
            if (errno == EAGAIN) continue;
            if (errno == EINTR) continue;
            error(errno, "Writing to socket failed");
            return -1;
        }
        /* ret may be less than len, if the writing was interrupted
           by a signal. */
        len -= ret;
        str += ret;
    }
    return 0;
}


int socket_set_blocking(int fd, int blocking)
{
    int flags, newflags;

    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        error(errno, "cannot get flags for fd %d", fd);
        return -1;
    }

    if (blocking)
        newflags = flags & ~O_NONBLOCK;
    else
        newflags = flags | O_NONBLOCK;

    if (newflags != flags) {
        if (fcntl(fd, F_SETFL, newflags) < 0) {
            error(errno, "cannot set flags for fd %d", fd);
            return -1;
        }
    }

    return 0;
}


int read_available(int fd, long wait_usec)
{
    fd_set rf;
    struct timeval to;
    int ret;
    div_t waits;

    gw_assert(fd >= 0);

    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    waits = div(wait_usec, 1000000);
    to.tv_sec = waits.quot;
    to.tv_usec = waits.rem;
retry:
    ret = select(fd + 1, &rf, NULL, NULL, &to);
    if (ret > 0 && FD_ISSET(fd, &rf))
        return 1;
    if (ret < 0) {
        /* In most select() implementations, to will now contain the
         * remaining time rather than the original time.  That is exactly
         * what we want when retrying after an interrupt. */
        switch (errno) {
            /*The first two entries here are OK*/
        case EINTR:
            goto retry;
        case EAGAIN:
            return 1;
            /* We are now sucking mud, figure things out here
             * as much as possible before it gets lost under
             * layers of abstraction.  */
        case EBADF:
            if (!FD_ISSET(fd, &rf)) {
                warning(0, "Tried to select on fd %d, not in the set!\n", fd);
            } else {
                warning(0, "Tried to select on invalid fd %d!\n", fd);
            }
            break;
        case EINVAL:
            /* Solaris catchall "It didn't work" error, lets apply
             * some tests and see if we can catch it. */

            /* First up, try invalid timeout*/
            if (to.tv_sec > 10000000)
                warning(0, "Wait more than three years for a select?\n");
            if (to.tv_usec > 1000000)
                warning(0, "There are only 1000000 usec in a second...\n");
            break;


        }
        return -1; 	/* some error */
    }
    return 0;
}



int udp_client_socket(void)
{
    int s;

    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
        error(errno, "Couldn't create a UDP socket");
        return -1;
    }

    return s;
}


int udp_bind(int port, const char *interface_name)
{
    int s;
    struct sockaddr_in sa;
    struct hostent hostinfo;
    char *buff = NULL;

    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
        error(errno, "Couldn't create a UDP socket");
        return -1;
    }

    sa = empty_sockaddr_in;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (strcmp(interface_name, "*") == 0)
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    else {
        if (gw_gethostbyname(&hostinfo, interface_name, &buff) == -1) {
            error(errno, "gethostbyname failed");
            gw_free(buff);
            return -1;
        }
        sa.sin_addr = *(struct in_addr *) hostinfo.h_addr;
    }

    if (bind(s, (struct sockaddr *) &sa, (int) sizeof(sa)) == -1) {
        error(errno, "Couldn't bind a UDP socket to port %d", port);
        (void) close(s);
        return -1;
    }

    gw_free(buff);

    return s;
}


Octstr *udp_create_address(Octstr *host_or_ip, int port)
{
    struct sockaddr_in sa;
    struct hostent h;
    char *buff = NULL;
    Octstr *ret;

    sa = empty_sockaddr_in;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if (strcmp(octstr_get_cstr(host_or_ip), "*") == 0) {
        sa.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (gw_gethostbyname(&h, octstr_get_cstr(host_or_ip), &buff) == -1) {
            error(0, "Couldn't find the IP number of `%s'",
                  octstr_get_cstr(host_or_ip));
            gw_free(buff);
            return NULL;
        }
        sa.sin_addr = *(struct in_addr *) h.h_addr;
    }

    ret = octstr_create_from_data((char *) &sa, sizeof(sa));
    gw_free(buff);

    return ret;
}


int udp_get_port(Octstr *addr)
{
    struct sockaddr_in sa;

    gw_assert(octstr_len(addr) == sizeof(sa));
    memcpy(&sa, octstr_get_cstr(addr), sizeof(sa));
    return ntohs(sa.sin_port);
}


Octstr *udp_get_ip(Octstr *addr)
{
    struct sockaddr_in sa;

    gw_assert(octstr_len(addr) == sizeof(sa));
    memcpy(&sa, octstr_get_cstr(addr), sizeof(sa));
    return gw_netaddr_to_octstr(AF_INET, &sa.sin_addr);
}


int udp_sendto(int s, Octstr *datagram, Octstr *addr)
{
    struct sockaddr_in sa;

    gw_assert(octstr_len(addr) == sizeof(sa));
    memcpy(&sa, octstr_get_cstr(addr), sizeof(sa));
    if (sendto(s, octstr_get_cstr(datagram), octstr_len(datagram), 0,
               (struct sockaddr *) &sa, (int) sizeof(sa)) == -1) {
        error(errno, "Couldn't send UDP packet");
        return -1;
    }
    return 0;
}


int udp_recvfrom(int s, Octstr **datagram, Octstr **addr)
{
    struct sockaddr_in sa;
    socklen_t salen;
    char *buf;
    int bytes;

    buf = gw_malloc(UDP_PACKET_MAX_SIZE);

    salen = sizeof(sa);
    bytes = recvfrom(s, buf, UDP_PACKET_MAX_SIZE, 0, (struct sockaddr *) &sa, &salen);
    if (bytes == -1) {
        if (errno != EAGAIN)
            error(errno, "Couldn't receive UDP packet");
	gw_free(buf);
        return -1;
    }

    *datagram = octstr_create_from_data(buf, bytes);
    *addr = octstr_create_from_data((char *) &sa, salen);

    gw_free(buf);

    return 0;
}


Octstr *host_ip(struct sockaddr_in addr)
{
    return gw_netaddr_to_octstr(AF_INET, &addr.sin_addr);
}


int host_port(struct sockaddr_in addr)
{
    return ntohs(addr.sin_port);
}


Octstr *get_official_name(void)
{
    gw_assert(official_name != NULL);
    return official_name;
}


Octstr *get_official_ip(void)
{
    gw_assert(official_ip != NULL);
    return official_ip;
}


static void setup_official_name(void)
{
    struct utsname u;
    struct hostent h;
    char *buff = NULL;

    gw_assert(official_name == NULL);
    if (uname(&u) == -1)
        panic(0, "uname failed - can't happen, unless " GW_NAME " is buggy.");
    if (gw_gethostbyname(&h, u.nodename, &buff) == -1) {
        error(0, "Can't find out official hostname for this host, "
              "using `%s' instead.", u.nodename);
        official_name = octstr_create(u.nodename);
        official_ip = octstr_create("127.0.0.1");
    } else {
        official_name = octstr_create(h.h_name);
        official_ip = gw_netaddr_to_octstr(AF_INET, h.h_addr);
    }
    gw_free(buff);
}


void socket_init(void)
{
    setup_official_name();
}

void socket_shutdown(void)
{
    octstr_destroy(official_name);
    official_name = NULL;
    octstr_destroy(official_ip);
    official_ip = NULL;
}


static Octstr *gw_netaddr_to_octstr4(unsigned char *src)
{
    return octstr_format("%d.%d.%d.%d", src[0], src[1], src[2], src[3]);
}


#ifdef AF_INET6
static Octstr *gw_netaddr_to_octstr6(unsigned char *src)
{
    return octstr_format(
	    	"%x:%x:%x:%x:"
		"%x:%x:%x:%x:"
		"%x:%x:%x:%x:"
		"%x:%x:%x:%x",
	         src[0],  src[1],  src[2],  src[3],
		 src[4],  src[5],  src[6],  src[7],
		 src[8],  src[9], src[10], src[11],
		src[12], src[13], src[14], src[15]);
}
#endif

Octstr *gw_netaddr_to_octstr(int af, void *src)
{
    switch (af) {
    case AF_INET:
	return gw_netaddr_to_octstr4(src);

#ifdef AF_INET6
    case AF_INET6:
	return gw_netaddr_to_octstr6(src);
#endif

    default:
	return NULL;
    }
}


int gw_accept(int fd, Octstr **client_addr)
{
    struct sockaddr_in addr;
    socklen_t addrlen;
    int new_fd;

    if (gwthread_pollfd(fd, POLLIN, -1.0) != POLLIN) {
	debug("gwlib.socket", 0, "gwthread_pollfd interrupted or failed");
	return -1;
    }
    addrlen = sizeof(addr);
    new_fd = accept(fd, (struct sockaddr *) &addr, &addrlen);
    if (new_fd == -1) {
	error(errno, "accept system call failed.");
	return -1;
    }
    *client_addr = host_ip(addr);
    debug("test_smsc", 0, "accept() succeeded, client from %s",
	  octstr_get_cstr(*client_addr));
    return new_fd;
}
