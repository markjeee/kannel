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
 *        WSP: Get PDU (data: URL)
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

static char usage[] = "\
Usage: fakewap [options] url ...\n\
\n\
where options are:\n\
\n\
-h		help\n\
-v		verbose\n\
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
-w		Write/print received data (experimental)\n\
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

#include "gwlib/gwlib.h"

#define GET_WTP_PDU_TYPE(hdr)  (hdr[0] >> 3)
static int get_wtp_pdu_type(Octstr *hdr) {
	return octstr_get_char(hdr, 0) >> 3;
}

#define WTP_PDU_INVOKE  1
#define WTP_PDU_RESULT  2
#define WTP_PDU_ACK     3
#define WTP_PDU_ABORT   4

/*
**  Common parameters
*/
char **urls;
int num_urls;

Octstr *hostname = NULL;
Octstr *gateway_addr = NULL;
double interval = 1.0;
unsigned short port = 9201;
int max_send = 1;
unsigned short tid_addition = 1;
Mutex *mutex;
int threads = 1;
int num_sent = 0;
time_t start_time, end_time;
double totaltime = 0, besttime = 1000000L,  worsttime = 0;
int verbose = 0;
int nofailexit = 0;
int writedata = 0;
int test_separation = 0;

/*
 * PDU type, version number and transaction class are supplied by a 
 * command line argument. WSP_Concat is a concatenation of WTP_Ack and 
 * WSP_Disconnect PDUs.
 */
unsigned char WSP_Connect[] = {0x06, 0x00, 0x00, 0x00, 
				/* WSP part */
				0x01, /* PDU type */
				0x10, /* Version 1.0 */
				0x00, /* Capability length */
				0x02, /* Headers length = 2*/
				/* Capabilities */
				/* Headers */
				0x80, 0x80 /* Accept: *\* */
				};
unsigned char WSP_ConnectReply[] = {0x16, 0x80, 0x00, 0x02 };
unsigned char WTP_Ack[] =          {0x18, 0x00, 0x00 };
unsigned char WTP_TidVe[] =        {0x1C, 0x00, 0x00 };
unsigned char WTP_Abort[] =        {0x20, 0x00, 0x00, 0x00 };
unsigned char WSP_Get[] =          {0x0E, 0x00, 0x00, 0x02, 0x40 };
/* This used to also expect a content-type of 0x94, but that's too difficult
 * to check now that Kannel does full header encoding. */
unsigned char WSP_Reply[] =        {0x16, 0x80, 0x00, 0x04, 0x20 };
unsigned char WSP_Disconnect[] =   {0x0E, 0x00, 0x00, 0x00, 0x05 };
unsigned char WSP_Concat[] = {0x00, 0x03, 0x18, 0x00, 0x00, 0x05, 0x0E, 0x00, 0x00, 0x00, 0x05 };

/*
**  In this case it does not matter what is the byte order
*/
#define SET_GTR( hdr ) hdr[0] |= 0x04
#define SET_TID( hdr, tid) \
	hdr[1] |= (0x7f & ((tid) >> 8)); \
	hdr[2] = (char)(tid)
#define GET_TID( hdr ) (((hdr[1] & 0x7f) << 8) + hdr[2])
#define CONSTRUCT_EXPECTED_REPLY_HDR( dest, template, tid ) \
    if (sizeof(dest) < sizeof(template)) panic(0,"buffer overflow.");\
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
**  if -w option has been defined, function prints the trace message and
**  the first bytes in the message header
*/   
static void print_data( const char * trace, unsigned char * msg,
                int msg_len ) {
    int i;

    if (verbose || writedata) {
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
**  Function sends message to WAP GW
*/
static int
wap_msg_send( int fd, unsigned char * hdr,
            int hdr_len, unsigned short tid, int tid_new, unsigned char * data,
            int data_len )
{
    int ret;
    Octstr *datagram;

    datagram = octstr_create("");
    if (hdr != NULL)
    	octstr_append_data(datagram, hdr, hdr_len);

    set_tid(datagram, tid);
    if (get_wtp_pdu_type(datagram) == WTP_PDU_INVOKE) {
	/* request ack every time */
	int c;
	c = octstr_get_char(datagram, 3);
	octstr_set_char(datagram, 3, c | 0x10);
	if (tid_new)
		set_tid_new(datagram);
    }

    if (data != NULL)
	octstr_append_data(datagram, data, data_len);
    
#if 0
    debug("fakewap", 0, "Sending WDP datagram:");
    octstr_dump(datagram, 0);
#endif
    ret = udp_sendto(fd, datagram, gateway_addr);

    if (ret == -1) {
        error(0, "Sending to socket failed");
        return -1;
    }

    if (verbose) {
	debug("", 0, "Sent packet:");
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
wap_msg_recv( int fd, const char * hdr, int hdr_len,
              unsigned short tid, unsigned char * data, int data_len,
              int timeout )
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
        if (timeout != 0)
        {
	    ret = read_available(fd, timeout * 1000 * 1000);
	    if (ret <= 0) {
                info(0, "Timeout while receiving from socket.\n");
		if(nofailexit){
		    continue;
		}else{
		    return fResponderIsDead ? -1 : 0;
		}
		/* continue if we got ack? */
	    }
        }
	
	ret = udp_recvfrom(fd, &datagram, &dummy);
	if (ret == 0) {
		octstr_get_many_chars(msg, datagram, 0, octstr_len(datagram));
		msg_len = octstr_len(datagram);
	}
	octstr_destroy(datagram);
	octstr_destroy(dummy);

        if (ret == -1) {
            error(0, "recv() from socket failed");
            return -1;
        }

        if (hdr != NULL) {
            /*
            **  Ignore extra header bits, WAP GWs return different values
            */
            if (msg_len >= hdr_len &&
                GET_WTP_PDU_TYPE(msg) == GET_WTP_PDU_TYPE(hdr) &&
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
                wap_msg_send( fd, WTP_TidVe, sizeof(WTP_TidVe), tid, 0,
                              NULL, 0 );
            }
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_ABORT) {
                print_msg( "Received WTP Abort", msg, msg_len );
            }
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_RESULT) {
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
        data_len = min( data_len, msg_len - hdr_len );
        memcpy( data, msg+hdr_len, data_len);
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
    long timeout = 10;  /* wap gw is broken if no input */
    unsigned short tid = 0;
    unsigned short old_tid;
    int tid_new = 0;
    int connection_retries = 0;
    int i_this;

    fd = udp_client_socket();
    if (fd == -1)
        panic(0, "Couldn't create socket.");

    /*
    **  Loop until all URLs have been requested
    */
    for (;;) {
		/*
		 ** Get start time of this request
		 */
    	gettimeofday(&now, &tz);
    	lastsec = (double) now.tv_sec + now.tv_usec / 1e6;

        /*
        **  Get next transaction number or exit if too many transactions
        */
        i_this = get_next_transaction();
        if (max_send != MAX_SEND  && i_this > max_send) break;

        /*
        **  Connect, save sid from reply and finally ack the reply
        */
	old_tid = tid;
        tid = next_tid(old_tid);
	tid_new = (tid < old_tid);  /* Did we wrap? */
        ret = wap_msg_send( fd, WSP_Connect, sizeof(WSP_Connect),
                            tid, tid_new, NULL, 0 );

        if (ret == -1) panic(0, "Send WSP_Connect failed");

        CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WSP_ConnectReply, tid );
        ret = wap_msg_recv( fd, reply_hdr, sizeof(WSP_ConnectReply),
                            tid, buf, sizeof(buf), timeout );

        if (ret == -1) panic(0, "Receive WSP_ConnectReply failed");

        if (ret > 2)
        {
            sid_len = ReadVarIntLen(buf);
            memcpy( sid, buf, sid_len);
        }
        /*
        **  Send abort and continue if we get an unexpected reply
        */
        if (ret == 0)  {
            if (connection_retries++ > 3) {
                panic(0, "Cannot connect WAP GW!");
            }
            wap_msg_send( fd, WTP_Abort, sizeof(WTP_Abort), tid, tid_new,
                          NULL, 0 );
            continue;
        }
        else {
            connection_retries = 0;
        }
        ret = wap_msg_send( fd, WTP_Ack, sizeof(WTP_Ack), tid, tid_new,
                            NULL, 0 );

        if (ret == -1) panic(0, "Send WTP_Ack failed");

        /*
        **  Request WML page with the given URL
        */
	old_tid = tid;
        tid = next_tid(old_tid);
	tid_new = (tid < old_tid);  /* Did we wrap? */
        url = choose_message(urls, num_urls);
        url_len = strlen(url);
        url_off = StoreVarInt( buf, url_len );
        memcpy( buf+url_off, url, url_len );
        ret = wap_msg_send( fd, WSP_Get, sizeof(WSP_Get), tid, tid_new,
			    buf, url_len+url_off );
        if (ret == -1) break;

        CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WSP_Reply, tid );
        ret = wap_msg_recv( fd, reply_hdr, sizeof(WSP_Reply),
                            tid, buf, sizeof(buf), timeout );
        if (ret == -1) break;
        /*
	** If we are testing separation, we concatenate WTP_Ack and 
        ** WSP_Disconnect messages.
        */
        if (test_separation){
           ret = wap_msg_send(fd, WSP_Concat, sizeof(WSP_Concat), tid, tid_new,
			     sid, sid_len);
           
           if (ret == -1) break;
        } else {
           ret = wap_msg_send( fd, WTP_Ack, sizeof(WTP_Ack), tid, tid_new,
			    NULL, 0 );

           if (ret == -1) break;

        /*
        **  Finally disconnect with the sid returned by connect reply
        */
           ret = wap_msg_send( fd, WSP_Disconnect, sizeof(WSP_Disconnect),
		            tid, tid_new, sid, sid_len );

           if (ret == -1) break;
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

		if (verbose == 1)
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
    int proto_version, pdu_type, tcl, tid_new;
#ifdef SunOS
    struct sigaction alrm;

    alrm.sa_handler = SIG_IGN;

    sigaction(SIGALRM,&alrm,NULL);
#endif
    gwlib_init();

    proto_version = 0;
    pdu_type = 1;
    tcl = 2;
    tid_new = 0;

    hostname = octstr_create("localhost");

    while ((opt = getopt(argc, argv, "Fhvc:g:p:P:m:i:t:V:T:t:nsd:w")) != EOF) {
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

	case 'T':
	    pdu_type = atoi(optarg);
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

	case 'v':
	    verbose = 1;
	    break;
	    
	case 'h':
	    help();
	    exit(0);
	    break;

	case 'F':
	    nofailexit=1;
	    break;

        case 'w':
            writedata = 1;
            break;

	case '?':
	default:
	    error(0, "Unknown option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }

    time(&start_time);

    if (optind >= argc)
        panic(0, "%s", usage);

	if (verbose != 1)
	{
		log_set_output_level (GW_INFO);
	}
    WSP_Connect[3] += (proto_version&3)<<6;
    WSP_Connect[0] += (pdu_type&15)<<3;
    WSP_Connect[3] += tcl&3;
    WSP_Connect[3] += (tid_new&1)<<5;
    
    gateway_addr = udp_create_address(hostname, port);

    urls = argv + optind;
    num_urls = argc - optind;

    srand((unsigned int) time(NULL));

    mutex = mutex_create();

    info(0, "fakewap starting");

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

    info(0, "fakewap complete.");
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
