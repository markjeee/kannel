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
 * smsc_p.h - private interface to SMS center subsystem
 *
 * Lars Wirzenius
 *
 * New API by Kalle Marjola 1999
 */

#ifndef SMSC_P_H
#define SMSC_P_H


#include <stddef.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

#include "smsc.h"

/*
 * List of SMS center types that we support.
 */
enum {
	SMSC_TYPE_DELETED,
	SMSC_TYPE_CIMD,
	SMSC_TYPE_EMI_X25,
	SMSC_TYPE_SEMA_X28,
	SMSC_TYPE_OIS,
};

/*
 * The implementation of the SMSCenter object. 
 */
#define DIAL_PREFIX_MAX_LEN 1024
struct SMSCenter {

	int type;
	int transport;
	
	char name[1024];
	int id;

        /* Routing prefixes (based on phone number) */
	char *preferred_prefix;
	char *allowed_prefix;
	char *denied_prefix;


	/* Alternative charset */
        long alt_charset;

	/* For locking/unlocking. */
	Mutex *mutex;

        /* for dying */
        volatile sig_atomic_t killed;

	/* General IO device */
	int socket;

	/* Maximum minutes idle time before ping is sent. 0 for no pings. */
	int keepalive;

	/* TCP/IP */
	char *hostname;
	int port;
        int receive_port; /* if used, with EMI 2.0/OIS 4.5 */
	
	/* PSTN/ISDN */
	char *phonenum;
	char *serialdevice;

	/* X.31 */
	char *x31_phonenum;
	char *x31_serialdevice;

	/* Unix pipes */
	char *pipe_command;

	/* CIMD */
	char *cimd_hostname;
	int cimd_port;
	char *cimd_username;
	char *cimd_password;
	time_t cimd_last_spoke;
	int cimd_config_bits;

	/* EMI_X25 */
	int emi_fd;
	FILE *emi_fp;
	char *emi_phonenum;
	char *emi_serialdevice;
	char *emi_hostname;
	int  emi_port;
	char *emi_username;
	char *emi_password;
	int emi_current_msg_number;
	time_t emi_last_spoke;
	int emi_backup_fd;
        int emi_backup_port;		/* different one! rename! */
        char *emi_backup_allow_ip;     
        int emi_our_port;		/* port to bind us when connecting smsc */
        int emi_secondary_fd;

        /* SEMA SMS2000 OIS 4.5 X28 */

        char * sema_smscnua;
        char * sema_homenua;
        char * sema_serialdevice;
        struct sema_msglist *sema_mt, *sema_mo;
        int sema_fd;

        /* SEMA SMS2000 OIS 5.0 (TCP/IP to X.25 router) */

        time_t ois_alive;
        time_t ois_alive2;
        void *ois_received_mo;
        int ois_ack_debt;
        int ois_flags;
        int ois_listening_socket;
        int ois_socket;
        char *ois_buffer;
        size_t ois_bufsize;
        size_t ois_buflen;
        Octstr *sender_prefix;

	/* For buffering input. */
	char *buffer;
	size_t bufsize;
	size_t buflen;
};


/*
 * Operations on an SMSCenter object.
 */
SMSCenter *smscenter_construct(void);
void smscenter_destruct(SMSCenter *smsc);
int smscenter_read_into_buffer(SMSCenter *);
void smscenter_remove_from_buffer(SMSCenter *smsc, size_t n);

/* Send an SMS message via an SMS center. Return -1 for error,
   0 for OK. */
int smscenter_submit_msg(SMSCenter *smsc, Msg *msg);


/* Receive an SMS message from an SMS center. Return -1 for error,
   0 end of messages (other end closed their end of the connection),
   or 1 for a message was received. If a message was received, a 
   pointer to it is returned via `*msg'. Note that this operation
   blocks until there is a message. */
int smscenter_receive_msg(SMSCenter *smsc, Msg **msg);


/* Is there an SMS message pending from an SMS center? Return -1 for
   error, 0 for no, 1 for yes. This operation won't block, but may
   not be instantaneous, if it has to read a few characters to see
   if there is a message. Use smscenter_receive_smsmessage to actually receive
   the message. */
int smscenter_pending_smsmessage(SMSCenter *smsc);


/*
 * Interface to Nokia SMS centers using CIMD.
 */
SMSCenter *cimd_open(char *hostname, int port, char *username, char *password);
int cimd_reopen(SMSCenter *smsc);
int cimd_close(SMSCenter *smsc);
int cimd_pending_smsmessage(SMSCenter *smsc);
int cimd_submit_msg(SMSCenter *smsc, Msg *msg);
int cimd_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to CMG SMS centers using EMI_X25.
 */
SMSCenter *emi_open(char *phonenum, char *serialdevice, char *username, char *password);
int emi_reopen(SMSCenter *smsc);
int emi_close(SMSCenter *smsc);
SMSCenter *emi_open_ip(char *hostname, int port, char *username,
		       char *password, int receive_port, char *allow_ip, int our_port);
int emi_reopen_ip(SMSCenter *smsc);
int emi_close_ip(SMSCenter *smsc);
int emi_pending_smsmessage(SMSCenter *smsc);
int emi_submit_msg(SMSCenter *smsc, Msg *msg);
int emi_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to Sema SMS centers using SM2000
 */
SMSCenter *sema_open(char *smscnua,  char *homenua, char* serialdevice,
		     int waitreport);
int sema_reopen(SMSCenter *smsc);
int sema_close(SMSCenter *smsc);
int sema_pending_smsmessage(SMSCenter *smsc);
int sema_submit_msg(SMSCenter *smsc, Msg *msg);
int sema_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to Sema SMS centers using OIS 5.0.
 * Interface to Sema SMS centers using SM2000
 */
SMSCenter *ois_open(int receiveport, const char *hostname, int port,
		    int debug_level);
int ois_reopen(SMSCenter *smsc);
int ois_close(SMSCenter *smsc);
int ois_pending_smsmessage(SMSCenter *smsc);
int ois_submit_msg(SMSCenter *smsc, const Msg *msg);
int ois_receive_msg(SMSCenter *smsc, Msg **msg);
void ois_delete_queue(SMSCenter *smsc);

#endif
