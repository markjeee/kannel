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
 * bb_store.h : declarations for the bearerbox box SMS storage/retrieval module
 *
 * Author: Alexander Malysh, 2005
 */

#ifndef BB_STORE_H_
#define BB_STORE_H_

#define BB_STORE_DEFAULT_DUMP_FREQ 10

/* return number of SMS messages in current store (file) */
extern long (*store_messages)(void);

/* assign ID and save given message to store. Return -1 if save failed */
extern int (*store_save)(Msg *msg);

/*
 * Store ack/nack to the store file for a given message with a given status.
 * @return: -1 if save failed ; 0 otherwise.
 */
extern int (*store_save_ack)(Msg *msg, ack_status_t status);

/* load store from file; delete any messages that have been relayed,
 * and create a new store file from remaining. Calling this function
 * might take a while, depending on store size
 * Return -1 if something fails (bb can then PANIC normally)
 */
extern int (*store_load)(void(*receive_msg)(Msg*));

/* dump currently non-acknowledged messages into file. This is done
 * automatically now and then, but can be forced. Return -1 if file
 * problems
 */
extern int (*store_dump)(void);

/*
 * Function pointers used inside the storage subsystem to pack and
 * unpack a Msg with variable serialization functions.
 */
extern Octstr* (*store_msg_pack)(Msg *msg);
extern Msg* (*store_msg_unpack)(Octstr *os);

/* initialize system. Return -1 if fname is bad (too long). */
int store_init(const Octstr *type, const Octstr *fname, long dump_freq,
               void *pack_func, void *unpack_func);

/* init shutdown (system dies when all acks have been processed) */
extern void (*store_shutdown)(void);

/* return all containing messages in the current store */
extern Octstr* (*store_status)(int status_type);

/**
 * Init functions for different store types.
 */
int store_spool_init(const Octstr *fname);
int store_file_init(const Octstr *fname, long dump_freq);


#endif /*BB_STORE_H_*/

