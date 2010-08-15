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

/* test_cimd2.c - fake cimd2 smsc
 *
 * This program pretends to be an CIMD 2 SMS center, accessible via IP.
 * It is used to test the Kannel smsc_cimd2 code.
 * 
 * Richard Braakman
 */

/* Note: The CIMD2 parsing code was written as a prototype, and currently
 * its main use is to exercise the *real* CIMD2 code in gw/smsc_cimd2.c.
 * Please don't use this code for anything real.
 * Richard Braakman */

/*
 * TODO: If log level is high and activity level is low, there will be
 * "SND" log entries for packets that are not sent, which is confusing
 * and should be fixed.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <limits.h>

#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gwlib/gwlib.h"

enum { TIMESTAMP_MAXLEN = 13 };

unsigned char *progname;

/* Set up a fake account for Kannel to use */
unsigned char *username = "foo";
unsigned char *password = "bar";

int port = 6789;

/* This can be useful to get past protocol-ID checks when testing spew. */
unsigned char *intro = "";

enum ACT {
	ACT_listen = 0,
	ACT_reply = 1,
	ACT_deliver = 2,
	ACT_flood = 3
};

enum SPEW {
	SPEW_nothing = 0,
	SPEW_binary = 1,
	SPEW_characters = 2,
	SPEW_packets = 3
};

enum LOG {
	LOG_nothing = 0,
	LOG_data = 1,
	LOG_packets = 2,
	LOG_sms = 3
};

enum CHK {
	CHK_nothing = 0,
	CHK_packets = 1,
	CHK_sums = 2,
	CHK_protocol = 3,
	CHK_sms = 4
};

int activity = ACT_listen;
int spew = SPEW_nothing;
int logging = LOG_nothing;
int checking = CHK_nothing;

int max_deliveries = -1;
int deliveries = 0;
time_t start_time = 0;

int sockfd = -1;

Octstr *inbuffer;
Octstr *outbuffer;
/* Maximum reasonable outbuffer size.  It can go above this, but we don't
 * deliberately add data when it's already more than this. */
enum { OUTBUFFER_LIMIT = 65536 };

/* Test dependencies on neatly-sized read and write chunks, by using
 * a deliberately evil buffer size.  1021 is the largest prime smaller
 * than 1024. */
enum { EVIL_BUFSIZE = 1021 };


enum CHARS {
	STX = 2,
	ETX = 3,
	TAB = 9,
	LF = 10,
	CR = 13
};

static void usage(FILE *out) {
	fprintf(out, "Usage: %s [options...]\n"
"  --help          Print this message\n"
"  --user USER     Allow clients to log in with username USER (default %s)\n"
"  --password PASS Allow clients to log in with password PASS (default %s)\n"
"  --intro INTRO   Send INTRO string before anything else (default nothing)\n"
"  --port PORT     TCP port to listen on (default %d)\n"
"  --activity ACT  Activity level of test server (default %d)\n"
"      ACT = 0     send nothing, just listen\n"
"      ACT = 1     send valid replies, do not initiate any transactions\n"
"      ACT = 2     attempt to deliver a random SMS every few seconds (NI)\n"
"      ACT = 3     deliver many random SMSes, measure throughput (NI)\n"
"  --spew SPEW     Flood client, overrides --activity (default %d)\n"
"      SPEW = 0    don't spew, use --activity instead\n"
"      SPEW = 1    spew random binary gunk at client\n"
"      SPEW = 2    spew random data of the right character set at client (NI)\n"
"      SPEW = 3    spew valid packets with random contents at client (NI)\n"
"  --logging LOG   Log level of test server (default %d)\n"
"      LOG = 0     log nothing\n"
"      LOG = 1     log all data\n"
"      LOG = 2     log summaries of valid packets\n"
"      LOG = 3     log successfully sent and received SMSes (NI)\n"
"  --checking CHK  Check level of test server (default %d)\n"
"      CHK = 0     check nothing\n"
"      CHK = 1     signal invalid packets (NI)\n"
"      CHK = 2     signal checksum errors (NI)\n"
"      CHK = 3     signal protocol errors (NI)\n"
"      CHK = 4     signal invalid SMS contents (NI)\n"
"  --max MAX       With high activity values, stop after MAX deliveries\n"
" NI means Not Implemented\n"
	, progname, username, password, port,
	activity, spew, logging, checking);
}

static void pretty_print(unsigned char *data, size_t length) {
	size_t i;
	int c;

	for (i = 0; i < length; i++) {
		c = data[i];
		switch(c) {
		default:
			if (isprint(c))
				putchar(c);
			else
				printf("<%d>", c);
			break;
		case TAB: fputs("<TAB>", stdout); break;
		case LF: fputs("<LF>\n", stdout); break;
		case CR: fputs("<CR>", stdout); break;
		case STX: fputs("<STX>", stdout); break;
		case ETX: fputs("<ETX>\n", stdout); break;
		}
	}
	fflush(stdout);
}

static void read_data(Octstr *in, int fd) {
	unsigned char buf[EVIL_BUFSIZE];
	int ret;

	ret = read(fd, buf, sizeof(buf));
	if (ret > 0) {
		octstr_append_data(in, buf, ret);
		if (logging == LOG_data)
			pretty_print(buf, ret);
	} else if (ret == 0) {
		fprintf(stderr, "Client closed socket\n");
		exit(0);
	} else {
		if (errno == EINTR || errno == EAGAIN)
			return;
		error(errno, "read_data");
		exit(1);
	}
}

static void write_data(Octstr *out, int fd) {
	unsigned char buf[EVIL_BUFSIZE];
	int len;
	ssize_t ret;
	
	len = sizeof(buf);
	if (len > octstr_len(out))
		len = octstr_len(out);
	if (len == 0)
		return;
	octstr_get_many_chars(buf, out, 0, len);
	ret = write(fd, buf, len);
	if (ret > 0) {
		if (logging == LOG_data)
			pretty_print(buf, ret);
		octstr_delete(out, 0, ret);
	} else if (ret == 0) {
		warning(0, "empty write");
	} else {
		if (errno == EINTR || errno == EAGAIN)
			return;
		error(errno, "write_data");
		exit(1);
	}
}

static void gen_message(Octstr *out);

/* Return the minimum interval (in microseconds) after which we will
 * want to be called again.  This value is only used if we _don't_
 * generate data this time through. */
static long gen_data(Octstr *out) {
	unsigned char buf[EVIL_BUFSIZE];
	size_t i;
	long interval = -1;
	static int last_sms;  /* Used by ACT_deliver */
	time_t now;

	if (max_deliveries < 0 || deliveries < max_deliveries) {
		switch (activity) {
		case ACT_deliver:
			now = time(NULL);
			if (last_sms == 0)
				last_sms = now;
			while (last_sms < now) {
				if (random() % 7 == 1) {
					gen_message(out);
					last_sms = now;
				} else
					last_sms++;
			}
			interval = 1000000;
			break;
		case ACT_flood:
			gen_message(out);
			break;
		}
	}

	switch (spew) {
	case SPEW_binary:
		for (i = 0; i < sizeof(buf); i++) {
			buf[i] = random() % 256;
		}
		octstr_append_data(out, buf, sizeof(buf));
		break;
	}

	return interval;
}

/******************************* CIMD 2 specific code ************************/

int awaiting_response = 0;

/* buf must be at least TIMESTAMP_MAXLEN bytes long. */
static void make_timestamp(unsigned char *buf, time_t fortime) {
	/* Is there a thread-safe version of gmtime? */
	struct tm tm = gw_gmtime(fortime);

	sprintf(buf, "%02d%02d%02d%02d%02d%02d",
		tm.tm_year % 100, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);
}
	

static void send_packet(Octstr *out, int opcode, int sequence, ...) {
	va_list ap;
	int parm;
	unsigned char *value;
	int checksum;
	int old_len, new_len;

	if (activity == ACT_listen)
		return;

	old_len = octstr_len(out);

	octstr_format_append(out, "%c%02d:%03d%c", STX, opcode, sequence, TAB);

	va_start(ap, sequence);
	for (parm = va_arg(ap, int); parm != 0; parm = va_arg(ap, int)) {
		value = va_arg(ap, unsigned char *);
		octstr_format_append(out, "%03d:%s\11", parm, value);
	}
	va_end(ap);

	/* Calculate checksum */
	checksum = 0;
	for (new_len = octstr_len(out); old_len < new_len; old_len++) {
		checksum = (checksum + octstr_get_char(out, old_len)) & 0xff;
	}

	octstr_format_append(out, "%02X%c", checksum, ETX);
}

static void send_error(Octstr *out, int opcode, int sequence,
			unsigned char *errorcode, unsigned char *errortext) {
	if (logging == LOG_packets)
		printf("SND: ERROR, %s\n", errortext);

	send_packet(out, opcode, sequence, 900, errorcode, 901, errortext, 0);
}

static int eat_char(Octstr *packet, int ch) {
	if (octstr_get_char(packet, 0) == ch) {
		octstr_delete(packet, 0, 1);
		return 0;
	}
	return -1;
}

static Octstr *eat_string_parm(Octstr *packet, int parm, int maxlen) {
	long start, datastart;
	long tab;
	Octstr *result;
	Octstr *parmheader;

    	parmheader = octstr_format("%c%03d:", TAB, parm);
	start = octstr_search(packet, parmheader, 0);
	if (start < 0) {
		octstr_destroy(parmheader);
		return NULL;
	}
	datastart = start + octstr_len(parmheader);

	tab = octstr_search_char(packet, TAB, datastart + 1);
	if (tab < 0) {
		tab = octstr_len(packet);
	}

	result = octstr_copy(packet, datastart, tab - datastart);
	octstr_delete(packet, start, tab - start);
	octstr_destroy(parmheader);
	return result;
}

static long eat_number(Octstr *ostr) {
	long result;
	long pos;

	pos = octstr_parse_long(&result, ostr, 0, 10);
	if (pos < 0)
		return INT_MIN;

	octstr_delete(ostr, 0, pos);
	return result;
}

static long eat_int_parm(Octstr *packet, int parm, int maxlen) {
	Octstr *value;
	long result;

	value = eat_string_parm(packet, parm, maxlen);
	if (!value)
		return INT_MIN;

	result = eat_number(value);
	if (octstr_len(value) > 0)
		result = INT_MIN;

	octstr_destroy(value);
	return result;
}

static void eat_checksum(Octstr *packet) {
	int len;
	int ch1, ch2, ch3;

	len = octstr_len(packet);

	if (len < 3)
		return;

	ch1 = octstr_get_char(packet, len - 3);
	ch2 = octstr_get_char(packet, len - 2);
	ch3 = octstr_get_char(packet, len - 1);
	
	if (isxdigit(ch3) && isxdigit(ch2) && ch1 == TAB)
		octstr_delete(packet, len - 3, 3);
}

static void handle_login(Octstr *packet, Octstr *out, int sequence) {
	Octstr *user = eat_string_parm(packet, 10, 32);
	Octstr *pass = eat_string_parm(packet, 11, 32);

	if (user == NULL)
		user = octstr_create("");
	if (pass == NULL)
		pass = octstr_create("");

	if (logging == LOG_packets)
		printf("RCV: Login user '%s', password '%s'\n",
			octstr_get_cstr(user), octstr_get_cstr(pass));

	if (octstr_str_compare(user, username) == 0 &&
	    octstr_str_compare(pass, password) == 0) {
		if (logging == LOG_packets)
			printf("SND: Login OK\n");
		send_packet(out, 51, sequence, 0);
	} else {
		send_error(out, 51, sequence, "100", "invalid login");
	}

	octstr_destroy(user);
	octstr_destroy(pass);
}

static void handle_logout(Octstr *packet, Octstr *out, int sequence) {
	if (logging == LOG_packets)
		printf("RCV: Logout\n");
	if (logging == LOG_packets)
		printf("SND: Logout OK\n");
	send_packet(out, 52, sequence, 0);
}

static void handle_submit(Octstr *packet, Octstr *out, int sequence) {
	Octstr *dest_addr = eat_string_parm(packet, 21, 20);
	Octstr *orig_addr = eat_string_parm(packet, 23, 20);
	long DCS = eat_int_parm(packet, 30, 3);
	Octstr *UDH = eat_string_parm(packet, 32, 280);
	Octstr *text = eat_string_parm(packet, 33, 480);
	Octstr *textb = eat_string_parm(packet, 34, 280);
	long valid_rel = eat_int_parm(packet, 50, 3);
	Octstr *valid_abs = eat_string_parm(packet, 51, 12);
	long proto_id = eat_int_parm(packet, 52, 3);
	long delivery_rel = eat_int_parm(packet, 53, 3);
	Octstr *delivery_abs = eat_string_parm(packet, 54, 12);
	long reply_path = eat_int_parm(packet, 55, 1);
	long SRR = eat_int_parm(packet, 56, 2);
	long cancel = eat_int_parm(packet, 58, 1);
	long tariff_class = eat_int_parm(packet, 64, 2);
	long service_desc = eat_int_parm(packet, 65, 1);
	long priority = eat_int_parm(packet, 67, 1);
	List *other_dests = gwlist_create();
	Octstr *tmp;

	while ((tmp = eat_string_parm(packet, 21, 20)))
		gwlist_append(other_dests, tmp);

	if (logging == LOG_packets) {
		int i;
		printf("RCV: Submit to %s", octstr_get_cstr(dest_addr));
		for (i = 0; i < gwlist_len(other_dests); i++) {
			printf(", %s",
				octstr_get_cstr(gwlist_get(other_dests, i)));
		}
		printf("\n");

		if (orig_addr)
			printf("    From: %s\n", octstr_get_cstr(orig_addr));
		if (DCS > INT_MIN)
			printf("    Data coding: %ld\n", DCS);
		if (UDH)
			printf("    User data header: %s\n",
				octstr_get_cstr(UDH));
		if (text)
			printf("    Text: %s\n", octstr_get_cstr(text));
		if (textb)
			printf("    Text (binary): %s\n",
				octstr_get_cstr(textb));
		if (valid_rel > INT_MIN)
			printf("    Validity period: %ld (relative)\n",
				valid_rel);
		if (valid_abs)
			printf("    Validity period: %s (absolute)\n",
				octstr_get_cstr(valid_abs));
		if (proto_id > INT_MIN)
			printf("    Protocol ID: %ld\n", proto_id);
		if (delivery_rel > INT_MIN)
			printf("    First delivery: %ld (relative)\n",
				delivery_rel);
		if (delivery_abs)
			printf("    First delivery: %s (absolute)\n",
				octstr_get_cstr(delivery_abs));
		if (reply_path == 0)
			printf("    Reply path disabled\n");
		else if (reply_path == 1)
			printf("    Reply path enabled\n");
		else if (reply_path > INT_MAX)
			printf("    Reply path: %ld\n", reply_path);
		if (SRR > INT_MAX)
			printf("    Status report flags: %ld\n", SRR);
		if (cancel == 0)
			printf("    Cancel disabled\n");
		else if (cancel == 1)
			printf("    Cancel enabled\n");
		else if (cancel > INT_MAX)
			printf("    Cancel enabled: %ld\n", cancel);
		if (tariff_class > INT_MAX)
			printf("    Tariff class: %ld\n", tariff_class);
		if (service_desc > INT_MAX)
			printf("    Service description: %ld\n", service_desc);
		if (priority > INT_MAX)
			printf("    Priority: %ld\n", priority);
	}

	if (!dest_addr) {
		send_error(out, 53, sequence, "300", "no destination");
	} else if (gwlist_len(other_dests) > 0) {
		send_error(out, 53, sequence, "301", "too many destinations");
	/* TODO: Report many other possible errors here */
	} else {
		unsigned char buf[TIMESTAMP_MAXLEN];

		make_timestamp(buf, time(NULL));
		if (logging == LOG_packets)
			printf("SND: Submit OK\n");
		send_packet(out, 53, sequence,
		            21, octstr_get_cstr(dest_addr),
			    60, buf,
			    0);
	}

	octstr_destroy(dest_addr);
	octstr_destroy(orig_addr);
	octstr_destroy(UDH);
	octstr_destroy(text);
	octstr_destroy(textb);
	octstr_destroy(valid_abs);
	octstr_destroy(delivery_abs);
	gwlist_destroy(other_dests, octstr_destroy_item);
}

static void handle_enquire(Octstr *packet, Octstr *out, int sequence) {
	Octstr *dest_addr = eat_string_parm(packet, 21, 20);
	Octstr *timestamp = eat_string_parm(packet, 60, 12);

	if (logging == LOG_packets)
		printf("RCV: Enquire status, dest='%s', time='%s'\n",
			dest_addr ? octstr_get_cstr(dest_addr) : "",
			timestamp ? octstr_get_cstr(timestamp) : "");

	if (!dest_addr) {
		send_error(out, 54, sequence, "400", "no destination");
	} else if (!timestamp) {
		send_error(out, 54, sequence, "401", "no timestamp");
	} else {
		if (logging == LOG_packets)
			printf("SND: Respond: status unknown\n");
		send_packet(out, 54, sequence,
			    21, octstr_get_cstr(dest_addr),
			    60, octstr_get_cstr(timestamp),
			    61, "0",
			    0);
	}
	octstr_destroy(dest_addr);
	octstr_destroy(timestamp);
}

static void handle_delivery_request(Octstr *packet, Octstr *out, int sequence) {
	long mode = eat_int_parm(packet, 68, 1);

	if (logging == LOG_packets) {
		switch (mode) {
		case 0: printf("RCV: Delivery request, messages waiting?\n");
			break;
		case 1: printf("RCV: Delivery request, one message\n");
			break;
		case 2: printf("RCV: Delivery request, all messages\n");
			break;
		case INT_MIN:
			printf("RCV: Delivery request, no mode\n");
			break;
		default:
			printf("RCV: Delivery request, mode %ld\n", mode);
		}
	}

	if (mode == INT_MIN)
		mode = 1;

	switch (mode) {
	case 0:
		if (logging == LOG_packets)
			printf("SND: Respond: 0 messages\n");
		send_packet(out, 55, sequence,
			    66, "0",
			    0);
		break;

	case 1:
		send_error(out, 55, sequence, "500", "no messages available");
		break;

	case 2:
		send_error(out, 55, sequence, "500", "no messages available");
		break;

	default:
		send_error(out, 55, sequence, "501", "bad mode");
		break;
	}
}

static void handle_cancel(Octstr *packet, Octstr *out, int sequence) {
	long mode = eat_int_parm(packet, 59, 1);
	Octstr *timestamp = eat_string_parm(packet, 60, 12);
	Octstr *destination = eat_string_parm(packet, 21, 20);

	if (logging == LOG_packets) {
		printf("RCV: Cancel");
		if (mode != INT_MIN)
			printf(", mode %ld", mode);
		if (destination)
			printf(", dest '%s'", octstr_get_cstr(destination));
		if (timestamp)
			printf(", time '%s'", octstr_get_cstr(timestamp));
		printf("\n");
	}

	if (mode < 0 || mode > 2)
		send_error(out, 56, sequence, "602", "bad mode");
	else {
		if (logging == LOG_packets)
			printf("SND: OK\n");
		send_packet(out, 56, sequence, 0);
	}
}


static void handle_set(Octstr *packet, Octstr *out, int sequence) {
	Octstr *pass = eat_string_parm(packet, 11, 32);

	if (pass) {
		if (logging == LOG_packets)
			printf("RCV: Set password to '%s'\n",
				octstr_get_cstr(pass));
		send_error(out, 58, sequence,
			"801", "changing password not allowed");
	} else {
		if (logging == LOG_packets)
			printf("RCV: Set, unknown parameters\n");
		send_error(out, 58, sequence, "3", "cannot set");
	}
}


static void handle_get(Octstr *packet, Octstr *out, int sequence) {
	long number = eat_int_parm(packet, 500, 3);

	if (logging == LOG_packets)
		printf("RCV: Get parameter #%ld\n", number);

	if (number == INT_MIN) {
		send_error(out, 59, sequence, "900", "missing parameter");
	} else if (number == 501) {
		unsigned char buf[TIMESTAMP_MAXLEN];
		make_timestamp(buf, time(NULL));
		if (logging == LOG_packets)
			printf("SND: OK, SMSC timestamp is '%s'\n", buf);
		send_packet(out, 59, sequence,
			    501, buf,
			    0);
	} else {
		send_error(out, 59, sequence, "900", "unknown parameter");
	}
}

static void handle_alive(Octstr *packet, Octstr *out, int sequence) {
	if (logging == LOG_packets)
		printf("RCV: Alive?\n");
	if (logging == LOG_packets)
		printf("SND: Alive.\n");
	send_packet(out, 90, sequence, 0);
}

static void handle_deliver_response(Octstr *packet, Octstr *out, int sequence) {
	awaiting_response = 0;
	if (logging == LOG_packets)
		printf("RCV: Deliver response\n");
	deliveries++;
	if (max_deliveries > 0 && deliveries == max_deliveries) {
		time_t elapsed = time(NULL) - start_time;
		printf("LOG: %ld deliveries in %ld seconds\n",
			(long) max_deliveries, (long) elapsed);
	}
	/* No need to respond to a response */
}

static void handle_deliver_status_report_response(Octstr *packet, Octstr *out, int sequence) {
	awaiting_response = 0;
	if (logging == LOG_packets)
		printf("RCV: Deliver status report response\n");
	/* No need to respond to a response */
}

static void handle_alive_response(Octstr *packet, Octstr *out, int sequence) {
	awaiting_response = 0;
	if (logging == LOG_packets)
		printf("RCV: Alive.\n");
	/* No need to respond to a response */
}

static void handle_nack(Octstr *packet, Octstr *out, int sequence) {
	awaiting_response = 0;
	if (logging == LOG_packets)
		printf("RCV: NACK\n");
	/* TODO: We should retransmit if we get a nack, but there's
	 * no record of what request we sent. */
}

typedef void (*packet_handler)(Octstr *, Octstr *, int);

struct {
	int opcode;
	packet_handler handler;
} handlers[] = {
	{ 1, handle_login },
	{ 2, handle_logout },
	{ 3, handle_submit },
	{ 4, handle_enquire },
	{ 5, handle_delivery_request },
	{ 6, handle_cancel },
	{ 8, handle_set },
	{ 9, handle_get },
	{ 40, handle_alive },
	{ 70, handle_deliver_response },
	{ 73, handle_deliver_status_report_response },
	{ 90, handle_alive_response },
	{ 99, handle_nack },
	{ -1, NULL },
};

static void parse_packet(Octstr *packet, Octstr *out) {
	int opcode, sequence;
	int i;

	eat_checksum(packet);

	opcode = eat_number(packet);
	if (opcode < 0 || eat_char(packet, ':') < 0)
		return;
	sequence = eat_number(packet);
	if (sequence < 0)
		return;

	for (i = 0; handlers[i].opcode >= 0; i++) {
		if (handlers[i].opcode == opcode) {
			(handlers[i].handler)(packet, out, sequence);
			break;
		}
	}

	if (handlers[i].opcode < 0) { /* Loop failed */
		if (logging == LOG_packets)
			printf("RCV: unknown operation %ld\n",
				(long) handlers[i].opcode);
		send_error(out, 98, sequence, "1", "unexpected operation");
	}
}

/* Parse the data stream for packets, and send out replies. */
static void parse_data(Octstr *in, Octstr *out) {
	int stx, etx;
	Octstr *packet;

	for (;;) {
		/* Look for start of packet.  Delete everything up to the start
		 * marker.  (CIMD2 section 3.1 says we can ignore any data
		 * transmitted between packets.) */
		stx = octstr_search_char(in, STX, 0);
		if (stx < 0)
			octstr_delete(in, 0, octstr_len(in));
		else if (stx > 0)
			octstr_delete(in, 0, stx);

		etx = octstr_search_char(in, ETX, 0);
		if (etx < 0)
			return;  /* Incomplete packet; wait for more data. */

		/* Copy the data between stx and etx */
		packet = octstr_copy(in, 1, etx - 1);
		/* Then cut the packet (including stx and etx) from inbuffer */
		octstr_delete(in, 0, etx + 1);

		parse_packet(packet, out);

		octstr_destroy(packet);
	}
}

static void random_address(unsigned char *buf, int size) {
	int len = random() % size;

	while (len--) {
		*buf++ = '0' + random() % 10;
	}

	*buf++ = '\0';
}

static void random_message(unsigned char *buf, int size) {
	int len = random() % size;

	while (len--) {
		do {
			*buf = random() % 256;
		} while (*buf == STX || *buf == ETX || *buf == TAB);
		buf++;
	}

	*buf++ = '\0';
}

static void random_hex(unsigned char *buf, int size) {
	int len = random() % size;

	/* Make even */
	len -= (len % 2);

	while (len--) {
		int c = random() % 16;
		if (c < 10)
			*buf++ = c + '0';
		else
			*buf++ = c - 10 + 'a';
	}

	*buf++ = '\0';
}

static void gen_message(Octstr *out) {
	static int send_seq = 0;
	unsigned char dest[21];
	unsigned char orig[21];
	unsigned char scts[TIMESTAMP_MAXLEN];
	unsigned char message[481];
	unsigned char udh[281];

	if (awaiting_response == 1)
		return;

	random_address(dest, sizeof(dest));
	random_address(orig, sizeof(orig));
	make_timestamp(scts, time(NULL));
	random_message(message, sizeof(message));
	if (random() % 2 == 0)
		random_hex(udh, sizeof(udh));
	else
		*udh = 0;

	if (logging == LOG_packets)
		printf("SND: Deliver message (random)\n");

	if (*udh) {
		send_packet(out, 20, send_seq,
			21, dest,
			23, orig,
			60, scts,
			32, udh,
			33, message,
			0);
	} else {
		send_packet(out, 20, send_seq,
			21, dest,
			23, orig,
			60, scts,
			33, message,
			0);
	}

	send_seq += 2;
	if (send_seq > 255)
		send_seq = 0;

	awaiting_response = 1;
}

/************************** CIMD 2 specific code ends ************************/

static void main_loop(void) {
	fd_set readfds, writefds;
	int n;
	static int reported_outfull = 0;
	int interval = -1;

	inbuffer = octstr_create("");
	outbuffer = octstr_create(intro);
	start_time = time(NULL);

	for (;;) {
		if (octstr_len(outbuffer) < OUTBUFFER_LIMIT) {
			interval = gen_data(outbuffer);
		} else if (!reported_outfull) {
			warning(0, "outbuffer getting full; waiting...");
			reported_outfull = 1;
		}

		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);

		if (octstr_len(outbuffer) > 0) {
			FD_ZERO(&writefds);
			FD_SET(sockfd, &writefds);
			n = select(sockfd+1, &readfds, &writefds, NULL, NULL);
		} else {
			struct timeval tv;
			struct timeval *tvp;

			if (interval >= 0) {
				tv.tv_sec = 0;
				tv.tv_usec = interval;
				tvp = &tv;
			} else {
				tvp = NULL;
			}
			n = select(sockfd+1, &readfds, NULL, NULL, tvp);
		}

		if (n < 0) {
			if (errno == EINTR) {
				warning(errno, "main loop, select");
				continue;
			}
			error(errno, "main loop, select");
			sleep(1);
			continue;
		}
		if (n > 0) {
			if (FD_ISSET(sockfd, &readfds)) {
				read_data(inbuffer, sockfd);
				parse_data(inbuffer, outbuffer);
			}
			if (octstr_len(outbuffer) > 0 &&
			    FD_ISSET(sockfd, &writefds)) {
				write_data(outbuffer, sockfd);
			}
			if (octstr_len(outbuffer) < OUTBUFFER_LIMIT) {
				reported_outfull = 0;
			}
		}
	}
}

static struct {
	unsigned char *option;
	void *location;
	int number;
} options[] = {
	{ "--user", &username, 0 },
	{ "--password", &password, 0 },
	{ "--port", &port, 1 },
	{ "--intro", &intro, 0 },
	{ "--activity", &activity, 1 },
	{ "--spew", &spew, 1 },
	{ "--logging", &logging, 1 },
	{ "--checking", &checking, 1 },
	{ "--max", &max_deliveries, 1 },
	{ NULL, NULL, 0 },
};

static int wait_for_client(int port) {
	struct sockaddr_in sin;
	socklen_t addrlen;
	int listenfd;
	int clientfd;
	Octstr *addr;

	listenfd = make_server_socket(port, NULL);
	if (listenfd < 0) {
		fprintf(stderr, "%s: failed to open socket at port %d\n",
			progname, port);
		exit(1);
	}

	do {
		addrlen = sizeof(sin);
		clientfd = accept(listenfd, (struct sockaddr *)&sin, &addrlen);
		if (clientfd < 0) {
			error(errno, "failed to accept new connection");
		}
	} while (clientfd < 0);

	if (socket_set_blocking(clientfd, 0) < 0) {
		panic(0, "failed to make client socket nonblocking");
	}

    	addr = gw_netaddr_to_octstr(AF_INET, &sin.sin_addr);
	info(0, "Accepted client from %s:%d",
		octstr_get_cstr(addr), ntohs(sin.sin_port));
    	octstr_destroy(addr);

	close(listenfd);

	return clientfd;
}
	

int main(int argc, char *argv[]) {
	int i;
	int opt;

	gwlib_init();

	progname = argv[0];
	srandom(0);  /* Make "random" data reproducible */

	for (i = 1; i < argc; i++) {
		for (opt = 0; options[opt].option; opt++) {
			if (strcmp(argv[i], options[opt].option) == 0) {
				if (i + 1 >= argc) {
					fprintf(stderr, "%s: missing argument to %s",
						progname, argv[i]);
					exit(2);
				}
				if (options[opt].number) {
					* (int *) options[opt].location = atoi(argv[i+1]);
				} else {
					* (char **) options[opt].location = argv[i+1];
				}
				i++;
				break;
			}
		}
		if (options[opt].option)
			continue;
		if (strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			exit(0);
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "%s: unknown option %s\n",
				progname, argv[i]);
			usage(stderr);
			exit(2);
		}
	}

	sockfd = wait_for_client(port);
	main_loop();
	return 0;
}
