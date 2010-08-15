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
 * test_urltrans.c - a simple program to test the URL translation module
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#include <stdlib.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "gw/urltrans.h"

static void help(void) {
	info(0, "Usage: test_urltrans [-r repeats] foo.smsconf pattern ...\n"
		"where -r means the number of times the test should be\n"
		"repeated.");
}

int main(int argc, char **argv) {
	int i, opt;
	long repeats;
	URLTranslationList *list;
	URLTranslation *t;
	Cfg *cfg;
	Octstr *name;
	
	gwlib_init();

	repeats = 1;

	while ((opt = getopt(argc, argv, "hr:")) != EOF) {
		switch (opt) {
		case 'r':
			repeats = atoi(optarg);
			break;

		case 'h':
			help();
			exit(0);
		
		case '?':
		default:
			error(0, "Invalid option %c", opt);
			help();
			panic(0, "Stopping.");
		}
	}

	if (optind + 1 >= argc) {
		error(0, "Missing arguments.");
		help();
		panic(0, "Stopping.");
	}
	name = octstr_create(argv[optind]);
	cfg = cfg_create(name);
	octstr_destroy(name);
	if (cfg_read(cfg) == -1)
		panic(0, "Couldn't read configuration file.");
	
	list = urltrans_create();
	if (urltrans_add_cfg(list, cfg) == -1)
		panic(0, "Error parsing configuration.");

	while (repeats-- > 0) {
		for (i = optind + 1; i < argc; ++i) {
		        Msg *msg = msg_create(sms);
		        msg->sms.msgdata = octstr_create(argv[i]);
			t = urltrans_find(list, msg);
			info(0, "type = %d", urltrans_type(t));
			msg_destroy(msg);
		}
	}
	urltrans_destroy(list);
	cfg_destroy(cfg);
	
	gwlib_shutdown();
	
	return 0;
}
