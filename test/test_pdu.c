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
 * test_pdu.c - test gw/wtp_pdu packing and unpacking.
 *
 * Richard Braakman
 */
 
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "wap/wtp_pdu.h"
#include "wap/wsp_pdu.h"

int main(int argc, char **argv) {
	int i;
	Octstr *packet = NULL;
	Octstr *newpacket = NULL;
	WTP_PDU *pdu = NULL;
	Octstr *wsp_data = NULL;
	WSP_PDU *wsp = NULL;

	gwlib_init();

	for (i = 1; i < argc; i++) {
		octstr_destroy(packet);  packet = NULL;
		octstr_destroy(newpacket);  newpacket = NULL;
		octstr_destroy(wsp_data);  wsp_data = NULL;
		wtp_pdu_destroy(pdu);  pdu = NULL;
		wsp_pdu_destroy(wsp);  wsp = NULL;

		packet = octstr_read_file(argv[i]);
		pdu = wtp_pdu_unpack(packet);
		if (!pdu) {
			warning(0, "Unpacking PDU %s failed", argv[i]);
			continue;
		}
		debug("test", 0, "PDU %s:", argv[i]);  
		wtp_pdu_dump(pdu, 0);
		newpacket = wtp_pdu_pack(pdu);
		if (!newpacket) {
			warning(0, "Repacking PDU %s failed", argv[i]);
			continue;
		}
		if (octstr_compare(packet, newpacket) != 0) {
			error(0, "Repacking PDU %s changed it", argv[i]);
			debug("test", 0, "Original:");
			octstr_dump(packet, 1);
			debug("test", 0, "New:");
			octstr_dump(newpacket, 1);
			continue;
		}
		if (pdu->type == Invoke) {
			wsp_data = pdu->u.Invoke.user_data;
		} else if (pdu->type == Result) {
			wsp_data = pdu->u.Result.user_data;
		} else {
			continue;
		}
		wsp_data = octstr_duplicate(wsp_data);

		wsp = wsp_pdu_unpack(wsp_data);
		if (!wsp) {
			warning(0, "Unpacking WSP data in %s failed", argv[i]);
			continue;
		}
		wsp_pdu_dump(wsp, 0);
		octstr_destroy(newpacket);
		newpacket = wsp_pdu_pack(wsp);
		if (!newpacket) {
			warning(0, "Repacking WSP data in %s failed", argv[i]);
			continue;
		}
		if (octstr_compare(wsp_data, newpacket) != 0) {
			error(0, "Repacking WSP data in %s changed it",
				argv[i]);
			debug("test", 0, "Original:");
			octstr_dump(wsp_data, 1);
			debug("test", 0, "New:");
			octstr_dump(newpacket, 1);
			continue;
		}
	}

	octstr_destroy(packet);
	octstr_destroy(newpacket);
	wtp_pdu_destroy(pdu);

	gwlib_shutdown();
    	return 0;
}
