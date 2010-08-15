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

/* gwpoll.c - implement poll() for systems that don't have it */

#include "gwlib/gwlib.h"

#ifndef HAVE_SYS_POLL_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

int gw_poll(struct pollfd *fdarray, unsigned int numfds, int timeout)
{
    struct timeval tv, *tvp;
    unsigned int i;
    int maxfd;
    fd_set readfds, *rfdp;
    fd_set writefds, *wfdp;
    fd_set exceptfds, *xfdp;
    int ret;
    int result;

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    maxfd = -1;
    /* These are the pointers we will pass to select().  We use them because
     * we may want to pass NULL for some of them. */
    tvp = NULL;
    rfdp = NULL;
    wfdp = NULL;
    xfdp = NULL;

    /* Deal with timeout.  We get it in milliseconds.  If it's negative,
     * block indefinitely, which we do in select() by passing a NULL
     * timeval pointer. */
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        tvp = &tv;
    }

    /* Deal with fdarray, and convert it to the three fd_sets used by select. */
    for (i = 0; i < numfds; i++) {
        int fd = fdarray[i].fd;
        int events = fdarray[i].events;
        if (fd < 0)
            continue;
        if (events & POLLIN) {
            FD_SET(fd, &readfds);
            rfdp = &readfds;
	}
        if (events & POLLOUT) {
            FD_SET(fd, &writefds);
            wfdp = &writefds;
	}
        if (events & POLLPRI) {
            FD_SET(fd, &exceptfds);
            xfdp = &exceptfds;
	}
        if (fd > maxfd && events & (POLLIN | POLLOUT | POLLPRI))
	    maxfd = fd;
    }

    ret = select(maxfd + 1, rfdp, wfdp, xfdp, tvp);
    if (ret < 0)
        return ret;

    /* Move the returned data from the fd sets to the revents fields
     * in fdarray.  We can't detect POLLNVAL except for obviously
     * invalid fd's, and detecting POLLHUP or POLLERR would require
     * an extra read() call per fd which is too expensive. */
    result = 0;
    for (i = 0; i < numfds; i++) {
        if (fdarray[i].fd < 0) {
	    fdarray[i].revents = POLLNVAL;
            continue;
        }
        fdarray[i].revents = 0;
        if (rfdp && FD_ISSET(fdarray[i].fd, &readfds))
	    fdarray[i].revents |= POLLIN;
        if (wfdp && FD_ISSET(fdarray[i].fd, &writefds))
	    fdarray[i].revents |= POLLOUT;
        if (xfdp && FD_ISSET(fdarray[i].fd, &exceptfds))
	    fdarray[i].revents |= POLLPRI;
	if (fdarray[i].revents != 0)
	    result++;
    }

    return result;
}

#endif
