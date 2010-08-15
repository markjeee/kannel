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
 * xml_definitions.h - definitions common to XML compilers
 *
 * This file contains definitions for global tokens 
 *
 * Tuomas Luttinen for Wapit Ltd.
 */

/***********************************************************************
 * Declarations of global tokens. 
 */

#define WBXML_SWITCH_PAGE 0x00
#define WBXML_END         0x01
#define WBXML_ENTITY      0x02
#define WBXML_STR_I       0x03
#define WBXML_LITERAL     0x04
#define WBXML_EXT_I_0     0x40
#define WBXML_EXT_I_1     0x41
#define WBXML_EXT_I_2     0x42
#define WBXML_PI          0x43
#define WBXML_LITERAL_C   0x44
#define WBXML_EXT_T_0     0x80
#define WBXML_EXT_T_1     0x81
#define WBXML_EXT_T_2     0x82
#define WBXML_STR_T       0x83
#define WBXML_LITERAL_A   0x84
#define WBXML_EXT_0       0xC0
#define WBXML_EXT_1       0xC1
#define WBXML_EXT_2       0xC2
#define WBXML_OPAQUE      0xC3
#define WBXML_LITERAL_AC  0xC4

#define WBXML_STR_END     0x00

#define WBXML_CONTENT_BIT 0x40
#define WBXML_ATTR_BIT    0x80

#define WBXML_STRING_TABLE_MIN 4    

#define WBXML_START_NUM 100

 







