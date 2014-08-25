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

/*
 * smsc.oisd.c - Driver for Sema Group SMS Center G8.1 (OIS 5.8)
 * using direct TCP/IP access interface
 *
 * Dariusz Markowicz <dm@tenbit.pl> 2002-2004
 *
 * This code is based on the CIMD2 module design.
 *
 * References:
 *
 *   [1] Sema SMSC Version G8.1 Open Interface Specification
 *       document version 5.8, 18 January 2001, Sema Telecoms.
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
    Octstr  *host;
    long    port;
    long    keepalive;
    Octstr  *my_number;
    long    validityperiod;
    int     no_dlr;

    int     socket;
    unsigned long send_seq;

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
#define RESPONSE_TIMEOUT (10 * 1000000)
#define RESULT_SUCCESS 0

enum {
    INVOKE = 0,
    RESULT = 1
};

/* Textual names for the operation codes defined by the OISD spec. */
/* If you make changes here, also change the operation table. */
enum {
    SUBMIT_SM = 0,
    STATUS_REPORT = 4,
    DELIVER_SM = 9,
    RETRIEVE_REQUEST = 11,

    /* Not a request; add to any request to make it a response */
    RESPONSE = 50
};

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
    { "Submit SM", SUBMIT_SM, 1, 0 },
    { "Status Report", STATUS_REPORT, 0, 1 },
    { "Deliver SM", DELIVER_SM, 0, 1 },
    { "Retrieve Request", RETRIEVE_REQUEST, 1, 0 },

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

/* Return true if a OISD client may send this operation */
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


/* Return true if a OISD server may send this operation */
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

/***************************************************************************
 * Packet encoding/decoding functions.  They handle packets at the octet   *
 * level, and know nothing of the network.                                 *
 ***************************************************************************/

struct packet
{
    unsigned long opref; /* operation reference */
    int operation;
    Octstr *data;        /* Encoded packet */
};

/* A reminder that packets are created without a valid sequence number */
#define BOGUS_SEQUENCE 0

static Msg *oisd_accept_delivery_report_message(struct packet *request,
                                                SMSCConn *conn);

static void packet_parse_header(struct packet *packet)
{
    packet->opref = (octstr_get_char(packet->data, 3) << 24)
                  | (octstr_get_char(packet->data, 2) << 16)
                  | (octstr_get_char(packet->data, 1) << 8)
                  | (octstr_get_char(packet->data, 0));

    packet->operation = octstr_get_char(packet->data, 5);
    if (octstr_get_char(packet->data, 4) == 1)
        packet->operation += RESPONSE;
}


/*
 * Accept an Octstr containing one packet, build a struct packet around
 * it, and return that struct.  The Octstr is stored in the struct.
 * No error checking is done here yet.
 */
static struct packet *packet_parse(Octstr *packet_data)
{
    struct packet *packet;

    packet = gw_malloc(sizeof(*packet));
    packet->data = packet_data;

    /* Fill in packet->operation and packet->opref */
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

/*
 * Find the first packet in "in", delete it from "in", and return it as
 * a struct.  Return NULL if "in" contains no packet.  Always delete
 * leading non-packet data from "in".
 */
static struct packet *packet_extract(Octstr *in, SMSCConn *conn)
{
    Octstr *packet;
    int size, i;
    static char s[4][4] = {
        { 0x01, 0x0b, 0x00, 0x00 },
        { 0x01, 0x00, 0x00, 0x00 },
        { 0x00, 0x04, 0x00, 0x00 },
        { 0x00, 0x09, 0x00, 0x00 }
    }; /* msgtype, oper, 0, 0 */
    char known_bytes[4];

    if (octstr_len(in) < 10)
        return NULL;
    octstr_get_many_chars(known_bytes, in, 4, 4);
    /* Find s, and delete everything up to it. */
    /* If packet starts with one of s, it should be good packet */
    for (i = 0; i < 4; i++) {
        if (memcmp(s[i], known_bytes, 4) == 0)
            break;
    }

    if (i >= 4) {
        error(0, "OISD[%s]: wrong packet",
              octstr_get_cstr(conn->id));
        octstr_dump(in, 0);
        return NULL;
    }

    /* Find end of packet */
    size = (octstr_get_char(in, 9) << 8) | octstr_get_char(in, 8);

    if (size + 10 > octstr_len(in))
        return NULL;

    packet = octstr_copy(in, 0, size + 10);
    octstr_delete(in, 0, size + 10);

    return packet_parse(packet);
}

static void packet_check_can_receive(struct packet *packet, SMSCConn *conn)
{
    gw_assert(packet != NULL);

    if (!operation_can_receive(packet->operation)) {
        Octstr *name = operation_name(packet->operation);
        warning(0, "OISD[%s]: SMSC sent us %s request",
                octstr_get_cstr(conn->id),
                octstr_get_cstr(name));
        octstr_destroy(name);
    }
}

static int oisd_expand_gsm7_to_bits(char *bits, Octstr *raw7)
{
    int i, j, k;
    int len;
    char ch;

    len = octstr_len(raw7) * 7; /* number of bits in the gsm 7-bit msg */

    for (j = i = 0; j < len; ++i) {
        ch = octstr_get_char(raw7, i);
        for (k = 0; k < 8; ++k) {
            bits[j++] = (char) (ch & 0x01);
            ch >>= 1;
        }
    }

    return j;
}

static char oisd_expand_gsm7_from_bits(const char *bits, int pos)
{
    int i;
    char ch;

    pos *= 7; /* septet position in bits */
    ch = '\0';
    for (i = 6; i >= 0; --i) {
        ch <<= 1;
        ch |= bits[pos + i];
    }

    return ch;
}

static Octstr *oisd_expand_gsm7(Octstr *raw7)
{
    Octstr *raw8;
    int i, len;
    char *bits;

    raw8 = octstr_create("");
    bits = gw_malloc(8 * octstr_len(raw7) + 1);

    oisd_expand_gsm7_to_bits(bits, raw7);
    len = octstr_len(raw7);

    for (i = 0; i < len; ++i) {
        octstr_append_char(raw8, oisd_expand_gsm7_from_bits(bits, i));
    }

    gw_free(bits);

    return raw8;
}

static void oisd_shrink_gsm7(Octstr *str)
{
    Octstr *result;
    int len, i;
    int numbits, value;

    result = octstr_create("");
    len = octstr_len(str);
    value = 0;
    numbits = 0;
    for (i = 0; i < len; i++) {
        value += octstr_get_char(str, i) << numbits;
        numbits += 7;
        if (numbits >= 8) {
            octstr_append_char(result, value & 0xff);
            value >>= 8;
            numbits -= 8;
        }
    }
    if (numbits > 0)
        octstr_append_char(result, value);
    octstr_delete(str, 0, LONG_MAX);
    octstr_append(str, result);
    octstr_destroy(result);
}

/****************************************************************************
 * Packet encoding functions.  They do not allow the creation of invalid
 * OISD packets.
 ***************************************************************************/

/* Build a new packet struct with this operation code and sequence number. */
static struct packet *packet_create(int operation, unsigned long opref)
{
    struct packet *packet;
    unsigned char header[10];

    packet = gw_malloc(sizeof(*packet));
    packet->operation = operation;
    packet->opref = opref;

    /* Opref */
    header[0] = opref & 0xff;
    header[1] = (opref >> 8) & 0xff;
    header[2] = (opref >> 16) & 0xff;
    header[3] = (opref >> 24) & 0xff;

    /* Message Type & Operation */
    if (operation > RESPONSE) {
        header[4] = RESULT;
        header[5] = operation - RESPONSE;
    } else {
        header[4] = INVOKE;
        header[5] = operation;
    }

    /* Unused */
    header[6] = 0;
    header[7] = 0;

    /* Data Size */
    header[8] = 0;
    header[9] = 0;

    packet->data = octstr_create_from_data((char *)header, 10);

    return packet;
}

static void packet_set_data_size(struct packet *packet)
{
    int len;

    gw_assert(packet != NULL);

    len = octstr_len(packet->data) - 10;

    octstr_set_char(packet->data, 8, len & 0xff); /* Data Size */
    octstr_set_char(packet->data, 9, (len >> 8) & 0xff);
}

static void packet_set_sequence(struct packet *packet, unsigned long opref)
{
    gw_assert(packet != NULL);

    octstr_set_char(packet->data, 0, opref & 0xff);
    octstr_set_char(packet->data, 1, (opref >> 8) & 0xff);
    octstr_set_char(packet->data, 2, (opref >> 16) & 0xff);
    octstr_set_char(packet->data, 3, (opref >> 24) & 0xff);
    packet->opref = opref;
}

static struct packet *packet_encode_message(Msg *msg, SMSCConn *conn)
{
    struct packet *packet;
    PrivData *pdata = conn->data;
    int DCS;
    int setvalidity = SMS_PARAM_UNDEFINED;
    int so = 0;
    int udhlen7, udhlen8;
    int msglen7, msglen8;
    Octstr *udhdata = NULL;
    Octstr *msgdata = NULL;

    gw_assert(msg != NULL);
    gw_assert(msg->type == sms);
    gw_assert(msg->sms.receiver != NULL);

    DCS = fields_to_dcs(msg, 0);
    if (msg->sms.sender == NULL)
        msg->sms.sender = octstr_create("");

    if (!parm_valid_address(msg->sms.receiver)) {
        warning(0, "OISD[%s]: non-digits in destination phone number '%s', discarded",
                octstr_get_cstr(conn->id),
                octstr_get_cstr(msg->sms.receiver));
        return NULL;
    }

    if (!parm_valid_address(msg->sms.sender)) {
        warning(0, "OISD[%s]: non-digits in originating phone number '%s', discarded",
                octstr_get_cstr(conn->id),
                octstr_get_cstr(msg->sms.sender));
        return NULL;
    }

    packet = packet_create(SUBMIT_SM, BOGUS_SEQUENCE);

    gw_assert(octstr_check_range(msg->sms.receiver, 0,
                                 octstr_len(msg->sms.receiver), isphonedigit));
    /* MSISDN length */
    octstr_append_char(packet->data,
                       (unsigned char) octstr_len(msg->sms.receiver));

    /* MSISDN */
    octstr_append(packet->data, msg->sms.receiver);

    /* Duplicate msg. behaviour */
    /* 1=reject duplicates, 2=allow duplicates */
    octstr_append_char(packet->data, 2);

    /* SME ref. no. unused in this protocol implementation, but set */
    octstr_append_char(packet->data, 0);
    octstr_append_char(packet->data, 0);
    octstr_append_char(packet->data, 0);
    octstr_append_char(packet->data, 0);

    /* Priority 0=high, 1=normal */
    octstr_append_char(packet->data, 1);
    gw_assert(octstr_check_range(msg->sms.sender, 0,
                                 octstr_len(msg->sms.sender), isphonedigit));

    /* Originating address length */
    octstr_append_char(packet->data,
                       (unsigned char) (octstr_len(msg->sms.sender) + 2));

    /* XXX: GSM operator dependent ? */
    /* TON */
    octstr_append_char(packet->data, 0x42);

    /* NPI */
    octstr_append_char(packet->data, 0x44);

    /* Originating address */
    octstr_append(packet->data, msg->sms.sender);

    /* Validity period type 0=none, 1=absolute, 2=relative */

    /*
     * Validity-Period (TP-VP)
     * see GSM 03.40 section 9.2.3.12
     */
    if (msg->sms.validity != SMS_PARAM_UNDEFINED)
    	setvalidity = (msg->sms.validity - time(NULL)) / 60;
    else if (setvalidity != SMS_PARAM_UNDEFINED)
    	setvalidity = pdata->validityperiod;
    if (setvalidity != SMS_PARAM_UNDEFINED) {
        /* Validity period type 0=none, 1=absolute, 2=relative */
        octstr_append_char(packet->data, 2);

        if (setvalidity > 635040)
            setvalidity = 255;
        else if (setvalidity >= 50400 && setvalidity <= 635040)
            setvalidity = (setvalidity - 1) / 7 / 24 / 60 + 192 + 1;
        else if (setvalidity > 43200 && setvalidity < 50400)
            setvalidity = 197;
        else if (setvalidity >= 2880 && setvalidity <= 43200)
            setvalidity = (setvalidity - 1) / 24 / 60 + 166 + 1;
        else if (setvalidity > 1440 && setvalidity < 2880)
            setvalidity = 168;
        else if (setvalidity >= 750 && setvalidity <= 1440)
            setvalidity = (setvalidity - 720 - 1) / 30 + 143 + 1;
        else if (setvalidity > 720 && setvalidity < 750)
            setvalidity = 144;
        else if (setvalidity >= 5 && setvalidity <= 720)
            setvalidity = (setvalidity - 1) / 5 - 1 + 1;
        else if (setvalidity < 5)
            setvalidity = 0;

        octstr_append_char(packet->data, setvalidity);
    } else {
        /* Validity period type 0=none, 1=absolute, 2=relative */
        octstr_append_char(packet->data, 0);
        setvalidity = 0; /* reset */
    }

    if (setvalidity >= 0 && setvalidity <= 143)
        debug("bb.smsc.oisd", 0, "OISD[%s]: Validity-Period: %d minutes",
              octstr_get_cstr(conn->id), (setvalidity + 1)*5);
    else if (setvalidity >= 144 && setvalidity <= 167)
        debug("bb.smsc.oisd", 0, "OISD[%s]: Validity-Period: %3.1f hours",
              octstr_get_cstr(conn->id), ((float)(setvalidity - 143) / 2) + 12);
    else if (setvalidity >= 168 && setvalidity <= 196)
        debug("bb.smsc.oisd", 0, "OISD[%s]: Validity-Period: %d days",
              octstr_get_cstr(conn->id), (setvalidity - 166));
    else
        debug("bb.smsc.oisd", 0, "OISD[%s]: Validity-Period: %d weeks",
              octstr_get_cstr(conn->id), (setvalidity - 192));

    /* Data coding scheme */
    octstr_append_char(packet->data, DCS);

    /* Explicitly ask not to get status reports.
     * If we do not do this, the server's default might be to
     * send status reports in some cases, and we don't do anything
     * with those reports anyway. */
    /* ask for the delivery reports if needed*/

    if (!pdata->no_dlr)
        if (DLR_IS_SUCCESS_OR_FAIL(msg->sms.dlr_mask))
            octstr_append_char(packet->data, 7);
        else
            octstr_append_char(packet->data, 0);
    else if (pdata->no_dlr && DLR_IS_SUCCESS_OR_FAIL(msg->sms.dlr_mask))
        warning(0, "OISD[%s]: dlr request make no sense while no-dlr set to true",
             octstr_get_cstr(conn->id));

    /* Protocol id 0=default */
    octstr_append_char(packet->data, 0);

    if (octstr_len(msg->sms.udhdata))
        so |= 0x02;
    if (msg->sms.coding == DC_8BIT)
        so |= 0x10;

    /* Submission options */
    octstr_append_char(packet->data, so);

    udhlen8 = octstr_len(msg->sms.udhdata);
    msglen8 = octstr_len(msg->sms.msgdata);

    udhdata = octstr_duplicate(msg->sms.udhdata);
    msgdata = octstr_duplicate(msg->sms.msgdata);

    if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) {
        debug("bb.sms.oisd", 0, "OISD[%s]: sending UTF-8=%s",
              octstr_get_cstr(conn->id),
              octstr_get_cstr(msg->sms.msgdata));
        charset_utf8_to_gsm(msgdata);
        oisd_shrink_gsm7(msgdata);
    }

    /* calculate lengths */
    udhlen7 = octstr_len(udhdata);
    msglen7 = octstr_len(msgdata);

    octstr_append_char(packet->data, (unsigned char) (udhlen8 + msglen8));
    octstr_append_char(packet->data, (unsigned char) (udhlen7 + msglen7));

    /*
     * debug("bb.sms.oisd", 0, "OISD[%s]: packet_encode_message udhlen8=%d, msglen8=%d",
     *       octstr_get_cstr(conn->id), udhlen8, msglen8);
     * debug("bb.sms.oisd", 0, "OISD[%s]: packet_encode_message udhlen7=%d, msglen7=%d",
     *       octstr_get_cstr(conn->id), udhlen7, msglen7);
     */

    /* copy text */
    octstr_append(packet->data, udhdata);
    octstr_append(packet->data, msgdata);

    /* Sub-logical SME number */
    octstr_append_char(packet->data, 0);
    octstr_append_char(packet->data, 0);

    octstr_destroy(udhdata);
    octstr_destroy(msgdata);

    return packet;
}

/***************************************************************************
 * Protocol functions.  These implement various transactions.              *
 ***************************************************************************/

/* Give this packet a proper sequence number for sending. */
static void packet_set_send_sequence(struct packet *packet, PrivData *pdata)
{
    gw_assert(pdata != NULL);

    packet_set_sequence(packet, pdata->send_seq);
    pdata->send_seq++;
}

static struct packet *oisd_get_packet(PrivData *pdata, Octstr **ts)
{
    struct packet *packet = NULL;

    gw_assert(pdata != NULL);

    /* If packet is already available, don't try to read anything */
    packet = packet_extract(pdata->inbuffer, pdata->conn);

    while (packet == NULL) {
        if (read_available(pdata->socket, RESPONSE_TIMEOUT) != 1) {
            warning(0, "OISD[%s]: SMSC is not responding",
                    octstr_get_cstr(pdata->conn->id));
            return NULL;
        }

        if (octstr_append_from_socket(pdata->inbuffer, pdata->socket) <= 0) {
            error(0, "OISD[%s]: oisd_get_packet: read failed",
                  octstr_get_cstr(pdata->conn->id));
            return NULL;
        }

        packet = packet_extract(pdata->inbuffer, pdata->conn);
    }

    packet_check_can_receive(packet, pdata->conn);
    debug("bb.sms.oisd", 0, "OISD[%s]: received",
          octstr_get_cstr(pdata->conn->id));
    if (packet->operation != RETRIEVE_REQUEST + RESPONSE)
        octstr_dump(packet->data, 0);
    if (ts)
        *ts = octstr_copy(packet->data, 15, 14);

    if (pdata->keepalive > 0)
        pdata->next_ping = time(NULL) + pdata->keepalive;

    return packet;
}

/*
 * Acknowledge a request.
 */
static void oisd_send_response(struct packet *request, PrivData *pdata)
{
    struct packet *response;

    gw_assert(request != NULL);
    gw_assert(request->operation < RESPONSE);

    response = packet_create(request->operation + RESPONSE, request->opref);

    octstr_append_char(response->data, (char) RESULT_SUCCESS);

    packet_set_data_size(response);

    debug("bb.sms.oisd", 0, "OISD[%s]: sending response",
          octstr_get_cstr(pdata->conn->id));
    octstr_dump(response->data, 0);

    /* Don't check errors here because if there is something
     * wrong with the socket, the main loop will detect it. */
    octstr_write_to_socket(pdata->socket, response->data);

    packet_destroy(response);
}

static Msg *oisd_accept_message(struct packet *request, SMSCConn *conn)
{
    Msg *msg = NULL;
    int DCS;
    int dest_len;
    int origin_len;
    int add_info;
    int msglen7, msglen8;
    int udh_len;

    msg = msg_create(sms);

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

    /* Additional information
     *   xxxxxxyz  This field conveys additional information to assist
     *             the recipient in interpreting the SM.
     *                   z = reply path
     *                   y = user data header indicator
     *                   x = reserved
     */

    /*
     * Destination addr. and Originating addr. w/o TOA
     */

    /* Destination addr. length */
    dest_len = octstr_get_char(request->data, 10);

    /* Destination addr. */
    msg->sms.receiver = octstr_copy(request->data, 11+2, dest_len-2);

    /* Originating addr. length */
    origin_len = octstr_get_char(request->data, 11+dest_len+4);

    /* Originating addr. */
    msg->sms.sender = octstr_copy(request->data, 11+dest_len+5+2, origin_len-2);

    DCS = octstr_get_char(request->data, 11+dest_len+5+origin_len);
    if (!dcs_to_fields(&msg, DCS)) {
        /* XXX: Should reject this message ? */
        debug("bb.sms.oisd", 0, "OISD[%s]: Invalid DCS",
              octstr_get_cstr(conn->id));
        dcs_to_fields(&msg, 0);
    }

    add_info = octstr_get_char(request->data,11+dest_len+5+origin_len+2);

    msglen7 = octstr_get_char(request->data, 11+dest_len+5+origin_len+3);
    msglen8 = octstr_get_char(request->data, 11+dest_len+5+origin_len+4);

    msg->sms.rpi = add_info & 0x01;

    debug("bb.sms.oisd", 0,
          "OISD[%s]: received DCS=%02X, add_info=%d, msglen7=%d, msglen8=%d, rpi=%ld",
          octstr_get_cstr(conn->id),
          DCS, add_info, msglen7, msglen8, msg->sms.rpi);

    if (msg->sms.coding == DC_7BIT) {
        msg->sms.msgdata =
            oisd_expand_gsm7(octstr_copy(request->data,
                                         11+dest_len+5+origin_len+5,
                                         msglen7));
            debug("bb.sms.oisd", 0, "OISD[%s]: received raw8=%s ",
                  octstr_get_cstr(conn->id),
                  octstr_get_cstr(msg->sms.msgdata));
        if (add_info & 0x02) {
            warning(0, "OISD[%s]: 7-bit UDH ?",
                    octstr_get_cstr(conn->id));
        } else {
            charset_gsm_to_utf8(msg->sms.msgdata);
            debug("bb.sms.oisd", 0, "OISD[%s]: received UTF-8=%s",
                  octstr_get_cstr(conn->id),
                  octstr_get_cstr(msg->sms.msgdata));
        }
    } else {
        /* 0xf4, 0xf5, 0xf6, 0xf7; 8bit to disp, mem, sim or term */
        if (add_info & 0x02) {
            udh_len = octstr_get_char(request->data,
                                      11+dest_len+5+origin_len+5)+1;
            msg->sms.msgdata =
                octstr_copy(request->data,
                            11+dest_len+5+origin_len+5+udh_len,
                            msglen8);
            msg->sms.udhdata =
                octstr_copy(request->data,
                            11+dest_len+5+origin_len+5,
                            udh_len);
        } else {
            msg->sms.msgdata =
                octstr_copy(request->data,
                            11+dest_len+5+origin_len+5,
                            msglen8);
        }
    }

    /* Code elsewhere in the gateway always expects the sender and
     * receiver fields to be filled, so we discard messages that
     * lack them.  If they should not be discarded, then the code
     * handling sms messages should be reviewed.  -- RB */
    if (!(msg->sms.receiver) || octstr_len(msg->sms.receiver) == 0) {
        info(0, "OISD[%s]: Got SMS without receiver, discarding.",
             octstr_get_cstr(conn->id));
        goto error;
    }

    if (!(msg->sms.sender) || octstr_len(msg->sms.sender) == 0) {
        info(0, "OISD[%s]: Got SMS without sender, discarding.",
              octstr_get_cstr(conn->id));
        goto error;
    }

    if ((!(msg->sms.msgdata) || octstr_len(msg->sms.msgdata) == 0)
        && (!(msg->sms.udhdata) || octstr_len(msg->sms.udhdata) == 0)) {
        msg->sms.msgdata = octstr_create("");
    }

    return msg;

error:
    msg_destroy(msg);
    return NULL;
}

/* Deal with a request from the OISD server, and acknowledge it. */
static void oisd_handle_request(struct packet *request, SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    Msg *msg = NULL;

    if (request->operation == STATUS_REPORT) {
        msg = oisd_accept_delivery_report_message(request, conn);
        if (msg)
            gwlist_append(pdata->received, msg);
    } else if (request->operation == DELIVER_SM) {
        msg = oisd_accept_message(request, conn);
        if (msg)
            gwlist_append(pdata->received, msg);
    }

    oisd_send_response(request, pdata);
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
 */
static int oisd_request(struct packet *request, SMSCConn *conn, Octstr **ts)
{
    PrivData *pdata = conn->data;
    int ret;
    struct packet *reply = NULL;
    int errorcode;
    int tries = 0;
    Octstr *request_name;

    gw_assert(pdata != NULL);
    gw_assert(request != NULL);
    gw_assert(operation_can_send(request->operation));

    if (pdata->socket < 0) {
        warning(0, "OISD[%s]: oisd_request: socket not open.",
                octstr_get_cstr(conn->id));
        return -2;
    }

    packet_set_data_size(request);

retransmit:
    packet_set_send_sequence(request, pdata);

    request_name = operation_name(request->operation);
    debug("bb.sms.oisd", 0, "OISD[%s]: sending %s request",
          octstr_get_cstr(conn->id),
          octstr_get_cstr(request_name));
    octstr_destroy(request_name);
    if (request->operation != RETRIEVE_REQUEST)
        octstr_dump(request->data, 0);

    ret = octstr_write_to_socket(pdata->socket, request->data);
    if (ret < 0)
        goto io_error;

next_reply:
    packet_destroy(reply);  /* destroy old, if any */
    reply = oisd_get_packet(pdata, ts);
    if (!reply)
        goto io_error;

    /* The server sent us a request.  Handle it, then wait for
     * a new reply. */
    if (reply->operation < RESPONSE) {
        oisd_handle_request(reply, conn);
        goto next_reply;
    }

    if (reply->opref != request->opref) {
        /* We got a response to a different request number than
         * what we send.  Strange. */
        warning(0, "OISD[%s]: response had unexpected sequence number; ignoring.",
                octstr_get_cstr(conn->id));
        goto next_reply;
    }

    if (reply->operation != request->operation + RESPONSE) {
        /* We got a response that didn't match our request */
        Octstr *request_name = operation_name(request->operation);
        Octstr *reply_name = operation_name(reply->operation);
        warning(0, "OISD[%s]: %s request got a %s",
                octstr_get_cstr(conn->id),
                octstr_get_cstr(request_name),
                octstr_get_cstr(reply_name));

        octstr_destroy(request_name);
        octstr_destroy(reply_name);
        octstr_dump(reply->data, 0);
        goto retry;
    }

    errorcode = octstr_get_char(reply->data, 10); /* Result */

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
        warning(0, "OISD[%s]: Retransmitting (take %d)",
                octstr_get_cstr(conn->id),
                tries);
        goto retransmit;
    }
    warning(0, "OISD[%s]: Giving up.",
            octstr_get_cstr(conn->id));
    goto io_error;
}

/* Close the SMSC socket without fanfare. */
static void oisd_close_socket(PrivData *pdata)
{
    gw_assert(pdata != NULL);

    if (pdata->socket < 0)
        return;

    if (close(pdata->socket) < 0)
        warning(errno, "OISD[%s]: error closing socket",
                octstr_get_cstr(pdata->conn->id));
    pdata->socket = -1;
}

/*
 * Open a socket to the SMSC, send a login packet, and wait for ack.
 * This may block.  Return 0 for success, or -1 for failure.
 * Make sure the socket is closed before calling this function, otherwise
 * we will leak fd's.
 */
static int oisd_login(SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    struct packet *packet = NULL;

    gw_assert(pdata != NULL);

    if (pdata->socket >= 0) {
        warning(0, "OISD[%s]: login: socket was already open; closing",
                octstr_get_cstr(conn->id));
        oisd_close_socket(pdata);
    }

    pdata->socket = tcpip_connect_to_server(
                        octstr_get_cstr(pdata->host),
                        pdata->port,
                        (conn->our_host ? octstr_get_cstr(conn->our_host) : NULL));
    if (pdata->socket != -1) {
        info(0, "OISD[%s] logged in.",
             octstr_get_cstr(conn->id));
        return 0;
    }
    error(0, "OISD[%s] login failed.",
          octstr_get_cstr(conn->id));
    oisd_close_socket(pdata);
    packet_destroy(packet);
    return -1;
}

static int oisd_send_delivery_request(SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    struct packet *packet = NULL;
    int ret;

    gw_assert(conn != NULL);

    packet = packet_create(RETRIEVE_REQUEST, BOGUS_SEQUENCE);

    gw_assert(octstr_check_range(pdata->my_number, 0,
                                 octstr_len(pdata->my_number),
                                 isphonedigit));
    /* Originating address length */
    octstr_append_char(packet->data,
                       (char) (octstr_len(pdata->my_number) + 2));
    /* TON */
    octstr_append_char(packet->data, 0x42);
    /* NPI */
    octstr_append_char(packet->data, 0x44);
    /* Originating address */
    octstr_append(packet->data, pdata->my_number);
    /* Receive ready flag */
    octstr_append_char(packet->data, 1);
    /* Retrieve order */
    octstr_append_char(packet->data, 0);

    ret = oisd_request(packet, conn, NULL);
    packet_destroy(packet);

    if (ret < 0)
        warning(0, "OISD[%s]: Sending delivery request failed.\n",
                octstr_get_cstr(conn->id));

    return ret;
}

static void oisd_destroy(PrivData *pdata)
{
    int discarded;

    if (pdata == NULL)
        return;

    octstr_destroy(pdata->host);
    octstr_destroy(pdata->inbuffer);
    octstr_destroy(pdata->my_number);

    discarded = gwlist_len(pdata->received);
    if (discarded > 0)
        warning(0, "OISD[%s]: discarded %d received messages",
                octstr_get_cstr(pdata->conn->id),
                discarded);

    gwlist_destroy(pdata->received, msg_destroy_item);
    gwlist_destroy(pdata->outgoing_queue, NULL);
    gwlist_destroy(pdata->stopped, NULL);

    gw_free(pdata);
}

static int oisd_submit_msg(SMSCConn *conn, Msg *msg)
{
    PrivData *pdata = conn->data;
    struct packet *packet;
    Octstr *ts = NULL;
    int ret;

    gw_assert(pdata != NULL);
    debug("bb.sms.oisd", 0, "OISD[%s]: sending message",
          octstr_get_cstr(conn->id));

    packet = packet_encode_message(msg, conn);
    if (!packet) {
        /* This is a protocol error. Does this help? I doubt..
         * But nevermind that.
         */
        bb_smscconn_send_failed(conn, msg,
                SMSCCONN_FAILED_MALFORMED, octstr_create("MALFORMED"));
        return -1;
    }

    ret = oisd_request(packet, conn, &ts);
    if((ret == 0) && (ts) && DLR_IS_SUCCESS_OR_FAIL(msg->sms.dlr_mask) && !pdata->no_dlr) {
        dlr_add(conn->name, ts, msg, 0);
    }
    octstr_destroy(ts);
    packet_destroy(packet);

    if (ret == -1) {
        bb_smscconn_send_failed(conn, msg,
                SMSCCONN_FAILED_REJECTED, octstr_create("REJECTED"));
    }
    else if (ret == -2) {
        oisd_close_socket(pdata);
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_TEMPORARILY, NULL);
        mutex_lock(conn->flow_mutex);
        conn->status = SMSCCONN_DISCONNECTED;
        mutex_unlock(conn->flow_mutex);
    }
    else {
        bb_smscconn_sent(conn, msg, NULL);
    }

    return ret;
}

static int oisd_receive_msg(SMSCConn *conn, Msg **msg)
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
            if (oisd_send_delivery_request(conn) < 0)
                return -1;
        }
        return 0;
    }

    if (ret < 0) {
        warning(errno, "OISD[%s]: oisd_receive_msg: read_available failed",
                octstr_get_cstr(conn->id));
        return -1;
    }

    /* We have some data waiting... see if it is an sms delivery. */
    ret = octstr_append_from_socket(pdata->inbuffer, pdata->socket);

    if (ret == 0) {
        warning(0, "OISD[%s]: oisd_receive_msg: service center closed connection.",
                octstr_get_cstr(conn->id));
        return -1;
    }
    if (ret < 0) {
        warning(0, "OISD[%s]: oisd_receive_msg: read failed",
                octstr_get_cstr(conn->id));
        return -1;
    }


    for (;;) {
        packet = packet_extract(pdata->inbuffer, conn);
        if (!packet)
            break;

        packet_check_can_receive(packet, conn);
        debug("bb.sms.oisd", 0, "OISD[%s]: received",
              octstr_get_cstr(pdata->conn->id));
        octstr_dump(packet->data, 0);

        if (packet->operation < RESPONSE)
            oisd_handle_request(packet, conn);
        else {
            error(0, "OISD[%s]: oisd_receive_msg: unexpected response packet",
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

static Msg *oisd_accept_delivery_report_message(struct packet *request,
                                                SMSCConn *conn)
{
    Msg *msg = NULL;
    Octstr *destination = NULL;
    Octstr *timestamp = NULL;
    int st_code;
    int code;
    int dest_len;

    /* MSISDN length */
    dest_len = octstr_get_char(request->data, 10);
    /* MSISDN */
    destination = octstr_copy(request->data, 10+1, dest_len);
    /* Accept time */
    timestamp = octstr_copy(request->data, 10+1+dest_len+1+4+4, 14);
    /* SM status */
    st_code = octstr_get_char(request->data, 10+1+dest_len+1+4+4+14);

    switch (st_code) {
    case 1:
    case 2:
        code = DLR_FAIL;
        break;
    case 3:   /* success */
        code = DLR_SUCCESS;
        break;
    case 4:
    case 5:
    case 6:
    default:
        code = 0;
    }

    if (code)
        msg = dlr_find(conn->name, timestamp, destination, code, 0);

    octstr_destroy(destination);
    octstr_destroy(timestamp);

    return msg;
}

static Msg *sms_receive(SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    int ret;
    Msg *newmsg = NULL;

    ret = oisd_receive_msg(conn, &newmsg);
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
    oisd_close_socket(pdata);
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
            if (oisd_login(conn) != 0) {
                error(0, "OISD[%s]: Couldn't connect to SMSC (retrying in %ld seconds).",
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
                debug("bb.sms.oisd", 0, "OISD[%s]: new message received",
                      octstr_get_cstr(conn->id));
                bb_smscconn_receive(conn, msg);
            }
        } while (msg);

        /* send messages */
        do {
            msg = gwlist_extract_first(pdata->outgoing_queue);
            if (msg) {
                sleep = 0;
                if (oisd_submit_msg(conn, msg) != 0) break;
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

static int oisd_add_msg_cb (SMSCConn *conn, Msg *sms)
{
    PrivData *pdata = conn->data;
    Msg *copy;

    copy = msg_duplicate(sms);
    gwlist_produce(pdata->outgoing_queue, copy);
    gwthread_wakeup(pdata->io_thread);

    return 0;
}

static int oisd_shutdown_cb (SMSCConn *conn, int finish_sending)
{
    PrivData *pdata = conn->data;

    debug("bb.sms", 0, "Shutting down SMSCConn OISD %s (%s)",
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

    if (conn->is_stopped) {
        gwlist_remove_producer(pdata->stopped);
        conn->is_stopped = 0;
    }

    if (pdata->io_thread != -1) {
        gwthread_wakeup(pdata->io_thread);
        gwthread_join(pdata->io_thread);
    }

    oisd_close_socket(pdata);
    oisd_destroy(pdata);

    debug("bb.sms", 0, "SMSCConn OISD %s shut down.",
          octstr_get_cstr(conn->id));
    conn->status = SMSCCONN_DEAD;
    bb_smscconn_killed();
    return 0;
}

static void oisd_start_cb (SMSCConn *conn)
{
    PrivData *pdata = conn->data;

    gwlist_remove_producer(pdata->stopped);
    /* in case there are messages in the buffer already */
    gwthread_wakeup(pdata->io_thread);
    debug("bb.sms", 0, "SMSCConn OISD %s, start called",
          octstr_get_cstr(conn->id));
}

static void oisd_stop_cb (SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    gwlist_add_producer(pdata->stopped);
    debug("bb.sms", 0, "SMSCConn OISD %s, stop called",
          octstr_get_cstr(conn->id));
}

static long oisd_queued_cb (SMSCConn *conn)
{
    PrivData *pdata = conn->data;
    conn->load = (pdata ? (conn->status != SMSCCONN_DEAD ?
                  gwlist_len(pdata->outgoing_queue) : 0) : 0);
    return conn->load;
}

int smsc_oisd_create(SMSCConn *conn, CfgGroup *grp)
{
    PrivData *pdata;
    int ok;

    pdata = gw_malloc(sizeof(PrivData));
    conn->data = pdata;
    pdata->conn = conn;

    pdata->no_dlr = 0;
    pdata->quitting = 0;
    pdata->socket = -1;
    pdata->received = gwlist_create();
    pdata->inbuffer = octstr_create("");
    pdata->send_seq = 1;
    pdata->outgoing_queue = gwlist_create();
    pdata->stopped = gwlist_create();
    gwlist_add_producer(pdata->outgoing_queue);

    if (conn->is_stopped)
        gwlist_add_producer(pdata->stopped);

    pdata->host = cfg_get(grp, octstr_imm("host"));
    if (cfg_get_integer(&(pdata->port), grp, octstr_imm("port")) == -1)
        pdata->port = 0;
    pdata->my_number = cfg_get(grp, octstr_imm("my-number"));
    if (cfg_get_integer(&(pdata->keepalive), grp, octstr_imm("keepalive")) == -1)
        pdata->keepalive = 0;
    if (cfg_get_integer(&(pdata->validityperiod), grp, octstr_imm("validityperiod")) == -1)
        pdata->validityperiod = SMS_PARAM_UNDEFINED;

    cfg_get_bool(&pdata->no_dlr, grp, octstr_imm("no-dlr"));

    /* Check that config is OK */
    ok = 1;
    if (pdata->host == NULL) {
        error(0, "OISD[%s]: Configuration file doesn't specify host",
              octstr_get_cstr(conn->id));
        ok = 0;
    }
    if (pdata->port == 0) {
        error(0, "OISD[%s]: Configuration file doesn't specify port",
              octstr_get_cstr(conn->id));
        ok = 0;
    }
    if (pdata->my_number == NULL && pdata->keepalive > 0) {
        error(0, "OISD[%s]: Configuration file doesn't specify my-number.",
              octstr_get_cstr(conn->id));
        ok = 0;
    }

    if (!ok) {
        oisd_destroy(pdata);
        return -1;
    }

    conn->name = octstr_format("OISD:%s:%d",
                     octstr_get_cstr(pdata->host),
                     pdata->port);


    if (pdata->keepalive > 0) {
        debug("bb.sms.oisd", 0, "OISD[%s]: Keepalive set to %ld seconds",
              octstr_get_cstr(conn->id),
              pdata->keepalive);
        pdata->next_ping = time(NULL) + pdata->keepalive;
    }

    if (pdata->validityperiod > 0) {
        debug("bb.sms.oisd", 0, "OISD[%s]: Validity-Period set to %ld",
              octstr_get_cstr(conn->id),
              pdata->validityperiod);
    }

    pdata->io_thread = gwthread_create(io_thread, conn);

    if (pdata->io_thread == -1) {

        error(0, "OISD[%s]: Couldn't start I/O thread.",
              octstr_get_cstr(conn->id));
        pdata->quitting = 1;
        gwthread_wakeup(pdata->io_thread);
        gwthread_join(pdata->io_thread);
        oisd_destroy(pdata);
        return -1;
    }

    conn->send_msg = oisd_add_msg_cb;
    conn->shutdown = oisd_shutdown_cb;
    conn->queued = oisd_queued_cb;
    conn->start_conn = oisd_start_cb;
    conn->stop_conn = oisd_stop_cb;

    return 0;
}

