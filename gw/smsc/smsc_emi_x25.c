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
* smsc_emi.c - implement interface to the CMG SMS Center (UCP/EMI).
* Mikael Gueck for WapIT Ltd.
*/

/* This file implements two smsc interfaces: EMI_X25 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/ioctl.h>

#include "gwlib/gwlib.h"
#include "smsc.h"
#include "smsc_p.h"
#include "alt_charsets.h"
#include "sms.h"

#ifndef CRTSCTS
#define CRTSCTS 0
#endif

/******************************************************************************
* Static functions
*/
static int get_data(SMSCenter *smsc, char *buff, int length);

static int put_data(SMSCenter *smsc, char *buff, int length, int is_backup);

static int memorybuffer_append_data(SMSCenter *smsc, char *buff, int length);

static int memorybuffer_insert_data(SMSCenter *smsc, char *buff, int length);

static int memorybuffer_has_rawmessage(SMSCenter *smsc, int type, char auth);

static int memorybuffer_cut_rawmessage(SMSCenter *smsc, char *buff, int length);

static int parse_rawmessage_to_msg(SMSCenter *smsc, Msg **msg,
                                   char *rawmessage, int length);

static int parse_msg_to_rawmessage(SMSCenter *smsc, Msg *msg,
                                   char *rawmessage, int length);

static int acknowledge_from_rawmessage(SMSCenter *smsc,
                                   char *rawmessage, int length);

static int parse_emi_to_iso88591(char *from, char *to,
                                 int length, int alt_charset);

static int parse_iso88591_to_emi(char *from, char *to,
                                 int length, int alt_charset);
static int parse_binary_to_emi(char *from, char *to, int length);

static int at_dial(char *device, char *phonenum,
                   char *at_prefix, time_t how_long);
static int guarantee_link(SMSCenter *smsc);


static void generate_checksum(const unsigned char *buffer,
                              unsigned char *checksum_out);
static int wait_for_ack(SMSCenter *smsc, int op_type);


static char char_iso_to_sms(unsigned char from, int alt_charset);
static char char_sms_to_iso(unsigned char from, int alt_charset);

/******************************************************************************
* Open the connection and log in - handshake baby
*/
static int emi_open_connection(SMSCenter *smsc)
{
    char tmpbuff[1024];

    sprintf(tmpbuff, "/dev/%s", smsc->emi_serialdevice);
    smsc->emi_fd = at_dial(tmpbuff, smsc->emi_phonenum, "ATD", 30);

    if (smsc->emi_fd <= 0)
        return -1;

    return 0;
}

/* open EMI smscenter */

SMSCenter *emi_open(char *phonenum, char *serialdevice, char *username, char *password)
{
    SMSCenter *smsc;

    smsc = smscenter_construct();
    if (smsc == NULL)
        goto error;

    smsc->type = SMSC_TYPE_EMI_X25;

    smsc->emi_phonenum = gw_strdup(phonenum);
    smsc->emi_serialdevice = gw_strdup(serialdevice);
    smsc->emi_username = gw_strdup(username);
    smsc->emi_password = gw_strdup(password);

    smsc->emi_current_msg_number = 0;

    if (emi_open_connection(smsc) < 0)
        goto error;

    sprintf(smsc->name, "EMI:%s:%s", smsc->emi_phonenum,
            smsc->emi_username);
    return smsc;

error:
    error(0, "emi_open failed");
    smscenter_destruct(smsc);
    return NULL;
}

int emi_reopen(SMSCenter *smsc)
{
    emi_close(smsc);

    if (emi_open_connection(smsc) < 0) {
        error(0, "emi_reopen failed");
        return -1;
    }
    return 0;
}

int emi_close(SMSCenter *smsc)
{
    return emi_close_ip(smsc);
}

static int emi_fill_ucp60_login(char *buf, char *OAdC, char *passwd) {
    int max_ia5passwd_len;
    char *ia5passwd;

    max_ia5passwd_len = strlen(passwd) * 2 + 1;
    ia5passwd = gw_malloc(max_ia5passwd_len);

    if (parse_binary_to_emi(passwd, ia5passwd, strlen(passwd)) < 0) {
        error(0, "parse_binary_to_emi failed");
        gw_free(ia5passwd);
        return -1;
    }

    sprintf(buf, "%s/%c/%c/%c/%s//%s/////",
	    OAdC,      /* OAdC: Address code originator */
	    '6',       /* OTON: 6 = Abbreviated number (short number alias) */
	    '5',       /* ONPI: 5 = Private (TCP/IP address/abbreviated number address) */
	    '1',       /* STYP: 1 = open session */
	    ia5passwd, /* PWD:  Current password encoded into IA5 characters */
	    "0100"     /* VERS: Version number  0100 */
	    );

    gw_free(ia5passwd);
    return 0;
}

static int emi_open_session(SMSCenter *smsc)
{
    char message_whole  [1024];
    char message_body   [1024];
    char message_header [50];
    char message_footer [10];
    char my_buffer      [1024];
    int length;

    memset(message_whole,  0, sizeof(message_whole));
    memset(message_body,   0, sizeof(message_body));
    memset(message_header, 0, sizeof(message_header));
    memset(message_footer, 0, sizeof(message_footer));

    if (emi_fill_ucp60_login(message_body, smsc->emi_username, smsc->emi_password) < 0) {
        error(0, "emi_fill_ucp60_login failed");
        return -1;
    }

    length = strlen(message_body);
    length += 13;  /* header (fixed) */
    length += 2;   /* footer (fixed) */
    length += 2;   /* slashes between header, body, footer */

    sprintf(message_header, "%02i/%05i/O/60",
            (smsc->emi_current_msg_number++ % 100), length);
    
    /* FOOTER */

    sprintf(my_buffer, "%s/%s/", message_header, message_body);
    generate_checksum((unsigned char *)my_buffer, (unsigned char *)message_footer);

    sprintf(message_whole, "\x02%s/%s/%s\x03", message_header,
            message_body, message_footer);

    debug("bb.sms.emi", 0, "final UCP60 msg: <%s>", message_whole);

    put_data(smsc, message_whole, strlen(message_whole), 0);

    if (!wait_for_ack(smsc, 60)) {
	info(0, "emi_open_session: wait for ack failed!");
	return -1;
    }

    return 0;
}


/*******************************************************
 * the actual protocol open... quite simple here */

static int emi_open_connection_ip(SMSCenter *smsc)
{
    smsc->emi_fd =
        tcpip_connect_to_server_with_port(smsc->emi_hostname,
                                          smsc->emi_port, smsc->emi_our_port,
					  NULL);
	    /* XXX add interface_name if required */
    if (smsc->emi_fd < 0)
        return -1;

    if (smsc->emi_username && smsc->emi_password) {
	return emi_open_session(smsc);
    }
    
    return 0;
}


int emi_reopen_ip(SMSCenter *smsc)
{
    emi_close_ip(smsc);

    return emi_open_connection_ip(smsc);
}


int emi_close_ip(SMSCenter *smsc)
{

    if (smsc->emi_fd == -1) {
        info(0, "Trying to close already closed EMI, ignoring");
        return 0;
    }
    close(smsc->emi_fd);
    smsc->emi_fd = -1;

    return 0;
}


/******************************************************************************
* Check if the buffers contain any messages
*/
int emi_pending_smsmessage(SMSCenter *smsc)
{

    char *tmpbuff;
    int n = 0;
    /*	time_t timenow; */

    /* Block until we have a connection */
    guarantee_link(smsc);

    /* If we have MO-message, then act (return 1) */
    if (memorybuffer_has_rawmessage(smsc, 52, 'O') > 0 ||
        memorybuffer_has_rawmessage(smsc, 1, 'O') > 0 )
        return 1;

    tmpbuff = gw_malloc(10 * 1024);
    memset(tmpbuff, 0, 10*1024);

    /* check for data */
    n = get_data(smsc, tmpbuff, 10 * 1024);
    if (n > 0)
        memorybuffer_insert_data(smsc, tmpbuff, n);

    /* delete all ACKs/NACKs/whatever */
    while (memorybuffer_has_rawmessage(smsc, 51, 'R') > 0 ||
           memorybuffer_has_rawmessage(smsc, 1, 'R') > 0)
        memorybuffer_cut_rawmessage(smsc, tmpbuff, 10*1024);

    gw_free(tmpbuff);

    /* If we have MO-message, then act (return 1) */

    if (memorybuffer_has_rawmessage(smsc, 52, 'O') > 0 ||
        memorybuffer_has_rawmessage(smsc, 1, 'O') > 0)
        return 1;

    /*
    	time(&timenow);
    	if( (smsc->emi_last_spoke + 60*20) < timenow) {
    		time(&smsc->emi_last_spoke);
    	}
    */

    return 0;

}




/******************************************************************************
 * Submit (send) a Mobile Terminated message to the EMI server
 */
int emi_submit_msg(SMSCenter *smsc, Msg *omsg)
{
    char *tmpbuff = NULL;

    if (smsc == NULL) goto error;
    if (omsg == NULL) goto error;

    tmpbuff = gw_malloc(10 * 1024);
    memset(tmpbuff, 0, 10*1024);

    if (parse_msg_to_rawmessage(smsc, omsg, tmpbuff, 10*1024) < 1)
        goto error;

    if (put_data(smsc, tmpbuff, strlen(tmpbuff), 0) < 0) {
        info(0, "put_data failed!");
        goto error;
    }

    wait_for_ack(smsc, 51);

    /*	smsc->emi_current_msg_number += 1; */
    debug("bb.sms.emi", 0, "Submit Ok...");

    gw_free(tmpbuff);
    return 0;

error:
    debug("bb.sms.emi", 0, "Submit Error...");

    gw_free(tmpbuff);
    return -1;
}

/******************************************************************************
* Receive a Mobile Terminated message to the EMI server
*/
int emi_receive_msg(SMSCenter *smsc, Msg **tmsg)
{
    char *tmpbuff;
    Msg *msg = NULL;

    *tmsg = NULL;

    tmpbuff = gw_malloc(10 * 1024);
    memset(tmpbuff, 0, 10*1024);

    /* get and delete message from buffer */
    memorybuffer_cut_rawmessage(smsc, tmpbuff, 10*1024);
    parse_rawmessage_to_msg(smsc, &msg, tmpbuff, strlen(tmpbuff));

    /* yeah yeah, I got the message... */
    acknowledge_from_rawmessage(smsc, tmpbuff, strlen(tmpbuff));

    /* return with the joyful news */
    gw_free(tmpbuff);

    if (msg == NULL) goto error;

    *tmsg = msg;

    return 1;

error:
    gw_free(tmpbuff);
    msg_destroy(msg);
    return -1;
}


/******************************************************************************
* Internal functions
*/


/******************************************************************************
* Guarantee that we have a link
*/
static int guarantee_link(SMSCenter *smsc)
{
    int need_to_connect = 0;

    /* If something is obviously wrong. */
    if (strstr(smsc->buffer, "OK")) need_to_connect = 1;
    if (strstr(smsc->buffer, "NO CARRIER")) need_to_connect = 1;
    if (strstr(smsc->buffer, "NO DIALTONE")) need_to_connect = 1;

    /* Clear the buffer */
    while (need_to_connect) {
        /* Connect */
        need_to_connect = emi_open_connection(smsc) < 0;

        /* Clear the buffer so that the next call to guarantee
           doesn't find the "NO CARRIER" string again. */
        smsc->buflen = 0;
        memset(smsc->buffer, 0, smsc->bufsize);
    }

    return 0;
}

static int at_dial(char *device, char *phonenum, char *at_prefix, time_t how_long)
{
    char tmpbuff[1024];
    int howmanyread = 0;
    int thistime = 0;
    int redial;
    int fd = -1;
    int ret;
    time_t timestart;
    struct termios tios;

    /* The time at the start of the function is used when
       determining whether we have used up our allotted
       dial time and have to abort. */
    time(&timestart);

    /* Open the device properly. Remember to set the
       access codes correctly. */
    fd = open(device, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (fd == -1) {
        error(errno, "at_dial: error opening character device <%s>", device);
        goto error;
    }
    tcflush(fd, TCIOFLUSH);

    /* The speed initialisation is pretty important. */
    tcgetattr(fd, &tios);
#if defined(B115200)
    cfsetospeed(&tios, B115200);
    cfsetispeed(&tios, B115200);
#elif defined(B76800)
    cfsetospeed(&tios, B76800);
    cfsetispeed(&tios, B76800);
#elif defined(B57600)
    cfsetospeed(&tios, B57600);
    cfsetispeed(&tios, B57600);
#elif defined(B38400)
    cfsetospeed(&tios, B38400);
    cfsetispeed(&tios, B38400);
#elif defined(B19200)
    cfsetospeed(&tios, B19200);
    cfsetispeed(&tios, B19200);
#elif defined(B9600)
    cfsetospeed(&tios, B9600);
    cfsetispeed(&tios, B9600);
#endif
    kannel_cfmakeraw(&tios);
    tios.c_cflag |= (HUPCL | CREAD | CRTSCTS);
    ret = tcsetattr(fd, TCSANOW, &tios);
    if (ret == -1) {
        error(errno, "EMI[X25]: at_dial: fail to set termios attribute");
    }

    /* Dial using an AT command string. */
    for (redial = 1; redial; ) {
        info(0, "at_dial: dialing <%s> on <%s> for <%i> seconds",
             phonenum, device,
             (int)(how_long - (time(NULL) - timestart)));

        /* Send AT dial request. */
        howmanyread = 0;
        sprintf(tmpbuff, "%s%s\r\n", at_prefix, phonenum);
        ret = write(fd, tmpbuff, strlen(tmpbuff));  /* errors... -mg */
        memset(&tmpbuff, 0, sizeof(tmpbuff));

        /* Read the answer to the AT command and react accordingly. */
        for (; ; ) {
            /* We don't want to dial forever */
            if (how_long != 0 && time(NULL) > timestart + how_long)
                goto timeout;

            /* We don't need more space for dialout */
            if (howmanyread >= (int) sizeof(tmpbuff))
                goto error;

            /* We read 1 char a time so that we don't
               accidentally read past the modem chat and
               into the SMSC datastream -mg */
            thistime = read(fd, &tmpbuff[howmanyread], 1);
            if (thistime == -1) {
                if (errno == EAGAIN) continue;
                if (errno == EINTR) continue;
                goto error;
            } else {
                howmanyread += thistime;
            }

            /* Search for the newline on the AT status line. */
            if (tmpbuff[howmanyread - 1] == '\r'
                || tmpbuff[howmanyread - 1] == '\n') {

                /* XXX ADD ALL POSSIBLE CHAT STRINGS XXX */

                if (strstr(tmpbuff, "CONNECT") != NULL) {
                    debug("bb.sms.emi", 0, "at_dial: CONNECT");
                    redial = 0;
                    break;

                } else if (strstr(tmpbuff, "NO CARRIER") != NULL) {
                    debug("bb.sms.emi", 0, "at_dial: NO CARRIER");
                    redial = 1;
                    break;

                } else if (strstr(tmpbuff, "BUSY") != NULL) {
                    debug("bb.sms.emi", 0, "at_dial: BUSY");
                    redial = 1;
                    break;

                } else if (strstr(tmpbuff, "NO DIALTONE") != NULL) {
                    debug("bb.sms.emi", 0, "at_dial: NO DIALTONE");
                    redial = 1;
                    break;

                }

            } /* End of if lastchr=='\r'||'\n'. */

            /* Thou shall not consume all system resources
               by repeatedly looping a strstr search when
               the string update latency is very high as it
               is in serial communication. -mg */
            usleep(1000);

        } /* End of read loop. */

        /* Thou shall not flood the modem with dial requests. -mg */
        sleep(1);

    } /* End of dial loop. */

    debug("bb.sms.emi", 0, "at_dial: done with dialing");
    return fd;

timeout:
    error(0, "at_dial timed out");
    close(fd);
    return -1;

error:
    error(0, "at_dial failed");
    close(fd);
    return -1;
}

/******************************************************************************
 * Wait for an ACK or NACK from the remote
 *
 * REQUIRED by the protocol that it must be waited...
 */
static int wait_for_ack(SMSCenter *smsc, int op_type)
{
    char *tmpbuff;
    int found = 0;
    int n;
    time_t start;

    tmpbuff = gw_malloc(10 * 1024);
    memset(tmpbuff, 0, 10*1024);
    start = time(NULL);
    do {
        /* check for data */
        n = get_data(smsc, tmpbuff, 1024 * 10);

	/* At least the X.31 interface wants to append the data.
	   Kalle, what about the TCP/IP interface? Am I correct
	   that you are assuming that the message arrives in a 
	   single read(2)? -mg */
	if (n > 0)
	    memorybuffer_append_data(smsc, tmpbuff, n);

        /* act on data */
        if (memorybuffer_has_rawmessage(smsc, op_type, 'R') > 0) {
            memorybuffer_cut_rawmessage(smsc, tmpbuff, 10*1024);
            debug("bb.sms.emi", 0, "Found ACK/NACK: <%s>", tmpbuff);
            found = 1;
        }
    } while (!found && ((time(NULL) - start) < 5));

    gw_free(tmpbuff);
    return found;
}


/******************************************************************************
 * Get the modem buffer data to buff, return the amount read
 *
 * Reads from main fd, but also from backup-fd - does accept if needed
 */
static int get_data(SMSCenter *smsc, char *buff, int length)
{
    int n = 0;

    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    fd_set rf;
    struct timeval to;
    int ret;

    memset(buff, 0, length);

    if (smsc->type == SMSC_TYPE_EMI_X25) {
        tcdrain(smsc->emi_fd);
        n = read(smsc->emi_fd, buff, length);
        return n;
    }

    FD_ZERO(&rf);
    if (smsc->emi_fd >= 0) FD_SET(smsc->emi_fd, &rf);
    if (smsc->emi_secondary_fd >= 0) FD_SET(smsc->emi_secondary_fd, &rf);
    if (smsc->emi_backup_fd > 0) FD_SET(smsc->emi_backup_fd, &rf);

    FD_SET(0, &rf);
    to.tv_sec = 0;
    to.tv_usec = 100;

    ret = select(FD_SETSIZE, &rf, NULL, NULL, &to);

    if (ret > 0) {
        if (smsc->emi_secondary_fd >= 0 && FD_ISSET(smsc->emi_secondary_fd, &rf)) {
            n = read(smsc->emi_secondary_fd, buff, length - 1);

            if (n == -1) {
                error(errno, "Error - Secondary socket closed");
                close(smsc->emi_secondary_fd);
                smsc->emi_secondary_fd = -1;
            } else if (n == 0) {
                info(0, "Secondary socket closed by SMSC");
                close(smsc->emi_secondary_fd);
                smsc->emi_secondary_fd = -1;
            } else {			/* UGLY! We  put 'X' after message */
                buff[n] = 'X'; 	/* if it is from secondary fd!!!  */
                n++;
            }
        } else if (smsc->emi_fd >= 0 && FD_ISSET(smsc->emi_fd, &rf)) {
            n = read(smsc->emi_fd, buff, length);
            if (n == 0) {
                close(smsc->emi_fd);
                info(0, "Main EMI socket closed by SMSC");
                smsc->emi_fd = -1; 	/* ready to be re-opened */
            }
        }
        if ((smsc->emi_backup_fd > 0) && FD_ISSET(smsc->emi_backup_fd, &rf)) {
            if (smsc->emi_secondary_fd == -1) {
		Octstr *ip, *allow;
		
                smsc->emi_secondary_fd = accept(smsc->emi_backup_fd,
			  (struct sockaddr *)&client_addr, &client_addr_len);

		ip = host_ip(client_addr);
		if (smsc->emi_backup_allow_ip == NULL)
		    allow = NULL;
		else
		    allow = octstr_create(smsc->emi_backup_allow_ip);
		if (is_allowed_ip(allow, octstr_imm("*.*.*.*"), ip) == 0) {
		    info(0, "SMSC secondary connection tried from <%s>, "
		    	    "disconnected",
			    octstr_get_cstr(ip));
		    octstr_destroy(ip);
		    octstr_destroy(allow);
		    close(smsc->emi_secondary_fd);
		    smsc->emi_secondary_fd = -1;
		    return 0;
		}
                info(0, "Secondary socket opened by SMSC from <%s>",
		     octstr_get_cstr(ip));
		octstr_destroy(ip);
		octstr_destroy(allow);
            } else
                info(0, "New connection request while old secondary is open!");
        }
    }
    if (n > 0) {
        debug("bb.sms.emi", 0, "get_data:Read %d bytes: <%.*s>", n, n, buff);
        debug("bb.sms.emi", 0, "get_data:smsc->buffer == <%s>", smsc->buffer);
    }
    return n;

}

/******************************************************************************
* Put the buff data to the modem buffer, return the amount of data put
*/
static int put_data(SMSCenter *smsc, char *buff, int length, int is_backup)
{
    size_t len = length;
    int ret;
    int fd = -1;

    fd = smsc->emi_fd;
    tcdrain(smsc->emi_fd);

    /* Write until all data has been successfully written to the fd. */
    while (len > 0) {
        ret = write(fd, buff, len);
        if (ret == -1) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) continue;
            error(errno, "Writing to fd failed");
            return -1;
        }
        /* ret may be less than len, if the writing
           was interrupted by a signal. */
        len -= ret;
        buff += ret;
    }

    if (smsc->type == SMSC_TYPE_EMI_X25) {
        /* Make sure the data gets written immediately.
           Wait a while just to add some latency so
           that the modem (or the UART) doesn't choke
           on the data. */
        tcdrain(smsc->emi_fd);
        usleep(1000);
    }

    return 0;
}

/******************************************************************************
* Append the buff data to smsc->buffer
*/
static int memorybuffer_append_data(SMSCenter *smsc, char *buff, int length)
{
    while (smsc->bufsize < (smsc->buflen + length)) { /* buffer too small */
        char *p = gw_realloc(smsc->buffer, smsc->bufsize * 2);
        smsc->buffer = p;
        smsc->bufsize *= 2;
    }

    memcpy(smsc->buffer + smsc->buflen, buff, length);
    smsc->buflen += length;
    return 0;

}

/******************************************************************************
* Insert (put to head) the buff data to smsc->buffer
*/
static int memorybuffer_insert_data(SMSCenter *smsc, char *buff, int length)
{
    while (smsc->bufsize < (smsc->buflen + length)) { /* buffer too small */
        char *p = gw_realloc(smsc->buffer, smsc->bufsize * 2);
        smsc->buffer = p;
        smsc->bufsize *= 2;
    }
    memmove(smsc->buffer + length, smsc->buffer, smsc->buflen);
    memcpy(smsc->buffer, buff, length);
    smsc->buflen += length;
    return 0;

}

/******************************************************************************
* Check the smsc->buffer for a raw STX...ETX message
*/
static int memorybuffer_has_rawmessage(SMSCenter *smsc, int type, char auth)
{
    char tmpbuff[1024], tmpbuff2[1024];
    char *stx, *etx;

    stx = memchr(smsc->buffer, '\2', smsc->buflen);
    etx = memchr(smsc->buffer, '\3', smsc->buflen);

    if (stx && etx && stx < etx) {
        strncpy(tmpbuff, stx, etx - stx + 1);
        tmpbuff[etx - stx + 1] = '\0';
        if (auth)
            sprintf(tmpbuff2, "/%c/%02i/", auth, type);
        else
            sprintf(tmpbuff2, "/%02i/", type);

        if (strstr(tmpbuff, tmpbuff2) != NULL) {
            debug("bb.sms.emi", 0, "found message <%c/%02i>...msg <%s>", auth, type, tmpbuff);
            return 1;
        }
    }
    return 0;

}

/******************************************************************************
* Cut the first raw message from the smsc->buffer
* and put it in buff, return success 0, failure -1
*/
static int memorybuffer_cut_rawmessage(SMSCenter *smsc, char *buff, int length)
{

    char *stx, *etx;
    int size_of_cut_piece;
    int size_of_the_rest;

    /* We don't check for NULLs since we're sure that nobody has fooled
       around with smsc->buffer since has_rawmessage was last called... */

    stx = memchr(smsc->buffer, '\2', smsc->buflen);
    etx = memchr(smsc->buffer, '\3', smsc->buflen);

    if (*(etx + 1) == 'X')	/* secondary! UGLY KLUDGE */
        etx++;

    size_of_cut_piece = (etx - stx) + 1;
    size_of_the_rest = (smsc->buflen - size_of_cut_piece);

    if (length < size_of_cut_piece) {
        error(0, "the buffer you provided for cutting was too small");
        return -1;
    }

    /* move the part before our magic rawmessage to the safe house */
    memcpy(buff, stx, size_of_cut_piece);
    buff[size_of_cut_piece] = '\0'; 	/* NULL-terminate */

    /* move the stuff in membuffer one step down */
    memmove(stx, etx + 1, (smsc->buffer + smsc->bufsize) - stx );

    smsc->buflen -= size_of_cut_piece;

    return 0;

}

/******************************************************************************
* Parse the raw message to the Msg structure
*/
static int parse_rawmessage_to_msg(SMSCenter *smsc, Msg **msg,
                                   char *rawmessage, int length)
{

    char emivars[128][1024];
    char *leftslash, *rightslash;
    char isotext[2048];
    int msgnbr;
    int tmpint;

    msgnbr = -1;

    memset(isotext, 0, sizeof(isotext));

    strncpy(isotext, rawmessage, length);
    leftslash = isotext;

    for (tmpint = 0; leftslash != NULL; tmpint++) {
        rightslash = strchr(leftslash + 1, '/');

        if (rightslash == NULL)
            rightslash = strchr(leftslash + 1, '\3');

        if (rightslash == NULL)
            break;

        *rightslash = '\0';
        strcpy(emivars[tmpint], leftslash + 1);
        leftslash = rightslash;
    }

    if (strcmp(emivars[3], "01") == 0) {
        if (strcmp(emivars[7], "2") == 0) {
            strcpy(isotext, emivars[8]);
        } else if (strcmp(emivars[7], "3") == 0) {
            parse_emi_to_iso88591(emivars[8], isotext, sizeof(isotext),
                                  smsc->alt_charset);
        } else {
            error(0, "Unknown 01-type EMI SMS (%s)", emivars[7]);
            strcpy(isotext, "");
        }
    } else if (strcmp(emivars[3], "51") == 0) {
        parse_emi_to_iso88591(emivars[24], isotext, sizeof(isotext),
                              smsc->alt_charset);
    } else if (strcmp(emivars[3], "52") == 0) {
        parse_emi_to_iso88591(emivars[24], isotext, sizeof(isotext),
                              smsc->alt_charset);
    } else {
        error(0, "HEY WE SHOULD NOT BE HERE!! Type = %s", emivars[3]);
        strcpy(isotext, "");
    }

    *msg = msg_create(sms);
    if (*msg == NULL) goto error;

    (*msg)->sms.sender = octstr_create(emivars[5]);
    (*msg)->sms.receiver = octstr_create(emivars[4]);
    (*msg)->sms.msgdata = octstr_create(isotext);
    (*msg)->sms.udhdata = NULL;

    return msgnbr;

error:
    return -1;
}

/*
 * notify the SMSC that we got the message
 */
static int acknowledge_from_rawmessage(SMSCenter *smsc,
                                       char *rawmessage, int length)
{

    char emivars[128][1024];
    char timestamp[2048], sender[2048], receiver[2048];
    char emitext[2048], isotext[2048];
    char *leftslash, *rightslash;
    int msgnbr;
    int tmpint;
    int is_backup = 0;

    msgnbr = -1;
    memset(&sender, 0, sizeof(sender));
    memset(&receiver, 0, sizeof(receiver));
    memset(&emitext, 0, sizeof(emitext));
    memset(&isotext, 0, sizeof(isotext));
    memset(&timestamp, 0, sizeof(timestamp));

    strncpy(isotext, rawmessage, length);
    leftslash = isotext;

    if (isotext[length - 1] == 'X')
        is_backup = 1;

    for (tmpint = 0; leftslash != NULL; tmpint++) {
        rightslash = strchr(leftslash + 1, '/');

        if (rightslash == NULL)
            rightslash = strchr(leftslash + 1, '\3');

        if (rightslash == NULL)
            break;

        *rightslash = '\0';
        strcpy(emivars[tmpint], leftslash + 1);
        leftslash = rightslash;
    }

    /* BODY */
    sprintf(isotext, "A//%s:%s", emivars[4], emivars[18]);
    sprintf(isotext, "A//%s:", emivars[5]);
    is_backup = 0;

    /* HEADER */

    debug("bb.sms.emi", 0, "acknowledge: type = '%s'", emivars[3]);

    sprintf(emitext, "%s/%05i/%s/%s", emivars[0], (int) strlen(isotext) + 17,
            "R", emivars[3]);

    smsc->emi_current_msg_number = atoi(emivars[0]) + 1;

    /* FOOTER */
    sprintf(timestamp, "%s/%s/", emitext, isotext);
    generate_checksum((unsigned char *)timestamp, (unsigned char *)receiver);

    sprintf(sender, "%c%s/%s/%s%c", 0x02, emitext, isotext, receiver, 0x03);
    put_data(smsc, sender, strlen(sender), is_backup);

    return msgnbr;

}


/******************************************************************************
* Parse the Msg structure to the raw message format
*/
static int parse_msg_to_rawmessage(SMSCenter *smsc, Msg *msg, char *rawmessage, int rawmessage_length)
{
    char message_whole[10*1024];
    char message_body[10*1024];
    char message_header[1024];
    char message_footer[1024];

    char my_buffer[10*1024];
    char my_buffer2[10*1024];
    char msgtext[1024];
    int length;
    char mt;
    char mcl[20];
    char snumbits[20];
    char xser[1024];
    int udh_len;

    memset(&message_whole, 0, sizeof(message_whole));
    memset(&message_body, 0, sizeof(message_body));
    memset(&message_header, 0, sizeof(message_header));
    memset(&message_footer, 0, sizeof(message_footer));
    memset(&my_buffer, 0, sizeof(my_buffer));
    memset(&my_buffer2, 0, sizeof(my_buffer2));
    mt = '3';
    memset(&snumbits, 0, sizeof(snumbits));
    memset(&xser, 0, sizeof(xser));

    /* XXX parse_iso88591_to_emi shouldn't use NUL terminated
     * strings, but Octstr directly, or a char* and a length.
     */
    if (octstr_len(msg->sms.udhdata)) {
        char xserbuf[258];
        /* we need a properly formated UDH here, there first byte contains his length
         * this will be formatted in the xser field of the EMI Protocol
         */
        udh_len = octstr_get_char(msg->sms.udhdata, 0) + 1;
        xserbuf[0] = 1;
        xserbuf[1] = udh_len;
        octstr_get_many_chars(&xserbuf[2], msg->sms.udhdata, 0, udh_len);
        parse_binary_to_emi(xserbuf, xser, udh_len + 2);
    } else {
        udh_len = 0;
    }

    if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) {
        octstr_get_many_chars(msgtext, msg->sms.msgdata, 0, octstr_len(msg->sms.msgdata));
        msgtext[octstr_len(msg->sms.msgdata)] = '\0';
        parse_iso88591_to_emi(msgtext, my_buffer2,
                                           octstr_len(msg->sms.msgdata),
                                           smsc->alt_charset);

        strcpy(snumbits, "");
        mt = '3';
        strcpy(mcl, "");
    } else {
        octstr_get_many_chars(msgtext, msg->sms.msgdata, 0, octstr_len(msg->sms.msgdata));

        parse_binary_to_emi(msgtext, my_buffer2, octstr_len(msg->sms.msgdata));

        sprintf(snumbits, "%04ld", octstr_len(msg->sms.msgdata)*8);
        mt = '4';
        strcpy(mcl, "1");
    }

    /* XXX Where is DCS ? Is it in XSER like in emi2 ? 
     * Please someone encode it with fields_to_dcs 
     */

    sprintf(message_body,
            "%s/%s/%s/%s/%s//%s////////////%c/%s/%s////%s//////%s//",
            octstr_get_cstr(msg->sms.receiver),
            msg->sms.sender ? octstr_get_cstr(msg->sms.sender) : "",
            "",
            "",
            "",
            "0100",
            mt,
            snumbits,
            my_buffer2,
            mcl,
            xser);

    /* HEADER */

    length = strlen(message_body);
    length += 13;  /* header (fixed) */
    length += 2;  /* footer (fixed) */
    length += 2;  /* slashes between header, body, footer */

    sprintf(message_header, "%02i/%05i/%s/%s", (smsc->emi_current_msg_number++ % 100), length, "O", "51");

    /* FOOTER */

    sprintf(my_buffer, "%s/%s/", message_header, message_body);
    generate_checksum((unsigned char *)my_buffer, (unsigned char *)message_footer);

    sprintf(message_whole, "%c%s/%s/%s%c", 0x02, message_header, message_body, message_footer, 0x03);

    strncpy(rawmessage, message_whole, rawmessage_length);

    if (smsc->type == SMSC_TYPE_EMI_X25) {
        /* IC3S braindead EMI stack chokes on this... must fix it at the next time... */
        strcat(rawmessage, "\r");
    }
    debug("bb.sms.emi", 0, "emi %d message %s",
          smsc->emi_current_msg_number, rawmessage);
    return strlen(rawmessage);
}

/******************************************************************************
* Parse the data from the two byte EMI code to normal ISO-8869-1
*/
static int parse_emi_to_iso88591(char *from, char *to, int length,
                                 int alt_charset)
{
    int hmtg = 0;
    unsigned int mychar;
    char tmpbuff[128];

    for (hmtg = 0; hmtg <= (int)strlen(from); hmtg += 2) {
        strncpy(tmpbuff, from + hmtg, 2);
        sscanf(tmpbuff, "%x", &mychar);
        to[hmtg / 2] = char_sms_to_iso(mychar, alt_charset);
    }

    to[(hmtg / 2)-1] = '\0';

    return 0;

}

/******************************************************************************
* Parse the data from normal ISO-8869-1 to the two byte EMI code
*/
static int parse_iso88591_to_emi(char *from, char *to,
        int length, int alt_charset)
{
    char buf[10];
    unsigned char tmpchar;
    char *ptr;

    if (!from || !to || length <= 0)
        return -1;

    *to = '\0';

    debug("bb.sms.emi", 0, "emi parsing <%s> to emi, length %d", from, length);

    for (ptr = from; length > 0; ptr++, length--) {
        tmpchar = char_iso_to_sms(*ptr, alt_charset);
        sprintf(buf, "%02X", tmpchar);
        strncat(to, buf, 2);
    }
    return 0;
}

/******************************************************************************
* Parse the data from binary to the two byte EMI code
*/
static int parse_binary_to_emi(char *from, char *to, int length)
{
    char buf[10];
    char *ptr;

    if (!from || !to || length <= 0)
        return -1;

    *to = '\0';

    for (ptr = from; length > 0; ptr++, length--) {
        sprintf(buf, "%02X", (unsigned char)*ptr);
        strncat(to, buf, 2);
    }

    return 0;
}


/******************************************************************************
* Generate the EMI message checksum
*/
static void generate_checksum(const unsigned char *buf, unsigned char *out)
{
    const unsigned char *p;
    int	j;

    j = 0;
    for (p = buf; *p != '\0'; p++) {
        j += *p;

        if (j >= 256)
            j -= 256;
    }

    sprintf((char *)out, "%02X", j);
}



/******************************************************************************
* Translate character from iso to emi_mt
* PGrönholm
*/
static char char_iso_to_sms(unsigned char from, int alt_charset)
{

    switch ((char)from) {

    case 'A':
        return 0x41;
    case 'B':
        return 0x42;
    case 'C':
        return 0x43;
    case 'D':
        return 0x44;
    case 'E':
        return 0x45;
    case 'F':
        return 0x46;
    case 'G':
        return 0x47;
    case 'H':
        return 0x48;
    case 'I':
        return 0x49;
    case 'J':
        return 0x4A;
    case 'K':
        return 0x4B;
    case 'L':
        return 0x4C;
    case 'M':
        return 0x4D;
    case 'N':
        return 0x4E;
    case 'O':
        return 0x4F;
    case 'P':
        return 0x50;
    case 'Q':
        return 0x51;
    case 'R':
        return 0x52;
    case 'S':
        return 0x53;
    case 'T':
        return 0x54;
    case 'U':
        return 0x55;
    case 'V':
        return 0x56;
    case 'W':
        return 0x57;
    case 'X':
        return 0x58;
    case 'Y':
        return 0x59;
    case 'Z':
        return 0x5A;

    case 'a':
        return 0x61;
    case 'b':
        return 0x62;
    case 'c':
        return 0x63;
    case 'd':
        return 0x64;
    case 'e':
        return 0x65;
    case 'f':
        return 0x66;
    case 'g':
        return 0x67;
    case 'h':
        return 0x68;
    case 'i':
        return 0x69;
    case 'j':
        return 0x6A;
    case 'k':
        return 0x6B;
    case 'l':
        return 0x6C;
    case 'm':
        return 0x6D;
    case 'n':
        return 0x6E;
    case 'o':
        return 0x6F;
    case 'p':
        return 0x70;
    case 'q':
        return 0x71;
    case 'r':
        return 0x72;
    case 's':
        return 0x73;
    case 't':
        return 0x74;
    case 'u':
        return 0x75;
    case 'v':
        return 0x76;
    case 'w':
        return 0x77;
    case 'x':
        return 0x78;
    case 'y':
        return 0x79;
    case 'z':
        return 0x7A;

    case '0':
        return 0x30;
    case '1':
        return 0x31;
    case '2':
        return 0x32;
    case '3':
        return 0x33;
    case '4':
        return 0x34;
    case '5':
        return 0x35;
    case '6':
        return 0x36;
    case '7':
        return 0x37;
    case '8':
        return 0x38;
    case '9':
        return 0x39;
    case ':':
        return 0x3A;
    case ';':
        return 0x3B;
    case '<':
        return 0x3C;
    case '=':
        return 0x3D;
    case '>':
        return 0x3E;
    case '?':
        return 0x3F;

    case 'Ä':
        return '[';
    case 'Ö':
        return '\\';
    case 'Å':
        return 0x0E;
    case 'Ü':
        return ']';
    case 'ä':
        return '{';
    case 'ö':
        return '|';
    case 'å':
        return 0x0F;
    case 'ü':
        return '}';
    case 'ß':
        return '~';
    case '§':
        return '^';
    case 'Ñ':
        return 0x5F;
    case 'ø':
        return 0x0C;

        /*		case 'Delta': return 0x10;	*/
        /*		case 'Fii': return 0x12;	*/
        /*		case 'Lambda': return 0x13;	*/
        /*		case 'Alpha': return 0x14;	*/
        /*		case 'Omega': return 0x15;	*/
        /*		case 'Pii': return 0x16;	*/
        /*		case 'Pii': return 0x17;	*/
        /*		case 'Delta': return 0x18;	*/
        /*		case 'Delta': return 0x19;	*/
        /*		case 'Delta': return 0x1A;	*/

    case ' ':
        return 0x20;
    case '@':
        if (alt_charset == EMI_SWAPPED_CHARS)
            return 0x00;
        else
            return 0x40;
    case '£':
        return 0x01;
    case '$':
        return 0x24;
    case '¥':
        return 0x03;
    case 'è':
        return 0x04;
    case 'é':
        return 0x05;
    case 'ù':
        return 0x06;
    case 'ì':
        return 0x07;
    case 'ò':
        return 0x08;
    case 'Ç':
        return 0x09;
    case '\r':
        return 0x0A;
    case 'Ø':
        return 0x0B;
    case '\n':
        return 0x0D;
    case 'Æ':
        return 0x1C;
    case 'æ':
        return 0x1D;
    case 'É':
        return 0x1F;

    case '!':
        return 0x21;
    case '"':
        return 0x22;
    case '#':
        return 0x23;
    case '¤':
        return 0x02;
    case '%':
        return 0x25;

    case '&':
        return 0x26;
    case '\'':
        return 0x27;
    case '(':
        return 0x28;
    case ')':
        return 0x29;
    case '*':
        return 0x2A;

    case '+':
        return 0x2B;
    case ',':
        return 0x2C;
    case '-':
        return 0x2D;
    case '.':
        return 0x2E;
    case '/':
        return 0x2F;

    case '¿':
        return 0x60;
    case 'ñ':
        return 0x1E;
    case 'à':
        return 0x7F;
    case '¡':
        if (alt_charset == EMI_SWAPPED_CHARS)
            return 0x40;
        else
            return 0x00;
    case '_':
        return 0x11;

    default:
        return 0x20;  /* space */

    } /* switch */
}


/******************************************************************************
* Translate character from emi_mo to iso
* PGrönholm
*/
static char char_sms_to_iso(unsigned char from, int alt_charset)
{

    switch ((int)from) {

    case 0x41:
        return 'A';
    case 0x42:
        return 'B';
    case 0x43:
        return 'C';
    case 0x44:
        return 'D';
    case 0x45:
        return 'E';
    case 0x46:
        return 'F';
    case 0x47:
        return 'G';
    case 0x48:
        return 'H';
    case 0x49:
        return 'I';
    case 0x4A:
        return 'J';
    case 0x4B:
        return 'K';
    case 0x4C:
        return 'L';
    case 0x4D:
        return 'M';
    case 0x4E:
        return 'N';
    case 0x4F:
        return 'O';
    case 0x50:
        return 'P';
    case 0x51:
        return 'Q';
    case 0x52:
        return 'R';
    case 0x53:
        return 'S';
    case 0x54:
        return 'T';
    case 0x55:
        return 'U';
    case 0x56:
        return 'V';
    case 0x57:
        return 'W';
    case 0x58:
        return 'X';
    case 0x59:
        return 'Y';
    case 0x5A:
        return 'Z';

    case 0x61:
        return 'a';
    case 0x62:
        return 'b';
    case 0x63:
        return 'c';
    case 0x64:
        return 'd';
    case 0x65:
        return 'e';
    case 0x66:
        return 'f';
    case 0x67:
        return 'g';
    case 0x68:
        return 'h';
    case 0x69:
        return 'i';
    case 0x6A:
        return 'j';
    case 0x6B:
        return 'k';
    case 0x6C:
        return 'l';
    case 0x6D:
        return 'm';
    case 0x6E:
        return 'n';
    case 0x6F:
        return 'o';
    case 0x70:
        return 'p';
    case 0x71:
        return 'q';
    case 0x72:
        return 'r';
    case 0x73:
        return 's';
    case 0x74:
        return 't';
    case 0x75:
        return 'u';
    case 0x76:
        return 'v';
    case 0x77:
        return 'w';
    case 0x78:
        return 'x';
    case 0x79:
        return 'y';
    case 0x7A:
        return 'z';

    case 0x30:
        return '0';
    case 0x31:
        return '1';
    case 0x32:
        return '2';
    case 0x33:
        return '3';
    case 0x34:
        return '4';
    case 0x35:
        return '5';
    case 0x36:
        return '6';
    case 0x37:
        return '7';
    case 0x38:
        return '8';
    case 0x39:
        return '9';
    case 0x3A:
        return ':';
    case 0x3B:
        return ';';
    case 0x3C:
        return '<';
    case 0x3D:
        return '=';
    case 0x3E:
        return '>';
    case 0x3F:
        return '?';

    case '[':
        return 'Ä';
    case '\\':
        return 'Ö';
    case '\xC5':
        return 'Å';
    case ']':
        return 'Ü';
    case '{':
        return 'ä';
    case '|':
        return 'ö';
    case 0xE5:
        return 'å';
    case '}':
        return 'ü';
    case '~':
        return 'ß';
    case 0xA7:
        return '§';
    case 0xD1:
        return 'Ñ';
    case 0xF8:
        return 'ø';

        /*		case 'Delta':	return 0x10;	*/
        /*		case 'Fii':		return 0x12;	*/
        /*		case 'Lambda':	return 0x13;	*/
        /*		case 'Alpha':	return 0x14;	*/
        /*		case 'Omega':	return 0x15;	*/
        /*		case 'Pii':		return 0x16;	*/
        /*		case 'Pii':		return 0x17;	*/
        /*		case 'Delta':	return 0x18;	*/
        /*		case 'Delta':	return 0x19;	*/
        /*		case 'Delta':	return 0x1A;	*/

    case 0x20:
        return ' ';
    case 0x40:
        return '@';
    case 0xA3:
        return '£';
    case 0x24:
        return '$';
    case 0xA5:
        return '¥';
    case 0xE8:
        return 'è';
    case 0xE9:
        return 'é';
    case 0xF9:
        return 'ù';
    case 0xEC:
        return 'ì';
    case 0xF2:
        return 'ò';
    case 0xC7:
        return 'Ç';
    case 0x0A:
        return '\r';
    case 0xD8:
        return 'Ø';
    case 0x0D:
        return '\n';
    case 0xC6:
        return 'Æ';
    case 0xE6:
        return 'æ';
    case 0x1F:
        return 'É';

    case 0x21:
        return '!';
    case 0x22:
        return '"';
    case 0x23:
        return '#';
    case 0xA4:
        return '¤';
    case 0x25:
        return '%';

    case 0x26:
        return '&';
    case 0x27:
        return '\'';
    case 0x28:
        return '(';
    case 0x29:
        return ')';
    case 0x2A:
        return '*';

    case 0x2B:
        return '+';
    case 0x2C:
        return ',';
    case 0x2D:
        return '-';
    case 0x2E:
        return '.';
    case 0x2F:
        return '/';

    case 0xBF:
        return '¿';
    case 0xF1:
        return 'ñ';
    case 0xE0:
        return 'à';
    case 0xA1:
        return '¡';
    case 0x5F:
        return '_';

    default:
        return ' ';

    } /* switch */
}
