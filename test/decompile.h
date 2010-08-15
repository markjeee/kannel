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
 * decompile.h - Declarations and types used by decompile.c
 *
 * Author: Chris Wulff, Vanteon (cwulff@vanteon.com)
 *
 */

#ifndef DECOMPILE_H_INCLUDED
#define DECOMPILE_H_INCLUDED


/* Global Tags */
#define TAG_SWITCH_PAGE 0x00
#define TAG_END         0x01
#define TAG_ENTITY      0x02
#define TAG_STR_I       0x03
#define TAG_LITERAL     0x04
#define TAG_EXT_I_0     0x40
#define TAG_EXT_I_1     0x41
#define TAG_EXT_I_2     0x42
#define TAG_PI          0x43
#define TAG_LITERAL_C   0x44
#define TAG_EXT_T_0     0x80
#define TAG_EXT_T_1     0x81
#define TAG_EXT_T_2     0x82
#define TAG_STR_T       0x83
#define TAG_LITERAL_A   0x84
#define TAG_EXT_0       0xc0
#define TAG_EXT_1       0xc1
#define TAG_EXT_2       0xc2
#define TAG_OPAQUE      0xc3
#define TAG_LITERAL_AC  0xc4

/* Codepage tag masks */
#define CODEPAGE_TAG_MASK        ((WBXML_TAG) 0x3f)
#define CODEPAGE_TAG_HAS_CONTENT ((WBXML_TAG) 0x40)
#define CODEPAGE_TAG_HAS_ATTRS   ((WBXML_TAG) 0x80)

/* Sizes */
#define SWITCHPAGE_SIZE 2

/* Codepage Tag Types */
typedef enum tagCP_TYPES
{
	CP_TAG_TAG,
	CP_TAG_ATTRSTART,
	CP_TAG_ATTRVALUE

} CP_TAG_TYPE;

/* Datatypes */
typedef int               BOOL;
#define FALSE 0
#define TRUE 1

typedef unsigned char     WBXML;
typedef WBXML*            P_WBXML;
typedef P_WBXML*          PP_WBXML;

typedef unsigned char     WBXML_TAG;
typedef WBXML_TAG*        P_WBXML_TAG;

typedef unsigned char     WBXML_CODEPAGE;

typedef unsigned long     WBXML_LENGTH;

typedef enum
{
	NODE_CODEPAGE_TAG,
	NODE_CODEPAGE_LITERAL_TAG,
	NODE_ATTRSTART,
	NODE_ATTRSTART_LITERAL,
	NODE_ATTRVALUE,
	NODE_ATTREND,
	NODE_STRING,
	NODE_DTD_TYPE,
	NODE_STRING_TABLE,
	NODE_VARIABLE_STRING,
	NODE_VARIABLE_INDEX

} WBXML_NODE_TYPE;

typedef struct tagWBXML_NODE
{
	void*           m_prev;   /* (P_WBXML_NODE) the previous sibling */
	void*           m_next;   /* (P_WBXML_NODE) the next sibling */
	void*           m_child;  /* (P_WBXML_NODE) the first child */
	void*           m_parent; /* (P_WBXML_NODE) the parent */
	WBXML_NODE_TYPE m_type;   /* type of this node */
	WBXML_CODEPAGE  m_page;   /* the codepage for this node */
	void*           m_data;   /* type specific node data */

} WBXML_NODE;

typedef WBXML_NODE* P_WBXML_NODE;

typedef enum
{
	VAR_ESCAPED,
	VAR_UNESCAPED,
	VAR_UNCHANGED

} WBXML_VARIABLE_TYPE;

typedef unsigned char       WBXML_U_INT8;
typedef WBXML_U_INT8*       P_WBXML_U_INT8;

#define MAX_MB_U_INT32_BYTES 4

typedef unsigned char       WBXML_MB_U_INT32[MAX_MB_U_INT32_BYTES];
typedef WBXML_MB_U_INT32*   P_WBXML_MB_U_INT32;

extern const WBXML_MB_U_INT32 ZERO_WBXML_MB_U_INT32;

typedef WBXML_MB_U_INT32    WBXML_STRING_INDEX;
typedef WBXML_STRING_INDEX* P_WBXML_STRING_INDEX;

typedef unsigned char*      WBXML_BYTES;
typedef WBXML_BYTES*        P_WBXML_BYTES;

typedef WBXML_MB_U_INT32 WBXML_DTD_TYPE;

typedef struct tagDTD_NODE_DATA
{
	WBXML_DTD_TYPE   m_dtdnum; /* DTD number */
	WBXML_MB_U_INT32 m_index;  /* DTD string table index (for DTD# 0) */

} DTD_NODE_DATA;

typedef struct tagWBXML_INFO
{
  P_WBXML        m_start;   /* Beginning of the binary buffer */
  P_WBXML        m_curpos;  /* Current binary buffer position */
  WBXML_LENGTH   m_length;  /* Length of the binary data */
  P_WBXML_NODE   m_tree;    /* WBXML parse tree */
  P_WBXML_NODE   m_curnode; /* current parse tree node */
  WBXML_CODEPAGE m_curpage; /* the current codepage */

} WBXML_INFO;

typedef WBXML_INFO*         P_WBXML_INFO;

typedef struct tagWBXML_STRING_TABLE
{
  WBXML_MB_U_INT32 m_length;
  WBXML_BYTES      m_strings;

} WBXML_STRING_TABLE;

typedef WBXML_STRING_TABLE* P_WBXML_STRING_TABLE;

typedef enum tagWBXML_PARSE_ERROR
{
  ERR_END_OF_DATA,
  ERR_INTERNAL_BAD_PARAM,
  ERR_TAG_NOT_FOUND,
  ERR_FILE_NOT_FOUND,
  ERR_FILE_NOT_READ,
  ERR_NOT_ENOUGH_MEMORY

} WBXML_PARSE_ERROR;

typedef enum tagWBXML_PARSE_WARNING
{
  WARN_FUTURE_EXPANSION_EXT_0,
  WARN_FUTURE_EXPANSION_EXT_1,
  WARN_FUTURE_EXPANSION_EXT_2

} WBXML_PARSE_WARNING;

typedef struct tagDTD_TYPE_LIST
{
	long  m_id;
	char* m_name;

} DTD_TYPE_LIST;

typedef struct tagCODEPAGE_TAG_NAME_LIST
{
	long           m_dtd_id;
	char*          m_name;
	WBXML_CODEPAGE m_page;
	WBXML_TAG      m_tag;

} CODEPAGE_TAG_NAME_LIST;

typedef CODEPAGE_TAG_NAME_LIST* P_CODEPAGE_TAG_NAME_LIST;

typedef struct tagCODEPAGE_ATTRSTART_NAME_LIST
{
	long           m_dtd_id;
	char*          m_name;
	char*          m_valueprefix;
	WBXML_CODEPAGE m_page;
	WBXML_TAG      m_tag;

} CODEPAGE_ATTRSTART_NAME_LIST;

typedef CODEPAGE_ATTRSTART_NAME_LIST* P_CODEPAGE_ATTRSTART_NAME_LIST;

typedef struct tagCODEPAGE_ATTRVALUE_NAME_LIST
{
	long           m_dtd_id;
	char*          m_name;
	WBXML_CODEPAGE m_page;
	WBXML_TAG      m_tag;

} CODEPAGE_ATTRVALUE_NAME_LIST;

typedef CODEPAGE_ATTRVALUE_NAME_LIST* P_CODEPAGE_ATTRVALUE_NAME_LIST;

/* Flow Control Prototypes */

void Message(char* msg);

void ParseError(WBXML_PARSE_ERROR error);
void ParseWarning(WBXML_PARSE_WARNING warning);

WBXML_LENGTH BytesLeft(P_WBXML_INFO buffer);

BOOL IsTag(P_WBXML_INFO buffer, WBXML_TAG tag);
BOOL IsCodepageTag(P_WBXML_INFO buffer, CP_TAG_TYPE type);

BOOL Is_attrValue  (P_WBXML_INFO buffer);
BOOL Is_extension  (P_WBXML_INFO buffer);
BOOL Is_string     (P_WBXML_INFO buffer);
BOOL Is_switchPage (P_WBXML_INFO buffer);
BOOL Is_inline     (P_WBXML_INFO buffer);
BOOL Is_tableref   (P_WBXML_INFO buffer);
BOOL Is_entity     (P_WBXML_INFO buffer);
BOOL Is_pi         (P_WBXML_INFO buffer);
BOOL Is_opaque     (P_WBXML_INFO buffer);
BOOL Is_zero       (P_WBXML_INFO buffer);

/* Basic Type Decoder Prototypes */

void Read_u_int8     (P_WBXML_INFO buffer, P_WBXML_U_INT8 result);
void Read_mb_u_int32 (P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result);
void Read_bytes      (P_WBXML_INFO buffer, WBXML_LENGTH length, P_WBXML_BYTES result);
void ReadFixedTag    (P_WBXML_INFO buffer, WBXML_TAG tag);
WBXML_TAG ReadCodepageTag (P_WBXML_INFO buffer, CP_TAG_TYPE type);

/* Basic Type Conversion Prototypes */

long mb_u_int32_to_long(P_WBXML_MB_U_INT32 value);

/* Document Structure Decoder Prototypes */

void Read_start      (P_WBXML_INFO buffer);
void Read_strtbl     (P_WBXML_INFO buffer);
void Read_body       (P_WBXML_INFO buffer);
void Read_element    (P_WBXML_INFO buffer);
void Read_content    (P_WBXML_INFO buffer);
WBXML_TAG Read_stag  (P_WBXML_INFO buffer);
void Read_attribute  (P_WBXML_INFO buffer);
void Read_attrStart  (P_WBXML_INFO buffer);
void Read_attrValue  (P_WBXML_INFO buffer);
void Read_extension  (P_WBXML_INFO buffer);
void Read_string     (P_WBXML_INFO buffer);
void Read_switchPage (P_WBXML_INFO buffer);
void Read_inline     (P_WBXML_INFO buffer);
void Read_tableref   (P_WBXML_INFO buffer);
void Read_entity     (P_WBXML_INFO buffer);
void Read_entcode    (P_WBXML_INFO buffer);
void Read_pi         (P_WBXML_INFO buffer);
void Read_opaque     (P_WBXML_INFO buffer);
void Read_version    (P_WBXML_INFO buffer);
void Read_publicid   (P_WBXML_INFO buffer);
void Read_charset    (P_WBXML_INFO buffer);
void Read_termstr    (P_WBXML_INFO buffer);
void Read_termstr_rtn(P_WBXML_INFO buffer, char** result);
void Read_index      (P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result);
void Read_length     (P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result);
void Read_zero       (P_WBXML_INFO buffer);
void Read_pageindex  (P_WBXML_INFO buffer, P_WBXML_U_INT8 result);

#endif /* _DECOMPILE_H_INCLUDED_ */
