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
 * test_radius_pdu.c - test RADIUS PDU packing and unpacking.
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "radius/radius_pdu.h"

int main(int argc, char **argv)
{
    Octstr *data, *filename, *rdata;
    RADIUS_PDU *pdu, *r;

    gwlib_init();

    data = filename = rdata = NULL;
    get_and_set_debugs(argc, argv, NULL);

    if (argc <= 1) {
        debug("",0,"Usage: %s [filename containing raw RADIUS PDU]", argv[0]);
        goto error;
    }

    filename = octstr_create(argv[1]);
    if ((data = octstr_read_file(octstr_get_cstr(filename))) == NULL)
        goto error;

    debug("",0,"Calling radius_pdu_unpack() now");
    pdu = radius_pdu_unpack(data);

    debug("",0,"PDU type code: %ld", pdu->u.Accounting_Request.code);
    debug("",0,"PDU identifier: %ld", pdu->u.Accounting_Request.identifier);
    debug("",0,"PDU length: %ld", pdu->u.Accounting_Request.length);
    octstr_dump_short(pdu->u.Accounting_Request.authenticator,0, "PDU authenticator");

    /* XXX authenticator md5 check does not work?! */
    /* radius_authenticate_pdu(pdu, data, octstr_imm("radius")); */

    /* create response PDU */
    r = radius_pdu_create(0x05, pdu);

    /* create response authenticator 
     * code+identifier(req)+length+authenticator(req)+(attributes)+secret 
     */
    r->u.Accounting_Response.identifier = pdu->u.Accounting_Request.identifier;
    r->u.Accounting_Response.authenticator = octstr_duplicate(pdu->u.Accounting_Request.authenticator);

    rdata = radius_pdu_pack(r);

    /* creates response autenticator in encoded PDU */
    radius_authenticate_pdu(r, &rdata, octstr_imm("radius"));

    octstr_dump_short(rdata, 0, "Encoded Response PDU");

    debug("",0,"Destroying RADIUS_PDUs");
    radius_pdu_destroy(pdu);
    radius_pdu_destroy(r);

error:    
    octstr_destroy(data);
    octstr_destroy(rdata);
    octstr_destroy(filename);

    gwlib_shutdown();

    return 0;
}
