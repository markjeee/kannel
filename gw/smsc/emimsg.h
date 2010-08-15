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
 * emimsg.h
 *
 * Declarations needed with EMI messages
 * Uoti Urpala 2001 */

#ifndef EMIMSG_H
#define EMIMSG_H


#include "gwlib/gwlib.h"


struct emimsg {
    int trn;
    char or;
    int ot;
    int num_fields;
    Octstr **fields;
};

/* Symbolic constants for the number of a field in a message */

enum {
    E01_ADC, E01_OADC, E01_AC, E01_MT, E01_AMSG, SZ01
};


/* All the 50-series messages have the same number of fields */
enum {
    E50_ADC, E50_OADC, E50_AC, E50_NRQ, E50_NADC, E50_NT, E50_NPID, E50_LRQ,
    E50_LRAD, E50_LPID, E50_DD, E50_DDT, E50_VP, E50_RPID, E50_SCTS, E50_DST,
    E50_RSN, E50_DSCTS, E50_MT, E50_NB, E50_NMSG=20, E50_AMSG=20, E50_TMSG=20,
    E50_MMS, E50_PR, E50_DCS, E50_MCLS, E50_RPI, E50_CPG, E50_RPLY, E50_OTOA,
    E50_HPLMN, E50_XSER, E50_RES4, E50_RES5, SZ50
};


enum {
    E60_OADC, E60_OTON, E60_ONPI, E60_STYP, E60_PWD, E60_NPWD, E60_VERS,
    E60_LADC, E60_LTON, E60_LNPI, E60_OPID, E60_RES1, SZ60
};


/* Create an EMI msg struct with operation type OT and TRN */
struct emimsg *emimsg_create_op(int ot, int trn, Octstr *whoami);


/* Create an empty EMI msg struct as reply */
struct emimsg *emimsg_create_reply(int ot, int trn, int positive,
		Octstr *whoami);


/* Destroy an EMI msg struct */
void emimsg_destroy(struct emimsg *emimsg);


/* Duplicate an EMI msg struct */ 
struct emimsg *emimsg_duplicate(struct emimsg *emimsg);



/* Create an emimsg struct from the string. */
/* Doesn't check that the string is strictly according to format */
struct emimsg *get_fields(Octstr *message, Octstr *whoami);


/* Send emimsg over conn using the EMI protocol. */
int emimsg_send(Connection *conn, struct emimsg *emimsg,
		Octstr *whoami);

#endif
