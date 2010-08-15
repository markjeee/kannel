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
 * wap_push_pap_mime.h: Header for a (gateway oriented) mime parser for pap 
 * module. This parser conforms proxy rules set in Push Message, chapter 7.
 * (Headers are passed as they are)
 *
 * By Aarno Syvänen for Wapit Ltd 
 */

#ifndef WAP_PUSH_PAP_MIME_H
#define WAP_PUSH_PAP_MIME_H

#include "gwlib/gwlib.h"

/*
 * Implementation of the external function, PAP uses MIME type multipart/
 * related to communicate a push message and related control information from 
 * pi to ppg. Mime_parse separates parts of message and in addition returns
 * MIME-part-headers of the content entity. Preamble and epilogue of are dis-
 * carded from control messages, but not from a multipart content entity.
 * Multipart/related content type is defined in rfc 2046, chapters 5.1, 5.1.1, 
 * and 5.1.7. Grammar is capitulated in rfc 2046 appendix A and in rfc 822, 
 * appendix D. Functions called by mime_parse remove parsed parts from the mime
 * content. 
 * Input: pointer to mime boundary and mime content
 * Output: in all cases, pointer to pap control document and push data. If 
 * there is a capabilities document, pointer to this is returned, too. If there
 * is none, pointer to NULL instead.
 * In addition, return 1 if parsing was succesfull, 0 otherwise.
 */

int mime_parse(Octstr *boundary, Octstr *mime_content, Octstr **pap_content, 
               Octstr **push_data, List **content_headers, 
               Octstr **rdf_content);

#endif
