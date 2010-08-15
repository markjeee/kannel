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
 * smsc_sema.c - implement sms2000 protocal by using X25 access
 * the data communication layer is implemented by using X28 protocol
 * 
 * Code implement submit invoke, status invoke, deliver invoke session
 * there is no internal db for storing delivered and undelivered message
 * 
 * IA5 is most common line coding scheme. 
 * smsc_sema support only IA5 encoding, hex and binary line encoding is not
 * supported.
 * 
 * smsc_sema support IA5 and GSM Data Code Scheme for delivered invoke message
 * smsc_sema support only IA5 Data Code Scheme for submit invoke message
 *  
 * Reference : SMS2000 Version 4.0 Open Interface Specification
 *             Open Source WAP Gateway Architecture Design 
 *             ESTI GSM 03.40
 *
 * Hao Shi 2000
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>


#include <netinet/in.h>
#include <netdb.h>

#include "gwlib/gwlib.h"
#include "smsc.h"
#include "smsc_p.h"
#include "alt_charsets.h"
#include "smsc_sema.h"
#include "sms.h"

#ifndef CRTSCTS
#define CRTSCTS 0
#endif


static unsigned char sema_counter[4] = "0000";
static int sema_wait_report = 1;
static int x28_data_mode = X28_COMMAND_MODE;

SMSCenter * sema_open(char* smscnua, char* homenua, 
		      char* serialdevice, int waitreport)
{
	SMSCenter *smsc;
	int nret = -1;

	smsc = smscenter_construct();
	if(smsc == NULL)
	  goto error;

	sprintf(smsc->name, "SEMA:X28:"); 

	smsc->type = SMSC_TYPE_SEMA_X28;
	smsc->sema_smscnua = gw_strdup(smscnua);
	smsc->sema_homenua = gw_strdup(homenua);
	smsc->sema_serialdevice = gw_strdup(serialdevice);
	sema_wait_report = waitreport;

	smsc->sema_mt = sema_msglist_new();
	if(smsc->sema_mt == NULL) goto error;
	
	smsc->sema_mo = sema_msglist_new();
	if(smsc->sema_mo == NULL) goto error;

	/* Open the device properly. Remember to set the
	   access codes correctly. */
	debug("smsc.sema", 0, "sema_open: open datalink");
	smsc->sema_fd = X28_open_data_link(smsc->sema_serialdevice);
	if(smsc->sema_fd == -1) goto error;

	/*  test the outgoing callX28 to smsc center */
        debug("smsc.sema", 0, "sema_open: test send link");
	nret = X28_open_send_link(smsc->sema_fd, smsc->sema_smscnua);
	if(nret < 1){
	  sleep(2);
	  nret = X28_open_send_link(smsc->sema_fd, smsc->sema_smscnua);  
	  if(nret < 1)
	    goto error;
	}
	X28_close_send_link(smsc->sema_fd);
	return smsc;

error:
	error(0, "sema_open: could not open");
	smscenter_destruct(smsc);
	return NULL;
}


int sema_reopen(SMSCenter *smsc)
{
    int nret = 0;

    debug("smsc.sema", 0, "reopening the connection");

    /*deallocate*/
    sema_msglist_free(smsc->sema_mt);
    sema_msglist_free(smsc->sema_mo);
    /*reallocate*/
    smsc->sema_mt = sema_msglist_new();
    if(smsc->sema_mt == NULL) goto error;
    smsc->sema_mo = sema_msglist_new();
    if(smsc->sema_mo == NULL) goto error;
    memset(smsc->buffer,0,sizeof(smsc->buffer));

    /* Open the device properly. Remember to set the
     access codes correctly. */
    smsc->sema_fd = X28_reopen_data_link(smsc->sema_fd, smsc->sema_serialdevice);
    if(smsc->sema_fd == -1){
	error(0,"sema_reopen_data_link: device file error");
	goto error;
    }
    /*test outgoing call to the smsc */
    nret = X28_open_send_link(smsc->sema_fd, smsc->sema_smscnua);
    if(nret < 1){
	error(0,"test send data link failed");
	goto error;
    }
    X28_close_send_link(smsc->sema_fd);
    return 0;
error:
    error(0, "sema_reopen_data_link: failed");
    return -1;
  
}

int sema_close(SMSCenter *smsc)
{
    if(smsc->sema_fd > 0)
	close(smsc->sema_fd);
    /*deallocate*/
    sema_msglist_free(smsc->sema_mt);
    sema_msglist_free(smsc->sema_mo);
    return 0;
}


int sema_submit_msg(SMSCenter *smsc, Msg *msg)
{
        int nret = 0;
        struct sema_msg *lmsg = NULL;
	struct sm_submit_invoke *submit_sm = NULL;
	char x28sender[2] = "A3";
        
	/* Validate msg */
	if(smsc == NULL){
	    error(0,"sema_submit_msg: SMSC is empty");
	    goto error;
	}
	if(msg == NULL){
	    error(0, "sema_submit_msg: Msg is empty");
	    goto error;
	}

	if(msg_type(msg) != sms) {
	    error(0, "sema_submit_sms: Msg is WRONG TYPE");
	    goto error;
	}
	/*  user data header is not supported in sm2000 X25 access
	    if(msg->sms.coding == DC_7BIT ...|| DC_UNDEF?){
	    error(0, "sema_submit_sms: submit invoke support in IA5 encoding(8 bits chars)");
	    goto error;
	    }

	    if(octstr_len(msg->sms.udhdata)){
	    error(0, "sema_submit_sms: submit invoke not support in IA5 encoding ");
	    goto error;
	    }
	*/
	lmsg = sema_msg_new();
	
	submit_sm = gw_malloc(sizeof(struct sm_submit_invoke));
	memset(submit_sm, 0, sizeof(struct sm_submit_invoke));

	lmsg->type = 'S';
	lmsg->encodetype = LINE_ENCODE_IA5;

	/* set operation reference */
	increment_counter();
	memcpy(lmsg->optref,sema_counter,4);
	/*set sme reference number*/
	increment_counter();
	memcpy(submit_sm->smerefnum, sema_counter, 4);
	/*we send as not key type*/
	submit_sm->smereftype= 2; /*key type is 1*/
	/*we set prority as normal*/
	submit_sm->priority = 1; /*0 is high*/ 
	/*set valid period type as relative*/
	submit_sm->validperiodtype = 2; /* 1 is absolute */
	/*time*/
	submit_sm->validperiodrela = 1; /* from 0 to 143 , fomula is (V+1)*5 min*/
	/*send msg without 00 header*/
	submit_sm->msisdnlen= octstr_len(msg->sms.receiver);
	submit_sm->msisdn = octstr_copy(msg->sms.receiver,0,submit_sm->msisdnlen);
	/* X25 access will always append sender during data transfer */
	submit_sm->origaddlen= 2; /* we need only to orignate address type */
	submit_sm->origadd = octstr_create_from_data(x28sender,2);
	/*data encoding scheme ,support only IA5 in current version*/
	submit_sm->DCS = 15; /*gsm is 0 ,IA5 is 15*/	
	/*protocal ,support only default value  0 in current version*/ 
	submit_sm->protocal = 0;
	/*replypath*/
	submit_sm->replypath= 0; /*gateway do not pay for reply*/
	/*status report*/
	if(sema_wait_report > 0)
	    submit_sm->statusreportrequest =4; /* deliver success, in bin form 00000100*/
	else
	    submit_sm->statusreportrequest = 0;/* no report */
	/* we support submit invoke only in IA5 line encoding*/
	submit_sm->textsizeoctect = submit_sm->textsizeseptet =
		octstr_len(msg->sms.msgdata);
	/*copy msg buffer*/
	submit_sm->shortmsg = octstr_copy(msg->sms.msgdata,
					  0, submit_sm->textsizeoctect);
	
	memset(submit_sm->smscrefnum,0,sizeof(submit_sm->smscrefnum));	
        /*pack the message body in 2kmsg*/
	lmsg->msgbody = submit_sm;
	nret = sema_msg_session_mt(smsc, lmsg);

	gw_free(submit_sm);
	submit_sm = NULL;
	sema_msg_free(lmsg);
	lmsg = NULL;
	

	if(nret == SESSION_MT_RECEIVE_SUCCESS){
	    debug("smsc.sema", 0, "sema_submit_msg: message is successfully delivered");
	    return 1; /*success*/
	}
	else if(nret == SESSION_MT_RECEIVE_TIMEOUT){
	    info(0, "sema_submit msg: session timed out without return");
	    return 0;
	}
	else if(nret == SESSION_MT_RECEIVE_ERR){
	    info(0, "sema_submit msg: smsc says submit failed!");
	    return 0;
	}

	return 1;
	
error:
	if(submit_sm)
	    gw_free(submit_sm);
	if(lmsg)
	    sema_msg_free(lmsg);
	return -1;
}


int sema_receive_msg(SMSCenter *smsc, Msg **msg)
{

    struct sema_msg *rmsg = NULL;
    struct sm_deliver_invoke *recieve_sm = NULL;
 
    while(sema_msglist_pop(smsc->sema_mo, &rmsg) == 1 ) {
  
	*msg = msg_create(sms);
	if(*msg==NULL) goto error;
    
	recieve_sm = (struct sm_deliver_invoke*) rmsg->msgbody;
	if(recieve_sm==NULL) goto error;
          
	/* as IA5(8 bit character) is the default line encoding used by X28
	 * and we do not support other line encoding scheme like binary or
	 * hex encoding
	 */
	(*msg)->sms.coding = DC_8BIT;
	/* OIS in X28 implementation does not include udh field */

	(*msg)->sms.sender = octstr_create_from_data(
	    octstr_get_cstr(recieve_sm->origadd) +2,
	    octstr_len(recieve_sm->origadd)-2);  
	(*msg)->sms.receiver = octstr_create_from_data(
	    octstr_get_cstr(recieve_sm->destadd) +2,
	    octstr_len(recieve_sm->destadd)-2);
  
	(*msg)->sms.msgdata = octstr_duplicate(recieve_sm->shortmsg);
	(*msg)->sms.udhdata = octstr_create("");
	gw_free(recieve_sm);
	sema_msg_free(rmsg);
	rmsg = NULL;
    }
    return 1;

error:
    error(0, "sema_receive_msg: can not create Smart Msg");
    return -1;
}


int sema_pending_smsmessage(SMSCenter *smsc)
{

    char data[1024];
    int ret = 0;
    char clrbuff[]="CLR\0";
    char errbuff[]="ERR\0";
    /* struct sema_msg* smsg = NULL;*/

  /* Receive raw data */
    ret = X28_data_read(smsc->sema_fd, smsc->buffer);
    if(ret == -1) {
	ret = X28_reopen_data_link(smsc->sema_fd, smsc->sema_serialdevice);
	if(ret == -1) goto error;
	return 0;
    } 

    /* Interpret the raw data */
    memset(data,0,sizeof(data));
    while(X28_msg_pop(smsc->buffer, data) == 0 ) {
	if(strlen(data) > 0){
	    if(strstr(data,clrbuff) != NULL ||
	       strstr(data,errbuff) != NULL){
		debug("smsc.sema", 0, "sema_pending_msg: Radio Pad Command line-%s",data);
	    }
	    else{   
	      
		ret = sema_msg_session_mo(smsc, data);
		if(ret == -1) goto error;
	    }
	    memset(data,0,sizeof(data));
	}
    }

    /* Signal that we got a MO message if mo list is not empty*/
    if(smsc->sema_mo->first != NULL){
	return 1;
    }


    return 0;

error:
    error(0,"sema_pending message: device file error");
    return -1;
}



static sema_msg* sema_msg_new(void)
{
    struct sema_msg *msg = NULL;
    msg = gw_malloc(sizeof(struct sema_msg));
    memset(msg, 0, sizeof(struct sema_msg));
    return msg;
}

static int sema_msg_free(sema_msg *msg) {
    if(msg == NULL) return 0;
    gw_free(msg);
    return 1;
}

static sema_msglist* sema_msglist_new(void) {

    struct sema_msglist *mlist = NULL;
    mlist = gw_malloc(sizeof(struct sema_msglist));
    memset(mlist, 0, sizeof(struct sema_msglist));
  
    mlist->first = NULL;
    mlist->last = NULL;
    mlist->count = 0;
    return mlist;
}

static void sema_msglist_free(sema_msglist *mlist) {

    struct sema_msg *pmsg = NULL;
    if(mlist == NULL) return; 
    while( sema_msglist_pop(mlist, &pmsg) == 1 )  {
	if(pmsg==NULL) break;
	sema_msg_free(pmsg);
	pmsg = NULL;
    }
    gw_free(mlist);
    mlist->count = 0;
}

static int sema_msglist_push(sema_msglist *plist, sema_msg *pmsg) {

    struct sema_msg * lmsg = NULL;
    if(plist == NULL) {
	info(0, "msglist_push: NULL msg list");
	goto error;
    }
    if(pmsg == NULL) {
	info(0, "msglist_push: NULL input");
	goto error;
    }
    /* If list is completely empty. */
    if( plist->first == NULL ) {
	plist->last = pmsg;
	plist->first = pmsg;
	pmsg->prev = NULL;
	pmsg->next = NULL;
    }
    else{
	lmsg=plist->last;
	lmsg->next=pmsg;
	pmsg->prev=lmsg;
	pmsg->next=NULL;
	plist->last=pmsg;
    }
    plist->count += 1;
    return 1;

error:
    error(0, "msglist_push: error");
    return 0;

}

static int sema_msglist_pop(sema_msglist *plist, sema_msg **msg) {

   if(plist == NULL) {
	info(0, "msglist_pop: NULL list");
	goto no_msg;
    }
    if(plist->first == NULL) {
	goto no_msg;
    }

    *msg = plist->first;
    if(plist->last == *msg) {
	plist->first = NULL;
	(*msg)->prev = NULL;
	plist->last = NULL;
    } 
    else {
	plist->first = (*msg)->next;
	plist->first->prev = NULL; 
	if(plist->first->next == NULL) 
	    plist->last = plist->first;
    }
    plist->count -= 1;
    return 1;

no_msg:
    return 0;
}



static int X28_open_data_link(char* device){
    int fd = -1, iret;
    struct termios tios;
    info(0,"open serial device %s",device);
    fd = open(device, O_RDWR|O_NONBLOCK|O_NOCTTY);
    if(fd==-1) {
	error(errno, "sema_open_data_link: error open(2)ing the character device <%s>",
	      device);
	if(errno == EACCES)
	    error(0, "sema_open_data_link: user has no right to access the serial device");
	return -1;
    }

    tcgetattr(fd, &tios);
    cfsetospeed(&tios, B4800);  /* check radio pad parameter*/
    cfsetispeed(&tios, B4800);
    kannel_cfmakeraw(&tios);
    tios.c_iflag |= IGNBRK|IGNPAR|INPCK|ISTRIP;
    tios.c_cflag |= (CSIZE|HUPCL | CREAD | CRTSCTS);
    tios.c_cflag ^= PARODD;
    tios.c_cflag |=CS7;
    iret = tcsetattr(fd, TCSANOW, &tios);
    if(iret == -1){
	error(errno,"sema_open_data_link: fail to set termios attribute");
	goto error;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
  
error:
    return -1;
}

static int X28_reopen_data_link(int oldpadfd ,char* device){
    int nret = 0;
    if(oldpadfd > 0){
	nret= close(oldpadfd);
	if(nret == -1){
	    error(errno,"sema_reopen_data_link: close device file failed!!");
	}
    }
    sleep(1);
    return X28_open_data_link(device);
}

	
static int X28_close_send_link(int padfd)
{
    char discnntbuff[5];
    char readbuff[1024];
    char finishconfirm[]="CLR CONF\0";
    int nret = 0, readall = 0;
    time_t tstart;
    time(&tstart);

    sprintf(discnntbuff,"%cCLR\r",0x10);
    memset(readbuff,0,sizeof(readbuff));

    /* what ever is the close return, data mode is unreliable now*/
    x28_data_mode = X28_COMMAND_MODE;

    if(padfd <= 0)
	goto datalink_error;
    while((time(NULL) - tstart) < INTERNAL_DISCONNECT_TIMEVAL){
	nret =write(padfd, discnntbuff, 5);
	if(nret == -1){
	    if(errno == EAGAIN || errno ==EINTR) continue;
	    else{
		goto datalink_error;
	    }
	}	
	sleep(1); /*wait 1 senconds for virtual link break*/	
	nret=read(padfd, readbuff+readall,128);
	if(nret == -1){
	    if(errno == EAGAIN || errno ==EINTR) continue;
	    else{
		goto datalink_error;
	    }
	}
	if(nret >0){
	    readall += nret;
	    if(strstr(readbuff,finishconfirm))
		return 1;
	}
    }
    return 0;
datalink_error:
    error(errno,"sema_close_send_link, device file error");
    return -1;
}



static int X28_open_send_link(int padfd, char *nua) {

    char readbuff[1024]; 
    char writebuff[129];
    char smscbuff[129];
    int readall = 0, readonce = 0, writeonce = 0, writeall = 0, i = 0;
    char X28prompt[]="*\r\n\0";
    time_t timestart;
  
    debug("smsc.sema", 0, "sema_open send link: call smsc  <%s> for <%i> seconds",
	  nua, (int)INTERNAL_CONNECT_TIMEVAL);
	
    /*  type few <cr> to invoke DTE */
    writebuff[0] = '\r';       
    memset(readbuff,0,sizeof(readbuff));     
    for(i = 0; i <= 3; i++)
    {
	readonce = writeonce = -1;
	writeonce = write(padfd, writebuff, 1);
	if(writeonce < 1){
	    if(errno == EINTR || errno == EAGAIN) continue;
	    else{
		goto datalink_error;
	    }
	}
	usleep(1000); /* wait for prompt */
	readonce = read(padfd, &readbuff[readall],1024);
	if(readonce == -1){
	    if(errno == EINTR || errno == EAGAIN) continue;
	    else{
		goto datalink_error;
	    }
	}
	else
	    readall += readonce;
    }
    if(strstr(readbuff, X28prompt) == NULL){
	warning(0,"X28_open_send_link: can not read command prompt, abort");
	return 0;
    }
	

    /* second, connect to the smsc now */
    memset(writebuff,0,sizeof(writebuff));
    memset(readbuff,0,sizeof(readbuff));  
    writeall = readall = 0;
    sprintf(writebuff, "%s\r", nua);
    sprintf(smscbuff, "%s COM",nua);
  
    while((size_t) writeall < strlen(writebuff)){
	writeonce = -1;
	writeonce = write(padfd, writebuff+writeall, strlen(writebuff)-writeall);
	if(writeonce == -1){
	    if(errno == EINTR || errno == EAGAIN)
		continue;
	    else
		goto datalink_error;
	}
	if(writeonce > 0)
	    writeall +=writeonce;
    }
    tcdrain(padfd); 
    usleep(1000*1000);/* wait for smsc answer */

    time(&timestart);
    while(time(NULL) - timestart < INTERNAL_CONNECT_TIMEVAL){
	if((size_t) readall >= sizeof(readbuff))
	    goto error_overflow;
	/* We read 1 char a time */
	readonce = read(padfd, &readbuff[readall], 1);
	if(readonce == -1) {
	    if(errno == EINTR || errno == EAGAIN) continue;
	    else
		goto datalink_error;
	}
	if(readonce > 0)
	    readall += readonce;
	/* Search for reponse line. */
	if(readall > 2 &&
	   readbuff[readall-1] == '\n' && 
	   readbuff[readall-2] == '\r') {
	    if(strstr(readbuff, smscbuff)) {
		debug("smsc.sema", 0,
		      "sema_open send link: smsc responded, virtual link established");
		x28_data_mode = X28_MT_DATA_MODE; 
		return 1;	
	    }
	} 
	usleep(1000);
    }
    info(0,"sema_open_send_link: connect timeout");
    return 0;
error_overflow:
    warning(0, "sema_open_send_link: command buffer overflow");
    return 0;
datalink_error:
    error(errno,"sema_open_send_link: device file error");
    return -1;
}



static int X28_data_read(int padfd, char *cbuffer) {
    char *p = NULL;
    int ret,  len;
    fd_set read_fd;
    struct timeval tv, tvinit;
    size_t readall;
     
    tvinit.tv_sec = 0;
    tvinit.tv_usec = 1000;
     
    readall = 0;
    for (;;) {
	FD_ZERO(&read_fd);
	FD_SET(padfd, &read_fd);
	tv = tvinit;
	ret = select(padfd + 1, &read_fd, NULL, NULL, &tv);
	if (ret == -1) {
	    if(errno==EINTR) goto got_data;
	    if(errno==EAGAIN) goto got_data;
	    error(errno, "Error doing select for fad");
	    return -1;
	} else if (ret == 0)
	    goto got_data;
	len = strlen(cbuffer);
	ret = read(padfd,
		   cbuffer + len,
		   256);    
	if (ret == -1) {
            error(errno," read device file");
	    return -1;
	}
	if (ret == 0)
	    goto eof;
       
	readall += ret;
	if ((size_t) len >  sizeof(cbuffer)- 256) {
	    p = gw_realloc(cbuffer, sizeof(cbuffer) * 2);
	    memset(p+len,0,sizeof(cbuffer)*2 - len);
	    cbuffer = p;
	}
	if(readall > 0)
	    break;
    }
     
eof:
    if(readall > 0)
	ret = 1;
    goto unblock;
     
got_data:
    ret = 0;
    goto unblock;

unblock:
    return ret;  

}

static int X28_data_send(int padfd, char *cbuffer,int sentonce) {
    int len = 0, pos = 0,writeonce = 0,writeall = 0;
 
    tcdrain(padfd);
    len = strlen(cbuffer);
    while(len > 0){
	if(len < sentonce) {
	    writeonce = write(padfd, cbuffer+pos, len);
	}
	else
	    writeonce = write(padfd, cbuffer+pos, sentonce);
 
	if (writeonce == -1) {
	    if(errno == EINTR || errno == EINTR)
		continue;
	    else{
		goto error;
	    }
	}
	if(writeonce > 0){
	    len -= writeonce;
	    pos += writeonce;
	    writeall = pos;
	}
    }
    tcdrain(padfd);
    return writeall;

error:
    error(errno,"sema_send data error: device file error");
    return -1;
}

static int X28_msg_pop(char *from, char *to)
{
    char* Rbuff =NULL;
    char* RRbuff = NULL;
    char mobuff[] ="COM\r\n\0";
    char mobuffend[] = "\r\0";
    char prompbuff[] = "*\r\0";
    int len = 0, Llen= 0, Rlen = 0,RRlen = 0;

    len = strlen(from);
    if(len <=0) goto no_msg;

    /* trim off rabbish header */
    while(*from == '\r' || *from == '\n'){
	len = strlen(from);
	if(len > 1){
	    memmove(from, from +1, len-1);
	    memset(from+(len-1), 0, 1);
	}
	else{
	    memset(from,0,len);
	    return -1;
	}
    }   

    len = strlen(from); 
    /*all kinds of useful infomation contains \r*/
    if((Rbuff=memchr(from,'\r',len)) == NULL)
	goto no_msg;

 
    /*check if it is a command prompt *\r\n */
    if((Rbuff -from) > 0 && *(Rbuff -1) == '*'){
	if(strlen(Rbuff) < 2) goto no_msg; /*\n is not coming yet*/
     
	if(Rbuff -from > 4){ /* command info */
	    Rlen = Rbuff -1 -from;
	    memcpy(to,from,Rlen);
	}
	x28_data_mode = X28_COMMAND_MODE;
	if(strlen(Rbuff+1) > 1){
	    Rlen = strlen(Rbuff +2);
	    memmove(from, Rbuff +2, Rlen);
	    memset(from+Rlen, 0, len-Rlen);
	}
	else
	    memset(from, 0,len);
    }/* check mo msg , format X121address+COM\r\n+msg+\r*/
    else if((Rbuff-from) > 3 &&  strstr(Rbuff-4,mobuff)!= NULL){
	if(strlen(Rbuff) < 3 ||
	   (RRbuff = strstr(Rbuff + 2, mobuffend)) == NULL)
	    goto no_msg; /*the msg+\r  is still coming*/

	RRlen = RRbuff - (Rbuff+2);
	if(RRlen > 4){ /* msg header is 4 byte always+msg content*/ 
	    memcpy(to, Rbuff +2 , RRlen);
	    x28_data_mode = X28_MO_DATA_MODE;
	}

	if(strlen(RRbuff) > 1){
	    Rlen = strlen(RRbuff +1);
	    memmove(from, RRbuff+1 ,Rlen);
	    memset(from+Rlen,0,len -Rlen);
	}
	else
	    memset(from,0,len);
    }
    else{/* it can be mt reply */
	if(Rbuff  - from > 0){ 
	    Llen = Rbuff - from;
	    memcpy(to, from, Llen);
	}    
	if(strlen(Rbuff) > 1){
	    Rlen = strlen(Rbuff+1);
	    memmove(from,Rbuff+1,Rlen);
	    memset(from+Rlen,0,len-Rlen);
	}
	else
	    memset(from,0,len);
    }

    /* check rest of line for link state: command mode or data mode */
    if(strstr(from,prompbuff) != NULL)
	x28_data_mode = X28_COMMAND_MODE;

    return 0;
no_msg:
    return -1;
}



static int sema_submit_result(SMSCenter *smsc, sema_msg* srcmsg, int result)
{
    char IA5buff[1024];
    unsigned char oct1byte[1];
    unsigned char ia5byte[2];
    unsigned char cTr='t';
    unsigned char cMr='m';
    unsigned char ccontinuebyte = 'P', ccr = '\r';
    int j = 0, iret;

    memset(IA5buff,0,sizeof(IA5buff));
    switch(srcmsg->type)
    {
    case 'M':
	memcpy(IA5buff,&cMr,1);/*msg type*/
	memcpy(IA5buff+1,&ccontinuebyte,1); /*continue bit*/
	memcpy(IA5buff+2,srcmsg->optref,4); /*operation reference*/
	write_variable_value(result,oct1byte);
	j=internal_char_hex_to_IA5(oct1byte[0],ia5byte);
	memcpy(IA5buff+6,ia5byte,j);
	memcpy(IA5buff+6+j,&ccr,1);/*result*/
	iret = X28_data_send(smsc->sema_fd,IA5buff,strlen(IA5buff));
	if(iret == -1) goto error;
	break;
    case 'T':
	memcpy(IA5buff,&cTr,1);
	memcpy(IA5buff+1,&ccontinuebyte,1);
	memcpy(IA5buff+2,srcmsg->optref,4); 
	write_variable_value(result,oct1byte);
	j=internal_char_hex_to_IA5(oct1byte[0],ia5byte);
	memcpy(IA5buff+6,ia5byte,j);
	memcpy(IA5buff+6+j,&ccr,1);
	iret = X28_data_send(smsc->sema_fd,IA5buff,strlen(IA5buff));
	if(iret == -1) goto error;	  
	break;
    default:  
	return 0; /*unsupoorted result msg type*/
    }
    return 1;	
error:
    error(0,"sk_submit_result: write to device file failed");
    return -1;
}

static int sema_msg_session_mt(SMSCenter *smsc, sema_msg* pmsg){
    struct msg_hash *segments = NULL;
    struct sema_msg* mtrmsg = NULL;
    struct sm_statusreport_invoke* report_invoke = NULL;
    struct sm_submit_result* submit_result = NULL;
    struct sm_submit_invoke* submit_invoke = NULL;
    struct sm_deliver_invoke* deliver_invoke = NULL;

    char data[1024], IA5buff[256], IA5chars[1024], mochars[10*1024];
    unsigned char ccontinuebyte, ccr = '\r';
    unsigned char cerr[] = "ERR\0",cclr[] = "CLR\0", tmp1[5] , tmp2[5];
  
    int  i, iseg = 0, ilen = 0,iret = 0, moret;
    int isrcved = 0, iTrcved = 0, decoderesult = 0;
    time_t tstart;

    submit_invoke = (struct sm_submit_invoke*) pmsg->msgbody;
    if(submit_invoke == NULL) goto error;

    /*encode first*/
    memset(IA5chars,0,sizeof(IA5chars));

    if(sema_encode_msg(pmsg, IA5chars) < 1) goto encode_error;

    /*divide segments, we send buffer no more than 128 byte once*/
    iseg = strlen(IA5chars)/121 + 1;
    segments = gw_malloc(iseg * sizeof(struct msg_hash));
    if(segments == NULL) goto error;


    /*first segments*/	
    if(strlen(IA5chars) < 121)
	ilen = strlen(IA5chars);
    else
	ilen = 121;
    segments[0].content = octstr_create_from_data((char *)&(pmsg->type), 1);/*msg type, in hex*/
    ccontinuebyte = pack_continous_byte(pmsg->encodetype, 1, iseg -1);
    octstr_insert_data(segments[0].content, 1, 
		       (char *)&ccontinuebyte, 1);  /*continue char, in hex*/
    octstr_insert_data(segments[0].content,
		       2, (char *)pmsg->optref, 4); /*operation reference, in hex*/
    octstr_insert_data(segments[0].content, 6,
		       IA5chars, ilen);
    octstr_insert_data(segments[0].content,
		       octstr_len(segments[0].content), (char *)&ccr, 1); /*<cr>*/
 
    /*rest segments*/
    for( i = 1; i < iseg; i++){
	if(strlen(IA5chars) - i*121 < 121)
	    ilen = strlen(IA5chars) - i*121;
	else
	    ilen =121;
	segments[i].content= octstr_create_from_data((char *)&(pmsg->type), 1); 
	ccontinuebyte = pack_continous_byte(pmsg->encodetype, 0, iseg -i-1);
	octstr_insert_data(segments[i].content, 1, (char *)&ccontinuebyte, 1); 
	octstr_insert_data(segments[i].content, 2, (char *)pmsg->optref, 4); 
	octstr_insert_data(segments[i].content, 6, 
			   IA5chars + i*121, ilen);
	octstr_insert_data(segments[i].content,
			   octstr_len(segments[i].content),(char *)&ccr, 1); 
    }

    if(x28_data_mode != X28_MT_DATA_MODE){
	/* do not trust any existing data mode*/    
	X28_close_send_link(smsc->sema_fd); 	
	/*open send link*/
	if((iret = X28_open_send_link(smsc->sema_fd,smsc->sema_smscnua)) < 1){
	    if(iret == -1){
		iret = X28_reopen_data_link(smsc->sema_fd, smsc->sema_serialdevice);
		if(iret == -1){
		    goto error;
		}
	    }
	    X28_close_send_link(smsc->sema_fd); 
	    sleep(1);
	    iret = X28_open_send_link(smsc->sema_fd,smsc->sema_smscnua);
	    if(iret < 1)
		goto sendlink_error;
	}
    }
    /*deliver buff*/
    for(i = 0; i < iseg; i++){
	memset(IA5buff,0,sizeof(IA5buff));
	memcpy(IA5buff,octstr_get_cstr(segments[i].content),
	       octstr_len(segments[i].content));

	iret =X28_data_send(smsc->sema_fd,IA5buff,strlen(IA5buff));
	if(iret == -1)
	    goto error;
	octstr_destroy(segments[i].content);
    }
    gw_free(segments);

    /*wait result and report return*/
    mtrmsg = sema_msg_new();
    memset(mochars,0,sizeof(mochars));

    time(&tstart);
    while(time(NULL) -tstart < INTERNAL_SESSION_MT_TIMEVAL){
	iret = X28_data_read(smsc->sema_fd, smsc->buffer);
	if(iret == -1)
	    goto error;
	
	/* Interpret the raw data */
	memset(data,0,sizeof(data));
	while(X28_msg_pop(smsc->buffer, data) == 0 ) {
	    if(strlen(data) > 0){
		if(strstr(data,(char *)cerr) != NULL ||
		   strstr(data,(char *)cclr) != NULL){
		  debug("smsc.sema", 0, "sema_mt_session: Radio Pad Command line-%s",data);
		    goto sendlink_error;
		}
		/* decode msg*/      
		decoderesult = sema_decode_msg(&mtrmsg,data);  
		if(decoderesult >= 0){
		    if(mtrmsg->type == 's'){ /*submit result*/
	 
			submit_result = (struct sm_submit_result*) mtrmsg->msgbody;
			if(submit_result == NULL) goto error;
			/* check result operation number is what we send */
			memset(tmp1,0,5); memset(tmp2,0,5);
			memcpy(tmp1,mtrmsg->optref,4);
			memcpy(tmp2, pmsg->optref,4);
			if(strstr((char *)tmp1,(char *)tmp2) != NULL){
			    isrcved = 1;
			    memcpy(submit_invoke->smscrefnum, submit_result->smscrefnum,4);
			}
			if(isrcved == 1 &&
			   submit_result->smeresult != 0){		 
			    gw_free(submit_result);
			    goto smsc_say_fail;  
			}
			gw_free(submit_result);
	
		    } 
		    else if(mtrmsg->type == 'T'){ /*report invoke*/
		
			report_invoke = (struct sm_statusreport_invoke*) mtrmsg->msgbody;
			if(report_invoke == NULL) goto error;
			/*check if report reference number is what we expect*/
			memset(tmp1,0,sizeof(tmp1)); memset(tmp2,0,sizeof(tmp2));
			memcpy(tmp1,report_invoke->smscrefnum,4);
			memcpy(tmp2,submit_invoke->smscrefnum,4);
			if(strstr((char *)tmp1,(char *)tmp2) != NULL){
			    iTrcved = 1;
			}
			decoderesult = 0; 
			iret = sema_submit_result(smsc, mtrmsg, decoderesult);
			if(iret == -1) goto error;
			if(iTrcved == 1 &&
			   report_invoke->status != 3){ /*3 means msg delivered*/
			    info(0,"sema_mt_session: submit invoke failed with report value-%i",report_invoke->status);
			    gw_free(report_invoke);
			    goto smsc_say_fail;
			}
			gw_free(report_invoke);

		    }
		    else if(mtrmsg->type == 'M'){/* deliver invoke*/
	  
			/* we do not deal with deliver in mt session*/ 
			decoderesult = 0;
			iret = sema_submit_result(smsc, mtrmsg, decoderesult);
			if(iret == -1) goto error;
			deliver_invoke = (struct sm_deliver_invoke*) mtrmsg->msgbody;
			if(deliver_invoke != NULL){
			    gw_free(deliver_invoke);
			    /*append buffer back to  smsc->buffer*/
			    ilen=strlen(mochars);
			    memcpy(mochars+ilen,data,strlen(data));
			    ilen=strlen(mochars);
			    memcpy(mochars+ilen,&ccr,1);
			}		
			time(&tstart);
		    }
		    /* clean msg for next read*/
		    memset(mtrmsg,0,sizeof(struct sema_msg));
		}
		/* clean buffer for next useful info*/
		memset(data,0,sizeof(data));
		if(sema_wait_report == 0 && isrcved == 1)
		{
		    info(0,"sema_mt_session: submit invoke delivered successfully to smsc");
		    goto mo_success;
		}
		if(sema_wait_report > 0 &&
		   isrcved == 1 && iTrcved == 1)
		{
		    info(0,"sema_mt_session: submit invoke delivered successfully to msisdn");
		    goto mo_success;
		}
	    }
	}
    }

/* mo_timeout: */
      info(0,"sema_mt_session: timeout without receiving all expected returns");
      moret = SESSION_MT_RECEIVE_TIMEOUT;
      goto mo_return;
mo_success:
    moret = SESSION_MT_RECEIVE_SUCCESS;
    goto mo_return;
smsc_say_fail:
    info(0,"sema_mt_session: smsc says message deliver failed!");
    moret = SESSION_MT_RECEIVE_ERR;
    goto mo_return;
mo_return:
    X28_close_send_link(smsc->sema_fd);
    /* we have to close here, otherwise smsc will wait for a long time
       untill it find out nothing is coming */
    sema_msg_free(mtrmsg);
    ilen = strlen(mochars);
    i =  strlen(smsc->buffer);
    if(ilen > 0){
	memmove( smsc->buffer+ilen,smsc->buffer,i);
	memcpy(smsc->buffer, mochars,ilen);
    }
    return moret;      	
sendlink_error:
    info(0,"sema_mt_session: X28 data link has broken");
    if(mtrmsg != NULL)
	sema_msg_free(mtrmsg);
    return 0;
encode_error:
    info(0,"sema_mt_session: Msg encode error");
    return 0;
error:
    error(0,"sema_mt session: memory allocation error or device file error");
    return -1;

}


static int sema_msg_session_mo(SMSCenter *smsc, char* cbuff){
  
    struct sema_msg *rmsg = NULL;
    int iret = 0, retresult = 0;

    struct sm_deliver_invoke* deliver_invoke = NULL;
    struct sm_statusreport_invoke* report_invoke = NULL;
 
    rmsg = sema_msg_new();

    iret = sema_decode_msg(&rmsg,cbuff);
    if(iret == - 1) goto msg_error;/* decode error */

    if(x28_data_mode == X28_COMMAND_MODE){
	/* do not trust any existing data mode*/

	/* XXX this should be fixed? -rpr */
	
	X28_close_send_link(smsc->sema_fd); 	
	/*open send link*/
	if(X28_open_send_link(smsc->sema_fd,smsc->sema_smscnua) < 1){
	    info(0,"sema_mo_session: can not establish send link");
	    return 0;
	}	  
    }

    if(rmsg->type == 'M'){ 	/* deliver invoke */
	retresult = 0;  	/* deliver invoke */  
	iret = sema_submit_result(smsc, rmsg, retresult); 
	if(iret == -1) goto error;
	deliver_invoke = (struct sm_deliver_invoke*) rmsg->msgbody;
	if(deliver_invoke == NULL) goto msg_error;
	sema_msglist_push(smsc->sema_mo, rmsg);  
	return 1;
    }
    else if(rmsg->type == 'T'){	/* status report */
	retresult = 0; 		/* let msg through */
	sema_submit_result(smsc, rmsg, retresult);
	if(iret == -1) goto error;
	report_invoke = (struct sm_statusreport_invoke*) rmsg->msgbody;
	if(report_invoke != NULL)  
	    gw_free(report_invoke);
    }
    else{ /* add your additional support here*/
    }
    sema_msg_free(rmsg);
    return 1;
  
msg_error:
    sema_msg_free(rmsg);
    error(0,"sema_mo session: Msg decode failed");
    return 0;
error:  
    error(0,"sema_mo session: device file error or memory allocation problem!");
    return -1;
}



static int sema_decode_msg(sema_msg **desmsg, char* octsrc) {
    struct sm_deliver_invoke *receive_sm = NULL;
    struct sm_statusreport_invoke* receive_report = NULL;
    struct sm_submit_result* submit_result = NULL;
	
    unsigned char tmp[1024],tmpgsm[1024];
    int octetlen, iret, iusedbyte;
    int imsgtopseg = 0, imsgfollownum = 0, imsgencodetype = 0;
    unsigned char cmsgtype, cmsgcontinuebyte;

    /* message type */
    if(strlen(octsrc) <= 4) goto no_msg;

    /* check if we support this type */
    cmsgtype = *octsrc;
    if(cmsgtype != 's' 		/* invoke reseult */
       && cmsgtype != 'M'  	/* deliver invoke */
       && cmsgtype != 'T'){  	/* report invoke */
	info(0,"sema_decode: msg type not supported");
	goto error_msg;
    }

    /*check if continue bit is correct */
    cmsgcontinuebyte = *(octsrc+1);
    iret = unpack_continous_byte(cmsgcontinuebyte,
				 &imsgencodetype,&imsgtopseg, &imsgfollownum);

    if(iret == -1){
	info(0,"sema_decode: msg continue bit can not be interpreted");
	goto error_msg;
    }

    /*status report and submit result will always be 1 segments
      for deliver invoke, if smsc can not send all the data in one packet,
      text data will be truncated ,so it's also 1 packet*/
	
    if(imsgtopseg == 0){
	info(0, "sema_decode: can not interpret more than one segments msg");
	goto error_msg;
    }	

    (*desmsg)->type = cmsgtype;
    (*desmsg)->continuebyte = cmsgcontinuebyte;
    (*desmsg)->encodetype = imsgencodetype;

    /*operation reference*/

    memcpy((*desmsg)->optref, octsrc +2, 4);
    octsrc += 6;
    iusedbyte = 0;
 
    switch(cmsgtype){
    case 's': /* submit invoke result */
	submit_result = gw_malloc(sizeof(struct sm_submit_result)); 
	memset(submit_result,0,sizeof(struct sm_submit_result));

	/* result */ 
	iusedbyte = line_scan_IA5_hex(octsrc, 1, tmp);
	if(iusedbyte < 1) goto error_submit;
	octetlen = 1;
	submit_result->smeresult = get_variable_value(tmp, &octetlen);
	if(submit_result->smeresult == SM_RESULT_SUCCESS)
	{
	    /*smsc reference number*/
	    octsrc += iusedbyte;	
	    iusedbyte = line_scan_IA5_hex(octsrc, 4,tmp);
	    if(iusedbyte <1) goto error_submit;	
	    memcpy(submit_result->smscrefnum, tmp, 4);
	    /*accept time*/
	    octsrc += iusedbyte;	 
	    iusedbyte = line_scan_IA5_hex(octsrc, 14,tmp);
	    if(iusedbyte < 1) goto error_submit;	
	    memcpy(submit_result->accepttime, tmp, 4);
	}
	(*desmsg)->msgbody = submit_result;
	break;
    case 'M': 
	/* deliver invoke*/
	receive_sm = gw_malloc(sizeof(struct sm_deliver_invoke));
	memset(receive_sm, 0, sizeof(struct sm_deliver_invoke));
	/*deliver destination address length*/
	iusedbyte = line_scan_IA5_hex(octsrc, 1, tmp);
	if(iusedbyte < 1) goto error_deliver;
	octetlen = 1;
	receive_sm->destaddlen = get_variable_value(tmp, &octetlen);
	/*deliver destination address*/
	octsrc +=iusedbyte;    
	iusedbyte = line_scan_IA5_hex(octsrc,receive_sm->destaddlen,tmp);
	if(iusedbyte < 1) goto error_deliver;
	receive_sm->destadd= octstr_create_from_data((char *)tmp,  receive_sm->destaddlen);
	/*smsc reference number*/
	octsrc +=iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc, 4,tmp);
	if(iusedbyte < 1) goto error_deliver;
	memcpy(receive_sm->smscrefnum, tmp, 4);
	/*originate address length*/
	octsrc +=iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc, 1, tmp);
	if(iusedbyte < 1) goto error_deliver;
	octetlen = 1;
	receive_sm->origaddlen = get_variable_value(tmp, &octetlen);
	/*originate address*/
	octsrc +=iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc, receive_sm->origaddlen, tmp);
	if(iusedbyte < 1) goto error_deliver;
	receive_sm->origadd= octstr_create_from_data((char *)tmp,receive_sm->origaddlen);
	/* data code scheme */ 
	octsrc +=iusedbyte;
	if(iusedbyte < 1) goto error_deliver;
	iusedbyte = line_scan_IA5_hex(octsrc, 1,tmp);
	octetlen = 1;
	receive_sm->DCS = get_variable_value(tmp, &octetlen);
	if(receive_sm->DCS != ENCODE_IA5 && receive_sm->DCS !=ENCODE_GSM){
	    info(0, "sema_decode, Data encoding scheme not supported");
	    goto error_deliver;
	}
	/* protocol */ 
	octsrc +=iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc, 1,tmp);
	if(iusedbyte < 1) goto error_deliver;
	octetlen = 1;
	receive_sm->protocal = get_variable_value(tmp, &octetlen);
	/* reply path */
	octsrc +=iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc, 1,tmp);
	if(iusedbyte < 1) goto error_deliver;
	octetlen = 1;
	receive_sm->replypath = get_variable_value(tmp, &octetlen);
	/*text size in septect*/
	octsrc +=iusedbyte;
	iusedbyte = internal_char_IA5_to_hex(octsrc, tmp);
	if(iusedbyte < 1) goto error_deliver;
	receive_sm->textsizeseptet = tmp[0];
	/*text size in octects*/
	octsrc +=iusedbyte;
	iusedbyte = internal_char_IA5_to_hex(octsrc, tmp);
	if(iusedbyte < 1) goto error_deliver;
	receive_sm->textsizeoctect = tmp[0];
	octsrc+=iusedbyte;
	
	/*message text*/
	
	iusedbyte = 0;
	memset(tmp,0,sizeof(tmp));						
	if(receive_sm->DCS == ENCODE_IA5 && receive_sm->textsizeoctect > 0) 
	{
	    iusedbyte = line_scan_IA5_hex(octsrc,receive_sm->textsizeoctect,tmp);	   
	    if(iusedbyte < 1) goto error_deliver;	
	    receive_sm->shortmsg =octstr_create_from_data( (char *)tmp,receive_sm->textsizeoctect);
	}
	else if(receive_sm->DCS == ENCODE_GSM &&  receive_sm->textsizeseptet > 0)
	{
	    memset(tmpgsm,0,sizeof(tmpgsm));
	  
	    iusedbyte = line_scan_IA5_hex(octsrc,receive_sm->textsizeoctect,tmp);
	    if(iusedbyte < 1) goto error_deliver;
	    line_scan_hex_GSM7(tmp,receive_sm->textsizeoctect,
			       receive_sm->textsizeseptet, tmpgsm);
	    receive_sm->shortmsg = octstr_create_from_data((char *)tmpgsm,
							   receive_sm->textsizeseptet);
	
	}
	else if(receive_sm->textsizeoctect <= 0)
	  receive_sm->shortmsg = octstr_create("");

	/*accepttime*/
	octsrc +=iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc,14,tmp);
	if(iusedbyte < 1) goto error_deliver;
	memcpy(receive_sm->accepttime, tmp,14);
	/*valid time*/
	octsrc +=iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc,14,tmp);
	if(iusedbyte < 1) goto error_deliver;
	memcpy(receive_sm->invoketime, tmp,14);
	(*desmsg)->msgbody = receive_sm;
	break;
    case 'T': 
	/* status report invoke */
	receive_report = gw_malloc(sizeof(struct sm_statusreport_invoke)); 
	memset(receive_report,0,sizeof(struct sm_statusreport_invoke));
	/*deliver msisdn address length*/
	iusedbyte = line_scan_IA5_hex(octsrc, 1, tmp);
	if(iusedbyte < 1) goto error_receive;
	octetlen = 1;
	receive_report->msisdnlen = get_variable_value(tmp, &octetlen);
	/*msisdn*/
	octsrc += iusedbyte;    
	iusedbyte = line_scan_IA5_hex(octsrc, receive_report->msisdnlen, tmp);
	if(iusedbyte < 1) goto error_receive;
	receive_report->msisdn = octstr_create_from_data( (char *)tmp,receive_report->msisdnlen);
	/*sme reference type*/
	octsrc += iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc, 1, tmp);
	if(iusedbyte < 1) goto error_receive;
	octetlen = 1;
	receive_report->smetype = get_variable_value(tmp, &octetlen);
	/*sme reference number */
	octsrc += iusedbyte;    
	iusedbyte = line_scan_IA5_hex(octsrc,4, tmp);
	if(iusedbyte < 1) goto error_receive;
	memcpy(receive_report->smerefnum ,tmp, 4);
	/*smsc reference number */
	octsrc += iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc,4, tmp);
	if(iusedbyte < 1) goto error_receive;
	memcpy(receive_report->smscrefnum ,tmp, 4);
	/*accepted time*/ 
	octsrc += iusedbyte;    
	iusedbyte = line_scan_IA5_hex(octsrc,14, tmp);
	if(iusedbyte < 1) goto error_receive;
	memcpy(receive_report->accepttime ,tmp, 4);
	/*status*/
	octsrc += iusedbyte;    
	iusedbyte = line_scan_IA5_hex(octsrc, 1, tmp);
	if(iusedbyte < 1) goto error_receive;
	octetlen = 1; 
	receive_report->status = get_variable_value(tmp, &octetlen);
	octsrc += iusedbyte;
	if(receive_report->status != 6) /*6 means unable to deliver , but retry*/
	{
	    iusedbyte = line_scan_IA5_hex(octsrc,14, tmp);
	    if(iusedbyte < 1) goto error_receive;
	    memcpy(receive_report->completetime ,tmp, 14);
	}
	else
	{
	    iusedbyte = line_scan_IA5_hex(octsrc,14, tmp);
	    if(iusedbyte < 1) goto error_receive;
	    memcpy(receive_report->intermediatime ,tmp, 14);
	}
	if(receive_report->status == 6 || receive_report->status == 1) /*unable to deliver ,both case */
	{       
	    octsrc += iusedbyte;
	    iusedbyte = line_scan_IA5_hex(octsrc, 1, tmp);
	    if(iusedbyte < 1) goto error_receive;
	    octetlen = 1;
	    receive_report->failreason = get_variable_value(tmp, &octetlen);
	}
	/*deliver orignate address length*/
	octsrc += iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc, 1, tmp);
	if(iusedbyte < 1) goto error_receive;
	octetlen = 1;
	receive_report->origaddlen = get_variable_value(tmp, &octetlen);
	/*orignate address*/
	octsrc += iusedbyte;
	iusedbyte = line_scan_IA5_hex(octsrc, receive_report->origaddlen, tmp);
	if(iusedbyte < 1) goto error_receive;
	receive_report->origadd = octstr_create_from_data((char *)tmp,  receive_report->msisdnlen);
	/* invoke time */
	octsrc += iusedbyte;     
	iusedbyte = line_scan_IA5_hex(octsrc,14, tmp);
	if(iusedbyte < 1){
	    goto error_receive;
	}
	memcpy(receive_report->invoketime ,tmp, 14);
	(*desmsg)->msgbody = receive_report;
	break;
    }
    return 1;

no_msg:
    info(0,"sema_decode: msg is empty");
    return 0;
error_receive:
    gw_free(receive_report);
    goto error_msg;
error_submit:
    gw_free(submit_result);
    goto error_msg;
error_deliver:
    gw_free(receive_sm);
    goto error_msg;
error_msg:
    info(0,"sema_decode:msg parameter is not recognized or unsupported");
    return 0;
}


static int sema_encode_msg(sema_msg* pmsg, char* str) {
    struct sm_submit_invoke *submit_sm = NULL;
    Octstr *IA5msg = NULL;
    int tSize = 0;
    unsigned char oc1byte[10];
    IA5msg = octstr_create("");	
    switch(pmsg->type)
    {
    case 'S':
	submit_sm = (struct sm_submit_invoke *) pmsg->msgbody; 
	write_variable_value(submit_sm->msisdnlen, oc1byte); /*msisdn len*/
	line_append_hex_IA5(IA5msg, oc1byte,1);
	line_append_hex_IA5(IA5msg, 
			    (unsigned char *)octstr_get_cstr(submit_sm->msisdn),
			    octstr_len(submit_sm->msisdn)); /*msisdn*/
	write_variable_value(submit_sm->smereftype, oc1byte);/*smetype*/
	line_append_hex_IA5(IA5msg, oc1byte,1);
	line_append_hex_IA5(IA5msg, submit_sm->smerefnum,4);/*sme reference*/
	write_variable_value(submit_sm->priority, oc1byte);/*priority*/
	line_append_hex_IA5(IA5msg, oc1byte,1);
	write_variable_value(submit_sm->origaddlen, oc1byte); /*orignating address length*/
	line_append_hex_IA5(IA5msg, oc1byte,1);
	line_append_hex_IA5(IA5msg, 
			    (unsigned char *)octstr_get_cstr(submit_sm->origadd),
			    octstr_len(submit_sm->origadd)); /*orignating address*/
	write_variable_value(submit_sm->validperiodtype, oc1byte); /*valid period type*/
	line_append_hex_IA5(IA5msg, oc1byte,1);
	write_variable_value(submit_sm->validperiodrela, oc1byte); /*relative period*/
	line_append_hex_IA5(IA5msg, oc1byte,1);
	write_variable_value(submit_sm->DCS, oc1byte);/*data code scheme*/
	line_append_hex_IA5(IA5msg, oc1byte,1);
	write_variable_value(submit_sm->statusreportrequest, oc1byte);/*request report*/
	line_append_hex_IA5(IA5msg, oc1byte,1);
	write_variable_value(submit_sm->protocal, oc1byte);/*protocal id*/
	line_append_hex_IA5(IA5msg, oc1byte, 1);
	write_variable_value(submit_sm->replypath, oc1byte);/*use reply path*/
	line_append_hex_IA5(IA5msg, oc1byte, 1);
	
            /*text size in 7 bits char*/
      	tSize = internal_char_hex_to_IA5(submit_sm->textsizeseptet,oc1byte);
	octstr_insert_data(IA5msg, octstr_len(IA5msg), (char *)oc1byte, tSize);

            /*text size in 8 bits char*/
	tSize = internal_char_hex_to_IA5(submit_sm->textsizeoctect,oc1byte);  
 	octstr_insert_data(IA5msg, octstr_len(IA5msg),(char *) oc1byte, tSize);

	line_append_hex_IA5(IA5msg,		      
			    (unsigned char *)octstr_get_cstr(submit_sm->shortmsg),
			    submit_sm->textsizeoctect); /*msg text*/
	memcpy(str,octstr_get_cstr(IA5msg),octstr_len(IA5msg));
	octstr_destroy(IA5msg);
	return 1;
    }
    return 0;
}


static int line_scan_hex_GSM7(unsigned char* from, 
			      int octects ,int spetets, unsigned char* to)
{
    char* cin2 =NULL;
    unsigned char c;
    char cin7[8];
    int i, pos, value;

    int lenin2=octects*8;
    cin2 = gw_malloc(lenin2);

    memset(cin2,48,lenin2); /*make many zeros*/
    /*tranverse the octects first, so ABC -> CBA(in bin form)*/
    for(i = 0; i < octects; i ++)
    {
	c = *(from + i);
      
	if(c & 1) 
	    cin2[(octects-1-i)*8 +7] = 49;
	if(c & 2) 
	    cin2[(octects-1-i)*8 +6] = 49;
	if(c & 4) 
	    cin2[(octects-1-i)*8 +5] = 49;
	if(c & 8) 
	    cin2[(octects-1-i)*8 +4] = 49;
	if(c & 16) 
	    cin2[(octects-1-i)*8 +3] = 49;
	if(c & 32) 
	    cin2[(octects-1-i)*8 +2] = 49;
	if(c & 64) 
	    cin2[(octects-1-i)*8 +1] = 49;
	if(c & 128) 
	    cin2[(octects-1-i)*8] = 49;
    }
  
    i= 1;
    while( i <= spetets ){
	pos=lenin2 -1 -(i*7 -1);
	memset(cin7,0,sizeof(cin7));
	memcpy(cin7, cin2 + pos, 7);
	value = 0;

	if(cin7[6] == '1')
	    value += 1;
	if(cin7[5] == '1')
	    value += 2;
	if(cin7[4] == '1')
	    value += 4;
	if(cin7[3] == '1')
	    value += 8;
	if(cin7[2] == '1')
	    value += 16;
	if(cin7[1] == '1')
	    value += 32;
	if(cin7[0] == '1')
	    value += 64;
	  
	to[i-1]=internal_char_hex_to_gsm(value);
	i +=1;
    }
    return i;
  
}

/* check SMS2000 Version 4.0 B.4.2.3 */
static int line_append_hex_IA5(Octstr* des, unsigned char* src, int len)
{

    unsigned char IA5char[3];
    unsigned char tmp[1024];
    int j=0;
    int i=0, iall=0;
    for(i=0; i<len; i++)
    {
	memset(IA5char, 0, sizeof(IA5char));
	j=internal_char_hex_to_IA5(*(src+i),IA5char);
	if(j >0){
	    memcpy(tmp+iall,IA5char,j);
	    iall += j;
	}
    }
    octstr_insert_data(des,octstr_len(des),(char *)tmp,iall);
    return iall;
}


/* check SMS2000 Version 4.0 B.4.2.3 */
static int line_scan_IA5_hex(char* from, 
			     int hexnum, unsigned char* to)
{
    unsigned char cha[1];
    int cn =0, cnall = 0, i = 0;
    char *tmpfrom = NULL;
    tmpfrom = from;
    for(i = 0; i< hexnum; i++)
    {
	cn=internal_char_IA5_to_hex(tmpfrom, cha);
	if(cn >0)
	{
	    memcpy(to+i,cha,1);
	    tmpfrom += cn;
	    cnall += cn;
	}
	else
	    return -1;
    }
    return cnall;
}


static unsigned char internal_char_hex_to_gsm(unsigned char from)
{
    switch (from){
    case 0x00: return '@';	
    case 0x01: return '£';
    case 0x02: return '$';
    case 0x03: return '¥';
    case 0x04: return 'è';
    case 0x05: return 'é';
    case 0x06: return 'ù';
    case 0x07: return 'ì';
    case 0x08: return 'ò';
    case 0x09: return 'Ç';
    case 0x0A: return '\n';
    case 0x0B: return 'Ø';
    case 0x0C: return 'ø';
    case 0x0D: return '\r';
    case 0x0E: return 'Å';
    case 0x0F: return 'å';
    case 0x10: return 'D';
    case 0x11: return ' ';
    case 0x12: return 'F';
    case 0x13: return 'G';
    case 0x14: return 'L';
    case 0x15: return 'W';
    case 0x16: return 'P';
    case 0x17: return 'Y';
    case 0x18: return 'S';
    case 0x19: return 'Q';
    case 0x1A: return 'X';
    case 0x1B: return ' ';
    case 0x1C: return 'Æ';
    case 0x1D: return 'æ';
    case 0x1E: return 'b';
    case 0x1F: return 'É';
    case 0x5B: return 'Ä';
    case 0x5C: return 'Ö';
    case 0x5D: return 'Ñ';
    case 0x5E: return 'Ü';
    case 0x5F: return '§';
    case 0x60: return '¿';
    case 0x7B: return 'a';
    case 0x7C: return 'ö';
    case 0x7D: return 'ñ';
    case 0x7E: return 'ü';
    case 0x7F: return 'à';
    default: return from;
    }
}

/* check SMS2000 Version 4.0 B.4.2.3 */
static int internal_char_hex_to_IA5(unsigned char from, unsigned char * to){

    if(from <= 0x1F)
    {
	to[0] = '^';
	to[1] = 0x40 + from;
	return 2;
    }
    else if(from ==0x5C) 
    {
	to[0] = 0x5C;
	to[1] = 0x5C;
	return 2;
    }
    else if(from == 0x5E)
    {	
	to[0] = 0x5C; 
	to[1] = 0x5E;
	return 2;
    }
    else if(from == 0x60)
    {	
	to[0] = 0x5C;
	to[1] = 0x60;
	return 2;
    }
    else if(from == 0x7E)
    {	
	to[0] = 0x5C;
	to[1] = 0x7E;
	return 2;
    }
    else if(from >= 0x20 && from <= 0x7E)
    {
	to [0] = from; 
	return 1;
    }
    else if(from == 0x7F)
    {
	to[0] = 0x5E;
	to[1] = 0x7E;
	return 2;
    }
    else if(from >= 0x80 && from <=0x9F)
    {
	to[0] = 0x7E;
	to[1] = from -0x40;
	return 2;
    }
    else if(from >= 0xA0 && from <=0xFE)
    {
	to[0] = 0x60;
	to[1] = from -0x80;
	return 2;
    }
    else if(from == 0xFF)
    {
	to[0] =to[1] = 0x7E;
	return 2;
    }
    else
	
	return -1;
}

static int internal_char_IA5_to_hex(char *from, unsigned char * to){
    int ret = -1;
    int len = strlen(from);

    if(*from == '^' && len >= 2 &&
       *(from +1) == '~') 
    {
	*to = 0x7F;	
	ret = 2;
    }
    else if(*from ==0x5C && len >= 2 &&
	    *(from +1) == 0x5C) 
    {   
	*to= 0x5C;		
	ret = 2;
    }
    else if(*from == 0x5C && len >= 2 &&
	    *(from+1) == 0x5E)
    {	
	*to= 0x5E;	
	ret = 2;
    }
    else if(*from == 0x5C && len >= 2 &&
	    *(from+1) == 0x60)
    {	
	*to= 0x60;
	ret = 2;
    }
    else if(*from == 0x5C && len >=2 &&
	    *(from+1) == 0x7E)
    {	
	*to= 0x7E;
	ret = 2;
    }
    else if(*from == '^' && len >= 2 && 
	    (*(from +1) >= 0x40 &&  *(from +1) <= 0x5F))
    {		
	*to = *(from +1) -0x40;		
	ret = 2;
    }
    else if(*from == '~' && len >= 2 && 
	    (*(from +1) >= 0x40 && *(from +1) <= 0x5F))
    {		
	*to = *(from +1) +0x40;	
	ret = 2;
    }
    else if(*from == '`' && len >= 2 &&
	    (*(from+1) >= 0x20 && *(from +1) <= 0x7E))
    {	
	*to = *(from +1) +0x80;
	ret = 2;
    }
    else if(*from >=0x20 && 
	    *from <=0x7E)
    {
	*to= *from;
	ret = 1;
    }
		
    return ret;
}
		

static void increment_counter(void)
{
    if(sema_counter[3] == 0x39)
	sema_counter[3] = 0x30;
    else
    {
	sema_counter[3] += 0x01;
	return;
    }
    if(sema_counter[2] == 0x39)
	sema_counter[2] = 0x30;
    else
    {
	sema_counter[2] += 0x01;
	return;
    }
    if(sema_counter[1] == 0x39)
	sema_counter[1] = 0x30;
    else
    {
	sema_counter[1] += 0x01;
	return;
    }	
    if(sema_counter[0] == 0x39)
	sema_counter[0] = 0x30;
    else
	sema_counter[0] += 0x01;
    return;
}	

/* check SMS2000 Version 4.0 B.4.2.2 */
static unsigned char pack_continous_byte(int encode, 
					 int isfirst, int follownum)
{
    char bin[4];
    int value;

    memset(bin, 0, 4);
    value = 0;

    if(isfirst == 1)
	strncpy(bin,"0101",4);
    else
	strncpy(bin,"0110",4);

    if(bin[3] == '1')
	value += 16;
    if(bin[2] == '1')
	value += 32;
    if(bin[1] == '1')
	value += 64;
    if(bin[0] == '1') /* although it's impossible */
	value += 128;
    return (value+follownum);
}

/* check SMS2000 Version 4.0 B.4.2.2 */
static int unpack_continous_byte(unsigned char continueCount, 
				 int * encode,
				 int * isfirst, 
				 int * follownum)
{
    int rest = 0;
    int head = 0;
      
    if(continueCount & 1)
	rest +=1;
    if(continueCount & 2)
	rest +=2;
    if(continueCount & 4)
	rest += 4;
    if(continueCount & 8)
	rest += 8;

    *follownum = rest;

    if(continueCount & 16)
	head += 1;
	
    if(continueCount & 32)
	head += 2;

    if(continueCount & 64)
	head += 4;

    if(continueCount & 128) /* though not possible */
	head += 8;


    *encode = *isfirst = -1;

    if(head == 5)
    {
	*encode =LINE_ENCODE_IA5;
	*isfirst = 1;
    }
    else if(head == 6)
    {
	*encode =LINE_ENCODE_IA5;
	*isfirst = 0;
    }
    else if(head == 4)
    {
	*encode =LINE_ENCODE_HEX;
	*isfirst = 1;
    }
    else if(head == 3)
    {
	*encode =LINE_ENCODE_HEX;
	*isfirst = 0;
    }
    else if(head == 7)
    {
	*encode =LINE_ENCODE_BIN;
	*isfirst = 1;
    }
    else if(head == 2)
    {
	*encode =LINE_ENCODE_BIN;
	*isfirst = 0;
    }
    if(*encode != -1 && *isfirst != -1)
	return 0;
    else
	return -1;
}

