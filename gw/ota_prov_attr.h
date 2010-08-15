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
 * ota_prov_attr.h: binary encoded XML tag constants for the OTA provisioning
 *
 * In order to make handsets accept browser settings sent over the air, the
 * settings must be provided in a binary encoded XML document with a specific
 * MIME-type depending on the setting type. The settings must be pushed over
 * SMS to a predefined WDP (Wireless Datagram Protocol) port (49999) as a WSP
 * (Wirless Session Protocol) connection less unsecure push. Handsets able to
 * receive settings over the air must always listen on this port.
 * 
 * Two setting MIME-types are defined:
 *
 *  - application/x-wap-prov.browser-settings
 *  - application/x-wap-prov.browser-bookmarks
 *
 * Description of the XML DTD (Document Type Definition):
 *
 *   <!ELEMENT CHARACTERISTIC-LIST(CHARACTERISTIC)+>
 *   <!ELEMENT CHARACTERISTIC(PARM*)>
 *   <!ATTLIST CHARACTERISTIC
 *     TYPE     CDATA       #REQUIRED
 *     VALUE    CDATA       #IMPLIED
 *   >
 *   <!ELEMENT PARM EMPTY>
 *   <!ATTLIST PARM
 *     NAME     CDATA       #REQUIRED
 *     VALUE    CDATA       #REQUIRED
 *   >
 *
 * Simple example of a XML document:
 *
 *   <?xml version="1.0"?>
 *   <!DOCTYPE CHARACTERISTIC-LIST SYSTEM "/DTD/characteristic_list.xml">
 *   <CHARACTERISTIC-LIST>
 *      <CHARACTERISTIC TYPE="ADDRESS">
 *          <PARM NAME="BEARER" VALUE="GSM/CSD"/>
 *          <PARM NAME="PROXY" VALUE="10.11.12.13"/>
 *          <PARM NAME="CSD_DIALSTRING" VALUE="013456789"/>
 *          <PARM NAME="PPP_AUTHTYPE" VALUE="PAP"/>
 *          <PARM NAME="PPP_AUTHNAME" VALUE="wapusr"/>
 *          <PARM NAME="PPP_AUTHSECRET" VALUE"thepasswd"/>
 *      </CHARACTERISTIC>
 *      <CHARACTERISTIC TYPE="NAME">
 *          <PARM NAME="NAME" VALUE="Our company's WAP settings"/>
 *      </CHARACTERISTIC>
 *      <CHARACTERISTIC TYPE="URL" VALUE="http://wap.company.com"/>
 *      <CHARACTERISTIC TYPE="MMSURL" VALUE="http://mms.company.com"/>
 *      <CHARACTERISTIC TYPE="BOOKMARK">
 *          <PARM NAME="NAME" VALUE="Our company's WAP site"/>
 *          <PARM NAME="URL" VALUE="http://wap.company.com"/>
 *      </CHARACTERISTIC>
 *   </CHARACTERISTIC-LIST>
 *
 * (based upon the Nokia Over The Air Settings Specification)
 *
 * initial re-engineered code by Yann Muller - 3G Lab, 2000.
 * fixed to support official specs by Stipe Tolj - Wapme Systems AG, 2001.
 * extensive inline documentation by Stipe Tolj - Wapme Systems AG, 2001.
 */

 /*
  * The XML document which is build in smsbox.c:smsbox_req_sendota() is
  * binary encoded according to WBXML with the following global tokens
  */

#define WBXML_TOK_END_STR_I             0x00
#define WBXML_TOK_END                   0x01
#define WBXML_TOK_STR_I                 0x03


/********************************************************************
 * Description of the single XML tag tokens
 */

#define WBXML_TOK_CHARACTERISTIC_LIST   0x05

/*
 * This element groups the browser settings into logical different types:
 * ADDRESS, BOOKMARK, URL, MMSURL, NAME and ID.
 */
#define WBXML_TOK_CHARACTERISTIC        0x06

/*
 * The PARM element is used to provide the actual value for the individual
 * settings parameters within each CHARACTERISTIC element.
 */
#define WBXML_TOK_PARM                  0x07

/*
 * Tokens representing the NAME or VALUE tags 
 */
#define WBXML_TOK_NAME                  0x10
#define WBXML_TOK_VALUE                 0x11


/******************************************************************** 
 * CHARACTERISTIC elements with TYPE=ADDRESS 
 * 
 * Characteristics elements with the TYPE=ADDRESS attribute embrace settings
 * concerning a particular bearer, e.g. GSM/SMS or GSM/CSD. Several address
 * settings can be provided in one document. However, for each bearer, only
 * the address settings listed first will be used. The type of the bearer is
 * specified by a PARM attribute and depending on the bearer additional PARM
 * elements are required or optional.
 *
 * Example:
 *
 *      <CHARACTERISTIC TYPE="ADDRESS">
 *          <PARM NAME="BEARER" VALUE="GSM/CSD"/>
 *          <PARM NAME="PROXY" VALUE="10.11.12.13"/>
 *          <PARM NAME="CSD_DIALSTRING" VALUE="013456789"/>
 *          <PARM NAME="PPP_AUTHTYPE" VALUE="PAP"/>
 *          <PARM NAME="PPP_AUTHNAME" VALUE="wapusr"/>
 *          <PARM NAME="PPP_AUTHSECRET" VALUE"thepasswd"/>
 *      </CHARACTERISTIC>
 */

#define WBXML_TOK_TYPE_ADDRESS          0x06

/*
 * The PARM element with NAME=BEARER attribute is used to identify the bearer
 * to be used for a specific setting set. VALUE can be assigned following:
 *
 *   VALUE -> [*GSM_CSD*|GSM_SMS|GSM_USSD|IS136_CSD|GPRS]
 */
#define WBXML_TOK_NAME_BEARER           0x12

#define WBXML_TOK_VALUE_GSM_CSD         0x45
#define WBXML_TOK_VALUE_GSM_SMS         0x46
#define WBXML_TOK_VALUE_GSM_USSD        0x47
#define WBXML_TOK_VALUE_IS136_CSD       0x48
#define WBXML_TOK_VALUE_GPRS            0x49

/*
 * The PARM element with NAME=PROXY attribute is used to identify the IP
 * address of the WAP proxy in case of CSD and the service number in case of
 * SMS. In case of USSD the PROXY can be either an IP address or an MSISDN
 * number. This is indicated in the PROXY_TYPE PARM element. VALUE can be 
 * assigned following:
 *
 *   VALUE -> proxy(using inline string)
 */
#define WBXML_TOK_NAME_PROXY            0x13

/*
 * The PARM element with NAME=PORT attribute specifies whether connection less
 * or connection oriented connections should be used. VALUE can be assigned
 * following:
 *
 *   VALUE -> [*9200*|9201|9202|9203]
 *
 * Use 9200 (or 9202) for connection less connections and 9201 (or 9203) for
 * connection oriented connections. Port numbers 9202 and 9203 enable secure
 * connections (by means of WTLS), whereas port numbers 9200 and 9201 disable
 * secure connections.
 */
#define WBXML_TOK_NAME_PORT             0x14

#define WBXML_TOK_VALUE_PORT_9200       0x60
#define WBXML_TOK_VALUE_PORT_9201       0x61
#define WBXML_TOK_VALUE_PORT_9202       0x62
#define WBXML_TOK_VALUE_PORT_9203       0x63

/*
 * The PARM element with the NAME=PROXY_TYPE attribute is used to identify 
 * the format of the PROXY PARM element. VALUE can be assigned following:
 *
 *   VALUE -> [*MSISDN_NO*|IPV4]
 */
#define WBXML_TOK_NAME_PROXY_TYPE       0x16

#define WBXML_TOK_VALUE_MSISDN_NO       0x76
#define WBXML_TOK_VALUE_IPV4            0x77

/*
 * The PARM elements with the NAME=PROXY_AUTHNAME and NAME=PROXY_AUTHSECRET
 * attributes indicates the login name and password to be used for gateway 
 * required authentication. Support of this PARM elements is manufacturer 
 * specific. VALUEs can be assigned following:
 *
 *   VALUE -> login name(using inline string)
 *   VALUE -> password(using inline string)
 */
#define WBXML_TOK_NAME_PROXY_AUTHNAME       0x18
#define WBXML_TOK_NAME_PROXY_AUTHSECRET     0x19

/*
 * The PARM element with the NAME=PROXY_LOGINTYPE attribute specifies whether
 * an automatic or manual login should be performed at the proxy. VALUE can
 * be assigned following:
 *
 *   VALUE -> [AUTOMATIC|MANUAL]
 *
 * Using the MANUAL logintype the user will be prompted for username and 
 * password when a browse session is started. Using the AUTOMATIC logintype
 * the user will be NOT prompted for username and password when a browse
 * session is started, but a static name and password from the WAP settingset
 * will be used.
 */
#define WBXML_TOK_NAME_PROXY_LOGINTYPE      0x1E

#define WBXML_TOK_VALUE_AUTOMATIC           0x64
#define WBXML_TOK_VALUE_MANUAL              0x65

/*
 * The PARM element with the NAME=PPP_AUTHTYPE attribute indicates which 
 * protocol to use for user authentication. VALUE can be assigned following:
 *
 *   VALUE -> [*PAP*|CHAP|MS_CHAP]
 *
 * PAP is short for Password Authentication Protocol, a type of authentication
 * which uses clear-text passwords and is the least sophisticated 
 * authentication protocol, and CHAP stands for Challenge Handshake 
 * Authentication Protocol, a protocol used to negotiate the most secure form
 * of encrypted authentication supported by both server and client. MS_CHAP
 * (Microsoft(tm)-CHAP) is similar to the CHAP protocol, but is using an 
 * encryption scheme that is alternative to the one used for CHAP.
 */
#define WBXML_TOK_NAME_PPP_AUTHTYPE         0x22

#define WBXML_TOK_VALUE_AUTH_PAP            0x70
#define WBXML_TOK_VALUE_AUTH_CHAP           0x71
#define WBXML_TOK_VALUE_AUTH_MS_CHAP        0x78

/*
 * The PARM elements with the NAME=PPP_AUTHNAME and NAME=PPP_AUTHSECRET
 * attributes indicate the login name and password to be used. VALUEs can be 
 * assigned following:
 *
 *   VALUE -> login name(using inline string)
 *   VALUE -> password(using inline string)
 *
 * Maximum length of login name is 32 bytes.
 * Maximum length of password is 20 bytes.
 */
#define WBXML_TOK_NAME_PPP_AUTHNAME         0x23
#define WBXML_TOK_NAME_PPP_AUTHSECRET       0x24

/*
 * The PARM element with the NAME=PPP_LOGINTYPE attribute specifies whether an
 * automatic or manual login should be performed in the PPP negotiation at the
 * access point of the service provider. VALUE can be assigned following
 *
 *   VALUE -> [AUTOMATIC|MANUAL]
 *
 * (same impacts as for PROXY_LOGINTYPE)
 */
#define WBXML_TOK_NAME_PPP_LOGINTYPE        0x1D

/*
 * The PARM element with the NAME=CSD_DIALSTRING attribute specifies the 
 * MSISDN number of the modem pool. VALUE can be assigned following:
 *
 *   VALUE -> msisdn number(using inline string)
 *
 * Maximum length of msisdn number is 21 bytes.
 */
#define WBXML_TOK_NAME_CSD_DIALSTRING       0x21

/*
 * The PARM element with the NAME=CSD_CALLTYPE attribute indicates the type
 * of circuit switched call to be used for connection. VALUE can be assigned
 * following:
 *
 *   VALUE -> [*ANALOGUE*|ISDN]
 * 
 * (In general the call type should be set to ANALOGUE since ISDN is not 
 * generaly available on all networks.)
 */
#define WBXML_TOK_NAME_CSD_CALLTYPE         0x28

#define WBXML_TOK_VALUE_CONN_ANALOGUE       0x72
#define WBXML_TOK_VALUE_CONN_ISDN           0x73

/*
 * The PARM element with the NAME=CSD_CALLSPEED attribute indicates the 
 * desired call speed to be used for the connection. VALUE can be assgined
 * following:
 *
 *   VALUE -> [*AUTO*|*9600*|14400|19200|28800|38400|43200|57600]
 *
 * Default value is AUTO when CSD_CALLTYPE is ANALOGUE and 9600 when 
 * CSD_CALLTYPE is ISDN.
 */
#define WBXML_TOK_NAME_CSD_CALLSPEED        0x29

#define WBXML_TOK_VALUE_SPEED_AUTO          0x6A
#define WBXML_TOK_VALUE_SPEED_9600          0x6B
#define WBXML_TOK_VALUE_SPEED_14400         0x6C
#define WBXML_TOK_VALUE_SPEED_19200         0x6D
#define WBXML_TOK_VALUE_SPEED_28800         0x6E
#define WBXML_TOK_VALUE_SPEED_38400         0x6F
#define WBXML_TOK_VALUE_SPEED_43200         0x74
#define WBXML_TOK_VALUE_SPEED_57600         0x75

/*
 * The PARM element with the NAME=ISP_NAME attribute indicates the name of the
 * Internet Service Provider. Support of this PARM element is manufacturer
 * specific. VALUE can be assigned following:
 *
 *   VALUE -> isp name(using inline string)
 *
 * Maximum length of isp name is 20 bytes.
 */
#define WBXML_TOK_NAME_ISP_NAME             0x7E

/*
 * The PARM element with the NAME=SMS_SMSC_ADDRESS attribute indicates the 
 * MSISDN number of the SMS Service Center (SMSC). VALUE can be assigned
 * following:
 *
 *   VALUE -> sms smsc address(using inline string)
 *
 * Maximum length of sms smsc address is 21 bytes.
 */
#define WBXML_TOK_NAME_SMS_SMSC_ADDRESS     0x1A

/*
 * The PARM element with the name NAME=USSD_SERVICE_CODE attribute indicates
 * the USSD service code. VALUE can be assigned following:
 *
 *   VALUE -> ussd service code(using inline string)
 * 
 * Maximum length of ussd service code is 10 bytes.
 */
#define WBXML_TOK_NAME_USSD_SERVICE_CODE    0x1B

/*
 * The PARM element with the NAME=GPRS_ACCESSPOINTNAME attribute indicates
 * the access point name on Gateway GRPS Support Nodes (GGSN). Allowed
 * characters are: ['a'-'z','A'-'Z','0'-'9','.','-','*']
 *
 *   VALUE -> acess point name(using inline string)
 *
 * Maximum length of access point name is 100 bytes.
 */
#define WBXML_TOK_NAME_GPRS_ACCESSPOINTNAME 0x1C


/******************************************************************** 
 * CHARACTERISTIC elements with TYPE=URL 
 * 
 * The CHARACTERISTIC element with the TYPE=URL attribute has only one 
 * attribute which indicates the URL of the home page. VALUES can be assigned
 * following:
 *
 *   VALUE -> url(using inline string)
 *
 * Maximum length of URL is 100 bytes.
 *
 * Example:
 *
 *      <CHARACTERISTIC TYPE="URL" VALUE="http://wap.company.com"/>
 */

#define WBXML_TOK_TYPE_URL                  0x07


/******************************************************************** 
 * CHARACTERISTIC elements with TYPE=MMSURL 
 * 
 * The CHARACTERISTIC element with the TYPE=MMSURL attribute has only one 
 * attribute which indicates the URL of the MMSC. VALUES can be assigned
 * following:
 *
 *   VALUE -> url(using inline string)
 *
 * Maximum length of URL is 100 bytes.
 *
 * Example:
 *
 *      <CHARACTERISTIC TYPE="MMSURL" VALUE="http://wap.company.com/mmsc"/>
 */

#define WBXML_TOK_TYPE_MMSURL               0x7C


/******************************************************************** 
 * CHARACTERISTIC elements with TYPE=NAME 
 *
 * This element type must contain exactly one PARM element with NAME=NAME,
 * which states the user-recognisable name to apply for the settings. The 
 * VALUE of the PARM element can be assigned following:
 *
 *   VALUE -> name(using inline string)
 *
 * Maximum length of name is 20 bytes.
 *
 * Example:
 * 
 *      <CHARACTERISTIC TYPE="NAME">
 *          <PARM NAME="NAME" VALUE="Our company's WAP settings"/>
 *      </CHARACTERISTIC>
 */

#define WBXML_TOK_TYPE_NAME                 0x08


/******************************************************************** 
 * CHARACTERISTIC elements with TYPE=BOOKMARK 
 *
 * This element must contain exactly two PARM elements, which define the 
 * name and URL for a homepage or for bookmarks.
 *
 * When this element is used with the MIME-type *.browser-settings the first
 * element indicates the homepage to be used together with the corresponding
 * settings. Note that the URL included in this element and the CHARACTERISTIC
 * element TYPE=URL are both required to define a homepage and their content
 * must be equal. A homepage and several bookmarks can be provided in one
 * document of the MIME-type referred to above. However, the maximum number of
 * bookmarks accepted is manufacturer specific.
 *
 * When this element is used with the MIME-type *.browser-bookmarks the 
 * element indicates bookmarks only
 *
 * Example:
 *
 *      <CHARACTERISTIC TYPE="BOOKMARK">
 *          <PARM NAME="NAME" VALUE="Our company's WAP site"/>
 *          <PARM NAME="URL" VALUE="http://wap.company.com"/>
 *      </CHARACTERISTIC>
 */

#define WBXML_TOK_TYPE_BOOKMARK             0x7F

/*
 * The PARM element with the NAME=NAME attribute indicates the name of the
 * bookmark or homepage. VALUE can be assigned following:
 *
 *   VALUE -> bookmark name(using inline string)
 *
 * Maximum length of bookmark name is 50 bytes.
 */
#define WBXML_TOK_NAME_NAME                 0x15

/*
 * The PARM element with the NAME=URL attribute indicates the URL of the
 * bookmark or homepage. VALUE can be assigned following:
 *
 *   VALUE -> bookmark url(using inline string)
 *
 * Maximum length of bookmark url is 255 bytes.
 */
#define WBXML_TOK_NAME_URL                  0x17


/******************************************************************** 
 * CHARACTERISTIC elements with TYPE=ID 
 *
 * This element type must contain exactly one PARM element, which defines an
 * ID to be used to provide some security to the provisioning application. 
 * The ID should be known by the subscriber through the subscription or 
 * through other communication with the operator. When provisioning data
 * containing the ID is received the user is able to verify the received ID
 * with the ID previously received by other means from the operator. Support
 * of this CHARACTERISTIC element is manufacturer specific.
 *
 * Example:
 *
 *      <CHARACTERISTIC TYPE="ID">
 *          <PARM NAME="NAME" VALUE="12345678"/>
 *      </CHARACTERISTIC>
 */

#define WBXML_TOK_TYPE_ID                   0x7D

/*
 * The PARM elment with the NAME=NAME attribute indicates the ID. VALUE can be
 * assigned following:
 *
 *   VALUE -> id(using inline string)
 *
 * Maximum length of id is 8 bytes.
 */

/* end of ota_prov_attr.h */
