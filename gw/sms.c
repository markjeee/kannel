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
 * sms.c - features specific to SMS but not particular to any SMSC protocol.
 *
 * This file current contains very little, but sms features that are
 * currently implemented separately in each protocol should be extracted
 * and placed here.
 */

#include "sms.h"
#include "dlr.h"

/* 
 * Encode DCS using sms fields
 * mode = 0= encode using 00xxx, 1= encode using Fx mode
 *
 */
int fields_to_dcs(Msg *msg, int mode) 
{
    int dcs=0;

    /* Coding defaults to 7BIT or to 8BIT if udh is set */
    if (msg->sms.coding == DC_UNDEF) {
	if (octstr_len(msg->sms.udhdata))
	  msg->sms.coding = DC_8BIT;
	else
	  msg->sms.coding = DC_7BIT;
    }


    /* MWI */
    if (msg->sms.mwi != MWI_UNDEF) {
	dcs = msg->sms.mwi;  /* sets bits 2, 1 and 0 */

	if (dcs & 0x04)	
	    dcs = (dcs & 0x03) | 0xC0; /* MWI Inactive */
	else {
	    dcs = (dcs & 0x03) | 0x08; /* MWI Active, sets bit 3 */

	    if (! octstr_len(msg->sms.msgdata))
		dcs |= 0xC0;	/* Discard */
	    else
		if (msg->sms.coding == DC_7BIT)
		    dcs |= 0xD0;	/* 7bit */
		else
		    dcs |= 0xE0;	/* UCS-2 */
	    	/* XXX Shouldn't happen to have mwi and dc=DC_8BIT! */
	}
    }

    /* Non-MWI */
    else {
	/* mode 0 or mode UNDEF */
	if (mode == 0 || mode == SMS_PARAM_UNDEFINED || msg->sms.coding == DC_UCS2 
	    || msg->sms.compress == COMPRESS_ON) { 
	    /* bits 7,6 are 0 */
	    if (msg->sms.compress == COMPRESS_ON)
		dcs |= 0x20; /* sets bit 5 */
	    if (msg->sms.mclass != MC_UNDEF)
		dcs |= (0x10 | msg->sms.mclass); /* sets bit 4,1,0 */
	    if (msg->sms.coding != DC_UNDEF)
		dcs |= (msg->sms.coding << 2); /* sets bit 3,2 */
	} 
	
	/* mode 1 */
	else {
	    dcs |= 0xF0; /* sets bits 7-3 */
	    if(msg->sms.coding != DC_UNDEF)
		dcs |= (msg->sms.coding << 2); /* only DC_7BIT or DC_8BIT, sets bit 2*/
	    if (msg->sms.mclass == MC_UNDEF)
                dcs |= 1; /* default meaning: ME specific */
            else
                dcs |= msg->sms.mclass; /* sets bit 1,0 */
	}
    }

    return dcs;
}


/*
 * Decode DCS to sms fields
 */
int dcs_to_fields(Msg **msg, int dcs) 
{
    /* Non-MWI Mode 1 */
    if ((dcs & 0xF0) == 0xF0) { 
        dcs &= 0x07;
        (*msg)->sms.coding = (dcs & 0x04) ? DC_8BIT : DC_7BIT; /* grab bit 2 */
        (*msg)->sms.mclass = dcs & 0x03; /* grab bits 1,0 */
        (*msg)->sms.alt_dcs = 1; /* set 0xFX data coding */
    }
    
    /* Non-MWI Mode 0 */
    else if ((dcs & 0xC0) == 0x00) { 
        (*msg)->sms.alt_dcs = 0;
        (*msg)->sms.compress = ((dcs & 0x20) == 0x20) ? 1 : 0; /* grab bit 5 */
        (*msg)->sms.mclass = ((dcs & 0x10) == 0x10) ? dcs & 0x03 : MC_UNDEF; 
						/* grab bit 0,1 if bit 4 is on */
        (*msg)->sms.coding = (dcs & 0x0C) >> 2; /* grab bit 3,2 */
    }

    /* MWI */
    else if ((dcs & 0xC0) == 0xC0) { 
        (*msg)->sms.alt_dcs = 0;
        (*msg)->sms.coding = ((dcs & 0x30) == 0x30) ? DC_UCS2 : DC_7BIT;
        if (!(dcs & 0x08))
            dcs |= 0x04; /* if bit 3 is active, have mwi += 4 */
        dcs &= 0x07;
        (*msg)->sms.mwi = dcs ; /* grab bits 1,0 */
    } 
    
    else {
        return 0;
    }

    return 1;
}


/*
 * Compute length of an Octstr after it will be converted to GSM 03.38 
 * 7 bit alphabet - escaped characters would be counted as two septets
 */
int sms_msgdata_len(Msg* msg) 
{
	int ret = 0;
	Octstr* msgdata = NULL;
	
	/* got a bad input */
	if (!msg || !msg->sms.msgdata) 
		return -1;

	if (msg->sms.coding == DC_7BIT) {
		msgdata = octstr_duplicate(msg->sms.msgdata);
		charset_utf8_to_gsm(msgdata);
		ret = octstr_len(msgdata);
		octstr_destroy(msgdata);
	} else 
		ret = octstr_len(msg->sms.msgdata);

	return ret;
}


int sms_swap(Msg *msg) 
{
    Octstr *sender = NULL;

    if (msg->sms.sender != NULL && msg->sms.receiver != NULL) {
        sender = msg->sms.sender;
        msg->sms.sender = msg->sms.receiver;
        msg->sms.receiver = sender;

        return 1;
    }

    return 0;
}


/*****************************************************************************
 *
 * Split an SMS message into smaller ones.
 */
#define CATENATE_UDH_LEN 5


static void prepend_catenation_udh(Msg *sms, int part_no, int num_messages,
    	    	    	    	   int msg_sequence)
{
    if (sms->sms.udhdata == NULL)
        sms->sms.udhdata = octstr_create("");
    if (octstr_len(sms->sms.udhdata) == 0)
	octstr_append_char(sms->sms.udhdata, CATENATE_UDH_LEN);
    octstr_format_append(sms->sms.udhdata, "%c\3%c%c%c", 
    	    	    	 0, msg_sequence, num_messages, part_no);

     /* Set the number of messages left, if any */
     if (part_no < num_messages)
     	sms->sms.msg_left = num_messages - part_no;
     else
     	sms->sms.msg_left = 0;
    /* 
     * Now that we added the concatenation information the
     * length is all wrong. we need to recalculate it. 
     */
    octstr_set_char(sms->sms.udhdata, 0, octstr_len(sms->sms.udhdata) - 1 );
}


static Octstr *extract_msgdata_part(Octstr *msgdata, Octstr *split_chars,
    	    	    	    	    int max_part_len)
{
    long i, len;
    Octstr *part;

    len = max_part_len;
    if (max_part_len < octstr_len(msgdata) && split_chars != NULL)
	for (i = max_part_len; i > 0; i--)
	    if (octstr_search_char(split_chars,
				   octstr_get_char(msgdata, i - 1), 0) != -1) {
		len = i;
		break;
	    }
    part = octstr_copy(msgdata, 0, len);
    octstr_delete(msgdata, 0, len);
    return part;
}


static Octstr *extract_msgdata_part_by_coding(Msg *msg, Octstr *split_chars,
        int max_part_len)
{
    Octstr *temp = NULL, *temp_utf;

    if (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2) {
        /* nothing to do here, just call the original extract_msgdata_part */
        return extract_msgdata_part(msg->sms.msgdata, split_chars, max_part_len);
    }

    /* convert to and the from gsm, so we drop all non GSM chars */
    charset_utf8_to_gsm(msg->sms.msgdata);
    charset_gsm_to_utf8(msg->sms.msgdata);

    /* 
     * else we need to do something special. I'll just get charset_gsm_truncate to
     * cut the string to the required length and then count real characters. 
     */
     temp = octstr_duplicate(msg->sms.msgdata);
     charset_utf8_to_gsm(temp);
     charset_gsm_truncate(temp, max_part_len);

     /* calculate utf-8 length */
     temp_utf = octstr_duplicate(temp);
     charset_gsm_to_utf8(temp_utf);
     max_part_len = octstr_len(temp_utf);

     octstr_destroy(temp);
     octstr_destroy(temp_utf);

     /* now just call the original extract_msgdata_part with the new length */
     return extract_msgdata_part(msg->sms.msgdata, split_chars, max_part_len);
}


List *sms_split(Msg *orig, Octstr *header, Octstr *footer, 
                Octstr *nonlast_suffix, Octstr *split_chars, 
                int catenate, unsigned long msg_sequence,
                int max_messages, int max_octets)
{
    long max_part_len, udh_len, hf_len, nlsuf_len;
    unsigned long total_messages, msgno;
    long last;
    List *list;
    Msg *part, *temp;

    hf_len = octstr_len(header) + octstr_len(footer);
    nlsuf_len = octstr_len(nonlast_suffix);
    udh_len = octstr_len(orig->sms.udhdata);

    /* First check whether the message is under one-part maximum */
    if (orig->sms.coding == DC_8BIT || orig->sms.coding == DC_UCS2)
        max_part_len = max_octets - udh_len - hf_len;
    else
        max_part_len = (max_octets - udh_len) * 8 / 7 - hf_len;

    if (sms_msgdata_len(orig) > max_part_len && catenate) {
        /* Change part length to take concatenation overhead into account */
        if (udh_len == 0)
            udh_len = 1;  /* Add the udh total length octet */
        udh_len += CATENATE_UDH_LEN;
        if (orig->sms.coding == DC_8BIT || orig->sms.coding == DC_UCS2)
            max_part_len = max_octets - udh_len - hf_len;
        else
            max_part_len = (max_octets - udh_len) * 8 / 7 - hf_len;
    }

    /* ensure max_part_len is never negativ */
    max_part_len = max_part_len > 0 ? max_part_len : 0;

    temp = msg_duplicate(orig);
    msgno = 0;
    list = gwlist_create();

    last = 0;
    do {
        msgno++;
        part = msg_duplicate(orig);

        /* 
         * if its a DLR request message getting split, 
         * only ask DLR for the first one 
         */
        if ((msgno > 1) && DLR_IS_ENABLED(part->sms.dlr_mask)) {
            octstr_destroy(part->sms.dlr_url);
            part->sms.dlr_url = NULL;
            part->sms.dlr_mask = 0;
        }
        octstr_destroy(part->sms.msgdata);
        if (sms_msgdata_len(temp) <= max_part_len || msgno == max_messages)
            last = 1;

        part->sms.msgdata = 
            extract_msgdata_part_by_coding(temp, split_chars,
                                           max_part_len - nlsuf_len);
        /* create new id for every part, except last */
        if (!last)
            uuid_generate(part->sms.id);

        if (header)
            octstr_insert(part->sms.msgdata, header, 0);
        if (footer)
            octstr_append(part->sms.msgdata, footer);
        if (!last && nonlast_suffix)
            octstr_append(part->sms.msgdata, nonlast_suffix);
        gwlist_append(list, part);
    } while (!last);

    total_messages = msgno;
    msg_destroy(temp);
    if (catenate && total_messages > 1) {
        for (msgno = 1; msgno <= total_messages; msgno++) {
            part = gwlist_get(list, msgno - 1);
            prepend_catenation_udh(part, msgno, total_messages, msg_sequence);
        }
    }

    return list;
}


int sms_priority_compare(const void *a, const void *b)
{
    int ret;
    Msg *msg1 = (Msg*)a, *msg2 = (Msg*)b;
    gw_assert(msg_type(msg1) == sms);
    gw_assert(msg_type(msg2) == sms);
    
    if (msg1->sms.priority > msg2->sms.priority)
        ret = 1;
    else if (msg1->sms.priority < msg2->sms.priority)
        ret = -1;
    else {
        if (msg1->sms.time > msg2->sms.time)
            ret = 1;
        else if (msg1->sms.time < msg2->sms.time)
            ret = -1;
        else
            ret = 0;
    }
    
    return ret;
}

