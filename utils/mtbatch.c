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
 * mtbatch.c - an MT batch run utility for bearerbox
 *
 * This utility reads in a content file which has the SMS text message and
 * a receivers file, which has receiver numbers in each line. It connects
 * to bearerbox as if it would be a smsbox and issues the SMS sequentially
 * to bearerbox.
 *
 * Stipe Tolj <stolj@kannel.org>
 * Vincent Chavanis <v.chavanis@telemaque.fr>
 *
 * XXX Add UDH capabilities.
 * XXX Support more charsets than 7-bit.
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

#include "msg.h"
#include "sms.h"
#include "dlr.h"
#include "bb.h"
#include "shared.h"
#include "heartbeat.h"

static char *pid_file;
static Octstr *smsbox_id = NULL;
static Octstr *content = NULL;
static List *lines = NULL;
static Octstr *bb_host;
static long bb_port;
static int bb_ssl;
static Counter *counter;
static Octstr *service = NULL;
static Octstr *account = NULL;
static Octstr *from = NULL;
static int dlr_mask = 0;
static Octstr *dlr_url = NULL;
static Octstr *smsc_id = NULL;
static double delay = 0;


static void write_pid_file(void) {
    FILE *f;
        
    if (pid_file != NULL) {
	f = fopen(pid_file, "w");
	fprintf(f, "%d\n", (int)getpid());
	fclose(f);
    }
}

/***********************************************************************
 * Communication with the bearerbox.
 */


/* 
 * Identify ourself to bearerbox for smsbox-specific routing inside bearerbox.
 * Do this even while no smsbox-id is given to unlock the sender thread in
 * bearerbox.
 */
static void identify_to_bearerbox(void)
{
    Msg *msg;
    
    msg = msg_create(admin);
    msg->admin.command = cmd_identify;
    msg->admin.boxc_id = octstr_duplicate(smsbox_id);
    write_to_bearerbox(msg);
}


/*
 * Read an Msg from the bearerbox and send it to the proper receiver
 * via a List. At the moment all messages are sent to the smsbox_requests
 * List.
 */
static void read_messages_from_bearerbox(void *arg)
{
    time_t start, t;
    unsigned long secs;
    unsigned long total_s, total_f, total_ft, total_b, total_o;
    Msg *msg;

    total_s = total_f = total_ft = total_b = total_o = 0;
    start = t = time(NULL);
    while (program_status != shutting_down) {
        int ret;

        /* block infinite for reading messages */
        ret = read_from_bearerbox(&msg, 0.5);
        if (ret == -1)
            break;
        else if (ret == 1) /* timeout */
            continue;
        else if (msg == NULL) /* just to be sure, may not happens */
            break;

        if (msg_type(msg) == admin) {
            if (msg->admin.command == cmd_shutdown ||
                msg->admin.command == cmd_restart) {
                info(0, "Bearerbox told us to die");
                program_status = shutting_down;
            }
            /*
             * XXXX here should be suspend/resume, add RSN
             */
            msg_destroy(msg);
        } else if (msg_type(msg) == ack) {
            counter_increase(counter);
            switch (msg->ack.nack) {
                case ack_success:
                    total_s++;
                    break;
                case ack_failed:
                    total_f++;
                    break;
                case ack_failed_tmp:
                    total_ft++;
                    break;
                case ack_buffered:
                    total_b++;
                    break;
            }
            msg_destroy(msg);
        } else {
            warning(0, "Received other message than ack/admin, ignoring!");
            msg_destroy(msg);
            total_o++;
        }
    }
    secs = difftime(time(NULL), start);
    info(0, "Received acks: %ld success, %ld failed, %ld failed temporarly, %ld queued, %ld other in %ld seconds "
         "(%.2f per second)", total_s, total_f, total_ft, total_b, total_o, secs,
         (float)(total_s+total_f+total_ft+total_b) / secs);
}

/*
 * Send a message to the bearerbox for delivery to a phone.
 * Return >= 0 for success & count of splitted sms messages, 
 * -1 for failure.  Does not destroy the msg.
 */
static int send_message(Msg *msg)
{
    unsigned long msg_count;
    List *list;
    
    gw_assert(msg != NULL);
    gw_assert(msg_type(msg) == sms);
    
    /* 
     * Encode our smsbox-id to the msg structure.
     * This will allow bearerbox to return specific answers to the
     * same smsbox, mainly for DLRs and SMS proxy modes.
     */
    if (smsbox_id != NULL) {
        msg->sms.boxc_id = octstr_duplicate(smsbox_id);
    }
    
    list = sms_split(msg, NULL, NULL, NULL, NULL, 1, 0, 100, MAX_SMS_OCTETS);
    msg_count = gwlist_len(list);
    gwlist_destroy(list, msg_destroy_item);

    debug("sms", 0, "message length %ld, sending %ld messages", 
          octstr_len(msg->sms.msgdata), msg_count);

    if (delay > 0)
        gwthread_sleep(delay);

    /* pass message to bearerbox */
    if (deliver_to_bearerbox(msg) != 0)
        return -1;
    
    return msg_count;
}


static void help(void) 
{
    info(0, "Usage: mtbatch [options] content-file receivers-file ...");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-b host");
    info(0, "    defines the host of bearerbox (default: localhost)");
    info(0, "-p port");
    info(0, "    the smsbox port to connect to (default: 13002)");
    info(0, "-s");
    info(0, "    inidicatr to use SSL for bearerbox connection (default: no)");
    info(0, "-i smsbox-id");
    info(0, "    defines the smsbox-id to be used for bearerbox connection (default: none)");
    info(0, "-f sender");
    info(0, "    which sender address should be used");
    info(0, "-D dlr-mask");
    info(0, "    defines the dlr-mask");
    info(0, "-u dlr-url");
    info(0, "    defines the dlr-url");
    info(0, "-n service");
    info(0, "    defines which service name should be logged (default: none)");
    info(0, "-a account");
    info(0, "    defines which account name should be logged (default: none)");
    info(0, "-d seconds");
    info(0, "    delay between message sending to bearerbox (default: 0)");
    info(0, "-r smsc-id");
    info(0, "    use a specific route for the MT traffic");
}

static void init_batch(Octstr *cfilename, Octstr *rfilename)
{
    Octstr *receivers;
    long lineno = 0; 

    content = octstr_read_file(octstr_get_cstr(cfilename)); 
    octstr_strip_crlfs(content);
    if (content == NULL) 
        panic(0,"Can not read content file `%s'.", 
              octstr_get_cstr(cfilename));
    info(0,"SMS-Text: <%s>", octstr_get_cstr(content));

    info(0,"Loading receiver list. This may take a while...");
    receivers = octstr_read_file(octstr_get_cstr(rfilename)); 
    if (receivers == NULL) 
        panic(0,"Can not read receivers file `%s'.", 
              octstr_get_cstr(rfilename)); 

    lines = octstr_split(receivers, octstr_imm("\n")); 
    lineno = gwlist_len(lines);
    if (lineno <= 0) 
        panic(0,"Receiver file seems empty!");

    info(0,"Receivers file `%s' contains %ld destination numbers.",
         octstr_get_cstr(rfilename), lineno);

    counter = counter_create();
}

static unsigned long run_batch(void)
{
    Octstr *no;
    unsigned long linerr = 0;
    unsigned long lineno = 0;

    while ((no = gwlist_consume(lines)) != NULL) {
        if (octstr_check_range(no, 0, 256, gw_isdigit)) {
            Msg *msg;
        
            lineno++;

            msg = msg_create(sms);

            msg->sms.smsc_id = smsc_id ? octstr_duplicate(smsc_id) : NULL;
            msg->sms.service = service ? octstr_duplicate(service) : NULL;
            msg->sms.sms_type = mt_push;
            msg->sms.sender = octstr_duplicate(from);
            msg->sms.receiver = octstr_duplicate(no);
            msg->sms.account = account ? octstr_duplicate(account) : NULL;
            msg->sms.msgdata = content ? octstr_duplicate(content) : octstr_create("");
            msg->sms.dlr_mask = dlr_mask;
            msg->sms.dlr_url = octstr_duplicate(dlr_url);
            msg->sms.udhdata = octstr_create("");
            msg->sms.coding = DC_7BIT;

            if (send_message(msg) < 0) {
                linerr++;
                info(0,"Failed to send message at line <%ld> for receiver `%s' to bearerbox.",
                      lineno, octstr_get_cstr(no));
                msg_destroy(msg);
            }
            octstr_destroy(no);
        }
    }
    info(0,"mtbatch has processed %ld messages with %ld errors", lineno, linerr);
    return lineno;
} 

int main(int argc, char **argv)
{
    int opt;
    unsigned long sended = 0;
    Octstr *cf, *rf;

    gwlib_init();

    bb_host = octstr_create("localhost");
    bb_port = 13001;
    bb_ssl = 0;
        
    while ((opt = getopt(argc, argv, "hv:b:p:si:n:a:f:D:u:d:r:")) != EOF) {
        switch (opt) {
            case 'v':
                log_set_output_level(atoi(optarg));
                break;
            case 'b':
                octstr_destroy(bb_host);
                bb_host = octstr_create(optarg);
                break;
            case 'p':
                bb_port = atoi(optarg);
                break;
            case 's':
                bb_ssl = 1;
                break;
            case 'i':
                smsbox_id = octstr_create(optarg);
                break;
            case 'n':
                service = octstr_create(optarg);
                break;
            case 'a':
                account = octstr_create(optarg);
                break;
            case 'f':
                from = octstr_create(optarg);
                break;
            case 'D':
                dlr_mask = atoi(optarg);
                break;
            case 'u':
                dlr_url = octstr_create(optarg);
                break;
            case 'd':
                delay = atof(optarg);
                break;
            case 'r':
                smsc_id = octstr_create(optarg);
                break;
            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping.");
        }
    }
    
    if (optind == argc || argc-optind < 2) {
        help();
        exit(1);
    }

    /* check some mandatory elements */
    if (from == NULL)
        panic(0,"Sender address not specified. Use option -f to specify sender address.");

    if ((DLR_IS_ENABLED(dlr_mask) && dlr_url == NULL) || (!DLR_IS_ENABLED(dlr_mask) && dlr_url != NULL))
        panic(0,"dlr-url address OR dlr-mask not specified. Use option -D or -u to specify dlr values");

    rf = octstr_create(argv[argc-1]);
    cf = octstr_create(argv[argc-2]);

    report_versions("mtbatch");
    write_pid_file();
 
    init_batch(cf, rf);

    connect_to_bearerbox(bb_host, bb_port, bb_ssl, NULL /* bb_our_host */);
    identify_to_bearerbox();
    gwthread_create(read_messages_from_bearerbox, NULL);

    sended = run_batch();

    /* avoid exiting before sending all msgs */
    while(sended > counter_value(counter)) {
         gwthread_sleep(0.1);
    }

    program_status = shutting_down;
    gwthread_join_all();

    octstr_destroy(bb_host);
    octstr_destroy(smsbox_id);
    octstr_destroy(content);
    octstr_destroy(service);
    octstr_destroy(account);
    octstr_destroy(dlr_url);
    octstr_destroy(smsc_id);
    counter_destroy(counter);
    gwlist_destroy(lines, octstr_destroy_item); 
   
    gwlib_shutdown();

    return 0;
}
