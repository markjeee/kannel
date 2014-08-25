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
 * fakewap.c - simulate wap clients talking directly to wap gw.
 *
 * This module can be built also in Windows where you should
 * add unzipped ".\wininc" to your include directories.
 *
 * The protocol:
 *
 *
 *    A)    Fakewap -> Gateway
 *
 *        WTP: Invoke PDU
 *        WSP: Connect PDU
 *
 *    B)    Gateway -> Fakewap
 *
 *        WTP: Result PDU
 *        WSP: ConnectReply PDU
 *
 *    C)    Fakewap -> Gateway
 *
 *        WTP: Ack PDU
 *
 *    D)    Fakewap -> Gateway
 *
 *        WTP: Invoke PDU
 *        WSP: Get/Post PDU (data: URL)
 *
 *    E)    Gateway -> Fakewap
 *
 *        WTP: Result PDU (data: WML page)
 *        WSP: Reply PDU
 *
 *    F)    Fakewap -> Gateway
 *
 *        WTP: Ack PDU
 *
 *    G)    Fakewap -> Gateway
 *
 *        WTP: Invoke PDU
 *        WSP: Disconnect PDU
 *
 *
 *    Packets A-C open a WAP session. Packets D-F fetch a WML page.
 *    Packet G closes the session.
 *
 * The test terminates when all packets have been sent.
 *
 * Tid verification uses following protocol (at WTP level only):
 *
 *    A)   Fakewap -> Gateway
 *
 *         Either WSP Connect PDU with tid_new flag set on or same PDU with a 
 *         *seriously* wrapped up tid (only WTP header affected). Seriously
 *         means tid being out of the window:
 *
 *         |----------------------------|
 *                  tid space
 *
 *         |-------------|
 *          wrapping up
 *          tid window
 *
 *    B)   Gateway -> Fakewap
 *
 *         Ack PDU, tid verification flag set on.
 *
 *    C)   Fakewap -> Gateway
 *
 *         Ack PDU, tid verification flag set on (this means a positive 
 *         answer). 
 *
 * Antti Saarenheimo for WapIT Ltd.
 */

#define MAX_SEND (0)

/* Versions:
 * v1.0 - added WSP GET result reassembly
 * v1.1 - added WSP POST - options -P, -C, -w
 * v1.2 - added source address (-I) option
 * v1.3 - send Nack for lost segments, reassembly fixes
 * v1.4 - parse WSP message and save only the received payload to the output file
 * v1.5 - support for connectionless get/post
 * v1.6 - robustness fixes for Post (resend group segments if no ack), packet loss simulation
 */
static char usage[] = "\
fakewap version 1.6\n\
Usage: fakewap [options] url ...\n\
\n\
where options are:\n\
\n\
-h		help\n\
-g hostname	hostname or IP number of gateway (default: localhost)\n\
-p port		port number of gateway (default: 9201)\n\
-m max		maximum number of requests fakewap will make (default: 1)\n\
-i interval	interval between requests (default: 1.0 seconds)\n\
-c threads	number of concurrent clients simulated (default: 1)\n\
-V protoversion	protocol version field, as an integer (default: 0)\n\
-T pdu-type	PDU type, as an integer (default: 1)\n\
-t tcl		transaction class, as an integer (default: 2)\n\
-n		set tid_new flag in packets, forces gateway to flush cache\n\
                (default: off)\n\
-s              test separation, by concatenating ack and disconnect pdus\n\
                (default: off)\n\
-d difference	difference between successive tid numbers (default: 1)\n\
-F		Accept failure and continue rather than exiting\n\
-A agent        user agent\n\
-C content-type Specify content type: text, mms\n\
-D level	debug level (0=none(default), 1=brief, 2=verbose\n\
-I addr[:port]  Specify source address\n\
-M mode         Transaction mode: 0=connectionless, 1=connection-oriented\n\
-P in-file	Post data from file\n\
-w out-file	Write received data to file\n\
-l loss-precent Simulate packet loss\n\
\n\
The urls are fetched in random order.\n\
";

#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/param.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "gwlib/gwlib.h"
#include "wap/wsp_pdu.h"

#define GET_WTP_PDU_TYPE(hdr)  ((hdr[0] >> 3) & 0x0f)
static int get_wtp_pdu_type(Octstr *hdr)
{
    return (octstr_get_char(hdr, 0) >> 3) & 0x0f;
}

#define WTP_PDU_INVOKE  1
#define WTP_PDU_RESULT  2
#define WTP_PDU_ACK     3
#define WTP_PDU_ABORT   4
#define WTP_PDU_SEGM_INVOKE 5
#define WTP_PDU_SEGM_RESULT 6
#define WTP_PDU_NACK        7

#define WSP_PDU_CONNECT    1
#define WSP_PDU_REPLY      4
#define WSP_PDU_DISCONNECT 5
#define WSP_PDU_GET        0x40
#define WSP_PDU_POST       0x60

#define TXN_MODE_CONNECTION_LESS      0
#define TXN_MODE_CONNECTION_ORIENTED  1

#define SAR_SEGM_SIZE            1200
#define	SAR_GROUP_LEN            4
#define SAR_MAX_RETRIES          3
#define WAP_MSG_RECEIVE_TIMEOUT  10

/*
**  Common parameters
*/
char **urls;
int num_urls;

Octstr *hostname = NULL;
Octstr *gateway_addr = NULL;
double interval = 1.0;
unsigned short port = 0;
int max_send = 1;
unsigned short tid_addition = 1;
Mutex *mutex;
int threads = 1;
int num_sent = 0;
time_t start_time, end_time;
double totaltime = 0, besttime = 1000000L,  worsttime = 0;
int brief; /* enable brief debug */
int verbose = 0;
int nofailexit = 0;
int test_separation = 0;
char* infile;
char* outfile;
const char* content_type;
struct sockaddr_in src_addr;
int transaction_mode;
int packet_loss; /* packet loss rate 0-99 */
Octstr *useragent;

/*
 * PDU type, version number and transaction class are supplied by a 
 * command line argument. WSP_Concat is a concatenation of WTP_Ack and 
 * WSP_Disconnect PDUs.
 */
unsigned char WSP_Connect[] = { /* WSP part */
                               0x01, /* PDU type */
                               0x10, /* Version 1.0 */
                               0x0a, /* Capability length */
                               0x0d, /* Headers length = 13 */
                               /* Capabilities */
                               0x04, 0x80, 0xc0, 0x80, 0x00, /* Client SDU */
                               0x04, 0x81, 0xc0, 0x80, 0x00, /* Server SDU */
                               /* Headers */
                               0x80, 0x80, /* Accept: *\* */
				};
unsigned char WSP_ConnectReply[] = {0x16, 0x80, 0x00, 0x02 };
unsigned char WSP_Get[] =          {0x40};
/* This used to also expect a content-type of 0x94, but that's too difficult
 * to check now that Kannel does full header encoding. */
unsigned char WSP_Reply[] =        {0x16, 0x80, 0x00, 0x04, 0x20 };
unsigned char WSP_Disconnect[] =   {0x05};
unsigned char WSP_Post[] = { /* wsp post */ 0x60 };
unsigned char WSP_Concat[] = {0x00, 0x03, 0x18, 0x00, 0x00, 0x05, 0x0E, 0x00, 0x00, 0x00, 0x05 };

unsigned char WTP_Ack[] =          {0x18, 0x00, 0x00 };
unsigned char WTP_TidVe[] =        {0x1C, 0x00, 0x00 };
unsigned char WTP_Abort[] =        {0x20, 0x00, 0x00, 0x00 };
unsigned char WTP_Invoke_Cl0[] = { /*wtp invoke,ttr*/ 0x0a, /*tid*/ 0x00, 0x00, /*ack+class0*/ 0x10 };
unsigned char WTP_Invoke_Cl2[] = { /*wtp invoke,ttr*/ 0x0a, /*tid*/ 0x00, 0x00, /*ack+class2*/ 0x12 };
unsigned char WTP_Invoke_Cl2MaxGrp[] = { /*wtp invoke*/ 0x0E, /*tid*/ 0x00, 0x00, /*ack/class2:*/ 0x12,
                                         /*tti max-group: 4200*/ 0x13, 0x04, 0x10, 0x68 };
unsigned char WTP_Result[] =  {0x10, 0x80, 0x00 };
unsigned char WTP_Segm_Result[] =  {0x30, 0x80, 0x00 };
unsigned char WTP_Segm_Ack[] = {0x98, 0x00, 0x00, 0x19, 0x00 };
unsigned char WTP_Nack[] = { /* wtp nack */ 0x38, /* tid */ 0x00, 0x00, /*num missing*/ 0x00 };
unsigned char WTP_Segm_Invoke[] = { /* wtp segm invoke*/ 0x28, /* tid */ 0x00, 0x00, /* psn */ 0x00 };

/*
**  In this case it does not matter what is the byte order
*/
#define SET_GTR( hdr ) hdr[0] |= 0x04
#define SET_TID( hdr, tid) \
	hdr[1] |= (0x7f & ((tid) >> 8)); \
	hdr[2] = (char)(tid)
#define GET_TID( hdr ) (((hdr[1] & 0x7f) << 8) + hdr[2])
#define CONSTRUCT_EXPECTED_REPLY_HDR( dest, template, tid ) \
    if (sizeof(dest) < sizeof(template)) panic(0,"fakewap: buffer overflow.");\
    memcpy( dest, template, sizeof(template));\
    SET_TID( dest, tid )

static void set_tid(Octstr *hdr, int tid) {
	int c;
	
	c = octstr_get_char(hdr, 1);
	c |= 0x7f & (tid >> 8);
	octstr_set_char(hdr, 1, c);
	octstr_set_char(hdr, 2, (unsigned char) tid);
}

/* Use this only on Invoke packets, the others have no tid_new field */
static void set_tid_new(Octstr *hdr) {
	int c;

	c = octstr_get_char(hdr, 3);
	c |= 0x40;
	octstr_set_char(hdr, 3, c);
}


#ifndef min
#define min(a,b) (a < b ? a : b)
#endif


/*
**  if -v option has been defined, function prints the trace message and
**  the first bytes in the message header
*/
static void print_msg( const char * trace, unsigned char * msg,
                int msg_len ) {
    int i;
    if (verbose) {
        mutex_lock( mutex );
        printf( "%s (len %d): ", trace, msg_len );
        for (i = 0; i < msg_len && i < 16; i++) printf( "%02X ", msg[i] );
        printf( "\n");
        mutex_unlock( mutex );
    }
}
/*
**  function prints the trace message and the first bytes in the message header
*/   
static void print_data( const char * trace, unsigned char * msg,
                int msg_len ) {
    int i;

    if (verbose)
    {
        mutex_lock( mutex );
        printf( "%s (len %d): ", trace, msg_len );
        for (i = 0; i < msg_len && i < msg_len; i++)
             printf( "%c", isprint(msg[i]) ? msg[i] : '_');
        printf( "\n");
        mutex_unlock( mutex );
    }
}

/* Choose a random message from a table of messages. */
static char *choose_message(char **urls, int num_urls) {
    /* the following doesn't give an even distribution, but who cares */
    return urls[gw_rand() % num_urls];
}


/* returns next tid, given current tid.  Every thread has its own
 * port, so has its own tid space. */
static unsigned short next_tid(unsigned short old_tid) { 
    return (old_tid + tid_addition) % (1 << 15);
}


/*
**  Function stores WAP/WSP variable length integer to buffer and returns 
**  actual len
*/
static int StoreVarInt( unsigned char *buf, unsigned long varInt )
{
    int i, len = 1, non_zero_bits = 7;

    /*
    **    Skip all zero high bits
    */
    while ((varInt >> non_zero_bits) != 0) {
        non_zero_bits += 7;
        len++;
    }
    /*
    **    Read the higest bits first.
    */
    for (i = 0; i < len; i++)
    {
        buf[i] = ((unsigned char)(varInt >> (non_zero_bits-7)) & 0x7f) | 0x80;
        non_zero_bits -= 7;
    }
    buf[len-1] &= 0x7f;
    return len;
}


/*
**  Function length of WAP/WSP variable length integer in the buffer
*/
static int ReadVarIntLen( const unsigned char *buf )
{
    int    len = 1;

    while (buf[len-1] & 0x80) len++;
    return len;
}

/*
**  Retrieve value of WAP/WSP variable length integer in the buffer
*/
static int ReadVarIntVal( const unsigned char *buf )
{
    int    len = 1;
    int    value = buf[0] & 0x7F;
    while (buf[len-1] & 0x80)
    {
        len++;
        value <<= 7;
        value |= buf[len-1] & 0x7F;
    }
    return value;
}

/*
**  Function sends message to WAP GW
*/
static int
wap_msg_send( int fd, unsigned char* wtp_hdr, int wtp_hdr_len, unsigned short tid, int tid_new,
              unsigned char* wsp_hdr, int wsp_hdr_len, unsigned char* data, int data_len )
{
    int ret;
    Octstr *datagram;

    datagram = octstr_create("");
    if (wtp_hdr != NULL) {
    	octstr_append_data(datagram, (char*)wtp_hdr, wtp_hdr_len);

        set_tid(datagram, tid);
        if (get_wtp_pdu_type(datagram) == WTP_PDU_INVOKE) {
            /* request ack every time */
            int c;
            c = octstr_get_char(datagram, 3);
            octstr_set_char(datagram, 3, c | 0x10);
            if (tid_new)
		set_tid_new(datagram);
        }
    }

    if (wsp_hdr != NULL)
        octstr_append_data(datagram, (char*)wsp_hdr, wsp_hdr_len);

    if (data != NULL)
	octstr_append_data(datagram, (char*)data, data_len);
    
#if 0
    debug("fakewap", 0, "Sending WDP datagram:");
    octstr_dump(datagram, 0);
#endif
    ret = udp_sendto(fd, datagram, gateway_addr);

    if (ret == -1) {
        error(0, "fakewap: Sending to socket failed");
        return -1;
    }

    if (brief) {
        if (wsp_hdr!=NULL) {
            int wsp_pdu;
            if (transaction_mode==TXN_MODE_CONNECTION_ORIENTED)
                wsp_pdu = wsp_hdr[0];
            else
                wsp_pdu = wsp_hdr[1];
            switch (wsp_pdu)
            {
            case WSP_PDU_CONNECT:
                debug("fakewap", 0, "Sent WSP_CONNECT packet");
                break;
            case WSP_PDU_DISCONNECT:
                debug("fakewap", 0, "Sent WSP_DISCONNECT packet");
                break;
            case WSP_PDU_GET:
                debug("fakewap", 0, "Sent WSP_GET packet");
                break;
            case WSP_PDU_POST:
                debug("fakewap", 0, "Sent WSP_POST packet");
                break;
            default:
                debug("fakewap", 0, "Sent WSP ??? packet");
                break;               
            }
        }
        else {
            switch (get_wtp_pdu_type(datagram))
            {
            case WTP_PDU_INVOKE:
                debug("fakewap", 0, "Sent WTP_INVOKE packet");
                break;
            case WTP_PDU_ACK:
                debug("fakewap", 0, "Sent WTP_ACK packet");
                break;
            case WTP_PDU_ABORT:
                debug("fakewap", 0, "Sent WTP_ABORT packet");
                break;
            case WTP_PDU_SEGM_INVOKE:
                debug("fakewap", 0, "Sent WTP_SEGM_INVOKE packet");
                break;
            case WTP_PDU_NACK:
                debug("fakewap", 0, "Sent WTP_NACK packet");
                break;
            default:
                debug("fakewap", 0, "Sent ??? packet");
                break;
            }
        }
    }
    if (verbose) {
	octstr_dump(datagram, 0);
    }
    octstr_destroy(datagram);
    return ret;
}

/*
**  Function receives a wap wtl/wsp message. If the headers has been
**  given, it must match with the received message.
**  Return value:
**      >  0 => length of received data
**      == 0 => got acknowlengement or abort but not the expected data
**      < 0  => error,
*/
static int
wap_msg_recv( int fd, const unsigned char* hdr, int hdr_len,
              unsigned short tid, unsigned char * data, int data_len,
              int timeout, int udp_flags )
{
    int ret;
    unsigned char msg[1024*64];
    int msg_len = 0;
    int    fResponderIsDead = 1;  /* assume this by default */
    Octstr *datagram, *dummy;

    /*
    **  Loop until we get the expected response or do timeout
    */
    for (;;)
    {
        time_t calltime;
        time(&calltime);

        if (timeout != 0)
        {
	    ret = read_available(fd, timeout * 1000 * 1000);
	    if (ret <= 0) {
                info(0, "fakewap: Timeout while receiving from socket.\n");
		if(nofailexit){
		    continue;
		}else{
		    return fResponderIsDead ? -1 : 0;
		}
		/* continue if we got ack? */
	    }
        }
	
	ret = udp_recvfrom_flags(fd, &datagram, &dummy, udp_flags);

        /* drop packet if packet loss simulation is enabled
         */
        if ((packet_loss > 0) && !(udp_flags&MSG_PEEK) && ((gw_rand() % 100) < packet_loss))
        {
            time_t currtime;
            time(&currtime);
            octstr_destroy(datagram);
            octstr_destroy(dummy);
            timeout -= (int)(currtime - calltime);
            debug("fakewap", 0, "Dropped packet, new timeout %d", timeout);
            continue;
        }
	if (ret == 0) {
            octstr_get_many_chars((char*)msg, datagram, 0, octstr_len(datagram));
		msg_len = octstr_len(datagram);
	}
	octstr_destroy(datagram);
	octstr_destroy(dummy);

        if (ret == -1) {
            error(0, "fakewap: recv() from socket failed");
            return -1;
        }

        if (hdr != NULL) {
            /*
            **  Ignore extra header bits, WAP GWs return different values
            */
            if ((msg_len >= hdr_len) &&
                (GET_WTP_PDU_TYPE(msg) == GET_WTP_PDU_TYPE(hdr)) &&
                (hdr_len <= 3 || !memcmp( msg+3, hdr+3, hdr_len-3 ))) {
                break;
            }
            /*
            **  Handle TID test, the answer is: Yes, we have an outstanding
            **  transaction with this tid. We must turn on TID_OK-flag, too.
            **  We have a separate tid verification PDU.
            */
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_ACK &&
                     GET_TID(msg) == tid) {
                print_msg( "Received tid verification", msg, msg_len );
                wap_msg_send( fd, WTP_TidVe, sizeof(WTP_TidVe), tid, 0, NULL, 0, NULL, 0 );
            }
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_ABORT) {
                print_msg( "Received WTP Abort", msg, msg_len );
            }
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_RESULT) {
               break;
            }
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_SEGM_RESULT) {
               break;
            }
            else {
                print_msg( "Received unexpected message", msg, msg_len );
            }
            fResponderIsDead = 0;
        }
        else {
            hdr_len = 0;
            break;
        }
    }
    print_msg( "Received packet", msg, msg_len );
    print_data( "Received data", msg, msg_len );

    if (data != NULL && msg_len > hdr_len) {
        data_len = min( data_len, msg_len );
        memcpy( data, msg, data_len);
    }
    else  data_len = 0;
    return data_len;
}


static int get_next_transaction(void) {
    int i_this;
    mutex_lock( mutex );
    i_this = num_sent + 1;
    if (max_send == MAX_SEND || num_sent < max_send) num_sent++;
    mutex_unlock( mutex );
    return i_this;
}

int send_post(int fd, unsigned short tid, int tid_new, char* url)
{
    unsigned char wtphdr[32];
    unsigned char msgbuf[32*1024];
    unsigned char contentlen_buf[8];
    unsigned char reply_hdr[32];
    long timeout = WAP_MSG_RECEIVE_TIMEOUT;  /* wap gw is broken if no input */

    /* open post input file */
    int infd = open(infile, O_RDONLY);
    if (infd < 0)
        panic(0, "fakewap: failed to open input file %s, errno %d", infile, errno);
            
    /* get size of input file */
    struct stat fstats;
    int ret = fstat(infd, &fstats);
    if (ret)
        panic(0, "fakewap: failed to get file stats, errno %d", errno);
    if (fstats.st_size == 0)
        panic(0, "fakewap: input file is empty");
    if (((transaction_mode==TXN_MODE_CONNECTION_LESS) && (fstats.st_size > (int)sizeof(msgbuf))) ||
        ((transaction_mode==TXN_MODE_CONNECTION_ORIENTED) && (fstats.st_size > 256*SAR_SEGM_SIZE))) {
        panic(0, "fakewap: input file size (%ld) is too large", fstats.st_size);
    }
            
    int nsegs = (fstats.st_size-1) / SAR_SEGM_SIZE;
    int tpi, gtr, ttr;
    int psn = 0;

    /* build WTP header */
    tpi = 1;
    ttr = (nsegs==0) ? 1 : 0;
    gtr = (ttr) ? 0 : 1;
    memcpy(wtphdr, WTP_Invoke_Cl2MaxGrp, sizeof(WTP_Invoke_Cl2MaxGrp));
    wtphdr[0] = (tpi << 7) | (WTP_PDU_INVOKE << 3) | (gtr << 2) | (ttr << 1);

    /* build WSP POST message */
    Octstr *postmsg;
    postmsg = octstr_create("");
    if (transaction_mode==TXN_MODE_CONNECTION_LESS) {
        msgbuf[0] = tid;
        octstr_append_data(postmsg, (char*)msgbuf, 1);
    }
    memcpy(msgbuf, WSP_Post, sizeof(WSP_Post));
    octstr_append_data(postmsg, (char*)msgbuf, sizeof(WSP_Post));

    /* add uri, content type, user agent, content length, accept */
    int url_len = strlen(url);
    int off = StoreVarInt( msgbuf, url_len );
    int content_type_len = strlen(content_type) + 1; /* +1 for eos */
    int contentlen_len;
    char acceptAll[] = { 0x80, 0x80 };
    contentlen_buf[0] = 0x8d; /* hdr type - content-length */
    if (fstats.st_size <= 127)
    {
        contentlen_buf[1] = 0x80 | (char)fstats.st_size;
        contentlen_len = 2;
    } else
    {
        contentlen_buf[1] = 2;
        contentlen_buf[2] = (char)(fstats.st_size >> 8);
        contentlen_buf[3] = (char)(fstats.st_size);
        contentlen_len = 4;
    }
    off += StoreVarInt( &msgbuf[off], content_type_len+octstr_len(useragent)+contentlen_len+sizeof(acceptAll) );
    memcpy( &msgbuf[off], url, url_len );
    off += url_len;
    memcpy( &msgbuf[off], content_type, content_type_len );
    off += content_type_len;
    memcpy( &msgbuf[off], octstr_get_cstr(useragent), octstr_len(useragent) );
    off += octstr_len(useragent);
    memcpy( &msgbuf[off], contentlen_buf, contentlen_len );
    off += contentlen_len;
    memcpy( &msgbuf[off], acceptAll, sizeof(acceptAll) );
    off += sizeof(acceptAll);
    octstr_append_data(postmsg, (char*)msgbuf, off);
    
    /* add payload */
    if (transaction_mode==TXN_MODE_CONNECTION_LESS) {
        ret = read(infd, msgbuf, sizeof(msgbuf));
    } else {
        ret = read(infd, msgbuf, SAR_SEGM_SIZE);
    }
    if (ret <= 0)
        panic(0, "fakewap: input file read error, errno %d", errno);
    
    debug("fakewap", 0, "Sending WSP_POST, url %s, Content-Type %s, User-Agent %s, Content-Length %lu",
          url, content_type, octstr_get_cstr(useragent) + 1, fstats.st_size );
    
    if (transaction_mode==TXN_MODE_CONNECTION_LESS) {
        ret = wap_msg_send( fd, NULL, 0, tid, tid_new,
                            (unsigned char*)octstr_get_cstr(postmsg), octstr_len(postmsg), msgbuf, ret );
        if (ret == -1)
        {
            error(0, "fakewap: failure sending connection-less wtp invoke");
            goto send_post_exit;
        }
    } else {
        int retry = 0;
        while (retry++ < SAR_MAX_RETRIES)
        {
            ret = wap_msg_send( fd, wtphdr, sizeof(WTP_Invoke_Cl2MaxGrp), tid, tid_new,
                                (unsigned char*)octstr_get_cstr(postmsg), octstr_len(postmsg), msgbuf, ret );
            if (ret == -1)
            {
                error(0, "fakewap: failure sending connection-oriented wtp invoke");
                goto send_post_exit;
            }
            /* receive invoke ack */
            CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WTP_Ack, tid );
            ret = wap_msg_recv( fd, reply_hdr, sizeof(WTP_Ack),
                                tid, msgbuf, sizeof(msgbuf), timeout, 0 );
            if (ret < 0)
            {
                if (retry>=SAR_MAX_RETRIES) {
                    error(0, "fakewap: failure receiving WTP Ack, psn %u, giving up", psn);
                    goto send_post_exit;
                }
                warning(0, "fakewap: timeout receiving WTP Ack, resend wtp invoke");
                continue;
            }
            debug("fakewap", 0, "Received WTP_ACK, psn 0");
            break;
        }
    }
    
    if (transaction_mode==TXN_MODE_CONNECTION_ORIENTED) {
        
        while (!ttr)
        {
            int  grppktnum = 0;   /* packet in the group */
            unsigned char  grpwtphdr[SAR_GROUP_LEN][32];
            unsigned char  grpmsgbuf[SAR_GROUP_LEN][SAR_SEGM_SIZE];
            int  grpmsglen[SAR_GROUP_LEN];
            int  grpsize = 0;

            /* build segments for a new group */
            gtr = 0;
            while (!gtr && !ttr)
            {
                psn++;
                
                /* create wtp header */
                memcpy(grpwtphdr[grppktnum], WTP_Segm_Invoke, sizeof(WTP_Segm_Invoke));
                tpi = 0;
                ttr = (nsegs==psn) ? 1 : 0;
                gtr = ttr ? 0 : (psn+1)%SAR_GROUP_LEN ? 0 : 1;
                grpwtphdr[grppktnum][0] = (tpi << 7) | (WTP_PDU_SEGM_INVOKE << 3) | (gtr << 2) | (ttr << 1);
                grpwtphdr[grppktnum][3] = psn;

                /* read payload from file */
                grpmsglen[grppktnum] = read(infd, (char*)grpmsgbuf[grppktnum], SAR_SEGM_SIZE);
                if (grpmsglen[grppktnum] <= 0)
                    panic(0, "fakewap: input file read error, errno %d", errno);

                grppktnum++;
                grpsize++;
            }
            
            int retry = 0;
            while (retry++ < SAR_MAX_RETRIES)
            {
                /* send a group of WTP Segmented Invoke messages */
                for (grppktnum = 0; grppktnum < grpsize; grppktnum++)
                {
                    debug("fakewap", 0, "Sending WTP_SEGM_INVOKE, psn %u, payload len %d",
                          psn-grpsize+grppktnum+1, grpmsglen[grppktnum]);
                    ret = wap_msg_send( fd, grpwtphdr[grppktnum], sizeof(WTP_Segm_Invoke), tid, tid_new,
                                        NULL, 0, grpmsgbuf[grppktnum], grpmsglen[grppktnum] );
                    if (ret == -1)
                    {
                        error(0, "fakewap: failure sending wtp invoke");
                        goto send_post_exit;
                    }
                }
                
                if (ttr) {
                    /* receive WTP Result */
                    CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WTP_Result, tid );
                    ret = wap_msg_recv( fd, reply_hdr, sizeof(WTP_Result),
                                        tid, msgbuf, sizeof(msgbuf), timeout, MSG_PEEK );
                } else {
                    /* receive segm invoke ack for the group */
                    CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WTP_Segm_Ack, tid );
                    reply_hdr[4] = psn;
                    ret = wap_msg_recv( fd, reply_hdr, sizeof(WTP_Segm_Ack),
                                        tid, msgbuf, sizeof(msgbuf), timeout, 0 );
                }
                if (ret < 0)
                {
                    if (retry>=SAR_MAX_RETRIES) {
                        error(0, "fakewap: failure receiving WTP Segm Ack, psn %u, giving up", psn);
                        goto send_post_exit;
                    }
                    warning(0, "fakewap: failure receiving WTP Segm Ack, psn %u, retrying", psn);
                    continue;
                }

                if (ttr) {
                    debug("fakewap", 0, "Received WTP_RESULT");
                } else {
                    debug("fakewap", 0, "Received WTP_ACK, psn %d", psn);
                }
                break;
            } /* while (retry) */
        } /* while (!ttr) */
    } /* if (transaction_mode==CONNECTION_ORIENTED) */

send_post_exit:
    octstr_destroy(postmsg);
    close(infd);
    return ret;
}

/*
**  Function (or thread) sets up a dgram socket.  Then it loops: WTL/WSP
**  Connect, Get a url and Disconnect until all requests are have been done.
*/
static void client_session( void * arg)
{
    int fd;
    int ret;
    int url_len = 0, url_off = 0;
    double nowsec, lastsec, tmp, sleepTime;
    long	uSleepTime;
    struct timeval now;
    struct timezone tz;
    char * url;
    unsigned char  sid[20];
    int            sid_len = 0;
    unsigned char  buf[64*1024];
    unsigned char reply_hdr[32];
    long timeout = WAP_MSG_RECEIVE_TIMEOUT;   /* wap gw is broken if no input */
    static unsigned short tid = 0;
    unsigned short old_tid;
    int tid_new = 0;
    int connection_retries = 0;
    int i_this;

    fd = udp_client_socket();
    if (fd == -1)
        panic(0, "fakewap: Couldn't create socket.");

    if (src_addr.sin_addr.s_addr!=INADDR_ANY || src_addr.sin_port!=0)
        if (bind(fd, (const struct sockaddr *)&src_addr, (int)sizeof(src_addr)) == -1)
            panic(0, "fakewap: Couldn't bind socket, errno %d", errno);

    /*
    **  Loop until all URLs have been requested
    */
    for (;;) {
        /*
         * Get start time of this request
         */
    	gettimeofday(&now, &tz);
    	lastsec = (double) now.tv_sec + now.tv_usec / 1e6;

        /*
         *  Get next transaction number or exit if too many transactions
         */
        i_this = get_next_transaction();
        if (max_send != MAX_SEND  && i_this > max_send) break;

        if (transaction_mode==TXN_MODE_CONNECTION_ORIENTED) {
            /*
             *  Connect, save sid from reply and finally ack the reply
             */
            old_tid = tid;
            tid = next_tid(old_tid);
            tid_new = (tid < old_tid);  /* Did we wrap? */

            WSP_Connect[3] = 2 + octstr_len(useragent); /* set header length */
            memcpy( buf, WSP_Connect, sizeof(WSP_Connect));
            memcpy( &buf[sizeof(WSP_Connect)], octstr_get_cstr(useragent), octstr_len(useragent) );
            ret = wap_msg_send( fd, WTP_Invoke_Cl2, sizeof(WTP_Invoke_Cl2), tid, tid_new,
                                buf, sizeof(WSP_Connect)+octstr_len(useragent), NULL, 0 );
            if (ret == -1) panic(0, "fakewap: Send WSP_Connect failed");
            
            CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WSP_ConnectReply, tid );
            ret = wap_msg_recv( fd, reply_hdr, sizeof(WSP_ConnectReply),
                                tid, buf, sizeof(buf), timeout, 0 );
            if (ret == -1) panic(0, "fakewap: Receive WSP_ConnectReply failed");

            if (ret > 2)
            {
                sid_len = ReadVarIntLen(&buf[4]);
                memcpy( sid, &buf[4], sid_len);
                debug("fakewap", 0, "Received WSP_ConnectReply, SessID %d", ReadVarIntVal(&buf[4]));
            }

            /*
            **  Send abort and continue if we get an unexpected reply
            */
            if (ret == 0)  {
                if (connection_retries++ > 3) {
                    panic(0, "fakewap: Cannot connect WAP GW!");
                }
                wap_msg_send( fd, WTP_Abort, sizeof(WTP_Abort), tid, tid_new, NULL, 0, NULL, 0 );
                continue;
            }
            else {
                connection_retries = 0;
            }
            ret = wap_msg_send( fd, WTP_Ack, sizeof(WTP_Ack), tid, tid_new, NULL, 0, NULL, 0 );

            if (ret == -1) panic(0, "fakewap: Send WTP_Ack failed");
        }

        /*
        **  Send WSP Get or Post for a given URL
        */
	old_tid = tid;
        tid = next_tid(old_tid);
	tid_new = (tid < old_tid);  /* Did we wrap? */
        url = choose_message(urls, num_urls);
        url_len = strlen(url);
        url_off = StoreVarInt( buf, url_len );
        memcpy( buf+url_off, url, url_len );
        buf[url_len+url_off] = 0x80; /* Accept: *\* */
        buf[url_len+url_off+1] = 0x80; /* Accept: *\* */

        /* send WSP Post if an input file is specified */
        if (infile)
        {
            ret = send_post(fd, tid, tid_new, url);
        }
        else /* send WSP Get */
        {
            if (transaction_mode==TXN_MODE_CONNECTION_LESS) {
                unsigned char wsphdr[32];
                wsphdr[0] = tid;
                memcpy(&wsphdr[1], WSP_Get, sizeof(WSP_Get));
                ret = wap_msg_send( fd, NULL, 0, tid, tid_new,
                                    wsphdr, sizeof(WSP_Get)+1, buf, url_len+url_off+2 );
            } else {
                ret = wap_msg_send( fd, WTP_Invoke_Cl2, sizeof(WTP_Invoke_Cl2), tid, tid_new,
                                    WSP_Get, sizeof(WSP_Get), buf, url_len+url_off );
            }
            if (ret == -1) break;
        }

        Octstr* wspreply = octstr_create("");
        if (transaction_mode==TXN_MODE_CONNECTION_LESS) {
            /* Connectionless - receive WSP Reply
             */
            reply_hdr[0] = tid;
            ret = wap_msg_recv( fd, reply_hdr, 1,
                                tid, buf, sizeof(buf), timeout, 0 );
            if (ret == -1) break;

            octstr_append_data(wspreply, (char*)&buf[1], ret-1);
        }
        else {
            /* Connection-Oriented - reassemble WSP Reply from WTP segments
             */
            int gtr = 0, ttr = 0, psn, gtr_psn = -1, ttr_psn = -1;
            unsigned char segment_data[256][1500];
            int segment_len[256];
            int data_offset;
            memset(segment_len, 0, sizeof(segment_len));
            while (!ttr)
            {
                CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WTP_Result, tid );
                ret = wap_msg_recv( fd, reply_hdr, sizeof(WTP_Result),
                                    tid, buf, sizeof(buf), timeout, 0 );
                timeout = WAP_MSG_RECEIVE_TIMEOUT;
                if (ret == -1) break;

                gtr = (buf[0] & 0x04) ? 1 : 0;
                ttr = (buf[0] & 0x02) ? 1 : 0;
                if (GET_WTP_PDU_TYPE(buf) == WTP_PDU_RESULT)
                {
                    psn = 0;
                    data_offset = 3;
                    debug("fakewap", 0, "Received WTP_RESULT pdu, gtr %d, ttr %d, payload len %d", gtr, ttr, ret - data_offset);
                }
                else /* segmented result */
                {
                    psn = buf[3];
                    data_offset = 4;
                    debug("fakewap", 0, "Received WTP_SEGM_RESULT pdu, psn %d, gtr %d, ttr %d, payload len %d", psn, gtr, ttr, ret - data_offset);
                }
                segment_len[psn] = ret - data_offset;
                if (segment_len[psn] > 1500)
                    panic(0, "fakewap: Segment %d exceeds 1500 bytes!?!", psn);
                memcpy(segment_data[psn], &buf[data_offset], segment_len[psn]);
                
                if (gtr || ttr || (psn < gtr_psn))
                {
                    if (gtr || ttr)
                        gtr_psn = psn;
                    if (ttr)
                        ttr_psn = psn;

                    /* check for lost segments and send NACK */
                    unsigned char lost_segs[256];
                    int i, num_lost = 0;
                    for (i = 0; i <= gtr_psn; i++)
                        if (!segment_len[i])
                        lost_segs[num_lost++] = i;
                    if (num_lost > 0)
                    {
                        /* send NACK since some segments got lost */
                        memcpy(buf, WTP_Nack, sizeof(WTP_Nack));
                        buf[3] = num_lost;
                        memcpy(buf+sizeof(WTP_Nack), lost_segs, num_lost);
                        debug("fakewap", 0, "Sending WTP_NACK pdu, num_lost %d, lost_seg0 %d", num_lost, lost_segs[0]);
                        ret = wap_msg_send( fd, buf, sizeof(WTP_Nack)+num_lost, tid, tid_new, NULL, 0, NULL, 0 );
                        ttr = 0;
                    }
                    else if (psn > 0)
                    {
                        /* send WTP Ack for a group of segments */
                        WTP_Segm_Ack[4] = gtr_psn;
                        debug("fakewap", 0, "Sending WTP_ACK pdu, gtr_psn %d", gtr_psn);
                        ret = wap_msg_send( fd, WTP_Segm_Ack, sizeof(WTP_Segm_Ack), tid, tid_new, NULL, 0, NULL, 0 );
                    }
                    else
                    {
                        /* send regular WTP Ack for first segment */
                        debug("fakewap", 0, "Sending WTP_ACK pdu, psn 0");
                        ret = wap_msg_send( fd, WTP_Ack, sizeof(WTP_Ack), tid, tid_new, NULL, 0, NULL, 0 );
                    }
                    if (ret == -1)
                        break;
                    if ((ttr_psn >= 0) && (num_lost==0)) {
                        /* received all segments */
                        ttr = 1;
                    }
                }
            }
            if (!ttr) {
                panic(0, "fakewap: Failed to receive entire message!?!");
                break;
            }

            for (psn = 0; psn <= gtr_psn; psn++)
                octstr_append_data(wspreply, (char*)segment_data[psn], segment_len[psn]);
        }
            
        /* validate WSP Reply
         */
        WSP_PDU* wsppdu = wsp_pdu_unpack(wspreply);
        if (!wsppdu)
            panic(0, "fakewap: Failed to unpack wsp message!?!");
        if (wsppdu->type != Reply) {
            error(0, "fakewap: Received WSP message type %u is not Reply!?!", octstr_get_char(wspreply, 0));
        }
        else
        {
            struct Reply* wspreply = &wsppdu->u.Reply;
            int status;
            if (wspreply->status <= 0x4f)
                status = (wspreply->status >> 4) * 100 + (wspreply->status & 0x0f);
            else if ((wspreply->status & 0xf0) == 0x50)
                status = 431 + (wspreply->status & 0x0f);
            else
                status = (wspreply->status >> 4) * 100 - 100 + (wspreply->status & 0x0f);
            if (status != 200) {
                warning(0, "fakewap: Warning - received reply with status %d", status);
            } else {
                info(0, "fakewap: Received WSP Reply with status code 200OK");
            }

            /* if output file arg was specified write the received wsp payload to file */
            if (outfile)
            {
                int outfd = creat(outfile, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
                if (outfd < 0) {
                    panic(0, "fakewap: failed to create output file %s, errno %d", outfile, errno);
                }

                ret = write(outfd, octstr_get_cstr(wspreply->data), octstr_len(wspreply->data));
                if (ret < 0) {
                    panic(0, "fakewap: failed to write to output file %s, errno %d", outfile, errno);
                }

                close(outfd);
                debug("fakewap", 0, "Wrote %d bytes of response payload to output file %s", ret, outfile);
            }
        }

        if (transaction_mode==TXN_MODE_CONNECTION_ORIENTED) {
            /*
            **  Finally disconnect with the sid returned by connect reply
            */
            {
                ret = wap_msg_send( fd, WTP_Invoke_Cl0, sizeof(WTP_Invoke_Cl0), tid, tid_new,
                                    WSP_Disconnect, sizeof(WSP_Disconnect), sid, sid_len );
                
                if (ret == -1) break;
            }
        }

        /*
        ** Get end time of the request
        */
        gettimeofday(&now, &tz);
        nowsec = (double) now.tv_sec + now.tv_usec / 1e6;
        tmp = nowsec - lastsec;	/* Duration of request */
        sleepTime = interval-tmp;	/* Amount of time left to sleep */
        uSleepTime = sleepTime * 1e6;

        mutex_lock( mutex );
        if (tmp < besttime) besttime = tmp;
        if (tmp > worsttime) worsttime = tmp;
        totaltime += tmp;
        mutex_unlock( mutex );

        if (brief == 1)
        {
            info(0, "fakewap: finished session # %d", i_this);
        }

        /*
        ** If we've done all the requests, then don't bother to sleep
        */
        if (i_this >= max_send) break;

        if (tmp < (double)interval) {
            usleep( uSleepTime );
        }
    }
    close(fd);

    /* The last end_time stays */
    mutex_lock( mutex );
    time(&end_time);
    mutex_unlock( mutex );
}


static void help(void) {
    info(0, "\n%s", usage);
}



/* The main program. */
int main(int argc, char **argv)
{
    int i, opt;
    double delta;
    int proto_version, tcl, tid_new;
#ifdef SunOS
    struct sigaction alrm;

    alrm.sa_handler = SIG_IGN;

    sigaction(SIGALRM,&alrm,NULL);
#endif

    gwlib_init();

    proto_version = 0;
    tcl = 2;
    tid_new = 0;

    hostname = octstr_create("localhost");

    /* restart args scanning */
    optind = 1;

    /* reset globals */
    interval = 1.0;
    port = 0;
    max_send = 1;
    tid_addition = 1;
    threads = 1;
    num_sent = 0;
    totaltime = 0;
    besttime = 1000000L;
    worsttime = 0;
    brief = 0;
    verbose = 0;
    test_separation = 0;
    infile = NULL;
    outfile = NULL;
    content_type = "text/plain";
    src_addr.sin_family = AF_INET;
    src_addr.sin_addr.s_addr = INADDR_ANY;
    src_addr.sin_port = 0;
    transaction_mode = TXN_MODE_CONNECTION_ORIENTED;
    packet_loss = 0;

    /* create default user agent header prepend with a9, and end with 0 */
    const char firstchar[] = {0xa9, 0}; /* code value for user agent header */
    Octstr* temp = octstr_create("fakewap");
    useragent = octstr_create(firstchar);
    octstr_append_data(useragent, octstr_get_cstr(temp), octstr_len(temp) );
    octstr_append_data(useragent, "\0", 1 );

    while ((opt = getopt(argc, argv, "Fhc:g:p:m:i:t:V:t:nsd:A:C:D:I:M:P:w:l:")) != EOF)
    {
	switch (opt) {
	case 'g':
	    octstr_destroy(hostname);
	    hostname = octstr_create(optarg);
	    break;

	case 'p':
	    port = atoi(optarg);
	    break;

	case 'm':
	    max_send = atoi(optarg);
	    break;

	case 'i':
	    interval = atof(optarg);
	    break;

	case 'c':
	    threads = atoi(optarg);
	    break;

	case 'V':
	    proto_version = atoi(optarg);
	    break;

	case 't':
	    tcl = atoi(optarg);
	    break;

	case 'n':
	    tid_new = 1;
	    break;

        case 's':
	    test_separation = 1;
            break;

	case 'd':
	    tid_addition = atoi(optarg);
	    break;

	case 'D':
	    brief = (atoi(optarg) >= 1);
	    verbose = (atoi(optarg) >= 2);
	    break;

	case 'h':
	    help();
	    exit(0);
	    break;

	case 'F':
	    nofailexit=1;
	    break;

        case 'w':
	    outfile = optarg;
            break;

	case 'A':
	    octstr_destroy(useragent);
            octstr_destroy(temp);
            /* create user agent header prepend with a9, and end with 0 */
	    useragent = octstr_create(firstchar);
	    temp = octstr_create(optarg);
    	    octstr_append_data(useragent, octstr_get_cstr(temp), octstr_len(temp) );
    	    octstr_append_data(useragent, "\0", 1 );
	    break;

	case 'C':
            if (!strcmp("mms", optarg))
                content_type = "application/vnd.wap.mms-message";
            else
                content_type = "text/plain";
	    break;

        case 'I':
        {
            unsigned int  byte0, byte1, byte2, byte3, srcport;
            int ret = sscanf(optarg, "%u.%u.%u.%u:%u", &byte0, &byte1, &byte2, &byte3, &srcport);
            if (ret!=4 && ret!=5)
                panic(0, "fakewap: invalid source address %s", optarg);
            src_addr.sin_addr.s_addr = htonl(byte0<<24 | byte1<<16 | byte2<<8 | byte3);
            if (ret==5)
                src_addr.sin_port = htons(srcport);
            break;
        }

	case 'M':
	    transaction_mode = atoi(optarg);
            if ((transaction_mode!=0) && (transaction_mode!=1)) {
                error(0, "fakewap: invalid transaction mode %s", optarg);
                return -1;
            }
	    break;

	case 'P':
	    infile = optarg;
	    break;

	case 'l':
	    packet_loss = atoi(optarg);
            if ((packet_loss < 0) && (packet_loss >=99)) {
                error(0, "fakewap: invalid packet loss rate %s, expect 0-99", optarg);
                return -1;
            }
	    break;

	case '?':
	default:
	    error(0, "fakewap: Unknown option %c", opt);
	    help();
	    panic(0, "fakewap: Stopping.");
	}
    }

    time(&start_time);

    if (optind >= argc)
        panic(0, "%s", usage);

    if ((!brief) && (!verbose))
    {
        log_set_output_level (GW_INFO);
    }

    if (port==0) {
        if (transaction_mode==TXN_MODE_CONNECTION_LESS)
            port = 9200;
        else
            port = 9201;
    }

    WTP_Invoke_Cl2[3] |= (proto_version&3)<<6;
    /* WTP_Invoke_Cl2[0] += (pdu_type&15)<<3; */
    WTP_Invoke_Cl2[3] |= tcl&3;
    WTP_Invoke_Cl2[3] |= (tid_new&1)<<5;

    gateway_addr = udp_create_address(hostname, port);

    urls = argv + optind;
    num_urls = argc - optind;

    srand((unsigned int) time(NULL));

    mutex = (Mutex*)mutex_create();

    info(0, "fakewap: starting");

    if (threads < 1) threads = 1;

    /*
    **  Start 'extra' client threads and finally execute the
    **  session of main thread
    */
    for (i = 1; i < threads; i++)
        gwthread_create(client_session, NULL);
    client_session(NULL);

    /* Wait for the other sessions to complete */
    gwthread_join_every(client_session);

    info(0, "fakewap: complete.");
    info(0, "fakewap: %d client threads made total %d transactions.", 
    	threads, num_sent);
    delta = difftime(end_time, start_time);
    info( 0, "fakewap: total running time %.1f seconds", delta);
    info( 0, "fakewap: %.1f messages/seconds on average", num_sent / delta);
    info( 0, "fakewap: time of best, worst and average transaction: "
             "%.1f s, %.1f s, %.1f s",
         besttime, worsttime, totaltime / num_sent );

    octstr_destroy(hostname);
    octstr_destroy(gateway_addr);
    mutex_destroy(mutex);

    gwlib_shutdown();
    return 0;
}
