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

 * gw/wap-maps.h - URL mapping 

 * 

 * Bruno Rodrigues  <bruno.rodrigues@litux.org>

 */



/*

 * Adds a mapping entry to map an url into an (all optional)

 * - an description name

 * - new url

 * - a query-string parameter name to send msisdn, if available

 * - an header name to send msisdn, if available

 * - msisdn header format

 * - accept cookies (1), not accept (0) or use global settings (-1)

 */

void wap_map_add_url(Octstr *name, Octstr *url, Octstr *map_url,

                     Octstr *send_msisdn_query,

                     Octstr *send_msisdn_header,

                     Octstr *send_msisdn_format,

                     int accept_cookies);



/*

 * Adds a mapping entry to map user/pass into a description

 * name and a msisdn

 */

void wap_map_add_user(Octstr *name, Octstr *user, Octstr *pass,

                      Octstr *msisdn);



/*

 * Destruction routines

 */

void wap_map_destroy(void);

void wap_map_user_destroy(void);



/* 

 * Maybe rewrite URL, if there is a mapping. This is where the runtime

 * lookup comes in (called from further down this file, wsp_http.c)

 */

void wap_map_url(Octstr **osp, Octstr **send_msisdn_query, 

                             Octstr **send_msisdn_header, 

                             Octstr **send_msisdn_format, int *accept_cookies);



/* 

 * Provides a mapping facility for resolving user and pass to an

 * predefined MSISDN.

 * Returns 1, if mapping has been found, 0 otherwise.

 */

int wap_map_user(Octstr **msisdn, Octstr *user, Octstr *pass);



/* 

 * Called during configuration read, this adds a mapping for the source URL

 * "DEVICE:home", to the given destination. The mapping is configured

 * as an in/out prefix mapping.

 */

void wap_map_url_config_device_home(char *to);



/* 

 * Called during configuration read, once for each "map-url" statement.

 * Interprets parameter value as a space-separated two-tuple of src and dst.

 */

void wap_map_url_config(char *s);





