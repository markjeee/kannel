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
 * smsc_ois.c - Sema Group SMS2000 (G6.0) Center (OIS 5.0).
 * Jouko Koski (EDS) for WapIT Ltd.
 *
 * The SMS2000 has a general X.25 access gateway for accessing the SMSC,
 * as described in the Open Interface Specification 5.0 document.
 * A protocol translator - like the Cisco 2501 router - hides all the
 * X.25 trickery from us. We just connect to a preconfigured router
 * address/port, and the translator forwards the connection to the SMS2000.
 * Correspondingly, if the SMSC has something to say, it looks like
 * the router were contacting our port. The router should be configured so,
 * that it has a pre-defined address and tcp port in X.25 automode establishing
 * a X.25 link and a similar configuration in X.25 side connecting to a pre-
 * defined address and port, it shall not encapsulate everything in Telnet
 * (set the stream mode), and it should suppress banner messages like "Trying
 * 9876...Open" (set the quiet mode).
 *
 * Whenever possible, I've tried to steal ideas and code from other smsc_*
 * files, particularly from Hao Shi's (EDS) original implementation for a
 * serial-line-connected PAD. However, the code is highly evolutionary,
 * because during the implementation new technical details kept popping
 * up all the time (initially, PAD commands were supposed to be used,
 * but the router was configured to "automode", so they weren't necessary;
 * instead the router gave banner messages and wanted some telnet negotiations;
 * the router insisted echoing everything and delayed <nul>s after <cr>s;
 * telnet transmit-binary mode solved that; then the stream mode (no telnet
 * encapsulation) was discovered; suddenly the banners were turned off also;
 * but still the smsc didn't deliver mo messages, because it wanted to
 * connect instead of using our existing connection; then we began to use
 * short connection sessions for transmitting instead of a single ever-
 * lasting connection, and also started to specifically listen for smsc
 * initiated connections, which yielded two separate input buffers; then
 * suddenly the banners were there again, so some intelligence had to be
 * added to adapt their (non-)existence; then revealed the spec version 4.5
 * had been obsolete all the time and we got 5.0; the router apparently
 * caused some extra packets on the x.25 side and everybody was blaming the
 * application; then the connection maintenance and buffering was again
 * revisited to achieve better performance and reliability... Really an
 * interesting story but think if it were about you instead of me :-)
 *
 * Really funny thing is that according to the spec the SMS2000 does have
 * a direct TCP/IP access interface. However, here we have the general X.25
 * access interface, since we started with the old spec and probably the
 * simpler TCP/IP access is not available in our particular customer's
 * installation, not at least when this was written. In the direct access
 * only single ever-lasting connection is necessary, and the messages are
 * the same but their format is different. Encoding tricks are the same.
 * So, if you are implementing that access mode some day, there are probably
 * differences between this access mode and yours on so many levels, that
 * simple if () selections won't work; write your own code from (nearly)
 * scratch and take appropriate encoding conversion functions here. Or do
 * just whatever you want, what should I care :-).
 */

#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "smsc.h"
#include "smsc_p.h"
#include "gwlib/gwlib.h"
#include "sms.h"

/* XXX Delete me and replace dcs with dcs_to_fields */
enum dcs_body_type {
    DCS_GSM_TEXT = 0,
    DCS_OCTET_DATA = 4    /* flag_8bit */
};


/* 'private:' */

int ois_debug_level = 0; /* some extra verbosity in debug logging */
/* 0=just normal debugging, 1=input/output messages, 2=function entries, */
/* 3=message assembly/disassembly, 4=disconnection tracing, */
/* 5=message conversions, and 8=message polling (=too much) */

#define SAY(d,s) { if (d<=ois_debug_level) debug("bb.sms.ois",0,s); }
#define SAY2(d,s,t) { if (d<=ois_debug_level) debug("bb.sms.ois",0,s,t); }
#define SAY3(d,s,t,u) { if (d<=ois_debug_level) debug("bb.sms.ois",0,s,t,u); }
#define IOTRACE(x,s,l) SAY3(1,"%s [%s]",x,ois_debug_str(s,l))


#define BUFLEN (511) /* sure enough for ois messages */
#define OIS_OPEN_WAITTIME (15) /* seconds, waiting for banners */
#define OIS_MESSAGE_WAITTIME (30) /* seconds, until closing idle connection */
#define OIS_WAITTIME (999999) /* microseconds, waiting for banners at a time */
#define OIS_NOWAIT (0) /* microseconds, not waiting */
#define MAXCOUNTER (10000) /* ois message id */
#define EOL ('\r') /* ois definition for the eol */

typedef struct ois_listentry {
    struct ois_listentry *next;
    Msg *msg;
} ois_listentry;

#define OIS_FLAG_DEBUG (0x000f)
#define OIS_FLAG_ERROR (0x0100)
#define OIS_FLAG_NOBANNER (0x0200)
#define OIS_FLAG_MULTIPLE_CALL (0x0400)
#define OIS_FLAG_CLOSED (0x0800)

static int ois_counter = 0; /* [0..MAXCOUNTER), ois "unique" message id */
static int ois_open_listener(SMSCenter *smsc);
static int ois_open_sender(SMSCenter *smsc);
static int ois_open_receiver(SMSCenter *smsc);
static void ois_disconnect_all(SMSCenter *smsc);
static void ois_disconnect(SMSCenter *smsc);
static int ois_read_into_buffer(SMSCenter *smsc, long wait_usec);
static int ois_check_input(SMSCenter *smsc, long wait_usec);
static int ois_check_incoming(SMSCenter *smsc, long wait_usec);
static void ois_append_to_list(ois_listentry **head, Msg *msg);
static int ois_int_to_i4(char *raw, int nbr);
static int ois_increment_counter(void);
static int ois_submit_sm_invoke(SMSCenter *smsc, const Msg *msg);
static int ois_encode_submit_sm_invoke(char *str, const Msg *msg);
static int ois_append_msisdn(char *raw, const Msg *msg);
static int ois_append_sme_reference_number(char *raw);
static int ois_append_priority(char *raw);
static int ois_append_originating_address(char *raw);
static int ois_append_validity_period(char *raw);
static int ois_append_data_coding_scheme(char *raw, const Msg *msg);
static int ois_append_status_report_request(char *raw);
static int ois_append_protocol_id(char *raw);
static int ois_append_submission_options(char *raw, const Msg *msg);
static int ois_append_sm_text(char *raw, const Msg *msg);
static int ois_submit_sm_result(SMSCenter *smsc, const char *buffer);
static int ois_decode_submit_sm_result(int *code, const char *str);
static int ois_deliver_sm_invoke(SMSCenter *smsc, const char *buffer);
static int ois_decode_deliver_sm_invoke(Msg *msg, const char *str);
static int ois_check_deliver_sm_invoke(const char *str);
static int ois_adjust_destination_address(Msg *msg, const char *raw);
static int ois_ignore_smsc_reference_number(const char *raw);
static int ois_adjust_originating_address(Msg *msg, const char *raw);
static int ois_adjust_data_coding_scheme(Msg *msg, const char *raw);
static int ois_ignore_protocol_id(const char *raw);
static int ois_adjust_additional_information(Msg *msg, const char *raw);
static int ois_adjust_sm_text(Msg *msg, const char *raw);
static int ois_ignore_time(const char *raw);
static int ois_deliver_sm_result(SMSCenter *smsc, int result, const char *str);
static int ois_encode_deliver_sm_result(char *str, int result);
static int ois_expand_gsm7(char *raw8, const char *raw7, int len);
static int ois_expand_gsm7_to_bits(char *bits, const char *raw7, int len);
static char ois_expand_gsm7_from_bits(const char *bits, int pos);
static int ois_convert_to_ia5(char *str, const char *raw, int len);
static int ois_convert_from_ia5(char *raw, const char *str);
static int ois_convert_to_iso88591(char *raw, int len);
static int ois_extract_msg_from_buffer(char *str, SMSCenter *smsc);
static int ois_extract_line_from_buffer(char *str, SMSCenter *smsc);
static void ois_swap_buffering(SMSCenter *smsc);
static const char *ois_debug_str(const char *raw, int len);

/* 'public:' */

/*
 * Establish a connection to the SMSC.
 */

SMSCenter *ois_open(int receiveport, const char *hostname, int port, int debug_level)
{
    SMSCenter *smsc;
    int ret;

    ois_debug_level = debug_level & OIS_FLAG_DEBUG;
    SAY(2, "ois_open");

    /* create a SMSCenter structure */

    smsc = smscenter_construct();
    if (smsc == NULL) {
	goto error;
    }

    smsc->type = SMSC_TYPE_OIS;
    smsc->receive_port = receiveport;
    smsc->hostname = gw_strdup(hostname);
    smsc->port = port;
    smsc->ois_flags = ois_debug_level;

    ret = ois_open_listener(smsc);
    if (ret < 0) {
	goto error;
    }
    sprintf(smsc->name, "OIS:TCP/X.25-Translator:localhost:%d:TCP:%.512s:%d",
	    smsc->receive_port, smsc->hostname, smsc->port);

    return smsc;

 error:
    error(0, "ois_open: could not open");
    smscenter_destruct(smsc);
    return NULL;
}


/*
 * Terminate the SMSC connection.
 */

int ois_close(SMSCenter *smsc)
{
    ois_debug_level = smsc->ois_flags & OIS_FLAG_DEBUG;
    SAY(2, "ois_close");

    if (smsc->type != SMSC_TYPE_OIS) {
	warning(0, "ois_close: closing a not-ois connection...");
    }

    ois_swap_buffering(smsc);
    smscenter_remove_from_buffer(smsc, smsc->buflen);
    ois_swap_buffering(smsc);
    smscenter_remove_from_buffer(smsc, smsc->buflen);
    SAY(4, "ois_close: ois_disconnect_all");
    ois_disconnect_all(smsc);

    return 0;
}


/*
 * Re-establish a SMSC connection.
 */

int ois_reopen(SMSCenter *smsc)
{
    int ret;

    ois_debug_level = smsc->ois_flags & OIS_FLAG_DEBUG;
    SAY(2, "ois_reopen");

    ois_close(smsc);

    if (smsc->type == SMSC_TYPE_OIS) {
	ret = ois_open_listener(smsc);
	if (ret < 0) {
	    goto error;
	}
    } else {
	error(0, "ois_reopen: wrong smsc type");
	goto error;
    }
    return 0;

 error:
    error(0, "ois_reopen: could not open");
    return -1;
}


/*
 * Check for MO messages.
 * Put all incoming MO messages into an internal queue.
 */

int ois_pending_smsmessage(SMSCenter *smsc)
{
    int ret;

    ois_debug_level = smsc->ois_flags & OIS_FLAG_DEBUG;
    SAY(8, "ois_pending_smsmessage");

    ret = ois_check_incoming(smsc, OIS_NOWAIT);
    if (ret == 0 && smsc->socket != -1) {
	ret = ois_check_input(smsc, OIS_NOWAIT);
    }
    if (ret == 0 && smsc->ois_socket != -1) {
	ois_swap_buffering(smsc);
	ret = ois_check_input(smsc, OIS_NOWAIT);
	ois_swap_buffering(smsc);
	if (smsc->ois_socket == -1 && smsc->ois_ack_debt != 0) {
	    warning(0, "ois_pending_smsmessage: missing %d ack(s)...",
		    smsc->ois_ack_debt);
	}
    }
    return ret;
}


/*
 * Send a MT message.
 */

int ois_submit_msg(SMSCenter *smsc, const Msg *msg)
{
    int ret;

    ois_debug_level = smsc->ois_flags & OIS_FLAG_DEBUG;
    SAY(2, "ois_submit_msg");
    ois_swap_buffering(smsc);

    if (msg_type((Msg *)msg) != sms) {
	error(0, "ois_submit_msg: can not handle message types other than smart_msg");
	goto error;
    }

    if (smsc->socket == -1) {
	ret = ois_open_sender(smsc);
	if (ret < 0) {
	    goto error;
	}
    }

    ret = ois_submit_sm_invoke(smsc, msg);
    if (ret < 0) {
	goto error_close;
    }

    ++smsc->ois_ack_debt;
    time(&smsc->ois_alive);
    ret = 0;
    goto out;

 error_close:
    if (smsc->ois_ack_debt != 0) {
	warning(0, "ois_submit_msg: missing %d ack(s)...",
		smsc->ois_ack_debt);
    }
    SAY(4, "ois_submit_msg: ois_disconnect in error_close");
    ois_disconnect(smsc);
 error:
    SAY(2, "ois_submit_msg error");
    ret = -1;
 out:
    ois_swap_buffering(smsc);
    return ret;
}


/*
 * Receive a MO message (from the internal queue).
 */

int ois_receive_msg(SMSCenter *smsc, Msg **msg)
{
    ois_listentry *item;

    ois_debug_level = smsc->ois_flags & OIS_FLAG_DEBUG;
    SAY(2, "ois_receive_msg");

    item = smsc->ois_received_mo;
    if (item == NULL) { /* no mo messages */
	if ((smsc->ois_flags & OIS_FLAG_ERROR) == 0) {
	    return 0;   /* should actually not happen */
	} else {
	    return -1;  /* error pending, reopen? */
	}
    } else {            /* we have a message waiting */
	smsc->ois_received_mo = item->next;
	*msg = item->msg;
	gw_free(item);
	return 1;       /* got the message */
    }
}


/*
 * Destruct the internal queue.
 */

void ois_delete_queue(SMSCenter *smsc)
{
    Msg *msg;

    ois_debug_level = smsc->ois_flags & OIS_FLAG_DEBUG;
    SAY(2, "ois_delete_queue");

    while (ois_receive_msg(smsc, &msg) > 0) {
	gw_free(msg);
    }
    return;
}








/*
 * Implementation of 'private:'
 */


static int ois_open_listener(SMSCenter *smsc)
{
    SAY(2, "ois_open_listener");

    smsc->ois_listening_socket = make_server_socket(smsc->receive_port, 
		    NULL);
	/* XXX add interface_name if required */
    if (smsc->ois_listening_socket < 0) {
	goto error;
    }
    if (socket_set_blocking(smsc->ois_listening_socket, 0) < 0) {
	ois_close(smsc);
	goto error;
    }
    smsc->ois_flags &= ~OIS_FLAG_ERROR;
    smsc->ois_flags &= ~OIS_FLAG_NOBANNER;
    smsc->ois_alive2 = time(&smsc->ois_alive);

    SAY2(2, "ois_open_listener fd=%d", smsc->ois_listening_socket);
    return 0;

 error:
    error(0, "ois_open_listener: failed to open listening socket");
    return -1;
}


static int ois_open_sender(SMSCenter *smsc)
{
    int ret;
    char buffer[BUFLEN+1];
    time_t now;
    time_t beginning;
    
    SAY(2, "ois_open_sender");
    debug("bb.sms.ois", 0, "connecting to host %s port %d",
	  smsc->hostname, smsc->port);

    time(&beginning);
    smsc->socket = tcpip_connect_to_server(smsc->hostname, smsc->port,
		    NULL);
	/* XXX add interface_name if required */
    if (smsc->socket < 0) {
	return -1;
    } else {
	smsc->buflen = 0;
	time(&smsc->ois_alive);
	smsc->ois_ack_debt = 0;
    }

    SAY2(2, "ois_open_sender fd=%d", smsc->socket);
    if (smsc->ois_flags & OIS_FLAG_NOBANNER) {
	return 0;
    }

    buffer[0] = '\0';
    for (time(&now); (now - beginning) < OIS_OPEN_WAITTIME; time(&now)) {
	ret = ois_read_into_buffer(smsc, OIS_WAITTIME);
	if (ret < 0) {
	    goto error;
	}

	if (smsc->buflen == 0) {
	    /* assume that the router is in the quiet mode */
	    /* there will be no banners */
	    smsc->ois_flags |= OIS_FLAG_NOBANNER;
	    debug("bb.sms.ois", 0, "assuming that %s:%d is in the quiet mode",
		  smsc->hostname, smsc->port);
	    return 0;
	}
	ret = ois_extract_line_from_buffer(buffer, smsc);
	if (ret > 0) {
	    if (strncmp(buffer, "Trying", 6) == 0 &&
		strstr(buffer, "...Open\r\n") != NULL) {
		time(&smsc->ois_alive);
		return 0;
	    } else {
		break;
	    }
	}
    }

 error:
    SAY(4, "ois_open_sender: ois_disconnect in error");
    ois_disconnect(smsc);
    error(0, "ois_open_sender: failed to connect [%s%s]",
	  buffer, ois_debug_str(smsc->buffer, smsc->buflen));
    return -1;
}


static int ois_open_receiver(SMSCenter *smsc)
{
    struct sockaddr_in addr;
    int addrlen;
    Octstr *os;

    SAY(2, "ois_open_receiver");

    /* the listening socket should be non-blocking... */

    addrlen = sizeof(addr);
    smsc->socket = accept(smsc->ois_listening_socket,
			  (struct sockaddr *)&addr, (socklen_t *)&addrlen);
    if (smsc->socket == -1) {
	if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
	    /* || errno == ECONNABORTED || errno == EPROTO) -Kalle 6.7 */
	{
	    return 0;
	} else {
	    error(errno, "ois_open_receiver: accept failed");
	    smsc->ois_flags |= OIS_FLAG_ERROR;
	    return -1;
	}
    }

    SAY2(2, "ois_open_receiver fd=%d", smsc->socket);
    os = gw_netaddr_to_octstr(AF_INET, &addr.sin_addr);
    debug("bb.sms.ois", 0, "connection from host %s port %hu",
	  octstr_get_cstr(os), ntohs(addr.sin_port));
    octstr_destroy(os);
    time(&smsc->ois_alive);
    return 0;
}


static void ois_disconnect_all(SMSCenter *smsc)
{
    SAY2(2, "ois_disconnect_all fd=%d", smsc->ois_listening_socket);

    ois_swap_buffering(smsc);
    SAY(4, "ois_disconnect_all: ois_disconnect");
    ois_disconnect(smsc); /* smsc->socket */
    ois_swap_buffering(smsc);
    SAY(4, "ois_disconnect_all: ois_disconnect");
    ois_disconnect(smsc); /* smsc->socket */

    if (smsc->ois_listening_socket != -1) {
	if (close(smsc->ois_listening_socket) == -1) {
	    warning(errno, "ois_disconnect_all: close failed...");
	}
	smsc->ois_listening_socket = -1;
    }
    return;
}


static void ois_disconnect(SMSCenter *smsc)
{
    SAY2(2, "ois_disconnect fd=%d", smsc->socket);

    if (smsc->socket != -1) {
	if (close(smsc->socket) == -1) {
	    warning(errno, "ois_disconnect: close failed...");
	}
	smsc->socket = -1;
    }
    return;
}


static int ois_read_into_buffer(SMSCenter *smsc, long wait_usec)
{
    int ret;

    SAY(8, "ois_read_into_buffer");

    if (smsc->socket == -1) {
	if ((smsc->ois_flags & OIS_FLAG_CLOSED) == 0) {
	    debug("bb.sms.ois", 0, "attempting to read from a closed socket");
	    smsc->ois_flags |= OIS_FLAG_CLOSED;
	}
	return 0;
    } else {
	smsc->ois_flags &= ~OIS_FLAG_CLOSED;
    }

    ret = read_available(smsc->socket, wait_usec);
    if (ret > 0) {
	time(&smsc->ois_alive);
	ret = smscenter_read_into_buffer(smsc);
	if (ret > 0 || (ret == 0 && smsc->buflen > 0)) {
	    SAY(2, "ois_read_into_buffer got something");
	} else if (ret == 0) {
	    if (smsc->buflen > 0) {
		SAY(2, "ois_read_into_buffer has something");
		ret = 1;
	    }
	    SAY(4, "ois_read_into_buffer: ois_disconnect");
	    ois_disconnect(smsc);
	}
    }
    return ret;
}


static int ois_check_input(SMSCenter *smsc, long wait_usec)
{
    char buffer[BUFLEN+1];
    time_t now;
    int ret;

    SAY(8, "ois_check_input");

    ret = ois_read_into_buffer(smsc, wait_usec);
    if (ret < 0) {
	goto error;
    }

    ret = ois_extract_msg_from_buffer(buffer, smsc);
    if (ret > 0) {
	IOTRACE("received", buffer, ret);
	switch (buffer[0]) {
	case 's':
	    ret = ois_submit_sm_result(smsc, buffer);
	    if (ret > 0) {
		warning(0, "ois_check_input: submit sm result signals (%d)...", ret);
	    } else if (ret < 0) {
		error(0, "ois_check_input: invalid submit sm result");
		goto error;
	    }
	    --smsc->ois_ack_debt;
	    time(&smsc->ois_alive);
	    break;
	case 'M':
	    ret = ois_deliver_sm_invoke(smsc, buffer);
	    if (ret >= 0) {
		ret = ois_deliver_sm_result(smsc, ret, buffer);
		if (ret < 0) {
		    goto error;
		}
	    } else {
		error(0, "ois_check_input: invalid deliver sm invoke");
		goto error;
	    }
	    time(&smsc->ois_alive);
	    break;
	default:
	    warning(0, "ois_check_input: unexpected message [%s]...",
		    ois_debug_str(buffer, ret));
	    break;
	}
    } else {
	if (smsc->socket != -1) {
	    time(&now);
	    if ((now - smsc->ois_alive) > OIS_MESSAGE_WAITTIME) {
		debug("bb.sms.ois", 0, "closing an idle connection");
		SAY(4, "ois_check_input: ois_disconnect");
		ois_disconnect(smsc);
	    }
	}
    }

    if (ret < 0) {
	error(0, "ois_check_input: malformatted message [%s]",
	      ois_debug_str(buffer, -ret));
	goto error;
    }

    if (smsc->ois_received_mo != NULL ||
	(smsc->ois_flags & OIS_FLAG_ERROR) != 0) {
	SAY(2, "ois_check_input has something");
	return 1; /* at least one message in the queue or an error pending */
    } else {
	return 0; /* no messages this time */
    }

 error:
    smsc->ois_flags |= OIS_FLAG_ERROR;
    return 1;
}


static int ois_check_incoming(SMSCenter *smsc, long wait_usec)
{
    fd_set read_fd;
    struct timeval tv;
    int ret;

    SAY(8, "ois_check_incoming");

    tv.tv_sec = 0;
    tv.tv_usec = wait_usec;

    FD_ZERO(&read_fd);
    FD_SET(smsc->ois_listening_socket, &read_fd);
    ret = select(smsc->ois_listening_socket + 1, &read_fd, NULL, NULL, &tv);
    if (ret == -1) {
	if (errno == EINTR || errno == EAGAIN) {
	    return 0;
	} else {
	    error(errno, "ois_check_incoming: select failed");
	    smsc->ois_flags |= OIS_FLAG_ERROR;
	    return -1;
	}
    } else if (ret == 0) {
	return 0;
    }

    /* if we end up here, someone is trying to connect */

    if (smsc->socket != -1) {
	if ((smsc->ois_flags & OIS_FLAG_MULTIPLE_CALL) == 0) {
	    /* if you see lots of these, maybe we should accept */
	    /* multiple incoming connections at a time... */
	    debug("bb.sms.ois", 0, "letting an incoming call to wait until the old one disconnects");
	    smsc->ois_flags |= OIS_FLAG_MULTIPLE_CALL;
	}
	return 0;
    }

    smsc->ois_flags &= ~OIS_FLAG_MULTIPLE_CALL;
    return ois_open_receiver(smsc);
}


static void ois_append_to_list(ois_listentry **head, Msg *msg)
{
    ois_listentry *item;
    ois_listentry *tail;

    SAY(2, "ois_append_to_list");

    item = gw_malloc(sizeof(ois_listentry));
    item->next = NULL;
    item->msg = msg;

    if (*head == NULL) {
	*head = item;
    } else { /* not so bright algorithm, but ok with relatively short lists */
	for (tail = *head; tail->next != NULL; tail = tail->next) ;
	tail->next = item;
    }
    return;
}



static int ois_int_to_i4(char *raw, int nbr)
{
    int pos;

    SAY(3, "ois_int_to_i4");

    for (pos = 0; pos < 4; ++pos) {
	raw[pos] = (char)(nbr % 0x100);
	nbr /= 0x100;
    }
    return 4;
}

static int ois_increment_counter(void)
{
    SAY(3, "ois_increment_counter");

    ois_counter = (ois_counter+1) % MAXCOUNTER;
    return ois_counter;
}


static int ois_submit_sm_invoke(SMSCenter *smsc, const Msg *msg)
{
    char body[BUFLEN+1];
    char buffer[BUFLEN+1];
    int len;
    int count;
    int i;
    int ret;

    SAY(2, "ois_submit_sm_invoke");

    /* construct a message */

    ois_increment_counter();                  /* once per invoke */
    len = ois_encode_submit_sm_invoke(body, msg);

    /* the x.25 gear should be capable to fragment large messages, but... */
    /* let's just use an explicit 128 byte blocks */

    count = (len-1) / 121;                    /* 121 = 128 - 6 - 1 */

    /* first part */

    sprintf(buffer, "%c%c%04d%.121s%c",
	    'S',                              /* submit sm invoke */
	    (char)(0x50|count),               /* ia5 encoding, first part */
	    ois_counter,
	    &body[0],
	    EOL);
    IOTRACE("sending", buffer, strlen(buffer));
    ret = write_to_socket(smsc->socket, buffer);
    if (ret < 0) {
	goto error;
    }

    /* additional parts */

    for (i = 1; i <= count; ++i) {
	sprintf(buffer, "%c%c%04d%.121s%c",
		'S',                          /* submit sm invoke */
		(char)(0x60|(count-i)),       /* ia5, additional part */
		ois_counter,
		&body[i*121],
		EOL);
	IOTRACE("sending", buffer, strlen(buffer));
	ret = write_to_socket(smsc->socket, buffer);
	if (ret < 0) {
	    goto error;
	}
    }

    SAY(2, "ois_submit_sm_invoke ok");
    return 0;

 error:
    SAY(2, "ois_submit_sm_invoke error");
    return -1;
}


static int ois_encode_submit_sm_invoke(char *str, const Msg *msg)
{
    char raw[BUFLEN];
    int pos;
    int ret;

    SAY(3, "ois_encode_submit_sm_invoke");

    /* construct the submit sm invoke body content */

    pos = 0;
    pos += ois_append_msisdn(&raw[pos], msg);
    pos += ois_append_sme_reference_number(&raw[pos]);
    pos += ois_append_priority(&raw[pos]);
    pos += ois_append_originating_address(&raw[pos]);
    pos += ois_append_validity_period(&raw[pos]);
    pos += ois_append_data_coding_scheme(&raw[pos], msg);
    pos += ois_append_status_report_request(&raw[pos]);
    pos += ois_append_protocol_id(&raw[pos]);
    pos += ois_append_submission_options(&raw[pos], msg);
    pos += ois_append_sm_text(&raw[pos], msg);

    ret = ois_convert_to_ia5(str, raw, pos);
    return ret;
}

static int ois_append_msisdn(char *raw, const Msg *msg)
{
    int len;

    SAY(3, "ois_append_msisdn");

    len = octstr_len(msg->sms.receiver);
    raw[0] = (char) len;
    memcpy(&raw[1], octstr_get_cstr(msg->sms.receiver), len);
    return 1 + len;
}

static int ois_append_sme_reference_number(char *raw)
{
    SAY(3, "ois_append_sme_reference_number");

    /* 1=key, 2=not key (OIS 4.5) */
    /* or 1=reject duplicates, 2=allow duplicates (OIS 5.0) */
    raw[0] = (char) 2;
    return 1 + ois_int_to_i4(&raw[1], ois_counter);
}

static int ois_append_priority(char *raw)
{
    SAY(3, "ois_append_priority");

    raw[0] = (char) 1; /* 0=high, 1=normal */
    return 1;
}

static int ois_append_originating_address(char *raw)
{
    SAY(3, "ois_append_originating_address");

    raw[0] = (char) 2; /* length */
    raw[1] = 'A'; /* A3=address type, actual address is unnecessary */
    raw[2] = '3';

    return 3;
}

static int ois_append_validity_period(char *raw)
{
    SAY(3, "ois_append_validity_period");

    raw[0] = (char) 2; /* 0=none, 1=absolute, 2=relative */
    raw[1] = (char) 1; /* relative, (v+1)*5 minutes, v<144 */
    return 2;
}

static int ois_append_data_coding_scheme(char *raw, const Msg *msg)
{
    SAY(3, "ois_append_data_coding_scheme");

    /* 0x0f is a special code for ASCII text, the SMSC will convert
     * this to GSM and set the DCS to 0.
     * FIXME: Convert to GSM ourselves and use DCS_GSM_TEXT.
     * FIXME: use fields_to_dcs and try to support DC_UCS2 too ;) */
    raw[0] = (char) (msg->sms.coding == DC_8BIT ? DCS_OCTET_DATA : 0x0f);
    return 1;
}

static int ois_append_status_report_request(char *raw)
{
    SAY(3, "ois_append_status_report_request");

    raw[0] = (char) 0x00; /* bit field, bit 0=abandoned, bit 2=delivered */
    return 1;
}

static int ois_append_protocol_id(char *raw)
{
    SAY(3, "ois_append_protocol_id");

    raw[0] = (char) 0; /* 0=default */
    return 1;
}

static int ois_append_submission_options(char *raw, const Msg *msg)
{
    SAY(3, "ois_append_submission_options");

    /* bit field, bit 0=reply path, bit 1=udh, bits 3-4=dcs interpretation */
    raw[0] = (char) 0x00;
    if (octstr_len(msg->sms.udhdata)) {
	raw[0] |= (char) 0x02;
    }
    if (msg->sms.coding == DC_8BIT) { /* XXX and UCS-2? */
	raw[0] |= (char) 0x10;
    }
    return 1;
}


static int ois_append_sm_text(char *raw, const Msg *msg)
{
    int udhlen7, udhlen8;
    int msglen7, msglen8;
    int len;

    SAY(3, "ois_append_sm_text");

    if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) {
        charset_utf8_to_gsm(msg->sms.udhdata);
        charset_utf8_to_gsm(msg->sms.msgdata);
    }


    /* calculate lengths */

    udhlen8 = octstr_len(msg->sms.udhdata);
    msglen8 = octstr_len(msg->sms.msgdata);

    udhlen7 = udhlen8;
    msglen7 = msglen8;
    len = udhlen8 + msglen8;

    /* copy text */

    raw[0] = (char) (len);
    raw[1] = (char) (udhlen7 + msglen7);
    memcpy(&raw[2], octstr_get_cstr(msg->sms.udhdata), udhlen8);
    memcpy(&raw[2+udhlen8], octstr_get_cstr(msg->sms.msgdata), msglen8);

    IOTRACE("encoding", &raw[2], len);

    return 2 + len;
}


static int ois_submit_sm_result(SMSCenter *smsc, const char *buffer)
{
    int status;
    int ret;

    SAY(2, "ois_submit_sm_result");

    ret = ois_decode_submit_sm_result(&status, buffer);
    if (ret < 0) {
	goto error;
    }

    return status;

 error:
    return -1;
}


static int ois_decode_submit_sm_result(int *code, const char *str)
{
    int buflen;
    char raw[BUFLEN];
    int len;

    SAY(3, "ois_decode_submit_sm_result");

    buflen = strlen(str) - 1;
    if (buflen < 7 || str[0] != 's' || str[1] != 0x50 || str[buflen] != EOL) {
	goto error;
    }

    len = ois_convert_from_ia5(raw, &str[6]);
    if (len <= 0) {
	goto error;
    }

    *code = raw[0];
    *code &= 0xff;

    /* there is smsc reference number and accept time, but we ignore them */

    return 0;

 error:
    return -1;
}


static int ois_deliver_sm_invoke(SMSCenter *smsc, const char *buffer)
{
    Msg *msg;
    int ret;
	ois_listentry **mo;
	
    SAY(2, "ois_deliver_sm_invoke");

    msg = msg_create(sms);

    ret = ois_decode_deliver_sm_invoke(msg, buffer);
    if (ret < 0) {
	goto error;
    }

	mo = (ois_listentry **)&smsc->ois_received_mo;
    ois_append_to_list(mo, msg);

    return 0;
    
 error:
    msg_destroy(msg);
    return -1;
}


static int ois_decode_deliver_sm_invoke(Msg *msg, const char *str)
{
    char body[BUFLEN+1];
    char raw[BUFLEN];
    int len;
    int i;
    int pos;
    int ret;

    SAY(3, "ois_decode_deliver_sm_invoke");

    ret = ois_check_deliver_sm_invoke(str);
    if (ret < 0) {
	goto error;
    }

    /* extract body */

    len = strlen(str);
    for (pos = 0, i = 6; i < len; ++i) {
	if (str[i] != EOL) {
	    body[pos++] = str[i];
	} else {
	    i += 6;
	}
    }
    body[pos] = '\0';
    memset(raw, '\0', sizeof(raw));
    len = ois_convert_from_ia5(raw, body);

    /* adjust msg values */

    pos = 0;
    pos += ois_adjust_destination_address(msg, &raw[pos]);
    pos += ois_ignore_smsc_reference_number(&raw[pos]);
    pos += ois_adjust_originating_address(msg, &raw[pos]);
    pos += ois_adjust_data_coding_scheme(msg, &raw[pos]);
    pos += ois_ignore_protocol_id(&raw[pos]);
    pos += ois_adjust_additional_information(msg, &raw[pos]);
    pos += ois_adjust_sm_text(msg, &raw[pos]);
    pos += ois_ignore_time(&raw[pos]); /* accept time */
    pos += ois_ignore_time(&raw[pos]); /* invoke time */
    if (pos != len) {
	error(0, "ois_decode_deliver_sm_invoke: message parsing error (%d!=%d)",
	      pos, len);
	goto error;
    }
    return 0;

 error:
    return -1;
}


static int ois_check_deliver_sm_invoke(const char *str)
{
    int buflen;
    char buffer[BUFLEN+1];
    int count;

    SAY(3, "ois_check_deliver_sm_invoke");

    /* check the (initial) header and trailer */

    buflen = strlen(str) - 1;
    if (buflen < 7 || str[0] != 'M' || (str[1] & 0x50) != 0x50
	|| str[buflen] != EOL) {
	goto error;
    }

    count = str[1] & 0x0f;
    while (--count >= 0)
    {
	/* check the additional header */

	sprintf(buffer, "%c%c%c%.4s",
		EOL,
		'M',                      /* deliver sm invoke */
		(char)(0x60|count),       /* ia5 encoding, additional part */
		&str[2]);
	if (strstr(str, buffer) == NULL) {
	    goto error;
	}
    }

    return 0;
    
 error:
    return -1;
}


static int ois_adjust_destination_address(Msg *msg, const char *raw)
{
    int len;

    SAY(3, "ois_adjust_destination_address");

    len = raw[0] & 0xff;
    msg->sms.receiver = octstr_create_from_data(&raw[1+2], len-2);

    return 1 + len;
}

static int ois_ignore_smsc_reference_number(const char *raw)
{
    int value;

    SAY(3, "ois_ignore_smsc_reference_number");

    value = raw[3] & 0xff;
    value <<= 8;
    value |= raw[2] & 0xff;
    value <<= 8;
    value |= raw[1] & 0xff;
    value <<= 8;
    value |= raw[0] & 0xff;

    return 4;
}

static int ois_adjust_originating_address(Msg *msg, const char *raw)
{
    int len;

    SAY(3, "ois_adjust_originating_address");

    len = raw[0] & 0xff;
    msg->sms.sender = octstr_create_from_data(&raw[1+2], len-2);

    return 1 + len;
}

static int ois_adjust_data_coding_scheme(Msg *msg, const char *raw)
{
    SAY(3, "ois_adjust_data_coding_scheme");

    /* we're using this variable temporarily: 
     * ois_adjust_sm_text will set the correct value */

    msg->sms.coding = (raw[0] & 0xff) + 1;

    return 1;
}

static int ois_ignore_protocol_id(const char *raw)
{
    int value;

    SAY(3, "ois_ignore_protocol_id");

    value = raw[0] & 0xff;

    return 1;
}

static int ois_adjust_additional_information(Msg *msg, const char *raw)
{
    SAY(3, "ois_adjust_additional_information");

    /* we're using this variable temporarily: 
     * ois_adjust_sm_text will set the correct value */
    msg->sms.mclass = raw[0] & 0xff;

    return 1;
}

static int ois_adjust_sm_text(Msg *msg, const char *raw)
{
    int msglen7, msglen8;
    char buffer[BUFLEN+1];

    SAY(3, "ois_adjust_sm_text");

    /* calculate lengths */

    msglen7 = raw[0] & 0xff;
    msglen8 = raw[1] & 0xff;

    /* copy text, note: flag contains temporarily the raw type description */

    switch ((msg->sms.coding - 1) & 0xff) { 
    case 0x00: /* gsm7 */
	ois_expand_gsm7(buffer, &raw[2], msglen7);
	ois_convert_to_iso88591(buffer, msglen7);
	if (msg->sms.mclass & 0x02) { /* XXX mclass temporarily */
	    msg->sms.msgdata = octstr_create("");
	    msg->sms.udhdata = octstr_create_from_data(buffer, msglen7);
	} else {
	    msg->sms.msgdata = octstr_create_from_data(buffer, msglen7);
	    msg->sms.udhdata = octstr_create("");
	}
	msg->sms.coding = DC_7BIT;
	break;
    case 0x0f: /* ia5 */
	memcpy(buffer, &raw[2], msglen8);
	ois_convert_to_iso88591(buffer, msglen8);
	if (msg->sms.mclass & 0x02) { /* XXX mclass temporarily */
	    msg->sms.msgdata = octstr_create("");
	    msg->sms.udhdata = octstr_create_from_data(buffer, msglen8);
	} else {
	    msg->sms.msgdata = octstr_create_from_data(buffer, msglen8);
	    msg->sms.udhdata = octstr_create("");
	}
	msg->sms.coding = DC_7BIT;
	break;
    default: /* 0xf4, 0xf5, 0xf6, 0xf7; 8bit to disp, mem, sim or term */ 
	if (msg->sms.mclass & 0x02) { /* XXX mclass temporarily */
	    msg->sms.msgdata = octstr_create("");
	    msg->sms.udhdata = octstr_create_from_data(&raw[2], msglen8);
	} else {
	    msg->sms.msgdata = octstr_create_from_data(&raw[2], msglen8);
	    msg->sms.udhdata = octstr_create("");
	}
	msg->sms.coding = DC_8BIT;
	break;
    }
    msg->sms.mclass = MC_UNDEF;

    if (octstr_len(msg->sms.udhdata)) {
	IOTRACE("decoded udh", octstr_get_cstr(msg->sms.udhdata),
		octstr_len(msg->sms.udhdata));
    } else {
	IOTRACE("decoded", octstr_get_cstr(msg->sms.msgdata),
		octstr_len(msg->sms.msgdata));
    }

    return 2 + msglen8;
}


static int ois_ignore_time(const char *raw)
{
    char str[15];

    SAY(3, "ois_ignore_time");

    strncpy(str, raw, 14); str[14] = '\0';

    return 14;
}



static int ois_deliver_sm_result(SMSCenter *smsc, int result, const char *str)
{
    char body[BUFLEN+1];
    char buffer[BUFLEN+1];
    int len;
    int ret;

    SAY(2, "ois_deliver_sm_result");

    /* construct a message */

    len = ois_encode_deliver_sm_result(body, result);

    /* first and only part */

    sprintf(buffer, "%c%c%.4s%.121s%c",
	    'm',                              /* deliver sm result */
	    (char)(0x50),                     /* ia5 encoding, the only part */
	    &str[2],
	    &body[0],
	    EOL);

    IOTRACE("sending", buffer, strlen(buffer));
    ret = write_to_socket(smsc->socket, buffer);
    if (ret < 0) {
	goto error;
    }

    return 0;

 error:
    return -1;
}


static int ois_encode_deliver_sm_result(char *str, int result)
{
    char raw[4];

    SAY(3, "ois_encode_deliver_sm_result");

    /* construct the deliver sm result body content */

    raw[0] = (char) result;

    return ois_convert_to_ia5(str, raw, 1);
}


static int ois_expand_gsm7(char *raw8, const char *raw7, int len)
{
    int i;
    char bits[8*(BUFLEN+1)];

    SAY2(3, "ois_expand_gsm7 len=%d", len);

    /* yeah, there are also better algorithms, but... */
    /* well, at least this is fairly portable and ok for small messages... */

    ois_expand_gsm7_to_bits(bits, raw7, len);
    for (i = 0; i < len; ++i) {
	raw8[i] = ois_expand_gsm7_from_bits(bits, i);
    }

    SAY2(5, "ois_expand_gsm7 gave [%s]", ois_debug_str(raw8, i));
    return i;
}

static int ois_expand_gsm7_to_bits(char *bits, const char *raw7, int len)
{
    int i, j, k;
    char ch;

    SAY(3, "ois_expand_gsm7_to_bits");

    len *= 7; /* number of bits in the gms 7-bit msg */

    for (j = i = 0; j < len; ++i) {
	ch = raw7[i];
	for (k = 0; k < 8; ++k) {
	    bits[j++] = (char) (ch & 0x01);
	    ch >>= 1;
	}
    }

    return j;
}

static char ois_expand_gsm7_from_bits(const char *bits, int pos)
{
    int i;
    char ch;

    SAY2(8, "ois_expand_gsm7_from_bits pos=%d", pos);

    pos *= 7; /* septet position in bits */
    ch = '\0';
    for (i = 6; i >= 0; --i) {
	ch <<= 1;
	ch |= bits[pos+i];
    }

    return ch;
}


static int ois_convert_to_ia5(char *str, const char *raw, int len)
{
    int j;
    int i;
    int ch;

    SAY2(3, "ois_convert_to_ia5 len=%d", len);

    for (j = i = 0; i < len; ++i) {
	ch = raw[i] & 0xff;
	if (ch == 0x5c || ch == 0x5e || ch == 0x60 || ch == 0x7e) {
  	    str[j++] = (char) 0x5c;
  	    str[j++] = (char) ch;
	} else if (0x20 <= ch && ch < 0x7f) {
	    str[j++] = (char) ch;
	} else if (0x00 <= ch && ch < 0x20) {
	    str[j++] = (char) 0x5e;
	    str[j++] = (char) ch + 0x40;
	} else if (0xa0 <= ch && ch < 0xff) {
	    str[j++] = (char) 0x60;
	    str[j++] = (char) ch - 0x80;
	} else if (0x80 <= ch && ch < 0xa0) {
	    str[j++] = (char) 0x7e;
	    str[j++] = (char) ch - 0x40;
	} else if (ch == 0x7f) {
	    str[j++] = (char) 0x5e;
	    str[j++] = (char) 0x7e;
	} else { /* ch == 0xff */
	    str[j++] = (char) 0x7e;
	    str[j++] = (char) 0x7e;
	}
    }

    str[j] = '\0';
    SAY2(5, "ois_convert_to_ia5 gave [%s]", ois_debug_str(str, j));
    return j;
}


static int ois_convert_from_ia5(char *raw, const char *str)
{
    int j;
    int i;
    int ch;

    SAY(3, "ois_convert_from_ia5");

    for (j = i = 0; ; ++i) {
	ch = str[i] & 0xff;
	if (ch < 0x20 || 0x7f <= ch) {
	    break;
	} else if (ch == 0x5c) {
	    ch = str[++i] & 0xff;
	    if (ch == 0x5c || ch == 0x5e || ch == 0x60 || ch == 0x7e) {
		raw[j++] = (char) ch;
	    } else {
		break;
	    }
	} else if (ch == 0x5e) {
	    ch = str[++i] & 0xff;
	    if (0x40 <= ch && ch < 0x60) {
		raw[j++] = (char) ch - 0x40;
	    } else if (ch == 0x7e) {
		raw[j++] = (char) 0x7f;
	    } else {
		break;
	    }
	} else if (ch == 0x60) {
	    ch = str[++i] & 0xff;
	    if (0x20 <= ch && ch < 0x7f) {
		raw[j++] = (char) ch + 0x80;
	    } else {
		break;
	    }
	} else if (ch == 0x7e) {
	    ch = str[++i] & 0xff;
	    if (0x40 <= ch && ch < 0x60) {
		raw[j++] = (char) ch + 0x40;
	    } else if (ch == 0x7e) {
		raw[j++] = (char) 0xff;
	    } else {
		break;
	    }
	} else { /* 0x20 <= ch && ch < 0x7f */
	    raw[j++] = (char) ch;
	}
    }

    SAY2(5, "ois_convert_from_ia5 gave [%s]", ois_debug_str(raw, j));
    return j;
}


static int ois_convert_to_iso88591(char *raw, int len)
{
    /* a best effort 1-to-1 conversion according to ois appendix a */

    static const char gsm_to_iso88591[] = {
	'@', 0xa3,'$', 0xa5,0xe8,0xe9,0xf9,0xec, /* 0x00 - 0x07 */
	0xf2,0xc7,'\n',0xd8,0xf8,'\r',0xc5,0xe5, /* 0x08 - 0x0f */
	'D', ' ', 'F', 'G', 'L', 'W', 'P', 'Y',  /* 0x10 - 0x17, poor! */
	'Y', 'S', 'X', ' ', 0xc6,0xe6,'b', 0xc9, /* 0x18 - 0x1f, poor! */
	' ', '!', '"', '#', 0xa4, '%', '&', '\'',/* 0x20 - 0x27 */
	'(', ')', '*', '+', ',', '-', '.', '/',  /* 0x28 - 0x2f */
	'0', '1', '2', '3', '4', '5', '6', '7',  /* 0x30 - 0x37 */
	'8', '9', ':', ';', '<', '=', '>', '?',  /* 0x38 - 0x3f */
	0xa1,'A', 'B', 'C', 'D', 'E', 'F', 'G',  /* 0x40 - 0x47 */
	'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',  /* 0x48 - 0x4f */
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',  /* 0x50 - 0x57 */
	'X', 'Y', 'Z', 0xc4,0xd6,0xd1,0xdc,0xa7, /* 0x58 - 0x5f */
	0xbf,'a', 'b', 'c', 'd', 'e', 'f', 'g',  /* 0x60 - 0x67 */
	'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',  /* 0x68 - 0x6f */
	'p', 'q', 'r', 's', 't', 'u', 'v', 'w',  /* 0x70 - 0x77 */
	'x', 'y', 'z', 0xe4,0xf6,0xf1,0xfc,0xe0  /* 0x78 - 0x7f */
    };

    int i;

    SAY2(3, "ois_convert_to_iso88591 len=%d", len);

    for (i = 0; i < len; ++i) {
	raw[i] = gsm_to_iso88591[raw[i] & 0x7f];
    }

    SAY2(5, "ois_convert_to_iso88591 gave [%s]", ois_debug_str(raw, i));
    return i;
}


/*
 * Extract a message from the internal buffer.
 */

static int ois_extract_msg_from_buffer(char *str, SMSCenter *smsc)
{
    int len;
    int count;

    SAY2(8, "ois_extract_msg_from_buffer buflen=%ld", (long)smsc->buflen);

    str[0] = '\0';

    if (smsc->buflen < 7) {             /* 7 = 6 + 1 */
	return 0;              /* we don't have a message yet */
    }

    if (strchr("SRDATECQLMPOVsrdatecqlmpov", smsc->buffer[0]) == NULL
	|| (smsc->buffer[1] & 0xf0) != 0x50) {

	goto error;
    }

    /* a valid message type, find the end of the message */

    count = smsc->buffer[1] & 0x0f;
    for (len = 0; (size_t) len < smsc->buflen; ++len) {
	if (smsc->buffer[len] == EOL) {
	    if (--count < 0) {
		++len;
		break;
	    }
	}
    }

    if (count >= 0) {          /* we don't have all the pieces */
	if (len < BUFLEN) {
	    return 0;          /* ...but maybe later */
	}
	goto error;
    }

    /* the buffer contains a promising message candidate */

    memcpy(str, smsc->buffer, len);
    str[len] = '\0';
    smscenter_remove_from_buffer(smsc, len); /* just the message */

    return len;

 error:
    for (len = 0; (size_t) len < smsc->buflen && smsc->buffer[len] != EOL; 
         ++len) ;
    if (len > BUFLEN) len = BUFLEN;

    memcpy(str, smsc->buffer, len);
    str[len] = '\0';
    smscenter_remove_from_buffer(smsc, smsc->buflen); /* everything */

    return -len;
}



/*
 * Extract a line from the internal buffer.
 */

static int ois_extract_line_from_buffer(char *str, SMSCenter *smsc)
{
    int len;

    SAY2(3, "ois_extract_line_from_buffer buflen=%ld", (long)smsc->buflen);

    str[0] = '\0';

    for (len = 0; (size_t) len < smsc->buflen && smsc->buffer[len] != '\n'; 
         ++len) ;

    if ((size_t) len >= smsc->buflen) {
	return 0;
    } else {
	++len;
    }

    /* the buffer contains a line */

    memcpy(str, smsc->buffer, len);
    str[len] = '\0';
    smscenter_remove_from_buffer(smsc, len); /* just the line */

    return len;
}


static void ois_swap_buffering(SMSCenter *smsc)
{
    time_t alive;
    int socket;
    char *buffer;
    size_t bufsize;
    size_t buflen;

    SAY(8, "ois_swap_buffering");

    if (smsc->ois_bufsize == 0) {
	smsc->ois_buflen = 0;
	smsc->ois_bufsize = smsc->bufsize;
	smsc->ois_buffer = gw_malloc(smsc->ois_bufsize);
	memset(smsc->ois_buffer, 0, smsc->ois_bufsize);
    }

    alive = smsc->ois_alive;
    smsc->ois_alive = smsc->ois_alive2;
    smsc->ois_alive2 = alive;

    socket = smsc->socket;
    smsc->socket = smsc->ois_socket;
    smsc->ois_socket = socket;

    buffer = smsc->buffer;
    smsc->buffer = smsc->ois_buffer;
    smsc->ois_buffer = buffer;

    buflen = smsc->buflen;
    smsc->buflen = smsc->ois_buflen;
    smsc->ois_buflen = buflen;

    bufsize = smsc->bufsize;
    smsc->bufsize = smsc->ois_bufsize;
    smsc->ois_bufsize = bufsize;

    return;
}


static const char *ois_debug_str(const char *raw, int len)
{
    static const char hex[] = "0123456789abcdef";
    static char str[4*(BUFLEN+1)+1];
    int pos;
    int ch;
    int i;

    pos = 0;
    for (i = 0; i < len; ++i) {
	ch = raw[i] & 0xff;
	if (0x20 <= ch && ch < 0x7f && ch != 0x5c) {
	    str[pos++] = (char) ch;
	} else {
	    str[pos++] = '\\';
	    str[pos++] = 'x';
	    str[pos++] = hex[ch/16];
	    str[pos++] = hex[ch%16];
	}
    }
    str[pos] = '\0';
    return str;
}
