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
 * smsc_smpp.c - SMPP v3.3, v3.4 and v5.0 implementation
 *
 * Lars Wirzenius
 * Stipe Tolj <stolj at kannel.org>
 * Alexander Malysh  <amalysh at kannel.org>
 */

/* XXX check SMSCConn conformance */
/* XXX UDH reception */
/* XXX check UDH sending fields esm_class and data_coding from GSM specs */
/* XXX charset conversions on incoming messages (didn't work earlier,
       either) */
/* XXX numbering plans and type of number: check spec */

#include "gwlib/gwlib.h"
#include "msg.h"
#include "smsc_p.h"
#include "smpp_pdu.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "sms.h"
#include "dlr.h"
#include "bearerbox.h"
#include "meta_data.h"
#include "load.h"

#define SMPP_DEFAULT_CHARSET "UTF-8"

/*
 * Select these based on whether you want to dump SMPP PDUs as they are
 * sent and received or not. Not dumping should be the default in at least
 * stable releases.
 */

#define DEBUG 1

#ifndef DEBUG
#define dump_pdu(msg, id, pdu) do{}while(0)
#else
/** This version does dump. */
#define dump_pdu(msg, id, pdu)                  \
    do {                                        \
        debug("bb.sms.smpp", 0, "SMPP[%s]: %s", \
            octstr_get_cstr(id), msg);          \
        smpp_pdu_dump(id, pdu);                 \
    } while(0)
#endif


/*
 * Some defaults.
 */

#define SMPP_ENQUIRE_LINK_INTERVAL  30.0
#define SMPP_MAX_PENDING_SUBMITS    10
#define SMPP_DEFAULT_VERSION        0x34
#define SMPP_DEFAULT_PRIORITY       0
#define SMPP_THROTTLING_SLEEP_TIME  1
#define SMPP_DEFAULT_CONNECTION_TIMEOUT  10 * SMPP_ENQUIRE_LINK_INTERVAL
#define SMPP_DEFAULT_WAITACK        60
#define SMPP_DEFAULT_SHUTDOWN_TIMEOUT 30
#define SMPP_DEFAULT_PORT           2775


/*
 * Some defines
 */
#define SMPP_WAITACK_RECONNECT      0x00
#define SMPP_WAITACK_REQUEUE        0x01
#define SMPP_WAITACK_NEVER_EXPIRE   0x02

/***********************************************************************
 * Implementation of the actual SMPP protocol: reading and writing
 * PDUs in the correct order.
 */


typedef struct {
    long transmitter;
    long receiver;
    gw_prioqueue_t *msgs_to_send;
    Dict *sent_msgs;
    List *received_msgs;
    Counter *message_id_counter;
    Octstr *host;
    Octstr *system_type;
    Octstr *username;
    Octstr *password;
    Octstr *address_range;
    Octstr *my_number;
    Octstr *service_type;
    int source_addr_ton;
    int source_addr_npi;
    int dest_addr_ton;
    int dest_addr_npi;
    int our_port;
    int our_receiver_port;
    long bind_addr_ton;
    long bind_addr_npi;
    int transmit_port;
    int receive_port;
    int use_ssl;
    Octstr *ssl_client_certkey_file;
    volatile int quitting;
    long enquire_link_interval;
    long max_pending_submits;
    int version;
    int priority;       /* set default priority for messages */
    int validityperiod;
    time_t throttling_err_time;
    int smpp_msg_id_type;  /* msg id in C string, hex or decimal */
    int autodetect_addr;
    Octstr *alt_charset;
    Octstr *alt_addr_charset;
    long connection_timeout;
    long wait_ack;
    int wait_ack_action;
    int esm_class;
    Load *load;
    SMSCConn *conn;
} SMPP;


struct smpp_msg {
    time_t sent_time;
    Msg *msg;
};


/*
 * create smpp_msg struct
 */
static inline struct smpp_msg* smpp_msg_create(Msg *msg)
{
    struct smpp_msg *result = gw_malloc(sizeof(struct smpp_msg));

    gw_assert(result != NULL);
    result->sent_time = time(NULL);
    result->msg = msg;

    return result;
}


/*
 * destroy smpp_msg struct. If destroy_msg flag is set, then message will be freed as well
 */
static inline void smpp_msg_destroy(struct smpp_msg *msg, int destroy_msg)
{
    /* sanity check */
    if (msg == NULL)
        return;

    if (destroy_msg && msg->msg != NULL)
        msg_destroy(msg->msg);

    gw_free(msg);
}


static SMPP *smpp_create(SMSCConn *conn, Octstr *host, int transmit_port,
                         int receive_port, int our_port, int our_receiver_port, Octstr *system_type,
                         Octstr *username, Octstr *password,
                         Octstr *address_range,
                         int source_addr_ton, int source_addr_npi,
                         int dest_addr_ton, int dest_addr_npi,
                         int enquire_link_interval, int max_pending_submits,
                         int version, int priority, int validity,
                         Octstr *my_number, int smpp_msg_id_type,
                         int autodetect_addr, Octstr *alt_charset, Octstr *alt_addr_charset,
                         Octstr *service_type, long connection_timeout,
                         long wait_ack, int wait_ack_action, int esm_class)
{
    SMPP *smpp;

    smpp = gw_malloc(sizeof(*smpp));
    smpp->transmitter = -1;
    smpp->receiver = -1;
    smpp->msgs_to_send = gw_prioqueue_create(sms_priority_compare);
    smpp->sent_msgs = dict_create(max_pending_submits, NULL);
    gw_prioqueue_add_producer(smpp->msgs_to_send);
    smpp->received_msgs = gwlist_create();
    smpp->message_id_counter = counter_create();
    counter_increase(smpp->message_id_counter);
    smpp->host = octstr_duplicate(host);
    smpp->system_type = octstr_duplicate(system_type);
    smpp->our_port = our_port;
    smpp->our_receiver_port = our_receiver_port;
    smpp->username = octstr_duplicate(username);
    smpp->password = octstr_duplicate(password);
    smpp->address_range = octstr_duplicate(address_range);
    smpp->source_addr_ton = source_addr_ton;
    smpp->source_addr_npi = source_addr_npi;
    smpp->dest_addr_ton = dest_addr_ton;
    smpp->dest_addr_npi = dest_addr_npi;
    smpp->my_number = octstr_duplicate(my_number);
    smpp->service_type = octstr_duplicate(service_type);
    smpp->transmit_port = transmit_port;
    smpp->receive_port = receive_port;
    smpp->enquire_link_interval = enquire_link_interval;
    smpp->max_pending_submits = max_pending_submits;
    smpp->quitting = 0;
    smpp->version = version;
    smpp->priority = priority;
    smpp->validityperiod = validity;
    smpp->conn = conn;
    smpp->throttling_err_time = 0;
    smpp->smpp_msg_id_type = smpp_msg_id_type;
    smpp->autodetect_addr = autodetect_addr;
    smpp->alt_charset = octstr_duplicate(alt_charset);
    smpp->alt_addr_charset = octstr_duplicate(alt_addr_charset);
    smpp->connection_timeout = connection_timeout;
    smpp->wait_ack = wait_ack;
    smpp->wait_ack_action = wait_ack_action;
    smpp->bind_addr_ton = 0;
    smpp->bind_addr_npi = 0;
    smpp->use_ssl = 0;
    smpp->ssl_client_certkey_file = NULL;
    smpp->load = load_create_real(0);
    load_add_interval(smpp->load, 1);
    smpp->esm_class = esm_class;

    return smpp;
}


static void smpp_destroy(SMPP *smpp)
{
    if (smpp != NULL) {
        gw_prioqueue_destroy(smpp->msgs_to_send, msg_destroy_item);
        dict_destroy(smpp->sent_msgs);
        gwlist_destroy(smpp->received_msgs, msg_destroy_item);
        counter_destroy(smpp->message_id_counter);
        octstr_destroy(smpp->host);
        octstr_destroy(smpp->username);
        octstr_destroy(smpp->password);
        octstr_destroy(smpp->system_type);
        octstr_destroy(smpp->service_type);
        octstr_destroy(smpp->address_range);
        octstr_destroy(smpp->my_number);
        octstr_destroy(smpp->alt_charset);
        octstr_destroy(smpp->alt_addr_charset);
        octstr_destroy(smpp->ssl_client_certkey_file);
        load_destroy(smpp->load);
        gw_free(smpp);
    }
}


/*
 * Try to read an SMPP PDU from a Connection. Return -1 for error (caller
 * should close the connection), -2 for malformed PDU , 0 for no PDU to
 * ready yet, or 1 for PDU
 * read and unpacked. Return a pointer to the PDU in `*pdu'. Use `*len'
 * to store the length of the PDU to read (it may be possible to read the
 * length, but not the rest of the PDU - we need to remember the lenght
 * for the next call). `*len' should be zero at the first call.
 */
static int read_pdu(SMPP *smpp, Connection *conn, long *len, SMPP_PDU **pdu)
{
    Octstr *os;

    if (*len == 0) {
        *len = smpp_pdu_read_len(conn);
        if (*len == -1) {
            error(0, "SMPP[%s]: Server sent garbage, ignored.",
                  octstr_get_cstr(smpp->conn->id));
            return -2;
        } else if (*len == 0) {
            if (conn_eof(conn) || conn_error(conn))
                return -1;
            return 0;
        }
    }

    os = smpp_pdu_read_data(conn, *len);
    if (os == NULL) {
        if (conn_eof(conn) || conn_error(conn))
            return -1;
        return 0;
    }
    *len = 0;

    *pdu = smpp_pdu_unpack(smpp->conn->id, os);
    if (*pdu == NULL) {
        error(0, "SMPP[%s]: PDU unpacking failed.",
              octstr_get_cstr(smpp->conn->id));
        debug("bb.sms.smpp", 0, "SMPP[%s]: Failed PDU follows.",
              octstr_get_cstr(smpp->conn->id));
        octstr_dump(os, 0);
        octstr_destroy(os);
        return -2;
    }

    octstr_destroy(os);
    return 1;
}


static long convert_addr_from_pdu(Octstr *id, Octstr *addr, long ton, long npi, Octstr *alt_addr_charset)
{
    long reason = SMPP_ESME_ROK;

    if (addr == NULL)
        return reason;

    switch(ton) {
        case GSM_ADDR_TON_INTERNATIONAL:
            /*
             * Checks to perform:
             *   1) assume international number has at least 7 chars
             *   2) the whole source addr consist of digits, exception '+' in front
             */
            if (octstr_len(addr) < 7) {
                /* We consider this as a "non-hard" condition, since there "may"
                 * be international numbers routable that are < 7 digits. Think
                 * of 2 digit country code + 3 digit emergency code. */
                warning(0, "SMPP[%s]: Malformed addr `%s', generally expected at least 7 digits. ",
                        octstr_get_cstr(id),
                        octstr_get_cstr(addr));
            } else if (octstr_get_char(addr, 0) == '+' &&
                       !octstr_check_range(addr, 1, 256, gw_isdigit)) {
                error(0, "SMPP[%s]: Malformed addr `%s', expected all digits. ",
                      octstr_get_cstr(id),
                      octstr_get_cstr(addr));
                reason = SMPP_ESME_RINVSRCADR;
                goto error;
            } else if (octstr_get_char(addr, 0) != '+' &&
                       !octstr_check_range(addr, 0, 256, gw_isdigit)) {
                error(0, "SMPP[%s]: Malformed addr `%s', expected all digits. ",
                      octstr_get_cstr(id),
                      octstr_get_cstr(addr));
                reason = SMPP_ESME_RINVSRCADR;
                goto error;
            }
            /* check if we received leading '00', then remove it*/
            if (octstr_search(addr, octstr_imm("00"), 0) == 0)
                octstr_delete(addr, 0, 2);
            
            /* international, insert '+' if not already here */
            if (octstr_get_char(addr, 0) != '+')
                octstr_insert_char(addr, 0, '+');
            
            break;
        case GSM_ADDR_TON_ALPHANUMERIC:
            if (octstr_len(addr) > 11) {
                /* alphanum sender, max. allowed length is 11 (according to GSM specs) */
                error(0, "SMPP[%s]: Malformed addr `%s', alphanumeric length greater 11 chars. ",
                      octstr_get_cstr(id),
                      octstr_get_cstr(addr));
                reason = SMPP_ESME_RINVSRCADR;
                goto error;
            }
            if (alt_addr_charset) {
                if (octstr_str_case_compare(alt_addr_charset, "gsm") == 0)
                    charset_gsm_to_utf8(addr);
                else if (charset_convert(addr, octstr_get_cstr(alt_addr_charset), SMPP_DEFAULT_CHARSET) != 0)
                    error(0, "Failed to convert address from charset <%s> to <%s>, leave as is.",
                          octstr_get_cstr(alt_addr_charset), SMPP_DEFAULT_CHARSET);
            }
            break;
        default: /* otherwise don't touch addr, user should handle it */
            break;
    }

error:
    return reason;
}



/*
 * Convert SMPP PDU to internal Msgs structure.
 * Return the Msg if all was fine and NULL otherwise, while getting
 * the failing reason delivered back in *reason.
 * XXX semantical check on the incoming values can be extended here.
 */
static Msg *pdu_to_msg(SMPP *smpp, SMPP_PDU *pdu, long *reason)
{
    Msg *msg;
    int ton, npi;

    gw_assert(pdu->type == deliver_sm);

    msg = msg_create(sms);
    gw_assert(msg != NULL);
    *reason = SMPP_ESME_ROK;

    /*
     * Reset source addr to have a prefixed '+' in case we have an
     * intl. TON to allow backend boxes (ie. smsbox) to distinguish
     * between national and international numbers.
     */
    ton = pdu->u.deliver_sm.source_addr_ton;
    npi = pdu->u.deliver_sm.source_addr_npi;
    /* check source addr */
    if ((*reason = convert_addr_from_pdu(smpp->conn->id, pdu->u.deliver_sm.source_addr, ton, npi, smpp->alt_addr_charset)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.sender = pdu->u.deliver_sm.source_addr;
    pdu->u.deliver_sm.source_addr = NULL;

    /*
     * Follows SMPP spec. v3.4. issue 1.2
     * it's not allowed to have destination_addr NULL
     */
    if (pdu->u.deliver_sm.destination_addr == NULL) {
        error(0, "SMPP[%s]: Malformed destination_addr `%s', may not be empty. "
              "Discarding MO message.", octstr_get_cstr(smpp->conn->id),
              octstr_get_cstr(pdu->u.deliver_sm.destination_addr));
        *reason = SMPP_ESME_RINVDSTADR;
        goto error;
    }

    /* Same reset of destination number as for source */
    ton = pdu->u.deliver_sm.dest_addr_ton;
    npi = pdu->u.deliver_sm.dest_addr_npi;
    /* check destination addr */
    if ((*reason = convert_addr_from_pdu(smpp->conn->id, pdu->u.deliver_sm.destination_addr, ton, npi, smpp->alt_addr_charset)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.receiver = pdu->u.deliver_sm.destination_addr;
    pdu->u.deliver_sm.destination_addr = NULL;

    /* SMSCs use service_type for billing information
     * According to SMPP v5.0 there is no 'billing_identification'
     * TLV in the deliver_sm PDU optional TLVs. */
    msg->sms.binfo = pdu->u.deliver_sm.service_type;
    pdu->u.deliver_sm.service_type = NULL;

    /* Foreign ID on MO */
    msg->sms.foreign_id = pdu->u.deliver_sm.receipted_message_id;
    pdu->u.deliver_sm.receipted_message_id = NULL;

    if (pdu->u.deliver_sm.esm_class & ESM_CLASS_SUBMIT_RPI)
        msg->sms.rpi = 1;

    /*
     * Check for message_payload if version > 0x33 and sm_length == 0
     * Note: SMPP spec. v3.4. doesn't allow to send both: message_payload & short_message!
     */
    if (smpp->version > 0x33 && pdu->u.deliver_sm.sm_length == 0 && pdu->u.deliver_sm.message_payload) {
        msg->sms.msgdata = pdu->u.deliver_sm.message_payload;
        pdu->u.deliver_sm.message_payload = NULL;
    }
    else {
        msg->sms.msgdata = pdu->u.deliver_sm.short_message;
        pdu->u.deliver_sm.short_message = NULL;
    }

    /* check sar_msg_ref_num, sar_segment_seqnum, sar_total_segments */
    if (smpp->version > 0x33 &&
    	pdu->u.deliver_sm.sar_msg_ref_num >= 0 && pdu->u.deliver_sm.sar_segment_seqnum > 0 && pdu->u.deliver_sm.sar_total_segments > 0) {
    	/*
    		For GSM networks, the concatenation related TLVs (sar_msg_ref_num, sar_total_segments, sar_segment_seqnum)
    		or port addressing related TLVs
    		(source_port, dest_port) cannot be used in conjunction with encoded User Data Header in the short_message
    		(user data) field. This means that the above listed TLVs cannot be used if the User Data Header Indicator flag is set.
    	*/
    	if (pdu->u.deliver_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
    		error(0, "SMPP[%s]: sar_msg_ref_num, sar_segment_seqnum, sar_total_segments in conjuction with UDHI used, rejected.",
    			  octstr_get_cstr(smpp->conn->id));
    		*reason = SMPP_ESME_RINVTLVVAL;
    		goto error;
    	}
    	/* create multipart UDH */
    	prepend_catenation_udh(msg,
    						   pdu->u.deliver_sm.sar_segment_seqnum,
    						   pdu->u.deliver_sm.sar_total_segments,
    						   pdu->u.deliver_sm.sar_msg_ref_num);
    }

    /*
     * Encode udh if udhi set
     * for reference see GSM03.40, section 9.2.3.24
     */
    if (pdu->u.deliver_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
        int udhl;
        udhl = octstr_get_char(msg->sms.msgdata, 0) + 1;
        debug("bb.sms.smpp",0,"SMPP[%s]: UDH length read as %d",
              octstr_get_cstr(smpp->conn->id), udhl);
        if (udhl > octstr_len(msg->sms.msgdata)) {
            error(0, "SMPP[%s]: Malformed UDH length indicator 0x%03x while message length "
                  "0x%03lx. Discarding MO message.", octstr_get_cstr(smpp->conn->id),
                  udhl, octstr_len(msg->sms.msgdata));
            *reason = SMPP_ESME_RINVESMCLASS;
            goto error;
        }
        msg->sms.udhdata = octstr_copy(msg->sms.msgdata, 0, udhl);
        octstr_delete(msg->sms.msgdata, 0, udhl);
    }

    dcs_to_fields(&msg, pdu->u.deliver_sm.data_coding);

    /* handle default data coding */
    switch (pdu->u.deliver_sm.data_coding) {
        case 0x00: /* default SMSC alphabet */
            /*
             * try to convert from something interesting if specified so
             * unless it was specified binary, i.e. UDH indicator was detected
             */
            if (smpp->alt_charset && msg->sms.coding != DC_8BIT) {
                if (charset_convert(msg->sms.msgdata, octstr_get_cstr(smpp->alt_charset), SMPP_DEFAULT_CHARSET) != 0)
                    error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
                          octstr_get_cstr(smpp->alt_charset), SMPP_DEFAULT_CHARSET);
                msg->sms.coding = DC_7BIT;
            } else { /* assume GSM 03.38 7-bit alphabet */
                charset_gsm_to_utf8(msg->sms.msgdata);
                msg->sms.coding = DC_7BIT;
            }
            break;
        case 0x01:
        	/* ASCII/IA5 - we don't need to perform any conversion
        	 * due that UTF-8's first range is exactly the ASCII table */
            msg->sms.coding = DC_7BIT; break;
        case 0x03: /* ISO-8859-1 - I'll convert to internal encoding */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-1", SMPP_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from ISO-8859-1 to " SMPP_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x02: /* 8 bit binary - do nothing */
        case 0x04: /* 8 bit binary - do nothing */
            msg->sms.coding = DC_8BIT; break;
        case 0x05: /* JIS - what do I do with that ? */
            break;
        case 0x06: /* Cyrllic - iso-8859-5, I'll convert to internal encoding */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-5", SMPP_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from cyrllic to " SMPP_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x07: /* Hebrew iso-8859-8, I'll convert to internal encoding */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-8", SMPP_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from hebrew to " SMPP_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x08: /* unicode UCS-2, yey */
            msg->sms.coding = DC_UCS2; break;

            /*
             * don't much care about the others,
             * you implement them if you feel like it
             */

        default:
            /*
             * some of smsc send with dcs from GSM 03.38 , but these are reserved in smpp spec.
             * So we just look decoded values from dcs_to_fields and if none there make our assumptions.
             * if we have an UDH indicator, we assume DC_8BIT.
             */
            if (msg->sms.coding == DC_UNDEF && (pdu->u.deliver_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR))
                msg->sms.coding = DC_8BIT;
            else if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) { /* assume GSM 7Bit , reencode */
                msg->sms.coding = DC_7BIT;
                charset_gsm_to_utf8(msg->sms.msgdata);
            }
    }
    msg->sms.pid = pdu->u.deliver_sm.protocol_id;

    /* set priority flag */
    msg->sms.priority = pdu->u.deliver_sm.priority_flag;

    if (msg->sms.meta_data == NULL)
        msg->sms.meta_data = octstr_create("");
    meta_data_set_values(msg->sms.meta_data, pdu->u.deliver_sm.tlv, "smpp", 1);

    return msg;

error:
    msg_destroy(msg);
    return NULL;
}


/*
 * Convert SMPP PDU to internal Msgs structure.
 * Return the Msg if all was fine and NULL otherwise, while getting
 * the failing reason delivered back in *reason.
 * XXX semantical check on the incoming values can be extended here.
 */
static Msg *data_sm_to_msg(SMPP *smpp, SMPP_PDU *pdu, long *reason)
{
    Msg *msg;
    int ton, npi;

    gw_assert(pdu->type == data_sm);

    msg = msg_create(sms);
    gw_assert(msg != NULL);
    *reason = SMPP_ESME_ROK;

    /*
     * Reset source addr to have a prefixed '+' in case we have an
     * intl. TON to allow backend boxes (ie. smsbox) to distinguish
     * between national and international numbers.
     */
    ton = pdu->u.data_sm.source_addr_ton;
    npi = pdu->u.data_sm.source_addr_npi;
    /* check source addr */
    if ((*reason = convert_addr_from_pdu(smpp->conn->id, pdu->u.data_sm.source_addr, ton, npi, smpp->alt_addr_charset)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.sender = pdu->u.data_sm.source_addr;
    pdu->u.data_sm.source_addr = NULL;

    /*
     * Follows SMPP spec. v3.4. issue 1.2
     * it's not allowed to have destination_addr NULL
     */
    if (pdu->u.data_sm.destination_addr == NULL) {
        error(0, "SMPP[%s]: Malformed destination_addr `%s', may not be empty. "
              "Discarding MO message.", octstr_get_cstr(smpp->conn->id),
              octstr_get_cstr(pdu->u.data_sm.destination_addr));
        *reason = SMPP_ESME_RINVDSTADR;
        goto error;
    }

    /* Same reset of destination number as for source */
    ton = pdu->u.data_sm.dest_addr_ton;
    npi = pdu->u.data_sm.dest_addr_npi;
    /* check destination addr */
    if ((*reason = convert_addr_from_pdu(smpp->conn->id, pdu->u.data_sm.destination_addr, ton, npi, smpp->alt_addr_charset)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.receiver = pdu->u.data_sm.destination_addr;
    pdu->u.data_sm.destination_addr = NULL;

    /* SMSCs use service_type for billing information */
    if (smpp->version == 0x50 && pdu->u.data_sm.billing_identification) {
    	msg->sms.binfo = pdu->u.data_sm.billing_identification;
    	pdu->u.data_sm.billing_identification = NULL;
    } else {
        msg->sms.binfo = pdu->u.data_sm.service_type;
        pdu->u.data_sm.service_type = NULL;
    }

    /* Foreign ID on MO */
    msg->sms.foreign_id = pdu->u.data_sm.receipted_message_id;
    pdu->u.data_sm.receipted_message_id = NULL;

    if (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_RPI)
        msg->sms.rpi = 1;

    msg->sms.msgdata = pdu->u.data_sm.message_payload;
    pdu->u.data_sm.message_payload = NULL;

    /* check sar_msg_ref_num, sar_segment_seqnum, sar_total_segments */
    if (pdu->u.data_sm.sar_msg_ref_num >= 0 && pdu->u.data_sm.sar_segment_seqnum > 0 && pdu->u.data_sm.sar_total_segments > 0) {
    	/*
    		For GSM networks, the concatenation related TLVs (sar_msg_ref_num, sar_total_segments, sar_segment_seqnum)
    		or port addressing related TLVs
    		(source_port, dest_port) cannot be used in conjunction with encoded User Data Header in the short_message
    		(user data) field. This means that the above listed TLVs cannot be used if the User Data Header Indicator flag is set.
    	*/
    	if (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
    		error(0, "SMPP[%s]: sar_msg_ref_num, sar_segment_seqnum, sar_total_segments in conjuction with UDHI used, rejected.",
    			  octstr_get_cstr(smpp->conn->id));
    		*reason = SMPP_ESME_RINVTLVVAL;
    		goto error;
    	}
    	/* create multipart UDH */
    	prepend_catenation_udh(msg,
    						   pdu->u.data_sm.sar_segment_seqnum,
    						   pdu->u.data_sm.sar_total_segments,
    						   pdu->u.data_sm.sar_msg_ref_num);
    }

    /*
     * Encode udh if udhi set
     * for reference see GSM03.40, section 9.2.3.24
     */
    if (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
        int udhl;
        udhl = octstr_get_char(msg->sms.msgdata, 0) + 1;
        debug("bb.sms.smpp",0,"SMPP[%s]: UDH length read as %d",
              octstr_get_cstr(smpp->conn->id), udhl);
        if (udhl > octstr_len(msg->sms.msgdata)) {
            error(0, "SMPP[%s]: Malformed UDH length indicator 0x%03x while message length "
                  "0x%03lx. Discarding MO message.", octstr_get_cstr(smpp->conn->id),
                  udhl, octstr_len(msg->sms.msgdata));
            *reason = SMPP_ESME_RINVESMCLASS;
            goto error;
        }
        msg->sms.udhdata = octstr_copy(msg->sms.msgdata, 0, udhl);
        octstr_delete(msg->sms.msgdata, 0, udhl);
    }

    dcs_to_fields(&msg, pdu->u.data_sm.data_coding);

    /* handle default data coding */
    switch (pdu->u.data_sm.data_coding) {
        case 0x00: /* default SMSC alphabet */
            /*
             * try to convert from something interesting if specified so
             * unless it was specified binary, i.e. UDH indicator was detected
             */
            if (smpp->alt_charset && msg->sms.coding != DC_8BIT) {
                if (charset_convert(msg->sms.msgdata, octstr_get_cstr(smpp->alt_charset), SMPP_DEFAULT_CHARSET) != 0)
                    error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
                          octstr_get_cstr(smpp->alt_charset), SMPP_DEFAULT_CHARSET);
                msg->sms.coding = DC_7BIT;
            } else { /* assume GSM 03.38 7-bit alphabet */
                charset_gsm_to_utf8(msg->sms.msgdata);
                msg->sms.coding = DC_7BIT;
            }
            break;
        case 0x01: /* ASCII or IA5 - not sure if I need to do anything */
            msg->sms.coding = DC_7BIT; break;
        case 0x03: /* ISO-8859-1 - I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-1", SMPP_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from ISO-8859-1 to " SMPP_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x02: /* 8 bit binary - do nothing */
        case 0x04: /* 8 bit binary - do nothing */
            msg->sms.coding = DC_8BIT; break;
        case 0x05: /* JIS - what do I do with that ? */
            break;
        case 0x06: /* Cyrllic - iso-8859-5, I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-5", SMPP_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from cyrllic to " SMPP_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x07: /* Hebrew iso-8859-8, I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-8", SMPP_DEFAULT_CHARSET) != 0)
                error(0, "Failed to convert msgdata from hebrew to " SMPP_DEFAULT_CHARSET ", will leave as is");
            msg->sms.coding = DC_7BIT; break;
        case 0x08: /* unicode UCS-2, yey */
            msg->sms.coding = DC_UCS2; break;

            /*
             * don't much care about the others,
             * you implement them if you feel like it
             */

        default:
            /*
             * some of smsc send with dcs from GSM 03.38 , but these are reserved in smpp spec.
             * So we just look decoded values from dcs_to_fields and if none there make our assumptions.
             * if we have an UDH indicator, we assume DC_8BIT.
             */
            if (msg->sms.coding == DC_UNDEF && (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR))
                msg->sms.coding = DC_8BIT;
            else if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) { /* assume GSM 7Bit , reencode */
                msg->sms.coding = DC_7BIT;
                charset_gsm_to_utf8(msg->sms.msgdata);
            }
    }

    if (msg->sms.meta_data == NULL)
        msg->sms.meta_data = octstr_create("");
    meta_data_set_values(msg->sms.meta_data, pdu->u.data_sm.tlv, "smpp", 1);

    return msg;

error:
    msg_destroy(msg);
    return NULL;
}



static long smpp_status_to_smscconn_failure_reason(long status)
{
    switch(status) {
        case SMPP_ESME_RMSGQFUL:
        case SMPP_ESME_RTHROTTLED:
        case SMPP_ESME_RX_T_APPN:
        case SMPP_ESME_RSYSERR:
            return SMSCCONN_FAILED_TEMPORARILY;
            break;

        default:
            return SMSCCONN_FAILED_REJECTED;
    }
}


static SMPP_PDU *msg_to_pdu(SMPP *smpp, Msg *msg)
{
    SMPP_PDU *pdu;
    int validity;

    pdu = smpp_pdu_create(submit_sm,
                          counter_increase(smpp->message_id_counter));

    pdu->u.submit_sm.source_addr = octstr_duplicate(msg->sms.sender);
    pdu->u.submit_sm.destination_addr = octstr_duplicate(msg->sms.receiver);

    /* Set the service type of the outgoing message. We'll use the config
     * directive as default and 'binfo' as specific parameter. */
    if (octstr_len(msg->sms.binfo)) {
        /* SMPP v5.0 has an own TLV for billing information */
    	if (smpp->version == 0x50) {
    		pdu->u.submit_sm.billing_identification = octstr_duplicate(msg->sms.binfo);
    	} else {
        	pdu->u.submit_sm.service_type = octstr_duplicate(msg->sms.binfo);
    	}
    } else {
    	pdu->u.submit_sm.service_type = octstr_duplicate(smpp->service_type);
    }

    /* Check for manual override of source ton and npi values */
    if (smpp->source_addr_ton > -1 && smpp->source_addr_npi > -1) {
        pdu->u.submit_sm.source_addr_ton = smpp->source_addr_ton;
        pdu->u.submit_sm.source_addr_npi = smpp->source_addr_npi;
        debug("bb.sms.smpp", 0, "SMPP[%s]: Manually forced source addr ton = %d, source add npi = %d",
              octstr_get_cstr(smpp->conn->id), smpp->source_addr_ton,
              smpp->source_addr_npi);
    } else {
        /* setup default values */
        pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */
        pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */
    }

    if (pdu->u.submit_sm.source_addr && smpp->autodetect_addr) {
        /* lets see if its international or alphanumeric sender */
        if (octstr_get_char(pdu->u.submit_sm.source_addr, 0) == '+') {
            if (!octstr_check_range(pdu->u.submit_sm.source_addr, 1, 256, gw_isdigit)) {
                pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC; /* alphanum */
                pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN;    /* short code */
                if (smpp->alt_addr_charset) {
                    if (octstr_str_case_compare(smpp->alt_addr_charset, "gsm") == 0) {
                        /* @ would break PDU if converted into GSM*/
                        octstr_replace(pdu->u.submit_sm.source_addr, octstr_imm("@"), octstr_imm("?"));
                        charset_utf8_to_gsm(pdu->u.submit_sm.source_addr);
                    } else if (charset_convert(pdu->u.submit_sm.source_addr, SMPP_DEFAULT_CHARSET, octstr_get_cstr(smpp->alt_addr_charset)) != 0)
                        error(0, "Failed to convert source_addr from charset <%s> to <%s>, will send as is.",
                                SMPP_DEFAULT_CHARSET, octstr_get_cstr(smpp->alt_addr_charset));
                }
            } else {
               /* numeric sender address with + in front -> international (remove the +) */
               octstr_delete(pdu->u.submit_sm.source_addr, 0, 1);
               pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_INTERNATIONAL;
            }
        } else {
            if (!octstr_check_range(pdu->u.submit_sm.source_addr,0, 256, gw_isdigit)) {
                pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC;
                pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN;
                if (smpp->alt_addr_charset) {
                    if (octstr_str_case_compare(smpp->alt_addr_charset, "gsm") == 0) {
                        /* @ would break PDU if converted into GSM */
                        octstr_replace(pdu->u.submit_sm.source_addr, octstr_imm("@"), octstr_imm("?"));
                        charset_utf8_to_gsm(pdu->u.submit_sm.source_addr);
                    } else if (charset_convert(pdu->u.submit_sm.source_addr, SMPP_DEFAULT_CHARSET, octstr_get_cstr(smpp->alt_addr_charset)) != 0)
                        error(0, "Failed to convert source_addr from charset <%s> to <%s>, will send as is.",
                                SMPP_DEFAULT_CHARSET, octstr_get_cstr(smpp->alt_addr_charset));
                }
            }
        }
    }

    /* Check for manual override of destination ton and npi values */
    if (smpp->dest_addr_ton > -1 && smpp->dest_addr_npi > -1) {
        pdu->u.submit_sm.dest_addr_ton = smpp->dest_addr_ton;
        pdu->u.submit_sm.dest_addr_npi = smpp->dest_addr_npi;
        debug("bb.sms.smpp", 0, "SMPP[%s]: Manually forced dest addr ton = %d, dest add npi = %d",
              octstr_get_cstr(smpp->conn->id), smpp->dest_addr_ton,
              smpp->dest_addr_npi);
    } else {
        pdu->u.submit_sm.dest_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */
        pdu->u.submit_sm.dest_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */
    }

    /*
     * if its a international number starting with +, lets remove the
     * '+' and set number type to international instead
     */
    if (octstr_get_char(pdu->u.submit_sm.destination_addr,0) == '+') {
        octstr_delete(pdu->u.submit_sm.destination_addr, 0,1);
        pdu->u.submit_sm.dest_addr_ton = GSM_ADDR_TON_INTERNATIONAL;
    }

    /* check length of src/dst address */
    if (octstr_len(pdu->u.submit_sm.destination_addr) > 20 ||
        octstr_len(pdu->u.submit_sm.source_addr) > 20) {
        smpp_pdu_destroy(pdu);
        return NULL;
    }

    /*
     * set the data coding scheme (DCS) field
     * check if we have a forced value for this from the smsc-group.
     * Note: if message class is set, then we _must_ force alt_dcs otherwise
     * dcs has reserved values (e.g. mclass=2, dcs=0x11). We check MWI flag
     * first here, because MWI and MCLASS can not be set at the same time and
     * function fields_to_dcs check MWI first, so we have no need to force alt_dcs
     * if MWI is set.
     */
    if (msg->sms.mwi == MWI_UNDEF && msg->sms.mclass != MC_UNDEF)
        pdu->u.submit_sm.data_coding = fields_to_dcs(msg, 1); /* force alt_dcs */
    else
        pdu->u.submit_sm.data_coding = fields_to_dcs(msg,
            (msg->sms.alt_dcs != SMS_PARAM_UNDEFINED ?
             msg->sms.alt_dcs : smpp->conn->alt_dcs));

    /* set protocol id */
    if (msg->sms.pid != SMS_PARAM_UNDEFINED)
        pdu->u.submit_sm.protocol_id = msg->sms.pid;

    /*
     * set the esm_class field
     * default is store and forward, plus udh and rpi if requested
     */
    pdu->u.submit_sm.esm_class = smpp->esm_class;
    if (octstr_len(msg->sms.udhdata))
        pdu->u.submit_sm.esm_class = pdu->u.submit_sm.esm_class |
            ESM_CLASS_SUBMIT_UDH_INDICATOR;
    if (msg->sms.rpi > 0)
        pdu->u.submit_sm.esm_class = pdu->u.submit_sm.esm_class |
            ESM_CLASS_SUBMIT_RPI;

    /*
     * set data segments and length
     */

    pdu->u.submit_sm.short_message = octstr_duplicate(msg->sms.msgdata);

    /*
     * only re-encoding if using default smsc charset that is defined via
     * alt-charset in smsc group and if MT is not binary
     */
    if (msg->sms.coding == DC_7BIT || (msg->sms.coding == DC_UNDEF && octstr_len(msg->sms.udhdata) == 0)) {
        /*
         * consider 3 cases:
         *  a) data_coding 0xFX: encoding should always be GSM 03.38 charset
         *  b) data_coding 0x00: encoding may be converted according to alt-charset
         *  c) data_coding 0x00: assume GSM 03.38 charset if alt-charset is not defined
         */
        if ((pdu->u.submit_sm.data_coding & 0xF0) ||
            (pdu->u.submit_sm.data_coding == 0 && !smpp->alt_charset)) {
            charset_utf8_to_gsm(pdu->u.submit_sm.short_message);
        } else if (pdu->u.submit_sm.data_coding == 0 && smpp->alt_charset) {
            /*
             * convert to the given alternative charset
             */
            if (charset_convert(pdu->u.submit_sm.short_message, SMPP_DEFAULT_CHARSET,
                                octstr_get_cstr(smpp->alt_charset)) != 0)
                error(0, "Failed to convert msgdata from charset <%s> to <%s>, will send as is.",
                             SMPP_DEFAULT_CHARSET, octstr_get_cstr(smpp->alt_charset));
        }
    }

    /* prepend udh if present */
    if (octstr_len(msg->sms.udhdata)) {
        octstr_insert(pdu->u.submit_sm.short_message, msg->sms.udhdata, 0);
    }

    pdu->u.submit_sm.sm_length = octstr_len(pdu->u.submit_sm.short_message);

    /*
     * check for validity and deferred settings
     * were message value has higher priority then smsc config group value
     * Note: we always send in UTC and just define "Time Difference" as 00 and
     *       direction '+'.
     */
    validity = SMS_PARAM_UNDEFINED;
    if (msg->sms.validity != SMS_PARAM_UNDEFINED)
    	validity = msg->sms.validity;
    else if (smpp->validityperiod != SMS_PARAM_UNDEFINED)
    	validity = time(NULL) + smpp->validityperiod * 60;
    if (validity != SMS_PARAM_UNDEFINED) {
        struct tm tm = gw_gmtime(validity);
        pdu->u.submit_sm.validity_period = octstr_format("%02d%02d%02d%02d%02d%02d000+",
                tm.tm_year % 100, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    if (msg->sms.deferred != SMS_PARAM_UNDEFINED) {
        struct tm tm = gw_gmtime(msg->sms.deferred);
        pdu->u.submit_sm.schedule_delivery_time = octstr_format("%02d%02d%02d%02d%02d%02d000+",
                tm.tm_year % 100, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    /* ask for the delivery reports if needed */
    if (DLR_IS_FAIL(msg->sms.dlr_mask) && !DLR_IS_SUCCESS(msg->sms.dlr_mask))
        pdu->u.submit_sm.registered_delivery = 2;
    else if (DLR_IS_SUCCESS_OR_FAIL(msg->sms.dlr_mask))
        pdu->u.submit_sm.registered_delivery = 1;

    if (DLR_IS_INTERMEDIATE(msg->sms.dlr_mask))
        pdu->u.submit_sm.registered_delivery += 16;

    /* set priority */
    if (msg->sms.priority >= 0 && msg->sms.priority <= 3)
        pdu->u.submit_sm.priority_flag = msg->sms.priority;
    else
        pdu->u.submit_sm.priority_flag = smpp->priority;

    /* set more messages to send */
    if (smpp->version > 0x33 && msg->sms.msg_left > 0)
        pdu->u.submit_sm.more_messages_to_send = 1;

    dict_destroy(pdu->u.submit_sm.tlv);
    pdu->u.submit_sm.tlv = meta_data_get_values(msg->sms.meta_data, "smpp");

	return pdu;
}


static int send_enquire_link(SMPP *smpp, Connection *conn, long *last_sent)
{
    SMPP_PDU *pdu;
    Octstr *os;
    int ret;

    if (difftime(date_universal_now(),*last_sent) < smpp->enquire_link_interval)
        return 0;
    *last_sent = date_universal_now();

    pdu = smpp_pdu_create(enquire_link, counter_increase(smpp->message_id_counter));
    dump_pdu("Sending enquire link:", smpp->conn->id, pdu);
    os = smpp_pdu_pack(smpp->conn->id, pdu);
    if (os != NULL)
        ret = conn_write(conn, os); /* Write errors checked by caller. */
    else
        ret = -1;
    octstr_destroy(os);
    smpp_pdu_destroy(pdu);

    return ret;
}

static int send_gnack(SMPP *smpp, Connection *conn, long reason, unsigned long seq_num)
{
    SMPP_PDU *pdu;
    Octstr *os;
    int ret;

    pdu = smpp_pdu_create(generic_nack, seq_num);
    pdu->u.generic_nack.command_status = reason;
    dump_pdu("Sending generic_nack:", smpp->conn->id, pdu);
    os = smpp_pdu_pack(smpp->conn->id, pdu);
    if (os != NULL)
        ret = conn_write(conn, os);
    else
        ret = -1;
    octstr_destroy(os);
    smpp_pdu_destroy(pdu);

    return ret;
}

static int send_unbind(SMPP *smpp, Connection *conn)
{
    SMPP_PDU *pdu;
    Octstr *os;
    int ret;

    pdu = smpp_pdu_create(unbind, counter_increase(smpp->message_id_counter));
    dump_pdu("Sending unbind:", smpp->conn->id, pdu);
    os = smpp_pdu_pack(smpp->conn->id, pdu);
    if (os != NULL)
        ret = conn_write(conn, os);
    else
        ret = -1;
    octstr_destroy(os);
    smpp_pdu_destroy(pdu);

    return ret;
}


static int send_pdu(Connection *conn, Octstr *id, SMPP_PDU *pdu)
{
    Octstr *os;
    int ret;

    dump_pdu("Sending PDU:", id, pdu);
    os = smpp_pdu_pack(id, pdu);
    if (os) {
        /* Caller checks for write errors later */
        ret = conn_write(conn, os);
        /* it's not a error if we still have data buffered */
        ret = (ret == 1) ? 0 : ret;
    } else
        ret = -1;
    octstr_destroy(os);
    return ret;
}


static int send_messages(SMPP *smpp, Connection *conn, long *pending_submits)
{
    Msg *msg;
    SMPP_PDU *pdu;
    Octstr *os;

    if (*pending_submits == -1)
        return 0;

    while (*pending_submits < smpp->max_pending_submits) {
        /* check our throughput */
        if (smpp->conn->throughput > 0 && load_get(smpp->load, 0) >= smpp->conn->throughput) {
            debug("bb.sms.smpp", 0, "SMPP[%s]: throughput limit exceeded (%.02f,%.02f)",
                  octstr_get_cstr(smpp->conn->id), load_get(smpp->load, 0), smpp->conn->throughput);
            break;
        }
        debug("bb.sms.smpp", 0, "SMPP[%s]: throughput (%.02f,%.02f)",
              octstr_get_cstr(smpp->conn->id), load_get(smpp->load, 0), smpp->conn->throughput);

        /* Get next message, quit if none to be sent */
        msg = gw_prioqueue_remove(smpp->msgs_to_send);
        if (msg == NULL)
            break;

        /* Send PDU, record it as waiting for ack from SMS center */
        pdu = msg_to_pdu(smpp, msg);
        if (pdu == NULL) {
            bb_smscconn_send_failed(smpp->conn, msg, SMSCCONN_FAILED_MALFORMED, octstr_create("MALFORMED SMS"));
            continue;
        }
        /* check for write errors */
        if (send_pdu(conn, smpp->conn->id, pdu) == 0) {
            struct smpp_msg *smpp_msg = smpp_msg_create(msg);
            os = octstr_format("%ld", pdu->u.submit_sm.sequence_number);
            dict_put(smpp->sent_msgs, os, smpp_msg);
            smpp_pdu_destroy(pdu);
            octstr_destroy(os);
            ++(*pending_submits);
            load_increase(smpp->load);
        }
        else { /* write error occurs */
            smpp_pdu_destroy(pdu);
            bb_smscconn_send_failed(smpp->conn, msg, SMSCCONN_FAILED_TEMPORARILY, NULL);
            return -1;
        }
    }

    return 0;
}


/*
 * Open transmission connection to SMS center. Return NULL for error,
 * open Connection for OK. Caller must set smpp->conn->status correctly
 * before calling this.
 */
static Connection *open_transmitter(SMPP *smpp)
{
    SMPP_PDU *bind;
    Connection *conn;

#ifdef HAVE_LIBSSL
    if (smpp->use_ssl)
        conn = conn_open_ssl(smpp->host, smpp->transmit_port, smpp->ssl_client_certkey_file, smpp->conn->our_host);
    else
#endif

    if (smpp->our_port > 0)
        conn = conn_open_tcp_with_port(smpp->host, smpp->transmit_port, smpp->our_port, smpp->conn->our_host );
    else
        conn = conn_open_tcp(smpp->host, smpp->transmit_port, smpp->conn->our_host);

    if (conn == NULL) {
        error(0, "SMPP[%s]: Couldn't connect to server.",
              octstr_get_cstr(smpp->conn->id));
        return NULL;
    }

    bind = smpp_pdu_create(bind_transmitter,
                counter_increase(smpp->message_id_counter));
    bind->u.bind_transmitter.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_transmitter.password = octstr_duplicate(smpp->password);
    if (smpp->system_type == NULL)
        bind->u.bind_transmitter.system_type = octstr_create("VMA");
    else
        bind->u.bind_transmitter.system_type =
            octstr_duplicate(smpp->system_type);
    bind->u.bind_transmitter.interface_version = smpp->version;
    bind->u.bind_transmitter.address_range =
        octstr_duplicate(smpp->address_range);
    bind->u.bind_transmitter.addr_ton = smpp->bind_addr_ton;
    bind->u.bind_transmitter.addr_npi = smpp->bind_addr_npi;
    if (send_pdu(conn, smpp->conn->id, bind) == -1) {
        error(0, "SMPP[%s]: Couldn't send bind_transmitter to server.",
              octstr_get_cstr(smpp->conn->id));
        conn_destroy(conn);
        conn = NULL;
    }
    smpp_pdu_destroy(bind);

    return conn;
}


/*
 * Open transceiver connection to SMS center. Return NULL for error,
 * open Connection for OK. Caller must set smpp->conn->status correctly
 * before calling this.
 */
static Connection *open_transceiver(SMPP *smpp)
{
    SMPP_PDU *bind;
    Connection *conn;

#ifdef HAVE_LIBSSL
    if (smpp->use_ssl)
        conn = conn_open_ssl(smpp->host, smpp->transmit_port, smpp->ssl_client_certkey_file, smpp->conn->our_host);
    else
#endif

    if (smpp->our_port > 0)
       conn = conn_open_tcp_with_port(smpp->host, smpp->transmit_port, smpp->our_port, smpp->conn->our_host );  
    else
        conn = conn_open_tcp(smpp->host, smpp->transmit_port, smpp->conn->our_host);

    if (conn == NULL) {
       error(0, "SMPP[%s]: Couldn't connect to server.",
             octstr_get_cstr(smpp->conn->id));
       return NULL;
    }

    bind = smpp_pdu_create(bind_transceiver,
                           counter_increase(smpp->message_id_counter));
    bind->u.bind_transceiver.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_transceiver.password = octstr_duplicate(smpp->password);
    if (smpp->system_type == NULL)
        bind->u.bind_transceiver.system_type = octstr_create("VMA");
    else
        bind->u.bind_transceiver.system_type = octstr_duplicate(smpp->system_type);
    bind->u.bind_transceiver.interface_version = smpp->version;
    bind->u.bind_transceiver.address_range = octstr_duplicate(smpp->address_range);
    bind->u.bind_transceiver.addr_ton = smpp->bind_addr_ton;
    bind->u.bind_transceiver.addr_npi = smpp->bind_addr_npi;
    if (send_pdu(conn, smpp->conn->id, bind) == -1) {
        error(0, "SMPP[%s]: Couldn't send bind_transceiver to server.",
              octstr_get_cstr(smpp->conn->id));
        conn_destroy(conn);
        conn = NULL;
    }
    smpp_pdu_destroy(bind);

    return conn;
}


/*
 * Open reception connection to SMS center. Return NULL for error,
 * open Connection for OK. Caller must set smpp->conn->status correctly
 * before calling this.
 */
static Connection *open_receiver(SMPP *smpp)
{
    SMPP_PDU *bind;
    Connection *conn;

#ifdef HAVE_LIBSSL
    if (smpp->use_ssl)
        conn = conn_open_ssl(smpp->host, smpp->receive_port, smpp->ssl_client_certkey_file, smpp->conn->our_host);
    else
#endif

    if (smpp->our_receiver_port > 0)
        conn = conn_open_tcp_with_port(smpp->host, smpp->receive_port, smpp->our_receiver_port, smpp->conn->our_host);
    else
        conn = conn_open_tcp(smpp->host, smpp->receive_port, smpp->conn->our_host);

    if (conn == NULL) {
        error(0, "SMPP[%s]: Couldn't connect to server.",
              octstr_get_cstr(smpp->conn->id));
        return NULL;
    }

    bind = smpp_pdu_create(bind_receiver,
                counter_increase(smpp->message_id_counter));
    bind->u.bind_receiver.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_receiver.password = octstr_duplicate(smpp->password);
    if (smpp->system_type == NULL)
        bind->u.bind_receiver.system_type = octstr_create("VMA");
    else
        bind->u.bind_receiver.system_type =
            octstr_duplicate(smpp->system_type);
    bind->u.bind_receiver.interface_version = smpp->version;
    bind->u.bind_receiver.address_range =
        octstr_duplicate(smpp->address_range);
    bind->u.bind_receiver.addr_ton = smpp->bind_addr_ton;
    bind->u.bind_receiver.addr_npi = smpp->bind_addr_npi;
    if (send_pdu(conn, smpp->conn->id, bind) == -1) {
        error(0, "SMPP[%s]: Couldn't send bind_receiver to server.",
              octstr_get_cstr(smpp->conn->id));
        conn_destroy(conn);
        conn = NULL;
    }
    smpp_pdu_destroy(bind);

    return conn;
}


/* 
 * See SMPP v5.0 spec [http://www.smsforum.net/smppv50.pdf.zip],
 * section 4.8.4.42 network_error_code for correct encoding. 
 */
static int error_from_network_error_code(Octstr *network_error_code)
{
    unsigned char *nec;
    int type;
    int err;
    
    if (network_error_code == NULL || octstr_len(network_error_code) != 3)
        return 0;
    
    nec = (unsigned char*) octstr_get_cstr(network_error_code);
    type = nec[0];
    err = (nec[1] << 8) | nec[2];

    if ((type >= '0') && (type <= '9')) {
        /* this is a bogous SMSC sending back network_error_code as 
         * 3 digit string instead as in the delivery report. */
        sscanf((char*) nec, "%03d", &err);
        return err;
    }
    
    return err;
}


static Msg *handle_dlr(SMPP *smpp, Octstr *destination_addr, Octstr *short_message, Octstr *message_payload, Octstr *receipted_message_id, long message_state, Octstr *network_error_code)
{
    Msg *dlrmsg = NULL;
    Octstr *respstr = NULL, *msgid = NULL, *network_err = NULL, *dlr_err = NULL, *tmp;
    int dlrstat = -1;
    int err_int = 0;

    /* first check for SMPP v3.4 and above */
    if (smpp->version > 0x33 && receipted_message_id) {
        msgid = octstr_duplicate(receipted_message_id);
        switch(message_state) {
        case 0: /* SCHEDULED, defined in SMPP v5.0, sec. 4.7.15, page 127 */
        	if (smpp->version == 0x50)	/* being very pedantic here */
                dlrstat = DLR_BUFFERED;
        	break;
        case 1: /* ENROUTE */
        case 6: /* ACCEPTED */
            dlrstat = DLR_BUFFERED;
            break;
        case 2: /* DELIVERED */
            dlrstat = DLR_SUCCESS;
            break;
        case 3: /* EXPIRED */
        case 4: /* DELETED */
        case 5: /* UNDELIVERABLE */
        case 7: /* UNKNOWN */
        case 8: /* REJECTED */
            dlrstat = DLR_FAIL;
            break;
        case 9: /* SKIPPED, defined in SMPP v5.0, sec. 4.7.15, page 127 */
        	if (smpp->version == 0x50)
        		dlrstat = DLR_FAIL;
        	break;
        case -1: /* message state is not present, partial SMPP v3.4 */
            debug("bb.sms.smpp", 0, "SMPP[%s]: Partial SMPP v3.4, receipted_message_id present but not message_state.",
                    octstr_get_cstr(smpp->conn->id));
            dlrstat = -1;
            break;
        default:
            warning(0, "SMPP[%s]: Got DLR with unknown 'message_state' (%ld).",
                octstr_get_cstr(smpp->conn->id), message_state);
            dlrstat = DLR_FAIL;
            break;
        }
    }

    if (network_error_code != NULL) {
        err_int = error_from_network_error_code(network_error_code);
        network_err = octstr_duplicate(network_error_code);
    }
    
    /* check for SMPP v.3.4. and message_payload */
    if (smpp->version > 0x33 && octstr_len(short_message) == 0)
        respstr = message_payload;
    else
        respstr = short_message;

    if (msgid == NULL || network_err == NULL || dlrstat == -1) {
        /* parse the respstr if it exists */
        if (respstr) {
            long curr = 0, vpos = 0;
            Octstr *stat = NULL;
            char id_cstr[65], stat_cstr[16], sub_d_cstr[15], done_d_cstr[15];
            char err_cstr[4];
            int sub, dlrvrd, ret;

            /* get server message id */
            /* first try sscanf way if thus failed then old way */
            ret = sscanf(octstr_get_cstr(respstr),
                         "id:%64[^ ] sub:%d dlvrd:%d submit date:%14[0-9] done "
                         "date:%14[0-9] stat:%15[^ ] err:%3[^ ]",
                         id_cstr, &sub, &dlrvrd, sub_d_cstr, done_d_cstr,
                         stat_cstr, err_cstr);
            if (ret == 7) {
                /* only if not already here */
                if (msgid == NULL) {
                    msgid = octstr_create(id_cstr);
                    octstr_strip_blanks(msgid);
                }
                stat = octstr_create(stat_cstr);
                octstr_strip_blanks(stat);
                sscanf(err_cstr, "%d", &err_int);
                dlr_err = octstr_create(err_cstr);
                octstr_strip_blanks(dlr_err);
            } else {
                debug("bb.sms.smpp", 0, "SMPP[%s]: Couldnot parse DLR string sscanf way,"
                      "fallback to old way. Please report!", octstr_get_cstr(smpp->conn->id));

                /* only if not already here */
                if (msgid == NULL) {
                    if ((curr = octstr_search(respstr, octstr_imm("id:"), 0)) != -1) {
                        vpos = octstr_search_char(respstr, ' ', curr);
                        if ((vpos-curr >0) && (vpos != -1))
                            msgid = octstr_copy(respstr, curr+3, vpos-curr-3);
                    } else {
                        msgid = NULL;
                    }
                }

                /* get err & status code */
                if ((curr = octstr_search(respstr, octstr_imm("stat:"), 0)) != -1) {
                    vpos = octstr_search_char(respstr, ' ', curr);
                    if ((vpos-curr >0) && (vpos != -1))
                        stat = octstr_copy(respstr, curr+5, vpos-curr-5);
                } else {
                    stat = NULL;
                }
                if ((curr = octstr_search(respstr, octstr_imm("err:"), 0)) != -1) {
                    vpos = octstr_search_char(respstr, ' ', curr);
                    if ((vpos-curr >0) && (vpos != -1))
                        dlr_err = octstr_copy(respstr, curr+4, vpos-curr-4);
                } else {
                    dlr_err = NULL;
                }
            }

            /*
             * we get the following status:
             * DELIVRD, ACCEPTD, EXPIRED, DELETED, UNDELIV, UNKNOWN, REJECTD
             *
             * Note: some buggy SMSC's send us immediately delivery notifications although
             *          we doesn't requested these.
             */
            if (dlrstat == -1) {
                if (stat != NULL && octstr_compare(stat, octstr_imm("DELIVRD")) == 0)
                    dlrstat = DLR_SUCCESS;
                else if (stat != NULL && (octstr_compare(stat, octstr_imm("ACCEPTD")) == 0 ||
                                octstr_compare(stat, octstr_imm("ACKED")) == 0 ||
                                octstr_compare(stat, octstr_imm("BUFFRED")) == 0 ||
                                octstr_compare(stat, octstr_imm("BUFFERD")) == 0 ||
                                octstr_compare(stat, octstr_imm("ENROUTE")) == 0))
                    dlrstat = DLR_BUFFERED;
                else
                    dlrstat = DLR_FAIL;
            }
            octstr_destroy(stat);
        }
    }

    if (msgid != NULL && dlrstat != -1) {
        /*
         * Obey which SMPP msg_id type this SMSC is using, where we
         * have the following semantics for the variable smpp_msg_id:
         *
         * bit 1: type for submit_sm_resp, bit 2: type for deliver_sm
         *
         * if bit is set value is hex otherwise dec
         *
         * 0x00 deliver_sm dec, submit_sm_resp dec
         * 0x01 deliver_sm dec, submit_sm_resp hex
         * 0x02 deliver_sm hex, submit_sm_resp dec
         * 0x03 deliver_sm hex, submit_sm_resp hex
         *
         * Default behaviour is SMPP spec compliant, which means
         * msg_ids should be C strings and hence non modified.
         */
        if (smpp->smpp_msg_id_type == -1) {
            /* the default, C string */
            tmp = octstr_duplicate(msgid);
        } else {
            if ((smpp->smpp_msg_id_type & 0x02) ||
                (!octstr_check_range(msgid, 0, octstr_len(msgid), gw_isdigit))) {
                tmp = octstr_format("%llu", strtoll(octstr_get_cstr(msgid), NULL, 16));
            } else {
                tmp = octstr_format("%llu", strtoll(octstr_get_cstr(msgid), NULL, 10));
            }
        }

        dlrmsg = dlr_find(smpp->conn->id,
            tmp, /* smsc message id */
            destination_addr, /* destination */
            dlrstat, 0);

        octstr_destroy(msgid);
    } else
        tmp = octstr_create("");

    if (network_err == NULL && dlr_err != NULL) {
        unsigned char ctmp[3];
        
        ctmp[0] = 3; /* we assume here its a GSM error due to lack of other information */
        ctmp[1] = (err_int >> 8) & 0xFF;
        ctmp[2] = (err_int & 0xFF);
        network_err = octstr_create_from_data((char*)ctmp, 3);
    }
    
    if (dlrmsg != NULL) {
        /*
         * we found the delivery report in our storage, so recode the
         * message structure.
         * The DLR trigger URL is indicated by msg->sms.dlr_url.
         * Add the DLR error code to meta-data.
         */
        dlrmsg->sms.msgdata = octstr_duplicate(respstr);
        dlrmsg->sms.sms_type = report_mo;
        dlrmsg->sms.account = octstr_duplicate(smpp->username);
        if (network_err != NULL) {
            if (dlrmsg->sms.meta_data == NULL) {
                dlrmsg->sms.meta_data = octstr_create("");
            }
            meta_data_set_value(dlrmsg->sms.meta_data, "smpp", octstr_imm("dlr_err"), network_err, 1);
        }
    } else {
        error(0,"SMPP[%s]: got DLR but could not find message or was not interested "
                "in it id<%s> dst<%s>, type<%d>",
                octstr_get_cstr(smpp->conn->id), octstr_get_cstr(tmp),
                octstr_get_cstr(destination_addr), dlrstat);
    }
    octstr_destroy(tmp);
    octstr_destroy(network_err);
    octstr_destroy(dlr_err);

    return dlrmsg;
}


static long smscconn_failure_reason_to_smpp_status(long reason)
{
    switch (reason) {
    case SMSCCONN_FAILED_REJECTED:
        return SMPP_ESME_RX_R_APPN;
    case SMSCCONN_SUCCESS:
        return SMPP_ESME_ROK;
    case SMSCCONN_FAILED_QFULL:
    case SMSCCONN_FAILED_TEMPORARILY:
        return SMPP_ESME_RX_T_APPN;
    }
    return SMPP_ESME_RX_T_APPN;
}


static int handle_pdu(SMPP *smpp, Connection *conn, SMPP_PDU *pdu,
                      long *pending_submits)
{
    SMPP_PDU *resp = NULL;
    Octstr *os;
    Msg *msg = NULL, *dlrmsg=NULL;
    struct smpp_msg *smpp_msg = NULL;
    long reason, cmd_stat;
    int ret = 0;

    /*
     * In order to keep the protocol implementation logically clean,
     * we will obey the required SMPP session state while processing
     * the PDUs, see Table 2-1, SMPP v3.4 spec, section 2.3, page 17.
     * Therefore we will interpret our abstracted smpp->conn->status
     * value as SMPP session state here.
     */
    switch (pdu->type) {
        case data_sm:
            /*
             * Session state check
             */
            if (!(smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV)) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session not bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }
            resp = smpp_pdu_create(data_sm_resp, pdu->u.data_sm.sequence_number);
            /*
             * If SMSCConn stopped then send temp. error code
             */
            mutex_lock(smpp->conn->flow_mutex);
            if (smpp->conn->is_stopped) {
                mutex_unlock(smpp->conn->flow_mutex);
                resp->u.data_sm_resp.command_status = SMPP_ESME_RX_T_APPN;
                break;
            }
            mutex_unlock(smpp->conn->flow_mutex);
            /* got a deliver ack (DLR)?
             * NOTE: following SMPP v3.4. spec. we are interested
             *       only on bits 2-5 (some SMSC's send 0x44, and it's
             *       spec. conforme)
             */
            if (pdu->u.data_sm.esm_class & (0x04|0x08|0x20)) {
                 debug("bb.sms.smpp",0,"SMPP[%s] handle_pdu, got DLR",
                       octstr_get_cstr(smpp->conn->id));
                 dlrmsg = handle_dlr(smpp, pdu->u.data_sm.source_addr, NULL, pdu->u.data_sm.message_payload,
                                     pdu->u.data_sm.receipted_message_id, pdu->u.data_sm.message_state, pdu->u.data_sm.network_error_code);
                 if (dlrmsg != NULL) {
                     if (dlrmsg->sms.meta_data == NULL)
                         dlrmsg->sms.meta_data = octstr_create("");
                     meta_data_set_values(dlrmsg->sms.meta_data, pdu->u.data_sm.tlv, "smpp", 0);
                     /* passing DLR to upper layer */
                     reason = bb_smscconn_receive(smpp->conn, dlrmsg);
                 } else {
                     /* no DLR will be passed, but we write an access-log entry */
                     reason = SMSCCONN_SUCCESS;
                     msg = data_sm_to_msg(smpp, pdu, &reason);
                     bb_alog_sms(smpp->conn, msg, "FAILED DLR SMS");
                     msg_destroy(msg);
                 }
                 resp->u.data_sm_resp.command_status = smscconn_failure_reason_to_smpp_status(reason);
            } else { /* MO message */
                 msg = data_sm_to_msg(smpp, pdu, &reason);
                 if (msg == NULL || reason != SMPP_ESME_ROK) {
                     resp->u.data_sm_resp.command_status = reason;
                     break;
                 }
                 /* Replace MO destination number with my-number */
                 if (octstr_len(smpp->my_number)) {
                     octstr_destroy(msg->sms.receiver);
                     msg->sms.receiver = octstr_duplicate(smpp->my_number);
                 }
                 time(&msg->sms.time);
                 msg->sms.smsc_id = octstr_duplicate(smpp->conn->id);
                 reason =  bb_smscconn_receive(smpp->conn, msg);
                 resp->u.data_sm_resp.command_status = smscconn_failure_reason_to_smpp_status(reason);
            }
            break;

        case deliver_sm:
            /*
             * Session state check
             */
            if (!(smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV)) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session not bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }

            /*
             * If SMSCConn stopped then send temp. error code
             */
            mutex_lock(smpp->conn->flow_mutex);
            if (smpp->conn->is_stopped) {
                mutex_unlock(smpp->conn->flow_mutex);
                resp = smpp_pdu_create(deliver_sm_resp,
                        pdu->u.deliver_sm.sequence_number);
                resp->u.deliver_sm_resp.command_status = SMPP_ESME_RX_T_APPN;
                break;
            }
            mutex_unlock(smpp->conn->flow_mutex);

            /* 
             * Got a deliver ack (DLR)?
             * NOTE: following SMPP v3.4. spec. we are interested
             *       only on bits 2-5 (some SMSC's send 0x44, and it's
             *       spec. conforme)
             */
            if (pdu->u.deliver_sm.esm_class & (0x04|0x08|0x20)) {

                debug("bb.sms.smpp",0,"SMPP[%s] handle_pdu, got DLR",
                      octstr_get_cstr(smpp->conn->id));

                dlrmsg = handle_dlr(smpp, pdu->u.deliver_sm.source_addr, pdu->u.deliver_sm.short_message, pdu->u.deliver_sm.message_payload,
                                    pdu->u.deliver_sm.receipted_message_id, pdu->u.deliver_sm.message_state, pdu->u.deliver_sm.network_error_code);
                resp = smpp_pdu_create(deliver_sm_resp, pdu->u.deliver_sm.sequence_number);
                if (dlrmsg != NULL) {
                    if (dlrmsg->sms.meta_data == NULL)
                        dlrmsg->sms.meta_data = octstr_create("");
                    meta_data_set_values(dlrmsg->sms.meta_data, pdu->u.deliver_sm.tlv, "smpp", 0);
                    reason = bb_smscconn_receive(smpp->conn, dlrmsg);
                } else
                    reason = SMSCCONN_SUCCESS;
                resp->u.deliver_sm_resp.command_status = smscconn_failure_reason_to_smpp_status(reason);
            } else {/* MO-SMS */
                resp = smpp_pdu_create(deliver_sm_resp,
                            pdu->u.deliver_sm.sequence_number);
                /* ensure the smsc-id is set */
                msg = pdu_to_msg(smpp, pdu, &reason);
                if (msg == NULL) {
                    resp->u.deliver_sm_resp.command_status = reason;
                    break;
                }

                /* Replace MO destination number with my-number */
                if (octstr_len(smpp->my_number)) {
                    octstr_destroy(msg->sms.receiver);
                    msg->sms.receiver = octstr_duplicate(smpp->my_number);
                }

                time(&msg->sms.time);
                msg->sms.smsc_id = octstr_duplicate(smpp->conn->id);
                msg->sms.account = octstr_duplicate(smpp->username);
                reason =  bb_smscconn_receive(smpp->conn, msg);
                resp->u.deliver_sm_resp.command_status = smscconn_failure_reason_to_smpp_status(reason);
            }
            break;

        case enquire_link:
            /*
             * Session state check
             */
            if (!(smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV)) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session not bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }
            resp = smpp_pdu_create(enquire_link_resp,
                        pdu->u.enquire_link.sequence_number);
            break;

        case enquire_link_resp:
            /*
             * Session state check
             */
            if (!(smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV)) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session not bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }
            if (pdu->u.enquire_link_resp.command_status != 0) {
                error(0, "SMPP[%s]: SMSC got error to enquire_link PDU, code 0x%08lx (%s).",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.enquire_link_resp.command_status,
                smpp_error_to_string(pdu->u.enquire_link_resp.command_status));
            }
            break;

        case submit_sm_resp:
            /*
             * Session state check
             */
            if (!(smpp->conn->status == SMSCCONN_ACTIVE)) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session not bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }

            os = octstr_format("%ld", pdu->u.submit_sm_resp.sequence_number);
            smpp_msg = dict_remove(smpp->sent_msgs, os);
            octstr_destroy(os);
            if (smpp_msg == NULL) {
                warning(0, "SMPP[%s]: SMSC sent submit_sm_resp PDU "
                        "with wrong sequence number 0x%08lx",
                        octstr_get_cstr(smpp->conn->id),
                        pdu->u.submit_sm_resp.sequence_number);
                break;
            }
            msg = smpp_msg->msg;
            smpp_msg_destroy(smpp_msg, 0);

            /* pack submit_sm_resp TLVs into metadata */
            if (msg->sms.meta_data == NULL)
                msg->sms.meta_data = octstr_create("");
            meta_data_set_values(msg->sms.meta_data, pdu->u.submit_sm_resp.tlv, "smpp_resp", 1);

            if (pdu->u.submit_sm_resp.command_status != 0) {
                error(0, "SMPP[%s]: SMSC returned error code 0x%08lx (%s) "
                      "in response to submit_sm PDU.",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.submit_sm_resp.command_status,
                      smpp_error_to_string(pdu->u.submit_sm_resp.command_status));
                reason = smpp_status_to_smscconn_failure_reason(
                            pdu->u.submit_sm_resp.command_status);

                /*
                 * check to see if we got a "throttling error", in which case we'll just
                 * sleep for a while
                 */
                if (pdu->u.submit_sm_resp.command_status == SMPP_ESME_RTHROTTLED)
                    time(&(smpp->throttling_err_time));
                else
                    smpp->throttling_err_time = 0;

                bb_smscconn_send_failed(smpp->conn, msg, reason, octstr_format("0x%08lx/%s", pdu->u.submit_sm_resp.command_status,
                                        smpp_error_to_string(pdu->u.submit_sm_resp.command_status)));
                --(*pending_submits);
            }
            else if (pdu->u.submit_sm_resp.message_id != NULL) {
                Octstr *tmp;

                /* check if msg_id is C string, decimal or hex for this SMSC */
                if (smpp->smpp_msg_id_type == -1) {
                    /* the default, C string */
                    tmp = octstr_duplicate(pdu->u.submit_sm_resp.message_id);
                } else {
                    if ((smpp->smpp_msg_id_type & 0x01) ||
                       (!octstr_check_range(pdu->u.submit_sm_resp.message_id, 0,
                            octstr_len(pdu->u.submit_sm_resp.message_id), gw_isdigit))) {
                        tmp = octstr_format("%llu", strtoll(  /* hex */
                            octstr_get_cstr(pdu->u.submit_sm_resp.message_id), NULL, 16));
                    } else {
                        tmp = octstr_format("%llu", strtoll(  /* decimal */
                            octstr_get_cstr(pdu->u.submit_sm_resp.message_id), NULL, 10));
                    }
                }

                /*
                 * SMSC ACK.. now we have the message ID.
                 * The message ID is inserted into the msg struct in dlr_add(),
                 * and we add it manually here if no DLR was requested, in
                 * order to get it logged to access-log.
                 */
                if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask)) {
                    dlr_add(smpp->conn->id, tmp, msg, 0);
                    octstr_destroy(tmp);
                } else {
                    octstr_destroy(msg->sms.foreign_id);
                    msg->sms.foreign_id = tmp;
                }

                bb_smscconn_sent(smpp->conn, msg, NULL);
                --(*pending_submits);
            } /* end if for SMSC ACK */
            else {
                error(0, "SMPP[%s]: SMSC returned error code 0x%08lx (%s) "
                      "in response to submit_sm PDU, but no `message_id' value!",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.submit_sm_resp.command_status,
                      smpp_error_to_string(pdu->u.submit_sm_resp.command_status));
                bb_smscconn_sent(smpp->conn, msg, NULL);
                --(*pending_submits);
            }
            break;

        case bind_transmitter_resp:
            /*
             * Session state check
             */
            if (smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }
            if (pdu->u.bind_transmitter_resp.command_status != 0 &&
                pdu->u.bind_transmitter_resp.command_status != SMPP_ESME_RALYNBD) {
                error(0, "SMPP[%s]: SMSC rejected login to transmit, code 0x%08lx (%s).",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.bind_transmitter_resp.command_status,
                smpp_error_to_string(pdu->u.bind_transmitter_resp.command_status));
                mutex_lock(smpp->conn->flow_mutex);
                smpp->conn->status = SMSCCONN_DISCONNECTED;
                mutex_unlock(smpp->conn->flow_mutex);
                if (pdu->u.bind_transmitter_resp.command_status == SMPP_ESME_RINVSYSID ||
                    pdu->u.bind_transmitter_resp.command_status == SMPP_ESME_RINVPASWD ||
                    pdu->u.bind_transmitter_resp.command_status == SMPP_ESME_RINVSYSTYP) {
                    smpp->quitting = 1;
                }
            } else {
                *pending_submits = 0;
                mutex_lock(smpp->conn->flow_mutex);
                smpp->conn->status = SMSCCONN_ACTIVE;
                time(&smpp->conn->connect_time);
                mutex_unlock(smpp->conn->flow_mutex);
                bb_smscconn_connected(smpp->conn);
            }
            break;

        case bind_transceiver_resp:
            /*
             * Session state check
             */
            if (smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }
            if (pdu->u.bind_transceiver_resp.command_status != 0 &&
                pdu->u.bind_transceiver_resp.command_status != SMPP_ESME_RALYNBD) {
                error(0, "SMPP[%s]: SMSC rejected login to transmit, code 0x%08lx (%s).",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.bind_transceiver_resp.command_status,
                 smpp_error_to_string(pdu->u.bind_transceiver_resp.command_status));
                 mutex_lock(smpp->conn->flow_mutex);
                 smpp->conn->status = SMSCCONN_DISCONNECTED;
                 mutex_unlock(smpp->conn->flow_mutex);
                 if (pdu->u.bind_transceiver_resp.command_status == SMPP_ESME_RINVSYSID ||
                     pdu->u.bind_transceiver_resp.command_status == SMPP_ESME_RINVPASWD ||
                     pdu->u.bind_transceiver_resp.command_status == SMPP_ESME_RINVSYSTYP) {
                     smpp->quitting = 1;
                 }
            } else {
                *pending_submits = 0;
                mutex_lock(smpp->conn->flow_mutex);
                smpp->conn->status = SMSCCONN_ACTIVE;
                time(&smpp->conn->connect_time);
                mutex_unlock(smpp->conn->flow_mutex);
                bb_smscconn_connected(smpp->conn);
            }
            break;

        case bind_receiver_resp:
            /*
             * Session state check
             */
            if (smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }
            if (pdu->u.bind_receiver_resp.command_status != 0 &&
                pdu->u.bind_receiver_resp.command_status != SMPP_ESME_RALYNBD) {
                error(0, "SMPP[%s]: SMSC rejected login to receive, code 0x%08lx (%s).",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.bind_receiver_resp.command_status,
                 smpp_error_to_string(pdu->u.bind_receiver_resp.command_status));
                 mutex_lock(smpp->conn->flow_mutex);
                 smpp->conn->status = SMSCCONN_DISCONNECTED;
                 mutex_unlock(smpp->conn->flow_mutex);
                 if (pdu->u.bind_receiver_resp.command_status == SMPP_ESME_RINVSYSID ||
                     pdu->u.bind_receiver_resp.command_status == SMPP_ESME_RINVPASWD ||
                     pdu->u.bind_receiver_resp.command_status == SMPP_ESME_RINVSYSTYP) {
                     smpp->quitting = 1;
                 }
            } else {
                /* set only receive status if no transmit is bind */
                mutex_lock(smpp->conn->flow_mutex);
                if (smpp->conn->status != SMSCCONN_ACTIVE) {
                    smpp->conn->status = SMSCCONN_ACTIVE_RECV;
                    time(&smpp->conn->connect_time);
                }
                mutex_unlock(smpp->conn->flow_mutex);
            }
            break;

        case unbind:
            /*
             * Session state check
             */
            if (!(smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV)) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session not bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }
            resp = smpp_pdu_create(unbind_resp, pdu->u.unbind.sequence_number);
            mutex_lock(smpp->conn->flow_mutex);
            smpp->conn->status = SMSCCONN_DISCONNECTED;
            mutex_unlock(smpp->conn->flow_mutex);
            *pending_submits = -1;
            break;

        case unbind_resp:
            /*
             * Session state check
             */
            if (!(smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV)) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session not bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }
            mutex_lock(smpp->conn->flow_mutex);
            smpp->conn->status = SMSCCONN_DISCONNECTED;
            mutex_unlock(smpp->conn->flow_mutex);
            break;

        case generic_nack:
            /*
             * Session state check
             */
            if (!(smpp->conn->status == SMSCCONN_ACTIVE ||
                    smpp->conn->status == SMSCCONN_ACTIVE_RECV)) {
                warning(0, "SMPP[%s]: SMSC sent %s PDU while session not bound, ignored.",
                        octstr_get_cstr(smpp->conn->id), pdu->type_name);
                return 0;
            }

            cmd_stat  = pdu->u.generic_nack.command_status;

            os = octstr_format("%ld", pdu->u.generic_nack.sequence_number);
            smpp_msg = dict_remove(smpp->sent_msgs, os);
            octstr_destroy(os);

            if (smpp_msg == NULL) {
                error(0, "SMPP[%s]: SMSC rejected last command, code 0x%08lx (%s).",
                      octstr_get_cstr(smpp->conn->id),
                      cmd_stat,
                smpp_error_to_string(cmd_stat));
            } else {
                msg = smpp_msg->msg;
                smpp_msg_destroy(smpp_msg, 0);

                error(0, "SMPP[%s]: SMSC returned error code 0x%08lx (%s) in response to submit_sm PDU.",
                      octstr_get_cstr(smpp->conn->id),
                      cmd_stat,
                smpp_error_to_string(cmd_stat));

                /*
                 * check to see if we got a "throttling error", in which case we'll just
                 * sleep for a while
                 */
                if (cmd_stat == SMPP_ESME_RTHROTTLED)
                    time(&(smpp->throttling_err_time));
                else
                    smpp->throttling_err_time = 0;

                reason = smpp_status_to_smscconn_failure_reason(cmd_stat);
                bb_smscconn_send_failed(smpp->conn, msg, reason,
                                        octstr_format("0x%08lx/%s", cmd_stat, smpp_error_to_string(cmd_stat)));
                --(*pending_submits);
            }
            break;
        
        default:
            error(0, "SMPP[%s]: Unhandled %s PDU type 0x%08lx, ignored.",
                  octstr_get_cstr(smpp->conn->id), pdu->type_name, pdu->type);
            /*
             * We received an unknown PDU type, therefore we will respond
             * with a generic_nack PDU, see SMPP v3.4 spec, section 3.3.
             */
            ret = send_gnack(smpp, conn, SMPP_ESME_RINVCMDID, pdu->u.generic_nack.sequence_number);
            break;
    }

    if (resp != NULL) {
        ret = send_pdu(conn, smpp->conn->id, resp) != -1 ? 0 : -1;
        smpp_pdu_destroy(resp);
    }

    return ret;
}


struct io_arg {
    SMPP *smpp;
    int transmitter;
};


static struct io_arg *io_arg_create(SMPP *smpp, int transmitter)
{
    struct io_arg *io_arg;

    io_arg = gw_malloc(sizeof(*io_arg));
    io_arg->smpp = smpp;
    io_arg->transmitter = transmitter;
    return io_arg;
}


/*
 * sent queue cleanup.
 * @return 1 if io_thread should reconnect; 0 if not
 */
static int do_queue_cleanup(SMPP *smpp, long *pending_submits)
{
    List *keys;
    Octstr *key;
    struct smpp_msg *smpp_msg;
    time_t now = time(NULL);

    if (*pending_submits <= 0)
        return 0;

    /* check if action set to wait ack for ever */
    if (smpp->wait_ack_action == SMPP_WAITACK_NEVER_EXPIRE)
        return 0;

    keys = dict_keys(smpp->sent_msgs);
    if (keys == NULL)
        return 0;

    while ((key = gwlist_extract_first(keys)) != NULL) {
        smpp_msg = dict_get(smpp->sent_msgs, key);
        if (smpp_msg != NULL && difftime(now, smpp_msg->sent_time) > smpp->wait_ack) {
            switch(smpp->wait_ack_action) {
                case SMPP_WAITACK_RECONNECT: /* reconnect */
                    /* found at least one not acked msg */
                    warning(0, "SMPP[%s]: Not ACKED message found, reconnecting.",
                                   octstr_get_cstr(smpp->conn->id));
                    octstr_destroy(key);
                    gwlist_destroy(keys, octstr_destroy_item);
                    return 1; /* io_thread will reconnect */
                case SMPP_WAITACK_REQUEUE: /* requeue */
                    smpp_msg = dict_remove(smpp->sent_msgs, key);
                    if (smpp_msg != NULL) {
                        warning(0, "SMPP[%s]: Not ACKED message found, will retransmit."
                                   " SENT<%ld>sec. ago, SEQ<%s>, DST<%s>",
                                   octstr_get_cstr(smpp->conn->id),
                                   (long)difftime(now, smpp_msg->sent_time) ,
                                   octstr_get_cstr(key),
                                   octstr_get_cstr(smpp_msg->msg->sms.receiver));
                        bb_smscconn_send_failed(smpp->conn, smpp_msg->msg, SMSCCONN_FAILED_TEMPORARILY,NULL);
                        smpp_msg_destroy(smpp_msg, 0);
                        (*pending_submits)--;
                    }
                    break;
                default:
                    error(0, "SMPP[%s] Unknown clenup action defined 0x%02x.",
                          octstr_get_cstr(smpp->conn->id), smpp->wait_ack_action);
                    octstr_destroy(key);
                    gwlist_destroy(keys, octstr_destroy_item);
                    return 0;
            }
        }
        octstr_destroy(key);
    }
    gwlist_destroy(keys, octstr_destroy_item);

    return 0;
}


/*
 * This is the main function for the background thread for doing I/O on
 * one SMPP connection (the one for transmitting or receiving messages).
 * It makes the initial connection to the SMPP server and re-connects
 * if there are I/O errors or other errors that require it.
 */
static void io_thread(void *arg)
{
    SMPP *smpp;
    struct io_arg *io_arg;
    int transmitter;
    Connection *conn;
    int ret;
    long pending_submits;
    long len;
    SMPP_PDU *pdu;
    double timeout;
    time_t last_cleanup, last_enquire_sent, last_response, now;

    io_arg = arg;
    smpp = io_arg->smpp;
    transmitter = io_arg->transmitter;
    gw_free(io_arg);

    /* Make sure we log into our own log-file if defined */
    log_thread_to(smpp->conn->log_idx);

#define IS_ACTIVE (smpp->conn->status == SMSCCONN_ACTIVE || smpp->conn->status == SMSCCONN_ACTIVE_RECV)

    conn = NULL;
    while (!smpp->quitting) {
        if (transmitter == 1)
            conn = open_transmitter(smpp);
        else if (transmitter == 2)
            conn = open_transceiver(smpp);
        else
            conn = open_receiver(smpp);
        
        pending_submits = -1;
        len = 0;
        last_response = last_cleanup = last_enquire_sent = time(NULL);
        while(conn != NULL) {
            ret = read_pdu(smpp, conn, &len, &pdu);
            if (ret == -1) { /* connection broken */
                error(0, "SMPP[%s]: I/O error or other error. Re-connecting.",
                      octstr_get_cstr(smpp->conn->id));
                break;
            } else if (ret == -2) {
                /* wrong pdu length , send gnack */
                len = 0;
                if (send_gnack(smpp, conn, SMPP_ESME_RINVCMDLEN, 0) == -1) {
                    error(0, "SMPP[%s]: I/O error or other error. Re-connecting.",
                          octstr_get_cstr(smpp->conn->id));
                    break;
                }
            } else if (ret == 1) { /* data available */
                /* Deal with the PDU we just got */
                dump_pdu("Got PDU:", smpp->conn->id, pdu);
                ret = handle_pdu(smpp, conn, pdu, &pending_submits);
                smpp_pdu_destroy(pdu);
                if (ret == -1) {
                    error(0, "SMPP[%s]: I/O error or other error. Re-connecting.",
                          octstr_get_cstr(smpp->conn->id));
                    break;
                }
                
                /*
                 * check if we are still connected
                 * Note: Function handle_pdu will set status to SMSCCONN_DISCONNECTED
                 * when unbind was received.
                 */
                if (smpp->conn->status == SMSCCONN_DISCONNECTED)
                    break;
                
                /*
                 * If we are not bounded then no PDU may coming from SMSC.
                 * It's just a workaround for buggy SMSC's who send enquire_link's
                 * although link is not bounded. Means: we doesn't notice these and if link
                 * keep to be not bounden we are reconnect after defined timeout elapsed.
                 */
                if (IS_ACTIVE) {
                    /*
                     * Store last response time.
                     */
                    time(&last_response);
                }
            } else { /* no data available */
                /* check last enquire_resp, if difftime > as idle_timeout
                 * mark connection as broken.
                 * We have some SMSC connections where connection seems to be OK, but
                 * in reality is broken, because no responses received.
                 */
                if (smpp->connection_timeout > 0 &&
                    difftime(time(NULL), last_response) > smpp->connection_timeout) {
                    /* connection seems to be broken */
                    warning(0, "Got no responses within %ld sec., reconnecting...",
                            (long) difftime(time(NULL), last_response));
                    break;
                }
                
                time(&now);
                timeout = last_enquire_sent + smpp->enquire_link_interval - now;
                if (!IS_ACTIVE && timeout <= 0)
                    timeout = smpp->enquire_link_interval;
                if (transmitter && gw_prioqueue_len(smpp->msgs_to_send) > 0 &&
                    smpp->throttling_err_time > 0 && pending_submits < smpp->max_pending_submits) {
                    time_t tr_timeout = smpp->throttling_err_time + SMPP_THROTTLING_SLEEP_TIME - now;
                    timeout = timeout > tr_timeout ? tr_timeout : timeout;
                } else if (transmitter && gw_prioqueue_len(smpp->msgs_to_send) > 0 && smpp->conn->throughput > 0 &&
                           smpp->max_pending_submits > pending_submits) {
                    double t = 1.0 / smpp->conn->throughput;
                    timeout = t < timeout ? t : timeout;
                }
                /* sleep a while */
                if (timeout > 0 && conn_wait(conn, timeout) == -1)
                    break;
            }
            
            /* send enquire link, only if connection is active */
            if (IS_ACTIVE && send_enquire_link(smpp, conn, &last_enquire_sent) == -1)
                break;
            
            /* cleanup sent queue */
            if (transmitter && difftime(time(NULL), last_cleanup) > smpp->wait_ack) {
                if (do_queue_cleanup(smpp, &pending_submits))
                    break; /* reconnect */
                time(&last_cleanup);
            }
            
            /* make sure we send */
            if (transmitter && difftime(time(NULL), smpp->throttling_err_time) > SMPP_THROTTLING_SLEEP_TIME) {
                smpp->throttling_err_time = 0;
                if (send_messages(smpp, conn, &pending_submits) == -1)
                    break;
            }
            
            /* unbind
             * Read so long as unbind_resp received or timeout passed. Otherwise we have
             * double delivered messages.
             */
            if (smpp->quitting) {
                if (!IS_ACTIVE || send_unbind(smpp, conn) == -1)
                    break;
                time(&last_response);
                while(conn_wait(conn, 1.00) != -1 && IS_ACTIVE &&
                      difftime(time(NULL), last_response) < SMPP_DEFAULT_SHUTDOWN_TIMEOUT) {
                    if (read_pdu(smpp, conn, &len, &pdu) == 1) {
                        dump_pdu("Got PDU:", smpp->conn->id, pdu);
                        handle_pdu(smpp, conn, pdu, &pending_submits);
                        smpp_pdu_destroy(pdu);
                    }
                }
                debug("bb.sms.smpp", 0, "SMPP[%s]: %s: break and shutting down",
                      octstr_get_cstr(smpp->conn->id), __PRETTY_FUNCTION__);
                
                break;
            }
        }

        if (conn != NULL) {
            conn_destroy(conn);
            conn = NULL;
        }
        /* set reconnecting status first so that core don't put msgs into our queue */
        if (!smpp->quitting) {
            error(0, "SMPP[%s]: Couldn't connect to SMS center (retrying in %ld seconds).",
                  octstr_get_cstr(smpp->conn->id), smpp->conn->reconnect_delay);
            mutex_lock(smpp->conn->flow_mutex);
            smpp->conn->status = SMSCCONN_RECONNECTING;
            mutex_unlock(smpp->conn->flow_mutex);
            gwthread_sleep(smpp->conn->reconnect_delay);
        }
        /*
         * put all queued messages back into global queue,so if
         * we have another link running than messages will be delivered
         * quickly
         */
        if (transmitter) {
            Msg *msg;
            struct smpp_msg *smpp_msg;
            List *noresp;
            Octstr *key;

            long reason = (smpp->quitting?SMSCCONN_FAILED_SHUTDOWN:SMSCCONN_FAILED_TEMPORARILY);

            while((msg = gw_prioqueue_remove(smpp->msgs_to_send)) != NULL)
                bb_smscconn_send_failed(smpp->conn, msg, reason, NULL);

            noresp = dict_keys(smpp->sent_msgs);
            while((key = gwlist_extract_first(noresp)) != NULL) {
                smpp_msg = dict_remove(smpp->sent_msgs, key);
                if (smpp_msg != NULL) {
                    bb_smscconn_send_failed(smpp->conn, smpp_msg->msg, reason, NULL);
                    smpp_msg_destroy(smpp_msg, 0);
                }
                octstr_destroy(key);
            }
            gwlist_destroy(noresp, NULL);
        }
    }
    
#undef IS_ACTIVE
    
    /*
     * Shutdown sequence as follow:
     *    1) if this is TX session so join receiver and free SMPP
     *    2) if RX session available but no TX session so nothing to join then free SMPP
     */
    if (transmitter && smpp->receiver != -1) {
        gwthread_wakeup(smpp->receiver);
        gwthread_join(smpp->receiver);
    }
    if (transmitter || smpp->transmitter == -1) {
        debug("bb.smpp", 0, "SMSCConn %s shut down.",
              octstr_get_cstr(smpp->conn->name));
        
        mutex_lock(smpp->conn->flow_mutex);
        smpp->conn->status = SMSCCONN_DEAD;
        smpp->conn->data = NULL;
        mutex_unlock(smpp->conn->flow_mutex);
        
        smpp_destroy(smpp);
        bb_smscconn_killed();
    }
}


/***********************************************************************
 * Functions called by smscconn.c via the SMSCConn function pointers.
 */


static long queued_cb(SMSCConn *conn)
{
    SMPP *smpp;

    smpp = conn->data;
    conn->load = (smpp ? (conn->status != SMSCCONN_DEAD ?
                  gw_prioqueue_len(smpp->msgs_to_send) : 0) : 0);
    return conn->load;
}


static int send_msg_cb(SMSCConn *conn, Msg *msg)
{
    SMPP *smpp;

    smpp = conn->data;
    gw_prioqueue_produce(smpp->msgs_to_send, msg_duplicate(msg));
    gwthread_wakeup(smpp->transmitter);
    return 0;
}


static int shutdown_cb(SMSCConn *conn, int finish_sending)
{
    SMPP *smpp;

    if (conn == NULL)
        return -1;

    debug("bb.smpp", 0, "Shutting down SMSCConn %s (%s)",
          octstr_get_cstr(conn->name),
          finish_sending ? "slow" : "instant");

    mutex_lock(conn->flow_mutex);

    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

    smpp = conn->data;
    if (smpp == NULL) {
        mutex_unlock(conn->flow_mutex);
        return 0;
    }

    smpp->quitting = 1;
    if  (smpp->transmitter != -1)
        gwthread_wakeup(smpp->transmitter);

    if (smpp->receiver != -1)
        gwthread_wakeup(smpp->receiver);

    mutex_unlock(conn->flow_mutex);

    return 0;
}


/***********************************************************************
 * Public interface. This version is suitable for the Kannel bearerbox
 * SMSCConn interface.
 */


int smsc_smpp_create(SMSCConn *conn, CfgGroup *grp)
{
    Octstr *host;
    long port;
    long receive_port;
    Octstr *username;
    Octstr *password;
    Octstr *system_id;
    Octstr *system_type;
    Octstr *address_range;
    long source_addr_ton;
    long source_addr_npi;
    long dest_addr_ton;
    long dest_addr_npi;
    long our_port;
    long our_receiver_port;
    Octstr *my_number;
    Octstr *service_type;
    SMPP *smpp;
    int ok;
    int transceiver_mode;
    Octstr *smsc_id;
    long enquire_link_interval;
    long max_pending_submits;
    long version;
    long priority;
    long validity;
    long smpp_msg_id_type;
    int autodetect_addr;
    Octstr *alt_charset;
    Octstr *alt_addr_charset;
    long connection_timeout, wait_ack, wait_ack_action;
    long esm_class;

    my_number = alt_addr_charset = alt_charset = NULL;
    transceiver_mode = 0;
    autodetect_addr = 1;

    host = cfg_get(grp, octstr_imm("host"));
    if (cfg_get_integer(&port, grp, octstr_imm("port")) == -1)
        port = 0;
    if (cfg_get_integer(&receive_port, grp, octstr_imm("receive-port")) == -1)
        receive_port = 0;

    if (cfg_get_integer(&our_port, grp, octstr_imm("our-port")) == -1)
        our_port = 0;
    if (cfg_get_integer(&our_receiver_port, grp, octstr_imm("our-receiver-port")) == -1)
        our_receiver_port = 0;

    cfg_get_bool(&transceiver_mode, grp, octstr_imm("transceiver-mode"));
    username = cfg_get(grp, octstr_imm("smsc-username"));
    password = cfg_get(grp, octstr_imm("smsc-password"));
    system_type = cfg_get(grp, octstr_imm("system-type"));
    address_range = cfg_get(grp, octstr_imm("address-range"));
    my_number = cfg_get(grp, octstr_imm("my-number"));
    service_type = cfg_get(grp, octstr_imm("service-type"));

    system_id = cfg_get(grp, octstr_imm("system-id"));
    if (system_id != NULL) {
        warning(0, "SMPP: obsolete system-id variable is set, "
               "use smsc-username instead.");
        if (username == NULL) {
            warning(0, "SMPP: smsc-username not set, using system-id instead");
            username = system_id;
        } else
            octstr_destroy(system_id);
    }

    /*
     * check if timing values have been configured, otherwise
     * use the predefined default values.
     */
    if (cfg_get_integer(&enquire_link_interval, grp,
                        octstr_imm("enquire-link-interval")) == -1)
        enquire_link_interval = SMPP_ENQUIRE_LINK_INTERVAL;
    if (cfg_get_integer(&max_pending_submits, grp,
                        octstr_imm("max-pending-submits")) == -1)
        max_pending_submits = SMPP_MAX_PENDING_SUBMITS;

    /* Check that config is OK */
    ok = 1;
    if (host == NULL) {
        error(0, "SMPP: Configuration file doesn't specify host");
        ok = 0;
    }
    if (port == 0 && receive_port == 0) {
        port = SMPP_DEFAULT_PORT;
        warning(0, "SMPP: Configuration file doesn't specify port or receive-port. "
                   "Using 'port = %ld' as default.", port);
    }
    if (port != 0 && receive_port != 0) {
        error(0, "SMPP: Configuration file can only have port or receive-port. "
                 "Usage of both in one group is deprecated!");
        ok = 0;
    }
    if (username == NULL) {
        error(0, "SMPP: Configuration file doesn't specify username.");
        ok = 0;
    }
    if (password == NULL) {
         error(0, "SMPP: Configuration file doesn't specify password.");
         ok = 0;
    }
    if (system_type == NULL) {
        error(0, "SMPP: Configuration file doesn't specify system-type.");
        ok = 0;
    }
    if (octstr_len(service_type) > 6) {
        error(0, "SMPP: Service type must be 6 characters or less.");
        ok = 0;
    }
    if (transceiver_mode && receive_port != 0) {
        warning(0, "SMPP: receive-port for transceiver mode defined, ignoring.");
        receive_port = 0;
    } 

    if (!ok)
        return -1;

    /* if the ton and npi values are forced, set them, else set them to -1 */
    if (cfg_get_integer(&source_addr_ton, grp,
                        octstr_imm("source-addr-ton")) == -1)
        source_addr_ton = -1;
    if (cfg_get_integer(&source_addr_npi, grp,
                        octstr_imm("source-addr-npi")) == -1)
        source_addr_npi = -1;
    if (cfg_get_integer(&dest_addr_ton, grp,
                        octstr_imm("dest-addr-ton")) == -1)
        dest_addr_ton = -1;
    if (cfg_get_integer(&dest_addr_npi, grp,
                        octstr_imm("dest-addr-npi")) == -1)
        dest_addr_npi = -1;

    /* if source addr autodetection should be used set this to 1 */
    if (cfg_get_bool(&autodetect_addr, grp, octstr_imm("source-addr-autodetect")) == -1)
        autodetect_addr = 1; /* default is autodetect if no option defined */

    /* check for any specified interface version */
    if (cfg_get_integer(&version, grp, octstr_imm("interface-version")) == -1)
        version = SMPP_DEFAULT_VERSION;
    else
        /* convert decimal to BCD */
        version = ((version / 10) << 4) + (version % 10);

    /* check for any specified priority value in range [0-5] */
    if (cfg_get_integer(&priority, grp, octstr_imm("priority")) == -1)
        priority = SMPP_DEFAULT_PRIORITY;
    else if (priority < 0 || priority > 3)
        panic(0, "SMPP: Invalid value for priority directive in configuraton (allowed range 0-3).");

    /* check for message validity period */
    if (cfg_get_integer(&validity, grp, octstr_imm("validityperiod")) == -1)
        validity = SMS_PARAM_UNDEFINED;
    else if (validity < 0)
        panic(0, "SMPP: Invalid value for validity period (allowed value >= 0).");

    /* set the msg_id type variable for this SMSC */
    if (cfg_get_integer(&smpp_msg_id_type, grp, octstr_imm("msg-id-type")) == -1) {
        /*
         * defaults to C string "as-is" style
         */
        smpp_msg_id_type = -1;
    } else {
        if (smpp_msg_id_type < 0 || smpp_msg_id_type > 3)
            panic(0,"SMPP: Invalid value for msg-id-type directive in configuraton");
    }

    /* check for an alternative charset */
    alt_charset = cfg_get(grp, octstr_imm("alt-charset"));
    alt_addr_charset = cfg_get(grp, octstr_imm("alt-addr-charset"));

    /* check for connection timeout */
    if (cfg_get_integer(&connection_timeout, grp, octstr_imm("connection-timeout")) == -1)
        connection_timeout = SMPP_DEFAULT_CONNECTION_TIMEOUT;

    /* check if wait-ack timeout set */
    if (cfg_get_integer(&wait_ack, grp, octstr_imm("wait-ack")) == -1)
        wait_ack = SMPP_DEFAULT_WAITACK;

    if (cfg_get_integer(&wait_ack_action, grp, octstr_imm("wait-ack-expire")) == -1)
        wait_ack_action = SMPP_WAITACK_REQUEUE;
    else if (wait_ack_action > 0x03 || wait_ack_action < 0)
        panic(0, "SMPP: Invalid wait-ack-expire directive in configuration.");

    if (cfg_get_integer(&esm_class, grp, octstr_imm("esm-class")) == -1) {
        esm_class = ESM_CLASS_SUBMIT_STORE_AND_FORWARD_MODE;
    } else if ( esm_class != ESM_CLASS_SUBMIT_DEFAULT_SMSC_MODE && 
              esm_class != ESM_CLASS_SUBMIT_STORE_AND_FORWARD_MODE ) {
        error(0, "SMPP: Invalid esm_class mode '%ld' in configuration. Switching to \"Store and Forward\".", 
                      esm_class);
        esm_class = ESM_CLASS_SUBMIT_STORE_AND_FORWARD_MODE;
    }

    smpp = smpp_create(conn, host, port, receive_port, our_port, our_receiver_port, system_type,
                       username, password, address_range,
                       source_addr_ton, source_addr_npi, dest_addr_ton,
                       dest_addr_npi, enquire_link_interval,
                       max_pending_submits, version, priority, validity, my_number,
                       smpp_msg_id_type, autodetect_addr, alt_charset, alt_addr_charset,
                       service_type, connection_timeout, wait_ack, wait_ack_action, esm_class);

    cfg_get_integer(&smpp->bind_addr_ton, grp, octstr_imm("bind-addr-ton"));
    cfg_get_integer(&smpp->bind_addr_npi, grp, octstr_imm("bind-addr-npi"));

    cfg_get_bool(&smpp->use_ssl, grp, octstr_imm("use-ssl"));
    if (smpp->use_ssl)
#ifndef HAVE_LIBSSL
        panic(0, "SMPP: Can not use 'use-ssl' without SSL support compiled in.");
#else
        smpp->ssl_client_certkey_file = cfg_get(grp, octstr_imm("ssl-client-certkey-file"));
#endif

    conn->data = smpp;
    conn->name = octstr_format("SMPP:%S:%d/%d:%S:%S",
                               host, port,
                               (!receive_port && transceiver_mode  ? port : receive_port),
                               username, system_type);

    smsc_id = cfg_get(grp, octstr_imm("smsc-id"));
    if (smsc_id == NULL) {
        conn->id = octstr_duplicate(conn->name);
    }

    octstr_destroy(host);
    octstr_destroy(username);
    octstr_destroy(password);
    octstr_destroy(system_type);
    octstr_destroy(address_range);
    octstr_destroy(my_number);
    octstr_destroy(smsc_id);
    octstr_destroy(alt_charset);
    octstr_destroy(alt_addr_charset);
    octstr_destroy(service_type);

    conn->status = SMSCCONN_CONNECTING;

    /*
     * I/O threads are only started if the corresponding ports
     * have been configured with positive numbers. Use 0 to
     * disable the creation of the corresponding thread.
     */
    if (port != 0)
        smpp->transmitter = gwthread_create(io_thread, io_arg_create(smpp,
                                           (transceiver_mode ? 2 : 1)));
    if (receive_port != 0)
        smpp->receiver = gwthread_create(io_thread, io_arg_create(smpp, 0));

    if ((port != 0 && smpp->transmitter == -1) ||
        (receive_port != 0 && smpp->receiver == -1)) {
        error(0, "SMPP[%s]: Couldn't start I/O threads.",
              octstr_get_cstr(smpp->conn->id));
        smpp->quitting = 1;
        if (smpp->transmitter != -1) {
            gwthread_wakeup(smpp->transmitter);
            gwthread_join(smpp->transmitter);
        }
        if (smpp->receiver != -1) {
            gwthread_wakeup(smpp->receiver);
            gwthread_join(smpp->receiver);
        }
        smpp_destroy(conn->data);
        conn->data = NULL;
        return -1;
    }

    conn->shutdown = shutdown_cb;
    conn->queued = queued_cb;
    conn->send_msg = send_msg_cb;

    return 0;
}

