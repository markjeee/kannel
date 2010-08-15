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
 * ota_prov.h: OTA settings and bookmarks provisioning routines
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

#ifndef OTA_PROV_H
#define OTA_PROV_H

#include "gwlib/gwlib.h"


/*
 * Our WSP data: a compiled OTA document
 * Return -2 when header error, -1 when compile error, 0 when no error
 */
int ota_pack_message(Msg **msg, Octstr *ota_doc, Octstr *doc_type, 
                     Octstr *from, Octstr *phone_number, Octstr *sec, Octstr *pin);

/*
 * Tokenizes a given 'ota-setting' group (without using the xml compiler) to
 * a binary message and returns the whole message including sender and 
 * receiver numbers.
 */
Msg *ota_tokenize_settings(CfgGroup *grp, Octstr *from, Octstr *receiver);

/*
 * Tokenizes a given 'ota-bookmark' group (without using the xml compiler) to
 * a binary message and returns the whole message including sender and 
 * receiver numbers.
 */
Msg *ota_tokenize_bookmarks(CfgGroup *grp, Octstr *from, Octstr *receiver);


#endif /* OTA_PROV_H */
