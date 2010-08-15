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
 * fdset.h - module for managing a large collection of file descriptors
 */

typedef struct FDSet FDSet;

/*
 * A function of this type will be called to indicate that a file descriptor
 * has shown activity.  The revents parameter is in the same format as
 * returned by poll().  The data pointer was supplied by the caller who
 * registered the fd with us in the first place.
 * NOTE: Beware of concurrency issues.  The callback function will run
 * in the fdset's private thread, not in the caller's thread.
 * This also means that if the callback does a lot of work it will slow
 * down the polling process.  This may be good or bad.
 */
typedef void fdset_callback_t(int fd, int revents, void *data);

/*
 * Create a new, empty file descriptor set and start its thread.
 * @timeout - idle timeout for any filedescriptor in this fdset after which
 *            callback function will be called with POLLERR as event.
 */
#define fdset_create() fdset_create_real(-1)
FDSet *fdset_create_real(long timeout);

/*
 * Destroy a file descriptor set.  Will emit a warning if any file
 * descriptors are still registered with it.
 */
void fdset_destroy(FDSet *set);

/* 
 * Register a file descriptor with this set, and listen for the specified
 * events (see fdset_listen() for details on that).  Record the callback
 * function which will be used to notify the caller about events.
 */
void fdset_register(FDSet *set, int fd, int events,
                    fdset_callback_t callback, void *data);

/*
 * Change the set of events to listen for for this file descriptor.
 * Events is in the same format as the events field in poll() -- in
 * practice, use POLLIN for "input available", POLLOUT for "ready to
 * accept more output", and POLLIN|POLLOUT for both.
 *
 * The mask field indicates which event flags can be affected.  For
 * example, if events is POLLIN and mask is POLLIN, then the POLLOUT
 * setting will not be changed by this.  If mask were POLLIN|POLLOUT,
 * then the POLLOUT setting would be turned off.
 *
 * The fd must first have been registered.  Locks are used to
 * guarantee that the callback function will not be called for
 * the old events after this function returns.
 */
void fdset_listen(FDSet *set, int fd, int mask, int events);

/*
 * Forget about this fd.  Locks are used to guarantee that the callback
 * function will not be called for this fd after this function returns.
 */
void fdset_unregister(FDSet *set, int fd);

/**
 * Set timeout in seconds for this FDSet.
 */
void fdset_set_timeout(FDSet *set, long timeout);
