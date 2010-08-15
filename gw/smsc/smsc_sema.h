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

#ifndef SMSC_SEMA_H
#define SMSC_SEMA_H

#include "gwlib/gwlib.h"
#include "smsc.h"
#include "smsc_p.h"

#define SM_RESULT_SUCCESS 0
#define SM_RESULT_FAIL_ARGS 1
#define SM_RESLUT_FAIL_SMSC_DB 2
#define SM_RESULT_FAIL_SMSC_BUSY 3
#define SM_RESULT_FAIL_SM_NOTFOUND 4
#define SM_RESULT_FAIL_SM_DUPLICATE 5

#define SME_RESULT_SUCCESS 0
#define SME_RESULT_INVALIDDATA 1
#define SME_RESULT_DBFULL 2
#define SME_RESULT_SMEBUSY 3
#define SME_RESULT_NOTUSED 4
#define SME_RESULT_DUPLICATESM 5
#define SME_RESULT_DESTUNAVAILABLE 6
#define SME_RESULT_CALLBARREDUSER 7
#define SME_RESULT_TRANSMISSION 21
#define SME_RESULT_FACILITYNOTSUPPORT 22
#define SME_RESULT_ERRORINSME 23
#define SME_RESULT_UNKNOWNSUBSCRIBER 24
#define SME_RESULT_CALLBARREDOPERATOR 25
#define SME_RESULT_CUGVIOLATION 26
#define SME_RESULT_NETWORKFAIL 120

#define ENCODE_IA5 15
#define ENCODE_GSM 0


/*internal definition*/
#define LINE_ENCODE_IA5 1
#define LINE_ENCODE_HEX 2
#define LINE_ENCODE_BIN 3

#define X28_COMMAND_MODE 0
#define X28_MT_DATA_MODE 1
#define X28_MO_DATA_MODE 2

#define INTERNAL_DISCONNECT_TIMEVAL 3
#define INTERNAL_READFD_TIMEVAL 1
#define INTERNAL_CONNECT_TIMEVAL 5
#define INTERNAL_SESSION_MT_TIMEVAL 20

#define SESSION_MT_RECEIVE_ERR 0
#define SESSION_MT_RECEIVE_TIMEOUT 1
#define SESSION_MT_RECEIVE_SUCCESS 2


typedef struct msg_hash{
  int key;
  Octstr* content;
} msg_hash;



typedef struct sema_msg{
  unsigned char type; /*1 byte */
  unsigned char continuebyte; /* 1 byte */
  unsigned char optref[4]; /*4 byte int */
  int encodetype; /* 1 byte ,0 is IA5, 1 is hex, 2 is bin */
  time_t logtime;

  void *msgbody; /* the actual message structure */ 
 
  struct sema_msg *prev, *next; 
} sema_msg;


typedef struct sema_msglist{
	int count;
	sema_msg* first;
	sema_msg* last;

}sema_msglist;



typedef struct sm_statusreport_result{
	unsigned int smeresult; /*see spec*/
}sm_statusreport_result;

typedef struct sm_statusreport_invoke{
	unsigned int msisdnlen; /*1 byte*/
	Octstr* msisdn; /* string */
	unsigned int smetype; /* 1byte, 0-sme, 1-smsc */
	unsigned char smerefnum[4]; /* 4 byte integer */
	unsigned char smscrefnum[4]; /* 4 byte int */
	char accepttime[14]; /*absolute format*/
	unsigned int status; 
	char completetime[14]; /*absolute format*/
	char intermediatime[14]; /*absolute format*/
	unsigned int failreason; /*see spec*/
	unsigned int origaddlen; /*1 byte;*/
	Octstr* origadd; /*1 byte; */
	char invoketime[14]; /*absolute format */
}sm_statusreport_invoke;



typedef struct sm_submit_result{ 
	unsigned int smeresult; 
  /*0- ok, 1-reject for rg problem, 2-db is full or db crash
  3-fail for smsc busy, 4- sm is not in db. 5- fail for already
  there(smsc ref or {sme ref, msdnid */

	unsigned char smscrefnum[4]; /* 4 byte int*/
	char accepttime[14]; /*YYMMDDHHMMSSZZ*/
} sm_submit_result;

typedef struct sm_submit_invoke{
	unsigned int msisdnlen; /*1 byte*/
	Octstr* msisdn; /* string */
	unsigned int smereftype; /* 1btype 1 key, 0 not key */
	unsigned char smerefnum[4]; /* 4 byte integer */
	unsigned int priority; /* 1 byte, 0 means high, 2normal */
	unsigned int origaddlen; /* 1 byte; */
	Octstr* origadd; /* in X25 it will b overwritten by NUA,
			    if user login, it will home NUA */
	unsigned int validperiodtype; /* 0-none, 1-absolute, 2-relative */
	char validperiodabs[14]; 
	unsigned int validperiodrela; /*1 byte ,usage see sema spec.*/

	unsigned int DCS; /* 1 byte */
	unsigned int statusreportrequest; 
    /* 1 byte, 0-failed(abondon), 1-expire, 2-delivered, 3-delete by sme,
       4-delete by smsc operator 5 retry*/
	unsigned int protocal; /*1 byte */
	unsigned int replypath; /* 0 mean can not use sender smsc, 1 means can*/
	unsigned int textsizeseptet; /*1byte, in gsm format, it's 7 bits*/
	unsigned int textsizeoctect; /* 1 byte in 8 bits */
	Octstr* shortmsg;
  
        unsigned char smscrefnum[4];
}sm_submit_invoke;


typedef struct sm_deliver_result{ /* if ok, must return */
	unsigned int smeresult; /* 0- ok, 1-reject for rg problem, 2-db is
				   full or db crash*/
} sm_deliver_result;

typedef struct sm_deliver_invoke{
	unsigned int destaddlen; /*1 byte*/
	Octstr* destadd; /*string*/
	unsigned char smscrefnum[4]; /*4 byte integer*/
	unsigned int origaddlen; /*1 byte*/
	Octstr* origadd; /* note, in X25 it will b overwritten by NUA,
			    if user login, it will home NUA*/

	unsigned int DCS; /*1 byte*/
	unsigned int protocal; /*1 byte*/
	unsigned int replypath; /* 0 mean can not use sender smsc, 1 means can*/
	unsigned int textsizeseptet; /* 1byte, in gsm format, it's 7 bits */
	unsigned int textsizeoctect; /* 1 byte in 8 bits */
	Octstr* shortmsg;
	char accepttime[14];
	char invoketime[14];
   
  /*note in X25 sublogical number is omit*/
	
}sm_deliver_invoke;


/********************** unimplemted msg here
typedef struct sm_login_result{ 
	unsigned int result; 
} sm_login_result;

typedef struct sm_login{
	unsigned int homenualen;
	Octstr* homenua;
	unsigned char pim[2]; 
	unsigned int result;
} sm_login;

typedef struct sm_delete_invoke_result{
	unsigned int result; 
} sm_delete_invoke_result;

typedef struct sm_delete_invoke{
	unsigned int smtype;
	unsigned char smscrefnum[4];
	unsigned char smerefnum[4]; 
	unsigned int msisdnlen; 
	Octstr* msisdn; 
	unsigned int origaddlen; 
	Octstr* origadd;

}sm_delete_invoke;

typedef struct sm_deleteall_invoke{
	unsigned int msisdnlen;
	Octstr* msisdn; 
	unsigned int origaddlen; 
	Octstr* origadd;
	unsigned int nostatusreport;

}sm_deleteall_invoke;


typedef struct sm_replace_result{
	unsigned int delete_result;
	unsigned int add_result;
	unsigned char smscrefnum[4];
	char accepttime[14];
} sm_replace_result;

typedef struct sm_replace_invoke{
	unsigned int smtype; 
	unsigned char smscrefnum[4];
	unsigned char smerefnum[4];
	unsigned int msisdnlen;
	Octstr* msisdn;
	unsigned int new_smereftype;
	unsigned char new_smerefnum[4];
	unsigned int new_priority;
	unsigned int new_origaddlen;
	Octstr* new_origadd;
	unsigned int new_validperiodtype;
	unsigned char new_validperiodabs[14];
	unsigned int new_validperiodrela;
	unsigned int new_DCS;
	unsigned int new_statusreportrequest;
	unsigned int new_protocal;
	unsigned int new_replypath;
	unsigned int new_textsizechar;
	unsigned int new_textsizebyte;
	Octstr* new_shortmsg;

}sm_replace_invoke;


typedef struct 2K_ENQUIRE_INVOKE{
	int smtype;
	int smscrefnum;
	int smerefnum;
	int msisdnlen;
	Octstrr* msisdn;
	int origaddlen;
	Octstr* origadd;
	int enquiretype;

}sm_enqire_invoke;


typedef struct 2K_ENQUIRE_RESULT{
	int result;
	int enquiretype;
	int status;
	char[15] completetime;
	int failreason;
	int priority;
	int origaddlen;
	char* origadd;
	char[15] accepttime;
	char[15] exipretime;
	int DCS;
	int statusreportrequest;
	int protocalid;
	int replypath;
	int textsizechar;
	int textsizebyte;
	char* shortmsg;

}sm_enqire_result;

other msg type ...******************************
*/


/* function definition */
static int sema_submit_result(SMSCenter*, sema_msg*, int);

static int X28_open_data_link(char*);

static int X28_reopen_data_link(int,char*);

static int X28_close_send_link(int);

static int X28_open_send_link(int,char*);

static int X28_data_read(int, char*);

static int X28_data_send(int, char*, int);

static int X28_msg_pop(char *, char *);

static int sema_msg_session_mt(SMSCenter*, sema_msg*);

static int sema_msg_session_mo(SMSCenter*, char*);

static sema_msg* sema_msg_new(void);

static int sema_msg_free(sema_msg *msg);

static sema_msglist* sema_msglist_new(void);

static void sema_msglist_free(sema_msglist*);

static int sema_msglist_push(sema_msglist*, sema_msg*);

static int sema_msglist_pop(sema_msglist*, sema_msg**);

/* static int sema_msgbuffer_pop(Octstr *, Octstr **); */

static int sema_decode_msg(sema_msg**, char*);

static int sema_encode_msg(sema_msg*, char*);

static int line_append_hex_IA5(Octstr* , unsigned char*, int);

static int line_scan_IA5_hex(char*, int, unsigned char*);

static int line_scan_hex_GSM7(unsigned char*,int,int,unsigned char*);

static int internal_char_IA5_to_hex(char *, unsigned char *);

static int internal_char_hex_to_IA5(unsigned char, unsigned char *);

static unsigned char internal_char_hex_to_gsm(unsigned char from);

static int unpack_continous_byte(unsigned char, int *, int * , int *);

static unsigned char pack_continous_byte(int, int, int);

static void increment_counter(void);

#endif











