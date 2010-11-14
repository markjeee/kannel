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
 * gw/smsc_at2.h
 *
 * New driver for serial connected AT based
 * devices.
 * 4.9.2001
 * Andreas Fink <afink@smsrelay.com>
 * 
 */

#ifndef SMSC_AT2_H
#define SMSC_AT2_H

#include "gwlib/gwlib.h"

/* maximum data to attempt to read in one go */
#define	MAX_READ        1023

/* Message types defines */
#define AT_DELIVER_SM   0
#define AT_SUBMIT_SM    1
#define AT_STATUS_REPORT_SM 2

/* type of phone number defines */
#define PNT_UNKNOWN     0
#define PNT_INTER       1
#define PNT_NATIONAL    2

/* The number of times to attempt to write a line should writing fail */
#define RETRY_WRITE 3

/* 
 * defines for use with the so-called "SIM buffering techinique":
 * once in how many seconds to poll the memory locations, 
 * if keepalive is _not_ set (will use keepalive time if set) 
 */
#define AT2_DEFAULT_SMS_POLL_INTERVAL	60

/*
 * Structures used in at2
 */
typedef struct ModemDef {
    Octstr *id;
    Octstr *name;
    Octstr *detect_string;
    Octstr *detect_string2;
    Octstr *init_string;
    Octstr *reset_string;
    long speed;
    Octstr *enable_hwhs;
    int	need_sleep;
    int	no_pin;
    int	no_smsc;
    long sendline_sleep;
    Octstr *keepalive_cmd;
    int	broken;
    Octstr *message_storage;
    long message_start;
    int	enable_mms;
} ModemDef;

typedef struct PrivAT2data {
    gw_prioqueue_t *outgoing_queue;
    ModemDef *modem;
    long device_thread;
    int	shutdown; /* Internal signal to shut down */
    Octstr *device;
    long speed;
    long keepalive;
    int	fd;	/* file descriptor */
    Octstr *ilb; /* input line buffer */
    Octstr *lines; /* the last few lines before OK was seen */
    Octstr *pin; /* PIN code */
    int	pin_ready;
    SMSCConn *conn;
    int phase2plus;
    Octstr *validityperiod;
    int retry;
    Octstr *my_number;
    Octstr *sms_center;
    Octstr *name;
    Octstr *configfile;
    Octstr *username;
    Octstr *password;
    Octstr *login_prompt;
    Octstr *password_prompt;
    int	sms_memory_poll_interval;
    int	sms_memory_capacity;
    int	sms_memory_usage;
    List *pending_incoming_messages;
    long max_error_count;
    Octstr *rawtcp_host;
    int rawtcp_port;
    int is_serial; /* false if device is rawtcp */ 
    int use_telnet; /* use telnet escape sequences */
 } PrivAT2data;


/*
 * Macro that is used inside smsc_at2.c in order to handle
 * octstr destruction more carefully.
 */
#define	O_DESTROY(a) { if(a) octstr_destroy(a); a = NULL; }
/* 
#define	at2_write_ctrlz(a) at2_write(a,"\032") 
*/

/*
 * open the specified device using the serial line
 */
static int at2_open_device(PrivAT2data *privdata);

/*
 * close the specified device and hence disconnect from the serial line 
 */
static void at2_close_device(PrivAT2data *privdata);

/*
 * checks if there are any incoming bytes and adds them to the line buffer
 */
static void at2_read_buffer(PrivAT2data *privdata, double timeout);

/* 
 * Looks for a full line to be read from the buffer. 
 * Returns the line and removes it from the buffer or if no full line 
 * is yet received waits until the line is there or a timeout occurs.
 * If gt_flag is set, it is also looking for a line containing '>' even 
 * there is no CR yet.
 */
static Octstr *at2_wait_line(PrivAT2data *privdata, time_t timeout, int gt_flag);

/*
 * Looks for a full line to be read from the buffer.
 * Returns the line and removes it from the buffer or if no full line 
 * is yet received returns NULL. If gt_flag is set, it is also looking for
 * a line containing > even there is no CR yet.
 */
static Octstr *at2_read_line(PrivAT2data *privdata, int gt_flag, double timeout);

/*
 * Writes a line out to the device and adds a carriage return/linefeed to it. 
 * Returns number of characters sent.
 */
static int at2_write_line(PrivAT2data *privdata, char *line);
static int at2_write_ctrlz(PrivAT2data *privdata);
static int at2_write(PrivAT2data *privdata, char *line);

/*
 * Clears incoming buffer
 */
static void at2_flush_buffer(PrivAT2data *privdata);
 
/*
 * Initializes the device after being opened, detects the modem type, 
 * sets speed settings etc.
 * On failure returns -1.
 */
static int at2_init_device(PrivAT2data *privdata);

/*
 * Sends an AT command to the modem and waits for a reply
 * Return values are:
 *   0 = OK
 *   1 = ERROR
 *   2 = SIM PIN
 *   3 = >
 *   4 = READY
 *   5 = CMGS
 *  -1 = timeout occurred
 */
static int at2_send_modem_command(PrivAT2data *privdata, char *cmd, time_t timeout, 
                           int greaterflag);

/*
 * Waits for the modem to send us something.
 */
static int at2_wait_modem_command(PrivAT2data *privdata, time_t timeout, 
                           int greaterflag, int *output);

/*
 * Sets the serial port speed on the device
 */
static void at2_set_speed(PrivAT2data *privdata, int bps);

/*
 * This is the main tread "sitting" on the device.
 * Its task is to initialize the modem then wait for messages 
 * to arrive or to be sent
 */
static void at2_device_thread(void *arg);

static int at2_shutdown_cb(SMSCConn *conn, int finish_sending);
static long at2_queued_cb(SMSCConn *conn);
static void at2_start_cb(SMSCConn *conn);
static int at2_add_msg_cb(SMSCConn *conn, Msg *sms);

/*
 * Starts the whole thing up
 */
int smsc_at2_create(SMSCConn *conn, CfgGroup *cfg);

/*
 * Extracts the first PDU in the string
 */
static int at2_pdu_extract(PrivAT2data *privdata, Octstr **pdu, Octstr *line, Octstr *smsc_number);

/*
 * Get the numeric value of the text hex
 */
static int at2_hexchar(int hexc);

/*
 * Decode a raw PDU into a Msg
 */
static Msg *at2_pdu_decode(Octstr *data, PrivAT2data *privdata);

/*
 * Decode a DELIVER PDU
 */
static Msg *at2_pdu_decode_deliver_sm(Octstr *data, PrivAT2data *privdata);

/*
 * Decode a SUBMIT-REPORT PDU
 */
static Msg *at2_pdu_decode_report_sm(Octstr *data, PrivAT2data *privdata);

/*
 * Converts the text representation of hexa to binary
 */
static Octstr *at2_convertpdu(Octstr *pdutext);

/*
 * Decode 7bit uncompressed user data
 */
static void at2_decode7bituncompressed(Octstr *input, int len, Octstr *decoded, 
                                int offset);

/*
 * Sends messages from the queue
 */
static void at2_send_messages(PrivAT2data *privdata);

/*
 * Sends a single message. 
 * After having it sent, the msg is no longe belonging to us
 */
static void at2_send_one_message(PrivAT2data *privdata, Msg *msg);

/*
 * Encode a Msg into a PDU
 */
static Octstr *at2_pdu_encode(Msg *msg, PrivAT2data *privdata);

/*
 * Encode 7bit uncompressed user data into an Octstr, prefixing with <offset> 0 bits
 */
static Octstr *at2_encode7bituncompressed(Octstr *input, int offset);

/*
 * Encode 8bit uncompressed user data into an Octstr
 */
static Octstr *at2_encode8bituncompressed(Octstr *input);

/*
 * Code a half-byte to its text hexa representation
 */
static int at2_numtext(int num);

/*
 * Try to detect modem speeds
 */
static int at2_detect_speed(PrivAT2data *privdata);

/*
 * Test modem speed
 */
static int at2_test_speed(PrivAT2data *privdata, long speed);

/*
 * Try to detect modem type
 */
static int at2_detect_modem_type(PrivAT2data *privdata);

/*
 * Read all defined modems from the included modem definition file
 */
static ModemDef *at2_read_modems(PrivAT2data *privdata, Octstr *file, Octstr *id, int idnumber);

/*
 * Destroy the ModemDef structure components
 */
static void at2_destroy_modem(ModemDef *modem);

/*
 * Checks whether any messages are buffered in message storage and extract them.
 */
static int at2_read_sms_memory(PrivAT2data *privdata);

/*
 * Memory capacity and usage check
 */
static int at2_check_sms_memory(PrivAT2data *privdata);

/*
 * This silly thing here will just translate a "swapped nibble" 
 * pseodo Hex encoding (from PDU) into something that people can 
 * actually understand.
 * Implementation completly ripped off Dennis Malmstrom timestamp 
 * patches against 1.0.3. Thanks Dennis! 
 */
static int swap_nibbles(unsigned char byte);

/*
 * creates a buffer with a valid PDU address field as per [GSM 03.40]
 * from an MSISDN number
 */
static Octstr *at2_format_address_field(Octstr *msisdn);

/*
 * Check the pending_incoming_messages queue for CMTI notifications.
 * Every notification is parsed and the messages are read (and deleted)
 * accordingly.
 */
static void at2_read_pending_incoming_messages(PrivAT2data *privdata);

/*
 * Set the memory storage location of the modem by sending a +CPMS command
 */
static int at2_set_message_storage(PrivAT2data *privdata, Octstr *memory_name);

/*
 * Reads a message from selected memory location and deletes it afterwards.
 * returns 0 on failure and 1 on success
 */
static int at2_read_delete_message(PrivAT2data *privdata, int message_number);

/*
 * Return appropriate error string for the given error code.
 */
static const char *at2_error_string(int code);

#endif /* SMSC_AT2_H */

