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
 * smsc_at.c
 * 
 * New driver for serial connected AT based
 * devices.
 * 4.9.2001
 * Andreas Fink <andreas@fink.org>
 
 * 23.6.2008, Andreas Fink,
 *   added support for telnet connections
 *   (for example Multi-Tech MTCBA-G-EN-F4)
 *
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <time.h>
#include <math.h>

#include "gwlib/gwlib.h"
#include "gwlib/charset.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "msg.h"
#include "sms.h"
#include "dlr.h"
#include "smsc_at.h"
#include "load.h"

static Octstr 			*gsm2number(Octstr *pdu);
static unsigned char	nibble2hex(unsigned char b);

static void  at2_scan_for_telnet_escapes(PrivAT2data *privdata)
{
    int len;
    int pos;
    int start;
    int a;
    int b;
    Octstr *hex;
   
    char answer[5];

    
    if(!privdata->ilb)
        return;
    start = 0;
    len = octstr_len(privdata->ilb);
    hex = octstr_duplicate(privdata->ilb);
    octstr_binary_to_hex(hex,1);
    
    octstr_destroy(hex);

    while(start < len)
    {
        pos = octstr_search_char(privdata->ilb, 0xFF, start);
        if(pos < 0)
            return;
        if((len - pos )<3)
            return;
        a = octstr_get_char(privdata->ilb,pos+1);
        b = octstr_get_char(privdata->ilb,pos+2);
        switch(a)
        {
        case 0xFD:    /* do! */
            answer[0] = 0xFF; /* escape */
            answer[1] = 0xFC; /* wont do any option*/
            answer[2] = b;
            write(privdata->fd,&answer,3);
            octstr_delete(privdata->ilb,pos,3);
            len -=3;
            break;
           break;
         case 0xFA:   /* do you support option b ? */
            octstr_delete(privdata->ilb,pos,3);
            len -=3;
            break;
            break;
        case 0xFB:    /* will */
            octstr_delete(privdata->ilb,pos,3);
            len -=3;
            break;
        case 0xFC:    /* wont */
            octstr_delete(privdata->ilb,pos,3);
            len -=3;
            break;
        }
        start = pos;
    }
    
}

static int at2_open_device1(PrivAT2data *privdata)
{
    info(0, "AT2[%s]: opening device", octstr_get_cstr(privdata->name));
    if (privdata->fd > 0) {
        warning(0, "AT2[%s]: trying to open device with not closed device!!! Please report!!!",
                 octstr_get_cstr(privdata->name));
        at2_close_device(privdata);
    }
    if (privdata->is_serial) {
        privdata->fd = open(octstr_get_cstr(privdata->device),
                            O_RDWR | O_NONBLOCK | O_NOCTTY);
        privdata->use_telnet = 0;
    } else {
        if (octstr_str_compare(privdata->device, "rawtcp") == 0) {
            privdata->use_telnet = 0;
            privdata->fd = tcpip_connect_to_server(octstr_get_cstr(privdata->rawtcp_host),
                                                   privdata->rawtcp_port, NULL); 
        }
        else if (octstr_str_compare(privdata->device, "telnet") == 0) {
            privdata->use_telnet = 1;
            privdata->fd = tcpip_connect_to_server(octstr_get_cstr(privdata->rawtcp_host),
                                                   privdata->rawtcp_port, NULL); 

        } else {
            gw_assert(0);
        }
    }
    if (privdata->fd == -1) {
        error(errno, "AT2[%s]: open failed! ERRNO=%d", octstr_get_cstr(privdata->name), errno);
        privdata->fd = -1;
        return -1;
    }
    debug("bb.smsc.at2", 0, "AT2[%s]: device opened. Telnet mode = %d", octstr_get_cstr(privdata->name),privdata->use_telnet);

    return 0;
}


static int at2_login_device(PrivAT2data *privdata)
{
    info(0, "AT2[%s]: Logging in", octstr_get_cstr(privdata->name));

    at2_read_buffer(privdata, 0);
    gwthread_sleep(0.5);
    at2_read_buffer(privdata, 0);

    if((octstr_len(privdata->username) == 0 ) && (octstr_len(privdata->password)> 0)) {
        at2_wait_modem_command(privdata, 10, 3, NULL);	/* wait for Password: prompt */
        at2_send_modem_command(privdata, octstr_get_cstr(privdata->password), 2,0); /* wait for OK: */
        at2_send_modem_command(privdata, "AT", 2,0); /* wait for OK: */
    }
    else if((octstr_len(privdata->username) > 0 ) && (octstr_len(privdata->password)> 0)) {
        at2_wait_modem_command(privdata, 10, 2, NULL);	/* wait for Login: prompt */
        at2_send_modem_command(privdata, octstr_get_cstr(privdata->username), 10,3); /* wait fo Password: */
        at2_send_modem_command(privdata, octstr_get_cstr(privdata->password), 2,0); /* wait for OK: */
        at2_send_modem_command(privdata, "AT", 2,0); /* wait for OK: */
    }

    return 0;
}


static int at2_open_device(PrivAT2data *privdata)
{
    struct termios tios;
    int ret;

    if ((ret = at2_open_device1(privdata)) != 0)
        return ret;

    if (!privdata->is_serial)
        return 0;
        
    tcgetattr(privdata->fd, &tios);

    kannel_cfmakeraw(&tios);
                      
    tios.c_iflag |= IGNBRK; /* ignore break & parity errors */
    tios.c_iflag &= ~INPCK; /* INPCK: disable parity check */
    tios.c_cflag |= HUPCL; /* hangup on close */
    tios.c_cflag |= CREAD; /* enable receiver */
    tios.c_cflag |= CLOCAL; /* Ignore modem control lines */
    tios.c_cflag &= ~CSIZE; /* set to 8 bit */
    tios.c_cflag |= CS8;
    tios.c_oflag &= ~ONLCR; /* no NL to CR-NL mapping outgoing */
    tios.c_iflag |= IGNPAR; /* ignore parity */
    tios.c_iflag &= ~INPCK;
#if defined(CRTSCTS)
    if(privdata->modem) {
        if(privdata->modem->hardware_flow_control) {
            tios.c_cflag |= CRTSCTS; /* enable hardware flow control */
        }
        else {
            tios.c_cflag &= ~CRTSCTS; /* disable hardware flow control */
        }
    }
    else {
        tios.c_cflag &= ~CRTSCTS; /* disable hardware flow control */
    }
#endif
    tios.c_cc[VSUSP] = 0; /* otherwhise we can not send CTRL Z */

    /*
    if ( ModemTypes[privdata->modemid].enable_parity )
    	tios.c_cflag ^= PARODD;
    */

    ret = tcsetattr(privdata->fd, TCSANOW, &tios); /* apply changes now */
    if (ret == -1) {
        error(errno, "AT2[%s]: at_data_link: fail to set termios attribute",
              octstr_get_cstr(privdata->name));
    }
    tcflush(privdata->fd, TCIOFLUSH);
         
    /* 
     * Nokia 7110 and 6210 need some time between opening
     * the connection and sending the first AT commands 
     */
    if (privdata->modem == NULL || privdata->modem->need_sleep)
        sleep(1);
    debug("bb.smsc.at2", 0, "AT2[%s]: device opened", octstr_get_cstr(privdata->name));
    return 0;
}


static void at2_close_device(PrivAT2data *privdata)
{
    info(0, "AT2[%s]: Closing device", octstr_get_cstr(privdata->name));
    if (privdata->fd != -1)
        close(privdata->fd);
    privdata->fd = -1;
    privdata->pin_ready = 0;
    privdata->phase2plus = 0;
    if (privdata->ilb != NULL)
        octstr_destroy(privdata->ilb);
    privdata->ilb = octstr_create("");
}


static void at2_read_buffer(PrivAT2data *privdata, double timeout)
{
    char buf[MAX_READ + 1];
    int ret;
    size_t count;
    signed int s;
    fd_set read_fd;
    struct timeval tv;

    if (privdata->fd == -1) {
        error(errno, "AT2[%s]: at2_read_buffer: fd = -1. Can not read", 
              octstr_get_cstr(privdata->name));
        return;
    }
    count = MAX_READ;

#ifdef SSIZE_MAX
    if (count > SSIZE_MAX)
        count = SSIZE_MAX;
#endif

    if (timeout <= 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
    } else {
      int usecs = timeout * 1000000;
      tv.tv_sec = usecs / 1000000;
      tv.tv_usec = usecs % 1000000;
    }

    FD_ZERO(&read_fd);
    FD_SET(privdata->fd, &read_fd);
    ret = select(privdata->fd + 1, &read_fd, NULL, NULL, &tv);
    if (ret == -1) {
        if (!(errno == EINTR || errno == EAGAIN))
            error(errno, "AT2[%s]: error on select", octstr_get_cstr(privdata->name));
        return;
    }

    if (ret == 0)
        return;

    s = read(privdata->fd, buf, count);
    if (s < 0) {
        error(errno, "AT2[%s]: at2_read_buffer: Error during read", 
              octstr_get_cstr(privdata->name));
        at2_close_device(privdata);
    } else {
        octstr_append_data(privdata->ilb, buf, s);
        if(privdata->use_telnet)
            at2_scan_for_telnet_escapes(privdata);
    }
}


static Octstr *at2_wait_line(PrivAT2data *privdata, time_t timeout, int gt_flag)
{
    Octstr *line;
    time_t end_time;
    time_t cur_time;

    if (timeout == 0)
        timeout = 3;
    end_time = time(NULL) + timeout;

    if (privdata->lines != NULL)
        octstr_destroy(privdata->lines);
    privdata->lines = octstr_create("");
    while (time(&cur_time) <= end_time) {
        line = at2_read_line(privdata, gt_flag, timeout);
        if (line)
            return line;
    }
    return NULL;
}


static Octstr *at2_extract_line(PrivAT2data *privdata, int gt_flag)
{
    int	eol;
    int gtloc;
    int len;
    Octstr *line;
    Octstr *buf2;
    int i;

    len = octstr_len(privdata->ilb);
    if (len == 0)
        return NULL;

    if (gt_flag==1) {
        /* looking for > if needed */
        gtloc = octstr_search_char(privdata->ilb, '>', 0); 
    } else if((gt_flag == 2) && (privdata->username)) { /* looking for "Login" */
        gtloc = -1;
        if(privdata->login_prompt) {
            gtloc = octstr_search(privdata->ilb,privdata->login_prompt,0);
        }
        if(gtloc == -1) {
            gtloc = octstr_search(privdata->ilb,octstr_imm("Login:"),0);
        }
        if(gtloc == -1) {
            gtloc = octstr_search(privdata->ilb,octstr_imm("Username:"),0);
        }
    } else if ((gt_flag == 3) && (privdata->password)) {/* looking for Password */
    	gtloc = -1;
        if(privdata->password_prompt) {
            gtloc = octstr_search(privdata->ilb,privdata->password_prompt,0);
        }
        if(gtloc == -1) {
            gtloc = octstr_search(privdata->ilb,octstr_imm("Password:"),0);
        }
    } else
        gtloc = -1;

    /*   
    if (gt_flag && (gtloc != -1))
        debug("bb.smsc.at2", 0, "in at2_read_line with gt_flag=1, gtloc=%d, ilb=%s",
              gtloc, octstr_get_cstr(privdata->ilb));
    */

    eol = octstr_search_char(privdata->ilb, '\r', 0); /* looking for CR */

    if ((gtloc != -1) && ((eol == -1) || (eol > gtloc)))
        eol = gtloc;

    if (eol == -1)
        return NULL;

    line = octstr_copy(privdata->ilb, 0, eol);
    buf2 = octstr_copy(privdata->ilb, eol + 1, len);
    octstr_destroy(privdata->ilb);
    privdata->ilb = buf2;

    /* remove any non printable chars (including linefeed for example) */
    for (i = 0; i < octstr_len(line); i++) {
        if (octstr_get_char(line, i) < 32)
            octstr_set_char(line, i, ' ');
    }
    octstr_strip_blanks(line);

    /* empty line, skipping */
    if (octstr_len(line) == 0 && (gt_flag == 0)) {
        return line;
    }
    if ((gt_flag) && (gtloc != -1)) {
        /* got to re-add it again as the parser needs to see it */
        octstr_append_cstr(line, ">"); 
    }
    debug("bb.smsc.at2", 0, "AT2[%s]: <-- %s", octstr_get_cstr(privdata->name), 
          octstr_get_cstr(line));
    return line;
}


static Octstr *at2_read_line(PrivAT2data *privdata, int gt_flag, double timeout)
{
    Octstr *line;

    line = at2_extract_line(privdata, gt_flag);
    if (!line) {
         at2_read_buffer(privdata, timeout);
         line = at2_extract_line(privdata, gt_flag);
    }
    return line;
}


static int at2_write_line(PrivAT2data *privdata, char *line)
{
    int count;
    int s = 0;
    int write_count = 0, data_written = 0;
    Octstr *linestr = NULL;

    linestr = octstr_format("%s\r", line);

    debug("bb.smsc.at2", 0, "AT2[%s]: --> %s^M", octstr_get_cstr(privdata->name), line);

    count = octstr_len(linestr);
    while (count > data_written) {
        errno = 0;
        s = write(privdata->fd, octstr_get_cstr(linestr) + data_written, 
                  count - data_written);
        if (s < 0 && errno == EAGAIN && write_count < RETRY_WRITE) {
            gwthread_sleep(1);
            ++write_count;
        } else if (s > 0) {
            data_written += s;
            write_count = 0;
        } else
            break;
    }
    O_DESTROY(linestr);
    if (s < 0) {
        error(errno, "AT2[%s]: Couldnot write to device.", 
              octstr_get_cstr(privdata->name));
        tcflush(privdata->fd, TCOFLUSH);
        return s;
    }
    tcdrain(privdata->fd);
    gwthread_sleep((double) (privdata->modem == NULL ? 
        100 : privdata->modem->sendline_sleep) / 1000);
    return s;
}


static int at2_write_ctrlz(PrivAT2data *privdata)
{
    int s;
    char *ctrlz = "\032" ;
    int write_count = 0;
    
    debug("bb.smsc.at2", 0, "AT2[%s]: --> ^Z", octstr_get_cstr(privdata->name));
    while (1) {
        errno = 0;
        s = write(privdata->fd, ctrlz, 1);
        if (s < 0 && errno == EAGAIN && write_count < RETRY_WRITE) {
            gwthread_sleep(1);
            ++write_count;
        } else
            break;
    }
    if (s < 0) {
        error(errno, "AT2[%s]: Couldnot write to device.", 
              octstr_get_cstr(privdata->name));
        tcflush(privdata->fd, TCOFLUSH);
        return s;
    }
    tcdrain(privdata->fd);
    gwthread_sleep((double) (privdata->modem == NULL ?
        100 : privdata->modem->sendline_sleep) / 1000);
    return s;
}
      

static int at2_write(PrivAT2data *privdata, char *line)
{
    int count, data_written = 0, write_count = 0;
    int s = 0;

    debug("bb.smsc.at2", 0, "AT2[%s]: --> %s", octstr_get_cstr(privdata->name), line);

    count = strlen(line);
    while(count > data_written) {
        s = write(privdata->fd, line + data_written, count - data_written);
        if (s < 0 && errno == EAGAIN && write_count < RETRY_WRITE) {
            gwthread_sleep(1);
            ++write_count;
        } else if (s > 0) {
            data_written += s;
            write_count = 0;
        } else
            break;
    }

    if (s < 0) {
        error(errno, "AT2[%s]: Couldnot write to device.",
              octstr_get_cstr(privdata->name));
        tcflush(privdata->fd, TCOFLUSH);
        return s;
    }
    tcdrain(privdata->fd);
    gwthread_sleep((double) (privdata->modem == NULL ?
        100 : privdata->modem->sendline_sleep) / 1000);
    return s;
}


static void at2_flush_buffer(PrivAT2data *privdata)
{
    at2_read_buffer(privdata, 0);
    octstr_destroy(privdata->ilb);
    privdata->ilb = octstr_create("");
}


static int at2_init_device(PrivAT2data *privdata)
{
    int ret;
    Octstr *setpin;

    info(0, "AT2[%s]: init device", octstr_get_cstr(privdata->name));

    at2_set_speed(privdata, privdata->speed);
    /* sleep 10 ms in order to get device some time to accept speed */
    gwthread_sleep(0.1);

    /* reset the modem */
    if (at2_send_modem_command(privdata, "ATZ", 0, 0) == -1) {
        error(0, "AT2[%s]: Wrong or no answer to ATZ, ignoring",
              octstr_get_cstr(privdata->name));
        return -1;
    }

    /* check if the modem responded */
    if (at2_send_modem_command(privdata, "AT", 0, 0) == -1) {
        error(0, "AT2[%s]: Wrong or no answer to AT. Trying again",
              octstr_get_cstr(privdata->name));
   	if (at2_send_modem_command(privdata, "AT", 0, 0) == -1) {
            error(0, "AT2[%s]: Second attempt to send AT failed",
                  octstr_get_cstr(privdata->name));
            return -1;
    	}
    }

    at2_flush_buffer(privdata);

    if (at2_send_modem_command(privdata, "AT&F", 7, 0) == -1) {
        error(0, "AT2[%s]: No answer to AT&F. Trying again",
              octstr_get_cstr(privdata->name));
	if (at2_send_modem_command(privdata, "AT&F", 7, 0) == -1) {
    	    return -1;
    	}
    }

    at2_flush_buffer(privdata);

    /* check if the modem responded */
    if (at2_send_modem_command(privdata, "ATE0", 0, 0) == -1) {
        error(0, "AT2[%s]: Wrong or no answer to ATE0. Trying again",
              octstr_get_cstr(privdata->name));
      if (at2_send_modem_command(privdata, "ATE0", 0, 0) == -1) {
            error(0, "AT2[%s]: Second attempt to send ATE0 failed",
                  octstr_get_cstr(privdata->name));
            return -1;
      }
    }

    at2_flush_buffer(privdata);


    /* enable hardware handshake */
    if (octstr_len(privdata->modem->enable_hwhs)) {
        if (at2_send_modem_command(privdata, 
            octstr_get_cstr(privdata->modem->enable_hwhs), 0, 0) == -1)
            info(0, "AT2[%s]: cannot enable hardware handshake", 
                 octstr_get_cstr(privdata->name));
    }

    /*
     * Check does the modem require a PIN and, if so, send it.
     * This is not supported by the Nokia Premicell 
     */
    if (!privdata->modem->no_pin) {
        ret = at2_send_modem_command(privdata, "AT+CPIN?", 10, 0);

        if (!privdata->pin_ready) {
            if (ret == 2) {
                if (privdata->pin == NULL)
                    return -1;
                setpin = octstr_format("AT+CPIN=\"%s\"", octstr_get_cstr(privdata->pin));
                ret = at2_send_modem_command(privdata, octstr_get_cstr(setpin), 0, 0);
                octstr_destroy(setpin);
                if (ret != 0 )
                    return -1;
            } else if (ret == -1)
                return -1;
        }

        /* 
         * we have to wait until +CPIN: READY appears before issuing
         * the next command. 10 sec should be suficient 
         */
        if (!privdata->pin_ready) {
            at2_wait_modem_command(privdata, 10, 0, NULL);
            if (!privdata->pin_ready) {
                at2_send_modem_command(privdata, "AT+CPIN?", 10, 0);
                if (!privdata->pin_ready) {
                    return -1; /* give up */
                }
            }
        }
    }
    /* 
     * Set the GSM SMS message center address if supplied 
     */
    if (octstr_len(privdata->sms_center)) {
        Octstr *temp;
        temp = octstr_create("AT+CSCA=");
        octstr_append_char(temp, 34);
        octstr_append(temp, privdata->sms_center);
        octstr_append_char(temp, 34);
        /* 
         * XXX If some modem don't process the +, remove it and add ",145"
         * and ",129" to national numbers
         */
        ret = at2_send_modem_command(privdata, octstr_get_cstr(temp), 0, 0);
        octstr_destroy(temp);
        if (ret == -1)
            return -1;
        if (ret > 0) {
            info(0, "AT2[%s]: Cannot set SMS message center, continuing", 
                 octstr_get_cstr(privdata->name));
        }
    }

    /* Set the modem to PDU mode and autodisplay of new messages */
    ret = at2_send_modem_command(privdata, "AT+CMGF=0", 0, 0);
    if (ret != 0 )
        return -1;

    /* lets see if it supports GSM SMS 2+ mode */
    ret = at2_send_modem_command(privdata, "AT+CSMS=?", 0, 0);
    if (ret != 0) {
        /* if it doesnt even understand the command, I'm sure it wont support it */
        privdata->phase2plus = 0; 
    } else {
        /* we have to take a part a string like +CSMS: (0,1,128) */
        Octstr *ts;
        int i;
        List *vals;

        ts = privdata->lines;
        privdata->lines = NULL;

        i = octstr_search_char(ts, '(', 0);
        if (i > 0) {
            octstr_delete(ts, 0, i + 1);
        }
        i = octstr_search_char(ts, ')', 0);
        if (i > 0) {
            octstr_truncate(ts, i);
        }
        vals = octstr_split(ts, octstr_imm(","));
        octstr_destroy(ts);
        ts = gwlist_search(vals, octstr_imm("1"), (void*) octstr_item_match);
        if (ts)
            privdata->phase2plus = 1;
        gwlist_destroy(vals, octstr_destroy_item);
    }
    if (privdata->phase2plus) {
        info(0, "AT2[%s]: Phase 2+ is supported", octstr_get_cstr(privdata->name));
        ret = at2_send_modem_command(privdata, "AT+CSMS=1", 0, 0);
        if (ret != 0)
            return -1;
    }

    /* send init string */
    ret = at2_send_modem_command(privdata, octstr_get_cstr(privdata->modem->init_string), 0, 0);
    if (ret != 0)
        return -1;

    if (privdata->sms_memory_poll_interval && privdata->modem->message_storage) {
        /* set message storage location for "SIM buffering" using the CPMS command */
        if (at2_set_message_storage(privdata, privdata->modem->message_storage) != 0)
            return -1;
    }

    info(0, "AT2[%s]: AT SMSC successfully opened.", octstr_get_cstr(privdata->name));
    return 0;
}


static int at2_send_modem_command(PrivAT2data *privdata, char *cmd, time_t timeout, int gt_flag)
{
    if (at2_write_line(privdata, cmd) == -1)
        return -1;
    return at2_wait_modem_command(privdata, timeout, gt_flag, NULL);
}


static int at2_wait_modem_command(PrivAT2data *privdata, time_t timeout, int gt_flag, int *output)
{
    Octstr *line = NULL;
    Octstr *line2 = NULL;
    Octstr *pdu = NULL;
    Octstr	*smsc_number = NULL;
    int ret;
    time_t end_time;
    time_t cur_time;
    Msg	*msg;
    int cmgr_flag = 0;

    if (!timeout)
    	timeout = 3;
    end_time = time(NULL) + timeout;

    if (privdata->lines != NULL)
        octstr_destroy(privdata->lines);
    privdata->lines = octstr_create("");
    
    smsc_number = octstr_create("");
    while (privdata->fd != -1 && time(&cur_time) <= end_time) {
        O_DESTROY(line);
        if ((line = at2_read_line(privdata, gt_flag, timeout))) {
            octstr_append(privdata->lines, line);
            octstr_append_cstr(privdata->lines, "\n");

            if (octstr_search(line, octstr_imm("SIM PIN"), 0) != -1) {
                ret = 2;
                goto end;
            }
            if (octstr_search(line, octstr_imm("OK"), 0) != -1) {
                ret = 0;
                goto end;
            }
            if ((gt_flag ) && (octstr_search(line, octstr_imm(">"), 0) != -1)) {
                ret = 1;
                goto end;
            }
            if (octstr_search(line, octstr_imm("RING"), 0) != -1) {
                at2_write_line(privdata, "ATH0");
                continue;
            }
            if (octstr_search(line, octstr_imm("+CPIN: READY"), 0) != -1) {
                privdata->pin_ready = 1;
                ret = 3;
                goto end;
            }
            if (octstr_search(line, octstr_imm("+CMS ERROR"), 0) != -1) {
                int errcode;
                error(0, "AT2[%s]: +CMS ERROR: %s", octstr_get_cstr(privdata->name), 
                      octstr_get_cstr(line));
                if (sscanf(octstr_get_cstr(line), "+CMS ERROR: %d", &errcode) == 1)
                    error(0, "AT2[%s]: +CMS ERROR: %s (%d)", octstr_get_cstr(privdata->name), 
                          at2_error_string(errcode), errcode);
                ret = 1;
                goto end;
            } else if (octstr_search(line, octstr_imm("+CME ERROR"), 0) != -1) {
                int errcode;
                error(0, "AT2[%s]: +CME ERROR: %s", octstr_get_cstr(privdata->name),
                      octstr_get_cstr(line));
                if (sscanf(octstr_get_cstr(line), "+CME ERROR: %d", &errcode) == 1)
                    error(0, "AT2[%s]: +CME ERROR: %s (%d)", octstr_get_cstr(privdata->name),
                          at2_error_string(errcode), errcode);
                ret = 1;
                goto end;
            }
            if (octstr_search(line, octstr_imm("+CMTI:"), 0) != -1 || 
                octstr_search(line, octstr_imm("+CDSI:"), 0) != -1) {
                /* 
                 * we received an incoming message indication
                 * put it in the pending_incoming_messages queue for later retrieval 
                 */
                debug("bb.smsc.at2", 0, "AT2[%s]: +CMTI incoming SMS indication: %s", 
                      octstr_get_cstr(privdata->name), octstr_get_cstr(line));
                gwlist_append(privdata->pending_incoming_messages, line);
                line = NULL;
                continue;
            }
            if (octstr_search(line, octstr_imm("+CMT:"), 0) != -1 ||
                octstr_search(line, octstr_imm("+CDS:"), 0) != -1 ||
                ((octstr_search(line, octstr_imm("+CMGR:"), 0) != -1) && (cmgr_flag = 1)) ) {
                line2 = at2_wait_line(privdata, 1, 0);

                if (line2 == NULL) {
                    error(0, "AT2[%s]: got +CMT but waiting for next line timed out", 
                          octstr_get_cstr(privdata->name));
                } else {
                    octstr_append_cstr(line, "\n");
                    octstr_append(line, line2);
                    O_DESTROY(line2);
                    at2_pdu_extract(privdata, &pdu, line, smsc_number);
                    if (pdu == NULL) {
                        error(0, "AT2[%s]: got +CMT but pdu_extract failed", 
                              octstr_get_cstr(privdata->name));
                    } else {
                        /* count message even if I can't decode it */
                        if (output)
                            ++(*output);
                        msg = at2_pdu_decode(pdu, privdata);
                        if (msg != NULL) {
                            octstr_destroy(msg->sms.smsc_id);
                            octstr_destroy(msg->sms.smsc_number);
                            msg->sms.smsc_id = octstr_duplicate(privdata->conn->id);
                            msg->sms.smsc_number = octstr_duplicate(smsc_number);
                            bb_smscconn_receive(privdata->conn, msg);
                        } else {
                            error(0, "AT2[%s]: could not decode PDU to a message.",
                                  octstr_get_cstr(privdata->name));
                        }

                        if (!cmgr_flag) {
                            if (privdata->phase2plus) {
                                at2_send_modem_command(privdata, "AT+CNMA", 3, 0);
                            }
                        }

                        O_DESTROY(pdu);
                    }
                }
                continue;
            }
            if ((octstr_search(line, octstr_imm("+CMGS:"),0) != -1) && (output)) {
                /* 
                 * found response to a +CMGS command, read the message id 
                 * and return it in output 
                 */
                long temp;
                if (octstr_parse_long(&temp, line, octstr_search(line, octstr_imm("+CMGS:"), 0) + 6, 10) == -1)
                    error(0, "AT2[%s]: Got +CMGS but failed to read message id", 
                          octstr_get_cstr(privdata->name));
                else
                    *output = temp;
            }
            /* finally check if we received a generic error */
            if (octstr_search(line, octstr_imm("ERROR"), 0) != -1) {
                int errcode;
                error(0, "AT2[%s]: Generic error: %s", octstr_get_cstr(privdata->name),
                      octstr_get_cstr(line));
                if (sscanf(octstr_get_cstr(line), "ERROR: %d", &errcode) == 1)
                    error(0, "AT2[%s]: Generic error: %s (%d)", octstr_get_cstr(privdata->name), 
                          at2_error_string(errcode), errcode);
                ret = -1;
                goto end;
            }
        }
    }

    /*
    error(0,"AT2[%s]: timeout. received <%s> until now, buffer size is %d, buf=%s",
          octstr_get_cstr(privdata->name),
          privdata->lines ? octstr_get_cstr(privdata->lines) : "<nothing>", len,
          privdata->ilb ? octstr_get_cstr(privdata->ilb) : "<nothing>");
    */
    O_DESTROY(line);
    O_DESTROY(line2);
    O_DESTROY(pdu);
    O_DESTROY(smsc_number);
    return -1; /* timeout */

end:
	O_DESTROY(smsc_number);
    octstr_append(privdata->lines, line);
    octstr_append_cstr(privdata->lines, "\n");
    O_DESTROY(line);
    O_DESTROY(line2);
    O_DESTROY(pdu);
    return ret;
}


static int at2_read_delete_message(PrivAT2data* privdata, int message_number)
{
    char cmd[20];
    int message_count = 0;

    sprintf(cmd, "AT+CMGR=%d", message_number);
    /* read one message from memory */
    at2_write_line(privdata, cmd);
    if (at2_wait_modem_command(privdata, 0, 0, &message_count) != 0) {
        debug("bb.smsc.at2", 0, "AT2[%s]: failed to get message %d.", 
              octstr_get_cstr(privdata->name), message_number);
        return 0; /* failed to read the message - skip to next message */
    }

    /* no need to delete if no message collected */
    if (!message_count) { 
        debug("bb.smsc.at2", 0, "AT2[%s]: not deleted.", 
              octstr_get_cstr(privdata->name));
        return 0;
    }

    sprintf(cmd, "AT+CMGD=%d", message_number); /* delete the message we just read */
    /* 
     * 3 seconds (default timeout of send_modem_command()) is not enough with some
     * modems if the message is large, so we'll give it 7 seconds
     */
    if (at2_send_modem_command(privdata, cmd, 7, 0) != 0) {  
        /* 
         * failed to delete the message, we'll just ignore it for now, 
         * this is bad, since if the message really didn't get deleted
         * we'll see it next time around. 
         */                
        error(2, "AT2[%s]: failed to delete message %d.",
              octstr_get_cstr(privdata->name), message_number);
    }

    return 1;
}


/*
 * This function loops through the pending_incoming_messages queue for CMTI
 * notifications.
 * Every notification is parsed and the messages are read (and deleted)
 * accordingly.
*/
static void at2_read_pending_incoming_messages(PrivAT2data *privdata)
{
    Octstr *current_storage = NULL;

    if (privdata->modem->message_storage) {
	    current_storage = octstr_duplicate(privdata->modem->message_storage);
    }
    while (gwlist_len(privdata->pending_incoming_messages) > 0) {
        int pos;
        long location;
        Octstr *cmti_storage = NULL, *line = NULL;
        
        line = gwlist_extract_first(privdata->pending_incoming_messages);
        /* message memory starts after the first quote in the string */
        if ((pos = octstr_search_char(line, '"', 0)) != -1) {
            /* grab memory storage name */
            int next_quote = octstr_search_char(line, '"', ++pos);
            if (next_quote == -1) { /* no second qoute - this line must be broken somehow */
                O_DESTROY(line);
                continue;
            }

            /* store notification storage location for reference */
            cmti_storage = octstr_copy(line, pos, next_quote - pos);
        } else
            /* reset pos for the next lookup which would start from the beginning if no memory
             * location was found */
            pos = 0; 

        /* if no message storage is set in configuration - set now */
        if (!privdata->modem->message_storage && cmti_storage) { 
            info(2, "AT2[%s]: CMTI received, but no message-storage is set in confiuration."
                 "setting now to <%s>", octstr_get_cstr(privdata->name), octstr_get_cstr(cmti_storage));
            privdata->modem->message_storage = octstr_duplicate(cmti_storage);
            current_storage = octstr_duplicate(cmti_storage);
            at2_set_message_storage(privdata, cmti_storage);
        }

        /* find the message id from the line, which should appear after the first comma */
        if ((pos = octstr_search_char(line, ',', pos)) == -1) { /* this CMTI notification is probably broken */
            error(2, "AT2[%s]: failed to find memory location in CMTI notification",
                  octstr_get_cstr(privdata->name));
            O_DESTROY(line);
            octstr_destroy(cmti_storage);
            continue;
        }

        if ((pos = octstr_parse_long(&location, line, ++pos, 10)) == -1) {
            /* there was an error parsing the message id. next! */
            error(2, "AT2[%s]: error parsing memory location in CMTI notification",
                  octstr_get_cstr(privdata->name));
            O_DESTROY(line);
            octstr_destroy(cmti_storage);
            continue;
        }

        /* check if we need to change storage location before issuing the read command */
        if (!current_storage || (octstr_compare(current_storage, cmti_storage) != 0)) {
            octstr_destroy(current_storage);
            current_storage = octstr_duplicate(cmti_storage);
            at2_set_message_storage(privdata, cmti_storage);
        }
        
        if (!at2_read_delete_message(privdata, location)) {
            error(1, "AT2[%s]: CMTI notification received, but no message found in memory!",
                  octstr_get_cstr(privdata->name));
        }
        
        octstr_destroy(line);
        octstr_destroy(cmti_storage);
    }
    
    /* set prefered message storage back to what configured */
    if (current_storage && privdata->modem->message_storage 
        && (octstr_compare(privdata->modem->message_storage, current_storage) != 0))
        at2_set_message_storage(privdata, privdata->modem->message_storage);

    octstr_destroy(current_storage);
}


static int at2_read_sms_memory(PrivAT2data* privdata)
{
    /* get memory status */
    if (at2_check_sms_memory(privdata) == -1) {
        debug("bb.smsc.at2", 0, "AT2[%s]: memory check error", octstr_get_cstr(privdata->name));
        return -1;
    }

    if (privdata->sms_memory_usage) {
        /*
         * that is - greater then 0, meaning there are some messages to fetch
         * now - I used to just loop over the first input_mem_sms_used locations, 
         * but it doesn't hold, since under load, messages may be received while 
         * we're in the loop, and get stored in locations towards the end of the list, 
         * thus creating 'holes' in the memory. 
         * 
         * There are two ways we can fix this : 
         *   (a) Just read the last message location, delete it and return.
         *       It's not a complete solution since holes can still be created if messages 
         *       are received between the memory check and the delete command, 
         *       and anyway - it will slow us down and won't hold well under pressure
         *   (b) Just scan the entire memory each call, bottom to top. 
         *       This will be slow too, but it'll be reliable.
         *
         * We can massivly improve performance by stopping after input_mem_sms_used messages
         * have been read, but send_modem_command returns 0 for no message as well as for a 
         * message read, and the only other way to implement it is by doing memory_check 
         * after each read and stoping when input_mem_sms_used get to 0. This is slow 
         * (modem commands take time) so we improve speed only if there are less then 10 
         * messages in memory.
         *
         * I implemented the alternative - changed at2_wait_modem_command to return the 
         * number of messages it collected.
         */
        int i;
        int message_count = 0; /* cound number of messages collected */
        ModemDef *modem = privdata->modem;

        debug("bb.smsc.at2", 0, "AT2[%s]: %d messages waiting in memory", 
              octstr_get_cstr(privdata->name), privdata->sms_memory_usage);

        /*
         * loop till end of memory or collected enouch messages
         */
        for (i = modem->message_start; i < (privdata->sms_memory_capacity  + modem->message_start) && message_count < privdata->sms_memory_usage; ++i) {

            /* if (meanwhile) there are pending CMTI notifications, process these first
             * to not let CMTI and sim buffering sit in each others way */
            while (gwlist_len(privdata->pending_incoming_messages) > 0) {
                at2_read_pending_incoming_messages(privdata);
            }
            /* read the message and delete it */
            message_count += at2_read_delete_message(privdata, i);
        }
    }
    
    /*
    at2_send_modem_command(privdata, ModemTypes[privdata->modemid].init1, 0, 0);
    */
    return 0;
}


static int at2_check_sms_memory(PrivAT2data *privdata)
{
    long values[4]; /* array to put response data in */
    int pos; /* position of parser in data stream */
    int ret;
    Octstr *search_cpms = NULL;

    /* select memory type and get report */
    if ((ret = at2_send_modem_command(privdata, "AT+CPMS?", 0, 0)) != 0) { 
        debug("bb.smsc.at2.memory_check", 0, "failed to send mem select command to modem %d", ret);
        return -1;
    }

    search_cpms = octstr_create("+CPMS:");

    if ((pos = octstr_search(privdata->lines, search_cpms, 0)) != -1) {
        /* got back a +CPMS response */
        int index = 0; /* index in values array */
        pos += 6; /* position of parser in the stream - start after header */

        /* skip memory indication */
        pos = octstr_search(privdata->lines, octstr_imm(","), pos) + 1; 

        /* find all the values */
        while (index < 4 && pos < octstr_len(privdata->lines) &&
               (pos = octstr_parse_long(&values[index], privdata->lines, pos, 10)) != -1) { 
            ++pos; /* skip number seperator */
            ++index; /* increment array index */
            if (index == 2)
                /* skip second memory indication */
                pos = octstr_search(privdata->lines, octstr_imm(","), pos) + 1; 
        }

        if (index < 4) { 
            /* didn't get all memory data - I don't why, so I'll bail */
            debug("bb.smsc.at2", 0, "AT2[%s]: couldn't parse all memory locations : %d:'%s'.",
                  octstr_get_cstr(privdata->name), index, 
                  &(octstr_get_cstr(privdata->lines)[pos]));
            O_DESTROY(search_cpms);
            return -1;
        }

        privdata->sms_memory_usage = values[0];
        privdata->sms_memory_capacity = values[1];
        /*
        privdata->output_mem_sms_used = values[2];
        privdata->output_mem_sms_capacity = values[3];
        */

        /* everything's cool */
        ret = 0; 

        /*  clear the buffer */
        O_DESTROY(privdata->lines);

    } else {
        debug("bb.smsc.at2", 0, "AT2[%s]: no correct header for CPMS response.", 
              octstr_get_cstr(privdata->name));

        /* didn't get a +CPMS response - this is clearly an error */
        ret = -1; 
    }

    O_DESTROY(search_cpms);
    return ret;
}


static void at2_set_speed(PrivAT2data *privdata, int bps)
{
    struct termios tios;
    int ret;
    int	speed;

    if (!privdata->is_serial)
        return;

    tcgetattr(privdata->fd, &tios);

    switch (bps) {
        case 300:
            speed = B300;
            break;
        case 1200:
            speed = B1200;
            break;
        case 2400:
            speed = B2400;
            break;
        case 4800:
            speed = B4800;
            break;
        case 9600:
            speed = B9600;
            break;
        case 19200:
            speed = B19200;
            break;
        case 38400:
            speed = B38400;
            break;
#ifdef B57600
        case 57600:
            speed = B57600;
            break;
#endif
#ifdef B115200
        case 115200:
            speed = B115200;
            break;
#endif
#ifdef B230400
        case 230400:
            speed = B230400;
            break;
#endif
#ifdef B460800
        case 460800:
            speed = B460800;
            break;
#endif
#ifdef B500000
        case 500000:
            speed = B500000;
            break;
#endif
#ifdef B576000
        case 576000:
            speed = B576000;
            break;
#endif
#ifdef B921600
        case 921600:
            speed = B921600;
            break;
#endif
        default:
#if B9600 == 9600
	     speed = bps;
#else
         speed = B9600;
#endif
    }
    
    cfsetospeed(&tios, speed);
    cfsetispeed(&tios, speed);
    ret = tcsetattr(privdata->fd, TCSANOW, &tios); /* apply changes now */
    if (ret == -1) {
        error(errno, "AT2[%s]: at_data_link: fail to set termios attribute",
              octstr_get_cstr(privdata->name));
    }
    tcflush(privdata->fd, TCIOFLUSH);

    info(0, "AT2[%s]: speed set to %d", octstr_get_cstr(privdata->name), bps);
}


static void at2_device_thread(void *arg)
{
    SMSCConn *conn = arg;
    PrivAT2data	*privdata = conn->data;
    int reconnecting = 0, error_count = 0;
    long idle_timeout, memory_poll_timeout = 0;

    conn->status = SMSCCONN_CONNECTING;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

reconnect:

    do {
        if (reconnecting) {
            if (conn->status == SMSCCONN_ACTIVE) {
                mutex_lock(conn->flow_mutex);
                conn->status = SMSCCONN_RECONNECTING;
                mutex_unlock(conn->flow_mutex);
            }
            error(0, "AT2[%s]: Couldn't connect (retrying in %ld seconds).",
                     octstr_get_cstr(privdata->name), conn->reconnect_delay);
            gwthread_sleep(conn->reconnect_delay);
            reconnecting = 0;
        }

        /* If modems->speed is defined, try to use it, else autodetect */
        if (privdata->speed == 0 && privdata->modem != NULL && privdata->modem->speed != 0) {

            info(0, "AT2[%s]: trying to use speed <%ld> from modem definition",
                 octstr_get_cstr(privdata->name), privdata->modem->speed);
            if (at2_test_speed(privdata, privdata->modem->speed) == 0) { 
                privdata->speed = privdata->modem->speed;
            	info(0, "AT2[%s]: speed is %ld", 
                     octstr_get_cstr(privdata->name), privdata->speed);
            } else {
                info(0, "AT2[%s]: speed in modem definition don't work, will autodetect", 
                     octstr_get_cstr(privdata->name));
            }
        }

        if (privdata->speed == 0 && at2_detect_speed(privdata) == -1) {
            reconnecting = 1;
            continue;
        }

        if (privdata->modem == NULL && at2_detect_modem_type(privdata) == -1) {
            reconnecting = 1;
            continue;
        }

        if (at2_open_device(privdata)) {
            error(errno, "AT2[%s]: at2_device_thread: open_at2_device failed.", 
                  octstr_get_cstr(privdata->name));
            reconnecting = 1;
            continue;
        }

        if (at2_login_device(privdata)) {
            error(errno, "AT2[%s]: at2_device_thread: at2_login_device failed.", 
                  octstr_get_cstr(privdata->name));
            reconnecting = 1;
            continue;
        }

        if (privdata->max_error_count > 0 && error_count > privdata->max_error_count 
            && privdata->modem != NULL && privdata->modem->reset_string != NULL) {
            error_count = 0;
            if (at2_send_modem_command(privdata,
                 octstr_get_cstr(privdata->modem->reset_string), 0, 0) != 0) {
                error(0, "AT2[%s]: Reset of modem failed.", octstr_get_cstr(privdata->name));
                at2_close_device(privdata);
                reconnecting = 1;
                continue;
            } else {
                info(0, "AT2[%s]: Modem reseted.", octstr_get_cstr(privdata->name));
            }
        }

        if (at2_init_device(privdata) != 0) {
            error(0, "AT2[%s]: Initialization of device failed. Attempt #%d on %ld max.", octstr_get_cstr(privdata->name),
                     error_count, privdata->max_error_count);
            at2_close_device(privdata);
            error_count++;
            reconnecting = 1;
            continue;
        } else
            error_count = 0;

        /* If we got here, then the device is opened */
        break;
    } while (!privdata->shutdown);

    mutex_lock(conn->flow_mutex);
    conn->status = SMSCCONN_ACTIVE;
    conn->connect_time = time(NULL);
    mutex_unlock(conn->flow_mutex);
    bb_smscconn_connected(conn);

    idle_timeout = 0;
    while (!privdata->shutdown) {
        at2_wait_modem_command(privdata, 1, 0, NULL);

        /* read error, so re-connect */
        if (privdata->fd == -1) {
            at2_close_device(privdata);
            reconnecting = 1;
            goto reconnect;
        }

        while (gwlist_len(privdata->pending_incoming_messages) > 0) {
            at2_read_pending_incoming_messages(privdata);
        }

        if (privdata->keepalive &&
            idle_timeout + privdata->keepalive < time(NULL)) {
            if (at2_send_modem_command(privdata, 
                octstr_get_cstr(privdata->modem->keepalive_cmd), 5, 0) < 0) {
                at2_close_device(privdata);
                reconnecting = 1;
                goto reconnect;
            }
            idle_timeout = time(NULL);
        }

        if (privdata->sms_memory_poll_interval &&
            memory_poll_timeout + privdata->sms_memory_poll_interval < time(NULL)) {
            if (at2_read_sms_memory(privdata) == -1) {
                at2_close_device(privdata);
                reconnecting = 1;
                goto reconnect;
            }
            memory_poll_timeout = time(NULL);
        }

        if (gw_prioqueue_len(privdata->outgoing_queue) > 0) {
            at2_send_messages(privdata);
            idle_timeout = time(NULL);
        }
    }
    at2_close_device(privdata);
    mutex_lock(conn->flow_mutex);
    conn->status = SMSCCONN_DISCONNECTED;
    mutex_unlock(conn->flow_mutex);
    /* maybe some cleanup here? */
    at2_destroy_modem(privdata->modem);
    octstr_destroy(privdata->device);
    octstr_destroy(privdata->ilb);
    octstr_destroy(privdata->lines);
    octstr_destroy(privdata->pin);
    octstr_destroy(privdata->validityperiod);
    octstr_destroy(privdata->my_number);
    octstr_destroy(privdata->sms_center);
    octstr_destroy(privdata->name);
    octstr_destroy(privdata->configfile);
    octstr_destroy(privdata->username);
    octstr_destroy(privdata->password);
    octstr_destroy(privdata->rawtcp_host);
    gw_prioqueue_destroy(privdata->outgoing_queue, NULL);
    gwlist_destroy(privdata->pending_incoming_messages, octstr_destroy_item);
    load_destroy(privdata->load);
    gw_free(conn->data);
    conn->data = NULL;
    mutex_lock(conn->flow_mutex);
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    conn->status = SMSCCONN_DEAD;
    mutex_unlock(conn->flow_mutex);
    bb_smscconn_killed();
}


static int at2_shutdown_cb(SMSCConn *conn, int finish_sending)
{
    PrivAT2data *privdata = conn->data;

    debug("bb.sms", 0, "AT2[%s]: Shutting down SMSCConn, %s",
          octstr_get_cstr(privdata->name),
          finish_sending ? "slow" : "instant");

    /* 
     * Documentation claims this would have been done by smscconn.c,
     * but isn't when this code is being written. 
     */
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    privdata->shutdown = 1; 
    /* 
     * Separate from why_killed to avoid locking, as
     * why_killed may be changed from outside? 
     */
    if (finish_sending == 0) {
        Msg *msg;
        while ((msg = gw_prioqueue_remove(privdata->outgoing_queue)) != NULL) {
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
        }
    }
    gwthread_wakeup(privdata->device_thread);
    return 0;

}


static long at2_queued_cb(SMSCConn *conn)
{
    PrivAT2data *privdata;

    privdata = conn->data;
    conn->load = (privdata ? (conn->status != SMSCCONN_DEAD ?        
                  gw_prioqueue_len(privdata->outgoing_queue) : 0) : 0);
    return conn->load;               
} 


static void at2_start_cb(SMSCConn *conn)
{
    PrivAT2data *privdata;

    privdata = conn->data;
    if (conn->status == SMSCCONN_DISCONNECTED)
        conn->status = SMSCCONN_ACTIVE;

    /* in case there are messages in the buffer already */
    gwthread_wakeup(privdata->device_thread); 
    debug("smsc.at2", 0, "AT2[%s]: start called", octstr_get_cstr(privdata->name));
}   

static int at2_add_msg_cb(SMSCConn *conn, Msg *sms)
{
    PrivAT2data *privdata;

    privdata = conn->data;
    gw_prioqueue_produce(privdata->outgoing_queue, msg_duplicate(sms));
    gwthread_wakeup(privdata->device_thread);
    return 0;
}


int smsc_at2_create(SMSCConn *conn, CfgGroup *cfg)
{
    PrivAT2data	*privdata;
    Octstr *modem_type_string;
    long portno;   /* has to be long because of cfg_get_integer */

    privdata = gw_malloc(sizeof(PrivAT2data));
    memset(privdata, 0, sizeof(PrivAT2data));
    privdata->outgoing_queue = gw_prioqueue_create(sms_priority_compare);
    privdata->pending_incoming_messages = gwlist_create();

    privdata->configfile = cfg_get_configfile(cfg);

    privdata->device = cfg_get(cfg, octstr_imm("device"));
    if (privdata->device == NULL) {
        error(0, "AT2[-]: 'device' missing in at2 configuration.");
        goto error;
    }
    
    if (octstr_str_compare(privdata->device, "rawtcp") == 0) {
        privdata->rawtcp_host = cfg_get(cfg, octstr_imm("host"));
        if (privdata->rawtcp_host == NULL) {
            error(0, "AT2[-]: 'host' missing in at2 rawtcp configuration.");
            goto error;
        }
        if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1) {
            error(0, "AT2[-]: 'port' missing in at2 rawtcp configuration.");
            goto error;
        }
        privdata->rawtcp_port = portno;
        privdata->is_serial = 0;
        privdata->use_telnet = 0;
    } else if (octstr_str_compare(privdata->device, "telnet") == 0) {
        privdata->rawtcp_host = cfg_get(cfg, octstr_imm("host"));
        if (privdata->rawtcp_host == NULL) {
            error(0, "AT2[-]: 'host' missing in at2 telnet configuration.");
            goto error;
        }
        if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1) {
            error(0, "AT2[-]: 'port' missing in at2 telnet configuration.");
            goto error;
        }
        privdata->rawtcp_port = portno;
        privdata->is_serial = 0;
        privdata->use_telnet = 1;
    } else {
        privdata->is_serial = 1;
        privdata->use_telnet = 0;
    }

    privdata->name = cfg_get(cfg, octstr_imm("smsc-id"));
    if (privdata->name == NULL) {
        privdata->name = octstr_duplicate(privdata->device);
    }

    privdata->speed = 0;
    cfg_get_integer(&privdata->speed, cfg, octstr_imm("speed"));

    privdata->keepalive = 0;
    cfg_get_integer(&privdata->keepalive, cfg, octstr_imm("keepalive"));

    cfg_get_bool(&privdata->sms_memory_poll_interval, cfg, octstr_imm("sim-buffering"));
    if (privdata->sms_memory_poll_interval) {
        if (privdata->keepalive)
            privdata->sms_memory_poll_interval = privdata->keepalive;
        else
            privdata->sms_memory_poll_interval = AT2_DEFAULT_SMS_POLL_INTERVAL;
    }

    privdata->my_number       = cfg_get(cfg, octstr_imm("my-number"));
    privdata->sms_center      = cfg_get(cfg, octstr_imm("sms-center"));
    privdata->username        = cfg_get(cfg, octstr_imm("smsc-username"));
    privdata->password        = cfg_get(cfg, octstr_imm("smsc-password"));
    privdata->login_prompt    = cfg_get(cfg, octstr_imm("login-prompt"));
    privdata->password_prompt = cfg_get(cfg, octstr_imm("password-prompt"));
    modem_type_string = cfg_get(cfg, octstr_imm("modemtype"));

    privdata->modem = NULL;

    if (modem_type_string != NULL) {
        if (octstr_compare(modem_type_string, octstr_imm("auto")) == 0 ||
            octstr_compare(modem_type_string, octstr_imm("autodetect")) == 0)
            O_DESTROY(modem_type_string);
    }

    if (octstr_len(modem_type_string) == 0) {
        info(0, "AT2[%s]: configuration doesn't show modemtype. will autodetect",
             octstr_get_cstr(privdata->name));
    } else {
        info(0, "AT2[%s]: configuration shows modemtype <%s>",
             octstr_get_cstr(privdata->name),
             octstr_get_cstr(modem_type_string));
        privdata->modem = at2_read_modems(privdata, privdata->configfile,
                                          modem_type_string, 0);
        if (privdata->modem == NULL) {
            info(0, "AT2[%s]: modemtype not found, revert to autodetect",
                 octstr_get_cstr(privdata->name));
        } else {
            info(0, "AT2[%s]: read modem definition for <%s>",
                 octstr_get_cstr(privdata->name),
                 octstr_get_cstr(privdata->modem->name));
        }
        O_DESTROY(modem_type_string);
    }

    privdata->ilb = octstr_create("");
    privdata->fd = -1;
    privdata->lines = NULL;
    privdata->pin = cfg_get(cfg, octstr_imm("pin"));
    privdata->pin_ready = 0;
    privdata->conn = conn;
    privdata->phase2plus = 0;
    privdata->validityperiod = cfg_get(cfg, octstr_imm("validityperiod"));
    if (cfg_get_integer((long *) &privdata->max_error_count,  cfg, octstr_imm("max-error-count")) == -1)
        privdata->max_error_count = -1;

    privdata->load = load_create_real(0);
    load_add_interval(privdata->load, 1);

    conn->data = privdata;
    conn->name = octstr_format("AT2[%s]", octstr_get_cstr(privdata->name));
    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = time(NULL);

    privdata->shutdown = 0;

    if ((privdata->device_thread = gwthread_create(at2_device_thread, conn)) == -1) {
        privdata->shutdown = 1;
        goto error;
    }

    conn->shutdown = at2_shutdown_cb;
    conn->queued = at2_queued_cb;
    conn->start_conn = at2_start_cb;
    conn->send_msg = at2_add_msg_cb;
    return 0;

error:
    error(0, "AT2[%s]: Failed to create at2 smsc connection",
          octstr_len(privdata->name) ? octstr_get_cstr(privdata->name) : "");
    if (privdata != NULL) {
        gw_prioqueue_destroy(privdata->outgoing_queue, NULL);
    }
    gw_free(privdata);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    info(0, "AT2[%s]: exiting", octstr_get_cstr(privdata->name));
    return -1;
}


static int at2_pdu_extract(PrivAT2data *privdata, Octstr **pdu, Octstr *line, Octstr *smsc_number)
{
    Octstr *buffer;
    long len = 0;
    int pos = 0;
    int tmp;
	Octstr *numtmp;
	Octstr *tmp2;
	
    buffer = octstr_duplicate(line);
    /* find the beginning of a message from the modem*/

    if ((pos = octstr_search(buffer, octstr_imm("+CDS:"), 0)) != -1) 
        pos += 5;
    else {
        if ((pos = octstr_search(buffer, octstr_imm("+CMT:"), 0)) != -1)
            pos += 5;
        else if ((pos = octstr_search(buffer, octstr_imm("+CMGR:"), 0)) != -1) {
            /* skip status field in +CMGR response */
            if ((pos = octstr_search(buffer, octstr_imm(","), pos + 6)) != -1) 
                pos++;
            else
                goto nomsg;
        } else
            goto nomsg;

        /* skip the next comma in CMGR and CMT responses */
        tmp = octstr_search(buffer, octstr_imm(","), pos);
        if (!privdata->modem->broken && tmp == -1)
            goto nomsg;
        if (tmp != -1)
            pos = tmp + 1;
    }

    /* read the message length */
    pos = octstr_parse_long(&len, buffer, pos, 10);
    if (pos == -1 || len == 0)
        goto nomsg;

    /* skip the spaces and line return */
    while (isspace(octstr_get_char(buffer, pos)))
        pos++;

	octstr_truncate(smsc_number,0);

    /* skip the SMSC address on some modem types */
    if (!privdata->modem->no_smsc) {
        tmp = at2_hexchar(octstr_get_char(buffer, pos)) * 16
              + at2_hexchar(octstr_get_char(buffer, pos + 1));
        if (tmp < 0)
            goto nomsg;
       
        numtmp = octstr_create_from_data(octstr_get_cstr(buffer)+pos+2,tmp * 2);	/* we now have the hexchars of the SMSC in GSM encoding */
		octstr_hex_to_binary(numtmp);
		tmp2 = gsm2number(numtmp);
		debug("bb.smsc.at2", 0, "AT2[%s]: received message from SMSC: %s", octstr_get_cstr(privdata->name), octstr_get_cstr(tmp2));
		octstr_destroy(numtmp);
		octstr_append(smsc_number,tmp2);
		octstr_destroy(tmp2);
        pos += 2 + tmp * 2;
    }

    /* check if the buffer is long enough to contain the full message */
    if (!privdata->modem->broken && octstr_len(buffer) < len * 2 + pos)
        goto nomsg;

    if (privdata->modem->broken && octstr_len(buffer) < len * 2)
        goto nomsg;

    /* copy the PDU then remove it from the input buffer*/
    *pdu = octstr_copy(buffer, pos, len * 2);

    octstr_destroy(buffer);
    return 1;

nomsg:
    octstr_destroy(buffer);
    return 0;
}

static unsigned char	nibble2hex(unsigned char b)
{
	if(b < 0x0A)
		return '0'+ b;
	else
		return 'A'+ b - 0x0A;
}

static Octstr *gsm2number(Octstr *pdu)
{
    Octstr *tmp = NULL;
    unsigned char c;
	unsigned char a;
	unsigned char b;
	int ton;
    int len;
	int pos;

	pos=0;
    len = octstr_len(pdu);
	if(len<= 0)
		return octstr_create("");
		
    ton = octstr_get_char(pdu,pos++);
    ton =  (ton >> 4) & 0x07;

	switch(ton)
	{
	case 0: /* unknown */
		tmp = octstr_create("");
		break;
	case 1: /* international */
		tmp = octstr_create("+");
		break;
	case 2: /* national */
		tmp = octstr_create("0");
		break;
	case 3: /* network-specific */
	default:
		tmp = octstr_create("");
		break;
	}
	while(--len > 0)
	{
	    c = octstr_get_char(pdu,pos++);
		a =  c & 0x0F;
		b =  ((c & 0xF0) >> 4);
	
		if((b == 0x0F) && (len < 2))
		{
			octstr_append_char(tmp, nibble2hex(a));
		}
		else
		{
			octstr_append_char(tmp, nibble2hex(a));
			octstr_append_char(tmp, nibble2hex(b));
		}
	}
	return tmp;
}

static int at2_hexchar(int hexc)
{
    hexc = toupper(hexc) - 48;
    return (hexc > 9) ? hexc - 7 : hexc;
}


static Msg *at2_pdu_decode(Octstr *data, PrivAT2data *privdata)
{
    int type;
    Msg *msg = NULL;

    /* Get the PDU type */
    type = octstr_get_char(data, 1) & 3;

    switch (type) {

        case AT_DELIVER_SM:
            msg = at2_pdu_decode_deliver_sm(data, privdata);
            break;
        case AT_STATUS_REPORT_SM:
            msg = at2_pdu_decode_report_sm(data, privdata);
            break;

            /* Add other message types here: */
    }

    return msg;
}


static Msg *at2_pdu_decode_deliver_sm(Octstr *data, PrivAT2data *privdata)
{
    int len, pos, i, ntype;
    int udhi, dcs, udhlen, pid;
    Octstr *origin = NULL;
    Octstr *udh = NULL;
    Octstr *text = NULL, *tmpstr;
    Octstr *pdu = NULL;
    Msg *message = NULL;
    struct universaltime mtime; /* time structure */
    long stime; /* time in seconds */
    int timezone; /* timezone in 15 minutes jumps from GMT */

    /* 
     * Note: some parts of the PDU are not decoded because they are
     * not needed for the Msg type. 
     */

    /* convert the pdu to binary format for ease of processing */
    pdu = at2_convertpdu(data);

    /* UDH Indicator */
    udhi = (octstr_get_char(pdu, 0) & 64) >> 6;

    /* originating address */
    len = octstr_get_char(pdu, 1);
    if (len > 20) /* maximum valid number of semi-octets in Address-Value field */
        goto msg_error;
    ntype = octstr_get_char(pdu, 2);

    pos = 3;
    if ((ntype & 0xD0) == 0xD0) {
        /* Alphanumeric sender */
        origin = octstr_create("");
        tmpstr = octstr_copy(pdu, 3, len);
        at2_decode7bituncompressed(tmpstr, (((len - 1) * 4 - 3) / 7) + 1, origin, 0);
        octstr_destroy(tmpstr);
        debug("bb.smsc.at2", 0, "AT2[%s]: Alphanumeric sender <%s>", 
              octstr_get_cstr(privdata->name), octstr_get_cstr(origin));
        pos += (len + 1) / 2;
    } else {
        origin = octstr_create("");
        if ((ntype & 0x90) == 0x90) {
            /* International number */
            octstr_append_char(origin, '+');
        }
        for (i = 0; i < len; i += 2, pos++) {
            octstr_append_char(origin, (octstr_get_char(pdu, pos) & 15) + 48);
            if (i + 1 < len)
                octstr_append_char(origin, (octstr_get_char(pdu, pos) >> 4) + 48);
        }
        debug("bb.smsc.at2", 0, "AT2[%s]: Numeric sender %s <%s>", 
              octstr_get_cstr(privdata->name), ((ntype & 0x90) == 0x90 ? "(international)" : ""), 
              octstr_get_cstr(origin));
    }

    if (pos > octstr_len(pdu))
        goto msg_error;

    /* PID */
    pid = octstr_get_char(pdu, pos);
    pos++;

    /* DCS */
    dcs = octstr_get_char(pdu, pos);
    pos++;

    /* get the timestamp */
    mtime.year = swap_nibbles(octstr_get_char(pdu, pos));
    pos++;
    mtime.year += (mtime.year < 70 ? 2000 : 1900);
    mtime.month = swap_nibbles(octstr_get_char(pdu, pos));
    mtime.month--;    
    pos++;
    mtime.day = swap_nibbles(octstr_get_char(pdu, pos));
    pos++;
    mtime.hour = swap_nibbles(octstr_get_char(pdu, pos));
    pos++;
    mtime.minute = swap_nibbles(octstr_get_char(pdu, pos));
    pos++;
    mtime.second = swap_nibbles(octstr_get_char(pdu, pos));
    pos++;

    /* 
     * time zone: 
     *
     * time zone is "swapped nibble", with the MSB as the sign (1 is negative).  
     */
    timezone = swap_nibbles(octstr_get_char(pdu, pos));
    pos++;
    timezone = ((timezone >> 7) ? -1 : 1) * (timezone & 127);
    /* 
     * Ok, that was the time zone as read from the PDU. Now how to interpert it? 
     * All the handsets I tested send the timestamp of their local time and the 
     * timezone as GMT+0. I assume that the timestamp is the handset's local time, 
     * so we need to apply the timezone in reverse to get GM time: 
     */

    /* 
     * time in PDU is handset's local time and timezone is handset's time zone 
     * difference from GMT 
     */
    mtime.hour -= timezone / 4;
    mtime.minute -= 15 * (timezone % 4);

    stime = date_convert_universal(&mtime);

    /* get data length
     * TODO: Is it allowed to have length = 0 ??? (alex)
     */
    len = octstr_get_char(pdu, pos);
    pos++;

    debug("bb.smsc.at2", 0, "AT2[%s]: User data length read as (%d)", 
          octstr_get_cstr(privdata->name), len);

    /* if there is a UDH */
    udhlen = 0;
    if (udhi && len > 0) {
        udhlen = octstr_get_char(pdu, pos);
        pos++;
        if (udhlen + 1 > len)
            goto msg_error;
        udh = octstr_copy(pdu, pos-1, udhlen+1);
        pos += udhlen;
        len -= udhlen + 1;
    } else if (len <= 0) /* len < 0 is impossible, but sure is sure */
        udhi = 0;

    debug("bb.smsc.at2", 0, "AT2[%s]: Udh decoding done len=%d udhi=%d udhlen=%d udh='%s'",
          octstr_get_cstr(privdata->name), len, udhi, udhlen, (udh ? octstr_get_cstr(udh) : ""));

    if (pos > octstr_len(pdu) || len < 0)
        goto msg_error;

    /* build the message */
    message = msg_create(sms);
    if (!dcs_to_fields(&message, dcs)) {
        /* TODO Should we reject this message? */
        error(0, "AT2[%s]: Invalid DCS (0x%02x)", octstr_get_cstr(privdata->name), dcs);
        dcs_to_fields(&message, 0);
    }

    message->sms.pid = pid;

    /* deal with the user data -- 7 or 8 bit encoded */
    tmpstr = octstr_copy(pdu, pos, len);
    if (message->sms.coding == DC_8BIT || message->sms.coding == DC_UCS2) {
        text = octstr_duplicate(tmpstr);
    } else {
        int offset = 0;
        text = octstr_create("");
        if (udhi && message->sms.coding == DC_7BIT) {
            int nbits;
            nbits = (udhlen + 1) * 8;
            /* fill bits for UDH to septet boundary */
            offset = (((nbits / 7) + 1) * 7 - nbits) % 7;
            /*
             * Fix length because UDH data length is determined
             * in septets if we are in GSM coding, otherwise it's in octets. Adding 6
             * will ensure that for an octet length of 0, we get septet length 0,
             * and for octet length 1 we get septet length 2. 
             */
            len = len + udhlen + 1 - (8 * (udhlen + 1) + 6) / 7;
        }
        at2_decode7bituncompressed(tmpstr, len, text, offset);
    }

    message->sms.sender = origin;
    if (octstr_len(privdata->my_number)) {
        message->sms.receiver = octstr_duplicate(privdata->my_number);
    } else {
        /* Put a dummy address in the receiver for now (SMSC requires one) */
        message->sms.receiver = octstr_create_from_data("1234", 4);
    }
    if (udhi) {
        message->sms.udhdata = udh;
    }
    message->sms.msgdata = text;
    message->sms.time = stime;

    /* cleanup */
    octstr_destroy(pdu);
    octstr_destroy(tmpstr);

    return message;
    
msg_error:
    error(1, "AT2[%s]: Invalid DELIVER-SMS pdu!", octstr_get_cstr(privdata->name));
    O_DESTROY(udh);
    O_DESTROY(origin);
    O_DESTROY(text);
    O_DESTROY(pdu);
    return NULL;
}


static Msg *at2_pdu_decode_report_sm(Octstr *data, PrivAT2data *privdata)
{
    Msg *dlrmsg = NULL;
    Octstr *pdu, *msg_id, *tmpstr = NULL, *receiver = NULL;
    int type, tp_mr, len, ntype, pos;

    /*
     * parse the PDU.
     */

    /* convert the pdu to binary format for ease of processing */
    pdu = at2_convertpdu(data);

    /* Message reference */
    tp_mr = octstr_get_char(pdu, 1);
    msg_id = octstr_format("%d", tp_mr);
    debug("bb.smsc.at2", 0, "AT2[%s]: got STATUS-REPORT for message <%d>:", 
          octstr_get_cstr(privdata->name), tp_mr);
    
    /* reciver address */
    len = octstr_get_char(pdu, 2);
    ntype = octstr_get_char(pdu, 3);

    pos = 4;
    if ((ntype & 0xD0) == 0xD0) {
        /* Alphanumeric sender */
        receiver = octstr_create("");
        tmpstr = octstr_copy(pdu, pos, (len + 1) / 2);
        at2_decode7bituncompressed(tmpstr, (((len - 1) * 4 - 3) / 7) + 1, receiver, 0);
        octstr_destroy(tmpstr);
        debug("bb.smsc.at2", 0, "AT2[%s]: Alphanumeric receiver <%s>",
              octstr_get_cstr(privdata->name), octstr_get_cstr(receiver));
        pos += (len + 1) / 2;
    } else {
        int i;
        receiver = octstr_create("");
        if ((ntype & 0x90) == 0x90) {
            /* International number */
            octstr_append_char(receiver, '+');
        }
        for (i = 0; i < len; i += 2, pos++) {
            octstr_append_char(receiver, (octstr_get_char(pdu, pos) & 15) + 48);
            if (i + 1 < len)
                octstr_append_char(receiver, (octstr_get_char(pdu, pos) >> 4) + 48);
        }
        debug("bb.smsc.at2", 0, "AT2[%s]: Numeric receiver %s <%s>",
              octstr_get_cstr(privdata->name), ((ntype & 0x90) == 0x90 ? "(international)" : ""),
              octstr_get_cstr(receiver));
    }

    pos += 14; /* skip time stamps for now */

    if ((type = octstr_get_char(pdu, pos)) == -1 ) {
        error(1, "AT2[%s]: STATUS-REPORT pdu too short to have TP-Status field !",
              octstr_get_cstr(privdata->name));
        goto error;
    }

	/* Check DLR type:
	 * 3GPP TS 23.040 defines this a bit mapped field with lots of options
	 * most of which are not really intersting to us, as we are only interested
	 * in one of three conditions : failed, held in SC for delivery later, or delivered successfuly
	 * and here's how I suggest to test it (read the 3GPP reference for further detailes) -
	 * we'll test the 6th and 5th bits (7th bit when set making all other values 'reseved' so I want to test it).
	 */
    type = type & 0xE0; /* filter out everything but the 7th, 6th and 5th bits */
    switch (type) {
        case 0x00:
            /* 0 0 : success class */
            type = DLR_SUCCESS;
            tmpstr = octstr_create("Success");
            break;
        case 0x20:
            /* 0 1 : buffered class (temporary error) */
            type = DLR_BUFFERED;
            tmpstr = octstr_create("Buffered");
            break;
        case 0x40:
        case 0x60:
        default:
            /* 1 0 : failed class */
            /* 1 1 : failed class (actually, temporary error but timed out) */
            /* and any other value (can't think of any) is considered failure */
            type = DLR_FAIL;
            tmpstr = octstr_create("Failed");
            break;
    }
    /* Actually, the above implementation is not correct, as the reference 
     * says that implementations should consider any "reserved" values to be 
     * "failure", but most reserved values fall into one of the three 
     * categories. It will catch "reserved" values where the first 3 MSBits 
     * are not set as "Success" which may not be correct. */

    if ((dlrmsg = dlr_find(privdata->conn->id, msg_id, receiver, type, 0)) == NULL) {
        debug("bb.smsc.at2", 1, "AT2[%s]: Received delivery notification but can't find that ID in the DLR storage",
              octstr_get_cstr(privdata->name));
	    goto error;
    }

    /* Beware DLR URL is now in msg->sms.dlr_url given by dlr_find() */
    dlrmsg->sms.msgdata = octstr_duplicate(tmpstr);
	
error:
    O_DESTROY(tmpstr);
    O_DESTROY(pdu);
    O_DESTROY(receiver);
    O_DESTROY(msg_id);
    return dlrmsg;
}

static Octstr *at2_convertpdu(Octstr *pdutext)
{
    Octstr *pdu;
    int i;
    int len = octstr_len(pdutext);

    pdu = octstr_create("");
    for (i = 0; i < len; i += 2) {
        octstr_append_char(pdu, at2_hexchar(octstr_get_char(pdutext, i)) * 16
                           + at2_hexchar(octstr_get_char(pdutext, i + 1)));
    }
    return pdu;
}


static int at2_rmask[8] = { 0, 1, 3, 7, 15, 31, 63, 127 };
static int at2_lmask[8] = { 0, 128, 192, 224, 240, 248, 252, 254 };

static void at2_decode7bituncompressed(Octstr *input, int len, Octstr *decoded, int offset)
{
    unsigned char septet, octet, prevoctet;
    int i;
    int r = 1;
    int c = 7;
    int pos = 0;

    /* Shift the buffer offset bits to the left */
    if (offset > 0) {
        unsigned char *ip;
        for (i = 0, ip = (unsigned char *)octstr_get_cstr(input); i < octstr_len(input); i++) {
            if (i == octstr_len(input) - 1)
                *ip = *ip >> offset;
            else
                *ip = (*ip >> offset) | (*(ip + 1) << (8 - offset));
            ip++;
        }
    }
    octet = octstr_get_char(input, pos);
    prevoctet = 0;
    for (i = 0; i < len; i++) {
        septet = ((octet & at2_rmask[c]) << (r - 1)) + prevoctet;
        octstr_append_char(decoded, septet);

        prevoctet = (octet & at2_lmask[r]) >> c;

        /* When r=7 we have a full character in prevoctet */
        if ((r == 7) && (i < len - 1)) {
            i++;
            octstr_append_char(decoded, prevoctet);
            prevoctet = 0;
        }

        r = (r > 6) ? 1 : r + 1;
        c = (c < 2) ? 7 : c - 1;

        pos++;
        octet = octstr_get_char(input, pos);
    }
    charset_gsm_to_utf8(decoded);
}


static void at2_send_messages(PrivAT2data *privdata)
{
    Msg *msg;

    if (privdata->modem->enable_mms && gw_prioqueue_len(privdata->outgoing_queue) > 1)                  
        at2_send_modem_command(privdata, "AT+CMMS=2", 0, 0);

    if (privdata->conn->throughput > 0 && load_get(privdata->load, 0) >= privdata->conn->throughput) {
      debug("bb.sms.at2", 0, "AT2[%s]: throughput limit exceeded (load: %.02f, throughput: %.02f)",
            octstr_get_cstr(privdata->conn->id), load_get(privdata->load, 0), privdata->conn->throughput);
    } else {
      if ((msg = gw_prioqueue_remove(privdata->outgoing_queue))) {                 
          load_increase(privdata->load);
          at2_send_one_message(privdata, msg);
      }
    }
}


static void at2_send_one_message(PrivAT2data *privdata, Msg *msg)
{
    char command[500];
    int ret = -1;
    char sc[3];

    if (octstr_len(privdata->my_number)) {
        octstr_destroy(msg->sms.sender);
        msg->sms.sender = octstr_duplicate(privdata->my_number);
    }

    /* 
     * The standard says you should be prepending the PDU with 00 to indicate 
     * to use the default SC. Some older modems dont expect this so it can be 
     * disabled 
     * NB: This extra padding is not counted in the CMGS byte count 
     */
    sc[0] = '\0';

    if (!privdata->modem->no_smsc)
        strcpy(sc, "00");

    if (msg_type(msg) == sms) {
        Octstr *pdu;
        int msg_id = -1;

        if ((pdu = at2_pdu_encode(msg, privdata)) == NULL) {
            error(2, "AT2[%s]: Error encoding PDU!",octstr_get_cstr(privdata->name));
            return;
        }

        /* 
         * send the initial command and then wait for > 
         */
        sprintf(command, "AT+CMGS=%ld", octstr_len(pdu) / 2);

        ret = at2_send_modem_command(privdata, command, 5, 1);
        debug("bb.smsc.at2", 0, "AT2[%s]: send command status: %d",
                octstr_get_cstr(privdata->name), ret);

        if (ret == 1) {/* > only! */

            /* 
             * Ok the > has been see now so we can send the PDU now and a 
             * control Z but no CR or LF 
             * 
             * We will handle the 'nokiaphone' types a bit differently, since
             * they have a generic error in accepting PDUs that are "too big".
             * Which means, PDU that are longer then 18 bytes get truncated by
             * the phone modems. We'll buffer the PDU output in a loop.
             * All other types will get handled as used to be.
             */

            if (octstr_compare(privdata->modem->id, octstr_imm("nokiaphone")) != 0) { 
                sprintf(command, "%s%s", sc, octstr_get_cstr(pdu));
                at2_write(privdata, command);
                at2_write_ctrlz(privdata);
            } else {
                /* include the CTRL-Z in the PDU string */
                sprintf(command, "%s%s%c", sc, octstr_get_cstr(pdu), 0x1A);

                /* chop PDU into 18-byte-at-a-time pieces to prevent choking 
                 * of certain GSM Phones (e.g. Nokia 6310, 6230 etc.) */
                if (strlen(command) > 18) {
                    char chop[20];
                    int len = strlen(command);
                    int pos = 0;
                    int ret = 18;

                    while (pos < len) {
                        if (pos + ret > len)
                            ret = len - pos;
                        memcpy(chop, command + pos, ret);
                        pos += ret;
                        chop[ret] = '\0';
                        at2_write(privdata, chop);
                        gwthread_sleep((double) 10/1000);
                    }
                } else {
                    at2_write(privdata, command);
                }
            }               

            /* wait 20 secs for modem command */
            ret = at2_wait_modem_command(privdata, 20, 0, &msg_id);
            debug("bb.smsc.at2", 0, "AT2[%s]: send command status: %d",
                  octstr_get_cstr(privdata->name), ret);

            if (ret != 0) {
                bb_smscconn_send_failed(privdata->conn, msg,
                        SMSCCONN_FAILED_TEMPORARILY, octstr_create("ERROR"));
            } else {
                /* store DLR message if needed for SMSC generated delivery reports */
                if (DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask)) {
                    if (msg_id == -1)
                        error(0,"AT2[%s]: delivery notification requested, but I have no message ID!",
                                octstr_get_cstr(privdata->name));
                    else {
                        Octstr *dlrmsgid = octstr_format("%d", msg_id);

                        dlr_add(privdata->conn->id, dlrmsgid, msg, 0);

                        O_DESTROY(dlrmsgid);

                    }
                }

                bb_smscconn_sent(privdata->conn, msg, NULL);
            }
        } else {
            error(0,"AT2[%s]: Error received, notifying failure, "
                 "sender: %s receiver: %s msgdata: %s udhdata: %s",
                  octstr_get_cstr(privdata->name),
                  octstr_get_cstr(msg->sms.sender), octstr_get_cstr(msg->sms.receiver),
                  octstr_get_cstr(msg->sms.msgdata), octstr_get_cstr(msg->sms.udhdata));
            bb_smscconn_send_failed(privdata->conn, msg,
                                    SMSCCONN_FAILED_TEMPORARILY, octstr_create("ERROR"));
        }
        O_DESTROY(pdu);
    }
}


static Octstr *at2_pdu_encode(Msg *msg, PrivAT2data *privdata)
{
    /*
     * Message coding is done as a binary octet string,
     * as per 3GPP TS 23.040 specification (GSM 03.40),
     */
    Octstr *pdu = NULL, *temp = NULL, *buffer = octstr_create("");
    int len, setvalidity = 0;

    /* 
     * message type SUBMIT , bit mapped :
     * bit7                            ..                                    bit0
     * TP-RP , TP-UDHI, TP-SRR, TP-VPF(4), TP-VPF(3), TP-RD, TP-MTI(1), TP-MTI(0)
     */
    octstr_append_char(buffer,
        ((msg->sms.rpi > 0 ? 1 : 0) << 7) /* TP-RP */
        | ((octstr_len(msg->sms.udhdata)  ? 1 : 0) << 6) /* TP-UDHI */
        | ((DLR_IS_ENABLED_DEVICE(msg->sms.dlr_mask) ? 1 : 0) << 5) /* TP-SRR */
        | 16 /* TP-VP(Rel)*/
        | 1 /* TP-MTI: SUBMIT_SM */
	);

    /* message reference (0 for now) */
    octstr_append_char(buffer, 0);

    /* destination address */
    if ((temp = at2_format_address_field(msg->sms.receiver)) == NULL)
        goto error;
    octstr_append(buffer, temp);
    O_DESTROY(temp);

    octstr_append_char(buffer, (msg->sms.pid == SMS_PARAM_UNDEFINED ? 0 : msg->sms.pid) ); /* protocol identifier */
    octstr_append_char(buffer, fields_to_dcs(msg, /* data coding scheme */
        (msg->sms.alt_dcs != SMS_PARAM_UNDEFINED ? msg->sms.alt_dcs : privdata->conn->alt_dcs)));

    /* 
     * Validity-Period (TP-VP)
     * see GSM 03.40 section 9.2.3.12
     * defaults to 24 hours = 167 if not set 
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
    } else
        setvalidity = (privdata->validityperiod != NULL ? 
            atoi(octstr_get_cstr(privdata->validityperiod)) : 167);

    if (setvalidity >= 0 && setvalidity <= 143)
        debug("bb.smsc.at2", 0, "AT2[%s]: TP-Validity-Period: %d minutes",
              octstr_get_cstr(privdata->name), (setvalidity + 1)*5);
    else if (setvalidity >= 144 && setvalidity <= 167)
        debug("bb.smsc.at2", 0, "AT2[%s]: TP-Validity-Period: %3.1f hours",
              octstr_get_cstr(privdata->name), ((float)(setvalidity - 143) / 2) + 12);
    else if (setvalidity >= 168 && setvalidity <= 196)
        debug("bb.smsc.at2", 0, "AT2[%s]: TP-Validity-Period: %d days",
              octstr_get_cstr(privdata->name), (setvalidity - 166));
    else
        debug("bb.smsc.at2", 0, "AT2[%s]: TP-Validity-Period: %d weeks",
              octstr_get_cstr(privdata->name), (setvalidity - 192));
    octstr_append_char(buffer, setvalidity);

    /* user data length - include length of UDH if it exists */
    len = sms_msgdata_len(msg);

    if (octstr_len(msg->sms.udhdata)) {
        if (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2) {
            len += octstr_len(msg->sms.udhdata);
            if (len > SMS_8BIT_MAX_LEN) { /* truncate user data to allow UDH to fit */
                octstr_delete(msg->sms.msgdata, SMS_8BIT_MAX_LEN - octstr_len(msg->sms.udhdata), 9999);
                len = SMS_8BIT_MAX_LEN;
            }
        } else {
            /*
             * The reason we branch here is because UDH data length is determined
             * in septets if we are in GSM coding, otherwise it's in octets. Adding 6
             * will ensure that for an octet length of 0, we get septet length 0,
             * and for octet length 1 we get septet length 2. 
             */
            int temp_len;
            len += (temp_len = (((8 * octstr_len(msg->sms.udhdata)) + 6) / 7));
            if (len > SMS_7BIT_MAX_LEN) { /* truncate user data to allow UDH to fit */
                octstr_delete(msg->sms.msgdata, SMS_7BIT_MAX_LEN - temp_len, 9999);
                len = SMS_7BIT_MAX_LEN;
            }
        }
    }

    octstr_append_char(buffer,len);

    if (octstr_len(msg->sms.udhdata)) /* udh */
        octstr_append(buffer, msg->sms.udhdata);

    /* user data */
    if (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2) {
        octstr_append(buffer, msg->sms.msgdata);
    } else {
        int offset = 0;
        Octstr *msgdata;

        /*
         * calculate the number of fill bits needed to align
         * the 7bit encoded user data on septet boundry
         */
        if (octstr_len(msg->sms.udhdata)) { /* Have UDH */
            int nbits = octstr_len(msg->sms.udhdata) * 8; /* Includes UDH length byte */
            offset = (((nbits / 7) + 1) * 7 - nbits) % 7; /* Fill bits */
        }

        msgdata = octstr_duplicate(msg->sms.msgdata);
        charset_utf8_to_gsm(msgdata);
        
        if ((temp = at2_encode7bituncompressed(msgdata, offset)) != NULL)
            octstr_append(buffer, temp);
        O_DESTROY(temp);
        octstr_destroy(msgdata);
    }

    /* convert PDU to HEX representation suitable for the AT2 command set */
    pdu = at2_encode8bituncompressed(buffer);
    O_DESTROY(buffer);

    return pdu;

error:
    O_DESTROY(temp);
    O_DESTROY(buffer);
    O_DESTROY(pdu);
    return NULL;
}


static Octstr *at2_encode7bituncompressed(Octstr *source, int offset)
{
    int LSBmask[8] = { 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F };
    int MSBmask[8] = { 0x00, 0x40, 0x60, 0x70, 0x78, 0x7C, 0x7E, 0x7F };
    int destRemain = (int)ceil((octstr_len(source) * 7.0 + offset) / 8.0);
    int i = (offset?8-offset:7), iStore = offset;
    int posS;
    Octstr *target = octstr_create("");
    int target_chr = 0, source_chr;

    /* start packing the septet stream into an octet stream */
    for (posS = 0; (source_chr = octstr_get_char(source, posS++)) != -1;) {
        /* grab least significant bits from current septet and 
         * store them packed to the right */
        target_chr |= (source_chr & LSBmask[i]) << iStore;
        /* store current byte if last command filled it */
        if (iStore != 0) {
            destRemain--;
            octstr_append_char(target, target_chr);
            target_chr = 0;
        }
        /* grab most significant bits from current septet and 
         * store them packed to the left */
        target_chr |= (source_chr & MSBmask[7 - i]) >> (8 - iStore) % 8;
        /* advance target bit index by 7 (modulo 8 addition ) */
        iStore = (--iStore < 0 ? 7 : iStore);
        /* if just finished packing 8 septets (into 7 octets) don't advance mask index */
        if (iStore != 0) 
            i = (++i > 7 ? 1 : i); 
    }

    /* don't forget to pack the leftovers ;-) */
    if (destRemain > 0)
        octstr_append_char(target, target_chr);

    return target;
}


static Octstr *at2_encode8bituncompressed(Octstr *input)
{
    int len, i;
    Octstr *out = octstr_create("");

    len = octstr_len(input);

    for (i = 0; i < len; i++) {
        /* each character is encoded in its hex representation (2 chars) */
        octstr_append_char(out, at2_numtext( (octstr_get_char(input, i) & 0xF0) >> 4));
        octstr_append_char(out, at2_numtext( (octstr_get_char(input, i) & 0x0F)));
    }
    return out;
}


static int at2_numtext(int num)
{
    return (num > 9) ? (num + 55) : (num + 48);
}


static int at2_detect_speed(PrivAT2data *privdata)
{
    int i;
    int autospeeds[] = { 
#ifdef B115200
	115200,
#endif
#ifdef	B57600
	57600, 
#endif
	38400, 19200, 9600 };

    debug("bb.smsc.at2", 0, "AT2[%s]: detecting modem speed. ", 
          octstr_get_cstr(privdata->name));

    for (i = 0; i < (sizeof(autospeeds) / sizeof(int)) && !privdata->shutdown; i++) {
        if(at2_test_speed(privdata, autospeeds[i]) == 0) {
            privdata->speed = autospeeds[i];
            break;
        }
    }
    if (privdata->speed == 0) {
        info(0, "AT2[%s]: cannot detect speed", octstr_get_cstr(privdata->name));
        return -1;
    }
    info(0, "AT2[%s]: detect speed is %ld", octstr_get_cstr(privdata->name), privdata->speed);
    return 0;
}


static int at2_test_speed(PrivAT2data *privdata, long speed) 
{
    int res;

    if (at2_open_device(privdata) == -1)
        return -1;

    at2_read_buffer(privdata, 0); /* give telnet escape sequences a chance */
    at2_set_speed(privdata, speed);
    /* send a return so the modem can detect the speed */
    res = at2_send_modem_command(privdata, "", 1, 0); 
    res = at2_send_modem_command(privdata, "AT", 0, 0);

    if (res != 0)
        res = at2_send_modem_command(privdata, "AT", 0, 0);
    if (res != 0)
        res = at2_send_modem_command(privdata, "AT", 0, 0);
        
    at2_close_device(privdata);

    return res;
}


static int at2_detect_modem_type(PrivAT2data *privdata)
{
    int res;
    ModemDef *modem;
    int i;

    debug("bb.smsc.at2", 0, "AT2[%s]: detecting modem type", octstr_get_cstr(privdata->name));

    if (at2_open_device(privdata) == -1)
        return -1;

    at2_set_speed(privdata, privdata->speed);
    /* sleep 10 ms in order to get device some time to accept speed */
    gwthread_sleep(0.1);

    /* reset the modem */
    if (at2_send_modem_command(privdata, "ATZ", 0, 0) == -1) {
        error(0, "AT2[%s]: Wrong or no answer to ATZ", octstr_get_cstr(privdata->name));
        at2_close_device(privdata);
        return -1;
    }

    /* check if the modem responded */
    if (at2_send_modem_command(privdata, "AT", 0, 0) == -1) {
        error(0, "AT2[%s]: Wrong or no answer to AT. Trying again", octstr_get_cstr(privdata->name));
        if (at2_send_modem_command(privdata, "AT", 0, 0) == -1) {
            error(0, "AT2[%s]: Second attempt to send AT failed", octstr_get_cstr(privdata->name));
            at2_close_device(privdata);
            return -1;
        }
    }

    at2_flush_buffer(privdata);

    /* send a return so the modem can detect the speed */
    res = at2_send_modem_command(privdata, "", 1, 0); 
    res = at2_send_modem_command(privdata, "AT", 0, 0);

    if (at2_send_modem_command(privdata, "AT&F", 0, 0) == -1) {
        at2_close_device(privdata);
        return -1;
    }

    at2_flush_buffer(privdata);

    if (at2_send_modem_command(privdata, "ATE0", 0, 0) == -1) {
        at2_close_device(privdata);
        return -1;
    }

    at2_flush_buffer(privdata);

    if (at2_send_modem_command(privdata, "ATI", 0, 0) == -1) {
        at2_close_device(privdata);
        return -1;
    }

    /* we try to detect the modem automatically */
    i = 1;
    while ((modem = at2_read_modems(privdata, privdata->configfile, NULL, i++)) != NULL) {

        if (octstr_len(modem->detect_string) == 0) {
            at2_destroy_modem(modem);
            continue;
        }

        /* 
        debug("bb.smsc.at2",0,"AT2[%s]: searching for %s", octstr_get_cstr(privdata->name), 
              octstr_get_cstr(modem->name)); 
        */

        if (octstr_search(privdata->lines, modem->detect_string, 0) != -1) {
            if (octstr_len(modem->detect_string2) == 0) {
                debug("bb.smsc.at2", 0, "AT2[%s]: found string <%s>, using modem definition <%s>", 
                      octstr_get_cstr(privdata->name), octstr_get_cstr(modem->detect_string), 
                      octstr_get_cstr(modem->name));
                privdata->modem = modem;
                break;
            } else {
                if (octstr_search(privdata->lines, modem->detect_string2, 0) != -1) {
                    debug("bb.smsc.at2", 0, "AT2[%s]: found string <%s> plus <%s>, using modem "
                          "definition <%s>", octstr_get_cstr(privdata->name), 
                          octstr_get_cstr(modem->detect_string), 
                          octstr_get_cstr(modem->detect_string2), 
                          octstr_get_cstr(modem->name));
                    privdata->modem = modem;
                    break;
                }
            }
        } else {
            /* Destroy modem */
            at2_destroy_modem(modem);
        }
    }

    if (privdata->modem == NULL) {
        debug("bb.smsc.at2", 0, "AT2[%s]: Cannot detect modem, using generic", 
              octstr_get_cstr(privdata->name));
        if ((modem = at2_read_modems(privdata, privdata->configfile, octstr_imm("generic"), 0)) == NULL) {
            panic(0, "AT2[%s]: Cannot detect modem and generic not found", 
                  octstr_get_cstr(privdata->name));
        } else {
            privdata->modem = modem;
        }
    }

    /* lets see if it supports GSM SMS 2+ mode */
    res = at2_send_modem_command(privdata, "AT+CSMS=?", 0, 0);
    if (res != 0)
        /* if it doesnt even understand the command, I'm sure it won't support it */
        privdata->phase2plus = 0; 
    else {
        /* we have to take a part a string like +CSMS: (0,1,128) */
        Octstr *ts;
        int i;
        List *vals;

        ts = privdata->lines;
        privdata->lines = NULL;

        i = octstr_search_char(ts, '(', 0);
        if (i > 0) {
            octstr_delete(ts, 0, i + 1);
        }
        i = octstr_search_char(ts, ')', 0);
        if (i > 0) {
            octstr_truncate(ts, i);
        }
        vals = octstr_split(ts, octstr_imm(","));
        octstr_destroy(ts);
        ts = gwlist_search(vals, octstr_imm("1"), (void*) octstr_item_match);
        if (ts)
            privdata->phase2plus = 1;
        gwlist_destroy(vals, octstr_destroy_item);
    }
    if (privdata->phase2plus)
        info(0, "AT2[%s]: Phase 2+ is supported", octstr_get_cstr(privdata->name));
    at2_close_device(privdata);
    return 0;
}


static ModemDef *at2_read_modems(PrivAT2data *privdata, Octstr *file, Octstr *id, int idnumber)
{
    Cfg *cfg;
    List *grplist;
    CfgGroup *grp;
    Octstr *p;
    ModemDef *modem;
    int i = 1;

    /* 
     * Use id and idnumber=0 or id=NULL and idnumber > 0 
     */
    if (octstr_len(id) == 0 && idnumber == 0)
        return NULL;

    if (idnumber == 0)
        debug("bb.smsc.at2", 0, "AT2[%s]: Reading modem definitions from <%s>", 
              octstr_get_cstr(privdata->name), octstr_get_cstr(file));
    cfg = cfg_create(file);

    if (cfg_read(cfg) == -1)
        panic(0, "Cannot read modem definition file");

    grplist = cfg_get_multi_group(cfg, octstr_imm("modems"));
    if (idnumber == 0)
        debug("bb.smsc.at2", 0, "AT2[%s]: Found <%ld> modems in config", 
              octstr_get_cstr(privdata->name), gwlist_len(grplist));

    if (grplist == NULL)
        panic(0, "Where are the modem definitions ?!?!");

    grp = NULL;
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p == NULL) {
            info(0, "Modems group without id, bad");
            continue;
        }
        /* Check by id */
        if (octstr_len(id) != 0 && octstr_compare(p, id) == 0) {
            O_DESTROY(p);
            break;
        }
        /* Check by idnumber */
        if (octstr_len(id) == 0 && idnumber == i) {
            O_DESTROY(p);
            break;
        }
        O_DESTROY(p);
        i++;
        grp = NULL;
    }
    if (grplist != NULL)
        gwlist_destroy(grplist, NULL);

    if (grp != NULL) {
        modem = gw_malloc(sizeof(ModemDef));

        modem->id = cfg_get(grp, octstr_imm("id"));

        modem->name = cfg_get(grp, octstr_imm("name"));
        if (modem->name == NULL)
            modem->name = octstr_duplicate(modem->id);

        modem->detect_string = cfg_get(grp, octstr_imm("detect-string"));
        modem->detect_string2 = cfg_get(grp, octstr_imm("detect-string2"));

        modem->init_string = cfg_get(grp, octstr_imm("init-string"));
        if (modem->init_string == NULL)
            modem->init_string = octstr_create("AT+CNMI=1,2,0,1,0");

        modem->reset_string = cfg_get(grp, octstr_imm("reset-string"));

        modem->speed = 9600;
        cfg_get_integer(&modem->speed, grp, octstr_imm("speed"));

        cfg_get_bool(&modem->need_sleep, grp, octstr_imm("need-sleep"));

        modem->enable_hwhs = cfg_get(grp, octstr_imm("enable-hwhs"));
        if (modem->enable_hwhs == NULL)
            modem->enable_hwhs = octstr_create("AT+IFC=2,2");

        cfg_get_bool(&modem->no_pin, grp, octstr_imm("no-pin"));

        cfg_get_bool(&modem->no_smsc, grp, octstr_imm("no-smsc"));

        modem->sendline_sleep = 100;
        cfg_get_integer(&modem->sendline_sleep, grp, octstr_imm("sendline-sleep"));

        modem->keepalive_cmd = cfg_get(grp, octstr_imm("keepalive-cmd"));
        if (modem->keepalive_cmd == NULL)
            modem->keepalive_cmd = octstr_create("AT");

        modem->message_storage = cfg_get(grp, octstr_imm("message-storage"));
        if (cfg_get_integer(&modem->message_start, grp, octstr_imm("message-start")))
            modem->message_start = 1;

        cfg_get_bool(&modem->enable_mms, grp, octstr_imm("enable-mms"));
        modem->hardware_flow_control = 1;
        cfg_get_bool(&modem->hardware_flow_control, grp, octstr_imm("hardware-flow-control"));

        /*	
        if (modem->message_storage == NULL)
            modem->message_storage = octstr_create("SM");
        */

        cfg_get_bool(&modem->broken, grp, octstr_imm("broken"));

        cfg_destroy(cfg);
        return modem;

    } else {
        cfg_destroy(cfg);
        return NULL;
    }
}


static void at2_destroy_modem(ModemDef *modem)
{
    if (modem != NULL) {
        O_DESTROY(modem->id);
        O_DESTROY(modem->name);
        O_DESTROY(modem->detect_string);
        O_DESTROY(modem->detect_string2);
        O_DESTROY(modem->init_string);
        O_DESTROY(modem->enable_hwhs);
        O_DESTROY(modem->keepalive_cmd);
        O_DESTROY(modem->message_storage);
        O_DESTROY(modem->reset_string);
        gw_free(modem);
    }
}


static int swap_nibbles(unsigned char byte)
{
    return ( ( byte & 15 ) * 10 ) + ( byte >> 4 );
}


static Octstr *at2_format_address_field(Octstr *msisdn)
{
    int ntype = PNT_UNKNOWN;
    Octstr *out = octstr_create("");
    Octstr *temp = octstr_duplicate(msisdn);

    octstr_strip_blanks(temp);
    /*
     * Check for international numbers
     * number starting with '+' or '00' are international,
     * others are national.
     */
    if (strncmp(octstr_get_cstr(msisdn), "+", 1) == 0) {
	octstr_delete(temp, 0, 1);
        ntype = PNT_INTER; /* international */
    } else if (strncmp(octstr_get_cstr(msisdn), "00", 2) == 0) {
        octstr_delete(temp, 0, 2);
        ntype = PNT_INTER; /* international */
    }

    /* address length */
    octstr_append_char(out, octstr_len(temp));

    /* Type of address : bit mapped values */
    octstr_append_char(out, 0x80 /* Type-of-address prefix */ |
			    0x01 /* Numbering-plan: MSISDN */ |
			    (ntype == PNT_INTER ? 0x10 : 0x00) /* Type-of-number: International or National */
			    );

    /* grab the digits from the MSISDN and encode as swapped semi-octets */
    while (out != NULL && octstr_len(temp) > 0) {
	int digit1, digit2;
	/* get the first two digit */
	digit1 = octstr_get_char(temp,0) - 48;
        digit2 = octstr_get_char(temp,1) - '0';
        if (digit2 < 0)
	    digit2 = 0x0F;
        if(digit1 >= 0 && digit1 < 16 && digit2 < 16) {
            octstr_append_char(out, (digit2 << 4) | digit1);
        }
        else {
            O_DESTROY(out);
            out = NULL;
        }
        octstr_delete(temp, 0, 2);
    }

    O_DESTROY(temp);
    return out;	
}


static int at2_set_message_storage(PrivAT2data *privdata, Octstr *memory_name)
{
    Octstr *temp;
    int ret;

    if (!memory_name || !privdata)
        return -1;

    temp = octstr_format("AT+CPMS=\"%S\"", memory_name);
    ret = at2_send_modem_command(privdata, octstr_get_cstr(temp), 0, 0);
    octstr_destroy(temp);

    return !ret ? 0 : -1;
}


static const char *at2_error_string(int errcode)
{
    /*
     * +CMS ERRORS
     * 0...127 from GSM 04.11 Annex E-2 values
     * 128...255 from GSM 03.40 subclause 9.2.3.22
     * 300...511 from GSM 07.05 subclause 3.2.5
     * 512+ are manufacturer specific according to GSM 07.05 subclause 3.2.5
     * 
     * +CME ERRORS
     * CME Error codes from GSM 07.07 section 9.2
     * GPP TS 27.007 /2/
     * GPRS-related errors - (GSM 04.08 cause codes)
     * 
     */
    switch (errcode) {
    case 0:
        /*
         * Default the code to 0 then when you extract the value from the
         * modem response message and no code is found, 0 will result.
         */
        return "Modem returned ERROR but no error code - possibly unsupported or invalid command?";
    case 1: 
        /* 
         * This cause indicates that the destination requested by the Mobile 
         * Station cannot be reached because, although the number is in a 
         * valid format, it is not currently assigned (allocated).
         */
        return "Unassigned (unallocated) number (+CMS) or No connection to phone (+CME)";
    case 2:
        return "Phone-adaptor link reserved";
    case 3: 
        /* 
         * This can be a lot of things, depending upon the command, but in general
         * it relates to trying to do a command when no connection exists.
         */
        return "Operation not allowed at this time (connection may be required)";
    case 4: 
        /* 
         * This can be a lot of things, depending upon the command, but in general
         * it relates to invaid parameters being passed.
         */
        return "Operation / Parameter(s) not supported";
    case 5: 
        return "PH-SIM PIN required";
    case 8:
        /*
         * This cause indicates that the MS has tried to send a mobile originating
         * short message when the MS's network operator or service provider has
         * forbidden such transactions.
         */
        return "Operator determined barring";
    case 10:
        /*
         * This cause indicates that the outgoing call barred service applies to
         * the short message service for the called destination.
         */
        return "Call barred (+CMS) or SIM not inserted or Card inserted is not a SIM (+CME)";
    case 11:
        return "SIM PIN required";
    case 12:
        return "SIM PUK required";
    case 13:
        return "SIM failure";
    case 14:
        return "SIM busy";
    case 15:
        return "SIM wrong";
    case 16:
        return "Incorrect password";
    case 17:
        /*
         * This cause is sent to the MS if the MSC cannot service an MS generated
         * request because of PLMN failures, e.g. problems in MAP.
         */
        return "Network failure (+CMS) or SIM PIN2 required (+CME)";
    case 18:
        return "SIM PUK2 required";
    case 20:
        return "Memory full";
    case 21:
        /*
         * This cause indicates that the equipment sending this cause does not 
         * wish to accept this short message, although it could have accepted 
         * the short message since the equipment sending this cause is neither 
         * busy nor incompatible.
         */
        return "Short message transfer rejected (+CMS) or Invalid Index (+CME)";
    case 22:
        /*
         * This cause is sent if the service request cannot be actioned because 
         * of congestion (e.g. no channel, facility busy/congested etc.). Or 
         * this cause indicates that the mobile station cannot store the 
         * incoming short message due to lack of storage capacity.
         */
        return "Congestion (+CMS) or Memory capacity exceeded (+CME)";
    case 23:
        return "Memory failure";
    case 24:
        return "Text string too long"; /* +CPBW, +CPIN, +CPIN2, +CLCK, +CPWD */
    case 25:
        return "Invalid characters in text string";
    case 26:
        return "Dial string too long"; /* +CPBW, ATD, +CCFC */
    case 27:
        /*
         * This cause indicates that the destination indicated by the Mobile 
         * Station cannot be reached because the interface to the destination 
         * is not functioning correctly. The term "not functioning correctly" 
         * indicates that a signalling message was unable to be delivered to 
         * the remote user; e.g., a physical layer or data link layer failure 
         * at the remote user, user equipment off-line, etc.
         * Also means "Invalid characters in dial string" for +CPBW.
         */
        return "Destination out of service";
    case 28:
        /*
         * This cause indicates that the subscriber is not registered in the PLMN 
         * (i.e. IMSI not known).
         */
        return "Unidentified subscriber";
    case 29:
        /*
         * This cause indicates that the facility requested by the Mobile Station 
         * is not supported by the PLMN.
         */
        return "Facility rejected";
    case 30:
        /*
         * This cause indicates that the subscriber is not registered in the HLR 
         * (i.e. IMSI or directory number is not allocated to a subscriber).
         * Also means "No network service" for +VTS, +COPS=?, +CLCK, +CCFC, +CCWA, +CUSD
         */
        return "Unknown subscriber (+CMS) or No network service (+CME)";
    case 31:
        return "Network timeout";
    case 32:
        return "Network not allowed - emergency calls only"; /* +COPS */
    case 38:
        /*
         * This cause indicates that the network is not functioning correctly and 
         * that the condition is likely to last a relatively long period of time; 
         * e.g., immediately reattempting the short message transfer is not 
         * likely to be successful.
         */
        return "Network out of order";
    case 40:
        return "Network personal PIN required (Network lock)";
    case 41:
        /*
         * This cause indicates that the network is not functioning correctly and 
         * that the condition is not likely to last a long period of time; e.g., 
         * the Mobile Station may wish to try another short message transfer 
         * attempt almost immediately.
         */
        return "Temporary failure (+CMS) or Network personalization PUK required (+CME)";
    case 42:
        /*
         * This cause indicates that the short message service cannot be serviced 
         * because of high traffic.
         */
        return "Congestion (+CMS) or Network subset personalization PIN required (+CME)";
    case 43:
        return "Network subset personalization PUK required";
    case 44:
        return "Service provider personalization PIN required";
    case 45:
        return "Service provider personalization PUK required";
    case 46:
        return "Corporate personalization PIN required";
    case 47:
        /*
         * This cause is used to report a resource unavailable event only when no 
         * other cause applies.
         */
        return "Resources unavailable, unspecified (+CMS) or Corporate personalization PUK required (+CME)";
    case 50:
        /*
         * This cause indicates that the requested short message service could not
         * be provided by the network because the user has not completed the 
         * necessary administrative arrangements with its supporting networks.
         */
        return "Requested facility not subscribed";
    case 69:
        /*
         * This cause indicates that the network is unable to provide the 
         * requested short message service.
         */
        return "Requested facility not implemented";
    case 81:
        /*
         * This cause indicates that the equipment sending this cause has received
         * a message with a short message reference which is not currently in use 
         * on the MS-network interface.
         */
        return "Invalid short message transfer reference value";
    case 95:
        /*
         * This cause is used to report an invalid message event only when no 
         * other cause in the invalid message class applies.
         */
        return "Invalid message, unspecified";
    case 96:
        /*
         * This cause indicates that the equipment sending this cause has received
         * a message where a mandatory information element is missing and/or has 
         * a content error (the two cases are indistinguishable).
         */
        return "Invalid mandatory information";
    case 97:
        /*
         * This cause indicates that the equipment sending this cause has received
         * a message with a message type it does not recognize either because this
         * is a message not defined or defined but not implemented by the 
         * equipment sending this cause.
         */
        return "Message type non-existent or not implemented";
    case 98:
        /*
         * This cause indicates that the equipment sending this cause has received
         * a message such that the procedures do not indicate that this is a 
         * permissible message to receive while in the short message transfer 
         * state.
         */
        return "Message not compatible with short message protocol state";
    case 99:
        /*
         * This cause indicates that the equipment sending this cause has received
         * a message which includes information elements not recognized because 
         * the information element identifier is not defined or it is defined 
         * but not implemented by the equipment sending the cause. However, the 
         * information element is not required to be present in the message in 
         * order for the equipment sending the cause to process the message.
         */
        return "Information element non-existent or not implemented";
    case 100:
        return "Unknown";
    case 103:
        return "Illegal MS (#3)"; /* +CGATT */
    case 106:
        return "Illegal ME (#6)"; /* +CGATT */
    case 107:
        return "GPRS services not allowed (#7)"; /* +CGATT */
    case 111:
        /*
         * This cause is used to report a protocol error event only when no other 
         * cause applies.
         * Also means "PLMN not allowed (#11)" for +CGATT
         */
        return "Protocol error, unspecified (+CMS) or PLMN not allowed (#11) (+CME)";
    case 112:
        return "Location area not allowed (#12)"; /* +CGATT */
    case 113:
        return "Roaming not allowed in this area (#13)"; /* +CGATT */
    case 127:
        /*
         * This cause indicates that there has been interworking with a network 
         * which does not provide causes for actions it takes; thus, the precise 
         * cause for a message which is being send cannot be ascertained.
         */
        return "Interworking, unspecified";
    case 128:
        return "Telematic interworking not supported";
    case 129:
        return "Short message Type 0 not supported";
    case 130:
        return "Cannot replace short message";
    case 132:
        return "Service option not supported (#32)"; /* +CGACT +CGDATA ATD*99 */
    case 133:
        return "Requested service option not subscribed (#33)"; /* +CGACT +CGDATA ATD*99 */
    case 134:
        return "Service option temporarily out of order (#34)"; /* +CGACT +CGDATA ATD*99 */
    case 143:
        return "Unspecified TP-PID error";
    case 144:
        return "Data coding scheme (alphabet) not supported";
    case 145:
        return "Message class not supported";
    case 148:
        return "Unspecified GPRS error";
    case 149:
        return "PDP authentication failure"; /* +CGACT +CGDATA ATD*99 */
    case 150:
        return "Invalid mobile class";
    case 159:
        return "Unspecified TP-DCS error";
    case 160:
        return "Command cannot be actioned";
    case 161:
        return "Unsupported command";
    case 175:
        return "Unspecified TP-Command error";
    case 176:
        return "TPDU not supported";
    case 192:
        return "SC busy";
    case 193:
        return "No SC subscription";
    case 194:
        return "SC system failure";
    case 195:
        return "Invalid SME address";
    case 196:
        return "Destination SME barred";
    case 197:
        return "SM Rejected-Duplicate SM";
    case 198:
        return "TP-VPF not supported";
    case 199:
        return "TP-VP not supported";
    case 208:
        return "DO SIM SMS storage full";
    case 209:
        return "No SMS storage capability in SIM";
    case 210:
        return "Error in MS";
    case 211:
        return "SIM Memory Capacity Exceeded";
    case 212:
        return "SIM Application Toolkit Busy";
    case 213:
        return "SIM data download error";
    case 255:
        return "Unspecified error cause";
    case 300:
        /*
         * Mobile equipment refers to the mobile device that communicates with
         * the wireless network. Usually it is a mobile phone or GSM/GPRS modem.
         * The SIM card is defined as a separate entity and is not part of mobile equipment.
         */
        return "Mobile equipment (ME) failure";
    case 301:
        /*
         * See +CMS error code 300 for the meaning of mobile equipment. 
         */
        return "SMS service of mobile equipment (ME) is reserved";
    case 302:
        return "The operation to be done by the AT command is not allowed";
    case 303:
        return "The operation to be done by the AT command is not supported";
    case 304:
        return "One or more parameter values assigned to the AT command are invalid";
    case 305:
        return "One or more parameter values assigned to the AT command are invalid";
    case 310:
        return "There is no SIM card";
    case 311:
        /*
         * The AT command +CPIN (command name in text: Enter PIN)
         * can be used to send the PIN to the SIM card.
         */
        return "The SIM card requires a PIN to operate";
    case 312:
        /*
         * The AT command +CPIN (command name in text: Enter PIN)
         * can be used to send the PH-SIM PIN to the SIM card.
         */           
        return "The SIM card requires a PH-SIM PIN to operate";
    case 313:
        return "SIM card failure";
    case 314:
        return "The SIM card is busy";
    case 315:
        return "The SIM card is wrong";
    case 316:
        /*
         * The AT command +CPIN (command name in text: Enter PIN)
         * can be used to send the PUK to the SIM card.
         */
        return "The SIM card requires a PUK to operate";
    case 317:
        return "The SIM card requires a PIN2 to operate";
    case 318:
        return "The SIM card requires a PUK2 to operate";
    case 320:
        return "Memory/message storage failure";
    case 321:
        return "The memory/message storage index assigned to the AT command is invalid";
    case 322:
        return "The memory/message storage is out of space";
    case 330:
        return "The SMS center (SMSC) address is unknown";
    case 331:
        return "No network service is available";
    case 332:
        return "Network timeout occurred";
    case 340:
        return "There is no need to send message ack by the AT command +CNMA";
    case 500:
        return "An unknown error occurred";
    case 512:
        /*
         * Resulting from +CMGS, +CMSS
         */
        return "User abort or MM establishment failure (SMS)";
    case 513:
        /*
         * Resulting from +CMGS, +CMSS
         */
       return "Lower layer falure (SMS)";
    case 514:
        /*
         * Resulting from +CMGS, +CMSS
         */
        return "CP error (SMS)";
    case 515:
        return "Please wait, service not available, init or command in progress";
    case 517:
        /*
         * Resulting from +STGI
         */
        return "SIM ToolKit facility not supported";
    case 518:
        /*
         * Resulting from +STGI
         */
        return "SIM ToolKit indication not received";
    case 519:
        /*
         * Resulting from +ECHO, +VIP
         */
        return "Reset the product to activate or change a new echo cancellation algorithm";
    case 520:
        /*
         * Resulting from +COPS=?
         */
        return "Automatic abort about get plmn list for an incoming call";
    case 526:
        /*
         * Resulting from +CLCK
         */
        return "PIN deactivation forbidden with this SIM card";
    case 527:
        /*
         * Resulting from +COPS
         */
        return "Please wait, RR or MM is busy. Retry your selection later";
    case 528:
        /*
         * Resulting from +COPS
         */
        return "Location update failure. Emergency calls only";
    case 529:
        /*
         * Resulting from +COPS
         */
        return "PLMN selection failure. Emergency calls only";
    case 531:
        /*
         * Resulting from +CMGS, +CMSS
         */
        return "SMS not sent: the <da> is not in FDN phonebook, and FDN lock is enabled";
    case 532:
        /*
         * Resulting from +WOPEN
         */
        return "The embedded application is activated so the objects flash are not erased";
    case 533:
        /*
         * Resulting from +ATD*99,+GACT,+CGDATA
         */
        return "Missing or unknown APN";
    default:
        return "Error number unknown. Ask google and add it";
    }
}

