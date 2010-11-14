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

/*
 * shared.h - utility functions shared by Kannel boxes
 *
 * The functions declared here are not part of any box in particular, but
 * are quite specific to Kannel, so they are not suitable for gwlib, either.
 *
 * Lars Wirzenius
 */

#ifndef SHARED_H
#define SHARED_H


#include "gwlib/gwlib.h"
#include "msg.h"

#define INFINITE_TIME -1

/*
 * Program status. Set this to shutting_down to make read_from_bearerbox
 * return even if the bearerbox hasn't closed the connection yet.
 */
extern volatile enum program_status {
    starting_up,
    running,
    shutting_down
} program_status;


/*
 * Open a connection to the bearerbox.
 */
Connection *connect_to_bearerbox_real(Octstr *host, int port, int ssl, Octstr *our_host);
void connect_to_bearerbox(Octstr *host, int port, int ssl, Octstr *our_host);


/*
 * Close connection to the bearerbox.
 */
void close_connection_to_bearerbox_real(Connection *conn);
void close_connection_to_bearerbox(void);


/*
 * Receive and store Msg from bearerbox into msg. Unblock the call when
 *  the given timeout for conn_wait() is reached. Use a negative value,
 * ie. -1 for an infinite blocking, hence no timeout applies.
 * Return 0 if Msg received ; -1 if error occurs; 1 if timedout.
 */
int read_from_bearerbox_real(Connection *conn, Msg **msg, double seconds);
int read_from_bearerbox(Msg **msg, double seconds);


/*
 * Send an Msg to the bearerbox, and destroy the Msg.
 */
void write_to_bearerbox_real(Connection *conn, Msg *pmsg);
void write_to_bearerbox(Msg *msg);


/*
 * Delivers a SMS to the bearerbox and returns an error code: 0 if
 * successfull. -1 if transfer failed.
 *
 * Note: Message is only destroyed if successfully delivered!
 */
int deliver_to_bearerbox_real(Connection *conn, Msg *msg);
int deliver_to_bearerbox(Msg *msg);

     
/*
 * Validates an OSI date.
 */
Octstr *parse_date(Octstr *date);


#endif






