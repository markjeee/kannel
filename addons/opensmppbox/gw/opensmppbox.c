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
 * Author: 2006 Chimit Software Development.
 * http://www.chimit.nl/ rene.kluwen@chimit.nl
 */

/*
 * opensmppbox.c - main program of the opensmppbox
 */

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <math.h>

#include "gwlib/gwlib.h"
#include "gw/msg.h"
#include "gw/shared.h"
#include "gw/bb.h"

#include "gw/smsc/smpp_pdu.h"
#include "gw/sms.h"
#include "gw/dlr.h"
#include "gw/heartbeat.h"
#include "gw/meta_data.h"

#undef GW_NAME
#undef GW_VERSION
#include "../sb-config.h"

#ifdef HAVE_PAM_SECURITY
#include <security/pam_appl.h>
#elif defined HAVE_PAM_PAM
#include <pam/pam_appl.h>
#endif

/* our config */
static Cfg *cfg;
/* have we received restart cmd from bearerbox? */
static volatile sig_atomic_t restart_smppbox = 0;
static volatile sig_atomic_t smppbox_status;
#define SMPP_DEAD 0
#define SMPP_SHUTDOWN 1
#define SMPP_RUNNING 2
static long smppbox_port;
static int smppbox_port_ssl = 0;
static long bearerbox_port;
static Octstr *bearerbox_host;
static int bearerbox_port_ssl = 0;
static Octstr *smpp_logins;
static Counter *boxid;
static int restart = 0;
static List *all_boxes;
static Dict *list_dict;
static Counter *catenated_sms_counter;
static long sms_max_length = MAX_SMS_OCTETS;
static long smpp_source_addr_ton = -1;
static long smpp_source_addr_npi = -1;
static int smpp_autodetect_addr = 0;
static long smpp_dest_addr_ton = -1;
static long smpp_dest_addr_npi = -1;

static Dict *smsc_by_receiver = NULL;
static Dict *smsc_by_smsbox_id = NULL;
static Dict *smsc_by_sender = NULL;
static Dict *smsc_by_sender_smsbox_id = NULL;

static Octstr *smppbox_id;
static Octstr *our_system_id;
static Octstr *route_to_smsc;
static time_t smpp_timeout;

static int systemidisboxcid;
static int enablepam;
static Octstr *pamacl;


#define TIMEOUT_SECONDS 300

typedef enum { SMPP_LOGIN_NOTLOGGEDIN, SMPP_LOGIN_TRANSMITTER, SMPP_LOGIN_RECEIVER, SMPP_LOGIN_TRANSCEIVER } smpp_login;

typedef struct _boxc {
    Connection	*smpp_connection;
    Connection	*bearerbox_connection;
    smpp_login	login_type;
    int		logged_in;
    int		is_wap;
    long	id;
    int		load;
    int		version;
    Octstr	*alt_charset;
    time_t	connect_time;
    Counter	*smpp_pdu_counter;
    Octstr	*client_ip;
    List	*incoming;
    List	*retry;   	/* If sending fails */
    List	*outgoing;
    Dict	*sent;
    Semaphore	*pending;
    volatile sig_atomic_t alive;
    Octstr	*boxc_id; /* identifies the connected opensmppbox instance */
    Octstr	*sms_service;
    Octstr	*route_to_smsc;
    Dict	*msg_acks;
    Dict	*deliver_acks;
    time_t	last_pdu_received;
    /* used to mark connection usable or still waiting for ident. msg */
    volatile int routable;


    Octstr	*service_type;
    long	source_addr_ton;
    long	source_addr_npi;
    int		autodetect_addr;
    long	dest_addr_ton;
    long	dest_addr_npi;
    int		alt_dcs;
    int		validityperiod;
    int		priority;
    int		mo_recode;


} Boxc;

void smpp_pdu_destroy_item(void *pdu)
{
	smpp_pdu_destroy(pdu);
}

static Octstr *boxc_route_msg_to_smsc(Boxc *box, Msg *msg);

/*
 * Use PAM (Pluggable Authentication Module) to check sendsms authentication.
 */

#ifdef HAVE_PAM

typedef const struct pam_message pam_message_type;

static const char *PAM_username;
static const char *PAM_password;

static int PAM_conv (int num_msg, pam_message_type **msg,
		     struct pam_response **resp,
		     void *appdata_ptr)
{
    int count = 0, replies = 0;
    struct pam_response *repl = NULL;
    int size = sizeof(struct pam_response);

#define GET_MEM \
	repl = gw_realloc(repl, size); \
	size += sizeof(struct pam_response)
#define COPY_STRING(s) (s) ? gw_strdup(s) : NULL

    for (count = 0; count < num_msg; count++) {
	switch (msg[count]->msg_style) {
	case PAM_PROMPT_ECHO_ON:
	    GET_MEM;
	    repl[replies].resp_retcode = PAM_SUCCESS;
	    repl[replies++].resp = COPY_STRING(PAM_username);
	    /* PAM frees resp */
	    break;

	case PAM_PROMPT_ECHO_OFF:
	    GET_MEM;
	    repl[replies].resp_retcode = PAM_SUCCESS;
	    repl[replies++].resp = COPY_STRING(PAM_password);
	    /* PAM frees resp */
	    break;

	case PAM_TEXT_INFO:
	    warning(0, "unexpected message from PAM: %s", msg[count]->msg);
	    break;

	case PAM_ERROR_MSG:
	default:
	    /* Must be an error of some sort... */
	    error(0, "unexpected error from PAM: %s", msg[count]->msg);
	    gw_free(repl);
	    return PAM_CONV_ERR;
	}
    }
    if (repl)
	*resp = repl;
    return PAM_SUCCESS;
}

static struct pam_conv PAM_conversation = {
    &PAM_conv,
    NULL
};


static int authenticate(const char *acl, const char *login, const char *passwd)
{
    pam_handle_t *pamh;
    int pam_error;
    
    PAM_username = login;
    PAM_password = passwd;
    
    pam_error = pam_start(acl, login, &PAM_conversation, &pamh);
    info(0, "Starting PAM for user: %s", login);
    if (pam_error != PAM_SUCCESS ||
        (pam_error = pam_authenticate(pamh, 0)) != PAM_SUCCESS) {
        warning(0, "PAM auth failed for user: %s", login);
	pam_end(pamh, pam_error);
	return 0;
    }
    pam_end(pamh, PAM_SUCCESS);
    info(0, "opensmppbox login by <%s>", login);
    return 1;
}

#endif /* HAVE_PAM */

/* check if login exists in database */
int check_login(Boxc *boxc, Octstr *system_id, Octstr *password, Octstr *system_type, smpp_login login_type) {
	int box;
	int success;
	Boxc *thisbox;
	FILE *fp;
	char systemid[255], passw[255], systemtype[255], allowed_ips[1024];
	Octstr *allowed_ips_str;

	fp = fopen(octstr_get_cstr(smpp_logins), "r");
	if (fp == NULL) {
		return 0;
	}
	while (!feof(fp)) {
		fscanf(fp, "%s %s %s %s\n", systemid, passw, systemtype, allowed_ips);
		if (systemidisboxcid) {
			success = (strcmp(octstr_get_cstr(system_id), systemid) == 0 && strcmp(octstr_get_cstr(password), passw) == 0);
		}
		else {
			success = (strcmp(octstr_get_cstr(system_id), systemid) == 0 && strcmp(octstr_get_cstr(password), passw) == 0 && strcmp(octstr_get_cstr(system_type), systemtype) == 0);
		}
		if (success) {
			if (strcmp(allowed_ips, "") != 0)  {
				allowed_ips_str = octstr_create(allowed_ips);
				if (is_allowed_ip(allowed_ips_str, octstr_imm("*.*.*.*"), boxc->client_ip) == 0) {
					info(0, "Box connection tried from denied host <%s>, disconnected", octstr_get_cstr(boxc->client_ip));
					octstr_destroy(allowed_ips_str);
					continue;
				}
				octstr_destroy(allowed_ips_str);
			}
			fclose(fp);
			goto valid_login;
		}
	}
	fclose(fp);
#ifdef HAVE_PAM
	if (enablepam && authenticate(octstr_get_cstr(pamacl), octstr_get_cstr(system_id), octstr_get_cstr(password))) {
		goto valid_login;
	}
#endif
	return 0;
valid_login:
	for (box = 0; box < gwlist_len(all_boxes); box++) {
		thisbox = (Boxc *)gwlist_get(all_boxes, box);
		if (octstr_compare(system_type, thisbox->boxc_id) == 0 && (thisbox->login_type == SMPP_LOGIN_TRANSCEIVER || (thisbox->login_type == login_type))) {
			debug("bb.sms.smpp", 0, "opensmppbox[%s]: Multiple login: disconnect.",
				octstr_get_cstr(thisbox->boxc_id));
			thisbox->alive = 0;
#ifdef HAVE_SHUTDOWN_CONNECTION
			shutdown_connection(thisbox->bearerbox_connection);
			shutdown_connection(thisbox->smpp_connection);
#endif
		}
	}
	return 1;
}

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
        debug("opensmppbox", 0, "SMPP[%s]: %s", \
            octstr_get_cstr(id), msg);          \
        smpp_pdu_dump(id, pdu);                 \
    } while(0)
#endif


/*
 * Converting SMPP timestamp to minutes relative 
 * to our localtime.
 * Return -1 if error detected
 * Author: amalysh@kannel.org
*/
static int timestamp_to_minutes(Octstr *timestamp)
{
    struct tm tm, local;
    time_t valutc, utc;
    int rc, diff, dummy, localdiff;
    char relation;

    if (octstr_len(timestamp) == 0)
        return 0;

    if (octstr_len(timestamp) != 16)
        return -1;

    /*
    * Timestamp format:
    * YYMMDDhhmmsstnn[+-R]
    * t - tenths of second (not used by us)
    * nn - Time difference in quarter hours between local and UTC time
    */
    rc = sscanf(octstr_get_cstr(timestamp),
            "%02d%02d%02d%02d%02d%02d%1d%02d%1c",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
            &dummy, &diff, &relation);
    if (rc != 9)
       return -1;

    utc = time(NULL);
    if (utc == ((time_t)-1))
        return 0;

    if (relation == '+' || relation == '-') {
        tm.tm_year += 100; /* number of years since 1900 */
        tm.tm_mon--; /* month 0-11 */
        tm.tm_isdst = -1;
        /* convert to sec. since 1970 */
        valutc = gw_mktime(&tm);
        if (valutc == ((time_t)-1))
            return -1;

        /* work out local time, because gw_mktime assume local time */
        local = gw_localtime(utc);
        tm = gw_gmtime(utc);
        local.tm_isdst = tm.tm_isdst = -1;
        localdiff = difftime(gw_mktime(&local), gw_mktime(&tm));
        valutc += localdiff;

        debug("sms.smpp",0, "diff between utc and localtime (%d)", localdiff);
        diff = diff*15*60;
        switch(relation) {
            case '+':
                valutc -= diff;
                break;
            case '-':
                valutc += diff;
                break;
        }
    } else if (relation == 'R') { /* relative to SMSC localtime */
        local = gw_localtime(utc);
        local.tm_year += tm.tm_year;
        local.tm_mon += tm.tm_mon;
        local.tm_mday += tm.tm_mday;
        local.tm_hour += tm.tm_hour;
        local.tm_min += tm.tm_min;
        local.tm_sec += tm.tm_sec;
        valutc = gw_mktime(&local);
        if (valutc == ((time_t)-1))
           return -1;
    } else {
        return -1;
    }
    tm = gw_gmtime(valutc);
    debug("sms.smpp",0,"Requested UTC timestamp: %02d-%02d-%02d %02d:%02d:%02d",
            tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    debug("sms.smpp", 0, "requested timestamp in min. (%ld)", (valutc - utc)/60);

    return ceil ( difftime (valutc, utc) / 60 );
}



/*
 *-------------------------------------------------
 *  receiver thingies
 *-------------------------------------------------
 *
*/

/* send to bearerbox */

static int send_msg(Connection *conn, Boxc *boxconn, Msg *pmsg)
{
	/* Caution: implicit msg_destroy */
	write_to_bearerbox_real(conn, pmsg);
	return 0;
}

/* for heartbeat fn */
/*
static void write_to_bearerboxes(Msg *msg)
{
	long pos;
	Boxc *box;

	for (pos = 0; pos < gwlist_len(all_boxes); pos++) {
		box = (Boxc *)gwlist_get(all_boxes, pos);
		send_msg(box->bearerbox_connection, box, msg);
	}
}
*/

/* for heartbeat fn */
/*
static long outstanding_requests(void)
{
    return 10; 
}
*/

/*
 * Identify ourself to bearerbox for opensmppbox-specific routing inside bearerbox.
 * Do this even while no opensmppbox-id is given to unlock the sender thread in
 * bearerbox.
 */
static void identify_to_bearerbox(Boxc *conn)
{
    Msg *msg;

    msg = msg_create(admin);
    msg->admin.command = cmd_identify;
    msg->admin.boxc_id = octstr_duplicate(conn->boxc_id);
    send_msg(conn->bearerbox_connection, conn, msg);
}

/* read from bearerbox */

static Msg *read_from_box(Connection *conn, Boxc *boxconn)
{
    Octstr *pack;
    Msg *msg;

    pack = NULL;
    while (boxconn->alive) {
	switch (read_from_bearerbox_real(conn, &msg, 1.0)) {
	case -1:
	    /* connection to bearerbox lost */
	    return NULL;
	    break;
	case  0:
	    /* all is well */
	    return msg;
	    break;
	case  1:
	    /* timeout */
	    break;
	}
    }

    return msg;
}

Msg *catenate_msg(List *list, int total)
{
	int current = 1, partno = 1, thismsg, max = 0;
	Msg *current_msg;
	Msg *ret = msg_duplicate(gwlist_get(list, 0));
	uuid_generate(ret->sms.id);

	octstr_destroy(ret->sms.udhdata);
	ret->sms.udhdata = NULL;
	octstr_delete(ret->sms.msgdata, 0, octstr_len(ret->sms.msgdata));
	while (max < total) {
		current_msg = gwlist_get(list, current - 1);
		if (current_msg) {
			thismsg = octstr_get_char(current_msg->sms.udhdata, 5);
			if (thismsg == partno) {
				octstr_append(ret->sms.msgdata, current_msg->sms.msgdata);
				max = 0;
				if (++partno > total) {
					return ret;
				}
			}
		}
		if (current >= total) {
			current = 0;
		}
		current++;
		max++;
	}
	/* fail */
	debug("opensmppbox", 0, "re-assembling message failed.");
	msg_destroy(ret);
	return NULL;
}

static long convert_addr_from_pdu(Octstr *id, Octstr *addr, long ton, long npi)
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
            error(0, "SMPP[%s]: Mallformed addr `%s', expected at least 7 digits. ",
                     octstr_get_cstr(id),
                     octstr_get_cstr(addr));
            reason = SMPP_ESME_RINVSRCADR;
            goto error;
        } else if (octstr_get_char(addr, 0) == '+' &&
                   !octstr_check_range(addr, 1, 256, gw_isdigit)) {
            error(0, "SMPP[%s]: Mallformed addr `%s', expected all digits. ",
                     octstr_get_cstr(id),
                     octstr_get_cstr(addr));
            reason = SMPP_ESME_RINVSRCADR;
            goto error;
        } else if (octstr_get_char(addr, 0) != '+' &&
                   !octstr_check_range(addr, 0, 256, gw_isdigit)) {
            error(0, "SMPP[%s]: Mallformed addr `%s', expected all digits. ",
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
            error(0, "SMPP[%s]: Mallformed addr `%s', alphanum length greater 11 chars. ",
                     octstr_get_cstr(id),
                     octstr_get_cstr(addr));
            reason = SMPP_ESME_RINVSRCADR;
            goto error;
        }
        break;
    default: /* otherwise don't touch addr, user should handle it */
        break;
    }
    
error:
    return reason;
}

static int send_pdu(Connection *conn, Octstr *id, SMPP_PDU *pdu)
{
    Octstr *os;
    int ret;

    dump_pdu("Sending PDU:", id, pdu);
    os = smpp_pdu_pack(id, pdu);
    if (os) {
        ret = conn_write(conn, os);   /* Caller checks for write errors later */
	octstr_destroy(os);
    }
    else {
	ret = -1;
    }
    return ret;
}

/* generate 8 character ID, taken from msgid */
static Octstr *generate_smppid(Msg *msg, int version)
{
	char uuidbuf[100];
	Octstr *result;

	// gw_assert(msg->type == sms); // we segfault on this

	uuid_unparse(msg->sms.id, uuidbuf);
	result = octstr_create_from_data(uuidbuf, version > 0x33 ? UUID_STR_LEN : 8);
	return result;
}

/* 
 * Try to read an SMPP PDU from a Connection. Return -1 for error (caller 
 * should close the connection), 0 for no PDU to ready yet, or 1 for PDU 
 * read and unpacked. Return a pointer to the PDU in `*pdu'. Use `*len' 
 * to store the length of the PDU to read (it may be possible to read the 
 * length, but not the rest of the PDU - we need to remember the lenght 
 * for the next call). `*len' should be zero at the first call. 
 */
static int read_pdu(Boxc *box, Connection *conn, long *len, SMPP_PDU **pdu) 
{ 
    Octstr *os; 
 
    if (*len == 0) { 
        *len = smpp_pdu_read_len(conn); 
        if (*len == -1) { 
            error(0, "opensmppbox[%s]: Server sent garbage, ignored.",
                  octstr_get_cstr(box->boxc_id));
            return -1; 
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
     
    *pdu = smpp_pdu_unpack(box->boxc_id, os); 
    if (*pdu == NULL) {
        error(0, "opensmppbox[%s]: PDU unpacking failed.",
              octstr_get_cstr(box->boxc_id));
        debug("bb.sms.smpp", 0, "opensmppbox[%s]: Failed PDU omitted.",
              octstr_get_cstr(box->boxc_id));
        /* octstr_dump(os, 0); */
        octstr_destroy(os);
        return -1;
    }

    octstr_destroy(os);
    return 1;
}

static List *msg_to_pdu(Boxc *box, Msg *msg)
{
    SMPP_PDU *pdu, *pdu2;
    List *pdulist = gwlist_create(), *parts;
    int dlrtype, catenate;
    int dlr_state = 7; /* UNKNOWN */
    Msg *dlr;
    char *text, *tmps, err[4] = { '0', '0', '0', '\0' };
    char submit_date_c_str[11] = { '\0' }, done_date_c_str[11] = { '\0' };
    struct tm tm_tmp;
    Octstr *msgid, *msgid2, *dlr_status, *dlvrd;
    /* split variables */
    unsigned long msg_sequence, msg_count;
    unsigned long submit_date;
    int max_msgs;
    Octstr *header, *footer, *suffix, *split_chars;
    Msg *msg2;
    
    pdu = smpp_pdu_create(deliver_sm,
    	    	    	  counter_increase(box->smpp_pdu_counter));

    pdu->u.deliver_sm.source_addr = octstr_duplicate(msg->sms.sender);
    pdu->u.deliver_sm.destination_addr = octstr_duplicate(msg->sms.receiver);

    /* Set the service type of the outgoing message. We'll use the config 
     * directive as default and 'binfo' as specific parameter. */
    pdu->u.deliver_sm.service_type = octstr_len(msg->sms.binfo) ? 
        octstr_duplicate(msg->sms.binfo) : octstr_duplicate(box->service_type);

    /* Check for manual override of source ton and npi values */
    if(box->source_addr_ton > -1 && box->source_addr_npi > -1) {
        pdu->u.deliver_sm.source_addr_ton = box->source_addr_ton;
        pdu->u.deliver_sm.source_addr_npi = box->source_addr_npi;
        debug("bb.sms.smpp", 0, "SMPP[%s]: Manually forced source addr ton = %ld, source add npi = %ld",
              octstr_get_cstr(box->boxc_id), box->source_addr_ton,
              box->source_addr_npi);
    } else {
        /* setup default values */
        pdu->u.deliver_sm.source_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */
        pdu->u.deliver_sm.source_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */
    }

    if (box->autodetect_addr) {
        /* lets see if its international or alphanumeric sender */
        if (octstr_get_char(pdu->u.deliver_sm.source_addr, 0) == '+') {
            if (!octstr_check_range(pdu->u.deliver_sm.source_addr, 1, 256, gw_isdigit)) {
                pdu->u.deliver_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC; /* alphanum */
                pdu->u.deliver_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN;    /* short code */
            } else {
               /* numeric sender address with + in front -> international (remove the +) */
               octstr_delete(pdu->u.deliver_sm.source_addr, 0, 1);
               pdu->u.deliver_sm.source_addr_ton = GSM_ADDR_TON_INTERNATIONAL;
    	    }
        } else {
            if (!octstr_check_range(pdu->u.deliver_sm.source_addr,0, 256, gw_isdigit)) {
                pdu->u.deliver_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC;
                pdu->u.deliver_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN;
            }
        }
    }

    /* Check for manual override of destination ton and npi values */
    if (box->dest_addr_ton > -1 && box->dest_addr_npi > -1) {
        pdu->u.deliver_sm.dest_addr_ton = box->dest_addr_ton;
        pdu->u.deliver_sm.dest_addr_npi = box->dest_addr_npi;
        debug("bb.sms.smpp", 0, "SMPP[%s]: Manually forced dest addr ton = %ld, dest add npi = %ld",
              octstr_get_cstr(box->boxc_id), box->dest_addr_ton,
              box->dest_addr_npi);
    } else {
        pdu->u.deliver_sm.dest_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */
        pdu->u.deliver_sm.dest_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */
    }

    /*
     * if its a international number starting with +, lets remove the
     * '+' and set number type to international instead
     */
    if (octstr_get_char(pdu->u.deliver_sm.destination_addr,0) == '+') {
        octstr_delete(pdu->u.deliver_sm.destination_addr, 0,1);
        pdu->u.deliver_sm.dest_addr_ton = GSM_ADDR_TON_INTERNATIONAL;
    }

    /* check length of src/dst address */
    if (octstr_len(pdu->u.deliver_sm.destination_addr) > 20 ||
        octstr_len(pdu->u.deliver_sm.source_addr) > 20) {
        smpp_pdu_destroy(pdu);
        gwlist_destroy(pdulist, NULL);
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
        pdu->u.deliver_sm.data_coding = fields_to_dcs(msg, 1); /* force alt_dcs */
    else
        pdu->u.deliver_sm.data_coding = fields_to_dcs(msg,
            (msg->sms.alt_dcs != SMS_PARAM_UNDEFINED ?
             msg->sms.alt_dcs : box->alt_dcs));

    /* set protocol id */
    if(msg->sms.pid != SMS_PARAM_UNDEFINED)
        pdu->u.deliver_sm.protocol_id = msg->sms.pid;

    /*
     * set the esm_class field
     * default is store and forward, plus udh and rpi if requested
     */
    pdu->u.deliver_sm.esm_class = 0;
    if (octstr_len(msg->sms.udhdata))
        pdu->u.deliver_sm.esm_class = pdu->u.deliver_sm.esm_class |
            ESM_CLASS_SUBMIT_UDH_INDICATOR;
    if (msg->sms.rpi > 0)
        pdu->u.deliver_sm.esm_class = pdu->u.deliver_sm.esm_class |
            ESM_CLASS_SUBMIT_RPI;

    /* Is this a delivery report? */
    if (msg->sms.sms_type == report_mo) {
	pdu->u.deliver_sm.esm_class |= ESM_CLASS_DELIVER_SMSC_DELIVER_ACK;
	dlrtype = msg->sms.dlr_mask;
	parts = octstr_split(msg->sms.dlr_url, octstr_imm(";"));
	msgid = gwlist_extract_first(parts);
	dlr = dlr_find(box->boxc_id, msgid, msg->sms.receiver, dlrtype, 0);
	if (dlr == NULL) {
		/* we could not find a corresponding dlr; nothing to send */
		smpp_pdu_destroy(pdu);
		gwlist_destroy(pdulist, NULL);
		octstr_destroy(msgid);
		gwlist_destroy(parts, octstr_destroy_item);
		return NULL;
	}
	dlvrd = octstr_imm("000");
	switch (dlrtype) {
	case DLR_UNDEFINED:
	case DLR_NOTHING:
		dlr_state = 8;
		dlr_status = octstr_imm("REJECTD");
		break;
	case DLR_SUCCESS:
		dlr_state = 2;
		dlr_status = octstr_imm("DELIVRD");
		dlvrd = octstr_imm("001");
		break;
	case DLR_BUFFERED:
		dlr_state = 6;
		dlr_status = octstr_imm("ACCEPTD");
		break;
	case DLR_SMSC_SUCCESS:
		/* please note that this state does not quite conform to the SMMP v3.4 spec */
		dlr_state = 0;
		dlr_status = octstr_imm("BUFFRED");
		break;
	case DLR_FAIL:
	case DLR_SMSC_FAIL:
		dlr_state = 5;
		dlr_status = octstr_imm("UNDELIV");
		break;
	}

	text = octstr_get_cstr(msg->sms.msgdata);

	tmps = strstr(text, "err:");
	if (tmps != NULL) {
	    /* we can't use 0-padding with %s, if this is really required,
	     * then convert the numeric string to a real integer. - st */
		snprintf(err, sizeof(err), "%3.3s", tmps + (4 * sizeof(char)));
		tmps = strstr(tmps, " ");
		text = tmps ? tmps + (1 * sizeof(char)) : "";
	}

	tmps = strstr(text, "text:");
	if (tmps != NULL) {
		text = tmps + (5 * sizeof(char));
	}

	/* restore original submission date from service */
	submit_date = 0;
	if (octstr_len(dlr->sms.service) > 0) {
		sscanf(octstr_get_cstr(dlr->sms.service), "%ld", &submit_date);
	}
	if (!submit_date || submit_date > dlr->sms.time) {
		submit_date = msg->sms.time;
	}

	tm_tmp = gw_localtime(submit_date);
	gw_strftime(submit_date_c_str, sizeof(submit_date_c_str), "%y%m%d%H%M", &tm_tmp);

	tm_tmp = gw_localtime(dlr->sms.time);
	gw_strftime(done_date_c_str, sizeof(done_date_c_str), "%y%m%d%H%M", &tm_tmp);

	/* the msgids are in dlr->dlr_url as reported by Victor Luchitz */
	gwlist_destroy(parts, octstr_destroy_item);
	parts = octstr_split(dlr->sms.dlr_url, octstr_imm(";"));
	octstr_destroy(gwlist_extract_first(parts));
	if (gwlist_len(parts) > 0) {
		while ((msgid2 = gwlist_extract_first(parts)) != NULL) {
			debug("opensmppbox", 0, "DLR for multipart message: sending %s.", octstr_get_cstr(msgid2));
			pdu2 = smpp_pdu_create(deliver_sm, counter_increase(box->smpp_pdu_counter));
			pdu2->u.deliver_sm.esm_class = pdu->u.deliver_sm.esm_class;
			pdu2->u.deliver_sm.source_addr_ton = pdu->u.deliver_sm.source_addr_ton;
			pdu2->u.deliver_sm.source_addr_npi = pdu->u.deliver_sm.source_addr_npi;
			pdu2->u.deliver_sm.dest_addr_ton = pdu->u.deliver_sm.dest_addr_ton;
			pdu2->u.deliver_sm.dest_addr_npi = pdu->u.deliver_sm.dest_addr_npi;
			pdu2->u.deliver_sm.data_coding = pdu->u.deliver_sm.data_coding;
			pdu2->u.deliver_sm.protocol_id = pdu->u.deliver_sm.protocol_id;
			pdu2->u.deliver_sm.source_addr = octstr_duplicate(pdu->u.deliver_sm.source_addr);
			pdu2->u.deliver_sm.destination_addr = octstr_duplicate(pdu->u.deliver_sm.destination_addr);
			pdu2->u.deliver_sm.service_type = octstr_duplicate(pdu->u.deliver_sm.service_type);
			if (box->version > 0x33) {
				pdu2->u.deliver_sm.receipted_message_id = octstr_duplicate(msgid2);
				pdu2->u.deliver_sm.message_state = dlr_state;
				dict_destroy(pdu2->u.deliver_sm.tlv);
				pdu2->u.deliver_sm.tlv = meta_data_get_values(msg->sms.meta_data, "smpp");
			}
			pdu2->u.deliver_sm.short_message = octstr_format("id:%S sub:001 dlvrd:%S submit date:%s done date:%s stat:%S err:%s text:%12s", msgid2, dlvrd, submit_date_c_str, done_date_c_str, dlr_status, err, text);
			octstr_destroy(msgid2);
			gwlist_append(pdulist, pdu2);
		}
        	smpp_pdu_destroy(pdu);
	}
	else {
		if (box->version > 0x33) {
			pdu->u.deliver_sm.receipted_message_id = octstr_duplicate(msgid);
			pdu->u.deliver_sm.message_state = dlr_state;
			dict_destroy(pdu->u.deliver_sm.tlv);
			pdu->u.deliver_sm.tlv = meta_data_get_values(msg->sms.meta_data, "smpp");
		}
		pdu->u.deliver_sm.short_message = octstr_format("id:%S sub:001 dlvrd:%S submit date:%s done date:%s stat:%S err:%s text:%12s", msgid, dlvrd, submit_date_c_str, done_date_c_str, dlr_status, err, text);
		gwlist_append(pdulist, pdu);
	}
	octstr_destroy(msgid);
	msg_destroy(dlr);
	gwlist_destroy(parts, octstr_destroy_item);
	return pdulist;
    }
    else {
	/* ask for the delivery reports if needed */
	if (DLR_IS_SUCCESS_OR_FAIL(msg->sms.dlr_mask))
		pdu->u.deliver_sm.registered_delivery = 1;
	else if (DLR_IS_FAIL(msg->sms.dlr_mask) && !DLR_IS_SUCCESS(msg->sms.dlr_mask))
		pdu->u.deliver_sm.registered_delivery = 2;
    	/*
     	* set data segments and length
     	*/

    	pdu->u.deliver_sm.short_message = octstr_duplicate(msg->sms.msgdata);

    }


    /*
     * only re-encoding if using default smsc charset that is defined via
     * alt-charset in smsc group and if MT is not binary
     */
    if (msg->sms.coding == DC_7BIT || (msg->sms.coding == DC_UNDEF && octstr_len(msg->sms.udhdata))) {
        /* 
         * consider 3 cases: 
         *  a) data_coding 0xFX: encoding should always be GSM 03.38 charset 
         *  b) data_coding 0x00: encoding may be converted according to alt-charset 
         *  c) data_coding 0x00: assume GSM 03.38 charset if alt-charset is not defined
         */
        if ((pdu->u.deliver_sm.data_coding & 0xF0) ||
            (!box->alt_charset && pdu->u.deliver_sm.data_coding == 0)) {
            charset_utf8_to_gsm(pdu->u.deliver_sm.short_message);
        }
        else if (pdu->u.deliver_sm.data_coding == 0 && box->alt_charset) {
            /*
             * convert to the given alternative charset
             */
            if (charset_convert(pdu->u.deliver_sm.short_message, "ISO-8859-1",
                                octstr_get_cstr(box->alt_charset)) != 0)
                error(0, "Failed to convert msgdata from charset <%s> to <%s>, will send as is.",
                             "ISO-8859-1", octstr_get_cstr(box->alt_charset));
        }
    }

    /* prepend udh if present */
    if (octstr_len(msg->sms.udhdata)) {
        octstr_insert(pdu->u.deliver_sm.short_message, msg->sms.udhdata, 0);
    }

    pdu->u.deliver_sm.sm_length = octstr_len(pdu->u.deliver_sm.short_message);

    /* set priority */
    if (msg->sms.priority >= 0 && msg->sms.priority <= 3)
        pdu->u.deliver_sm.priority_flag = msg->sms.priority;
    else
        pdu->u.deliver_sm.priority_flag = box->priority;

    /* set more messages to send */
/*
    if (box->version > 0x33 && msg->sms.msg_left > 0)
        pdu->u.deliver_sm.more_messages_to_send = 1;
*/

    header = NULL;
    footer = NULL;
    suffix = NULL;
    split_chars = NULL;
    catenate = 1;
    max_msgs = 255;
    if (catenate)
    	msg_sequence = counter_increase(catenated_sms_counter) & 0xFF;
    else
    	msg_sequence = 0;

    /* split sms */
    parts = sms_split(msg, header, footer, suffix, split_chars, catenate,
    	    	     msg_sequence, max_msgs, sms_max_length);
    msg_count = gwlist_len(parts);

    if ((msg_count > 1) && (box->version > 0x33)) {
        Octstr *use_message_payload_meta;
        long use_message_payload;

        use_message_payload_meta = meta_data_get_value(msg->sms.meta_data, "smpp", octstr_imm("use_message_payload"));
        use_message_payload = strtol(octstr_get_cstr(use_message_payload_meta), 0, 0);

        if (use_message_payload) {
            /* copy short message data to message_payload TLV */
            pdu->u.deliver_sm.message_payload = octstr_duplicate(pdu->u.deliver_sm.short_message);
            octstr_destroy(pdu->u.deliver_sm.short_message);
            pdu->u.deliver_sm.short_message = NULL;
            pdu->u.deliver_sm.sm_length = 0;

            /* pass the message as a single pdu */
            msg_count = 1;
        }
		
        octstr_destroy(use_message_payload_meta);
    }

    if (msg_count == 1) {
        /* don't create split_parts of sms fit into one */
        gwlist_destroy(parts, msg_destroy_item);
        parts = NULL;
    }

    debug("SMPP", 0, "message length %ld, sending %ld message%s",
        octstr_len(msg->sms.msgdata), msg_count, msg_count == 1 ? "" : "s");

    if (parts) {
        while((msg2 = gwlist_extract_first(parts)) != NULL) {
	        pdu2 = smpp_pdu_create(deliver_sm, counter_increase(box->smpp_pdu_counter));
	        pdu2->u.deliver_sm.source_addr_ton = pdu->u.deliver_sm.source_addr_ton;
	        pdu2->u.deliver_sm.source_addr_npi = pdu->u.deliver_sm.source_addr_npi;
	        pdu2->u.deliver_sm.dest_addr_ton = pdu->u.deliver_sm.dest_addr_ton;
	        pdu2->u.deliver_sm.dest_addr_npi = pdu->u.deliver_sm.dest_addr_npi;
	        pdu2->u.deliver_sm.data_coding = pdu->u.deliver_sm.data_coding;
	        pdu2->u.deliver_sm.protocol_id = pdu->u.deliver_sm.protocol_id;
	        pdu2->u.deliver_sm.source_addr = octstr_duplicate(pdu->u.deliver_sm.source_addr);
	        pdu2->u.deliver_sm.destination_addr = octstr_duplicate(pdu->u.deliver_sm.destination_addr);
	        pdu2->u.deliver_sm.service_type = octstr_duplicate(pdu->u.deliver_sm.service_type);

	        /* the following condition is currently always true */
	        /* uncomment in case we're doing a SAR-split instead */
	        if (/*octstr_len(msg2->sms.udhdata) > 0*/1) {
	            pdu2->u.deliver_sm.esm_class = pdu->u.deliver_sm.esm_class | ESM_CLASS_DELIVER_UDH_INDICATOR;
	            pdu2->u.deliver_sm.short_message = octstr_cat(msg2->sms.udhdata, msg2->sms.msgdata);
	        }
	        else {
	            pdu2->u.deliver_sm.short_message = octstr_duplicate(msg2->sms.msgdata);
	        }

	        if (box->version > 0x33) {
		    dict_destroy(pdu2->u.deliver_sm.tlv);
	            pdu2->u.deliver_sm.tlv = meta_data_get_values(msg->sms.meta_data, "smpp");
	        }

	        gwlist_append(pdulist, pdu2);
	        msg_destroy(msg2);
        }
		
        smpp_pdu_destroy(pdu);	
    }
    else {
        if (box->version > 0x33) {
	    dict_destroy(pdu->u.deliver_sm.tlv);
            pdu->u.deliver_sm.tlv = meta_data_get_values(msg->sms.meta_data, "smpp");
        }

        gwlist_append(pdulist, pdu);
    }

    return pdulist;
}

/*
 * Convert SMPP PDU to internal Msgs structure.
 * Return the Msg if all was fine and NULL otherwise, while getting 
 * the failing reason delivered back in *reason.
 * XXX semantical check on the incoming values can be extended here.
 */
static Msg *pdu_to_msg(Boxc *box, SMPP_PDU *pdu, long *reason)
{
    Msg *msg;
    int ton, npi;

    gw_assert(pdu->type == submit_sm);

    msg = msg_create(sms);
    gw_assert(msg != NULL);
    msg->sms.sms_type = mt_push;
    *reason = SMPP_ESME_ROK;

    /* 
     * Reset source addr to have a prefixed '+' in case we have an 
     * intl. TON to allow backend boxes (ie. smsbox) to distinguish
     * between national and international numbers.
     */
    ton = pdu->u.submit_sm.source_addr_ton;
    npi = pdu->u.submit_sm.source_addr_npi;
    /* check source addr */
    if ((*reason = convert_addr_from_pdu(box->boxc_id, pdu->u.submit_sm.source_addr, ton, npi)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.sender = pdu->u.submit_sm.source_addr;
    pdu->u.submit_sm.source_addr = NULL;
    msg->sms.service = octstr_duplicate(box->sms_service);

    /* 
     * Follows SMPP spec. v3.4. issue 1.2 
     * it's not allowed to have destination_addr NULL 
     */
    if (pdu->u.submit_sm.destination_addr == NULL) {
        error(0, "SMPP[%s]: Mallformed destination_addr `%s', may not be empty. "
                 "Discarding MO message.", octstr_get_cstr(box->boxc_id),
                     octstr_get_cstr(pdu->u.submit_sm.destination_addr));
        *reason = SMPP_ESME_RINVDSTADR;
        goto error;
    }

    /* copy priority_flag into msg */
    if (pdu->u.submit_sm.priority_flag >= 0 && pdu->u.submit_sm.priority_flag <= 3) {
	msg->sms.priority = pdu->u.submit_sm.priority_flag;
    }

    /* Same reset of destination number as for source */
    ton = pdu->u.submit_sm.dest_addr_ton;
    npi = pdu->u.submit_sm.dest_addr_npi;
    /* check destination addr */
    if ((*reason = convert_addr_from_pdu(box->boxc_id, pdu->u.submit_sm.destination_addr, ton, npi)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.receiver = pdu->u.submit_sm.destination_addr;
    pdu->u.submit_sm.destination_addr = NULL;

    /* SMSCs use service_type for billing information */
    msg->sms.binfo = pdu->u.submit_sm.service_type;
    pdu->u.submit_sm.service_type = NULL;

    if (pdu->u.submit_sm.esm_class & ESM_CLASS_SUBMIT_RPI)
        msg->sms.rpi = 1;

    /*
     * Check for message_payload if version > 0x33 and sm_length == 0
     * Note: SMPP spec. v3.4. doesn't allow to send both: message_payload & short_message!
     */
    if (box->version > 0x33 && pdu->u.submit_sm.sm_length == 0 && pdu->u.submit_sm.message_payload) {
        msg->sms.msgdata = pdu->u.submit_sm.message_payload;
        pdu->u.submit_sm.message_payload = NULL;
    }
    else {
        msg->sms.msgdata = pdu->u.submit_sm.short_message;
        pdu->u.submit_sm.short_message = NULL;
    }

    /*
     * Encode udh if udhi set
     * for reference see GSM03.40, section 9.2.3.24
     */
    if (pdu->u.submit_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
        int udhl;
        udhl = octstr_get_char(msg->sms.msgdata, 0) + 1;
        debug("bb.sms.smpp",0,"SMPP[%s]: UDH length read as %d", 
              octstr_get_cstr(box->boxc_id), udhl);
        if (udhl > octstr_len(msg->sms.msgdata)) {
            error(0, "SMPP[%s]: Mallformed UDH length indicator 0x%03x while message length "
                     "0x%03lx. Discarding MO message.", octstr_get_cstr(box->boxc_id),
                     udhl, octstr_len(msg->sms.msgdata));
            *reason = SMPP_ESME_RINVESMCLASS;
            goto error;
        }
        msg->sms.udhdata = octstr_copy(msg->sms.msgdata, 0, udhl);
        octstr_delete(msg->sms.msgdata, 0, udhl);
    }

    dcs_to_fields(&msg, pdu->u.submit_sm.data_coding);

    /* handle default data coding */
    switch (pdu->u.submit_sm.data_coding) {
        case 0x00: /* default SMSC alphabet */
            /*
             * try to convert from something interesting if specified so
             * unless it was specified binary, ie. UDH indicator was detected
             */
            if (box->alt_charset && msg->sms.coding != DC_8BIT) {
                if (charset_convert(msg->sms.msgdata, octstr_get_cstr(box->alt_charset), "ISO-8859-1") != 0)
                    error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
                             octstr_get_cstr(box->alt_charset), "ISO-8859-1");
                msg->sms.coding = DC_7BIT;
            } else { /* assume GSM 03.38 7-bit alphabet */
                charset_gsm_to_utf8(msg->sms.msgdata);
                msg->sms.coding = DC_7BIT;
            }
            break;
        case 0x01: /* ASCII or IA5 - not sure if I need to do anything */
        case 0x03: /* ISO-8859-1 - do nothing */
            msg->sms.coding = DC_7BIT; break;
        case 0x02: /* 8 bit binary - do nothing */
        case 0x04: /* 8 bit binary - do nothing */
            msg->sms.coding = DC_8BIT; break;
        case 0x05: /* JIS - what do I do with that ? */
            break;
        case 0x06: /* Cyrllic - iso-8859-5, I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-5", "UCS-2BE") != 0)
                error(0, "Failed to convert msgdata from cyrllic to UCS-2, will leave as is");
            msg->sms.coding = DC_UCS2; break;
        case 0x07: /* Hebrew iso-8859-8, I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-8", "UCS-2BE") != 0)
                error(0, "Failed to convert msgdata from hebrew to UCS-2, will leave as is");
            msg->sms.coding = DC_UCS2; break;
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
            if (msg->sms.coding == DC_UNDEF && pdu->u.submit_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR)
                msg->sms.coding = DC_8BIT;
            else if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) { /* assume GSM 7Bit , reencode */
                msg->sms.coding = DC_7BIT;
                charset_gsm_to_utf8(msg->sms.msgdata);
            }
    }
    msg->sms.pid = pdu->u.submit_sm.protocol_id;

    /* set priority flag */
    msg->sms.priority = pdu->u.submit_sm.priority_flag;

    /* ask for the delivery reports if needed */
    switch (pdu->u.submit_sm.registered_delivery & 0x03) {
    case 1:
	msg->sms.dlr_mask = (DLR_SUCCESS | DLR_FAIL | DLR_SMSC_FAIL);
	break;
    case 2:
	msg->sms.dlr_mask = (DLR_FAIL | DLR_SMSC_FAIL);
	break;
    default:
	msg->sms.dlr_mask = 0;
	break;
    }
    if (pdu->u.submit_sm.esm_class & (0x04|0x08)) {
	msg->sms.sms_type = report_mo;
    }

    if (box->version > 0x33) {
    	if (msg->sms.meta_data == NULL)
        	msg->sms.meta_data = octstr_create("");
    	meta_data_set_values(msg->sms.meta_data, pdu->u.submit_sm.tlv, "smpp", 1);
    }

    msg->sms.time = time(NULL);
    
    /* set validity period if needed */
    if (pdu->u.submit_sm.validity_period) {
        msg->sms.validity = time(NULL) + timestamp_to_minutes(pdu->u.submit_sm.validity_period) * 60;
    }

    
    /* set schedule delivery time if needed */
    if (pdu->u.submit_sm.schedule_delivery_time) {
        msg->sms.deferred = time(NULL) + timestamp_to_minutes(pdu->u.submit_sm.schedule_delivery_time) * 60;
    }

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
static Msg *data_sm_to_msg(Boxc *box, SMPP_PDU *pdu, long *reason)
{
    Msg *msg;
    int ton, npi;

    gw_assert(pdu->type == data_sm);

    msg = msg_create(sms);
    gw_assert(msg != NULL);
    msg->sms.sms_type = mt_push;
    *reason = SMPP_ESME_ROK;

    /* 
     * Reset source addr to have a prefixed '+' in case we have an 
     * intl. TON to allow backend boxes (ie. smsbox) to distinguish
     * between national and international numbers.
     */
    ton = pdu->u.data_sm.source_addr_ton;
    npi = pdu->u.data_sm.source_addr_npi;
    /* check source addr */
    if ((*reason = convert_addr_from_pdu(box->boxc_id, pdu->u.data_sm.source_addr, ton, npi)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.sender = pdu->u.data_sm.source_addr;
    pdu->u.data_sm.source_addr = NULL;

    /* 
     * Follows SMPP spec. v3.4. issue 1.2 
     * it's not allowed to have destination_addr NULL 
     */
    if (pdu->u.data_sm.destination_addr == NULL) {
        error(0, "SMPP[%s]: Mallformed destination_addr `%s', may not be empty. "
                 "Discarding MO message.", octstr_get_cstr(box->boxc_id),
                     octstr_get_cstr(pdu->u.data_sm.destination_addr));
        *reason = SMPP_ESME_RINVDSTADR;
        goto error;
    }

    /* Same reset of destination number as for source */
    ton = pdu->u.data_sm.dest_addr_ton;
    npi = pdu->u.data_sm.dest_addr_npi;
    /* check destination addr */
    if ((*reason = convert_addr_from_pdu(box->boxc_id, pdu->u.data_sm.destination_addr, ton, npi)) != SMPP_ESME_ROK)
        goto error;
    msg->sms.receiver = pdu->u.data_sm.destination_addr;
    pdu->u.data_sm.destination_addr = NULL;

    /* SMSCs use service_type for billing information */
    msg->sms.binfo = pdu->u.data_sm.service_type;
    pdu->u.data_sm.service_type = NULL;

    if (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_RPI)
        msg->sms.rpi = 1;

    msg->sms.msgdata = pdu->u.data_sm.message_payload;
    pdu->u.data_sm.message_payload = NULL;

    /*
     * Encode udh if udhi set
     * for reference see GSM03.40, section 9.2.3.24
     */
    if (pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
        int udhl;
        udhl = octstr_get_char(msg->sms.msgdata, 0) + 1;
        debug("bb.sms.smpp",0,"SMPP[%s]: UDH length read as %d", 
              octstr_get_cstr(box->boxc_id), udhl);
        if (udhl > octstr_len(msg->sms.msgdata)) {
            error(0, "SMPP[%s]: Mallformed UDH length indicator 0x%03x while message length "
                     "0x%03lx. Discarding MO message.", octstr_get_cstr(box->boxc_id),
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
             * unless it was specified binary, ie. UDH indicator was detected
             */
            if (box->alt_charset && msg->sms.coding != DC_8BIT) {
                if (charset_convert(msg->sms.msgdata, octstr_get_cstr(box->alt_charset), "ISO-8859-1") != 0)
                    error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
                             octstr_get_cstr(box->alt_charset), "ISO-8859-1");
                msg->sms.coding = DC_7BIT;
            } else { /* assume GSM 03.38 7-bit alphabet */
                charset_gsm_to_utf8(msg->sms.msgdata);
                msg->sms.coding = DC_7BIT;
            }
            break;
        case 0x01: /* ASCII or IA5 - not sure if I need to do anything */
        case 0x03: /* ISO-8859-1 - do nothing */
            msg->sms.coding = DC_7BIT; break;
        case 0x02: /* 8 bit binary - do nothing */
        case 0x04: /* 8 bit binary - do nothing */
            msg->sms.coding = DC_8BIT; break;
        case 0x05: /* JIS - what do I do with that ? */
            break;
        case 0x06: /* Cyrllic - iso-8859-5, I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-5", "UCS-2BE") != 0)
                error(0, "Failed to convert msgdata from cyrllic to UCS-2, will leave as is");
            msg->sms.coding = DC_UCS2; break;
        case 0x07: /* Hebrew iso-8859-8, I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-8", "UCS-2BE") != 0)
                error(0, "Failed to convert msgdata from hebrew to UCS-2, will leave as is");
            msg->sms.coding = DC_UCS2; break;
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
            if (msg->sms.coding == DC_UNDEF && pdu->u.data_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR)
                msg->sms.coding = DC_8BIT;
            else if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) { /* assume GSM 7Bit , reencode */
                msg->sms.coding = DC_7BIT;
                charset_gsm_to_utf8(msg->sms.msgdata);
            }
    }

    if (box->version > 0x33) {
    	if (msg->sms.meta_data == NULL)
        	msg->sms.meta_data = octstr_create("");
    	meta_data_set_values(msg->sms.meta_data, pdu->u.data_sm.tlv, "smpp", 1);
    }

    msg->sms.time = time(NULL);

    return msg;

error:
    msg_destroy(msg);
    return NULL;
}

Octstr *concat_msgids(Octstr *msgid, List *list)
{
	Octstr *ret = octstr_duplicate(msgid);
	int i;
	Msg *msg;

	for (i = 0; i < gwlist_len(list); i++) {
		msg = gwlist_get(list, i);
		octstr_append(ret, octstr_imm(";"));
		octstr_append(ret, msg->sms.dlr_url);
	}
	return ret;
}

void check_multipart(Boxc *box, Msg *msg, int *msg_to_send, Msg **msg2, List **parts_list)
{
	int reference, total;
	Octstr *key;

	if (msg->sms.udhdata && octstr_len(msg->sms.udhdata) == 6 && octstr_get_char(msg->sms.udhdata, 1) == 0) {
		/* We collect long messages as one and send them to bearerbox as a whole, so they can be sent
		   from the same smsc. */
		(*msg_to_send) = 0;
		debug("opensmppbox", 0, "assemble multi-part message.");
		reference = octstr_get_char(msg->sms.udhdata, 3);
		total = octstr_get_char(msg->sms.udhdata, 4);
		key = octstr_format("%S-%i", msg->sms.receiver, reference);
		(*parts_list) = dict_get(list_dict, key);
		if (NULL == (*parts_list)) {
			(*parts_list) = gwlist_create();
			dict_put(list_dict, key, (*parts_list));
		}
		debug("opensmppbox", 0, "received %ld of %d.", gwlist_len((*parts_list)) + 1, total);
		if ((gwlist_len((*parts_list)) + 1) == total) {
			debug("opensmppbox", 0, "received all parts of multi-part message.");
			gwlist_append((*parts_list), msg);
			/* assemble message */
			(*msg2) = catenate_msg((*parts_list), total);
			dict_put(list_dict, key, NULL);
			octstr_destroy(key);
			if (NULL == (*msg2)) {
				/* we could not assemble an appropiate message */
				debug("opensmppbox", 0, "Invalid multi-part message.");
				
			}
			else {
				Octstr *smsc_id = boxc_route_msg_to_smsc(box, *msg2);
				(*msg2)->sms.smsc_id = smsc_id ? octstr_duplicate(smsc_id) : NULL;
				(*msg2)->sms.boxc_id = octstr_duplicate(box->boxc_id);
				debug("opensmppbox", 0, "multi-part message, length: %ld.", octstr_len((*msg2)->sms.msgdata));
				(*msg_to_send) = 1;
			}
		}
		else {
			gwlist_append((*parts_list), msg);
			octstr_destroy(key);
		}
	}
}

static void handle_pdu(Connection *conn, Boxc *box, SMPP_PDU *pdu) {
	SMPP_PDU *resp = NULL;
	Msg *msg, *msg2, *mack;
	long reason;
	Octstr *msgid = NULL, *hold_service, *system_type;
	int msg_to_send = 1;
	List *parts_list = NULL;
	char id[UUID_STR_LEN + 1];

	dump_pdu("Got PDU:", box->boxc_id, pdu);
	switch (pdu->type) {
	case bind_transmitter:
	case bind_receiver:
	case bind_transceiver:
		break;
	default:
		if (!box->logged_in) {
			resp = smpp_pdu_create(generic_nack, pdu->u.generic_nack.sequence_number);
			resp->u.generic_nack.command_status = SMPP_ESME_RINVPASWD;
			goto error;
		}
		break;
	}
	switch (pdu->type) {
	case bind_transmitter:
		system_type = pdu->u.bind_transmitter.system_type ? pdu->u.bind_transmitter.system_type : octstr_imm("");
		if (check_login(box, pdu->u.bind_transmitter.system_id, pdu->u.bind_transmitter.password, system_type, SMPP_LOGIN_TRANSMITTER)) {
			box->logged_in = 1;
			box->version = pdu->u.bind_transmitter.interface_version;
			box->login_type = SMPP_LOGIN_TRANSMITTER;
			box->boxc_id = systemidisboxcid ? octstr_duplicate(pdu->u.bind_transmitter.system_id) : octstr_duplicate(system_type);
			box->sms_service = octstr_duplicate(pdu->u.bind_transmitter.system_id);
			identify_to_bearerbox(box);
			resp = smpp_pdu_create(bind_transmitter_resp, pdu->u.bind_transmitter.sequence_number);
			resp->u.bind_transmitter_resp.system_id = octstr_duplicate(our_system_id);
		}
		else {
			resp = smpp_pdu_create(bind_transmitter_resp, pdu->u.bind_transmitter_resp.sequence_number);
			resp->u.bind_transmitter.command_status = 0x0d; /* invalid login */
		}
		break;
	case bind_receiver:
		system_type = pdu->u.bind_receiver.system_type ? pdu->u.bind_receiver.system_type : octstr_imm("");
		if (check_login(box, pdu->u.bind_receiver.system_id, pdu->u.bind_receiver.password, system_type, SMPP_LOGIN_RECEIVER)) {
			box->logged_in = 1;
			box->version = pdu->u.bind_receiver.interface_version;
			box->login_type = SMPP_LOGIN_RECEIVER;
			box->boxc_id = systemidisboxcid ? octstr_duplicate(pdu->u.bind_transmitter.system_id) : octstr_duplicate(system_type);
			box->sms_service = octstr_duplicate(pdu->u.bind_receiver.system_id);
			identify_to_bearerbox(box);
			resp = smpp_pdu_create(bind_receiver_resp, pdu->u.bind_receiver.sequence_number);
			resp->u.bind_receiver_resp.system_id = octstr_duplicate(our_system_id);
		}
		else {
			resp = smpp_pdu_create(bind_receiver_resp, pdu->u.bind_receiver.sequence_number);
			resp->u.bind_receiver_resp.command_status = 0x0d; /* invalid login */
		}
		break;
	case bind_transceiver:
		system_type = pdu->u.bind_transceiver.system_type ? pdu->u.bind_transceiver.system_type : octstr_imm("");
		if (check_login(box, pdu->u.bind_transceiver.system_id, pdu->u.bind_transceiver.password, system_type, SMPP_LOGIN_TRANSCEIVER)) {
			box->logged_in = 1;
			box->version = pdu->u.bind_transceiver.interface_version;
			box->login_type = SMPP_LOGIN_TRANSCEIVER;
			box->boxc_id = systemidisboxcid ? octstr_duplicate(pdu->u.bind_transmitter.system_id) : octstr_duplicate(system_type);
			box->sms_service = octstr_duplicate(pdu->u.bind_transceiver.system_id);
			identify_to_bearerbox(box);
			resp = smpp_pdu_create(bind_transceiver_resp, pdu->u.bind_transceiver.sequence_number);
			resp->u.bind_transceiver_resp.system_id = octstr_duplicate(our_system_id);
		}
		else {
			resp = smpp_pdu_create(bind_transceiver_resp, pdu->u.bind_transceiver.sequence_number);
			resp->u.bind_transceiver_resp.command_status = 0x0d; /* invalid login */
		}
		break;
	case unbind:
		resp = smpp_pdu_create(unbind_resp, pdu->u.unbind.sequence_number);
		box->logged_in = 0;
		box->alive = 0;
		break;
	case enquire_link:
		resp = smpp_pdu_create(enquire_link_resp,
			pdu->u.enquire_link.sequence_number);
		break;
	case data_sm:
		msg = data_sm_to_msg(box, pdu, &reason);
		msg2 = msg;
		if (msg == NULL) {
			resp = smpp_pdu_create(generic_nack, pdu->u.data_sm.sequence_number);
			resp->u.generic_nack.command_status = SMPP_ESME_RUNKNOWNERR;
		}
		else {
			Octstr *smsc_id = boxc_route_msg_to_smsc(box, msg);
			check_multipart(box, msg, &msg_to_send, &msg2, &parts_list);
			msg->sms.smsc_id = smsc_id ? octstr_duplicate(smsc_id) : NULL;
			msg->sms.boxc_id = octstr_duplicate(box->boxc_id);
			msg_dump(msg, 0);
			resp = smpp_pdu_create(data_sm_resp, pdu->u.data_sm.sequence_number);
			msgid = generate_smppid(msg, box->version);
			msg->sms.dlr_url = octstr_duplicate(msgid);
			resp->u.data_sm_resp.message_id = msgid;
			if (msg_to_send) {
				if (DLR_IS_ENABLED(msg2->sms.dlr_mask)) {
					hold_service = msg2->sms.service;
					msg2->sms.service = octstr_format("%ld", msg2->sms.time);
					msgid = generate_smppid(msg2, box->version);
					if (parts_list) {
						msg2->sms.dlr_url = concat_msgids(msgid, parts_list);
					}
					dlr_add(box->boxc_id, msgid, msg2, 0);
					octstr_destroy(msgid);
					octstr_destroy(msg2->sms.service);
					msg2->sms.service = hold_service;
				}
				uuid_unparse(msg2->sms.id, id);
				msgid = octstr_create(id);
				dict_put(box->msg_acks, msgid, resp);
				resp = NULL;
				send_msg(box->bearerbox_connection, box, msg2);
				if (parts_list) {
					/* destroy values */
					gwlist_destroy(parts_list, msg_destroy_item);
				}
			}
		}
		break;
	case submit_sm:
		msg = pdu_to_msg(box, pdu, &reason);
		msg2 = msg;
		if (msg == NULL) {
			resp = smpp_pdu_create(generic_nack, pdu->u.submit_sm.sequence_number);
			resp->u.generic_nack.command_status = SMPP_ESME_RUNKNOWNERR;
		}
		else {
			Octstr *smsc_id = boxc_route_msg_to_smsc(box, msg);
			check_multipart(box, msg, &msg_to_send, &msg2, &parts_list);
			msg->sms.smsc_id = smsc_id ? octstr_duplicate(smsc_id) : NULL;
			msg->sms.boxc_id = octstr_duplicate(box->boxc_id);
			msg_dump(msg, 0);
			resp = smpp_pdu_create(submit_sm_resp, pdu->u.submit_sm.sequence_number);
			msgid = generate_smppid(msg, box->version);
			msg->sms.dlr_url = octstr_duplicate(msgid);
			resp->u.submit_sm_resp.message_id = msgid;
			if (msg_to_send) {
				if (DLR_IS_ENABLED(msg2->sms.dlr_mask)) {
					hold_service = msg2->sms.service;
					msg2->sms.service = octstr_format("%ld", msg2->sms.time);
					msgid = generate_smppid(msg2, box->version);
					if (parts_list) {
						msg2->sms.dlr_url = concat_msgids(msgid, parts_list);
					}
					dlr_add(box->boxc_id, msgid, msg2, 0);
					octstr_destroy(msgid);
					octstr_destroy(msg2->sms.service);
					msg2->sms.service = hold_service;
				}
				uuid_unparse(msg2->sms.id, id);
				msgid = octstr_create(id);
				dict_put(box->msg_acks, msgid, resp);
				octstr_destroy(msgid);
				resp = NULL;
				send_msg(box->bearerbox_connection, box, msg2);
				if (parts_list) {
					/* destroy values */
					gwlist_destroy(parts_list, msg_destroy_item);
				}
			}
		}
		break;
	case deliver_sm_resp:
		msgid = octstr_format("%ld", pdu->u.deliver_sm_resp.sequence_number);
		mack = dict_get(box->deliver_acks, msgid);
		if (mack) {
			msg = msg_duplicate(mack);
			/* TODO: ack_failed_tmp */
			if (pdu->u.deliver_sm_resp.command_status != 0) {
				msg->ack.nack = ack_failed;
			}
			send_msg(box->bearerbox_connection, box, msg);
			dict_put(box->deliver_acks, msgid, NULL);
		}
		octstr_destroy(msgid);
		break;
	case unbind_resp:
		box->logged_in = 0;
		box->alive = 0;
	default:
		error(0, "SMPP[%s]: Unknown PDU type 0x%08lx, ignored.",
			octstr_get_cstr(box->boxc_id), pdu->type);
		/*
		    send gnack , see smpp3.4 spec., section 3.3
		    because we doesn't know what kind of pdu received, we assume generick_nack_resp
		    (header always the same)
		*/
		resp = smpp_pdu_create(generic_nack, pdu->u.generic_nack.sequence_number);
		resp->u.generic_nack.command_status = SMPP_ESME_RINVCMDID;
		break;
	}
error:
	smpp_pdu_destroy(pdu);
	if (resp != NULL) {
		send_pdu(conn, box->boxc_id, resp);
		smpp_pdu_destroy(resp);
	}
}

/*
 *-------------------------------------------------
 *  sender thingies
 *-------------------------------------------------
 *
*/

static Boxc *boxc_create(int fd, Octstr *ip, int ssl)
{
    Boxc *boxc;

    boxc = gw_malloc(sizeof(Boxc));
    boxc->logged_in = 0;
    boxc->is_wap = 0;
    boxc->load = 0;
    boxc->smpp_connection = conn_wrap_fd(fd, ssl);
    boxc->id = counter_increase(boxid);
    boxc->client_ip = octstr_duplicate(ip);
    boxc->alive = 1;
    boxc->connect_time = time(NULL);
    boxc->boxc_id = NULL;
    boxc->routable = 0;
    boxc->smpp_pdu_counter = counter_create();
    boxc->alt_charset = NULL; /* todo: get from config */
    boxc->version = 0x33; /* default value, set upon receiving a bind */
    boxc->route_to_smsc = route_to_smsc ? octstr_duplicate(route_to_smsc) : NULL;
    boxc->msg_acks = dict_create(256, smpp_pdu_destroy_item);
    boxc->deliver_acks = dict_create(256, msg_destroy_item);

    boxc->service_type = NULL;

    boxc->source_addr_ton = smpp_source_addr_ton;
    boxc->source_addr_npi = smpp_source_addr_npi;
    boxc->autodetect_addr = smpp_autodetect_addr;
    boxc->dest_addr_ton = smpp_dest_addr_ton;
    boxc->dest_addr_npi = smpp_dest_addr_npi;

    boxc->alt_dcs = 0;
    boxc->validityperiod = -1;	
    boxc->priority = 0;
    boxc->mo_recode = 0;

    return boxc;
}

static void boxc_destroy(Boxc *boxc)
{
    if (boxc == NULL)
	    return;

    /* do nothing to the lists, as they are only references */

    if (boxc->smpp_connection)
	    conn_destroy(boxc->smpp_connection);
    if (boxc->bearerbox_connection)
	    conn_destroy(boxc->bearerbox_connection);
    if (boxc->boxc_id)
	    octstr_destroy(boxc->boxc_id);
    if (boxc->alt_charset)
	    octstr_destroy(boxc->alt_charset);
    counter_destroy(boxc->smpp_pdu_counter);
    if (boxc->route_to_smsc) {
	    octstr_destroy(boxc->route_to_smsc);
    }
    if (boxc->client_ip)
	    octstr_destroy(boxc->client_ip);
    dict_destroy(boxc->msg_acks);
    dict_destroy(boxc->deliver_acks);
    octstr_destroy(boxc->sms_service);
    gw_free(boxc);
}

static Octstr *boxc_route_msg_to_smsc(Boxc *box, Msg *msg)
{
	Octstr *os = NULL, *smsc_id;

	if (msg->sms.smsc_id != NULL)
		return msg->sms.smsc_id;

        char *receiver =  octstr_get_cstr(msg->sms.receiver);
        if ( (receiver) && (strlen(receiver) > 0) ) {
                smsc_id = dict_get(smsc_by_receiver, msg->sms.receiver);
        }
	else {
		receiver = "";
	}

	if (!smsc_id) {
	        os = octstr_format("%s:%s", octstr_get_cstr(msg->sms.sender),
		    octstr_get_cstr(box->boxc_id));
	        smsc_id = dict_get(smsc_by_sender_smsbox_id, os);
		octstr_destroy(os);
        };
	if (!smsc_id)
		smsc_id = dict_get(smsc_by_sender, msg->sms.sender);
	if (!smsc_id)
		smsc_id = dict_get(smsc_by_smsbox_id, box->boxc_id);
	if (!smsc_id)
		smsc_id = box->route_to_smsc;

	if (smsc_id)
		debug("opensmppbox", 0, "routed msg '%s' to smsc '%s'",
		receiver, octstr_get_cstr(smsc_id));

	return smsc_id;
}

/* ------------------------------------------------------------------
 * SMPP thingies
 * ------------------------------------------------------------------
*/

/* generally, SMPP connections are always non-encrypted. */
static Boxc *accept_smpp(int fd, int ssl)
{
    Boxc *newconn;
    Octstr *ip;

    int newfd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    client_addr_len = sizeof(client_addr);

    newfd = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (newfd < 0)
        return NULL;

    ip = host_ip(client_addr);

    newconn = boxc_create(newfd, ip, 0);

    /*
     * check if the SSL handshake was successfull, otherwise
     * this is no valid box connection any more
     */
#ifdef HAVE_LIBSSL
     if (ssl && !conn_get_ssl(newconn->smpp_connection))
        return NULL;
#endif

    info(0, "Client connected from <%s>", octstr_get_cstr(ip));
    octstr_destroy(ip);

    /* XXX TODO: do the hand-shake, baby, yeah-yeah! */

    return newconn;
}

static void smpp_to_bearerbox(void *arg)
{
    Boxc *box = arg;
    Connection *conn = box->smpp_connection;
    SMPP_PDU *pdu;
    long len;

    box->last_pdu_received = time(NULL);
    len = 0;
    while (smppbox_status == SMPP_RUNNING && box->alive) {
		switch (read_pdu(box, conn, &len, &pdu)) {
		case -1:
			error(0, "Invalid SMPP PDU received.");
			box->alive = 0;
			break;
		case 0:
			// idling
			if (time(NULL) - box->last_pdu_received > smpp_timeout) {
				box->alive = 0;
			}
	    		gwthread_sleep(1);
			break;
		case 1:
			box->last_pdu_received = time(NULL);
			handle_pdu(conn, box, pdu);
			break;
		}
    }
#ifdef HAVE_SHUTDOWN_CONNECTION
    shutdown_connection(box->bearerbox_connection);
#endif
}

/* if this login was made as a transmitter, then find the corresponding receiver connection */
static Boxc *find_receiver_box(Boxc *box)
{
	Boxc *thisbox;
	int cnt;

	if (box->login_type == SMPP_LOGIN_RECEIVER || box->login_type == SMPP_LOGIN_TRANSCEIVER) {
		return box;
	}
	for (cnt = 0; cnt < gwlist_len(all_boxes); cnt++) {
		thisbox = (Boxc *)gwlist_get(all_boxes, cnt);
		if ((thisbox->login_type == SMPP_LOGIN_RECEIVER || thisbox->login_type == SMPP_LOGIN_TRANSCEIVER) && (octstr_compare(thisbox->boxc_id, box->boxc_id) == 0) && thisbox->alive) {
			return thisbox;
		}
	}
	return box;
}

static void bearerbox_to_smpp(void *arg)
{
    Msg *msg, *mack;
    Boxc *box = arg;
    SMPP_PDU *pdu;
    List *pdulist;
    int dreport, errcode;
    Boxc *receiver_box;
    char id[UUID_STR_LEN + 1];
    Octstr *msgid;

    while (smppbox_status == SMPP_RUNNING && box->alive) {

	msg = read_from_box(box->bearerbox_connection, box);
        if (msg == NULL) {
	    if ((!box->alive) || conn_eof(box->bearerbox_connection)) {
            	/* tell opensmppbox to die */
	    	/* the client closes the connection, after that die in receiver */
	    	box->alive = 0;
	    }
	    continue;
        }
	if (msg_type(msg) == admin) {
	    if (msg->admin.command == cmd_shutdown) {
		info(0, "Bearerbox told us to die");
		box->alive = 0;
	    } else if (msg->admin.command == cmd_restart) {
		info(0, "Bearerbox told us to restart");
		restart = 1;
		box->alive = 0;
	    }
	}
        if (msg_type(msg) == heartbeat) {
	    // todo
            debug("opensmppbox", 0, "bearerbox_to_smpp: catch an heartbeat - we are alive");
            msg_destroy(msg);
            continue;
        }
	if (msg_type(msg) == ack) {
	    uuid_unparse(msg->ack.id, id);
	    msgid = octstr_create(id);
	    pdu = dict_get(box->msg_acks, msgid);
	    errcode = SMPP_ESME_RMSGQFUL; /* in case we get ack_failed_tmp */
	    if (pdu) {
		switch (msg->ack.nack) {
		case ack_buffered:
		case ack_success:
			/* we can send the submit_sm_resp as-is */
			break;
		case ack_failed:
			errcode = SMPP_ESME_RSUBMITFAIL;
			/* no break */
		case ack_failed_tmp:
			switch (pdu->type) {
			case submit_sm_resp:
				octstr_destroy(pdu->u.submit_sm_resp.message_id);
				pdu->u.submit_sm_resp.message_id = NULL;
				pdu->u.submit_sm_resp.command_status = errcode;
				break;
			case data_sm_resp:
				octstr_destroy(pdu->u.data_sm_resp.message_id);
				pdu->u.data_sm_resp.message_id = NULL;
				pdu->u.data_sm_resp.command_status = errcode;
				break;
			default:
				debug("opensmppbox", 0, "Getting failure ack on unexpected pdu: %s.", pdu->type_name);
				break;
			}
			break;
		default:
			debug("opensmppbox", 0, "Unknown ack.nack type: %ld.", msg->ack.nack);
			break;
		}
		send_pdu(box->smpp_connection, box->boxc_id, pdu);
		dict_put(box->msg_acks, msgid, NULL); /* also destroys item */
	    }
	    else {
		debug("opensmppbox", 0, "Ack to unknown message: %s.", id);
	    }
	    octstr_destroy(msgid);
	}
        if (!box->alive) {
		msg_destroy(msg);
		break;
	}
	if (msg_type(msg) == sms) {
		info(0, "We received an SMS message.");
		if (msg->sms.sms_type == report_mo)
			dreport = 1;
		else
			dreport = 0;
		/* Recode to iso-8859-1 the MO message if possible */
		if (box->mo_recode && msg->sms.coding == DC_UCS2) {
			int converted = 0;
			Octstr *text;

			text = octstr_duplicate(msg->sms.msgdata);
			if(0 == octstr_recode (octstr_imm("iso-8859-1"), octstr_imm("UTF-16BE"), text)) {
				if(octstr_search(text, octstr_imm("&#"), 0) == -1) {
					/* XXX I'm trying to search for &#xxxx; text, which indicates that the
					* text couldn't be recoded.
					* We should use other function to do the recode or detect it using
					* other method */
					info(0, "MO message converted from UCS-2 to ISO-8859-1");
					octstr_destroy(msg->sms.msgdata);
					msg->sms.msgdata = octstr_duplicate(text);
					msg->sms.charset = octstr_create("ISO-8859-1");
					msg->sms.coding = DC_7BIT;
					converted=1;
				} else {
					octstr_destroy(text);
	            			text = octstr_duplicate(msg->sms.msgdata);
				}
			}
			if(!converted && 0 == octstr_recode (octstr_imm("UTF-8"), octstr_imm("UTF-16BE"), text)) {
				if(octstr_search(text, octstr_imm("&#"), 0) == -1) {
					/* XXX I'm trying to search for &#xxxx; text, which indicates that the
					* text couldn't be recoded.
					* We should use other function to do the recode or detect it using
					* other method */
					info(0, "MO message converted from UCS-2 to UTF-8");
					octstr_destroy(msg->sms.msgdata);
					msg->sms.msgdata = octstr_duplicate(text);
					msg->sms.charset = octstr_create("UTF-8");
					msg->sms.coding = DC_7BIT;
					/* redundant, but this code could be used if another convertion is required
					converted=1;
				} else {
					octstr_destroy(text);
					text = octstr_duplicate(msg->sms.msgdata);
				*/
				}
			}
			octstr_destroy(text);
		}
		if (octstr_len(msg->sms.sender) == 0 ||
			octstr_len(msg->sms.receiver) == 0) {
			error(0, "smppbox_req_thread: no sender/receiver, dump follows:");
			msg_dump(msg, 0);
			/*
			* Send NACK to bearerbox, otherwise message remains in store file.
			*/
			mack = msg_create(ack);
			mack->ack.nack = ack_failed;
			mack->ack.time = msg->sms.time;
			uuid_copy(mack->ack.id, msg->sms.id);
			send_msg(box->bearerbox_connection, box, mack);

			msg_destroy(msg);
			continue;
		}
		/* create ack message to be sent afterwards */
		mack = msg_create(ack);
		mack->ack.nack = ack_success;
		mack->ack.time = msg->sms.time;
		uuid_copy(mack->ack.id, msg->sms.id);

		msgid = NULL;
		receiver_box = find_receiver_box(box);
		pdulist = msg_to_pdu(receiver_box, msg);
		if (pdulist != NULL) {
			while ((pdu = gwlist_extract_first(pdulist)) != NULL) {
				if (NULL == msgid) {
					/* Put ack in dict. We will send it as soon as we received a deliver_sm_resp */
					msgid = octstr_format("%ld", pdu->u.deliver_sm.sequence_number);
					dict_put(receiver_box->deliver_acks, msgid, mack);
				}
				send_pdu(receiver_box->smpp_connection, box->boxc_id, pdu);
				smpp_pdu_destroy(pdu);
			}
			if (msgid)
				octstr_destroy(msgid);
			gwlist_destroy(pdulist, NULL);
		}
		else {
			/* Send NACK to bearerbox, otherwise message remains in store file. */
			warning(0, "msg_to_pdu failed, sending negative ack");
			mack->ack.nack = ack_failed;
			send_msg(box->bearerbox_connection, box, mack);
		}		
	}
        msg_destroy(msg);
    }
}

static void run_smppbox(void *arg)
{
    int fd;
    Boxc *newconn;
    long sender;

    fd = (int)arg;
    newconn = accept_smpp(fd, 0);
    if (newconn == NULL) {
	    panic(0, "Socket accept failed");
	    return;
    }
    newconn->boxc_id = octstr_duplicate(smppbox_id);
    newconn->bearerbox_connection = connect_to_bearerbox_real(bearerbox_host, bearerbox_port, bearerbox_port_ssl, NULL /* bb_our_host */);
	/* XXX add our_host if required */
    if (newconn->bearerbox_connection == NULL) {
	    error(0, "opensmppbox: Failed to connect to bearerbox." );
	    boxc_destroy(newconn);
	    return;
    }

    gwlist_append(all_boxes, newconn);

#ifdef DO_HEARTBEATS
    /* we dont do heartbeats for now */
    if (0 > heartbeat_start(write_to_bearerboxes, DEFAULT_HEARTBEAT,
				       outstanding_requests)) {
        info(0, "OpenSMPPBox: Could not start heartbeat.");
    }
#endif

    sender = gwthread_create(smpp_to_bearerbox, newconn);
    if (sender == -1) {
	    error(0, "Failed to start a new thread, disconnecting client <%s>",
	          octstr_get_cstr(newconn->client_ip));
    		boxc_destroy(newconn);
	    return;
    }
    bearerbox_to_smpp(newconn);
    gwthread_join(sender);
    gwlist_delete_equal(all_boxes, newconn);
    boxc_destroy(newconn);
}

static void wait_for_connections(int fd, void (*function) (void *arg), 
    	    	    	    	 List *waited)
{
    int ret = 0;
    int timeout = 10; /* 10 sec. */

    gw_assert(function != NULL);
    
    while(smppbox_status == SMPP_RUNNING) {

            ret = gwthread_pollfd(fd, POLLIN, 1.0);
	    if (smppbox_status == SMPP_SHUTDOWN) {
	        if (ret == -1 || !timeout)
                    break;
                else
                    timeout--;
	    }

	    if (ret > 0) {
	        gwthread_create(function, (void *)fd);
	        gwthread_sleep(1.0);
	    } else if (ret < 0) {
	        if(errno==EINTR) continue;
	        if(errno==EAGAIN) continue;
	        error(errno, "wait_for_connections failed");
	    }
    }
}

static void smppboxc_run(void *arg)
{
    int fd;
    int port;

    port = (int)arg;
    
    fd = make_server_socket(port, NULL); 
    	/* XXX add interface_name if required */

    if (fd < 0) {
	    panic(0, "Could not open opensmppbox port %d", port);
    }

    /*
     * infinitely wait for new connections;
     * to shut down the system, SIGTERM is send and then
     * select drops with error, so we can check the status
     */

    info(0, "Waiting for SMPP connections on port %d.", port);
    wait_for_connections(fd, run_smppbox, NULL);
    info(0, "No more waiting for SMPP connections.");

    /* close listen socket */
    close(fd);
}



/***********************************************************************
 * Main program. Configuration, signal handling, etc.
 */

static void signal_handler(int signum) {
    /* On some implementations (i.e. linuxthreads), signals are delivered
     * to all threads.  We only want to handle each signal once for the
     * entire box, and we let the gwthread wrapper take care of choosing
     * one.
     */
    if (!gwthread_shouldhandlesignal(signum))
        return;

    switch (signum) {
        case SIGINT:

       	    if (smppbox_status == SMPP_RUNNING) {
                error(0, "SIGINT received, aborting program...");
		smppbox_status = SMPP_SHUTDOWN;
            }
            break;

        case SIGHUP:
            warning(0, "SIGHUP received, catching and re-opening logs");
            log_reopen();
            alog_reopen();
            break;

        /* 
         * It would be more proper to use SIGUSR1 for this, but on some
         * platforms that's reserved by the pthread support. 
         */
        case SIGQUIT:
	       warning(0, "SIGQUIT received, reporting memory usage.");
	       gw_check_leaks();
	       break;
    }
}


static void setup_signal_handlers(void) {
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGPIPE, &act, NULL);
}



static void gw_smpp_enter(Cfg *cfg)
{
}

static void gw_smpp_leave()
{
}

static void init_smsc_routes(Cfg *cfg)
{
    CfgGroup *grp;
    List *list, *items;
    Octstr *smsc_id, *boxc_ids, *shortcodes, *receiver_shortcodes;
    int i, j;

    smsc_by_receiver = dict_create(1000, (void(*)(void *)) octstr_destroy);
    smsc_by_smsbox_id = dict_create(30, (void(*)(void *)) octstr_destroy);
    smsc_by_sender = dict_create(50, (void(*)(void *)) octstr_destroy);
    smsc_by_sender_smsbox_id = dict_create(50, (void(*)(void *)) octstr_destroy);

    smsc_id = boxc_ids = shortcodes = receiver_shortcodes = NULL;
    list = items = NULL;

    list = cfg_get_multi_group(cfg, octstr_imm("smsc-route"));

    /* loop multi-group "client-route" */
    while (list && (grp = gwlist_extract_first(list)) != NULL) {
        if ((smsc_id = cfg_get(grp, octstr_imm("smsc-id"))) == NULL) {
            grp_dump(grp);
            panic(0,"'smsc-id-route' group without valid 'smsc-id' directive!");
        }

        /*
         * If smsbox-id is given, then any message coming from the specified
         * smsbox-id in the list will be routed to this smsc.
         * If shortcode is given, then any message with sender number
         * matching those will be routed to this smsc.
         * If both are given, then only sender within shortcode originating
         * from sysmtem-id list will be routed to this smsc. So if both are 
         * present then this is a logical AND operation.
         */
        boxc_ids = cfg_get(grp, octstr_imm("smsbox-id"));
        shortcodes = cfg_get(grp, octstr_imm("shortcode"));
        receiver_shortcodes = cfg_get(grp, octstr_imm("receiver-shortcode"));

        /* Consider the receiver options: receiver-shortcode. */
        if (receiver_shortcodes) {
            /* receiver-shortcode applies to all MTs from all smscs */
            items = octstr_split(receiver_shortcodes, octstr_imm(";"));
            for (i = 0; i < gwlist_len(items); i++) {
                Octstr *item = gwlist_get(items, i);
                octstr_strip_blanks(item);

                debug("opensmppbox",0,"Adding smsc routing to id <%s> for receiver no <%s>",
                      octstr_get_cstr(smsc_id), octstr_get_cstr(item));

                if (!dict_put_once(smsc_by_receiver, item, octstr_duplicate(smsc_id)))
                    panic(0, "Routing for receiver no <%s> already exists!",
                          octstr_get_cstr(item));
            }
            gwlist_destroy(items, octstr_destroy_item);
        };

        /* consider now the 3 possibilities: */
        if (boxc_ids && !shortcodes) {
            /* smsbox-id only, so all MT traffic */
            items = octstr_split(boxc_ids, octstr_imm(";"));
            for (i = 0; i < gwlist_len(items); i++) {
                Octstr *item = gwlist_get(items, i);
                octstr_strip_blanks(item);

                debug("opensmppbox",0,"Adding smsc routing to id <%s> for box id <%s>",
                      octstr_get_cstr(smsc_id), octstr_get_cstr(item));

                if (!dict_put_once(smsc_by_smsbox_id, item, octstr_duplicate(smsc_id)))
                    panic(0, "Routing for box-id <%s> already exists!",
                          octstr_get_cstr(item));
            }
            gwlist_destroy(items, octstr_destroy_item);
            octstr_destroy(boxc_ids);   
        }
        else if (!boxc_ids && shortcodes) {
            /* shortcode only, so these MTs from all smscs */
            items = octstr_split(shortcodes, octstr_imm(";"));
            for (i = 0; i < gwlist_len(items); i++) {
                Octstr *item = gwlist_get(items, i);
                octstr_strip_blanks(item);

                debug("opensmppbox",0,"Adding smsc routing to id <%s> for sender no <%s>",
                      octstr_get_cstr(smsc_id), octstr_get_cstr(item));

                if (!dict_put_once(smsc_by_sender, item, octstr_duplicate(smsc_id)))
                    panic(0, "Routing for sender no <%s> already exists!",
                          octstr_get_cstr(item));
            }
            gwlist_destroy(items, octstr_destroy_item);
            octstr_destroy(shortcodes);
        }
        else if (boxc_ids && shortcodes) {
            /* both, so only specified MTs from specified smsbox ids */
            items = octstr_split(shortcodes, octstr_imm(";"));
            for (i = 0; i < gwlist_len(items); i++) {
                List *subitems;
                Octstr *item = gwlist_get(items, i);
                octstr_strip_blanks(item);
                subitems = octstr_split(boxc_ids, octstr_imm(";"));
                for (j = 0; j < gwlist_len(subitems); j++) {
                    Octstr *subitem = gwlist_get(subitems, j);
                    octstr_strip_blanks(subitem);

                    debug("opensmppbox",0,"Adding smsc routing to id <%s> "
                          "for sender no <%s> and smsbox id <%s>",
                          octstr_get_cstr(smsc_id), octstr_get_cstr(item),
                          octstr_get_cstr(subitem));

                    /* construct the dict key '<shortcode>:<smsbox-id>' */
                    octstr_insert(subitem, item, 0);
                    octstr_insert_char(subitem, octstr_len(item), ':');
                    if (!dict_put_once(smsc_by_sender_smsbox_id, subitem, octstr_duplicate(smsc_id)))
                        panic(0, "Routing for sender:smsbox-id <%s> already exists!",
                              octstr_get_cstr(subitem));
                }
                gwlist_destroy(subitems, octstr_destroy_item);
            }
            gwlist_destroy(items, octstr_destroy_item);
            octstr_destroy(shortcodes);
        }
        octstr_destroy(smsc_id);
        octstr_destroy(boxc_ids);
        octstr_destroy(shortcodes);
    }

    gwlist_destroy(list, NULL);
}

static void destroy_smsc_routes(void)
{
    dict_destroy(smsc_by_receiver);
    smsc_by_receiver = NULL;

    dict_destroy(smsc_by_smsbox_id);
    smsc_by_smsbox_id = NULL;

    dict_destroy(smsc_by_sender);
    smsc_by_sender = NULL;

    dict_destroy(smsc_by_sender_smsbox_id);
    smsc_by_sender_smsbox_id = NULL;
}

static void init_smppbox(Cfg *cfg)
{
	CfgGroup *grp;
	Octstr *logfile;
	long lvl;

	/* some default values */
	smppbox_port = 13005;
	smppbox_port_ssl = 0;
	bearerbox_host = NULL;
	bearerbox_port_ssl = 0;
	logfile = NULL;
	lvl = 0;
	systemidisboxcid = 0; /* default backward compatible */
	enablepam = 0; /* also default false */

	/* init dlr storage */
	dlr_init(cfg);

	/* initialize low level PDUs */
	if (smpp_pdu_init(cfg) == -1)
    	    panic(0, "Connot start with PDU init failed.");

	/*
	 * first we take the port number in bearerbox and other values from the
	 * opensmppbox group in configuration file
	*/

	grp = cfg_get_single_group(cfg, octstr_imm("opensmppbox"));
	if (grp == NULL)
		panic(0, "No 'opensmppbox' group in configuration");

	bearerbox_host = cfg_get(grp, octstr_imm("bearerbox-host"));
	if (!bearerbox_host) {
		bearerbox_host = octstr_create(BB_DEFAULT_HOST);
	}
	if (cfg_get_integer(&bearerbox_port, grp, octstr_imm("bearerbox-port")) == -1)
		bearerbox_port = BB_DEFAULT_SMSBOX_PORT;
#ifdef HAVE_LIBSSL
#if 0
	cfg_get_bool(&bearerbox_port_ssl, grp, octstr_imm("bearerbox-port-ssl"));
	conn_config_ssl(grp);
#endif
#endif 

	smppbox_id = cfg_get(grp, octstr_imm("opensmppbox-id"));
	our_system_id = cfg_get(grp, octstr_imm("our-system-id"));
	route_to_smsc = cfg_get(grp, octstr_imm("route-to-smsc"));
	if (our_system_id == NULL) {
		panic(0, "our-system-id is not set.");
	}

	/* setup logfile stuff */
	logfile = cfg_get(grp, octstr_imm("log-file"));

	cfg_get_integer(&lvl, grp, octstr_imm("log-level"));

	if (cfg_get_integer(&smppbox_port, grp, octstr_imm("opensmppbox-port")) == -1)
		smppbox_port = 2345;

	smpp_logins = cfg_get(grp, octstr_imm("smpp-logins"));

	if (smpp_logins == NULL) {
		panic(0, "No user file specified.");
	}

	if (logfile != NULL) {
		info(0, "Starting to log to file %s level %ld", 
			octstr_get_cstr(logfile), lvl);
		log_open(octstr_get_cstr(logfile), lvl, GW_NON_EXCL);
		octstr_destroy(logfile);
	}

	if (cfg_get_integer(&smpp_timeout, grp, octstr_imm("timeout")) == -1)
		smpp_timeout = TIMEOUT_SECONDS;
	if (cfg_get_integer(&smpp_source_addr_ton, grp, octstr_imm("source-addr-ton")) == -1)
		smpp_source_addr_ton = -1;
	if (cfg_get_integer(&smpp_source_addr_npi, grp, octstr_imm("source-addr-npi")) == -1)
		smpp_source_addr_npi = -1;
	if (cfg_get_bool(&smpp_autodetect_addr, grp, octstr_imm("source-addr-auto")) == -1)
		smpp_autodetect_addr = 0;
	if (cfg_get_integer(&smpp_dest_addr_ton, grp, octstr_imm("dest-addr-ton")) == -1)
		smpp_dest_addr_ton = -1;
	if (cfg_get_integer(&smpp_dest_addr_npi, grp, octstr_imm("dest-addr-npi")) == -1)
		smpp_dest_addr_npi = -1;

	cfg_get_bool(&systemidisboxcid, grp, octstr_imm("use-systemid-as-smsboxid"));
	cfg_get_bool(&enablepam, grp, octstr_imm("enable-pam"));
	pamacl = cfg_get(grp, octstr_imm("pam-acl"));
	if (NULL == pamacl) {
		pamacl = octstr_create("kannel");
	}
	if (enablepam && !systemidisboxcid) {
		panic(0, "enable-pam requires systemid-is-boxcid=true.");
	}
#ifndef HAVE_PAM
	if (enablepam) {
		panic(0, "enable-pam is set but we are compiled without pam support.");
	}
#endif
	if (enablepam) {
		info(0, "Using PAM authentication.");
	}

	init_smsc_routes(cfg);

	catenated_sms_counter = counter_create();
        boxid = counter_create();
	gw_smpp_enter(cfg);

	smppbox_status = SMPP_RUNNING;
}

static int check_args(int i, int argc, char **argv) {
    if (strcmp(argv[i], "-H")==0 || strcmp(argv[i], "--tryhttp")==0) {
	//only_try_http = 1;
    } else
	return -1;

    return 0;
} 

/*
 * Adding hooks to kannel check config
 *
 * Martin Conte.
 */

static int smppbox_is_allowed_in_group(Octstr *group, Octstr *variable)
{
    Octstr *groupstr;

    groupstr = octstr_imm("group");

    #define OCTSTR(name) \
        if (octstr_compare(octstr_imm(#name), variable) == 0) \
        return 1;
    #define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
    #define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
    #include "opensmppbox-cfg.def"

    return 0;
}

#undef OCTSTR
#undef SINGLE_GROUP
#undef MULTI_GROUP

static int smppbox_is_single_group(Octstr *query)
{
    #define OCTSTR(name)
    #define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 1;
    #define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 0;
    #include "opensmppbox-cfg.def"
    return 0;
}

int main(int argc, char **argv)
{
	int cf_index;
	Octstr *filename, *version;

	gwlib_init();
	all_boxes = gwlist_create();
	list_dict = dict_create(1, NULL);

	cf_index = get_and_set_debugs(argc, argv, check_args);
	setup_signal_handlers();

	if (argv[cf_index] == NULL)
		filename = octstr_create("opensmppbox.conf");
	else
		filename = octstr_create(argv[cf_index]);

	cfg = cfg_create(filename);

	/* Adding cfg-checks to core */

	cfg_add_hooks(smppbox_is_allowed_in_group, smppbox_is_single_group);

	if (cfg_read(cfg) == -1)
		panic(0, "Couldn't read configuration from `%s'.", octstr_get_cstr(filename));

	octstr_destroy(filename);

	version = octstr_format("opensmppbox version %s gwlib", GW_VERSION);
	report_versions(octstr_get_cstr(version));
	octstr_destroy(version);

	init_smppbox(cfg);

	smppboxc_run((void *)smppbox_port);

	/* shutdown dlr storage */
	heartbeat_stop(ALL_HEARTBEATS);
	dlr_shutdown();
	destroy_smsc_routes();
	counter_destroy(catenated_sms_counter);
	counter_destroy(boxid);

	if (restart_smppbox) {
		gwthread_sleep(1.0);
	}

	gwlist_destroy(all_boxes, NULL);
	gw_smpp_leave();
	gwlib_shutdown();

	if (restart_smppbox)
		execvp(argv[0], argv);
	return 0;
}
