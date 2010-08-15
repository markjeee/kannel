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
 * ota_prov.c: OTA settings and bookmarks provisioning routines
 *
 * This module contains routines for the SMS OTA (auto configuration) message 
 * creation and manipulation for the sendota HTTP interface.
 *
 * Official Nokia and Ericsson WAP OTA configuration settings coded 
 * by Stipe Tolj <stolj@kannel.org>, Wapme Systems AG.
 * 
 * Officual OMA ProvCont OTA provisioning coded 
 * by Paul Bagyenda, digital solutions Ltd.
 * 
 * XML compiler by Aarno Syvänen <aarno@wiral.com>, Wiral Ltd.
 */

#include <string.h>

#include "gwlib/gwlib.h"

#ifdef HAVE_LIBSSL
#include <openssl/hmac.h>
#endif

#include "msg.h"
#include "sms.h"
#include "ota_prov.h"
#include "ota_prov_attr.h"
#include "ota_compiler.h"

#include "wap/wsp_headers.h"


/***************************************************************************
 *
 * Implementation of the internal function
 */

/*
 * Append the User Data Header (UDH) including the lenght (UDHL). Only ports 
 * UDH here - SAR UDH is added when (or if) we split the message. This is our
 * *specific* WDP layer.
 */
static void ota_pack_udh(Msg **msg, Octstr *doc_type)
{
    (*msg)->sms.udhdata = octstr_create("");
    if (octstr_case_compare(doc_type, octstr_imm("oma-settings")) == 0) 
        octstr_append_from_hex((*msg)->sms.udhdata, "0605040B840B84");    
    else if (octstr_case_compare(doc_type, octstr_imm("syncsettings")) == 0) {
        octstr_append_from_hex((*msg)->sms.udhdata, "060504C34CC002");
    } else 
        octstr_append_from_hex((*msg)->sms.udhdata, "060504C34FC002");    
 }


/*
 * Our WSP headers: Push Id, PDU type, headers, charset.
 */
static int ota_pack_push_headers(Msg **msg, Octstr *mime_type, Octstr *sec, 
                                 Octstr *pin, Octstr *ota_binary)
{    
    (*msg)->sms.msgdata = octstr_create("");
    if (octstr_case_compare(mime_type, octstr_imm("settings")) == 0) {
        
        /* PUSH ID, PDU type, header length, value length */
        octstr_append_from_hex((*msg)->sms.msgdata, "01062C1F2A");
        /* MIME type for settings */
        octstr_format_append((*msg)->sms.msgdata, "%s", 
                             "application/x-wap-prov.browser-settings");
        octstr_append_from_hex((*msg)->sms.msgdata, "00");
        /* charset UTF-8 */
        octstr_append_from_hex((*msg)->sms.msgdata, "81EA");

    } else if (octstr_case_compare(mime_type, octstr_imm("bookmarks")) == 0) {
        
        /* PUSH ID, PDU type, header length, value length */
        octstr_append_from_hex((*msg)->sms.msgdata, "01062D1F2B");
        /* MIME type for bookmarks */
        octstr_format_append((*msg)->sms.msgdata, "%s", 
                             "application/x-wap-prov.browser-bookmarks");
        octstr_append_from_hex((*msg)->sms.msgdata, "00");
        /* charset UTF-8 */
        octstr_append_from_hex((*msg)->sms.msgdata, "81EA");

    } else if (octstr_case_compare(mime_type, octstr_imm("syncsettings")) == 0) {

        octstr_append_from_hex((*msg)->sms.msgdata, "3406060502020b81EA"); 

    } else if (octstr_case_compare(mime_type, octstr_imm("oma-settings")) == 0) {
        Octstr *hdr = octstr_create(""), *mac; 
        unsigned char *p;
        unsigned int mac_len;
#ifdef HAVE_LIBSSL
        unsigned char macbuf[EVP_MAX_MD_SIZE];
#endif

        /* PUSH ID, PDU type, header length, value length */
        octstr_append_from_hex((*msg)->sms.msgdata, "0106");
    
        octstr_append_from_hex(hdr, "1f2db6"); /* Content type + other type + sec param */
        wsp_pack_short_integer(hdr, 0x11);
        if (octstr_case_compare(sec, octstr_imm("netwpin")) == 0)
            wsp_pack_short_integer(hdr, 0x0);       
        else if (octstr_case_compare(sec, octstr_imm("userpin")) == 0)
            wsp_pack_short_integer(hdr, 0x01);          
        else if (octstr_case_compare(sec, octstr_imm("usernetwpin")) == 0)
            wsp_pack_short_integer(hdr, 0x02);          
        else if (octstr_case_compare(sec, octstr_imm("userpinmac")) == 0)
            wsp_pack_short_integer(hdr, 0x03); /* XXXX Although not quite supported now.*/          
        else {
            warning(0, "OMA ProvCont: Unknown SEC pin type '%s'.", octstr_get_cstr(sec));
            wsp_pack_short_integer(hdr, 0x01);          
        }
        wsp_pack_short_integer(hdr, 0x12); /* MAC */

#ifdef HAVE_LIBSSL
        p = HMAC(EVP_sha1(), octstr_get_cstr(pin), octstr_len(pin), 
                 (unsigned char *)octstr_get_cstr(ota_binary), octstr_len(ota_binary), 
                 macbuf, &mac_len);
#else
        mac_len = 0;
        p = "";
        warning(0, "OMA ProvCont: No SSL Support, '%s' not supported!", octstr_get_cstr(mime_type));
#endif
        mac = octstr_create_from_data((char *)p, mac_len);
        octstr_binary_to_hex(mac, 1);
    
        octstr_append(hdr, mac);
        octstr_append_from_hex(hdr, "00");
    
        octstr_append_uintvar((*msg)->sms.msgdata, octstr_len(hdr));
        octstr_append((*msg)->sms.msgdata, hdr);
    
        octstr_destroy(hdr);
        octstr_destroy(mac);
        
    } else {
        warning(0, "Unknown MIME type in OTA request, type '%s' is unsupported.", 
                octstr_get_cstr(mime_type));
        return 0;
    }

    return 1;
}


/***************************************************************************
 *
 * Implementation of the external function
 */

int ota_pack_message(Msg **msg, Octstr *ota_doc, Octstr *doc_type, 
                     Octstr *from, Octstr *phone_number, Octstr *sec, Octstr *pin)
{
    Octstr *ota_binary;

    *msg = msg_create(sms);
    (*msg)->sms.sms_type = mt_push;

    ota_pack_udh(msg, doc_type);

    if (ota_compile(ota_doc, octstr_imm("UTF-8"), &ota_binary) == -1)
        goto cerror;
        
    if (!ota_pack_push_headers(msg, doc_type, sec, pin, ota_binary))
        goto herror;

    octstr_format_append((*msg)->sms.msgdata, "%S", ota_binary);
    (*msg)->sms.sender = octstr_duplicate(from);
    (*msg)->sms.receiver = octstr_duplicate(phone_number);
    (*msg)->sms.coding = DC_8BIT;
    (*msg)->sms.time = time(NULL);

    octstr_dump((*msg)->sms.msgdata, 0);
    info(0, "/cgi-bin/sendota: XML request for target <%s>", octstr_get_cstr(phone_number));

    octstr_destroy(ota_binary);
    octstr_destroy(ota_doc);
    octstr_destroy(doc_type);
    octstr_destroy(from);
    octstr_destroy(sec);
    octstr_destroy(pin);
    return 0;

herror:
    octstr_destroy(ota_binary);
    octstr_destroy(ota_doc);
    octstr_destroy(doc_type);
    octstr_destroy(from);
    octstr_destroy(sec);
    octstr_destroy(pin);
    return -2;

cerror:
    octstr_destroy(ota_doc);
    octstr_destroy(doc_type);
    octstr_destroy(from);
    octstr_destroy(sec);
    octstr_destroy(pin);
    return -1;
}


Msg *ota_tokenize_settings(CfgGroup *grp, Octstr *from, Octstr *receiver)
{
    Octstr *url, *desc, *ipaddr, *phonenum, *username, *passwd;
    int speed, bearer, calltype, connection, security, authent;
    Msg *msg;
    Octstr *p;
    
    url = NULL;
    desc = NULL;
    ipaddr = NULL;
    phonenum = NULL;
    username = NULL;
    passwd = NULL;
    bearer = -1;
    calltype =  WBXML_TOK_VALUE_CONN_ISDN;
    connection = WBXML_TOK_VALUE_PORT_9201;
    security = 0;
    authent = WBXML_TOK_VALUE_AUTH_PAP;

    url = cfg_get(grp, octstr_imm("location"));
    desc = cfg_get(grp, octstr_imm("service"));
    ipaddr = cfg_get(grp, octstr_imm("ipaddress"));
    phonenum = cfg_get(grp, octstr_imm("phonenumber"));
    p = cfg_get(grp, octstr_imm("bearer"));
    if (p != NULL) {
        if (strcasecmp(octstr_get_cstr(p), "data") == 0)
            bearer = WBXML_TOK_VALUE_GSM_CSD;
        else
            bearer = -1;
        octstr_destroy(p);
    }
    p = cfg_get(grp, octstr_imm("calltype"));
    if (p != NULL) {
        if (strcasecmp(octstr_get_cstr(p), "analog") == 0)
            calltype = WBXML_TOK_VALUE_CONN_ANALOGUE;
        else
            calltype =  WBXML_TOK_VALUE_CONN_ISDN;
        octstr_destroy(p);
    }
	
    speed = WBXML_TOK_VALUE_SPEED_9600;
    p = cfg_get(grp, octstr_imm("speed"));
    if (p != NULL) {
        if (octstr_compare(p, octstr_imm("14400")) == 0)
            speed = WBXML_TOK_VALUE_SPEED_14400;
        octstr_destroy(p);
    }

    /* connection mode: UDP (port 9200) or TCP (port 9201)*/
    p = cfg_get(grp, octstr_imm("connection"));
    if (p != NULL) {
        if (strcasecmp(octstr_get_cstr(p), "temp") == 0)
            connection = WBXML_TOK_VALUE_PORT_9200;
        else
            connection = WBXML_TOK_VALUE_PORT_9201;
        octstr_destroy(p);
    }

    /* dial in security: CHAP or PAP */
    p = cfg_get(grp, octstr_imm("pppsecurity"));
    if (p != NULL) {
        if (strcasecmp(octstr_get_cstr(p), "on") == 0)
            authent = WBXML_TOK_VALUE_AUTH_CHAP;
        else
            authent = WBXML_TOK_VALUE_AUTH_PAP;
        octstr_destroy(p);
    }
    
    /* WTLS: for UDP (port 9202) or TCP (port 9203) */
    p = cfg_get(grp, octstr_imm("authentication"));
    if (p != NULL) {
        if (strcasecmp(octstr_get_cstr(p), "secure") == 0)
            security = 1;
        else
            security = WBXML_TOK_VALUE_PORT_9201;
        octstr_destroy(p);
    }
    if (security == 1)
        connection = (connection == WBXML_TOK_VALUE_PORT_9201)? 
            WBXML_TOK_VALUE_PORT_9203 : WBXML_TOK_VALUE_PORT_9202;
    
    username = cfg_get(grp, octstr_imm("login"));
    passwd = cfg_get(grp, octstr_imm("secret"));
    
    msg = msg_create(sms);

    /*
     * Append the User Data Header (UDH) including the lenght (UDHL)
     * WDP layer (start WDP headers)
     */
    
    msg->sms.sms_type = mt_push;
    msg->sms.udhdata = octstr_create("");

    /* 
     * Within OTA spec this is "0B0504C34FC0020003040201", but it works
     * with the following too?!
     */
    octstr_append_from_hex(msg->sms.udhdata, "060504C34FC002");
    /* WDP layer (end WDP headers) */         

    /*
     * WSP layer (start WSP headers)
     */
    
    msg->sms.msgdata = octstr_create("");
    /* PUSH ID, PDU type, header length, value length */
    octstr_append_from_hex(msg->sms.msgdata, "01062C1F2A");
    /* MIME-type: application/x-wap-prov.browser-settings */
    octstr_format_append(msg->sms.msgdata, "%s", 
                         "application/x-wap-prov.browser-settings");
    octstr_append_from_hex(msg->sms.msgdata, "00");
    /* charset UTF-8 */
    octstr_append_from_hex(msg->sms.msgdata, "81EA");
    /* WSP layer (end WSP headers) */

    /*
     * WSP layer (start WSP data field)
     */

    /* WBXML version 1.1 */
    octstr_append_from_hex(msg->sms.msgdata, "0101");
    /* charset UTF-8 */
    octstr_append_from_hex(msg->sms.msgdata, "6A00");

    /* CHARACTERISTIC_LIST */
    octstr_append_from_hex(msg->sms.msgdata, "45");
    /* CHARACTERISTIC with content and attributes */
    octstr_append_from_hex(msg->sms.msgdata, "C6");
    /* TYPE=ADDRESS */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_TYPE_ADDRESS);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* bearer type */
    if (bearer != -1) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=BEARER, VALUE=GSM_CSD */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_BEARER);
        octstr_append_char(msg->sms.msgdata, bearer);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* IP address */
    if (ipaddr != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PROXY, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PROXY);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, octstr_duplicate(ipaddr));
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* connection type */
    if (connection != -1) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PORT, VALUE */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PORT);
        octstr_append_char(msg->sms.msgdata, connection);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* phone number */
    if (phonenum != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=CSD_DIALSTRING, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_CSD_DIALSTRING);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, octstr_duplicate(phonenum));
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* authentication */
    /* PARM with attributes */
    octstr_append_from_hex(msg->sms.msgdata, "87");
     /* NAME=PPP_AUTHTYPE, VALUE */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PPP_AUTHTYPE);
    octstr_append_char(msg->sms.msgdata, authent);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    /* user name */
    if (username != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PPP_AUTHNAME, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PPP_AUTHNAME);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, octstr_duplicate(username));
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* password */
    if (passwd != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PPP_AUTHSECRET, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PPP_AUTHSECRET);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, octstr_duplicate(passwd));
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* data call type */
    if (calltype != -1) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=CSD_CALLTYPE, VALUE */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_CSD_CALLTYPE);
        octstr_append_char(msg->sms.msgdata, calltype);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* speed */
    /* PARM with attributes */
    octstr_append_from_hex(msg->sms.msgdata, "87");
    /* NAME=CSD_CALLSPEED, VALUE */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_CSD_CALLSPEED);
    octstr_append_char(msg->sms.msgdata, speed);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* end CHARACTERISTIC TYPE=ADDRESS */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* homepage */
    if (url != NULL) {
        /* CHARACTERISTIC with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "86");
        /* TYPE=URL */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_TYPE_URL);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, url);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }

    /* CHARACTERISTIC with content and attributes */
    octstr_append_from_hex(msg->sms.msgdata, "C6");
    /* TYPE=NAME */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_TYPE_NAME);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* service description */
    if (desc != NULL) {
        /* PARAM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=NAME, VALUE, inline */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_NAME);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, desc);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }

    /* end of CHARACTERISTIC */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    /* end of CHARACTERISTIC-LIST */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    /* WSP layer (end WSP data field) */

    msg->sms.sender = from;
    msg->sms.receiver = octstr_duplicate(receiver);
    msg->sms.coding = DC_8BIT;
    
    msg->sms.time = time(NULL);
                              
    octstr_destroy(url);
    octstr_destroy(desc);
    octstr_destroy(ipaddr);
    octstr_destroy(phonenum);
    octstr_destroy(username);
    octstr_destroy(passwd);

    return msg;
}


Msg *ota_tokenize_bookmarks(CfgGroup *grp, Octstr *from, Octstr *receiver)
{
    Octstr *url, *name;
    Msg *msg;
    
    url = NULL;
    name = NULL;

    url = cfg_get(grp, octstr_imm("url"));
    name = cfg_get(grp, octstr_imm("name"));
    
    msg = msg_create(sms);

    /*
     * Append the User Data Header (UDH) including the lenght (UDHL)
     * WDP layer (start WDP headers)
     */
    
    msg->sms.sms_type = mt_push;
    msg->sms.udhdata = octstr_create("");

    octstr_append_from_hex(msg->sms.udhdata, "060504C34FC002");
    /* WDP layer (end WDP headers) */

    /*
     * WSP layer (start WSP headers)
     */
    
    msg->sms.msgdata = octstr_create("");
    /* PUSH ID, PDU type, header length, value length */
    octstr_append_from_hex(msg->sms.msgdata, "01062D1F2B");
    /* MIME-type: application/x-wap-prov.browser-bookmarks */
    octstr_format_append(msg->sms.msgdata, "%s", 
                         "application/x-wap-prov.browser-bookmarks");
    octstr_append_from_hex(msg->sms.msgdata, "00");
    /* charset UTF-8 */
    octstr_append_from_hex(msg->sms.msgdata, "81EA");
    /* WSP layer (end WSP headers) */

    /*
     * WSP layer (start WSP data field)
     */

    /* WBXML version 1.1 */
    octstr_append_from_hex(msg->sms.msgdata, "0101");
    /* charset UTF-8 */
    octstr_append_from_hex(msg->sms.msgdata, "6A00");

    /* CHARACTERISTIC_LIST */
    octstr_append_from_hex(msg->sms.msgdata, "45");
    /* CHARACTERISTIC with content and attributes */
    octstr_append_from_hex(msg->sms.msgdata, "C6");
    /* TYPE=BOOKMARK */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_TYPE_BOOKMARK);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* name */
    if (name != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PROXY, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_NAME);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, octstr_duplicate(name));
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* URL */
    if (url != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PROXY, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_URL);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, octstr_duplicate(url));
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }

    /* end of CHARACTERISTIC */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    /* end of CHARACTERISTIC-LIST */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    /* WSP layer (end WSP data field) */

    msg->sms.sender = from;
    msg->sms.receiver = octstr_duplicate(receiver);
    msg->sms.coding = DC_8BIT;
    
    msg->sms.time = time(NULL);
                              
    octstr_destroy(name);
    octstr_destroy(url);

    return msg;
}

