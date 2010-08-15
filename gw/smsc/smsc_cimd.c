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

/*****************************************************************************
* smsc_cimd.c - Nokia SMS Center (CIMD 1.3).
* Mikael Gueck for WapIT Ltd.
*/

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "smsc.h"
#include "smsc_p.h"
#include "gwlib/gwlib.h"
#include "alt_charsets.h"

/******************************************************************************
* Static functions
*/

/* do the handshake baby */
static int cimd_open_connection(SMSCenter *smsc);

/* waits for an ACK message, returns the ACK command number or -1 for error */
static int expect_acknowledge(SMSCenter *smsc, int *cmd, int *err);

/* sends a general ACK */
static int send_acknowledge(SMSCenter *smsc);

/* Reconnect to a CIMD server, use an existing structure */
static int connect_tcpip(SMSCenter *smsc);

static int parse_cimd_to_iso88591(char *from, char *to, int length);

static int parse_iso88591_to_cimd(
    char *from, char *to, int length, int alt_charset);

/******************************************************************************
* Open the connection and log in
*
* return 0 if ok, -1 on failure
*/
static int cimd_open_connection(SMSCenter *smsc)
{

    char *tmpbuff = NULL;
    int ret = 0;

    int cmd = 0, err = 0;

    /* allocate some spare space */
    tmpbuff = gw_malloc(10 * 1024);
    memset(tmpbuff, 0, 10*1024);

    /* connect */
    smsc->socket = tcpip_connect_to_server(smsc->cimd_hostname, smsc->cimd_port,
	NULL);
	/* XXX add interface_name if required */

    if (smsc->socket == -1)
        goto error;

    /* receive protocol string "CIMD rel 1.37\n" */
    for (;;) {
        ret = smscenter_read_into_buffer(smsc);
        if (strstr(smsc->buffer, "CIMD rel 1.37\n") != NULL)
            break;
        if (ret < 0) goto logout;
    }

    debug("bb.sms.cimd", 0, "got the server identification tag");

    smscenter_remove_from_buffer(smsc, smsc->buflen);

    /* send login string */
    sprintf(tmpbuff, "%c%s%c%s%c%s%c%s%c%c",
            0x02,
            "01", 0x09,
            smsc->cimd_username, 0x09,
            smsc->cimd_password, 0x09,
            "11",
            0x03, 0x0A);

    ret = write_to_socket(smsc->socket, tmpbuff);
    if (ret < 0) goto logout;

    /* get an acknowledge message */

    smsc->cimd_last_spoke = 0;

    if (expect_acknowledge(smsc, &cmd, &err) < 1)
        goto logout;

    debug("bb.sms.cimd", 0, "logged in");

    gw_free(tmpbuff);
    return 0;

logout:
    cimd_close(smsc);

error:
    error(0, "cimd_open: could not open/handshake");
    gw_free(tmpbuff);
    return -1;
}



/******************************************************************************
* Open the smscenter
*/
SMSCenter *cimd_open(char *hostname, int port, char *username, char *password)
{

    SMSCenter *smsc = NULL;
    int ret = 0;

    /* create a SMSCenter structure */
    smsc = smscenter_construct();
    if (smsc == NULL) goto error;
    smsc->type = SMSC_TYPE_CIMD;
    smsc->cimd_hostname = gw_strdup(hostname);
    smsc->hostname = gw_strdup(hostname); /* Needed by read_into_buffer() */
    smsc->cimd_port = port;
    smsc->cimd_username = gw_strdup(username);
    smsc->cimd_password = gw_strdup(password);

    ret = cimd_open_connection(smsc);
    if (ret < 0)
        goto error;

    sprintf(smsc->name, "CIMD:%s:%d:%s", smsc->cimd_hostname,
            smsc->cimd_port, smsc->cimd_username);
    return smsc;

error:
    error(0, "cimd_open: could not open!");
    smscenter_destruct(smsc);
    return NULL;
}


/******************************************************************************
* Re-open the connection and log in
*
* return -1 if failed
*/
int cimd_reopen(SMSCenter *smsc)
{

    cimd_close(smsc);

    if (cimd_open_connection(smsc) < 0) {
        error(0, "Failed to re-open the connection!");
        return -1;
    }
    return 0;
}



/******************************************************************************
* Log out and close the socket
*
*/
int cimd_close(SMSCenter *smsc)
{

    char *cbuff = NULL;
    int sum;
    int ret;

    if (smsc->socket == -1) {
        debug("bb.sms.cimd", 0, "Trying to close cimd while already closed!");
        return 0;
    }
    cbuff = gw_malloc(2 * 1024);

    sprintf(cbuff, "%c%s%c%s%c%c", 0x02, "02", 0x09, "11", 0x03, 0x0A);

    sum = write_to_socket(smsc->socket, cbuff);
    if (sum < 0) goto error;

    /* this time we don't block waiting for acknowledge */
    recv(smsc->socket, cbuff, 2*1024, 0);

    gw_free(cbuff);

    ret = close(smsc->socket);
    smsc->socket = -1;
    return ret;

error:
    gw_free(cbuff);
    return -1;
}


/******************************************************************************
* Check for MO messages, returns as in smsc_submit_smsmessage in smsc.h
*/
int cimd_pending_smsmessage(SMSCenter *smsc)
{

    char *tmpbuff = NULL, *newline = NULL;
    int ret = 0;
    time_t thetime = 0;

    /* check for input sanity */
    if (smsc == NULL)
        goto error;

    /* we can only query every 5 seconds */
    thetime = time(NULL);
    if ((smsc->cimd_last_spoke + 5) > thetime) goto no_messages;
    smsc->cimd_last_spoke = thetime;

    /* allocate some spare space */
    tmpbuff = gw_malloc(10 * 1024);
    memset(tmpbuff, 0, 10*1024);

    sprintf(tmpbuff, "%c%s%c%s%c%c",
            0x02,         /* stx */
            "05", 0x09,   /* request for message, tab */
            "11",         /* dummy chksum */
            0x03, 0x0A);  /* etx, lf */

    /* send the poll message to determine if we have messages in queue */
    ret = write_to_socket(smsc->socket, tmpbuff);
    if (ret < 0) {
        debug("bb.sms.cimd", 0, "sending poll message failed");
        goto error;
    }
    /* block while waiting for answer that dataset ends to a 0x0A */
    for (;;) {

        newline = memchr(smsc->buffer, 0x0A, smsc->buflen);
        if (newline != NULL) break;

        newline = memchr(smsc->buffer, 0x03, smsc->buflen);
        if (newline != NULL) break;

        ret = smscenter_read_into_buffer(smsc);
        if (ret <= 0) {
            debug("bb.sms.cimd", 0, "read_into_buffer failed!, ret=%d", ret);
            goto error;
        }

        usleep(500);

        /* Reconnect if no results in 30 seconds */
        if (time(NULL) > (thetime + 30)) {

            error(0, "timeout occurred, maybe the connection was broken?");

            /* Reconnect if neccessary, this catches most of them */
            /* XXX this is an ugly kludge, but then again,
               CIMD 1.3 is an ugly kludge. */
            connect_tcpip(smsc);
            goto no_messages;

        }

    }

    /* if we got an nck, cut the message out and return 0 */
    newline = memchr(smsc->buffer, 0x15, smsc->buflen);
    if (newline != NULL) {
        newline = memchr(smsc->buffer, 0x0A, smsc->buflen);
        if (newline == NULL)
            newline = memchr(smsc->buffer, 0x03, smsc->buflen);
        smscenter_remove_from_buffer(smsc, newline - smsc->buffer + 1);
        goto no_messages;
    }

    /* miracle of miracles, we got a message */
    gw_free(tmpbuff);
    return 1;

no_messages:
    gw_free(tmpbuff);
    return 0;

error:

    debug("bb.sms.cimd", 0, "smscenter_pending_smsmessage: returning error");
    gw_free(tmpbuff);
    return -1;
}



/******************************************************************************
* Send a MT message, returns as in smsc_submit_smsmessage in smsc.h
*/
int cimd_submit_msg(SMSCenter *smsc, Msg *msg)
{

    char *tmpbuff = NULL, *tmptext = NULL;
    char msgtext[1024];
    int ret;
    int cmd = 0, err = 0;

    /* Fix these by implementing a could-not-send-because-
       protocol-does-not-allow in smsc.c or smsgateway.c */
    if (octstr_len(msg->sms.msgdata) + octstr_len(msg->sms.udhdata) < 1) {
	if (msg->sms.msgdata == NULL)
	    msg->sms.msgdata = octstr_create("");
	octstr_append_from_hex(msg->sms.msgdata, "20");
    }
    if (octstr_len(msg->sms.sender) < 1) {
        warning(0, "cimd_submit_smsmessage: ignoring message with 0-length field");
        goto okay;  /* THIS IS NOT OKAY!!!! XXX */
    }
    if (octstr_len(msg->sms.receiver) < 1) {
        warning(0, "cimd_submit_smsmessage: ignoring message with 0-length field");
        goto okay;  /* THIS IS NOT OKAY!!!! XXX */
    }

    tmpbuff = gw_malloc(10 * 1024);
    tmptext = gw_malloc(10 * 1024);

    memset(tmpbuff, 0, 10*1024);
    memset(tmptext, 0, 10*1024);
    memset(msgtext, 0, sizeof(msgtext));

    if (octstr_len(msg->sms.udhdata)) {
        octstr_get_many_chars(msgtext, msg->sms.udhdata, 0, octstr_len(msg->sms.udhdata));
        octstr_get_many_chars(msgtext + octstr_len(msg->sms.udhdata),
                              msg->sms.msgdata, 0,
                              140 - octstr_len(msg->sms.udhdata));
    } else {
        octstr_get_many_chars(msgtext, msg->sms.msgdata, 0,
                              octstr_len(msg->sms.msgdata));
    }

    /* XXX parse_iso88591_to_cimd should use Octstr
     * directly, or get a char* and a length, instead of using NUL
     * terminated strings.
     */
    parse_iso88591_to_cimd(msgtext, tmptext, 10*1024, smsc->alt_charset);

    /* If messages has UDHs, add the magic number 31 to the right spot */
    sprintf(tmpbuff, "%c%s%c%s%c%s%c%s%c%s%c%s%c%s%c%c",
            0x02,
            "03", 0x09,
            octstr_get_cstr(msg->sms.receiver), 0x09,
            tmptext, 0x09,
            "", 0x09,
            "", 0x09,
            (octstr_len(msg->sms.udhdata)) ? "31" : "", 0x09,
            "11", 0x03, 0x0A);

    ret = write_to_socket(smsc->socket, tmpbuff);
    if (ret < 0) {
        debug("bb.sms.cimd", 0, "cimd_submit_smsmessage: socket write error");
        goto error;
    }

    /* The Nokia SMSC MAY be configured to send delivery
       information, which we then will HAVE to acknowledge.
       Naturally the CIMD 1.3 protocol does not include any
       kind of negotiation mechanism. */
    ret = expect_acknowledge(smsc, &cmd, &err);

    if (ret >= 1) {

        if (cmd == 4) {
            send_acknowledge(smsc);
            goto okay;
        } else if (cmd == 3) {
            goto okay;
        }

    } else if (ret == 0) {

        if (cmd == 4) {
            send_acknowledge(smsc);
            goto okay;  /* FIXME XXX THIS IS BOGUS, FIX SMSGATEWAY.C */
            goto error;
        } else if (cmd == 3) {
            goto okay;  /* FIXME XXX THIS IS BOGUS, FIX SMSGATEWAY.C */
            goto error;
        } else {
            error(0, "Unexpected behaviour from the CIMD server");
            debug("bb.sms.cimd", 0, "cimd_submit_smsmessage: acknowledge was <%i>", ret);
            debug("bb.sms.cimd", 0, "cimd_submit_smsmessage: buffer==<%s>", smsc->buffer);
            goto error;
        }

    }

okay:
    gw_free(tmpbuff);
    gw_free(tmptext);
    return 0;

error:
    debug("bb.sms.cimd", 0, "cimd_submit_smsmessage: returning error");
    gw_free(tmpbuff);
    gw_free(tmptext);
    return -1;

}



int cimd_receive_msg(SMSCenter *smsc, Msg **msg)
{

    char *tmpbuff = NULL, *sender = NULL;
    char *receiver = NULL, *text = NULL, *scts = NULL;
    char *tmpchar = NULL;

    debug("bb.sms.cimd", 0, "cimd_receive_smsmessage: starting");

    /* the PENDING function has previously requested for
       the message and checked that it safely found its 
       way into the memory buffer (smsc->buffer) */

    /* we want to temporarily store some data */
    tmpbuff = gw_malloc(10 * 1024);
    sender = gw_malloc(10 * 1024);
    receiver = gw_malloc(10 * 1024);
    text = gw_malloc(10 * 1024);
    scts = gw_malloc(10 * 1024);

    memset(tmpbuff, 0, 10 * 1024);
    memset(sender, 0, 10 * 1024);
    memset(receiver, 0, 10 * 1024);
    memset(text, 0, 10 * 1024);
    memset(scts, 0, 10 * 1024);

    /* cut the raw message out from the message buffer */
    tmpchar = memchr(smsc->buffer, 0x0A, smsc->buflen);
    if (tmpchar == NULL) {
        tmpchar = memchr(smsc->buffer, 0x03, smsc->buflen);
        if (tmpchar == NULL) goto error;
    }

    strncpy(tmpbuff, smsc->buffer, tmpchar - smsc->buffer);
    smscenter_remove_from_buffer(smsc, tmpchar - smsc->buffer + 1);

    /* Parse the raw message */
    sscanf(tmpbuff,
           "\x02\x06\tC:05\t%[^\t]\t%[^\t]\t%[^\t]\t%[^\t]\t11\x03\x0A",
           receiver, sender, text, scts);

    sscanf(tmpbuff,
           "\x02\x06\tC:05\t%[^\t]\t%[^\t]\t%[^\t]\t%[^\t]\t11\x03",
           receiver, sender, text, scts);

    /* Translate from the CIMD character set to iso8859-1 */
    parse_cimd_to_iso88591(text, tmpbuff, 10*1024);
    strncpy(text, tmpbuff, 480);

    /* create a smsmessage structure out of the components */
    *msg = msg_create(sms);
    if (*msg == NULL) return -1;
    (*msg)->sms.sender = octstr_create(sender);
    (*msg)->sms.receiver = octstr_create(receiver);
    (*msg)->sms.msgdata = octstr_create(text);

    /* Send acknowledge */
    send_acknowledge(smsc);

    /* We got a message so we can instantly check for a new one. */
    smsc->cimd_last_spoke -= 5;

    /* Free and Finish */

    gw_free(tmpbuff);
    gw_free(sender);
    gw_free(receiver);
    gw_free(text);
    gw_free(scts);

    debug("bb.sms.cimd", 0, "cimd_receive_smsmessage: return ok");

    return 1;

error:
    debug("bb.sms.cimd", 0, "cimd_receive_smsmessage: failed");
    gw_free(tmpbuff);
    gw_free(sender);
    gw_free(receiver);
    gw_free(text);
    gw_free(scts);

    debug("bb.sms.cimd", 0, "cimd_receive_smsmessage: return failed");

    return -1;
}

/******************************************************************************
* In(f)ternal Functions
*/

static int connect_tcpip(SMSCenter *smsc)
{

    char *tmpbuff = NULL;
    int ret = 0;
    int cmd = 0, err = 0;

    debug("bb.sms.cimd", 0, "reconnecting to <%s>", smsc->name);

    /* allocate some spare space */
    tmpbuff = gw_malloc(10 * 1024);
    memset(tmpbuff, 0, 10*1024);

    /* Close connection */
    close(smsc->socket);

    smsc->socket = -1;

    /* Be sure to open a socket. */
    for (;;) {
        smsc->socket = tcpip_connect_to_server(
                           smsc->cimd_hostname, smsc->cimd_port,
			   NULL);
	    /* XXX add interface_name if required */

        if (smsc->socket != -1) break;

        usleep(1000);
    }

    /* Empty the buffer, there might be an evil ghost inside... */

    memset(smsc->buffer, 0, smsc->bufsize);
    smsc->buflen = 0;

    /* Expect the protocol string "CIMD rel 1.37\n" */
    for (;;) {
        ret = smscenter_read_into_buffer(smsc);
        if (ret < 0) goto logout;
        if (strstr(smsc->buffer, "CIMD rel 1.37\n") != NULL)
            break;
        usleep(1000);
    }
    smscenter_remove_from_buffer(smsc, smsc->buflen);

    /* send login string */
    sprintf(tmpbuff, "%c%s%c%s%c%s%c%s%c%c",
            0x02,
            "01", 0x09,
            smsc->cimd_username, 0x09,
            smsc->cimd_password, 0x09,
            "11",
            0x03, 0x0A);

    ret = write_to_socket(smsc->socket, tmpbuff);
    if (ret < 0) goto logout;

    /* get an acknowledge message */

    smsc->cimd_last_spoke = 0;

    if (expect_acknowledge(smsc, &cmd, &err) < 1)
        goto logout;

    debug("bb.sms.cimd", 0, "cimd_connect_tcpip: logged in");

    gw_free(tmpbuff);

    return 1;

logout:
    close(smsc->socket);
    gw_free(tmpbuff);
    return 0;
}

/******************************************************************************
* Yeah, we got the message!
*/
static int send_acknowledge(SMSCenter *smsc)
{

    char tmpbuff[100];
    int tmpint;

    if (tmpbuff == NULL) {
        error(0, "cimd_send_acknowledge: memory allocation failure");
        goto error;
    }

    memset(tmpbuff, 0, sizeof(tmpbuff));

    sprintf(tmpbuff, "\2\6\t11\3\n");

    tmpint = write_to_socket(smsc->socket, tmpbuff);
    if (tmpint == -1) {
        error(0, "cimd_send_acknowledge: connection failure");
        goto error;
    }

    return 0;

error:
    debug("bb.sms.cimd", 0, "cimd_send_acknowledge: failed");
    return -1;
}

/******************************************************************************
* Wait for the Nokia piece of *!%&%*^H^H^H^H^H^H^H^H^H^H^H^H^H^H^H^H SMSC
* to catch up with our swift operation, block until... (~1sec?)
*/
static int expect_acknowledge(SMSCenter *smsc, int *cmd, int *err)
{

    char *end_of_dataset = NULL;
    char *ack = NULL, *nck = NULL;
    char *cmdspecifier = NULL, *errorspecifier = NULL;
    int ret = 0;

#if 0
    time_t thetime;
    time(&thetime);
#endif

    if (smsc == NULL) goto error;

    /* Loop until we get an acknowledgement message. */
    for (;;) {

        /* If the server is configured in to end a dataset with a \n */
        end_of_dataset = memchr(smsc->buffer, '\n', smsc->buflen);
        if (end_of_dataset != NULL) break;

        /* If the server is configured in to end a dataset with a \3 */
        end_of_dataset = memchr(smsc->buffer, 0x03, smsc->buflen);
        if (end_of_dataset != NULL) break;

        ret = smscenter_read_into_buffer(smsc);
        if (ret <= 0) {
            if (errno == EAGAIN) continue;
            if (errno == EINTR) continue;
            return -1;
        }

        usleep(500);

#if 0
        /* Abort if no results in 30 seconds */

        if (time(NULL) > (thetime + 30)) {

            error(0, "timeout occurred, maybe the connection was broken?");
            if (errno == EPIPE) {
                error(0, "broken pipe");
            } /* if errno */

            goto error;

        } /* if time */
#endif
    }

    /* Check if our request was answered or denied */
    ack = memchr(smsc->buffer, 0x06, end_of_dataset - smsc->buffer);
    nck = memchr(smsc->buffer, 0x15, end_of_dataset - smsc->buffer);

    /* Get the command code from the acknowledge message */
    cmdspecifier = strstr(smsc->buffer, "\tC:");
    if (cmdspecifier != NULL)
        *cmd = strtol(cmdspecifier + 3, NULL, 10);
    else
        *cmd = 0;

    errorspecifier = strstr(smsc->buffer, "\tE:");
    if (errorspecifier != NULL)
        *err = strtol(errorspecifier + 3, NULL, 10);
    else
        *err = 0;

    debug("bb.sms.cimd", 0, "cimd_pending_smsmessage: smsc->buffer == <%s>", smsc->buffer);

    /* Remove the acknowledge message from the incoming buffer. */
    smscenter_remove_from_buffer(smsc, end_of_dataset - smsc->buffer + 1);

    /* if we got an acknowledge */
    if (ack != NULL) {
        info(0, "cimd_pending_smsmessage: got ACK");
        return 1;
    }

    /* if we got an NOT acknowledge */
    if (nck != NULL) {
        info(0, "cimd_pending_smsmessage: got NCK");
        return 0;
    }

    /* if we got an ERROR */
error:
    error(0, "cimd_expect_acknowledge failed");
    return -1;

}

/******************************************************************************
* Convert a string from ISO-8859-1 to the CIMD character set
*/
static int parse_iso88591_to_cimd(char* from, char* to,
        int length, int alt_charset)
{

    char *temp = to;

    if (from == NULL || to == NULL || length == 0)
        return -1;

    *to = '\0';

    while ((*from != '\0') && ((int) strlen(temp) < (length - 2))) {

        switch (*from) {

        case '@': strcat(to, "_Oa"); to += 3; break;
        case '£': strcat(to, "_L-"); to += 3; break;

        case '$':
            if (alt_charset == CIMD_PLAIN_DOLLAR_SIGN) {
                strcat(to, "$");
                to++;
            } else {
                strcat(to, "_$ ");
                to += 3;
            }
            break;

        case 'Å': strcat(to, "_A*"); to += 3; break;
        case 'å': strcat(to, "_a*"); to += 3; break;
        case 'ä': strcat(to, "_a\""); to += 3; break;
        case 'ö': strcat(to, "_o\""); to += 3; break;
        case 'Ä': strcat(to, "_A\""); to += 3; break;
        case 'Ö': strcat(to, "_O\""); to += 3; break;
        case '¥': strcat(to, "_Y-"); to += 3; break;
        case 'è': strcat(to, "_e`"); to += 3; break;
        case 'é': strcat(to, "_e´"); to += 3; break;
        case 'ù': strcat(to, "_u`"); to += 3; break;
        case 'ì': strcat(to, "_i`"); to += 3; break;
        case 'ò': strcat(to, "_o`"); to += 3; break;
        case 'Ç': strcat(to, "_C,"); to += 3; break;
        case 'Ø': strcat(to, "_O/"); to += 3; break;
        case 'ø': strcat(to, "_o/"); to += 3; break;
        case 'Æ': strcat(to, "_AE"); to += 3; break;
        case 'æ': strcat(to, "_ae"); to += 3; break;
        case 'ß': strcat(to, "_ss"); to += 3; break;
        case 'É': strcat(to, "_E´"); to += 3; break;
        case '¿': strcat(to, "_??"); to += 3; break;
        case 'Ü': strcat(to, "_U\""); to += 3; break;
        case 'ñ': strcat(to, "_n~"); to += 3; break;
        case 'ü': strcat(to, "_u\""); to += 3; break;
        case 'à': strcat(to, "_a`"); to += 3; break;
        case '¡': strcat(to, "_!!"); to += 3; break;
        case '_': strcat(to, "_--"); to += 3; break;
        case 'Ñ': strcat(to, "_N~"); to += 3; break;
        case '!': strcat(to, "!"); to++; break;
        case '"': strcat(to, "\""); to++; break;
        case '#': strcat(to, "#"); to++; break;
        case '¤': strcat(to, "¤"); to++; break;
        case '%': strcat(to, "%"); to++; break;
        case '&': strcat(to, "&"); to++; break;
        case '\'': strcat(to, "'"); to++; break;
        case '(': strcat(to, "("); to++; break;
        case ')': strcat(to, ")"); to++; break;
        case '*': strcat(to, "*"); to++; break;
        case '+': strcat(to, "+"); to++; break;
        case ',': strcat(to, ","); to++; break;
        case '-': strcat(to, "-"); to++; break;
        case '.': strcat(to, "."); to++; break;
        case '/': strcat(to, "/"); to++; break;
        case '0': strcat(to, "0"); to++; break;
        case '1': strcat(to, "1"); to++; break;
        case '2': strcat(to, "2"); to++; break;
        case '3': strcat(to, "3"); to++; break;
        case '4': strcat(to, "4"); to++; break;
        case '5': strcat(to, "5"); to++; break;
        case '6': strcat(to, "6"); to++; break;
        case '7': strcat(to, "7"); to++; break;
        case '8': strcat(to, "8"); to++; break;
        case '9': strcat(to, "9"); to++; break;
        case ':': strcat(to, ":"); to++; break;
        case ';': strcat(to, ";"); to++; break;
        case '<': strcat(to, "<"); to++; break;
        case '=': strcat(to, "="); to++; break;
        case '>': strcat(to, ">"); to++; break;
        case '?': strcat(to, "?"); to++; break;
        case 'A': strcat(to, "A"); to++; break;
        case 'B': strcat(to, "B"); to++; break;
        case 'C': strcat(to, "C"); to++; break;
        case 'D': strcat(to, "D"); to++; break;
        case 'E': strcat(to, "E"); to++; break;
        case 'F': strcat(to, "F"); to++; break;
        case 'G': strcat(to, "G"); to++; break;
        case 'H': strcat(to, "H"); to++; break;
        case 'I': strcat(to, "I"); to++; break;
        case 'J': strcat(to, "J"); to++; break;
        case 'K': strcat(to, "K"); to++; break;
        case 'L': strcat(to, "L"); to++; break;
        case 'M': strcat(to, "M"); to++; break;
        case 'N': strcat(to, "N"); to++; break;
        case 'O': strcat(to, "O"); to++; break;
        case 'P': strcat(to, "P"); to++; break;
        case 'Q': strcat(to, "Q"); to++; break;
        case 'R': strcat(to, "R"); to++; break;
        case 'S': strcat(to, "S"); to++; break;
        case 'T': strcat(to, "T"); to++; break;
        case 'U': strcat(to, "U"); to++; break;
        case 'V': strcat(to, "V"); to++; break;
        case 'W': strcat(to, "W"); to++; break;
        case 'X': strcat(to, "X"); to++; break;
        case 'Y': strcat(to, "Y"); to++; break;
        case 'Z': strcat(to, "Z"); to++; break;
        case 'a': strcat(to, "a"); to++; break;
        case 'b': strcat(to, "b"); to++; break;
        case 'c': strcat(to, "c"); to++; break;
        case 'd': strcat(to, "d"); to++; break;
        case 'e': strcat(to, "e"); to++; break;
        case 'f': strcat(to, "f"); to++; break;
        case 'g': strcat(to, "g"); to++; break;
        case 'h': strcat(to, "h"); to++; break;
        case 'i': strcat(to, "i"); to++; break;
        case 'j': strcat(to, "j"); to++; break;
        case 'k': strcat(to, "k"); to++; break;
        case 'l': strcat(to, "l"); to++; break;
        case 'm': strcat(to, "m"); to++; break;
        case 'n': strcat(to, "n"); to++; break;
        case 'o': strcat(to, "o"); to++; break;
        case 'p': strcat(to, "p"); to++; break;
        case 'q': strcat(to, "q"); to++; break;
        case 'r': strcat(to, "r"); to++; break;
        case 's': strcat(to, "s"); to++; break;
        case 't': strcat(to, "t"); to++; break;
        case 'u': strcat(to, "u"); to++; break;
        case 'v': strcat(to, "v"); to++; break;
        case 'w': strcat(to, "w"); to++; break;
        case 'x': strcat(to, "x"); to++; break;
        case 'y': strcat(to, "y"); to++; break;
        case 'z': strcat(to, "z"); to++; break;
        case ' ': strcat(to, " "); to++; break;
        case '\r': strcat(to, "\r"); to++; break;
        case '\n': strcat(to, "\n"); to++; break;

        default: strcat(to, "_??"); to += 3; break;
        }
        from++;
    }

    *to = '\0';

    return strlen(temp);
}


/******************************************************************************
* Convert a string from the CIMD character set to ISO-8859-1
*/
static int parse_cimd_to_iso88591(char* from, char* to, int length)
{

    int my_int, temp_int;

    *to = '\0';

    for (my_int = 0; my_int < (int)strlen(from) && (int)strlen(to) < length; ) {

        if (from[my_int] == '_' && from[my_int + 1] == 'a' && from[my_int + 2] == '"') {
            strcat(to, "ä");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'a' && from[my_int + 2] == '*') {
            strcat(to, "å");
            my_int += 3;
        }

        /* argh, this drives me nu---uuutts */

        else if (from[my_int] == '@') {
            strcat(to, "@");
            my_int ++;
        }
        else if (from[my_int] == '_' && from[my_int + 1] == 'O' && from[my_int + 2] == 'a') {
            strcat(to, "@");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'L' && from[my_int + 2] == '-') {
            strcat(to, "£");
            my_int += 3;
        }

        /* this following one is against specifications but what to do
         * when it works?!? (the other is NOT used) rpr 1.10. */

        else if (from[my_int] == '$') {
            strcat(to, "$");
            my_int ++;
        }
        else if (from[my_int] == '_' && from[my_int + 1] == '$' && from[my_int + 2] == ' ') {
            strcat(to, "$");
            my_int += 3;
        }
        else if (from[my_int] == '_' && from[my_int + 1] == 'A' && from[my_int + 2] == '*') {
            strcat(to, "Å");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'o' && from[my_int + 2] == '"') {
            strcat(to, "ö");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'A' && from[my_int + 2] == '"') {
            strcat(to, "Ä");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'O' && from[my_int + 2] == '"') {
            strcat(to, "Ö");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'Y' && from[my_int + 2] == '-') {
            strcat(to, "¥");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'e' && from[my_int + 2] == '`') {
            strcat(to, "è");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'e' && from[my_int + 2] == '´') {
            strcat(to, "é");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'u' && from[my_int + 2] == '`') {
            strcat(to, "ù");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'i' && from[my_int + 2] == '`') {
            strcat(to, "ì");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'o' && from[my_int + 2] == '`') {
            strcat(to, "ò");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'C' && from[my_int + 2] == ',') {
            strcat(to, "Ç");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'O' && from[my_int + 2] == '/') {
            strcat(to, "Ø");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'o' && from[my_int + 2] == '/') {
            strcat(to, "ø");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'A' && from[my_int + 2] == 'E') {
            strcat(to, "Æ");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'a' && from[my_int + 2] == 'e') {
            strcat(to, "æ");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 's' && from[my_int + 2] == 's') {
            strcat(to, "ß");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'E' && from[my_int + 2] == '´') {
            strcat(to, "É");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == '?' && from[my_int + 2] == '?') {
            strcat(to, "¿");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'U' && from[my_int + 2] == '"') {
            strcat(to, "Ü");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'n' && from[my_int + 2] == '~' ) {
            strcat(to, "ñ");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'u' && from[my_int + 2] == '"') {
            strcat(to, "ü");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'a' && from[my_int + 2] == '`') {
            strcat(to, "à");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == '!' && from[my_int + 2] == '!') {
            strcat(to, "¡");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == '-' && from[my_int + 2] == '-') {
            strcat(to, "_");
            my_int += 3;
        } else if (from[my_int] == '_' && from[my_int + 1] == 'N' && from[my_int + 2] == '~') {
            strcat(to, "_");
            my_int += 3;
        }

        /* I just LOVE the designers of this protocol -mg */
        else if (from[my_int] == ']') {
            strcat(to, "Å");
            my_int++;
        } else if (from[my_int] == '}') {
            strcat(to, "å");
            my_int++;
        } else if (from[my_int] == '[') {
            strcat(to, "Ä");
            my_int++;
        } else if (from[my_int] == '{') {
            strcat(to, "ä");
            my_int++;
        } else if (from[my_int] == '\\') {
            strcat(to, "Ö");
            my_int++;
        } else if (from[my_int] == '|') {
            strcat(to, "ö");
            my_int++;
        }
        else if (from[my_int] == '!') {
            strcat(to, "!");
            my_int++;
        } else if (from[my_int] == '"') {
            strcat(to, "\"");
            my_int++;
        } else if (from[my_int] == '#') {
            strcat(to, "#");
            my_int++;
        } else if (from[my_int] == '¤') {
            strcat(to, "¤");
            my_int++;
        } else if (from[my_int] == '%') {
            strcat(to, "%");
            my_int++;
        } else if (from[my_int] == '&') {
            strcat(to, "&");
            my_int++;
        } else if (from[my_int] == '\'') {
            strcat(to, "'");
            my_int++;
        } else if (from[my_int] == '(') {
            strcat(to, "(");
            my_int++;
        } else if (from[my_int] == ')') {
            strcat(to, ")");
            my_int++;
        } else if (from[my_int] == '*') {
            strcat(to, "*");
            my_int++;
        } else if (from[my_int] == '+') {
            strcat(to, "+");
            my_int++;
        } else if (from[my_int] == ',') {
            strcat(to, ",");
            my_int++;
        } else if (from[my_int] == '-') {
            strcat(to, "-");
            my_int++;
        } else if (from[my_int] == '.') {
            strcat(to, ".");
            my_int++;
        } else if (from[my_int] == '/') {
            strcat(to, "/");
            my_int++;
        } else if (from[my_int] == '0') {
            strcat(to, "0");
            my_int++;
        } else if (from[my_int] == '1') {
            strcat(to, "1");
            my_int++;
        } else if (from[my_int] == '2') {
            strcat(to, "2");
            my_int++;
        } else if (from[my_int] == '3') {
            strcat(to, "3");
            my_int++;
        } else if (from[my_int] == '4') {
            strcat(to, "4");
            my_int++;
        } else if (from[my_int] == '5') {
            strcat(to, "5");
            my_int++;
        } else if (from[my_int] == '6') {
            strcat(to, "6");
            my_int++;
        } else if (from[my_int] == '7') {
            strcat(to, "7");
            my_int++;
        } else if (from[my_int] == '8') {
            strcat(to, "8");
            my_int++;
        } else if (from[my_int] == '9') {
            strcat(to, "9");
            my_int++;
        } else if (from[my_int] == ':') {
            strcat(to, ":");
            my_int++;
        } else if (from[my_int] == ';') {
            strcat(to, ";");
            my_int++;
        } else if (from[my_int] == '<') {
            strcat(to, "<");
            my_int++;
        } else if (from[my_int] == '=') {
            strcat(to, "=");
            my_int++;
        } else if (from[my_int] == '>') {
            strcat(to, ">");
            my_int++;
        } else if (from[my_int] == '?') {
            strcat(to, "?");
            my_int++;
        } else if (from[my_int] == 'A') {
            strcat(to, "A");
            my_int++;
        } else if (from[my_int] == 'B') {
            strcat(to, "B");
            my_int++;
        } else if (from[my_int] == 'C') {
            strcat(to, "C");
            my_int++;
        } else if (from[my_int] == 'D') {
            strcat(to, "D");
            my_int++;
        } else if (from[my_int] == 'E') {
            strcat(to, "E");
            my_int++;
        } else if (from[my_int] == 'F') {
            strcat(to, "F");
            my_int++;
        } else if (from[my_int] == 'G') {
            strcat(to, "G");
            my_int++;
        } else if (from[my_int] == 'H') {
            strcat(to, "H");
            my_int++;
        } else if (from[my_int] == 'I') {
            strcat(to, "I");
            my_int++;
        } else if (from[my_int] == 'J') {
            strcat(to, "J");
            my_int++;
        } else if (from[my_int] == 'K') {
            strcat(to, "K");
            my_int++;
        } else if (from[my_int] == 'L') {
            strcat(to, "L");
            my_int++;
        } else if (from[my_int] == 'M') {
            strcat(to, "M");
            my_int++;
        } else if (from[my_int] == 'N') {
            strcat(to, "N");
            my_int++;
        } else if (from[my_int] == 'O') {
            strcat(to, "O");
            my_int++;
        } else if (from[my_int] == 'P') {
            strcat(to, "P");
            my_int++;
        } else if (from[my_int] == 'Q') {
            strcat(to, "Q");
            my_int++;
        } else if (from[my_int] == 'R') {
            strcat(to, "R");
            my_int++;
        } else if (from[my_int] == 'S') {
            strcat(to, "S");
            my_int++;
        } else if (from[my_int] == 'T') {
            strcat(to, "T");
            my_int++;
        } else if (from[my_int] == 'U') {
            strcat(to, "U");
            my_int++;
        } else if (from[my_int] == 'V') {
            strcat(to, "V");
            my_int++;
        } else if (from[my_int] == 'W') {
            strcat(to, "W");
            my_int++;
        } else if (from[my_int] == 'X') {
            strcat(to, "X");
            my_int++;
        } else if (from[my_int] == 'Y') {
            strcat(to, "Y");
            my_int++;
        } else if (from[my_int] == 'Z') {
            strcat(to, "Z");
            my_int++;
        } else if (from[my_int] == 'a') {
            strcat(to, "a");
            my_int++;
        } else if (from[my_int] == 'b') {
            strcat(to, "b");
            my_int++;
        } else if (from[my_int] == 'c') {
            strcat(to, "c");
            my_int++;
        } else if (from[my_int] == 'd') {
            strcat(to, "d");
            my_int++;
        } else if (from[my_int] == 'e') {
            strcat(to, "e");
            my_int++;
        } else if (from[my_int] == 'f') {
            strcat(to, "f");
            my_int++;
        } else if (from[my_int] == 'g') {
            strcat(to, "g");
            my_int++;
        } else if (from[my_int] == 'h') {
            strcat(to, "h");
            my_int++;
        } else if (from[my_int] == 'i') {
            strcat(to, "i");
            my_int++;
        } else if (from[my_int] == 'j') {
            strcat(to, "j");
            my_int++;
        } else if (from[my_int] == 'k') {
            strcat(to, "k");
            my_int++;
        } else if (from[my_int] == 'l') {
            strcat(to, "l");
            my_int++;
        } else if (from[my_int] == 'm') {
            strcat(to, "m");
            my_int++;
        } else if (from[my_int] == 'n') {
            strcat(to, "n");
            my_int++;
        } else if (from[my_int] == 'o') {
            strcat(to, "o");
            my_int++;
        } else if (from[my_int] == 'p') {
            strcat(to, "p");
            my_int++;
        } else if (from[my_int] == 'q') {
            strcat(to, "q");
            my_int++;
        } else if (from[my_int] == 'r') {
            strcat(to, "r");
            my_int++;
        } else if (from[my_int] == 's') {
            strcat(to, "s");
            my_int++;
        } else if (from[my_int] == 't') {
            strcat(to, "t");
            my_int++;
        } else if (from[my_int] == 'u') {
            strcat(to, "u");
            my_int++;
        } else if (from[my_int] == 'v') {
            strcat(to, "v");
            my_int++;
        } else if (from[my_int] == 'w') {
            strcat(to, "w");
            my_int++;
        } else if (from[my_int] == 'x') {
            strcat(to, "x");
            my_int++;
        } else if (from[my_int] == 'y') {
            strcat(to, "y");
            my_int++;
        } else if (from[my_int] == 'z') {
            strcat(to, "z");
            my_int++;
        } else if (from[my_int] == ' ') {
            strcat(to, " ");
            my_int++;
        } else if (from[my_int] == '\r') {
            strcat(to, "\r");
            my_int++;
        } else if (from[my_int] == '\n') {
            strcat(to, "\n");
            my_int++;
        }
        else { /* of course it might be that nothing happened */
            debug("bb.sms.cimd", 0, "parse: [%c:%02X %c:%02X %c:%02X]", from[my_int],
                  from[my_int], from[my_int + 1], from[my_int + 1],
                  from[my_int + 2], from[my_int + 2]);

            temp_int = strlen(to);
            to[temp_int] = 0xBF; 	/* '¿' */
            to[temp_int + 1] = '\0';
            my_int++;
        }

    } /* for */

    return strlen(to);
}
