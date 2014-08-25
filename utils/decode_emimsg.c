/* ====================================================================
 * The Kannel Software License, Version 1.0
 *
 * Copyright (c) 2001-2014 Kannel Group
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
 * decode_emimsg.c - This tool can decode an UCP/EMI packet. <v.chavanis@telemaque.fr>
 *
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gw/smsc/emimsg.c"
#include "gw/alt_charsets.h"

static char *emi_typeop(int typeop)
{
    switch (typeop) {
    case 31:
        return "Alert Operation";
    case 51:
        return "Submit Short Message operation";
    case 52:
        return "Delivery Short Message operation";
    case 53:
        return "Delivery notification operation";
    case 54:
        return "Modify Short Message operation";
    case 55:
        return "Inquiry message operation";
    case 56:
        return "Delete message operation";
    case 57:
        return "Response Inquiry message operation";
    case 58:
        return "Response delete message operation";
    case 60:
        return "Session management operation";
    case 61:
        return "Provisioning actions operation";
    default:
        return "!UNRECOGNIZED CODE!";
    }
}

int main (int argc, char **argv)
{
    Octstr *message, *whoami;
    struct emimsg *emimsg;

    printf("/* This tool can decode an UCP/EMI packet. <v.chavanis@telemaque.fr> */\n\n");

    gwlib_init();

    if (argc < 2)
        panic(0, "Syntax: %s <packet_without_STX/ETX>\n", argv[0]);

    message = octstr_format("\02%s\03", argv[1]); // fit the UCP specs.
    whoami = octstr_create("DECODE");

    emimsg = get_fields(message, whoami);

    if (emimsg != NULL) {
        printf("\n");
        printf("TRN      \t%d\n", emimsg->trn);
        printf("TYPE     \t%c (%s)\n", emimsg->or, emimsg->or == 'R' ? "Result" : "Operation");
        printf("OPERATION\t%d (%s)\n", emimsg->ot, emi_typeop (emimsg->ot));

        if (emimsg->ot == 01) {
            printf("E01_ADC  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E01_ADC]));
            printf("E01_OADC \t%s\n",
                    octstr_get_cstr(emimsg->fields[E01_OADC]));
            printf("E01_AC   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E01_AC]));
            printf("E01_ADC  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E01_ADC]));
            printf("E01_MT   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E01_MT]));
            if (octstr_get_char(emimsg->fields[E01_MT], 0) == '3') {
                charset_gsm_to_latin1(emimsg->fields[E01_AMSG]);
            }
            printf("E01_AMSG \t%s\n",
                    octstr_get_cstr(emimsg->fields[E01_AMSG]));
        }

        if ((emimsg->ot == 31 || (emimsg->ot >= 50 && emimsg->ot <= 60))
                && emimsg->or == 'R' && 
                (octstr_get_char(emimsg->fields[E50_ADC], 0) == 'A' ||
                octstr_get_char(emimsg->fields[E50_ADC], 0) == 'N')) {
            printf("E%d_ACK  \t%s\n", emimsg->ot,
                    octstr_get_cstr(emimsg->fields[E50_ADC]));
            printf("E%d_SM   \t%s\n", emimsg->ot,
                    octstr_get_cstr(emimsg->fields[E50_OADC]));
        }

        if (emimsg->ot == 31 && emimsg->or == 'O') {
            printf("E50_ADC  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_ADC]));
            printf("E50_PID  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_OADC]));
        }

        if (emimsg->ot >= 50 && emimsg->ot <= 59 && 
                octstr_get_char(emimsg->fields[E50_ADC], 0) != 'A' &&
                octstr_get_char(emimsg->fields[E50_ADC], 0) != 'N') {
            printf("E50_ADC  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_ADC]));
            printf("E50_OADC \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_OADC]));
            printf("E50_AC   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_AC]));
            printf("E50_NRQ  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_NRQ]));
            printf("E50_NADC \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_NADC]));
            printf("E50_NT   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_NT]));
            printf("E50_NPID \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_NPID]));
            printf("E50_LRQ  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_LRQ]));
            printf("E50_LRAD \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_LRAD]));
            printf("E50_LPID \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_LPID]));
            printf("E50_DD   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_DD]));
            printf("E50_DDT  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_DDT]));
            printf("E50_VP   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_VP]));
            printf("E50_RPID \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_RPID]));
            printf("E50_SCTS \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_SCTS]));
            printf("E50_DST  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_DST]));
            printf("E50_RSN  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_RSN]));
            printf("E50_DSCTS\t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_DSCTS]));
            printf("E50_MT   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_MT]));
            printf("E50_NB   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_NB]));
            printf("E50_NMSG \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_NMSG]));
            if (emimsg->fields[E50_AMSG])
                octstr_hex_to_binary (emimsg->fields[E50_AMSG]);
            if (octstr_get_char(emimsg->fields[E50_MT], 0) == '3') {
                charset_gsm_to_latin1(emimsg->fields[E50_AMSG]);
            }

            printf("E50_AMSG \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_AMSG]));
            printf("E50_TMSG \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_TMSG]));
            printf("E50_MMS  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_MMS]));
            printf("E50_PR   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_PR]));
            printf("E50_DCS  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_DCS]));
            printf("E50_MCLS \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_MCLS]));
            printf("E50_RPI  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_RPI]));
            printf("E50_CPG  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_CPG]));
            printf("E50_RPLY \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_RPLY]));
            printf("E50_OTOA \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_OTOA]));
            printf("E50_HPLMN\t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_HPLMN]));
            printf("E50_XSER \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_XSER]));
            printf("E50_RES4 \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_RES4]));
            printf("E50_RES5 \t%s\n",
                    octstr_get_cstr(emimsg->fields[E50_RES5]));
        }

        if ((emimsg->ot == 60 || emimsg->ot == 61) &&
                (octstr_get_char(emimsg->fields[E50_ADC], 0) != 'A' &&
                octstr_get_char(emimsg->fields[E50_ADC], 0) != 'N')) {
            printf("E60_OADC  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_OADC]));
            printf("E60_OTON  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_OTON]));
            printf("E60_ONPI  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_ONPI]));
            printf("E60_STYP  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_STYP]));
            if (emimsg->fields[E60_PWD])
                octstr_hex_to_binary (emimsg->fields[E60_PWD]);
            printf("E60_PWD   \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_PWD]));
            printf("E60_NPWD  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_NPWD]));
            printf("E60_VERS  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_VERS]));
            printf("E60_LADC  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_LADC]));
            printf("E60_LTON  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_LTON]));
            printf("E60_LNPI  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_LNPI]));
            printf("E60_OPID  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_OPID]));
            printf("E60_RES1  \t%s\n",
                    octstr_get_cstr(emimsg->fields[E60_RES1]));
        }
    }

    octstr_destroy(message);
    octstr_destroy(whoami);
    gwlib_shutdown();
    
    return 0;
}
