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
 * gw/dlr.h
 *
 * Implementation of handling delivery reports (DLRs)
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <stolj@wapme.de>, 22.03.2002
 */

#ifndef	DLR_H
#define	DLR_H 1

#define	DLR_UNDEFINED       -1
#define	DLR_NOTHING         0x00
#define	DLR_SUCCESS         0x01
#define	DLR_FAIL            0x02
#define	DLR_BUFFERED        0x04
#define	DLR_SMSC_SUCCESS    0x08
#define	DLR_SMSC_FAIL       0x10
#define	DLR_INTERMEDIATE    0x20

#define DLR_IS_DEFINED(dlr)          (dlr != DLR_UNDEFINED)
#define DLR_IS_ENABLED(dlr)          (DLR_IS_DEFINED(dlr) && (dlr & (DLR_SUCCESS | DLR_FAIL | DLR_BUFFERED | DLR_SMSC_SUCCESS | DLR_SMSC_FAIL)))
#define DLR_IS_ENABLED_DEVICE(dlr)   (DLR_IS_DEFINED(dlr) && (dlr & (DLR_SUCCESS | DLR_FAIL | DLR_BUFFERED)))
#define DLR_IS_ENABLED_SMSC(dlr)     (DLR_IS_DEFINED(dlr) && (dlr & (DLR_SMSC_SUCCESS | DLR_SMSC_FAIL)))
#define DLR_IS_NOT_FINAL(dlr)        (DLR_IS_DEFINED(dlr) && (dlr & (DLR_BUFFERED | DLR_SMSC_SUCCESS)))
#define DLR_IS_SUCCESS_OR_FAIL(dlr)  (DLR_IS_DEFINED(dlr) && (dlr & (DLR_SUCCESS | DLR_FAIL)))
#define DLR_IS_SUCCESS(dlr)          (DLR_IS_DEFINED(dlr) && (dlr & DLR_SUCCESS))
#define DLR_IS_FAIL(dlr)             (DLR_IS_DEFINED(dlr) && (dlr & DLR_FAIL))
#define DLR_IS_BUFFERED(dlr)         (DLR_IS_DEFINED(dlr) && (dlr & DLR_BUFFERED))
#define DLR_IS_SMSC_SUCCESS(dlr)     (DLR_IS_DEFINED(dlr) && (dlr & DLR_SMSC_SUCCESS))
#define DLR_IS_SMSC_FAIL(dlr)        (DLR_IS_DEFINED(dlr) && (dlr & DLR_SMSC_FAIL))
#define DLR_IS_INTERMEDIATE(dlr)     (DLR_IS_DEFINED(dlr) && (dlr & DLR_INTERMEDIATE))

/* DLR initialization routine (abstracted) */
void dlr_init(Cfg *cfg);

/* DLR shutdown routine (abstracted) */
void dlr_shutdown(void);

/* 
 * Add a new entry to the list
 */
void dlr_add(const Octstr *smsc, const Octstr *ts, Msg *msg);

/* 
 * Find an entry in the list. If there is one a message is returned and 
 * the entry is removed from the list otherwhise the message returned is NULL 
 */
Msg* dlr_find(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int type, int use_dst);

/* return the number of DLR messages in the current waiting queue */
long dlr_messages(void);

/* 
 * Flush all DLR messages in the current waiting queue.
 * Beware to take bearerbox to suspended state before doing this.
 */
void dlr_flush(void);

/*
 * Return type of dlr storage
 */
const char* dlr_type(void);

/*
 * Helper function, create DLR from given message
 */
Msg* create_dlr_from_msg(const Octstr *smsc, const Msg *msg, const Octstr *reply, long stat);

/*
 * Yet not used functions.
 */
void dlr_save(const char *filename);
void dlr_load(const char *filename);

#endif /* DLR_H */

