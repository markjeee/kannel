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
 * smsc.h - interface to SMS center subsystem
 *
 * Lars Wirzenius for WapIT Ltd.
 *
 * New API by Kalle Marjola 1999
 */

#ifndef SMSC_H
#define SMSC_H


#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "gwlib/gwlib.h"
#include "msg.h"

/*
 * A data structure representing an SMS center. This data structure
 * is opaque: users MUST NOT use the fields directly, only the
 * smsc_* functions may do so.
 */
typedef struct SMSCenter SMSCenter;


/* Open the connection to an SMS center. 'grp' is a configgroup which
   determines the sms center. See details from sample configuration file
   'kannel.conf'

   The operation returns NULL for error and the pointer
   to the new SMSCenter structure for OK.
   */
SMSCenter *smsc_open(CfgGroup *grp);

/*
 * reopen once opened SMS Center connection. Close old connection if
 * exists
 *
 * return 0 on success
 * return -1 if failed
 * return -2 if failed and no use to repeat the progress (i.e. currently
 *   reopen not implemented)
 */
int smsc_reopen(SMSCenter *smsc);


/* Return the `name' of an SMC center. Name is defined here as a string that
   a human understands that uniquely identifies the SMSC. This operation
   cannot fail. */
char *smsc_name(SMSCenter *smsc);


/* Close the connection to an SMS center. Return -1 for error
   (the connection will be closed anyway, but there was some error
   while doing so, so it wasn't closed cleanly), or 0 for OK.
   Return 0 if the smsc is NULL or smsc is already closed.
 */
int smsc_close(SMSCenter *smsc);



#endif
