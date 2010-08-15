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
 * sms.h - definitions specific to SMS but not particular to any SMSC protocol.
 *
 * Sms features that are currently implemented separately in each protocol 
 * should be extracted and placed here.
 */

/*
 * DCS Encoding, acording to ETSI 03.38 v7.2.0
 *
 * 00abcdef
 *      bit 5 (a) indicates compressed text
 *      bit 4 (b) indicates Message Class value presence
 *      bits 3,2 (c,d) indicates Data Coding (00=7bit, 01=8bit, 10=UCS-2)
 *      bits 1,0 (e,f) indicates Message Class, if bit 4(b) is active
 *
 * 11110abc
 *      bit 2 (a) indicates 0=7bit, 1=8bit
 *      bits 1,0 (b,c) indicates Message Class
 *
 * 11abc0de
 *      bits 5,4 (a,b) indicates 00=discard message, 01=store message
 *                               10=store message and text is UCS-2
 *      bit 3 (c) indicates indication active
 *      bits 1,0 (d,e) indicates indicator (00=voice mail, 01=fax,
 *                                          10=email, 11=other)
 */


#ifndef SMS_H
#define SMS_H

#include "msg.h"

#define SMS_PARAM_UNDEFINED  MSG_PARAM_UNDEFINED

#define MC_UNDEF   SMS_PARAM_UNDEFINED
#define MC_CLASS0  0
#define MC_CLASS1  1
#define MC_CLASS2  2
#define MC_CLASS3  3

#define MWI_UNDEF      SMS_PARAM_UNDEFINED
#define MWI_VOICE_ON   0
#define MWI_FAX_ON     1
#define MWI_EMAIL_ON   2
#define MWI_OTHER_ON   3
#define MWI_VOICE_OFF  4
#define MWI_FAX_OFF    5
#define MWI_EMAIL_OFF  6
#define MWI_OTHER_OFF  7

#define DC_UNDEF  SMS_PARAM_UNDEFINED
#define DC_7BIT   0
#define DC_8BIT   1
#define DC_UCS2   2

#define COMPRESS_UNDEF  SMS_PARAM_UNDEFINED
#define COMPRESS_OFF    0
#define COMPRESS_ON     1

#define RPI_UNDEF  SMS_PARAM_UNDEFINED
#define RPI_OFF    0
#define RPI_ON     1

#define SMS_7BIT_MAX_LEN 160
#define SMS_8BIT_MAX_LEN 140
#define SMS_UCS2_MAX_LEN 70
/*
 * Maximum number of octets in an SMS message. Note that this is 8 bit
 * characters, not 7 bit characters.
 */
#define MAX_SMS_OCTETS 140

/* Encode DCS using sms fields
 * mode = 0= encode using 00xxxxxx, 1= encode using 1111xxxx mode
 */
int fields_to_dcs(Msg *msg, int mode);


/*
 * Decode DCS to sms fields
 *  returns 0 if dcs is invalid
 */
int dcs_to_fields(Msg **msg, int mode);


/*
 * Compute length of the message data in Msg after it will be converted 
 * to the proper coding. 
 * If coding is 7 bit, then sms_msgdata_len will return the number of 
 * septets this message will convert to, taking into account GSM 03.38
 * escape sequences of special chars, which would count as two septets.
 */
int sms_msgdata_len(Msg *msg);


/*
 * Swap an MO message to an MT message (hence swap receiver/sender addresses)
 * and vice versa for internal bearerbox rerouting (if needed).
 * Returns 1 if successfull, 0 otherwise.
 */
int sms_swap(Msg *msg);


/*
 *
 * Split an SMS message into smaller ones.
 *
 * The original SMS message is represented as an Msg object, and the
 * resulting list of smaller ones is represented as a List of Msg objects.
 * A plain text header and/or footer can be added to each part, and an
 * additional suffix can be added to each part except the last one.
 * Optionally, a UDH prefix can be added to each part so that phones
 * that understand this prefix can join the messages into one large one
 * again. At most `max_messages' parts will be generated; surplus text
 * from the original message will be silently ignored.
 *
 * If the original message has UDH, they will be duplicated in each part.
 * It is an error to use catenation and UDH together, or catenation and 7
 * bit mode toghether; in these cases, catenation is silently ignored.
 *
 * If `catenate' is true, `msg_sequence' is used as the sequence number for
 * the logical message. The catenation UDH contain three numbers: the
 * concatenated message reference, which is constant for all parts of
 * the logical message, the total number of parts in the logical message,
 * and the sequence number of the current part.
 *
 * Note that `msg_sequence' must have a value in the range 0..255.
 *
 * `max_octets' gives the maximum number of octets in on message, including
 * UDH, and after 7 bit characters have been packed into octets.
 */
List *sms_split(Msg *orig, Octstr *header, Octstr *footer,
                Octstr *nonlast_suffix, Octstr *split_chars, int catenate,
                unsigned long msg_sequence, int max_messages, int max_octets);

/**
 * Compare priority and time of two sms's.
 * @return -1 of a < b; 0 a = b; 1 a > b
 */
int sms_priority_compare(const void *a, const void *b);

#endif
