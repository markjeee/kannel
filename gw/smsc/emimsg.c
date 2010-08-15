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
 * emimsg.c
 *
 * Functions for working with EMI messages
 * Uoti Urpala 2001 */


#include "emimsg.h"

/* Return an error string corresponding to the number. */
static char *emi_strerror(int errnum)
{
    switch (errnum) {
    case  1: return "Checksum error";
    case  2: return "Syntax error";
    case  3: return "Operation not supported by system";
    case  4: return "Operation not allowed";
    case  5: return "Call barring active";
    case  6: return "AdC invalid";
    case  7: return "Authentication failure";
    case  8: return "Legitimisation code for all calls, failure";
    case  9: return "GA not valid";
    case 10: return "Repetition not allowed";
    case 11: return "Legitimisation code for repetition, failure";
    case 12: return "Priority call not allowed";
    case 13: return "Legitimisation code for priority call, failure";
    case 14: return "Urgent message not allowed";
    case 15: return "Legitimisation code for urgent message, failure";
    case 16: return "Reverse charging not allowed";
    case 17: return "Legitimisation code for reverse charging, failure";
    case 18: return "Deferred delivery not allowed";
    case 19: return "New AC not valid";
    case 20: return "New legitimisation code not valid";
    case 21: return "Standard text not valid";
    case 22: return "Time period not valid";
    case 23: return "Message type not supported by system";
    case 24: return "Message too long";
    case 25: return "Requested standard text not valid";
    case 26: return "Message type not valid for the pager type";
    case 27: return "Message not found in smsc";
    case 30: return "Subscriber hang-up";
    case 31: return "Fax group not supported";
    case 32: return "Fax message type not supported";
    case 33: return "Address already in list (60 series)";
    case 34: return "Address not in list (60 series)";
    case 35: return "List full, cannot add address to list (60 series)";
    case 36: return "RPID already in use";
    case 37: return "Delivery in progress";
    case 38: return "Message forwarded";
    default: return "!UNRECOGNIZED ERROR CODE!";
    }
}


static int field_count_op(int ot, Octstr *whoami)
{
    switch (ot) {
    case 01:
	return SZ01;
    case 31:
	return 2;
    case 51:
    case 52:
    case 53:
	return SZ50;
    case 60:
	return SZ60;
    default:
	error(0, "EMI2[%s]: Unsupported EMI operation request type %d", 
	      octstr_get_cstr(whoami), ot);
	return -1;
    }
}


static int field_count_reply(int ot, int posit, Octstr *whoami)
{
    switch(ot) {
    case 01:
	return posit ? 2 : 3;
    case 31:
	return posit ? 2 : 3;
    case 51:
    case 52:
    case 53:
	return 3;
    case 60:
	return posit ? 2 : 3;
    default:
	error(0, "EMI2[%s]: Unsupported EMI operation reply type %d", 
	      octstr_get_cstr(whoami), ot);
	return -1;
    }
}


static struct emimsg *emimsg_create_withlen(int len)
{
    struct emimsg *ret;

    ret = gw_malloc(sizeof(struct emimsg));
    ret->fields = gw_malloc(len * sizeof(Octstr *));
    ret->num_fields = len;
    while (--len >= 0)
	ret->fields[len] = NULL;
    return ret;
}


struct emimsg *emimsg_create_op(int ot, int trn, Octstr *whoami)
{
    int len;
    struct emimsg *ret;

    len = field_count_op(ot, whoami);
    if (len < 0)
	return NULL;
    ret = emimsg_create_withlen(len);
    ret->ot = ot;
    ret->or = 'O';
    ret->trn = trn;
    return ret;
}


static struct emimsg *emimsg_create_reply_s(int ot, int trn, int positive,
		Octstr *whoami)
{
    int len;
    struct emimsg *ret;

    len = field_count_reply(ot, positive, whoami);
    if (len < 0)
	return NULL;
    ret = emimsg_create_withlen(len);
    ret->ot = ot;
    ret->or = 'R';
    ret->trn = trn;
    return ret;
}


struct emimsg *emimsg_create_reply(int ot, int trn, int positive, 
		Octstr *whoami)
{
    struct emimsg *ret;

    ret = emimsg_create_reply_s(ot, trn, positive, whoami);
    if (ret) {
	if (positive)
	    ret->fields[0] = octstr_create("A");
	else
	    ret->fields[0] = octstr_create("N");
    }
    return ret;
}


void emimsg_destroy(struct emimsg *emimsg)
{
    int len;

    len = emimsg->num_fields;
    while (--len >= 0) {
        octstr_destroy(emimsg->fields[len]);  /* octstr_destroy(NULL) is ok */
        emimsg->fields[len] = NULL;
    }
    gw_free(emimsg->fields);
    gw_free(emimsg);
}


struct emimsg *emimsg_duplicate(struct emimsg *emimsg)
{
    int len;
    struct emimsg *ret;

    len = emimsg->num_fields;
    if (len < 0)
        return NULL;
    ret = gw_malloc(sizeof(struct emimsg));
    ret->fields = gw_malloc(len * sizeof(Octstr *));
    ret->num_fields = len;
    while (--len >= 0)
        ret->fields[len] = octstr_duplicate(emimsg->fields[len]);
    ret->ot = emimsg->ot;
    ret->or = emimsg->or;
    ret->trn = emimsg->trn;

    return ret;
}


/* The argument can be either the whole message (with the stx/etx start/end
   characters), or miss the last 3 characters (checksum digits and etx) */
static int calculate_checksum(Octstr *message)
{
    int end, i, checksum;

    end = octstr_len(message);
    if (octstr_get_char(message, end - 1) == 3)  /* etx, whole message */
	end -= 3;
    checksum = 0;
    for (i = 1; i < end; i++)
	checksum += octstr_get_char(message, i);
    return checksum & 0xff;
}


static Octstr *emimsg_tostring(struct emimsg *emimsg)
{
    int i, checksum;
    Octstr *result, *data;
    char *hexits = "0123456789ABCDEF";

    data = octstr_create("");
    for (i = 0; i < emimsg->num_fields; i++) {
	if (emimsg->fields[i])
	    octstr_append(data, emimsg->fields[i]);
	octstr_append_char(data, '/');
    }
    result = octstr_format("\02%02d/%05d/%c/%02d/%S", emimsg->trn,
		octstr_len(data) + 16, emimsg->or, emimsg->ot, data);
    checksum = calculate_checksum(result);
    octstr_append_char(result, hexits[checksum >> 4 & 15]);
    octstr_append_char(result, hexits[checksum & 15]);
    octstr_append_char(result, 3);
    octstr_destroy(data);
    return result;
}


/* Doesn't check that the string is strictly according to format */
struct emimsg *get_fields(Octstr *message, Octstr *whoami)
{
    long trn, len, ot, checksum; /* because of Octstr_parse_long... */
    char or, posit;
    long fieldno, pos, pos2;
    struct emimsg *result = NULL;

    debug("smsc.emi2", 0, "EMI2[%s]: emi2 parsing packet: <%s>",
	  octstr_get_cstr(whoami), octstr_get_cstr(message));
    if (octstr_get_char(message, 0) != 2 ||
	octstr_get_char(message, octstr_len(message) - 1) != 3)
	goto error;
    if (octstr_parse_long(&trn, message, 1, 10) != 3)
	goto error;
    if (octstr_parse_long(&len, message, 4, 10) != 9)
	goto error;
    if (octstr_len(message) != len + 2)     /* +2 for start/end markers */
	goto error;
    if ( (or = octstr_get_char(message, 10)) != 'O' && or != 'R')
	goto error;
    if (octstr_parse_long(&ot, message, 12, 10) != 14)
	goto error;
    if (or == 'O')
	result = emimsg_create_op(ot, trn, whoami);
    else {
	posit = octstr_get_char(message, 15);
	if (posit == 'A')
	    result = emimsg_create_reply_s(ot, trn, 1, whoami);
	else if (posit == 'N')
	    result = emimsg_create_reply_s(ot, trn, 0, whoami);
	else
	    goto error;
    }
    if (result == NULL)
	goto error;
    pos2 = 14;
    for (fieldno = 0; fieldno < result->num_fields; fieldno++) {
	pos = pos2 + 1;
	if ( (pos2 = octstr_search_char(message, '/', pos)) == -1)
	    goto error;
	if (pos2 > pos)
	    result->fields[fieldno] = octstr_copy(message, pos, pos2 - pos);
    }
    if (octstr_search_char(message, '/', pos2 + 1) != -1) {
	int extrafields = 0;

	pos = pos2;
	while ((pos = octstr_search_char(message, '/', pos + 1)) != -1) {
	    extrafields++;
	    pos2 = pos;
	}
	/* The extra fields are ignored */
	warning(0, "get_fields: EMI message of type %d/%c has %d more fields "
		"than expected.", result->ot, result->or, extrafields);
    }
    if (octstr_parse_long(&checksum, message, pos2 + 1, 16) !=
	octstr_len(message) - 1 || checksum != calculate_checksum(message))
	goto error;
    if (result->or == 'R' && octstr_get_char(result->fields[0], 0) == 'N') {
	long errcode;
	if (!result->fields[1] ||
	    octstr_parse_long(&errcode, result->fields[1], 0, 10) != 2)
	    goto error;
	error(0, "EMI2[%s]: Got negative ack. op:%d, trn:%d, error:%ld (%s), message:%s",
	      octstr_get_cstr(whoami),
	      result->ot, result->trn, errcode, emi_strerror(errcode),
	      result->fields[2] ? octstr_get_cstr(result->fields[2]) : "");
    }
    return result;
error:
    error(0, "EMI2[%s]: Invalid EMI packet: %s", octstr_get_cstr(whoami),
	  octstr_get_cstr(message));
    if (result)
	emimsg_destroy(result);
    return NULL;
}


int emimsg_send(Connection *conn, struct emimsg *emimsg, Octstr *whoami)
{
    Octstr *string;

    string = emimsg_tostring(emimsg);
    if (!string) {
	error(0, "EMI2[%s]: emimsg_send: conversion to string failed",
	      octstr_get_cstr(whoami));
	return -1;
    }
    if (emimsg->ot == 60)
	debug("smsc.emi2", 0, "EMI2[%s]: Sending operation type 60, message with "
	      "password not shown in log file.", octstr_get_cstr(whoami));
    else
	debug("smsc.emi2", 0, "EMI2[%s]: emi2 sending packet: <%s>",
	      octstr_get_cstr(whoami), octstr_get_cstr(string));
    if (conn_write(conn, string) == -1) {
	octstr_destroy(string);
	error(0, "EMI2[%s]: emimsg_send: write failed",
	      octstr_get_cstr(whoami));
	return -1;
    }
    octstr_destroy(string);
    return 1;
}
