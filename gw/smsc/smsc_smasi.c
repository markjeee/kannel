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
 * Implementation of a SM/ASI SMSC module.
 *
 * Stipe Tolj <stolj@wapme.de>
 *
 * This module connects to a CriticalPath InVoke SMS Center which
 * uses the SM/ASI protocol. 
 * The module is heavily based on the SMPP module design.
 *
 * TODO:
 * 1. alt_dcs is not used. Instead, msg->sms.mclass is used as the SMASI
 *    Class.
 * 2. Numbers are not handled correctly, I guess. SMASI allows only(?)
 *    international numbers without leading double zero. How to ensure
 *    this?
 * 3. Handling of npi and ton correct?
 * 4. SubmitMulti PDUs not supported.
 * 5. Replace PDUs not supported.
 * 6. Status PDUs not supported.
 * 7. Cancel PDUs not supported.
 * 8. UserRes PDUs not supported.
 * 9. Smsc PDUs not supported.
 * 10. EnquireLink PDUs not supported.
 */

#include "gwlib/gwlib.h"
#include "msg.h"
#include "smsc_p.h"
#include "smasi_pdu.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "sms.h"
#include "dlr.h"

#define DEBUG 1

#ifndef DEBUG
static void dump_pdu(const char *msg, Octstr *id, SMASI_PDU *pdu) { }
#else
static void dump_pdu(const char *msg, Octstr *id, SMASI_PDU *pdu) 
{
    debug("bb.sms.smasi", 0, "SMASI[%s]: %s", octstr_get_cstr(id), msg);
    smasi_pdu_dump(pdu);
}
#endif


/************************************************************************/
/* DEFAULT SETTINGS                                                     */
/************************************************************************/

#define SMASI_DEFAULT_PORT          21500
#define SMASI_DEFAULT_PRIORITY      0
#define MAX_PENDING_SUBMITS         10
#define SMASI_THROTTLING_SLEEP_TIME 15
#define SMASI_ENQUIRE_LINK_INTERVAL  30.0 


/************************************************************************/
/* OVERRIDE SETTINGS                                                    */
/************************************************************************/

/* Set these to -1 if no override desired. Values carried in message will
 * be used then. Or the defaults - if message has no values.
 * 
 * Otherwise these values will be forced!
 */

#define SMASI_OVERRIDE_SOURCE_TON    1
#define SMASI_OVERRIDE_SOURCE_NPI    -1
#define SMASI_OVERRIDE_DEST_TON      -1
#define SMASI_OVERRIDE_DEST_NPI      -1


/************************************************************************/
/* SMASI STRUCTURE AND RELATED FUNCTIONS                                */
/************************************************************************/

typedef struct {
    SMSCConn * conn;                 /* connection to the bearerbox */
    int thread_handle;               /* handle for the SMASI thread */
    List *msgs_to_send;
    Dict *sent_msgs;                 /* hash table for send, but yet not confirmed */
    List *received_msgs;             /* list of received, but yet not processed */
    Counter *message_id_counter;     /* sequence number */
    Octstr *host;                    /* host or IP of the SMASI server */
    long port;                       /* port to connect to */
    Octstr *username;     
    Octstr * password;
    Octstr * my_number;
    long source_addr_ton;
    long source_addr_npi;
    long dest_addr_ton;
    long dest_addr_npi;
    long priority;
    time_t throttling_err_time;
    int quitting;
    long enquire_link_interval;
    int logged_off;
} SMASI;


static SMASI *smasi_create(SMSCConn *conn) 
{

    SMASI *smasi = gw_malloc(sizeof(SMASI));

    smasi->conn = conn;

    smasi->thread_handle = -1;
    smasi->msgs_to_send = gwlist_create();
    smasi->sent_msgs = dict_create(16, NULL);
    smasi->received_msgs = gwlist_create();
    smasi->message_id_counter = counter_create();
    smasi->host = NULL;
    smasi->username = NULL;
    smasi->password = NULL;
    smasi->source_addr_ton = -1;
    smasi->source_addr_npi = -1;
    smasi->dest_addr_ton = -1;
    smasi->dest_addr_npi = -1;
    smasi->my_number = NULL;
    smasi->port = 21500;
    smasi->quitting = 0;
    smasi->logged_off = 0;
    smasi->priority = 0;
    smasi->throttling_err_time = 0;
    smasi->enquire_link_interval = 30;

    gwlist_add_producer(smasi->msgs_to_send);

    return smasi;
} 


static void smasi_destroy(SMASI *smasi) 
{
    if (smasi == NULL) return;

    gwlist_destroy(smasi->msgs_to_send, msg_destroy_item);
    dict_destroy(smasi->sent_msgs);
    gwlist_destroy(smasi->received_msgs, msg_destroy_item);
    counter_destroy(smasi->message_id_counter);
    octstr_destroy(smasi->host);
    octstr_destroy(smasi->username);
    octstr_destroy(smasi->password);
    gw_free(smasi);
} 



/************************************************************************/
/* DATA ENCODING                                                        */
/************************************************************************/

/* These values will be initialized on module startup. They contain the
 * ASCII representation of the chars that need to be escaped in the message
 * body before transmission. Example: "," (comma) will be represented by
 * the octet string ":2c".
 */

static Octstr *colon = NULL;
static Octstr *assign = NULL;
static Octstr *comma = NULL;
static Octstr *cr = NULL;
static Octstr *lf = NULL;


/*
 * Escapes outgoing message body data by replacing occurrences of "special"
 * chars inside the octet string.
 */
static void escape_data(Octstr *data) 
{
    long pos = 0;

    /* This one uses a different approach than the encode and decode
     * functions. Because it is assumed, that only a fraction of the
     * contained chars have to be escaped.
     */
    while (pos < octstr_len(data)) {
        Octstr * escaped = NULL;
        int check = octstr_get_char(data, pos);

        if (check == ':') escaped = colon;
        else if (check == '=') escaped = assign;
        else if (check == ',') escaped = comma;
        else if (check == '\n') escaped = cr;
        else if (check == '\r') escaped = lf;

        if (escaped != NULL) {
            /* If the current char has to be escaped, delete the char from
             * the source string, replace it with the escape sequence, and
             * advance position until after the inserted sequence.
             */
            octstr_delete(data, pos, 1);
            octstr_insert(data, escaped, pos);
            pos += octstr_len(escaped);
        } else {
            /* If not escaped, simply skip the current char. */
            pos++;
        } 
    } 
} 


/*
 * Unescapes incoming message body data by replacing occurrences of escaped
 * chars with their original character representation.
 */
static void unescape_data(Octstr *data) 
{
    long pos = 0;

    /* Again, an inplace transformation is used. Because, again, it is
     * assumed that only a fraction of chars has to be unescaped.
     */
    while (pos < octstr_len(data)) {
        int check = octstr_get_char(data, pos);

        if (check == ':') {
            char byte = 0;
            int msb = octstr_get_char(data, pos + 1);
            int lsb = octstr_get_char(data, pos + 2);
 
            if (msb == '0') msb = 0;
            else if (msb >= '1' && msb <= '9') msb -= '1' + 1;
            else msb -= 'a' + 10;

            if (lsb == '0') lsb = 0;
            else if (lsb >= '1' && lsb <= '9') lsb -= '1' + 1;
            else lsb -= 'a' + 10;

            byte = msb << 4 | lsb;

            /* Do inplace unescaping. */
            octstr_delete(data, pos, 3);
            octstr_insert_data(data, pos, &byte, 1);
        } 
        pos++;
    } 
}


/*
 * Will replace a binary data octet string (inplace) with a SMASI conform
 * ASCII representation of the data.
 */
static void encode_binary_data(Octstr *data) 
{
    Octstr *result = octstr_create("");
    long pos = 0;

    while (pos < octstr_len(data)) {
        int encode = octstr_get_char(data, pos);
        int msb = (encode & 0xf0) >> 4;
        int lsb = (encode & 0x0f) >> 0;

        if (msb == 0) msb = '0';
        else if (msb < 10) msb = '1' + msb - 1;
        else msb = 'a' + msb - 10;

        if (lsb == 0) lsb = '0';
        else if (lsb < 10) lsb = '1' + lsb - 1;
        else lsb = 'a' + lsb - 10;

        octstr_append_char(result, ':');
        octstr_append_char(result, msb);
        octstr_append_char(result, lsb);

        pos++;
    } 
    /* Replace binary data octet string with ASCII representation. */
    octstr_delete(data, 0, octstr_len(data));
    octstr_append(data, result);
    octstr_destroy(result);
}


/*
 * Re-escape SMASI ASCII representation of binary data with the
 * original binary data octet string.
 * XXX this may be done by the internal parser routines too.
 */
static void decode_binary_data(Octstr *data) 
{
    long pos = 0;

    while (pos < octstr_len(data)) {
        int check = octstr_get_char(data, pos);

        if (check == ':') {
            Octstr *byte;
            int msb = octstr_get_char(data, pos + 1);
            int lsb = octstr_get_char(data, pos + 2);

            if (msb != -1 && lsb != -1) {
                byte = octstr_create("");
                octstr_append_char(byte, msb);
                octstr_append_char(byte, lsb);

                if (octstr_hex_to_binary(byte) != -1) {
                    /* Do inplace unescaping. */
                    octstr_delete(data, pos, 3);
                    octstr_insert(data, byte, pos);
                } else {
                    error(0, "Malformed binary encoded data.");
                }

                octstr_destroy(byte);
            }
        } 
        pos++;
    } 
}


/************************************************************************/
/* MESSAGE PROCESSING                                                   */
/************************************************************************/

static Octstr *get_ton_npi_value(int override, int message) 
{
    if (override != -1) {
        debug("bb.sms.smasi", 0, "SMASI: Manually forced ton or npi to `%d'", 
              override);
        return (octstr_format("%ld", override));
    } else {
        return (octstr_format("%ld", message));
    }
}


/*
 * Gets the value to be used as source_addr_ton. Will use override values
 * if configured. Will use values from message otherwise. Or fall back to
 * defaults if nothing given.
 */
static Octstr *get_source_addr_ton(SMASI *smasi, Msg *msg) 
{
    return get_ton_npi_value(smasi->source_addr_ton, 
                             GSM_ADDR_TON_INTERNATIONAL);
}


/*
 * Gets the value to be used as source_addr_npi. Will use override values
 * if configured. Will use values from message otherwise. Or fall back to
 * defaults if nothing given.
 */
static Octstr *get_source_addr_npi(SMASI *smasi, Msg *msg) 
{
    return get_ton_npi_value(smasi->source_addr_npi, 
                             GSM_ADDR_NPI_E164);
}


/*
 * Gets the value to be used as dest_addr_ton. Will use override values
 * if configured. Will use values from message otherwise. Or fall back to
 * defaults if nothing given.
 */
static Octstr *get_dest_addr_ton(SMASI *smasi, Msg *msg) 
{
    return get_ton_npi_value(smasi->dest_addr_ton, 
                             GSM_ADDR_TON_INTERNATIONAL);
}


/*
 * Gets the value to be used as dest_addr_npi. Will use override values
 * if configured. Will use values from message otherwise. Or fall back to
 * defaults if nothing given.
 */
static Octstr *get_dest_addr_npi(SMASI *smasi, Msg *msg) 
{
    return get_ton_npi_value(smasi->dest_addr_npi, 
                             GSM_ADDR_NPI_E164);
}


/*
 * Determine the originator (sender number) type based on the number. Will
 * change the originator number if necessary.
 */
static Octstr *get_originator_type(SMASI *smasi, Octstr *originator) 
{
    /* International or alphanumeric sender? */
    if (octstr_get_char(originator, 0) == '+') {
        if (!octstr_check_range(originator, 1, 256, gw_isdigit)) {
            return octstr_format("%ld", GSM_ADDR_TON_ALPHANUMERIC);
        } else {
           /* Numeric sender address with + in front: The + has to be
            * removed from this international number.
            */
           octstr_delete(originator, 0, 1);
           return octstr_format("%ld", GSM_ADDR_TON_INTERNATIONAL);
        }
    } else if (!octstr_check_range(originator, 0, 256, gw_isdigit)) {
       return octstr_format("%ld", GSM_ADDR_TON_ALPHANUMERIC);
    }

    /* Return the default value. */
    return octstr_format("%ld", GSM_ADDR_TON_INTERNATIONAL);
}


/*
 * Creates a SubmitReq PDU from an outgoing message.
 */
static SMASI_PDU *msg_to_pdu(SMASI *smasi, Msg *msg) 
{
    SMASI_PDU *pdu = smasi_pdu_create(SubmitReq);

    pdu->u.SubmitReq.Destination = octstr_duplicate(msg->sms.receiver);
    pdu->u.SubmitReq.Body = octstr_duplicate(msg->sms.msgdata);
    pdu->u.SubmitReq.Originator = octstr_duplicate(msg->sms.sender);

    pdu->u.SubmitReq.OriginatorType = 
        get_originator_type(smasi, pdu->u.SubmitReq.Originator);

    pdu->u.SubmitReq.Sequence = 
        octstr_format("%ld", counter_increase(smasi->message_id_counter));


    /* If its a international number starting with +, lets remove the +. */
    if (octstr_get_char(pdu->u.SubmitReq.Destination, 0) == '+')
        octstr_delete(pdu->u.SubmitReq.Destination, 0, 1);

    /* Do ton and npi override - if configured. Use values from message
     * otherwise.
     */
    pdu->u.SubmitReq.OriginatorType = get_source_addr_ton(smasi, msg);
    pdu->u.SubmitReq.OriginatorPlan = get_source_addr_npi(smasi, msg);
    pdu->u.SubmitReq.DestinationType = get_dest_addr_ton(smasi, msg);
    pdu->u.SubmitReq.DestinationPlan = get_dest_addr_npi(smasi, msg);
       
    /* Set priority. */
    if (smasi->priority >= 0 && smasi->priority <= 3) {
        pdu->u.SubmitReq.MqPriority = octstr_format("%ld", smasi->priority);
    } else {
        pdu->u.SubmitReq.MqPriority = octstr_format("%ld", 0);
    }

    /* Set encoding. */
    if (msg->sms.coding != DC_UNDEF) {
        if (msg->sms.coding == DC_7BIT)
            pdu->u.SubmitReq.MsEncoding = octstr_create("7bit");
        else if (msg->sms.coding == DC_8BIT)
            pdu->u.SubmitReq.MsEncoding = octstr_create("8bit");
        else if (msg->sms.coding == DC_UCS2)
            pdu->u.SubmitReq.MsEncoding = octstr_create("16bit");

        /* Everything else will default to 7bit. */
    }

    /* Set messaging class - if within defined parameter range. */
    if (msg->sms.mclass != MC_UNDEF)
        pdu->u.SubmitReq.Class = octstr_format("%ld", msg->sms.mclass);

    /* Set Protocol ID. */
    pdu->u.SubmitReq.ProtocolID = octstr_format("%ld", 
	(msg->sms.pid == SMS_PARAM_UNDEFINED ? 0 : msg->sms.pid));

    /* Check if SMS is binary. */
    if (msg->sms.udhdata && octstr_len(msg->sms.udhdata) > 0) {
        
        pdu->u.SubmitReq.UserDataHeader =
          octstr_duplicate(msg->sms.udhdata);

        pdu->u.SubmitReq.BodyEncoding =
          octstr_create("Data");

        if (pdu->u.SubmitReq.MsEncoding)
          octstr_destroy(pdu->u.SubmitReq.MsEncoding);

        pdu->u.SubmitReq.MsEncoding =
          octstr_create("transparent");

        /* Encode data. */
        encode_binary_data(pdu->u.SubmitReq.UserDataHeader);
        encode_binary_data(pdu->u.SubmitReq.Body);
    } else {

        /* Otherwise do data escaping. */
        escape_data(pdu->u.SubmitReq.Body);
    } 

    return pdu;
} 


/*
 * Create a message structure from an incoming DeliverReq PDU.
 */
static Msg *pdu_to_msg(SMASI_PDU *pdu) 
{
    Msg *msg = NULL;

    gw_assert(pdu->type == DeliverReq);
    gw_assert(pdu->u.DeliverReq.Originator);
    gw_assert(pdu->u.DeliverReq.Destination);
    gw_assert(pdu->u.DeliverReq.Body);

    msg = msg_create(sms);

    msg->sms.sender = octstr_duplicate(pdu->u.DeliverReq.Originator);
    msg->sms.receiver = octstr_duplicate(pdu->u.DeliverReq.Destination);
    msg->sms.msgdata = octstr_duplicate(pdu->u.DeliverReq.Body);
 
    /* Read priority. */
    if (pdu->u.DeliverReq.ProtocolId)
        if (octstr_parse_long(&msg->sms.pid, 
                              pdu->u.DeliverReq.ProtocolId, 0, 10) == -1)
            msg->sms.pid = SMS_PARAM_UNDEFINED;

    /* Read Coding. */
    if (pdu->u.DeliverReq.MsEncoding) {

        /* Use specified coding. */
        if (octstr_str_compare(pdu->u.DeliverReq.MsEncoding, "7bit") == 0)
            msg->sms.coding = DC_7BIT;
        else if (octstr_str_compare(pdu->u.DeliverReq.MsEncoding, "8bit") == 0)
            msg->sms.coding = DC_8BIT;
        else if (octstr_str_compare(pdu->u.DeliverReq.MsEncoding, "UCS2") == 0)
            msg->sms.coding = DC_UCS2;
        else if (octstr_str_compare(pdu->u.DeliverReq.MsEncoding, "transparent") == 0)
            msg->sms.coding = DC_8BIT;
    } else {

        /* Determine specified coding according to udhdata presence. */
        if (pdu->u.DeliverReq.UserDataHeader)
            msg->sms.coding = DC_8BIT;
        else
            msg->sms.coding = DC_7BIT;
    }

    /* Unescape (non-binary) or decode (binary) data. */
    if (msg->sms.coding == DC_8BIT) {

        decode_binary_data(msg->sms.msgdata);
        if (pdu->u.DeliverReq.UserDataHeader &&
            octstr_len(pdu->u.DeliverReq.UserDataHeader) > 0) {
            msg->sms.udhdata = octstr_duplicate(pdu->u.DeliverReq.UserDataHeader);
            decode_binary_data(msg->sms.udhdata);
        }

    } else {
        unescape_data(msg->sms.msgdata);
    } 

    /* Read message class. */
    if (pdu->u.DeliverReq.Class &&
        octstr_parse_long(&msg->sms.mclass, 
                          pdu->u.DeliverReq.Class, 0, 10) == -1)
            msg->sms.mclass = MC_UNDEF;    /* Set to unspecified. */

    /* Read protocol ID. */
    if (pdu->u.DeliverReq.ProtocolId &&
        octstr_parse_long(&msg->sms.pid, 
                          pdu->u.DeliverReq.ProtocolId, 0, 10) == -1)
            msg->sms.pid = SMS_PARAM_UNDEFINED;

    return msg;
}
 

/************************************************************************/
/* PDU HANDLING                                                         */
/************************************************************************/

static void send_logoff(SMASI *smasi, Connection *conn) 
{
    SMASI_PDU *pdu = NULL;
    Octstr *os = NULL;

    counter_increase(smasi->message_id_counter);

    pdu = smasi_pdu_create(LogoffReq);
    pdu->u.LogoffReq.Reason = octstr_create("Client shutting down");
    dump_pdu("Sending !LogoffReq:", smasi->conn->id, pdu); 

    os = smasi_pdu_pack(pdu);
    conn_write(conn, os);
    octstr_destroy(os);
    smasi_pdu_destroy(pdu);
}

 
static void send_enquire_link(SMASI *smasi, Connection *conn, long *last_sent) 
{ 
    SMASI_PDU *pdu = NULL; 
    Octstr *os = NULL; 
 
    if (date_universal_now() - *last_sent < smasi->enquire_link_interval)
        return; 
    *last_sent = date_universal_now(); 
 
    pdu = smasi_pdu_create(EnquireLinkReq);
    dump_pdu("Sending EnquireLinkReq:", smasi->conn->id, pdu); 
    os = smasi_pdu_pack(pdu); 
    if (os)
	   conn_write(conn, os); /* Write errors checked by caller. */ 
    octstr_destroy(os); 
    smasi_pdu_destroy(pdu); 
} 


static int send_pdu(Connection *conn, Octstr *id, SMASI_PDU *pdu)
{
    Octstr * os = NULL;
    int ret = 0;

    dump_pdu("Sending PDU:", id, pdu); 
    os = smasi_pdu_pack(pdu);
    if (os) ret = conn_write(conn, os);
    else ret = -1;

    octstr_destroy(os);
    return ret;
}
 

/*
 * Try to read a SMASI PDU from a connection. Return -1 for error (caller
 * should close the connection), 0 for no PDU ready yet, or 1 for PDU read
 * and unpacked. Return a pointer to the PDU in `*pdu'.
 */
static int read_pdu(SMASI *smasi, Connection *conn, SMASI_PDU **pdu) 
{
    Octstr *os;
    
    os = smasi_pdu_read(conn);
    if (os == NULL) {
        if (conn_eof(conn) || conn_error(conn)) 
            return -1;
        return 0;
    }

    *pdu = smasi_pdu_unpack(os);
    if (*pdu == NULL) {
        error(0, "SMASI[%s]: PDU unpacking failed.",
              octstr_get_cstr(smasi->conn->id));
        debug("bb.sms.smasi", 0, "SMASI[%s]: Failed PDU follows.",
              octstr_get_cstr(smasi->conn->id));
        octstr_dump(os, 0);
        octstr_destroy(os);
        return -1;
    }
    octstr_destroy(os);
    return 1;
}


static void handle_pdu(SMASI *smasi, Connection *conn, 
                       SMASI_PDU *pdu, long *pending_submits) 
{
    SMASI_PDU *resp = NULL;
    Msg *msg = NULL;
    long reason;

    switch (pdu->type) {

        case DeliverReq:
            msg = pdu_to_msg(pdu);

            msg_dump(msg, 0);

            if (smasi->my_number && octstr_len(smasi->my_number)) {
                octstr_destroy(msg->sms.receiver);
                msg->sms.receiver = octstr_duplicate(smasi->my_number);
            }

            time(&msg->sms.time);
            msg->sms.smsc_id = octstr_duplicate(smasi->conn->id);
            bb_smscconn_receive(smasi->conn, msg);
            resp = smasi_pdu_create(DeliverConf);

            if (pdu->u.DeliverReq.Sequence)
                resp->u.DeliverConf.Sequence =
                  octstr_duplicate(pdu->u.DeliverReq.Sequence);

            if (pdu->u.DeliverReq.MsgReference)
                resp->u.DeliverConf.MsgReference =
                  octstr_duplicate(pdu->u.DeliverReq.MsgReference);
            break;

        case SubmitConf:
            if (pdu->u.SubmitConf.Sequence) {
                msg = dict_remove(smasi->sent_msgs, 
                                  pdu->u.SubmitConf.Sequence);
            } else {
                msg = NULL;
            }

            if (msg == NULL) {
                warning(0, "SMASI[%s]: SMSC sent SubmitConf for unknown message.",
                        octstr_get_cstr(smasi->conn->id));
            } else {
                debug("bb.sms.smasi",0,
                      "SMSC[%s]: SMSC confirmed msg seq <%s> ref <%s>",
                       octstr_get_cstr(smasi->conn->id),
                       octstr_get_cstr(pdu->u.SubmitConf.Sequence),
                       octstr_get_cstr(pdu->u.SubmitConf.MsgReference));

                bb_smscconn_sent(smasi->conn, msg, NULL);

                --(*pending_submits);
            }
            break;

        case SubmitRej:
            if (pdu->u.SubmitRej.Sequence) {
                msg = dict_remove(smasi->sent_msgs, 
                                  pdu->u.SubmitRej.Sequence);
            } else {
                msg = NULL;
            }

            error(0, "SMASI[%s]: SMSC returned error code %s for "
                  "message ref <%s>", octstr_get_cstr(smasi->conn->id),
                  octstr_get_cstr(pdu->u.SubmitRej.RejectCode),
                  octstr_get_cstr(pdu->u.SubmitRej.MsgReference));

            if (msg == NULL) {
               warning(0, "SMASI[%s]: SMSC sent SubmitRej for unknown message.",
                       octstr_get_cstr(smasi->conn->id));
            } else {
                reason = SMSCCONN_FAILED_REJECTED;
                bb_smscconn_send_failed(smasi->conn, msg, reason,
		            octstr_create("REJECTED"));
                --(*pending_submits);
            }
            break;

        case LogonConf:
            *pending_submits = 0;

            smasi->conn->status = SMSCCONN_ACTIVE;
            smasi->conn->connect_time = time(NULL);

            bb_smscconn_connected(smasi->conn);

            info(0, "SMASI[%s]: connection to SMSC established.",
                 octstr_get_cstr(smasi->conn->id));
            break;

        case LogonRej:
            if (octstr_len(pdu->u.LogonRej.Reason) > 0) {
                error(0, "SMASI[%s]: SMSC rejected login with reason <%s>",
                      octstr_get_cstr(smasi->conn->id),
                      octstr_get_cstr(pdu->u.LogonRej.Reason));
            } else {
                error(0, "SMASI[%s]: SMSC rejected login without reason",
                      octstr_get_cstr(smasi->conn->id));
            }
            break;

        case LogoffConf:
            info(0, "SMASI[%s]: SMSC confirmed logoff.",
                 octstr_get_cstr(smasi->conn->id));
            smasi->logged_off = 1;
            break;

        default:
            warning(0, "SMASI[%s]: Unknown PDU type <%s>, ignored.",
                    octstr_get_cstr(smasi->conn->id), pdu->type_name);
            break;
    }

    if (resp != NULL) {
        send_pdu(conn, smasi->conn->id, resp);
        smasi_pdu_destroy(resp);
    }
}


/************************************************************************/
/* SMASI CONNECTION HANDLING                                            */
/************************************************************************/

/*
 * Open transmission connection to SMS center. Return NULL for error,
 * open connection for OK. Caller must set smasi->conn->status correctly
 * before calling this.
 */
static Connection *open_connection(SMASI *smasi) 
{
    Connection *conn = conn_open_tcp_with_port(smasi->host, smasi->port, 0, smasi->conn->our_host);

    if (conn == NULL) {
        error(0, "SMASI[%s]: Couldn't connect to server.",
              octstr_get_cstr(smasi->conn->id));
        return NULL;
    } else {
        SMASI_PDU *logon = smasi_pdu_create(LogonReq);

        logon->u.LogonReq.Name = octstr_duplicate(smasi->username);
        logon->u.LogonReq.Password = octstr_duplicate(smasi->password);

        counter_increase(smasi->message_id_counter);

        send_pdu(conn, smasi->conn->id, logon);

        smasi_pdu_destroy(logon);
    }

    return conn;
} 


static void send_messages(SMASI *smasi, Connection *conn, 
                          long *pending_submits) 
{
    double delay = 0;

    if (*pending_submits == -1) return;

    if (smasi->conn->throughput > 0) {
        delay = 1.0 / smasi->conn->throughput;
    }

    while (*pending_submits < MAX_PENDING_SUBMITS) {
        SMASI_PDU *pdu = NULL;
        /* Get next message, quit if none to be sent. */
        Msg *msg = gwlist_extract_first(smasi->msgs_to_send);

        if (msg == NULL) break;

        /* Send PDU, record it as waiting for ack from SMSC. */
        pdu = msg_to_pdu(smasi, msg);

        if (pdu->u.SubmitReq.Sequence)
            dict_put(smasi->sent_msgs, pdu->u.SubmitReq.Sequence, msg);

        send_pdu(conn, smasi->conn->id, pdu);

        smasi_pdu_destroy(pdu);

        /* obey throughput speed limit, if any */
        if (smasi->conn->throughput > 0)
            gwthread_sleep(delay);

        ++(*pending_submits);
    }
}


/*
 * This is the main function for the background thread for doing I/O on
 * one SMASI connection (the one for transmitting or receiving messages).
 * It makes the initial connection to the SMASI server and re-connects
 * if there are I/O errors or other errors that require it.
 */
static void smasi_thread(void *arg) 
{
    long pending_submits;
    long len;
    SMASI_PDU *pdu;
    SMASI *smasi;
    int logoff_already_sent = 0;
    int ret;
    Connection *conn;
    long last_enquire_sent; 
    double timeout; 

    smasi = arg;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(smasi->conn->log_idx);
    
    while (!smasi->quitting) {

        conn = open_connection(smasi);
        if (conn == NULL) {
            error(0, "SMASI[%s]: Could not connect to SMSC center " \
                  "(retrying in %ld seconds).",
                  octstr_get_cstr(smasi->conn->id), smasi->conn->reconnect_delay);

            gwthread_sleep(smasi->conn->reconnect_delay);
            smasi->conn->status = SMSCCONN_RECONNECTING;
            continue;
        }

        last_enquire_sent = date_universal_now(); 
        pending_submits = -1;
        len = 0;

        for (;;) {
            timeout = last_enquire_sent + smasi->enquire_link_interval
                        - date_universal_now(); 

            /* wait for activity */
            if (conn_wait(conn, timeout) == -1) {
                error(0, "SMASI[%s]: I/O error or other error. Re-connecting.",
                      octstr_get_cstr(smasi->conn->id));
                break;
            }

            /* Send logoff request if module is shutting down. */
            if (smasi->quitting && !logoff_already_sent) {
                send_logoff(smasi, conn);
                logoff_already_sent = 1;
            } 

            /* send an enquire link */
            send_enquire_link(smasi, conn, &last_enquire_sent); 

            /* Receive incoming PDUs. */
            while ((ret = read_pdu(smasi, conn, &pdu)) == 1) {
                /* Deal with the PDU we just got */ 
                dump_pdu("Got PDU:", smasi->conn->id, pdu);

                /* Process the received PDU. */
                handle_pdu(smasi, conn, pdu, &pending_submits);

                smasi_pdu_destroy(pdu);

                /* Bail out if logoff confirmed. */
                if (smasi->logged_off) break;

                /* Make sure we send even if we read a lot. */
                if ((!smasi->throttling_err_time ||
                    ((time(NULL) - smasi->throttling_err_time) >
                     SMASI_THROTTLING_SLEEP_TIME
                      && !(smasi->throttling_err_time = 0))))
                    send_messages(smasi, conn, &pending_submits);
            } 

            /* Check if connection broken. */
            if (ret == -1) {
                error(0, "SMASI[%s]: I/O error or other error. Re-connecting.",
                      octstr_get_cstr(smasi->conn->id));
                break;
            } 

            /* Bail out if logoff confirmed. */
            if (smasi->logged_off) break;

            if ((!smasi->throttling_err_time ||
                ((time(NULL) - smasi->throttling_err_time) >
                 SMASI_THROTTLING_SLEEP_TIME
                  && !(smasi->throttling_err_time = 0))))
                send_messages(smasi, conn, &pending_submits);

        } 

        conn_destroy(conn);
        conn = NULL;
    } 
} 


/************************************************************************/
/* SMSCCONN INTERFACE                                                   */
/************************************************************************/

static long queued_cb(SMSCConn *conn) 
{
    SMASI *smasi = conn->data;

    conn->load = (smasi ? (conn->status != SMSCCONN_DEAD ? 
                    gwlist_len(smasi->msgs_to_send) : 0) : 0);

    return conn->load;
} 


static int send_msg_cb(SMSCConn *conn, Msg *msg) 
{
    SMASI *smasi = conn->data;

    gwlist_produce(smasi->msgs_to_send, msg_duplicate(msg));
    gwthread_wakeup(smasi->thread_handle);

    return 0;
}


static int shutdown_cb(SMSCConn *conn, int finish_sending) 
{
    SMASI *smasi = NULL;

    debug("bb.sms.smasi", 0, "Shutting down SMSCConn %s (%s)",
          octstr_get_cstr(conn->name), finish_sending ? "slow" : "instant");

    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

    smasi = conn->data;
    smasi->quitting = 1;
    gwthread_wakeup(smasi->thread_handle);
    gwthread_join(smasi->thread_handle);
    smasi_destroy(smasi);

    debug("bb.sms.smasi", 0, "SMSCConn %s shut down.",
          octstr_get_cstr(conn->name));
    conn->status = SMSCCONN_DEAD;
    bb_smscconn_killed();

    /* Clean up. */
    octstr_destroy(colon);
    octstr_destroy(assign);
    octstr_destroy(comma);
    octstr_destroy(cr);
    octstr_destroy(lf);

    return 0;
}


/*
 * Configures the SMASI structure according to the configuration.
 *
 * @return 0 on complete success. -1 if failed due to missing or invalid
 * configuration entry.
 */
static int init_configuration(SMASI *smasi, CfgGroup *config) 
{
    /* Read mandatory entries. */
    smasi->host = cfg_get(config, octstr_imm("host"));
    smasi->username = cfg_get(config, octstr_imm("smsc-username"));
    smasi->password = cfg_get(config, octstr_imm("smsc-password"));

    /* Check configuration. */
    if (smasi->host == NULL) {
        error(0,"SMASI: Configuration file doesn't specify host");
        return -1;
    }
    if (smasi->username == NULL) {
        error(0, "SMASI: Configuration file doesn't specify username.");
        return -1;
    }
    if (smasi->password == NULL) {
        error(0, "SMASI: Configuration file doesn't specify password.");
        return -1;
    }

    /* Read optional entries. Set default values if not set. */
    smasi->my_number = cfg_get(config, octstr_imm("my-number"));
    if (cfg_get_integer(&smasi->port, config, octstr_imm("port")) == -1)
        smasi->port = SMASI_DEFAULT_PORT;
    if (cfg_get_integer(&smasi->source_addr_ton, config,
      octstr_imm("source-addr-ton")) == -1)
        smasi->source_addr_ton = SMASI_OVERRIDE_SOURCE_TON;
    if (cfg_get_integer(&smasi->source_addr_npi, config,
      octstr_imm("source-addr-npi")) == -1)
        smasi->source_addr_npi = SMASI_OVERRIDE_SOURCE_NPI;
    if (cfg_get_integer(&smasi->dest_addr_ton, config,
      octstr_imm("dest-addr-ton")) == -1)
        smasi->dest_addr_ton = SMASI_OVERRIDE_DEST_TON;
    if (cfg_get_integer(&smasi->dest_addr_npi, config,
      octstr_imm("dest-addr-npi")) == -1)
        smasi->dest_addr_npi = SMASI_OVERRIDE_DEST_NPI;
    if (cfg_get_integer(&smasi->priority, config,
      octstr_imm("priority")) == -1)
        smasi->priority = SMASI_DEFAULT_PRIORITY;
    if (cfg_get_integer(&smasi->enquire_link_interval, config,
      octstr_imm("enquire-link-interval")) == -1)
        smasi->enquire_link_interval = SMASI_ENQUIRE_LINK_INTERVAL;
   
    /* Configure SMSC connection. */
    smasi->conn->data = smasi;
    smasi->conn->name = octstr_format("SMASI:%S:%d:%S",
        smasi->host, smasi->port, smasi->username);

    smasi->conn->id = cfg_get(config, octstr_imm("smsc-id"));

    if (smasi->conn->id == NULL)
      smasi->conn->id = octstr_duplicate(smasi->conn->name);

    return 0;
} 


int smsc_smasi_create(SMSCConn *conn, CfgGroup *config) 
{
    SMASI *smasi = NULL;

    /* Initialize data encoding subsystem. */
    colon = octstr_create(":3a");
    assign = octstr_create(":3d");
    comma = octstr_create(":2c");
    cr = octstr_create(":0a");
    lf = octstr_create(":0d");

    /* Create main SMASI structure and initialize it with configuration
     * settings.
     */
    smasi = smasi_create(conn);

    if (init_configuration(smasi, config) != 0)
        panic(0, "SMASI SMSC module configuration invalid.");

    conn->status = SMSCCONN_CONNECTING;

    /* Port is always set to a configured value or defaults to 21500.
     * Therefore, threads are always started.
     */
    smasi->thread_handle = gwthread_create(smasi_thread, smasi);

    if (smasi->thread_handle == -1) {
        error(0, "SMASI[%s]: Couldn't start SMASI thread.",
              octstr_get_cstr(smasi->conn->id));
        smasi_destroy(conn->data);
        return -1;
    } 

    /* Setup control function pointers. */
    conn->shutdown = shutdown_cb;
    conn->queued = queued_cb;
    conn->send_msg = send_msg_cb;

    return 0;
}

