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
 * test_radius_acct.c - program to test RADIUS accounting proxy thread
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <unistd.h>
#include <string.h>

#include "gwlib/gwlib.h"
#include "radius/radius_pdu.h"


/*
 * Updates the internal RADIUS mapping table. Returns 1 if the 
 * mapping has been new and put into the table, otherwise if it's 
 * a duplicate returns 0.
 */
static int update_table(RADIUS_PDU *pdu, Dict **table)
{
    Octstr *client_ip, *msisdn;
    Octstr *type;
    int ret = 0;
    
    client_ip = msisdn = NULL;

    /* only add if we have a Accounting-Request PDU */
    if (pdu->type == 0x04) {

        /* check if we have a START or STOP event */
        type = dict_get(pdu->attr, octstr_imm("Acct-Status-Type"));
        
        /* grep the needed data */
        client_ip = dict_get(pdu->attr, octstr_imm("Framed-IP-Address"));
        msisdn = dict_get(pdu->attr, octstr_imm("Calling-Station-Id"));

        if (octstr_compare(type, octstr_imm("1")) == 0) {
            /* START */
            if (dict_get(*table, client_ip) == NULL) {
                dict_put(*table, client_ip, msisdn); 
                info(0, "RADIUS: Mapping `%s <-> %s' added.",
                     octstr_get_cstr(client_ip), octstr_get_cstr(msisdn));
                ret = 1;
            } else {
                warning(0, "RADIUS: Duplicate mapping for `%s <-> %s' received",
                        octstr_get_cstr(client_ip), octstr_get_cstr(msisdn));
            }
        }
        else if (octstr_compare(type, octstr_imm("2")) == 0) {
            /* STOP */
            msisdn = dict_get(*table, client_ip);
            dict_remove(*table, client_ip);
        }
        else {
            error(0, "RADIUS: unknown Acct-Status-Type `%s' received.", 
                  octstr_get_cstr(type));
        }
    }

    octstr_destroy(client_ip);
    octstr_destroy(msisdn);

    return ret;
}

static void server(int lport, int pport) 
{
	int i;
    int ss, cs; /* server and client socket */
	Octstr *data, *from, *addr;
    /* pid_t pid = getpid(); */
    Dict *radius_table;

   	/* create client binding */
	cs = udp_client_socket();
	addr = udp_create_address(octstr_create("localhost"), pport);

    /* create server binding */
	ss = udp_bind(lport, "0.0.0.0");
	if (ss == -1)
		panic(0, "Couldn't set up server socket for port %d.", lport);

    /* init hash table */
    radius_table = dict_create(30, (void (*)(void *))octstr_destroy);

    i = 1;
	while (1) {
        RADIUS_PDU *pdu, *r;
        Octstr *rdata;
        int forward = 0;

        /* get request */
		if (udp_recvfrom(ss, &data, &from) == -1)
			panic(0, "Couldn't receive request data from NAS");
		info(0, "Got data from NAS <%s:%d>", 
             octstr_get_cstr(udp_get_ip(from)), udp_get_port(from));

        /*
        debug("",0,"Saving PDU packet");
        f = fopen(octstr_get_cstr(octstr_format("/tmp/radius-pdu.%ld.%d", pid, i)), "w");
        octstr_print(f, data);
        fclose(f);
        */
        
        pdu = radius_pdu_unpack(data);
        info(0, "PDU type: %s", pdu->type_name);

        /* XXX authenticator md5 check does not work?! */
        /* radius_authenticate_pdu(pdu, data, octstr_imm("radius")); */

        /* store to hash table if not present yet */
        forward = update_table(pdu, &radius_table);

        /* create response PDU for NAS */
        r = radius_pdu_create(0x05, pdu);

        /* 
         * create response authenticator 
         * code+identifier(req)+length+authenticator(req)+(attributes)+secret 
         */
        r->u.Accounting_Response.identifier = pdu->u.Accounting_Request.identifier;
        r->u.Accounting_Response.authenticator = 
            octstr_duplicate(pdu->u.Accounting_Request.authenticator);

        rdata = radius_pdu_pack(r);

        /* creates response autenticator in encoded PDU */
        radius_authenticate_pdu(r, &rdata, octstr_imm("radius"));

        /* forward request to remote RADIUS server only if table updated */
        if (forward) {
            if (udp_sendto(cs, data, addr) == -1)
                panic(0, "Couldn't send to remote RADIUS.");
            if (udp_recvfrom(cs, &data, &from) == -1)
                panic(0, "Couldn't receive from remote RADIUS.");
            info(0, "Got data from remote RADIUS <%s:%d>", 
                 octstr_get_cstr(udp_get_ip(from)), udp_get_port(from));
        }

        /* send response to NAS */
        if (udp_sendto(ss, rdata, from) == -1)
			panic(0, "Couldn't send response data to NAS.");

        radius_pdu_destroy(pdu);
        radius_pdu_destroy(r);

        octstr_destroy(rdata);
        i++;

        debug("",0,"Mapping table contains %ld elements", 
              dict_key_count(radius_table)); 
	}
}

int main(int argc, char **argv) {
	int lport, pport;
	
	gwlib_init();

	if (argc != 3)
		panic(0, "usage: test_radius_acct <your RADIUS acct port> <remote RADIUS port>");
	
	lport = atoi(argv[1]);
    pport = atoi(argv[2]);

    server(lport, pport);

	return 0;
}
