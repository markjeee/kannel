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
 * msg.h - declarations for message manipulation
 * 
 * This file declares the Msg data type and the functions to manipulate it.
 * 
 * Lars Wirzenius
 */


#ifndef MSG_H
#define MSG_H

#include "gwlib/gwlib.h"

#define MSG_PARAM_UNDEFINED -1

enum msg_type {
	#define MSG(type, stmt) type,
	#include "msg-decl.h"
	msg_type_count
};

typedef struct {
	enum msg_type type;

	#define INTEGER(name) long name;
	#define OCTSTR(name) Octstr *name;
	#define UUID(name) uuid_t name;
	#define VOID(name) void *name;
	#define MSG(type, stmt) struct type stmt type;
	#include "msg-decl.h"
} Msg;

struct split_parts {
    /* original large message */
    Msg *orig;
    /* how many parts still not sent */
    Counter *parts_left;
    /* status of splitted message parts */
    long status;
    /* pointer to SMSCConn */
    void *smsc_conn;
};

/* enums for Msg fields */

/* sms message type */

enum {
    mo = 0,
    mt_reply = 1,
    mt_push = 2,
    report_mo = 3,
    report_mt = 4
};

/* admin commands */
enum {
    cmd_shutdown = 0,
    cmd_suspend = 1,
    cmd_resume = 2,
    cmd_identify = 3,
    cmd_restart = 4
};

/* ack message status */
typedef enum {
    ack_success = 0,
    ack_failed = 1,     /* do not try again (e.g. no route) */
    ack_failed_tmp = 2, /* temporary failed, try again (e.g. queue full) */
    ack_buffered = 3
} ack_status_t;

/*
 * Create a new, empty Msg object. Panics if fails.
 */
Msg *msg_create_real(enum msg_type type, const char *file, long line,
                     const char *func);
#define msg_create(type) \
    gw_claim_area(msg_create_real((type), __FILE__, __LINE__, __func__))

/*
 * Create a new Msg object that is a copy of an existing one.
 * Panics if fails.
 */
Msg *msg_duplicate(Msg *msg);


/*
 * Return type of the message
 */
enum msg_type msg_type(Msg *msg);


/*
 * Destroy an Msg object. All fields are also destroyed.
 */
void msg_destroy(Msg *msg);


/*
 * Destroy an Msg object. Wrapper around msg_destroy to make it suitable for
 * gwlist_destroy.
 */
void msg_destroy_item(void *msg);


/*
 * For debugging: Output with `debug' (in gwlib/log.h) the contents of
 * an Msg object.
 */
void msg_dump(Msg *msg, int level);


/*
 * Pack an Msg into an Octstr. Panics if fails.
  */
Octstr *msg_pack(Msg *msg);


/*
 * Unpack an Msg from an Octstr. Return NULL for failure, otherwise a pointer
 * to the Msg.
 */
Msg *msg_unpack_real(Octstr *os, const char *file, long line, const char *func);
#define msg_unpack(os) \
    gw_claim_area(msg_unpack_real((os), __FILE__, __LINE__, __func__))
Msg *msg_unpack_wrapper(Octstr *os);

#endif
