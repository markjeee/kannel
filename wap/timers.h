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
 * timers.h - interface to timers and timer sets.
 *
 * Timers can be set to elapse after a specified number of seconds
 * (the "interval").  They can be stopped before elapsing, and the
 * interval can be changed.
 *
 * An "output list" is defined for each timer.  When it elapses, an
 * event is generated on this list.  The event may be removed from
 * the output list if the timer is destroyed or extended before the
 * event is consumed.
 *
 * The event to use when a timer elapses is provided by the caller.
 * The timer module will "own" it, and be responsible for deallocation.
 * This will be true until the event has been consumed from the output
 * list (at which point it is owned by the consuming thread).
 * While the event is on the output list, it is in a gray area, because
 * the timer module might still take it back.  This won't be a problem
 * as long as you access the event only by consuming it.
 *
 * Timers work best if the thread that manipulates the timer (the
 * "calling thread") is the same thread that consumes the output list.
 * This way, it can be guaranteed that the calling thread will not
 * see a timer elapse after being destroyed, or while being extended,
 * because the elapse event will be deleted during such an operation.
 *
 * The timer_* functions have been renamed to gwtimer_* to avoid
 * a name conflict on Solaris systems.
 */

#ifndef TIMERS_H
#define TIMERS_H

#include "gwlib/gwlib.h"
#include "wap_events.h"

typedef struct Timer Timer;

/*
 * Start up the timer system.
 * Can be called more than once, in which case multiple shutdowns are
 * also required.
 */
void timers_init(void);

/*
 * Stop all timers and shut down the timer system.
 */
void timers_shutdown(void);

/*
 * Create a timer and tell it to use the specified output list when
 * it elapses.  Do not start it yet.  Return the new timer.
 */
Timer *gwtimer_create(List *outputlist);

/*
 * Destroy this timer and free its resources.  Stop it first, if needed.
 */
void gwtimer_destroy(Timer *timer);

/*
 * Make the timer elapse after 'interval' seconds, at which time it
 * will push event 'event' on the output list defined for its timer set.
 * - If the timer was already running, these parameters will override
 *   its old settings.
 * - If the timer has already elapsed, try to remove its event from
 *   the output list.
 * If this is not the first time the timer was started, the event
 * pointer is allowed to be NULL.  In that case the event pointer
 * from the previous call to timer_start for this timer is re-used.
 * NOTE: Each timer must have a unique event pointer.  The caller must
 * create the event, and passes control of it to the timer module with
 * this call.
 */
void gwtimer_start(Timer *timer, int interval, WAPEvent *event);

/*
 * Stop this timer.  If it has already elapsed, try to remove its
 * event from the output list.
 */
void gwtimer_stop(Timer *timer);

#endif
