/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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

/* Driver for CIMD 2 SMS centres.
 * Copyright 2000  WapIT Oy Ltd.
 * Author: Richard Braakman
 *
 * Upgrade to SMSCConn API: 2002 Kalle Marjola / 2003 Angel Fradejas
 */

/* TODO: Check checksums on incoming packets */
/* TODO: Leading or trailing spaces are not allowed on parameters
 * "user identity" and "password".  Check this. */
/* TODO: Try to use the "More messages to send" flag */

/* This code is based on the CIMD 2 spec, version 2-0 en.
 * All USSD-specific parts have been left out, since we only want to
 * communicate with SMSC's.
 *
 * I found one contradiction in the spec:
 *
 * - The definition of Integer parameters specifies decimal digits only,
 *   but at least one Integer parameter (Validity Period Relative) can
 *   be negative.  I assume that this means a leading - is valid.
 */

#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <unistd.h>

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"

#include "shared.h"
#include "sms.h"
#include "dlr.h"


typedef struct privdata {
    Octstr  *username;
    Octstr  *password;
    Octstr  *host;
    long    port;
    long    our_port;
    long    keepalive;
    Octstr  *my_number;
    int no_dlr;

    int     socket;
    int     send_seq;
    int     receive_seq;

    Octstr  *inbuffer;
    List    *received;

    time_t  next_ping;

    List *outgoing_queue;
    SMSCConn *conn;
    int io_thread;
    int quitting;
    List *stopped; /* list-trick for suspend/isolate */

} PrivData;



/* Microseconds before giving up on a request */
#define RESPONSE_TIMEOUT (60 * 1000000)

/* Textual names for the operation codes defined by the CIMD 2 spec. */
/* If you make changes here, also change the operation table. */
enum {
    /* Requests from client */
    LOGIN = 1,
    LOGOUT = 2,
    SUBMIT_MESSAGE = 3,
    ENQUIRE_MESSAGE_STATUS = 4,
    DELIVERY_REQUEST = 5,
    CANCEL_MESSAGE = 6,
    SET_REQ = 8,
    GET_REQ = 9,

    /* Requests from server */
    DELIVER_MESSAGE = 20,
    DELIVER_STATUS_REPORT = 23,

    /* Requests from either */
    ALIVE = 40,

    /* Not a request; add to any request to make it a response */
    RESPONSE = 50,

    /* Responses not related to requests */
    GENERAL_ERROR_RESPONSE = 98,
    NACK = 99
};

/* Textual names for the parameters defined by the CIMD 2 spec. */
/* If you make changes here, also change the parameter table. */
enum {
    P_USER_IDENTITY = 10,
    P_PASSWORD = 11,
    P_DESTINATION_ADDRESS = 21,
    P_ORIGINATING_ADDRESS = 23,
    P_ORIGINATING_IMSI = 26,
    P_ALPHANUMERIC_ORIGINATING_ADDRESS = 27,
    P_ORIGINATED_VISITED_MSC = 28,
    P_DATA_CODING_SCHEME = 30,
    P_USER_DATA_HEADER = 32,
    P_USER_DATA = 33,
    P_USER_DATA_BINARY = 34,
    P_MORE_MESSAGES_TO_SEND = 44,
    P_VALIDITY_PERIOD_RELATIVE = 50,
    P_VALIDITY_PERIOD_ABSOLUTE = 51,
    P_PROTOCOL_IDENTIFIER = 52,
    P_FIRST_DELIVERY_TIME_RELATIVE = 53,
    P_FIRST_DELIVERY_TIME_ABSOLUTE = 54,
    P_REPLY_PATH = 55,
    P_STATUS_REPORT_REQUEST = 56,
    P_CANCEL_ENABLED = 58,
    P_CANCEL_MODE = 59,
    P_MC_TIMESTAMP = 60,
    P_STATUS_CODE = 61,
    P_STATUS_ERROR_CODE = 62,
    P_DISCHARGE_TIME = 63,
    P_TARIFF_CLASS = 64,
    P_SERVICE_DESCRIPTION = 65,
    P_MESSAGE_COUNT = 66,
    P_PRIORITY = 67,
    P_DELIVERY_REQUEST_MODE = 68,
    P_SERVICE_CENTER_ADDRESS = 69,
    P_GET_PARAMETER = 500,
    P_MC_TIME = 501,
    P_ERROR_CODE = 900,
    P_ERROR_TEXT = 901
};

/***************************************************************************/
/* Table of properties of the parameters defined by CIMD 2, and some       */
/* functions to look up fields.                                            */
/***************************************************************************/

/* Parameter types, internal.  CIMD 2 spec considers P_TIME to be "Integer"
 * and P_SMS to be "User Data". */
enum { P_INT, P_STRING, P_ADDRESS, P_TIME, P_HEX, P_SMS };

/* Information about the parameters defined by the CIMD 2 spec.
 * Used for warning about invalid incoming messages, and for validating
 * outgoing messages. */
static const struct
{
    char *name;
    int number;
    int maxlen;
    int type;  /* P_ values */
    int minval, maxval;  /* For P_INT */
}
parameters[] = {
    { "user identity", P_USER_IDENTITY, 32, P_STRING },
    { "password", P_PASSWORD, 32, P_STRING },
    { "destination address", P_DESTINATION_ADDRESS, 20, P_ADDRESS },
    { "originating address", P_ORIGINATING_ADDRESS, 20, P_ADDRESS },
    /* IMSI is International Mobile Subscriber Identity number */
    { "originating IMSI", P_ORIGINATING_IMSI, 20, P_ADDRESS },
    { "alphanumeric originating address", P_ALPHANUMERIC_ORIGINATING_ADDRESS, 11, P_STRING },
    { "originated visited MSC", P_ORIGINATED_VISITED_MSC, 20, P_ADDRESS },
    { "data coding scheme", P_DATA_CODING_SCHEME, 3, P_INT, 0, 255 },
    { "user data header", P_USER_DATA_HEADER, 280, P_HEX },
    { "user data", P_USER_DATA, 480, P_SMS },
    { "user data binary", P_USER_DATA_BINARY, 280, P_HEX },
    { "more messages to send", P_MORE_MESSAGES_TO_SEND, 1, P_INT, 0, 1 },
    { "validity period relative", P_VALIDITY_PERIOD_RELATIVE, 3, P_INT, -1, 255 },
    { "validity period absolute", P_VALIDITY_PERIOD_ABSOLUTE, 12, P_TIME },
    { "protocol identifier", P_PROTOCOL_IDENTIFIER, 3, P_INT, 0, 255 },
    { "first delivery time relative", P_FIRST_DELIVERY_TIME_RELATIVE, 3, P_INT, -1, 255 },
    { "first delivery time absolute", P_FIRST_DELIVERY_TIME_ABSOLUTE, 12, P_TIME },
    { "reply path", P_REPLY_PATH, 1, P_INT, 0, 1 },
    { "status report request", P_STATUS_REPORT_REQUEST, 2, P_INT, 0, 63 },
    { "cancel enabled", P_CANCEL_ENABLED, 1, P_INT, 0, 1 },
    { "cancel mode", P_CANCEL_MODE, 1, P_INT, 0, 2 },
    { "service centre timestamp", P_MC_TIMESTAMP, 12, P_TIME },
    { "status code", P_STATUS_CODE, 2, P_INT, 0, 9 },
    { "status error code", P_STATUS_ERROR_CODE, 3, P_INT, 0, 999 },
    { "discharge time", P_DISCHARGE_TIME, 12, P_TIME },
    { "tariff class", P_TARIFF_CLASS, 2, P_INT, 0, 99 },
    { "service description", P_SERVICE_DESCRIPTION, 2, P_INT, 0, 9 },
    { "message count", P_MESSAGE_COUNT, 3, P_INT, 0, 999 },
    { "priority", P_PRIORITY, 1, P_INT, 1, 9 },
    { "delivery request mode", P_DELIVERY_REQUEST_MODE, 1, P_INT, 0, 2 },
    { "service center address", P_SERVICE_CENTER_ADDRESS, 20, P_ADDRESS },
    { "get parameter", P_GET_PARAMETER, 3, P_INT, 501, 999 },
    { "MC time", P_MC_TIME, 12, P_TIME },
    { "error code", P_ERROR_CODE, 3, P_INT, 0, 999 },
    { "error text", P_ERROR_TEXT, 64, P_STRING },
    { NULL }
};

/* Return the index in the parameters array for this parameter id.
 * Return -1 if it is not found. */
static int parm_index(int parmno)
{
    int i;

    for (i = 0; parameters[i].name != NULL; i++) {
        if (parameters[i].number == parmno)
            return i;
    }

    return -1;
}

#ifndef NO_GWASSERT
/* Return the type of this parameter id.  Return -1 if the id is unknown. */
static int parm_type(int parmno)
{
    int i = parm_index(parmno);

    if (i < 0)
        return -1;

    return parameters[i].type;
}
#endif

/* Return the max length for this parameter id.
 * Return -1 if the id is unknown. */
static int parm_maxlen(int parmno)
{
    int i = parm_index(parmno);

    if (i < 0)
        return -1;

    return parameters[i].maxlen;
}

static const char *parm_name(int parmno)
{
    int i = parm_index(parmno);

    if (i < 0)
        return NULL;

    return parameters[i].name;
}

#ifndef NO_GWASSERT
/* Return 1 if the value for this (Integer) parameter is in range.
 * Return 0 otherwise.  Return -1 if the parameter was not found.  */
static int parm_in_range(int parmno, long value)
{
    int i;

    i = parm_index(parmno);

    if (i < 0)
        return -1;

    return (value >= parameters[i].minval && value <= parameters[i].maxval);
}
#endif

/* Helper function to check P_ADDRESS type */
static int isphonedigit(int c)
{
    return isdigit(c) || c == '+' || c == '-';
}

static int parm_valid_address(Octstr *value)
{
    return octstr_check_range(value, 0, octstr_len(value), isphonedigit);
}

/***************************************************************************/
/* Some functions to look up information about operation codes             */
/***************************************************************************/

static int operation_find(int operation);
static Octstr *operation_name(int operation);
static int operation_can_send(int operation);
static int operation_can_receive(int operation);

static const struct
{
    char *name;
    int code;
    int can_send;
    int can_receive;
}
operations[] = {
    { "Login", LOGIN, 1, 0 },
    { "Logout", LOGOUT, 1, 0 },
    { "Submit message", SUBMIT_MESSAGE, 1, 0 },
    { "Enquire message status", ENQUIRE_MESSAGE_STATUS, 1, 0 },
    { "Delivery request", DELIVERY_REQUEST, 1, 0 },
    { "Cancel message", CANCEL_MESSAGE, 1, 0 },
    { "Set parameter", SET_REQ, 1, 0 },
    { "Get parameter", GET_REQ, 1, 0 },

    { "Deliver message", DELIVER_MESSAGE, 0, 1 },
    { "Deliver status report", DELIVER_STATUS_REPORT, 0, 1 },

    { "Alive", ALIVE, 1, 1 },

    { "NACK", NACK, 1, 1 },
    { "General error response", GENERAL_ERROR_RESPONSE, 0, 1 },

    { NULL, 0, 0, 0 }
};

static int operation_find(int operation)
{
    int i;

    for (i = 0; operations[i].name != NULL; i++) {
        if (operations[i].code == operation)
            return i;
    }

    return -1;
}

/* Return a human-readable representation of this operation code */
static Octstr *operation_name(int operation)
{
    int i;

    i = operation_find(operation);
    if (i >= 0)
        return octstr_create(operations[i].name);

    if (operation >= RESPONSE) {
        i = operation_find(operation - RESPONSE);
        if (i >= 0) {
            Octstr *name = octstr_create(operations[i].name);
            octstr_append_cstr(name, " response");
            return name;
        }
    }

    /* Put the operation number here when we have octstr_format */
    return octstr_create("(unknown)");
}

/* Return true if a CIMD2 client may send this operation */
static int operation_can_send(int operation)
{
    int i = operation_find(operation);

    if (i >= 0)
        return operations[i].can_send;

    /* If we can receive the request, then we can send the response. */
    if (operation >= RESPONSE)
        return operation_can_receive(operation - RESPONSE);

    return 0;
}


/* Return true if a CIMD2 server may send this operation */
static int operation_can_receive(int operation)
{
    int i = operation_find(operation);

    if (i >= 0)
        return operations[i].can_receive;

    /* If we can send the request, then we can receive the response. */
    if (operation >= RESPONSE)
        return operation_can_send(operation - RESPONSE);

    return 0;
}

/***************************************************************************/
/* Packet encoding/decoding functions.  They handle packets at the octet   */
/* level, and know nothing of the network.                                 */
/***************************************************************************/

struct packet
{
    /* operation and seq are -1 if their value could not be parsed */
    int operation;
    int seq;   /* Sequence number */
    Octstr *data;   /* Encoded packet */
    /* CIMD 2 packet structure is so simple that packet information is
     * stored as a valid encoded packet, and decoded as necessary.
     * Exceptions: operation code and sequence number are also stored
     * as ints for speed, and the checksum is not added until the packet
     * is about to be sent.  Since checksums are optional, the packet
     * is still valid without a checksum.
     *
     * The sequence number is kept at 0 until it's time to actually
     * send the packet, so that the send functions have control over
     * the sequence numbers.
     */
};

/* These are the separators defined by the CIMD 2 spec */
#define STX 2   /* Start of packet */
#define ETX 3   /* End of packet */
#define TAB 9   /* End of parameter */

/* The same separators, in string form */
#define STX_str "\02"
#define ETX_str "\03"
#define TAB_str "\011"

/* A reminder that packets are created without a valid sequence number */
#define BOGUS_SEQUENCE 0

static Msg *cimd2_accept_delivery_report_message(struct packet *request,
						 SMSCConn *conn);
/* Look for the STX OO:SSS TAB header defined by CIMD 2, where OO is the
 * operation code in two decimals and SSS is the sequence number in three
 * decimals.  Leave the results in the proper fields of the packet.
 * Try to make sense of headers that don't fit this pattern; validating
 * the packet format is not our job. */
static void packet_parse_header(struct packet *packet)
{
    int pos;
    long number;

    /* Set default values, in case we can't parse the fields */
    packet->operation = -1;
    packet->seq = -1;

    pos = octstr_parse_long(&number, packet->data, 1, 10);
    if (pos < 0)
        return;
    packet->operation = number;

    if (octstr_get_char(packet->data, pos++) != ':')
        return;

    pos = octstr_parse_long(&number, packet->data, pos, 10);
    if (pos < 0)
        return;
    packet->seq = number;
}


/* Accept an Octstr containing one packet, build a struct packet around
 * it, and return that struct.  The Octstr is stored in the struct.
 * No error checking is done here yet. */
static struct packet *packet_parse(Octstr *packet_data)
{
    struct packet *packet;

    packet = gw_malloc(sizeof(*packet));
    packet->data = packet_data;

    /* Fill in packet->operation and packet->seq */
    packet_parse_header(packet);

    return packet;
}

/* Deallocate this packet */
static void packet_destroy(struct packet *packet)
{
    if (packet != NULL) {
        octstr_destroy(packet->data);
        gw_free(packet);
    }
}

/* Find the first packet in "in", delete it from "in", and return it as
 * a struct.  Return NULL if "in" contains no packet.  Always delete
 * leading non-packet data from "in".  (The CIMD 2 spec says we should
 * ignore any data between the packet markers). */
static struct packet *packet_extract(Octstr *in, SMSCConn *conn)
{
    int stx, etx;
    Octstr *packet;

    /* Find STX, and delete everything up to it */
    stx = octstr_search_char(in, STX, 0);
    if (stx < 0) {
        octstr_delete(in, 0, octstr_len(in));
        return NULL;
    } else {
        octstr_delete(in, 0, stx);
    }

    /* STX is now in position 0.  Find ETX. */
    etx = octstr_search_char(in, ETX, 1);
    if (etx < 0)
        return NULL;

    /* What shall we do with STX data... STX data... ETX?
     * Either skip to the second STX, or assume an ETX marker before
     * the STX.  Doing the latter has a chance of succeeding, and
     * will at least allow good logging of the error. */
    stx = octstr_search_char(in, STX, 1);
    if (stx >= 0 && stx < etx) {
        warning(0, "CIMD2[%s]: packet without end marker",
                octstr_get_cstr(conn->id));
        packet = octstr_copy(in, 0, stx);
        octstr_delete(in, 0, stx);
        octstr_append_cstr(packet, ETX_str);
    } else {
        /* Normal case. Copy packet, and cut it from the source. */
        packet = octstr_copy(in, 0, etx + 1);
        octstr_delete(in, 0, etx + 1);
    }

    return packet_parse(packet);
}

/* The get_parm functions always return the first parameter with the
 * correct id.  There is only one case where the spec allows multiple
 * parameters with the same id, and that is when an SMS has multiple
 * destination addresses.  We only support one destination address anyway. */

/* Look for the first parameter with id 'parmno' and return its value.
 * Return NULL if the parameter was not found. */
static Octstr *packet_get_parm(struct packet *packet, int parmno)
{
    long pos, next;
    long valuepos;
    long number;

    gw_assert(packet != NULL);
    pos = octstr_search_char(packet->data, TAB, 0);
    if (pos < 0)
        return NULL;  /* Bad packet, nothing we can do */

    /* Parameters have a tab on each end.  If we don't find the
     * closing tab, we're at the checksum, so we stop. */
    for ( ;
          (next = octstr_search_char(packet->data, TAB, pos + 1)) >= 0;
          pos = next) {
        if (octstr_parse_long(&number, packet->data, pos + 1, 10) < 0)
            continue;
        if (number != parmno)
            continue;
        valuepos = octstr_search_char(packet->data, ':', pos + 1);
        if (valuepos < 0)
            continue;  /* badly formatted parm */
        valuepos++;  /* skip the ':' */

        /* Found the right parameter */
        return octstr_copy(packet->data, valuepos, next - valuepos);
    }

    return NULL;
}


/* Look for an Integer parameter with id 'parmno' in the packet and
 * return its value.  Return INT_MIN if the parameter was not found.
 * (Unfortunately, -1 is a valid parameter value for at least one
 * parameter.) */
static long packet_get_int_parm(struct packet *packet, int parmno)
{
    Octstr *valuestr = NULL;
    long value;

    /* Our code should never even try a bad parameter access. */
    gw_assert(parm_type(parmno) == P_INT);

    valuestr = packet_get_parm(packet, parmno);
    if (!valuestr)
        goto error;

    if (octstr_parse_long(&value, valuestr, 0, 10) < 0)
        goto error;

    octstr_destroy(valuestr);
    return value;

error:
    octstr_destroy(valuestr);
    return INT_MIN;
}

/* Look for a String parameter with id 'parmno' in the packet and
 * return its value.  Return NULL if the parameter was not found.
 * No translations are done on the value. */
static Octstr *packet_get_string_parm(struct packet *packet, int parmno)
{
    /* Our code should never even try a bad parameter access. */
    gw_assert(parm_type(parmno) == P_STRING);

    return packet_get_parm(packet, parmno);
}

/* Look for an Address parameter with id 'parmno' in the packet and
 * return its value.  Return NULL if the parameter was not found.
 * No translations are done on the value. */
static Octstr *packet_get_address_parm(struct packet *packet, int parmno)
{
    /* Our code should never even try a bad parameter access. */
    gw_assert(parm_type(parmno) == P_ADDRESS);

    return packet_get_parm(packet, parmno);
}

/* Look for an SMS parameter with id 'parmno' in the packet and return its
 * value.  Return NULL if the parameter was not found.  No translations
 * are done on the value, so it will be in the ISO-Latin-1 character set
 * with CIMD2-specific escapes. */
static Octstr *packet_get_sms_parm(struct packet *packet, int parmno)
{
    /* Our code should never even try a bad parameter access. */
    gw_assert(parm_type(parmno) == P_SMS);

    return packet_get_parm(packet, parmno);
}

/* There is no packet_get_time_parm because the CIMD 2 timestamp
 * format is useless.  It's in the local time of the MC, with
 * a 2-digit year and no DST information.  We can do without.
 */

/* Look for a Hex parameter with id 'parmno' in the packet and return
 * its value.  Return NULL if the parameter was not found.  The value
 * is de-hexed. */
static Octstr *packet_get_hex_parm(struct packet *packet, int parmno)
{
    Octstr *value = NULL;

    /* Our code should never even try a bad parameter access. */
    gw_assert(parm_type(parmno) == P_HEX);

    value = packet_get_parm(packet, parmno);
    if (!value)
        goto error;

    if (octstr_hex_to_binary(value) < 0)
        goto error;

    return value;

error:
    octstr_destroy(value);
    return NULL;
}


/* Check if the header is according to CIMD 2 spec, generating log
 * entries as necessary.  Return -1 if anything was wrong, otherwise 0. */
static int packet_check_header(struct packet *packet, SMSCConn *conn)
{
    Octstr *data;

    gw_assert(packet != NULL);
    data = packet->data;

    /* The header must have a two-digit operation code, a colon,
     * and a three-digit sequence number, followed by a tab.
     * (CIMD2, 3.1) */
    if (octstr_len(data) < 8 ||
        !octstr_check_range(data, 1, 2, gw_isdigit) ||
        octstr_get_char(data, 3) != ':' ||
        !octstr_check_range(data, 4, 3, gw_isdigit) ||
        octstr_get_char(data, 7) != TAB) {
        warning(0, "CIMD2[%s]: packet header in wrong format",
                octstr_get_cstr(conn->id));
        return -1;
    }

    return 0;
}

static int packet_check_parameter(struct packet *packet, long pos, long len, SMSCConn *conn)
{
    Octstr *data;
    long parm;
    long dpos, dlen;
    int negative;
    long value;
    int i;
    int errors = 0;

    gw_assert(packet != NULL);
    data = packet->data;

    /* The parameter header should be TAB, followed by a three-digit
     * parameter number, a colon, and the data.  We already know about
     * the tab. */

    if (len < 5 ||
        !octstr_check_range(data, pos + 1, 3, gw_isdigit) ||
        octstr_get_char(data, pos + 4) != ':') {
        warning(0, "CIMD2[%s]: parameter at offset %ld in wrong format",
                octstr_get_cstr(conn->id),
                pos);
        errors++;
    }

    /* If we can't parse a parameter number, there's nothing more
     * that we can check. */
    dpos = octstr_parse_long(&parm, data, pos + 1, 10);
    if (dpos < 0)
        return -1;
    if (octstr_get_char(data, dpos) == ':')
        dpos++;
    dlen = len - (dpos - pos);
    /* dlen can not go negative because octstr_parse_long must have
     * been stopped by the TAB at the end of the parameter data. */
    gw_assert(dlen >= 0);

    i = parm_index(parm);

    if (i < 0) {
        warning(0, "CIMD2[%s]: packet contains unknown parameter %ld", 
                octstr_get_cstr(conn->id),
                parm);
        return -1;
    }

    if (dlen > parameters[i].maxlen) {
        warning(0, "CIMD2[%s]: packet has '%s' parameter with length %ld, spec says max %d",
                octstr_get_cstr(conn->id),
                parameters[i].name, len, parameters[i].maxlen);
        errors++;
    }

    switch (parameters[i].type) {
    case P_INT:
        /* Allow a leading - */
        negative = (octstr_get_char(data, dpos) == '-');
        if (!octstr_check_range(data, dpos + negative,
                                dlen - negative, gw_isdigit)) {
            warning(0, "CIMD2[%s]: packet has '%s' parameter with non-integer contents", 
                    octstr_get_cstr(conn->id),
                    parameters[i].name);
            errors++;
        }
        if (octstr_parse_long(&value, data, dpos, 10) >= 0 &&
            (value < parameters[i].minval || value > parameters[i].maxval)) {
            warning(0, "CIMD2[%s]: packet has '%s' parameter out of range (value %ld, min %d, max %d)",
                    octstr_get_cstr(conn->id),
                    parameters[i].name, value,
                    parameters[i].minval, parameters[i].maxval);
            errors++;
        }
        break;
    case P_TIME:
        if (!octstr_check_range(data, dpos, dlen, gw_isdigit)) {
            warning(0, "CIMD2[%s]: packet has '%s' parameter with non-digit contents", 
                    octstr_get_cstr(conn->id),
                    parameters[i].name);
            errors++;
        }
        break;
    case P_ADDRESS:
        if (!octstr_check_range(data, dpos, dlen, isphonedigit)) {
            warning(0, "CIMD2[%s]: packet has '%s' parameter with non phone number contents", 
                    octstr_get_cstr(conn->id),
                    parameters[i].name);
            errors++;
        }
        break;
    case P_HEX:
        if (!octstr_check_range(data, dpos, dlen, gw_isxdigit)) {
            warning(0, "CIMD2[%s]: packet has '%s' parameter with non-hex contents", 
                    octstr_get_cstr(conn->id),
                    parameters[i].name);
            errors++;
        }
        if (dlen % 2 != 0) {
            warning(0, "CIMD2[%s]: packet has odd-length '%s' parameter", 
                    octstr_get_cstr(conn->id),
                    parameters[i].name);
            errors++;
        }
        break;
    case P_SMS:
    case P_STRING:  /* nothing to check */
        break;
    }

    if (errors > 0)
        return -1;
    return 0;
}


/* Check the packet against the CIMD 2 spec, generating log entries as
 * necessary. Return -1 if anything was wrong, otherwise 0. */
/* TODO: Check if parameters found actually belong in the packet type */
static int packet_check(struct packet *packet, SMSCConn *conn)
{
    int errors = 0;
    long pos, next;
    Octstr *data;

    gw_assert(packet != NULL);
    data = packet->data;

    if (octstr_search_char(data, 0, 0) >= 0) {
        /* CIMD2 spec does not allow NUL bytes in a packet */
        warning(0, "CIMD2[%s]: packet contains NULs",
                octstr_get_cstr(conn->id));
        errors++;
    }

    /* Assume the packet starts with STX and ends with ETX,
     * because we parsed it that way in the first place. */

    errors += (packet_check_header(packet,conn) < 0);

    /* Parameters are separated by tabs.  After the last parameter
     * there is a tab, an optional two-digit checksum, and the ETX.
     * Check each parameter in turn, by skipping from tab to tab.
     */
    /* Start at the first tab, wherever it is, so that we can still
     * check parameters if the header was weird. */
    pos = octstr_search_char(data, TAB, 0);
    for ( ; pos >= 0; pos = next) {
        next = octstr_search_char(data, TAB, pos + 1);
        if (next >= 0) {
            errors += (packet_check_parameter(packet, pos, next - pos, conn) < 0);
        } else {
            /* Check if the checksum has the right format.  Don't
             * check the sum itself here, that will be done in a
             * separate call later. */
            /* There are two valid formats: TAB ETX (no checksum)
             * and TAB digit digit ETX.  We already know the TAB
             * and the ETX are there. */
            if (!(octstr_len(data) - pos == 2 ||
                  (octstr_len(data) - pos == 4 &&
                   octstr_check_range(data, pos + 1, 2, gw_isxdigit)))) {
                warning(0, "CIMD2[%s]: packet checksum in wrong format",
                        octstr_get_cstr(conn->id));
                errors++;
            }
        }
    }


    if (errors > 0) {
        octstr_dump(packet->data, 0);
        return -1;
    }

    return 0;
}

static void packet_check_can_receive(struct packet *packet, SMSCConn *conn)
{
    gw_assert(packet != NULL);

    if (!operation_can_receive(packet->operation)) {
        Octstr *name = operation_name(packet->operation);
        warning(0, "CIMD2[%s]: SMSC sent us %s request",
                octstr_get_cstr(conn->id),
                octstr_get_cstr(name));
        octstr_destroy(name);
    }
}

/* Table of known error codes */
static struct
{
    int code;
    char *text;
}
cimd2_errors[] = {
    { 0, "No error" },
    { 1, "Unexpected operation" },
    { 2, "Syntax error" },
    { 3, "Unsupported parameter error" },
    { 4, "Connection to message center lost" },
    { 5, "No response from message center" },
    { 6, "General system error" },
    { 7, "Cannot find information" },
    { 8, "Parameter formatting error" },
    { 9, "Requested operation failed" },
    /* LOGIN error codes */
    { 100, "Invalid login" },
    { 101, "Incorrect access type" },
    { 102, "Too many users with this login id" },
    { 103, "Login refused by message center" },
    /* SUBMIT MESSAGE error codes */
    { 300, "Incorrect destination address" },
    { 301, "Incorrect number of destination addresses" },
    { 302, "Syntax error in user data parameter" },
    { 303, "Incorrect bin/head/normal user data parameter combination" },
    { 304, "Incorrect data coding scheme parameter usage" },
    { 305, "Incorrect validity period parameters usage" },
    { 306, "Incorrect originator address usage" },
    { 307, "Incorrect pid paramterer usage" },
    { 308, "Incorrect first delivery parameter usage" },
    { 309, "Incorrect reply path usage" },
    { 310, "Incorrect status report request parameter usage" },
    { 311, "Incorrect cancel enabled parameter usage" },
    { 312, "Incorrect priority parameter usage" },
    { 313, "Incorrect tariff class parameter usage" },
    { 314, "Incorrect service description parameter usage" },
    { 315, "Incorrect transport type parameter usage" },
    { 316, "Incorrect message type parameter usage" },
    { 318, "Incorrect mms parameter usage" },
    { 319, "Incorrect operation timer parameter usage" },
    /* ENQUIRE MESSAGE STATUS error codes */
    { 400, "Incorrect address parameter usage" },
    { 401, "Incorrect scts parameter usage" },
    /* DELIVERY REQUEST error codes */
    { 500, "Incorrect scts parameter usage" },
    { 501, "Incorrect mode parameter usage" },
    { 502, "Incorrect parameter combination" },
    /* CANCEL MESSAGE error codes */
    { 600, "Incorrect scts parameter usage" },
    { 601, "Incorrect address parameter usage" },
    { 602, "Incorrect mode parameter usage" },
    { 603, "Incorrect parameter combination" },
    /* SET error codes */
    { 800, "Changing password failed" },
    { 801, "Changing password not allowed" },
    /* GET error codes */
    { 900, "Unsupported item requested" },
    { -1, NULL }
};

static int packet_display_error(struct packet *packet, SMSCConn *conn)
{
    int code;
    Octstr *text = NULL;
    Octstr *opname = NULL;

    code = packet_get_int_parm(packet, P_ERROR_CODE);
    text = packet_get_string_parm(packet, P_ERROR_TEXT);

    if (code <= 0) {
        octstr_destroy(text);
        return 0;
    }

    if (text == NULL) {
        /* No error text.  Try to find it in the table. */
        int i;
        for (i = 0; cimd2_errors[i].text != NULL; i++) {
            if (cimd2_errors[i].code == code) {
                text = octstr_create(cimd2_errors[i].text);
                break;
            }
        }
    }

    if (text == NULL) {
        /* Still no error text.  Make one up. */
        text = octstr_create("Unknown error");
    }

    opname = operation_name(packet->operation);
    error(0, "CIMD2[%s]: %s contained error message:",
          octstr_get_cstr(conn->id),
          octstr_get_cstr(opname));
    error(0, "code %03d: %s", code, octstr_get_cstr(text));
    octstr_destroy(opname);
    octstr_destroy(text);
    return code;
}

/* Table of special combinations, for convert_gsm_to_latin1. */
/* Each cimd1, cimd2 pair is mapped to a character in the GSM default
 * character set. */
static const struct
{
    unsigned char cimd1, cimd2;
    unsigned char gsm;
}
cimd_combinations[] = {
    { 'O', 'a', 0 },     /* @ */
    { 'L', '-', 1 },     /* Pounds sterling */
    { 'Y', '-', 3 },     /* Yen */
    { 'e', '`', 4 },     /* egrave */
    { 'e', '\'', 5 },    /* eacute */
    { 'u', '`', 6 },     /* ugrave */
    { 'i', '`', 7 },     /* igrave */
    { 'o', '`', 8 },     /* ograve */
    { 'C', ',', 9 },     /* C cedilla */
    { 'O', '/', 11 },    /* Oslash */
    { 'o', '/', 12 },    /* oslash */
    { 'A', '*', 14 },    /* Aring */
    { 'a', '*', 15 },    /* aring */
    { 'g', 'd', 16 },    /* greek delta */
    { '-', '-', 17 },    /* underscore */
    { 'g', 'f', 18 },    /* greek phi */
    { 'g', 'g', 19 },    /* greek gamma */
    { 'g', 'l', 20 },    /* greek lambda */
    { 'g', 'o', 21 },    /* greek omega */
    { 'g', 'p', 22 },    /* greek pi */
    { 'g', 'i', 23 },    /* greek psi */
    { 'g', 's', 24 },    /* greek sigma */
    { 'g', 't', 25 },    /* greek theta */
    { 'g', 'x', 26 },    /* greek xi */
    { 'X', 'X', 27 },    /* escape */
    { 'A', 'E', 28 },    /* AE ligature */
    { 'a', 'e', 29 },    /* ae ligature */
    { 's', 's', 30 },    /* german double s */
    { 'E', '\'', 31 },   /* Eacute */
    { 'q', 'q', '"' },
    { 'o', 'x', 36 },    /* international currency symbol */
    { '!', '!', 64 },    /* inverted ! */
    { 'A', '"', 91 },    /* Adieresis */
    { 'O', '"', 92 },    /* Odieresis */
    { 'N', '~', 93 },    /* N tilde */
    { 'U', '"', 94 },    /* Udieresis */
    { 's', 'o', 95 },    /* section mark */
    { '?', '?', 96 },    /* inverted ? */
    { 'a', '"', 123 },   /* adieresis */
    { 'o', '"', 124 },   /* odieresis */
    { 'n', '~', 125 },   /* n tilde */
    { 'u', '"', 126 },   /* udieresis */
    { 'a', '`', 127 },   /* agrave */
    { 0, 0, 0 }
};


/* Convert text in the CIMD2 User Data format to the GSM default
 * character set.
 * CIMD2 allows 8-bit characters in this format; they map directly
 * to the corresponding ISO-8859-1 characters.  Since we are heading
 * toward that character set in the end, we don't bother converting
 * those to GSM. */
static void convert_cimd2_to_gsm(Octstr *text, SMSCConn *conn)
{
    long pos, len;
    int cimd1, cimd2;
    int c;
    int i;

    /* CIMD2 uses four single-character mappings that do not map
     * to themselves:
     * '@' from 64 to 0, '$' from 36 to 2, ']' from 93 to 14 (A-ring),
     * and '}' from 125 to 15 (a-ring).
     * Other than those, we only have to worry about the escape
     * sequences introduced by _ (underscore).
     */

    len = octstr_len(text);
    for (pos = 0; pos < len; pos++) {
        c = octstr_get_char(text, pos);
        if (c == '@')
            octstr_set_char(text, pos, 0);
        else if (c == '$')
            octstr_set_char(text, pos, 2);
        else if (c == ']')
            octstr_set_char(text, pos, 14);
        else if (c == '}')
            octstr_set_char(text, pos, 15);
        else if (c == '_' && pos + 2 < len) {
            cimd1 = octstr_get_char(text, pos + 1);
            cimd2 = octstr_get_char(text, pos + 2);
            for (i = 0; cimd_combinations[i].cimd1 != 0; i++) {
                if (cimd_combinations[i].cimd1 == cimd1 &&
                    cimd_combinations[i].cimd2 == cimd2)
                    break;
            }
            if (cimd_combinations[i].cimd1 == 0)
                warning(0, "CIMD2[%s]: Encountered unknown "
                        "escape code _%c%c, ignoring.",
                        octstr_get_cstr(conn->id),
                        cimd1, cimd2);
            else {
                octstr_delete(text, pos, 2);
                octstr_set_char(text, pos, cimd_combinations[i].gsm);
                len = octstr_len(text);
            }
        }
    }
}


/* Convert text in the GSM default character set to the CIMD2 User Data
 * format, which is a representation of the GSM default character set
 * in the lower 7 bits of ISO-8859-1.  (8-bit characters are also
 * allowed, but it's just as easy not to use them.) */
static void convert_gsm_to_cimd2(Octstr *text)
{
    long pos, len;

    len = octstr_len(text);
    for (pos = 0; pos < len; pos++) {
        int c, i;

        c = octstr_get_char(text, pos);
        /* If c is not in the GSM alphabet at this point,
         * the caller did something badly wrong. */
        gw_assert(c >= 0);
        gw_assert(c < 128);

        for (i = 0; cimd_combinations[i].cimd1 != 0; i++) {
            if (cimd_combinations[i].gsm == c)
                break;
        }

        if (cimd_combinations[i].gsm == c) {
            /* Escape sequence */
            octstr_insert_data(text, pos, "_ ", 2);
            pos += 2;
            len += 2;
            octstr_set_char(text, pos - 1, cimd_combinations[i].cimd1);
            octstr_set_char(text, pos, cimd_combinations[i].cimd2);
        } else if (c == 2) {
            /* The dollar sign is the only GSM character that
            	 * does not have a CIMD escape sequence and does not
             * map to itself. */
            octstr_set_char(text, pos, '$');
        }
    }
}


/***************************************************************************/
/* Packet encoding functions.  They do not allow the creation of invalid   */
/* CIMD 2 packets.                                                         */
/***************************************************************************/

/* Build a new packet struct with this operation code and sequence number. */
static struct packet *packet_create(int operation, int seq)
{
    struct packet *packet;
    char minpacket[sizeof("sOO:SSSte")];

    packet = gw_malloc(sizeof(*packet));
    packet->operation = operation;
    packet->seq = seq;
    sprintf(minpacket, STX_str "%02d:%03d" TAB_str ETX_str, operation, seq);
    packet->data = octstr_create(minpacket);

    return packet;
}

/* Add a parameter to the end of packet */
static void packet_add_parm(struct packet *packet, int parmtype,
                            int parmno, Octstr *value, SMSCConn *conn)
{
    char parmh[sizeof("tPPP:")];
    long position;
    long len;
    int copied = 0;

    len = octstr_len(value);

    gw_assert(packet != NULL);
    gw_assert(parm_type(parmno) == parmtype);

    if (len > parm_maxlen(parmno)) {
        warning(0, "CIMD2[%s]: %s parameter too long, truncating from "
                "%ld to %ld characters",
                octstr_get_cstr(conn->id), 
                parm_name(parmno),
                len, 
                (long) parm_maxlen(parmno));
        value = octstr_copy(value, 0, parm_maxlen(parmno));
        copied = 1;
    }

    /* There's a TAB and ETX at the end; insert it before those.
     * The new parameter will come with a new starting TAB. */
    position = octstr_len(packet->data) - 2;

    sprintf(parmh, TAB_str "%03d:", parmno);
    octstr_insert_data(packet->data, position, parmh, strlen(parmh));
    octstr_insert(packet->data, value, position + strlen(parmh));
    if (copied)
        octstr_destroy(value);
}

/* Add a String parameter to the packet */
static void packet_add_string_parm(struct packet *packet, int parmno, Octstr *value, SMSCConn *conn)
{
    packet_add_parm(packet, P_STRING, parmno, value, conn);
}

/* Add an Address parameter to the packet */
static void packet_add_address_parm(struct packet *packet, int parmno, Octstr *value, SMSCConn *conn)
{
    gw_assert(octstr_check_range(value, 0, octstr_len(value), isphonedigit));
    packet_add_parm(packet, P_ADDRESS, parmno, value, conn);
}

/* Add an SMS parameter to the packet.  The caller is expected to have done
 * the translation to the GSM character set already.  */
static void packet_add_sms_parm(struct packet *packet, int parmno, Octstr *value, SMSCConn *conn)
{
    packet_add_parm(packet, P_SMS, parmno, value, conn);
}

/* There is no function for adding a Time parameter to the packet, because
 * the format makes Time parameters useless for us.  If you find that you
 * need to use them, then also add code for querying the SMS center timestamp
 * and using that for synchronization.  And beware of DST changes. */

/* Add a Hexadecimal parameter to the packet */
static void packet_add_hex_parm(struct packet *packet, int parmno, Octstr *value, SMSCConn *conn)
{
    value = octstr_duplicate(value);
    octstr_binary_to_hex(value, 1);   /* 1 for uppercase hex, i.e. A .. F */
    packet_add_parm(packet, P_HEX, parmno, value, conn);
    octstr_destroy(value);
}

/* Add an Integer parameter to the packet */
static void packet_add_int_parm(struct packet *packet, int parmno, long value, SMSCConn *conn)
{
    char buf[128];
    Octstr *valuestr;

    gw_assert(parm_in_range(parmno, value));

    sprintf(buf, "%ld", value);
    valuestr = octstr_create(buf);
    packet_add_parm(packet, P_INT, parmno, valuestr, conn);
    octstr_destroy(valuestr);
}

static void packet_set_checksum(struct packet *packet)
{
    Octstr *data;
    int checksum;
    long pos, len;
    char buf[16];

    gw_assert(packet != NULL);

    data = packet->data;
    if (octstr_get_char(data, octstr_len(data) - 2) != TAB) {
        /* Packet already has checksum; kill it. */
        octstr_delete(data, octstr_len(data) - 3, 2);
    }

    gw_assert(octstr_get_char(data, octstr_len(data) - 2) == TAB);

    /* Sum all the way up to the last TAB */
    checksum = 0;
    for (pos = 0, len = octstr_len(data); pos < len - 1; pos++) {
        checksum += octstr_get_char(data, pos);
        checksum &= 0xff;
    }

    sprintf(buf, "%02X", checksum);
    octstr_insert_data(data, len - 1, buf, 2);
}

static void packet_set_sequence(struct packet *packet, int seq)
{
    char buf[16];

    gw_assert(packet != NULL);
    gw_assert(seq >= 0);
    gw_assert(seq < 256);

    sprintf(buf, "%03d", seq);

    /* Start at 4 to skip the <STX> ZZ: part of the header. */
    octstr_set_char(packet->data, 4, buf[0]);
    octstr_set_char(packet->data, 5, buf[1]);
    octstr_set_char(packet->data, 6, buf[2]);
    packet->seq = seq;
}

static struct packet *packet_encode_message(Msg *msg, Octstr *sender_prefix, SMSCConn *conn)
{
    struct packet *packet;
    PrivData *pdata = conn->data;
    Octstr *text;
    int spaceleft;
    long truncated;
    int dcs = 0;
    int setvalidity = 0;

    gw_assert(msg != NULL);
    gw_assert(msg->type == sms);
    gw_assert(msg->sms.receiver != NULL);

    dcs = fields_to_dcs(msg, (msg->sms.alt_dcs != -1 ? 
        msg->sms.alt_dcs : conn->alt_dcs));
    if (msg->sms.sender == NULL)
        msg->sms.sender = octstr_create("");

    if (!parm_valid_address(msg->sms.receiver)) {
        warning(0, "CIMD2[%s]: non-digits in destination phone number '%s', discarded",
                octstr_get_cstr(conn->id),
                octstr_get_cstr(msg->sms.receiver));
        return NULL;
    }

    packet = packet_create(SUBMIT_MESSAGE, BOGUS_SEQUENCE);

    packet_add_address_parm(packet, P_DESTINATION_ADDRESS, msg->sms.receiver, conn);

    /* CIMD2 interprets the originating address as a sub-address to
     * our connection number (so if the connection is "400" and we
     * fill in "600" as the sender number, the user sees "400600").
     * Since we have no way to ask what this number is, it has to
     * be configured. */

    /* Quick and dirty check to see if we are using alphanumeric sender */
    if (parm_valid_address(msg->sms.sender)) {
        /* We are not, so send in the usual way */
        /* Speed up the default case */
        if (octstr_len(sender_prefix) == 0) {
            packet_add_address_parm(packet, P_ORIGINATING_ADDRESS,msg->sms.sender, conn);
        }
        else if (octstr_compare(sender_prefix, octstr_imm("never")) != 0) {
            if (octstr_ncompare(sender_prefix, msg->sms.sender,
                                octstr_len(sender_prefix)) == 0) {
                Octstr *sender;
                sender = octstr_copy(msg->sms.sender,
                                     octstr_len(sender_prefix), octstr_len(msg->sms.sender));
                packet_add_address_parm(packet, P_ORIGINATING_ADDRESS, sender, conn);
                octstr_destroy(sender);
            } else {
                warning(0, "CIMD2[%s]: Sending message with originating address <%s>, "
                        "which does not start with the sender-prefix.",
                        octstr_get_cstr(conn->id),
                        octstr_get_cstr(msg->sms.sender));
            }
        }
    }
    else {
        /* The test above to check if sender was all digits failed, so assume we want alphanumeric sender */
        packet_add_string_parm(packet, P_ALPHANUMERIC_ORIGINATING_ADDRESS,msg->sms.sender, conn);
    }

    /* Add the validity period if necessary.  This sets the relative validity
     * period as this is the description of the "validity" parameter of the
     * sendsms interface.
     *
     * Convert from minutes to GSM 03.40 specification (section 9.2.3.12).
     * 0-143   = 0 to 12 hours in 5 minute increments.
     * 144-167 = 12hrs30min to 24hrs in 30 minute increments.
     * 168-196 = 2days to 30days in 1 day increments.
     * 197-255 = 5weeks to 63weeks in 1 week increments.
     *
     * This code was copied from smsc_at2.c.
     */
    if (msg->sms.validity != MSG_PARAM_UNDEFINED) {
        long val = (msg->sms.validity - time(NULL)) / 60;
        if (val > 635040)
            setvalidity = 255;
        if (val >= 50400 && val <= 635040)
            setvalidity = (val - 1) / 7 / 24 / 60 + 192 + 1;
        if (val > 43200 && val < 50400)
            setvalidity = 197;
        if (val >= 2880 && val <= 43200)
            setvalidity = (val - 1) / 24 / 60 + 166 + 1;
        if (val > 1440 && val < 2880)
            setvalidity = 168;
        if (val >= 750 && val <= 1440)
            setvalidity = (val - 720 - 1) / 30 + 143 + 1;
        if (val > 720 && val < 750)
            setvalidity = 144;
        if (val >= 5 && val <= 720)
            setvalidity = (val - 1) / 5 - 1 + 1;
        if (val < 5)
            setvalidity = 0;

        packet_add_int_parm(packet, P_VALIDITY_PERIOD_RELATIVE, setvalidity, conn);
    }

    /* Explicitly ask not to get status reports.
     * If we do not do this, the server's default might be to
     * send status reports in some cases, and we don't do anything
     * with those reports anyway. */
    /* ask for the delivery reports if needed*/

    if (!pdata->no_dlr)
        if (DLR_IS_SUCCESS_OR_FAIL(msg->sms.dlr_mask))
            packet_add_int_parm(packet, P_STATUS_REPORT_REQUEST, 14, conn);
    	else
            packet_add_int_parm(packet, P_STATUS_REPORT_REQUEST, 0, conn);
    else if( pdata->no_dlr && DLR_IS_SUCCESS_OR_FAIL(msg->sms.dlr_mask)) 
    	warning(0, "CIMD2[%s]: dlr request make no sense while no-dlr set to true",
    		 octstr_get_cstr(conn->id));

    /* Turn off reply path as default.
     * This avoids phones automatically asking for a reply
     */
    if (msg->sms.rpi > 0)
	packet_add_int_parm(packet, P_REPLY_PATH, 1, conn);
    else
    	packet_add_int_parm(packet, P_REPLY_PATH, 0, conn);

    /* Use binfo to set the tariff class */
    if (octstr_len(msg->sms.binfo))
	packet_add_parm(packet, P_INT, P_TARIFF_CLASS, msg->sms.binfo, conn);

    /* Set the protocol identifier if requested */
    if (msg->sms.pid > 0)
        packet_add_int_parm(packet, P_PROTOCOL_IDENTIFIER, msg->sms.pid, conn);
 
	/* If there are more messages to the same destination, then set the
	* More Messages to Send flag. This allow faster delivery of many messages 
	* to the same destination
	*/
    if (msg->sms.msg_left > 0)
        packet_add_int_parm(packet, P_MORE_MESSAGES_TO_SEND, 1, conn);
    else
        packet_add_int_parm(packet, P_MORE_MESSAGES_TO_SEND, 0, conn);
	
    truncated = 0;

    spaceleft = 140;
    if (octstr_len(msg->sms.udhdata)) {
        /* udhdata will be truncated and warned about if
         * it does not fit. */
        packet_add_hex_parm(packet, P_USER_DATA_HEADER, msg->sms.udhdata, conn);
    }
    if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF)
        spaceleft = spaceleft * 8 / 7;
    if (spaceleft < 0)
        spaceleft = 0;

    text = octstr_duplicate(msg->sms.msgdata);

    if (octstr_len(text) > 0 && spaceleft == 0) {
        warning(0, "CIMD2[%s]: message filled up with UDH, no room for message text",
                octstr_get_cstr(conn->id));
    } else if (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2) {
        if (octstr_len(text) > spaceleft) {
            truncated = octstr_len(text) - spaceleft;
            octstr_truncate(text, spaceleft);
        }
        packet_add_hex_parm(packet, P_USER_DATA_BINARY, text, conn);
    } else {
        /* Going from latin1 to GSM to CIMD2 may seem like a
         * detour, but it's the only way to get all the escape
         * codes right. */
        charset_utf8_to_gsm(text);
        truncated = charset_gsm_truncate(text, spaceleft);
        convert_gsm_to_cimd2(text);
        packet_add_sms_parm(packet, P_USER_DATA, text, conn);
    }

    if (dcs != 0)
        packet_add_int_parm(packet, P_DATA_CODING_SCHEME, dcs, conn);

    if (truncated > 0) {
        warning(0, "CIMD2[%s]: truncating message text to fit in %d characters.", 
                octstr_get_cstr(conn->id),
                spaceleft);
    }

    octstr_destroy(text);
    return packet;
}

/***************************************************************************/
/* Protocol functions.  These implement various transactions.              */
/***************************************************************************/

/* Give this packet a proper sequence number for sending. */
static void packet_set_send_sequence(struct packet *packet, PrivData *pdata)
{
    gw_assert(pdata != NULL);
    /* LOGIN packets always have sequence number 001 */
    if (packet->operation == LOGIN)
        pdata->send_seq = 1;
    /* Send sequence numbers are always odd, receiving are always even */
    gw_assert(pdata->send_seq % 2 == 1);

    packet_set_sequence(packet, pdata->send_seq);
    pdata->send_seq += 2;
    if (pdata->send_seq > 256)
        pdata->send_seq = 1;
}

static struct packet *cimd2_get_packet(PrivData *pdata, Octstr **ts)
{
    struct packet *packet = NULL;

    gw_assert(pdata != NULL);

    /* If packet is already available, don't try to read anything */
    packet = packet_extract(pdata->inbuffer, pdata->conn);

    while (packet == NULL) {
        if (read_available(pdata->socket, RESPONSE_TIMEOUT) != 1) {
            warning(0, "CIMD2[%s]: SMSC is not responding",
                    octstr_get_cstr(pdata->conn->id));
            return NULL;
        }

        if (octstr_append_from_socket(pdata->inbuffer, pdata->socket) <= 0) {
            error(0, "CIMD2[%s]: cimd2_get_packet: read failed",
                  octstr_get_cstr(pdata->conn->id));
            return NULL;
        }

        packet = packet_extract(pdata->inbuffer, pdata->conn);
    }

    packet_check(packet,pdata->conn);
    packet_check_can_receive(packet,pdata->conn);
    debug("bb.sms.cimd2", 0, "CIMD2[%s]: received: <%s>",
          octstr_get_cstr(pdata->conn->id), 
          octstr_get_cstr(packet->data));
    if (ts)
        *ts = packet_get_parm(packet,P_MC_TIMESTAMP);

    if (pdata->keepalive > 0)
        pdata->next_ping = time(NULL) + pdata->keepalive;

    return packet;
}

/* Acknowledge a request.  The CIMD 2 spec only defines positive responses
 * to the server, because the server is perfect. */
static void cimd2_send_response(struct packet *request, PrivData *pdata)
{
    struct packet *response;

    gw_assert(request != NULL);
    gw_assert(request->operation < RESPONSE);

    response = packet_create(request->operation + RESPONSE, request->seq);
    packet_set_checksum(response);

    debug("bb.sms.cimd2", 0, "CIMD2[%s]: sending <%s>",
          octstr_get_cstr(pdata->conn->id),
          octstr_get_cstr(response->data));

    /* Don't check errors here because if there is something
     * wrong with the socket, the main loop will detect it. */
    octstr_write_to_socket(pdata->socket, response->data);

    packet_destroy(response);
}

static Msg *cimd2_accept_message(struct packet *request, SMSCConn *conn)
{
    Msg *message = NULL;
    Octstr *destination = NULL;
    Octstr *origin = NULL;
    Octstr *UDH = NULL;
    Octstr *text = NULL;
    int DCS;

    /* See GSM 03.38.  The bit patterns we can handle are:
     *   000xyyxx  Uncompressed text, yy indicates alphabet.
     *                   yy = 00, default alphabet
     *                   yy = 01, 8-bit data
     *                   yy = 10, UCS-2
     *                   yy = 11, reserved
     *   1111xyxx  Data, y indicates alphabet.
     *                   y = 0, default alphabet
     *                   y = 1, 8-bit data
     */
    DCS = packet_get_int_parm(request, P_DATA_CODING_SCHEME);

    destination = packet_get_address_parm(request, P_DESTINATION_ADDRESS);
    origin = packet_get_address_parm(request, P_ORIGINATING_ADDRESS);
    UDH = packet_get_hex_parm(request, P_USER_DATA_HEADER);
    /* Text is either in User Data or User Data Binary field. */
    text = packet_get_sms_parm(request, P_USER_DATA);
    if (text != NULL) {
        convert_cimd2_to_gsm(text,conn);
        charset_gsm_to_utf8(text);
    } else {
        /*
         * FIXME: If DCS indicates GSM charset, and we get it in binary,
         * then it's probably bit-packed.  We'll have to undo it because
         * our "charset_gsm" means one gsm character per octet.  This is
         * not currently supported. -- RB
         */
        text = packet_get_hex_parm(request, P_USER_DATA_BINARY);
    }

    /* Code elsewhere in the gateway always expects the sender and
     * receiver fields to be filled, so we discard messages that
     * lack them.  If they should not be discarded, then the code
     * handling sms messages should be reviewed.  -- RB */
    if (!destination || octstr_len(destination) == 0) {
        info(0, "CIMD2[%s]: Got SMS without receiver, discarding.",
              octstr_get_cstr(conn->id));
        goto error;
    }
    if (!origin || octstr_len(origin) == 0) {
        info(0, "CIMD2[%s]: Got SMS without sender, discarding.",
              octstr_get_cstr(conn->id));
        goto error;
    }

    if (!text && (!UDH || octstr_len(UDH) == 0)) {
        info(0, "CIMD2[%s]: Got empty SMS, ignoring.",
              octstr_get_cstr(conn->id));
        goto error;
    }
    
    message = msg_create(sms);
    if (! dcs_to_fields(&message, DCS)) {
	/* XXX Should reject this message ? */
        debug("bb.sms.cimd2", 0, "CIMD2[%s]: Invalid DCS",
              octstr_get_cstr(conn->id));
	dcs_to_fields(&message, 0);
    }
    time(&message->sms.time);
    message->sms.sender = origin;
    message->sms.receiver = destination;
    if (UDH) {
        message->sms.udhdata = UDH;
    }
    message->sms.msgdata = text;
    return message;

error:
    msg_destroy(message);
    octstr_destroy(destination);
    octstr_destroy(origin);
    octstr_destroy(UDH);
    octstr_destroy(text);
    return NULL;
}

/* Deal with a request from the CIMD2 server, and acknowledge it. */
static void cimd2_handle_request(struct packet *request, SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    Msg *message = NULL;

    if ((request->seq == 254 && pdata->receive_seq == 0) ||
            request->seq == pdata->receive_seq - 2) {
        warning(0, "CIMD2[%s]: request had same sequence number as previous.",
                octstr_get_cstr(conn->id));
    }
    else {
        pdata->receive_seq = request->seq + 2;
        if (pdata->receive_seq > 254)
            pdata->receive_seq = 0;

        if (request->operation == DELIVER_STATUS_REPORT) {
            message = cimd2_accept_delivery_report_message(request, conn);
            if (message)
                gwlist_append(pdata->received, message);
        }
        else if (request->operation == DELIVER_MESSAGE) {
            message = cimd2_accept_message(request,conn);
            if (message)
                gwlist_append(pdata->received, message);
        }
    }

    cimd2_send_response(request, pdata);
}

/* Send a request and wait for the ack.  If the other side responds with
 * an error code, attempt to correct and retry.
 * If other packets arrive while we wait for the ack, handle them.
 *
 * Return -1 if the SMSC refused the request.  Return -2 for other
 * errors, such as being unable to send the request at all.  If the
 * function returns -2, the caller would do well to try to reopen the
 * connection.
 *
 * The SMSCenter must be already open.
 *
 * TODO: This function has grown large and complex.  Break it up
 * into smaller pieces.
 */
static int cimd2_request(struct packet *request, SMSCConn *conn, Octstr **ts)
{
    PrivData *pdata = conn->data;
    int ret;
    struct packet *reply = NULL;
    int errorcode;
    int tries = 0;

    gw_assert(pdata != NULL);
    gw_assert(request != NULL);
    gw_assert(operation_can_send(request->operation));

    if (pdata->socket < 0) {
        warning(0, "CIMD2[%s]: cimd2_request: socket not open.",
                octstr_get_cstr(conn->id));
        return -2;        
    }
    
retransmit:
    packet_set_send_sequence(request, pdata);
    packet_set_checksum(request);

    debug("bb.sms.cimd2", 0, "CIMD2[%s]: sending <%s>",
          octstr_get_cstr(conn->id),
          octstr_get_cstr(request->data));

    ret = octstr_write_to_socket(pdata->socket, request->data);
    if (ret < 0)
        goto io_error;

next_reply:
    packet_destroy(reply);  /* destroy old, if any */
    reply = cimd2_get_packet(pdata, ts);
    if (!reply)
        goto io_error;

    errorcode = packet_display_error(reply,conn);

    if (reply->operation == NACK) {
        warning(0, "CIMD2[%s]: received NACK",
                octstr_get_cstr(conn->id));
        octstr_dump(reply->data, 0);
        /* Correct sequence number if server says it was wrong,
         * but only if server's number is sane. */
        if (reply->seq != request->seq && (reply->seq % 2) == 1) {
            warning(0, "CIMD2[%s]: correcting sequence number from %ld to %ld.",
                    octstr_get_cstr(conn->id),
                    (long) pdata->send_seq, 
                    (long) reply->seq);
            pdata->send_seq = reply->seq;
        }
        goto retry;
    }

    if (reply->operation == GENERAL_ERROR_RESPONSE) {
        error(0, "CIMD2[%s]: received general error response",
              octstr_get_cstr(conn->id));
        goto io_error;
    }

    /* The server sent us a request.  Handle it, then wait for
     * a new reply. */
    if (reply->operation < RESPONSE) {
        cimd2_handle_request(reply, conn);
        goto next_reply;
    }

    if (reply->seq != request->seq) {
        /* We got a response to a different request number than
         * what we send.  Strange. */
        warning(0, "CIMD2[%s]: response had unexpected sequence number; ignoring.",
                octstr_get_cstr(conn->id));
        goto next_reply;
    }

    if (reply->operation != request->operation + RESPONSE) {
        /* We got a response that didn't match our request */
        Octstr *request_name = operation_name(request->operation);
        Octstr *reply_name = operation_name(reply->operation);
        warning(0, "CIMD2[%s]: %s request got a %s",
                octstr_get_cstr(conn->id),
                octstr_get_cstr(request_name),
                octstr_get_cstr(reply_name));

        octstr_destroy(request_name);
        octstr_destroy(reply_name);
        octstr_dump(reply->data, 0);
        goto retry;
    }

    if (errorcode > 0)
        goto error;

    /* The reply passed all the checks... looks like the SMSC accepted
     * our request! */
    packet_destroy(reply);
    return 0;

io_error:
    packet_destroy(reply);
    return -2;

error:
    packet_destroy(reply);
    return -1;

retry:
    if (++tries < 3) {
        warning(0, "CIMD2[%s]: Retransmitting (take %d)", 
                octstr_get_cstr(conn->id),
                tries);
        goto retransmit;
    }
    warning(0, "CIMD2[%s]: Giving up.",
            octstr_get_cstr(conn->id));
    goto io_error;
}

/* Close the SMSC socket without fanfare. */
static void cimd2_close_socket(PrivData *pdata)
{
    gw_assert(pdata != NULL);

    if (pdata->socket < 0)
        return;

    if (close(pdata->socket) < 0)
        warning(errno, "CIMD2[%s]: error closing socket",
                octstr_get_cstr(pdata->conn->id));
    pdata->socket = -1;
}

/* Open a socket to the SMSC, send a login packet, and wait for ack.
 * This may block.  Return 0 for success, or -1 for failure. */
/* Make sure the socket is closed before calling this function, otherwise
 * we will leak fd's. */
static int cimd2_login(SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    int ret;
    struct packet *packet = NULL;

    gw_assert(pdata != NULL);

    if (pdata->socket >= 0) {
        warning(0, "CIMD2[%s]: login: socket was already open; closing",
                octstr_get_cstr(conn->id));
        cimd2_close_socket(pdata);
    }
    
    pdata->socket = tcpip_connect_to_server_with_port(
                                            octstr_get_cstr(pdata->host),
                                            pdata->port,
                                            pdata->our_port,
                                            (conn->our_host ? octstr_get_cstr(conn->our_host) : NULL)); 
    if (pdata->socket != -1) {
    
        packet = packet_create(LOGIN, BOGUS_SEQUENCE);
        packet_add_string_parm(packet, P_USER_IDENTITY, pdata->username, conn);
        packet_add_string_parm(packet, P_PASSWORD, pdata->password, conn);
        
        ret = cimd2_request(packet, conn, NULL);
        
        if (ret >= 0) {
            packet_destroy(packet);
            info(0, "CIMD2[%s] logged in.",
                 octstr_get_cstr(conn->id));
            return 0;
        }
    }
    error(0, "CIMD2[%s] login failed.",
          octstr_get_cstr(conn->id));
    cimd2_close_socket(pdata);
    packet_destroy(packet);
    return -1;
}

static void cimd2_logout(SMSCConn *conn)
{
    struct packet *packet = NULL;
    int ret;

    packet = packet_create(LOGOUT, BOGUS_SEQUENCE);

    /* TODO: Don't wait very long for a response in this case. */
    ret = cimd2_request(packet, conn, NULL);

    if (ret == 0) {
        info(0, "CIMD2[%s] logged out.",
             octstr_get_cstr(conn->id));
    }
    packet_destroy(packet);
}

static int cimd2_send_alive(SMSCConn *conn)
{
    struct packet *packet = NULL;
    int ret;

    packet = packet_create(ALIVE, BOGUS_SEQUENCE);
    ret = cimd2_request(packet, conn, NULL);
    packet_destroy(packet);

    if (ret < 0)
        warning(0, "CIMD2[%s]: SMSC not alive.",
                octstr_get_cstr(conn->id));

    return ret;
}


static void cimd2_destroy(PrivData *pdata)
{
    int discarded;

    if (pdata == NULL) 
        return;
        
    octstr_destroy(pdata->host);
    octstr_destroy(pdata->username);
    octstr_destroy(pdata->password);
    octstr_destroy(pdata->inbuffer);
    octstr_destroy(pdata->my_number);

    discarded = gwlist_len(pdata->received);
    if (discarded > 0)
        warning(0, "CIMD2[%s]: discarded %d received messages",
                octstr_get_cstr(pdata->conn->id), 
                discarded);

    gwlist_destroy(pdata->received, msg_destroy_item);
    gwlist_destroy(pdata->outgoing_queue, NULL);
    gwlist_destroy(pdata->stopped, NULL);

    gw_free(pdata);
}


static int cimd2_submit_msg(SMSCConn *conn, Msg *msg)
{
    PrivData *pdata = conn->data;
    struct packet *packet;
    Octstr *ts = NULL;
    int ret;

    gw_assert(pdata != NULL);
    debug("bb.sms.cimd2", 0, "CIMD2[%s]: sending message",
        octstr_get_cstr(conn->id));

    packet = packet_encode_message(msg, pdata->my_number,conn);
    if (!packet) {
        /* This is a protocol error. Does this help? I doubt..
         * But nevermind that.
         */
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_MALFORMED, octstr_create("MALFORMED"));
        return -1;
    }

    ret = cimd2_request(packet, conn, &ts);
    if ((ret == 0) && (ts) && DLR_IS_SUCCESS_OR_FAIL(msg->sms.dlr_mask) && !pdata->no_dlr) {
        dlr_add(conn->name, ts, msg, 1);
    }
    octstr_destroy(ts);
    packet_destroy(packet);
    
    if (ret == -1) {
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));
    }
    else if (ret == -2) {
        cimd2_close_socket(pdata);
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_TEMPORARILY, NULL);
        mutex_lock(conn->flow_mutex);
        conn->status = SMSCCONN_DISCONNECTED;
        mutex_unlock(conn->flow_mutex);
    }
    else {
        bb_smscconn_sent(conn,msg, NULL);
    }

    return ret;
}

static int cimd2_receive_msg(SMSCConn *conn, Msg **msg)
{
    PrivData *pdata = conn->data;
    long ret;
    struct packet *packet;

    gw_assert(pdata != NULL);

    if (gwlist_len(pdata->received) > 0) {
        *msg = gwlist_consume(pdata->received);
        return 1;
    }

    if (pdata->socket < 0) {
        /* XXX We have to assume that smsc_send_message is
         * currently trying to reopen, so we have to make
         * this thread wait.  It should be done in a nicer
         * way. */
        return 0;
    }
    
    ret = read_available(pdata->socket, 0);
    if (ret == 0) {
        if (pdata->keepalive > 0 && pdata->next_ping < time(NULL)) {
            if (cimd2_send_alive(conn) < 0)
		return -1;
        }
        return 0;
    }

    if (ret < 0) {
        warning(errno, "CIMD2[%s]: cimd2_receive_msg: read_available failed",
                octstr_get_cstr(conn->id));
        return -1;
    }

    /* We have some data waiting... see if it is an sms delivery. */
    ret = octstr_append_from_socket(pdata->inbuffer, pdata->socket);

    if (ret == 0) {
        warning(0, "CIMD2[%s]: cimd2_receive_msg: service center closed connection.",
                octstr_get_cstr(conn->id));
        return -1;
    }
    if (ret < 0) {
        warning(0, "CIMD2[%s]: cimd2_receive_msg: read failed",
                octstr_get_cstr(conn->id));
        return -1;
    }


    for (;;) {
        packet = packet_extract(pdata->inbuffer,conn);
        if (!packet)
            break;

        packet_check(packet,conn);
        packet_check_can_receive(packet,conn);
        debug("bb.sms.cimd2", 0, "CIMD2[%s]: received: <%s>",
              octstr_get_cstr(pdata->conn->id), 
              octstr_get_cstr(packet->data));

        if (packet->operation < RESPONSE)
            cimd2_handle_request(packet, conn);
        else {
            error(0, "CIMD2[%s]: cimd2_receive_msg: unexpected response packet",
                octstr_get_cstr(conn->id));
            octstr_dump(packet->data, 0);
        }

        packet_destroy(packet);
    }

    if (gwlist_len(pdata->received) > 0) {
	*msg = gwlist_consume(pdata->received);
        return 1;
    }
    return 0;
}



static Msg *cimd2_accept_delivery_report_message(struct packet *request,
						 SMSCConn *conn)
{
    Msg *msg = NULL;
    Octstr *destination = NULL;
    Octstr *timestamp = NULL;
    Octstr *statuscode = NULL;
    int st_code;
    int code;

    destination = packet_get_parm(request, P_DESTINATION_ADDRESS);
    timestamp = packet_get_parm(request, P_MC_TIMESTAMP);
    statuscode = packet_get_parm(request, P_STATUS_CODE);

    st_code = atoi(octstr_get_cstr(statuscode));

    switch(st_code)
    {
    case 2:  /* validity period expired */
    case 3:  /* delivery failed */
    case 6: /* last no response */
    case 7: /* message cancelled */
    case 8: /* message deleted */
    case 9: /* message deleted by cancel */
	code = DLR_FAIL;
    	break;
    case 4: /* delivery successful */
    	code = DLR_SUCCESS;
    	break;
    default:
        code = 0;
    }
    if(code)
        msg = dlr_find(conn->name, timestamp, destination, code, 1);
    else
        msg = NULL;

    /* recode the body into msgdata */
    if (msg) {
        msg->sms.msgdata = packet_get_parm(request, P_USER_DATA);
        if (!msg->sms.msgdata) {
            msg->sms.msgdata = statuscode;
            statuscode = NULL;
        }
    }

    octstr_destroy(statuscode);
    octstr_destroy(destination);
    octstr_destroy(timestamp);

    return msg;
 }

static Msg *sms_receive(SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    int ret;
    Msg *newmsg = NULL;

    ret = cimd2_receive_msg(conn, &newmsg);
    if (ret == 1) {
        /* if any smsc_id available, use it */
        newmsg->sms.smsc_id = octstr_duplicate(conn->id);
        return newmsg;
    } 
    else if (ret == 0) { /* no message, just retry... */
        return NULL;
    } 
    /* error. reconnect. */
    msg_destroy(newmsg);
    mutex_lock(conn->flow_mutex);
    cimd2_close_socket(pdata);
    conn->status = SMSCCONN_DISCONNECTED;
    mutex_unlock(conn->flow_mutex);
    return NULL;
}


static void io_thread (void *arg)
{
    Msg       *msg;
    SMSCConn  *conn = arg;
    PrivData *pdata = conn->data;
    double    sleep = 0.0001;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    /* remove messages from SMSC until we are killed */
    while (!pdata->quitting) {
    
        gwlist_consume(pdata->stopped); /* block here if suspended/isolated */
      
        /* check that connection is active */
        if (conn->status != SMSCCONN_ACTIVE) {
            if (cimd2_login(conn) != 0) { 
                error(0, "CIMD2[%s]: Couldn't connect to SMSC (retrying in %ld seconds).",
                      octstr_get_cstr(conn->id), 
                      conn->reconnect_delay);
                gwthread_sleep(conn->reconnect_delay);
                mutex_lock(conn->flow_mutex);
                conn->status = SMSCCONN_RECONNECTING; 
                mutex_unlock(conn->flow_mutex);
                continue; 
            } 
            mutex_lock(conn->flow_mutex);
            conn->status = SMSCCONN_ACTIVE;
            conn->connect_time = time(NULL);
            bb_smscconn_connected(conn);
            mutex_unlock(conn->flow_mutex);
        }

        /* receive messages */
        do { 
            msg = sms_receive(conn);
            if (msg) {
                sleep = 0;
                debug("bb.sms.cimd2", 0, "CIMD2[%s]: new message received",
                      octstr_get_cstr(conn->id));
                bb_smscconn_receive(conn, msg);
            }
        } while (msg);
 
        /* send messages */
        do {
            msg = gwlist_extract_first(pdata->outgoing_queue);
            if (msg) {
                sleep = 0;
                if (cimd2_submit_msg(conn,msg) != 0) break;
            }
        } while (msg);
 
        if (sleep > 0) {

            /* note that this implementations means that we sleep even
             * when we fail connection.. but time is very short, anyway
             */
            gwthread_sleep(sleep);
            /* gradually sleep longer and longer times until something starts to
             * happen - this of course reduces response time, but that's better than
             * extensive CPU usage when it is not used
             */
            sleep *= 2;
            if (sleep >= 2.0)
                sleep = 1.999999;
        }
        else {
            sleep = 0.0001;
        }
    }
}


static int cimd2_add_msg_cb (SMSCConn *conn, Msg *sms)
{
    PrivData *pdata = conn->data;
    Msg *copy;

    copy = msg_duplicate(sms);
    gwlist_produce(pdata->outgoing_queue, copy);
    gwthread_wakeup(pdata->io_thread);

    return 0;
}


static int cimd2_shutdown_cb (SMSCConn *conn, int finish_sending)
{
    PrivData *pdata = conn->data;
    
    debug("bb.sms", 0, "Shutting down SMSCConn CIMD2 %s (%s)",
          octstr_get_cstr(conn->id), 
          finish_sending ? "slow" : "instant");

    /* Documentation claims this would have been done by smscconn.c,
       but isn't when this code is being written. */
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    pdata->quitting = 1;     /* Separate from why_killed to avoid locking, as
                              * why_killed may be changed from outside? */

    if (finish_sending == 0) {
        Msg *msg;
        while ((msg = gwlist_extract_first(pdata->outgoing_queue)) != NULL) {
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
        }
    }

    cimd2_logout(conn);
    if (conn->is_stopped) {
        gwlist_remove_producer(pdata->stopped);
        conn->is_stopped = 0;
    }

    if (pdata->io_thread != -1) {
        gwthread_wakeup(pdata->io_thread);
        gwthread_join(pdata->io_thread);
    }

    cimd2_close_socket(pdata);
    cimd2_destroy(pdata); 
     
    debug("bb.sms", 0, "SMSCConn CIMD2 %s shut down.",  
          octstr_get_cstr(conn->id)); 
    conn->status = SMSCCONN_DEAD; 
    bb_smscconn_killed(); 
    return 0;
}    

static void cimd2_start_cb (SMSCConn *conn)
{
    PrivData *pdata = conn->data;

    gwlist_remove_producer(pdata->stopped);
    /* in case there are messages in the buffer already */
    gwthread_wakeup(pdata->io_thread);
    debug("bb.sms", 0, "SMSCConn CIMD2 %s, start called",
          octstr_get_cstr(conn->id));
}

static void cimd2_stop_cb (SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    gwlist_add_producer(pdata->stopped);
    debug("bb.sms", 0, "SMSCConn CIMD2 %s, stop called",
          octstr_get_cstr(conn->id));
}

static long cimd2_queued_cb (SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    conn->load = (pdata ? (conn->status != SMSCCONN_DEAD ? 
                  gwlist_len(pdata->outgoing_queue) : 0) : 0);
    return conn->load; 
}

int smsc_cimd2_create(SMSCConn *conn, CfgGroup *grp)
{
    PrivData *pdata;
    int ok;
    int maxlen;

    pdata = gw_malloc(sizeof(PrivData));
    conn->data = pdata;
    pdata->conn = conn;
   
    pdata->no_dlr = 0;
    pdata->quitting = 0;
    pdata->socket = -1;
    pdata->received = gwlist_create();
    pdata->inbuffer = octstr_create("");
    pdata->send_seq = 1;
    pdata->receive_seq = 0;
    pdata->outgoing_queue = gwlist_create();
    pdata->stopped = gwlist_create();
    gwlist_add_producer(pdata->outgoing_queue);

    if (conn->is_stopped)
      gwlist_add_producer(pdata->stopped);

    pdata->host = cfg_get(grp, octstr_imm("host"));
    if (cfg_get_integer(&(pdata->port), grp, octstr_imm("port")) == -1)
      pdata->port = 0;
    if (cfg_get_integer(&(pdata->our_port), grp, octstr_imm("our-port")) == -1)
      pdata->our_port = 0;
    pdata->username = cfg_get(grp, octstr_imm("smsc-username"));
    pdata->password = cfg_get(grp, octstr_imm("smsc-password"));
    pdata->my_number = cfg_get(grp, octstr_imm("my-number"));
    if (cfg_get_integer(&(pdata->keepalive), grp,octstr_imm("keepalive")) == -1)
        pdata->keepalive = 0;

    cfg_get_bool(&pdata->no_dlr, grp, octstr_imm("no-dlr"));
    
    /* Check that config is OK */
    ok = 1;
    if (pdata->host == NULL) {
        error(0,"CIMD2[%s]: Configuration file doesn't specify host",
              octstr_get_cstr(conn->id));
        ok = 0;
    }
    if (pdata->port == 0) {
        error(0,"CIMD2[%s]: Configuration file doesn't specify port",
              octstr_get_cstr(conn->id));
        ok = 0;
    }
    if (pdata->username == NULL) {
        error(0, "CIMD2[%s]: Configuration file doesn't specify username.",
              octstr_get_cstr(conn->id));
        ok = 0;
    }
    if (pdata->password == NULL) {
        error(0, "CIMD2[%s]: Configuration file doesn't specify password.",
              octstr_get_cstr(conn->id));
        ok = 0;
    }

    if (!ok) {
        cimd2_destroy(pdata);
        return -1;
    }

    conn->name = octstr_format("CIMD2:%s:%d:%s",
                     octstr_get_cstr(pdata->host),
                     pdata->port,
                     octstr_get_cstr(pdata->username));


    if (pdata->keepalive > 0) {
      debug("bb.sms.cimd2", 0, "CIMD2[%s]: Keepalive set to %ld seconds", 
            octstr_get_cstr(conn->id),
            pdata->keepalive);
        pdata->next_ping = time(NULL) + pdata->keepalive;
    }

    maxlen = parm_maxlen(P_USER_IDENTITY);
    if (octstr_len(pdata->username) > maxlen) {
        octstr_truncate(pdata->username, maxlen);
        warning(0, "CIMD2[%s]: Truncating username to %d chars", 
                octstr_get_cstr(conn->id),
                maxlen);
    }

    maxlen = parm_maxlen(P_PASSWORD);
    if (octstr_len(pdata->password) > maxlen) {
        octstr_truncate(pdata->password, maxlen);
        warning(0, "CIMD2[%s]: Truncating password to %d chars", 
                octstr_get_cstr(conn->id),
                maxlen);
    }

    pdata->io_thread = gwthread_create(io_thread, conn);

    if (pdata->io_thread == -1) {  

        error(0,"CIMD2[%s]: Couldn't start I/O thread.",
              octstr_get_cstr(conn->id));
        pdata->quitting = 1;
        gwthread_wakeup(pdata->io_thread);
        gwthread_join(pdata->io_thread);
        cimd2_destroy(pdata);
        return -1;  
    } 

    conn->send_msg = cimd2_add_msg_cb;
    conn->shutdown = cimd2_shutdown_cb;
    conn->queued = cimd2_queued_cb;
    conn->start_conn = cimd2_start_cb;
    conn->stop_conn = cimd2_stop_cb;

    return 0;
}

