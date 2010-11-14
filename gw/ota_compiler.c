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
 * ota_compiler.c: Tokenizes OTA provisioning documents 
 * 
 * This compiler handles the following OTA config formats:
 * 
 *   - Nokia/Ericsson OTA settings specificaion. DTD is defined in 
 *     Over The Air Settings Specification (hereafter called OTA), chapter 6. 
 *     (See http://www.americas.nokia.com/messaging/default.asp)
 * 
 *   - OMA OTA client provisionig content specification, as defined in
 *     document OMA-WAP-ProvCont-V1.1-20050428-C.pdf. (hereafter called OMA)
 *     (See http://www.openmobilealliance.com/release_program/cp_v1_1.htm)
 * 
 * Histrorically the Nokia/Ericsson OTA config format was the first scratch 
 * in allowing remote WAP profile configuration via SMS bearer. While the WAP
 * Forum transfered into the Open Mobile Alliance (OMA), the technical working
 * groups addopted the provisioning concept to a more generic OTA provisioning
 * concept. The OMA client provisioning specs v1.1 are part of the WAP 2.0 
 * protocol stack. 
 * 
 * Aarno Syvänen for Wiral Ltd
 * Stipe Tolj <stolj@kannel.org> for Wapme Systems AG
 * Paul Bagyenda for digital solutions Ltd.
 */

#include <ctype.h>
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/debugXML.h>
#include <libxml/encoding.h>

#include "shared.h"
#include "xml_shared.h"
#include "ota_compiler.h"

/****************************************************************************
 *
 * Global variables
 *
 * Two token table types, one and two token fields
 */

struct ota_2table_t {
    char *name;
    unsigned char token;
};

typedef struct ota_2table_t ota_2table_t;

/*
 * Ota tokenizes whole of attribute value, or uses an inline string. See ota, 
 * chapter 8.2.
 */
struct ota_3table_t {
    char *name;
    char *value;
    unsigned char token;
    unsigned char code_page;
};

typedef struct ota_3table_t ota_3table_t;

/*
 * Elements from tag code page zero. These are defined in OTA, chapter 8.1
 * and OMA, chapter 7.1.
 */

static ota_2table_t ota_elements[] = {
    { "SYNCSETTINGS", 0x15 },
    { "WAP-PROVISIONINGDOC", 0x05 },
    { "CHARACTERISTIC-LIST", 0x05 },
    { "CHARACTERISTIC", 0x06 },
    { "PARM", 0x07 }
};

#define NUMBER_OF_ELEMENTS sizeof(ota_elements)/sizeof(ota_elements[0])

/*
 * SYNCSETTINGS tags are defined in OTA specs 7.0, chapter 11.1
 */

static ota_2table_t ota_syncsettings_elements[] = {
    { "Addr", 0x45 },
    { "AddrType", 0x46 },
    { "Auth", 0x47 },
    { "AuthLevel", 0x48 },
    { "AuthScheme", 0x49 },
    { "Bearer", 0x4A },
    { "ConRef", 0x4B },
    { "ConType", 0x4C },
    { "Cred", 0x4D },
    { "CTType", 0x4E },
    { "CTVer", 0x4F },
    { "HostAddr", 0x50 },
    { "Name", 0x51 },
    { "Port", 0x52 },
    { "RefID", 0x53 },
    { "RemoteDB", 0x54 },
    { "URI", 0x56 },
    { "Username", 0x57 },
    { "Version", 0x58 }
};

#define NUMBER_OF_SYNCSETTINGS_ELEMENTS sizeof(ota_syncsettings_elements)/sizeof(ota_syncsettings_elements[0])

/*
 * Attribute names and values from code page zero. These are defined in ota,
 * chapter 8.2. Some values are presented as inline strings; in this case 
 * value "INLINE" is used. (Note a quirk: there is an attribute with name 
 * "VALUE".)
 *
 * For a documentation of the single attributes see gw/ota_prov_attr.h.
 */

static ota_3table_t ota_attributes[] = {
    { "TYPE", "ADDRESS", 0x06 },
    { "TYPE", "URL", 0x07 },
    { "TYPE", "MMSURL", 0x7c },
    { "TYPE", "NAME", 0x08 },
    { "TYPE", "ID", 0x7d },
    { "TYPE", "BOOKMARK", 0x7f },
    { "NAME", "BEARER", 0x12 },
    { "NAME", "PROXY", 0x13 },
    { "NAME", "PORT", 0x14 },
    { "NAME", "NAME", 0x15 },
    { "NAME", "PROXY_TYPE", 0x16 },
    { "NAME", "URL", 0x17 },
    { "NAME", "PROXY_AUTHNAME", 0x18 },
    { "NAME", "PROXY_AUTHSECRET", 0x19 },
    { "NAME", "SMS_SMSC_ADDRESS", 0x1a },
    { "NAME", "USSD_SERVICE_CODE", 0x1b },
    { "NAME", "GPRS_ACCESSPOINTNAME", 0x1c },
    { "NAME", "PPP_LOGINTYPE", 0x1d },
    { "NAME", "PROXY_LOGINTYPE", 0x1e },
    { "NAME", "CSD_DIALSTRING", 0x21 },
    { "NAME", "CSD_CALLTYPE", 0x28 },
    { "NAME", "CSD_CALLSPEED", 0x29 },
    { "NAME", "PPP_AUTHTYPE", 0x22 },
    { "NAME", "PPP_AUTHNAME", 0x23 },
    { "NAME", "PPP_AUTHSECRET", 0x24 },
    { "NAME", "ISP_NAME", 0x7e },
    { "NAME", "INLINE", 0x10 },
    { "VALUE", "GSM/CSD", 0x45 },
    { "VALUE", "GSM/SMS", 0x46 },
    { "VALUE", "GSM/USSD", 0x47 },
    { "VALUE", "IS-136/CSD", 0x48 },
    { "VALUE", "GPRS", 0x49 },
    { "VALUE", "9200", 0x60 },
    { "VALUE", "9201", 0x61 },
    { "VALUE", "9202", 0x62 },
    { "VALUE", "9203", 0x63 },
    { "VALUE", "AUTOMATIC", 0x64 },
    { "VALUE", "MANUAL", 0x65 },
    { "VALUE", "AUTO", 0x6a },
    { "VALUE", "9600", 0x6b },
    { "VALUE", "14400", 0x6c },
    { "VALUE", "19200", 0x6d },
    { "VALUE", "28800", 0x6e },
    { "VALUE", "38400", 0x6f },
    { "VALUE", "PAP", 0x70 },
    { "VALUE", "CHAP", 0x71 },
    { "VALUE", "ANALOGUE", 0x72 },
    { "VALUE", "ISDN", 0x73 },
    { "VALUE", "43200", 0x74 },
    { "VALUE", "57600", 0x75 },
    { "VALUE", "MSISDN_NO", 0x76 },
    { "VALUE", "IPV4", 0x77 },
    { "VALUE", "MS_CHAP", 0x78 },
    { "VALUE", "INLINE", 0x11 }
};

#define NUMBER_OF_ATTRIBUTES sizeof(ota_attributes)/sizeof(ota_attributes[0])

/*
 * Defines OMA ProvCont WBXML tokens, see chapter 7.
 * Value 'INLINE' has to be always last in attribute group, since this
 * is a break condition within a while loop.
 */

static ota_3table_t oma_ota_attributes[] = {
    { "VERSION", "1.0", 0x46 },
    { "VERSION", "INLINE", 0x45 },
    { "TYPE", "PXLOGICAL", 0x51 },
    { "TYPE", "PXPHYSICAL", 0x52 },
    { "TYPE", "PORT", 0x53 },
    { "TYPE", "VALIDITY", 0x54 },
    { "TYPE", "NAPDEF", 0x55 },
    { "TYPE", "BOOTSTRAP", 0x56 },
    { "TYPE", "VENDORCONFIG", 0x57 },
    { "TYPE", "PXAUTHINFO", 0x59 },
    { "TYPE", "NAPAUTHINFO", 0x5A },
    { "TYPE", "ACCESS", 0x5B },
    { "TYPE", "BEARERINFO", 0x5C },
    { "TYPE", "DNS-ADDRINFO", 0x5D },
    { "TYPE", "CLIENTIDENTITY", 0x58 },
    { "TYPE", "APPLICATION", 0x55, 1 },
    { "TYPE", "APPADDR", 0x56, 1 },
    { "TYPE", "APPAUTH", 0x57, 1 },
    { "TYPE", "RESOURCE", 0x59, 1 },
    { "TYPE", "WLAN", 0x5A, 1 },
    { "TYPE", "SEC-SSID", 0x5B, 1 },
    { "TYPE", "EAP", 0x5C, 1 },
    { "TYPE", "CERT", 0x5D, 1 },
    { "TYPE", "WEPKEY", 0x5E, 1 },
    { "TYPE", "INLINE", 0x50 },
    { "NAME", "NAME", 0x7 },
    { "NAME", "NAP-ADDRESS", 0x8 },
    { "NAME", "NAP-ADDRTYPE", 0x9 },
    { "NAME", "CALLTYPE", 0xA },
    { "NAME", "VALIDUNTIL", 0xB },
    { "NAME", "AUTHTYPE", 0xC },
    { "NAME", "AUTHNAME", 0xD },
    { "NAME", "AUTHSECRET", 0xE },
    { "NAME", "LINGER", 0xF },
    { "NAME", "BEARER", 0x10 },
    { "NAME", "NAPID", 0x11 },
    { "NAME", "COUNTRY", 0x12 },
    { "NAME", "NETWORK", 0x13 },
    { "NAME", "INTERNET", 0x14 },
    { "NAME", "PROXY-ID", 0x15 },
    { "NAME", "PROXY-PROVIDER-ID", 0x16 },
    { "NAME", "DOMAIN", 0x17 },
    { "NAME", "PROVURL", 0x18 },
    { "NAME", "PXAUTH-TYPE", 0x19 },
    { "NAME", "PXAUTH-ID", 0x1A },
    { "NAME", "PXAUTH-PW", 0x1B },
    { "NAME", "STARTPAGE", 0x1C },
    { "NAME", "BASAUTH-ID", 0x1D },
    { "NAME", "BASAUTH-PW", 0x1E },
    { "NAME", "PUSHENABLED", 0x1F },
    { "NAME", "PXADDR", 0x20 },
    { "NAME", "PXADDRTYPE", 0x21 },
    { "NAME", "TO-NAPID", 0x22 },
    { "NAME", "PORTNBR", 0x23 },
    { "NAME", "SERVICE", 0x24 },
    { "NAME", "LINKSPEED", 0x25 },
    { "NAME", "DNLINKSPEED", 0x26 },
    { "NAME", "LOCAL-ADDR", 0x27 },
    { "NAME", "LOCAL-ADDRTYPE", 0x28 },
    { "NAME", "CONTEXT-ALLOW", 0x29 },
    { "NAME", "TRUST", 0x2A },
    { "NAME", "MASTER", 0x2B },
    { "NAME", "SID", 0x2C },
    { "NAME", "SOC", 0x2D },
    { "NAME", "WSP-VERSION", 0x2E },
    { "NAME", "PHYSICAL-PROXY-ID", 0x2F },
    { "NAME", "CLIENT-ID", 0x30 },
    { "NAME", "DELIVERY-ERR-SDU", 0x31 },
    { "NAME", "DELIVERY-ORDER", 0x32 },
    { "NAME", "TRAFFIC-CLASS", 0x33 },
    { "NAME", "MAX-SDU-SIZE", 0x34 },
    { "NAME", "MAX-BITRATE-UPLINK", 0x35 },
    { "NAME", "MAX-BITRATE-DNLINK", 0x36 },
    { "NAME", "RESIDUAL-BER", 0x37 },
    { "NAME", "SDU-ERROR-RATIO", 0x38 },
    { "NAME", "TRAFFIC-HANDL-PRIO", 0x39 },
    { "NAME", "TRANSFER-DELAY", 0x3A },
    { "NAME", "GUARANTEED-BITRATE-UPLINK", 0x3B },
    { "NAME", "GUARANTEED-BITRATE-DNLINK", 0x3C },
    { "NAME", "PXADDR-FQDN", 0x3D },
    { "NAME", "PROXY-PW", 0x3E },
    { "NAME", "PPGAUTH-TYPE", 0x3F },
    { "NAME", "PULLENABLED", 0x47 },
    { "NAME", "DNS-ADDR", 0x48 },
    { "NAME", "MAX-NUM-RETRY", 0x49 },
    { "NAME", "FIRST-RETRY-TIMEOUT", 0x4A },
    { "NAME", "REREG-THRESHOLD", 0x4B },
    { "NAME", "T-BIT", 0x4C },
    { "NAME", "AUTH-ENTITY", 0x4E },
    { "NAME", "SPI", 0x4F },
    { "NAME", "AACCEPT", 0x2E, 1 },
    { "NAME", "AAUTHDATA", 0x2F, 1 },
    { "NAME", "AAUTHLEVEL", 0x30, 1 },
    { "NAME", "AAUTHNAME", 0x31, 1 },
    { "NAME", "AAUTHSECRET", 0x32, 1 },
    { "NAME", "AAUTHTYPE", 0x33, 1 },
    { "NAME", "ADDR", 0x34, 1 },
    { "NAME", "ADDRTYPE", 0x35, 1 },
    { "NAME", "APPID", 0x36, 1 },
    { "NAME", "APROTOCOL", 0x37, 1 },
    { "NAME", "PROVIDER-ID", 0x38, 1 },
    { "NAME", "TO-PROXY", 0x39, 1 },
    { "NAME", "URI", 0x3A, 1 },
    { "NAME", "RULE", 0x3B, 1 },
    { "NAME", "APPREF", 0x3C, 1 },
    { "NAME", "TO-APPREF", 0x3D, 1 },
    { "NAME", "PRI-SSID", 0x3E, 1 },
    { "NAME", "PRI-U-SSID", 0x3F, 1 },
    { "NAME", "PRI-H-SSID", 0x40, 1 },
    { "NAME", "S-SSID", 0x41, 1 },
    { "NAME", "S-U-SSID", 0x42, 1 },
    { "NAME", "NETMODE", 0x43, 1 },
    { "NAME", "SECMODE", 0x44, 1 },
    { "NAME", "EAPTYPE", 0x45, 1 },
    { "NAME", "USERNAME", 0x46, 1 },
    { "NAME", "PASSWORD", 0x47, 1 },
    { "NAME", "REALM", 0x48, 1 },
    { "NAME", "USE-PSEUD", 0x49, 1 },
    { "NAME", "ENCAPS", 0x5B, 1 },
    { "NAME", "VER-SER-REALM", 0x4C, 1 },
    { "NAME", "CLIENT-AUTH", 0x4D, 1 },
    { "NAME", "SES-VAL-TIME", 0x4E, 1 },
    { "NAME", "CIP-SUIT", 0x4F, 1 },
    { "NAME", "PEAP-V0", 0x60, 1 },
    { "NAME", "PEAP-V1", 0x61, 1 },
    { "NAME", "PEAP-V2", 0x62, 1 },
    { "NAME", "ISS-NAME", 0x63, 1 },
    { "NAME", "SUB-NAME", 0x64, 1 },
    { "NAME", "CERT-TYPE", 0x65, 1 },
    { "NAME", "SER-NUM", 0x66, 1 },
    { "NAME", "SUB-KEY-ID", 0x67, 1 },
    { "NAME", "THUMBPRINT", 0x68, 1 },
    { "NAME", "WPA-PRES-KEY-ASC", 0x69, 1 },
    { "NAME", "WPA-PRES-KEY-HEX", 0x6A, 1 },
    { "NAME", "WEPKEYIND", 0x6B, 1 },
    { "NAME", "WEPAUTHMODE", 0x6C, 1 },
    { "NAME", "LENGTH", 0x6D, 1 },
    { "NAME", "INDEX", 0x6E, 1 },
    { "NAME", "DATA", 0x6F, 1 },
    { "NAME", "WLANHAND", 0x70, 1 },
    { "NAME", "EDIT-SET", 0x71, 1 },
    { "NAME", "VIEW-SET", 0x72, 1 },
    { "NAME", "FORW-SET", 0x73, 1 },
    { "NAME", "INLINE", 0x5 },
    { "VALUE", "IPV4", 0x85 },
    { "VALUE", "IPV6", 0x86 },
    { "VALUE", "E164", 0x87 },
    { "VALUE", "ALPHA", 0x88 },
    { "VALUE", "APN", 0x89 },
    { "VALUE", "SCODE", 0x8A },
    { "VALUE", "TETRA-ITSI", 0x8B },
    { "VALUE", "MAN", 0x8C },
    { "VALUE", "APPSRV", 0x8D, 1 },
    { "VALUE", "OBEX", 0x8E, 1 },
    { "VALUE", "ANALOG-MODEM", 0x90 },
    { "VALUE", "V.120", 0x91 },
    { "VALUE", "V.110", 0x92 },
    { "VALUE", "X.31", 0x93 },
    { "VALUE", "BIT-TRANSPARENT", 0x94 },
    { "VALUE", "DIRECT-ASYNCHRONOUS-DATA-SERVICE", 0x95 },
    { "VALUE", "PAP", 0x9A },
    { "VALUE", "CHAP", 0x9B },
    { "VALUE", "HTTP-BASIC", 0x9C },
    { "VALUE", "HTTP-DIGEST", 0x9D },
    { "VALUE", "WTLS-SS", 0x9E },
    { "VALUE", "MD5", 0x9F },
    { "VALUE", "GSM-USSD", 0xA2 },
    { "VALUE", "GSM-SMS", 0xA3 },
    { "VALUE", "ANSI-136-GUTS", 0xA4 },
    { "VALUE", "IS-95-CDMA-SMS", 0xA5 },
    { "VALUE", "IS-95-CDMA-CSD", 0xA6 },
    { "VALUE", "IS-95-CDMA-PACKET", 0xA7 },
    { "VALUE", "ANSI-136-CSD", 0xA8 },
    { "VALUE", "ANSI-136-GPRS", 0xA9 },
    { "VALUE", "GSM-CSD", 0xAA },
    { "VALUE", "GSM-GPRS", 0xAB },
    { "VALUE", "AMPS-CDPD", 0xAC },
    { "VALUE", "PDC-CSD", 0xAD },
    { "VALUE", "PDC-PACKET", 0xAE },
    { "VALUE", "IDEN-SMS", 0xAF },
    { "VALUE", "IDEN-CSD", 0xB0 },
    { "VALUE", "IDEN-PACKET", 0xB1 },
    { "VALUE", "FLEX/REFLEX", 0xB2 },
    { "VALUE", "PHS-SMS", 0xB3 },
    { "VALUE", "PHS-CSD", 0xB4 },
    { "VALUE", "TETRA-SDS", 0xB5 },
    { "VALUE", "TETRA-PACKET", 0xB6 },
    { "VALUE", "ANSI-136-GHOST", 0xB7 },
    { "VALUE", "MOBITEX-MPAK", 0xB8 },
    { "VALUE", "CDMA2000-1X-SIMPLE-IP", 0xB9 },
    { "VALUE", "CDMA2000-1X-MOBILE-IP", 0xBA },
    { "VALUE", "3G-GSM", 0xBB },
    { "VALUE", "WLAN", 0xBC },
    { "VALUE", "AUTOBAUDING", 0xC5 },
    { "VALUE", "CL-WSP", 0xCA },
    { "VALUE", "CO-WSP", 0xCB },
    { "VALUE", "CL-SEC-WSP", 0xCC },
    { "VALUE", "CO-SEC-WSP", 0xCD },
    { "VALUE", "CL-SEC-WTA", 0xCE },
    { "VALUE", "CO-SEC-WTA", 0xCF },
    { "VALUE", "OTA-HTTP-TO", 0xD0 },
    { "VALUE", "OTA-HTTP-TLS-TO", 0xD1 },
    { "VALUE", "OTA-HTTP-PO", 0xD2 },
    { "VALUE", "OTA-HTTP-TLS-PO", 0xD3 },
    { "VALUE", ",", 0x90, 1 },
    { "VALUE", "HTTP-", 0x91, 1 },
    { "VALUE", "BASIC", 0x92, 1 },
    { "VALUE", "DIGEST", 0x93, 1 },
    { "VALUE", "AAA", 0xE0 },
    { "VALUE", "HA", 0xE1 },
    { "VALUE", "INLINE", 0x6 },
};

#define OMA_VALUE_TAG 0x06

#define NUMBER_OF_OMA_ATTRIBUTES sizeof(oma_ota_attributes)/sizeof(oma_ota_attributes[0])

#include "xml_definitions.h"

/****************************************************************************
 *
 * Prototypes of internal functions. Note that 'Ptr' means here '*'.
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
                          simple_binary_t **ota_binary);
static int parse_node(xmlNodePtr node, simple_binary_t **otabxml);    
static int parse_element(xmlNodePtr node, simple_binary_t **otabxml);
static int parse_attribute(xmlAttrPtr attr, simple_binary_t **otabxml); 

/***************************************************************************
 *
 * Implementation of the external function
 */

int ota_compile(Octstr *ota_doc, Octstr *charset, Octstr **ota_binary)
{
    simple_binary_t *otabxml;
    int ret;
    xmlDocPtr pDoc;
    size_t size;
    char *ota_c_text;

    *ota_binary = octstr_create(""); 
    otabxml = simple_binary_create();

    octstr_strip_blanks(ota_doc);
    octstr_shrink_blanks(ota_doc);
    set_charset(ota_doc, charset);
    size = octstr_len(ota_doc);
    ota_c_text = octstr_get_cstr(ota_doc);
    pDoc = xmlParseMemory(ota_c_text, size);

    ret = 0;
    if (pDoc) {
        ret = parse_document(pDoc, charset, &otabxml);
        simple_binary_output(*ota_binary, otabxml);
        xmlFreeDoc(pDoc);
    } else {
        xmlFreeDoc(pDoc);
        octstr_destroy(*ota_binary);
        simple_binary_destroy(otabxml);
        error(0, "OTA: No document to parse. Probably an error in OTA source");
        return -1;
    }

    simple_binary_destroy(otabxml);

    return ret;
}

/*****************************************************************************
 *
 * Implementation of internal functions
 *
 * Parse document node. Store wbmxl version number and character set into the 
 * start of the document. There are no wapforum public identifier for ota. 
 * FIXME: Add parse_prologue!
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
                          simple_binary_t **otabxml)
{
    xmlNodePtr node;

    if (document->intSubset && document->intSubset->ExternalID 
        && strcmp((char *)document->intSubset->ExternalID, "-//WAPFORUM//DTD PROV 1.0//EN") == 0) {
        /* OMA ProvCont */
        (*otabxml)->wbxml_version = 0x03; /* WBXML Version number 1.3  */
        (*otabxml)->public_id = 0x0B; /* Public id for this kind of doc */  
    } else {
        /* OTA */
        (*otabxml)->wbxml_version = 0x01; /* WBXML Version number 1.1  */
        (*otabxml)->public_id = 0x01; /* Public id for an unknown document type */
    }
    (*otabxml)->code_page = 0;
    
    charset = octstr_create("UTF-8");
    (*otabxml)->charset = parse_charset(charset);
    octstr_destroy(charset);

    node = xmlDocGetRootElement(document);
    return parse_node(node, otabxml);
}

/*
 * The recursive parsing function for the parsing tree. Function checks the 
 * type of the node, calls for the right parse function for the type, then 
 * calls itself for the first child of the current node if there's one and 
 * after that calls itself for the next child on the list.
 */

static int parse_node(xmlNodePtr node, simple_binary_t **otabxml)
{
    int status = 0;
    
    /* Call for the parser function of the node type. */
    switch (node->type) {
        case XML_ELEMENT_NODE:
            status = parse_element(node, otabxml);
            break;
        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
        /*
         * XML has also many other node types, these are not needed with 
         * OTA. Therefore they are assumed to be an error.
         */
        default:
            error(0, "OTA compiler: Unknown XML node in the OTA source.");
            return -1;
            break;
    }

    /* 
     * If node is an element with content, it will need an end tag after it's
     * children. The status for it is returned by parse_element.
     */
    switch (status) {
        case 0:
            if (node->children != NULL && parse_node(node->children, otabxml) == -1)
                return -1;
            break;
        case 1:
            if (node->children != NULL && parse_node(node->children, otabxml) == -1)
                return -1;
            parse_end(otabxml);
            break;
        case -1: /* Something went wrong in the parsing. */
            return -1;
            break;
        default:
            warning(0,"OTA compiler: Undefined return value in a parse function.");
            return -1;
            break;
    }

    if (node->next != NULL && parse_node(node->next, otabxml) == -1)
        return -1;

    return 0;
}

/*
 * Parse only valid syncsettings tags. Output element tags as binary
 *  tokens. If the element has CDATA content, output it.
 * Returns:      1, add an end tag (element node has no children)
 *               0, do not add an end tag (it has children)
 *              -1, an error occurred
 */
static int parse_ota_syncsettings(xmlNodePtr node, simple_binary_t **otabxml)
{
    Octstr *name, *content;
    unsigned char status_bits, ota_hex;
    int add_end_tag;
    size_t i;

    name = NULL;
    content = NULL;
    name = octstr_create((char *)node->name);
    if (octstr_len(name) == 0) {
        goto error;
    }

    i = 0;
    while (i < NUMBER_OF_SYNCSETTINGS_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(ota_syncsettings_elements[i].name)) == 0)
            break;
        ++i;
    }

    if (i == NUMBER_OF_SYNCSETTINGS_ELEMENTS) {
        goto error;
    }

    ota_hex = ota_syncsettings_elements[i].token;
    output_char(ota_syncsettings_elements[i].token, otabxml);

    /* if the node has CDATA content output it. 
     * Else expect child tags */
    if (!only_blanks((char *)node->children->content)) {
        content = octstr_create((char *)node->children->content);
        parse_inline_string(content, otabxml);
    }

    add_end_tag = 0;
    if ((status_bits = element_check_content(node)) > 0) {
        ota_hex = ota_hex | status_bits;
        /* If this node has children, the end tag must be added after them. */
        if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT) {
            add_end_tag = 1;
        }
    }

    octstr_destroy(content);
    octstr_destroy(name);
    return add_end_tag;

    error:
        warning(0, "OTA compiler: Unknown tag '%s' in OTA SyncSettings source",
                octstr_get_cstr(name));
        octstr_destroy(content);
        octstr_destroy(name);
        return -1;
}

/*
 * Parse an element node. Check if there is a token for an element tag; if not
 * output the element as a string, else output the token. After that, call 
 * attribute parsing functions
 * Returns:      1, add an end tag (element node has no children)
 *               0, do not add an end tag (it has children)
 *              -1, an error occurred
 */
static int parse_element(xmlNodePtr node, simple_binary_t **otabxml)
{
    Octstr *name;
    size_t i;
    unsigned char status_bits, ota_hex;
    int add_end_tag, syncstat;
    xmlAttrPtr attribute;

    /* if compiling a syncsettings document there's no need to
       continue with the parsing of ota or oma tags. */
    syncstat = -1;
    if (octstr_search_char((**otabxml).binary, 0x55, 0) == 0) {
        syncstat = parse_ota_syncsettings(node, otabxml);
        if (syncstat >= 0) {
            return syncstat;
        }
    }

    name = octstr_create((char *)node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(ota_elements[i].name)) == 0)
            break;
        ++i;
    }

    status_bits = 0x00;
    ota_hex = 0x00;
    add_end_tag = 0;

    if (i != NUMBER_OF_ELEMENTS) {
        ota_hex = ota_elements[i].token;
        if ((status_bits = element_check_content(node)) > 0) {
            ota_hex = ota_hex | status_bits;
            if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
                add_end_tag = 1;
        }
        output_char(ota_hex, otabxml);
    } else {
        warning(0, "OTA compiler: Unknown tag '%s' in OTA source", octstr_get_cstr(name));
        ota_hex = WBXML_LITERAL;
        if ((status_bits = element_check_content(node)) > 0) {
            ota_hex = ota_hex | status_bits;
            /* If this node has children, the end tag must be added after them. */
            if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
                add_end_tag = 1;
        }
        output_char(ota_hex, otabxml);
        output_octet_string(octstr_duplicate(name), otabxml);
    }

    if (node->properties != NULL) {
        attribute = node->properties;
        while (attribute != NULL) {
            parse_attribute(attribute, otabxml);
            attribute = attribute->next;
        }
        parse_end(otabxml);
    }

    octstr_destroy(name);
    return add_end_tag;
}

/*
 * Tokenises an attribute, and in most cases, its value. (Some values are re-
 * presented as an inline string). Tokenisation is based on tables in ota, 
 * chapters 8.1 and 8.2. 
 * Returns 0 when success, -1 when error.
 */
static int parse_attribute(xmlAttrPtr attr, simple_binary_t **otabxml)
{
    Octstr *name, *value, *valueos, *nameos;
    unsigned char ota_hex;
    size_t i, limit;
    ota_3table_t *alist;

    name = octstr_create((char *)attr->name);

    if (attr->children != NULL)
        value = create_octstr_from_node((char *)attr->children);
    else 
        value = NULL;

    if (value == NULL)
        goto error;

    /* OMA has it's own dedicated public ID, so use this */        
    if ((*otabxml)->public_id == 0x0B) { 
        alist = oma_ota_attributes;
        limit = NUMBER_OF_OMA_ATTRIBUTES;
    } else {
        alist = ota_attributes;
        limit = NUMBER_OF_ATTRIBUTES;
    }

    i = 0;
    valueos = NULL;
    nameos = NULL;
    while (i < limit) {
        nameos = octstr_imm(alist[i].name);
        if (octstr_case_compare(name, nameos) == 0) {
            if (alist[i].value != NULL) {
                valueos = octstr_imm(alist[i].value);
            }
            if (octstr_case_compare(value, valueos) == 0) {
                break;
            }
            if (octstr_compare(valueos, octstr_imm("INLINE")) == 0) {
                break;
            }
        }
       ++i;
    }

    if (i == limit) {
        warning(0, "OTA compiler: Unknown attribute '%s' in OTA source, "
                   "with value '%s'.", 
                octstr_get_cstr(name), octstr_get_cstr(value));
        goto error;
    }

    ota_hex = alist[i].token;
    /* if not inline used */
    if (octstr_compare(valueos, octstr_imm("INLINE")) != 0) {
        /* Switch code page. */
        if (alist[i].code_page != (*otabxml)->code_page) { 
            output_char(0, otabxml);
            output_char(alist[i].code_page, otabxml);
            (*otabxml)->code_page = alist[i].code_page;
        }
        /* if OMA add value tag */
        if ((*otabxml)->public_id == 0x0B && name 
            && octstr_case_compare(name, octstr_imm("value")) == 0)
            output_char(OMA_VALUE_TAG, otabxml);
        output_char(ota_hex, otabxml);
    } else {
        /* Switch code page. */
        if (alist[i].code_page != (*otabxml)->code_page) {
            output_char(0, otabxml);
            output_char(alist[i].code_page, otabxml);
            (*otabxml)->code_page = alist[i].code_page;
        }
        output_char(ota_hex, otabxml);
        parse_inline_string(value, otabxml);
    }  

    octstr_destroy(name);
    octstr_destroy(value);
    return 0;

error:
    octstr_destroy(name);
    octstr_destroy(value);
    return -1;
}
