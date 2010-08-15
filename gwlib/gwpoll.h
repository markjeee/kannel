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

/* gwpoll.h - define poll() for systems that don't have it */

#ifndef GWPOLL_H
#define GWPOLL_H

/* If the system supplies it, we're done.  Assume that if the header file
 * exists, it will define poll() and struct pollfd for us. */
#ifdef HAVE_SYS_POLL_H

#include <sys/poll.h>

/* Most systems accept any negative timeout value as meaning infinite
 * timeout.  FreeBSD explicitly wants INFTIM, however.  Other systems
 * don't define INFTIM.  So we use it if it's defined, and -1 otherwise.
 */

#ifdef INFTIM
#define POLL_NOTIMEOUT INFTIM
#else
#define POLL_NOTIMEOUT (-1)
#endif

#else

/* Define struct pollfd and the event bits, and declare a function poll()
 * that uses them and is a wrapper around select(). */

struct pollfd {
    int fd;	   /* file descriptor */
    short events;  /* requested events */
    short revents; /* returned events */
};

/* Bits for events and revents */
#define POLLIN   1    /* Reading will not block */
#define POLLPRI  2    /* Urgent data available for reading */
#define POLLOUT  4    /* Writing will not block */

/* Bits only used in revents */
#define POLLERR  8    /* Error condition */
#define POLLHUP  16   /* Hung up: fd was closed by other side */
#define POLLNVAL 32   /* Invalid: fd not open or not valid */

#define POLL_NOTIMEOUT (-1)

/* Implement the function as gw_poll, in case the system does have a poll()
 * function in its libraries but just fails to define it in sys/poll.h. */
#define poll(fdarray, numfds, timeout) gw_poll(fdarray, numfds, timeout)
int gw_poll(struct pollfd *fdarray, unsigned int numfds, int timeout);

#endif  /* !HAVE_SYS_POLL_H */

#endif
