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
 * decompile.c - A program to test the WML compiler. This tool was written
 *               from the WBXML 1.2 and WML 1.1 specs.
 *
 * Author: Chris Wulff, Vanteon (cwulff@vanteon.com)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include "decompile.h"

const WBXML_MB_U_INT32 ZERO_WBXML_MB_U_INT32 = {0,0,0,0};
long dtd_id;

#define INDENT_SIZE		4

DTD_TYPE_LIST DTDTypeList[] =
{
	{1, "UNKNOWN"},
	{2, "-//WAPFORUM//DTD WML 1.0//EN\"\n"
	 "\"http://www.wapforum.org/DTD/wml.xml"},
	{3, "-//WAPFORUM//DTD WTA 1.0//EN"},
	{4, "-//WAPFORUM//DTD WML 1.1//EN\"\n"
	 "\"http://www.wapforum.org/DTD/wml_1.1.xml"},
	{5, "-//WAPFORUM//DTD SI 1.0//EN\"\n"
	 "\"http://www.wapforum.org/DTD/si.dtd"},
	{6, "-//WAPFORUM//DTD SL 1.0//EN\"\n"
	 "\"http://www.wapforum.org/DTD/sl.dtd"},
	{7, "-//WAPFORUM//DTD CO 1.0//EN"},
	{8, "-//WAPFORUM//DTD CHANNEL 1.1//EN"},
	{9, "-//WAPFORUM//DTD WML 1.2//EN\"\n"
	 "\"http://www.wapforum.org/DTD/wml12.dtd"},
	{0, NULL}
};


/**************************************
 * DTD Public Type 4 (WML 1.1) Tables *
 **************************************/

CODEPAGE_TAG_NAME_LIST CodepageTagNames[] =
{
	{4, "a",         0, 0x1c},
	{4, "anchor",    0, 0x22},
	{4, "access",    0, 0x23},
	{4, "b",         0, 0x24},
	{4, "big",       0, 0x25},
	{4, "br",        0, 0x26},
	{4, "card",      0, 0x27},
	{4, "do",        0, 0x28},
	{4, "em",        0, 0x29},
	{4, "fieldset",  0, 0x2a},
	{4, "go",        0, 0x2b},
	{4, "head",      0, 0x2c},
	{4, "i",         0, 0x2d},
	{4, "img",       0, 0x2e},
	{4, "input",     0, 0x2f},
	{4, "meta",      0, 0x30},
	{4, "noop",      0, 0x31},
	{4, "p",         0, 0x20},
	{4, "postfield", 0, 0x21},
	{4, "pre",       0, 0x1b},
	{4, "prev",      0, 0x32},
	{4, "onevent",   0, 0x33},
	{4, "optgroup",  0, 0x34},
	{4, "option",    0, 0x35},
	{4, "refresh",   0, 0x36},
	{4, "select",    0, 0x37},
	{4, "setvar",    0, 0x3e},
	{4, "small",     0, 0x38},
	{4, "strong",    0, 0x39},
	{4, "table",     0, 0x1f},
	{4, "td",        0, 0x1d},
	{4, "template",  0, 0x3b},
	{4, "timer",     0, 0x3c},
	{4, "tr",        0, 0x1e},
	{4, "u",         0, 0x3d},
	{4, "wml",       0, 0x3f},

	{6, "TAG_05",    1, 0x05},
	{6, "TAG_06",    1, 0x06},
	{6, "TAG_07",    1, 0x07},

	{0, NULL, 0, 0}
};

CODEPAGE_ATTRSTART_NAME_LIST CodepageAttrstartNames[] =
{
	{4, "accept-charset",  NULL,                                0, 0x05},
	{4, "accesskey",       NULL,                                0, 0x5e},
	{4, "align",           NULL,                                0, 0x52},
	{4, "align",           "bottom",                            0, 0x06},
	{4, "align",           "center",                            0, 0x07},
	{4, "align",           "left",                              0, 0x08},
	{4, "align",           "middle",                            0, 0x09},
	{4, "align",           "right",                             0, 0x0a},
	{4, "align",           "top",                               0, 0x0b},
	{4, "alt",             NULL,                                0, 0x0c},
	{4, "class",           NULL,                                0, 0x54},
	{4, "columns",         NULL,                                0, 0x53},
	{4, "content",         NULL,                                0, 0x0d},
	{4, "content",         "application/vnd.wap.wmlc;charset=", 0, 0x5c},
	{4, "domain",          NULL,                                0, 0x0f},
	{4, "emptyok",         "false",                             0, 0x10},
	{4, "emptyok",         "true",                              0, 0x11},
	{4, "enctype",         NULL,                                0, 0x5f},
	{4, "enctype",         "application/x-www-form-urlencoded", 0, 0x60},
	{4, "enctype",         "multipart/form-data",               0, 0x61},
	{4, "format",          NULL,                                0, 0x12},
	{4, "forua",           "false",                             0, 0x56},
	{4, "forua",           "true",                              0, 0x57},
	{4, "height",          NULL,                                0, 0x13},
	{4, "href",            NULL,                                0, 0x4a},
	{4, "href",            "http://",                           0, 0x4b},
	{4, "href",            "https://",                          0, 0x4c},
	{4, "hspace",          NULL,                                0, 0x14},
	{4, "http-equiv",      NULL,                                0, 0x5a},
	{4, "http-equiv",      "Content-Type",                      0, 0x5b},
	{4, "http-equiv",      "Expires",                           0, 0x5d},
	{4, "id",              NULL,                                0, 0x55},
	{4, "ivalue",          NULL,                                0, 0x15},
	{4, "iname",           NULL,                                0, 0x16},
	{4, "label",           NULL,                                0, 0x18},
	{4, "localsrc",        NULL,                                0, 0x19},
	{4, "maxlength",       NULL,                                0, 0x1a},
	{4, "method",          "get",                               0, 0x1b},
	{4, "method",          "post",                              0, 0x1c},
	{4, "mode",            "nowrap",                            0, 0x1d},
	{4, "mode",            "wrap",                              0, 0x1e},
	{4, "multiple",        "false",                             0, 0x1f},
	{4, "multiple",        "true",                              0, 0x20},
	{4, "name",            NULL,                                0, 0x21},
	{4, "newcontext",      "false",                             0, 0x22},
	{4, "newcontext",      "true",                              0, 0x23},
	{4, "onenterbackward", NULL,                                0, 0x25},
	{4, "onenterforward",  NULL,                                0, 0x26},
	{4, "onpick",          NULL,                                0, 0x24},
	{4, "ontimer",         NULL,                                0, 0x27},
	{4, "optional",        "false",                             0, 0x28},
	{4, "optional",        "true",                              0, 0x29},
	{4, "path",            NULL,                                0, 0x2a},
	{4, "scheme",          NULL,                                0, 0x2e},
	{4, "sendreferer",     "false",                             0, 0x2f},
	{4, "sendreferer",     "true",                              0, 0x30},
	{4, "size",            NULL,                                0, 0x31},
	{4, "src",             NULL,                                0, 0x32},
	{4, "src",             "http://",                           0, 0x58},
	{4, "src",             "https://",                          0, 0x59},
	{4, "ordered",         "true",                              0, 0x33},
	{4, "ordered",         "false",                             0, 0x34},
	{4, "tabindex",        NULL,                                0, 0x35},
	{4, "title",           NULL,                                0, 0x36},
	{4, "type",            NULL,                                0, 0x37},
	{4, "type",            "accept",                            0, 0x38},
	{4, "type",            "delete",                            0, 0x39},
	{4, "type",            "help",                              0, 0x3a},
	{4, "type",            "password",                          0, 0x3b},
	{4, "type",            "onpick",                            0, 0x3c},
	{4, "type",            "onenterbackward",                   0, 0x3d},
	{4, "type",            "onenterforward",                    0, 0x3e},
	{4, "type",            "ontimer",                           0, 0x3f},
	{4, "type",            "options",                           0, 0x45},
	{4, "type",            "prev",                              0, 0x46},
	{4, "type",            "reset",                             0, 0x47},
	{4, "type",            "text",                              0, 0x48},
	{4, "type",            "vnd.",                              0, 0x49},
	{4, "value",           NULL,                                0, 0x4d},
	{4, "vspace",          NULL,                                0, 0x4e},
	{4, "width",           NULL,                                0, 0x4f},
	{4, "xml:lang",        NULL,                                0, 0x50},

	{6, "ATTR_06",         NULL,                                1, 0x06},
	{6, "ATTR_07",         NULL,                                1, 0x07},
	{6, "ATTR_08",         NULL,                                1, 0x08},
	{6, "ATTR_11",         NULL,                                1, 0x11},
	{6, "ATTR_12",         NULL,                                1, 0x12},
	{6, "ATTR_13",         NULL,                                1, 0x13},
	{6, "ATTR_14",         NULL,                                1, 0x14},
	{6, "ATTR_15",         NULL,                                1, 0x15},
	{6, "ATTR_21",         NULL,                                1, 0x21},
	{6, "ATTR_22",         NULL,                                1, 0x22},
	{6, "ATTR_23",         NULL,                                1, 0x23},
	{6, "ATTR_24",         NULL,                                1, 0x24},
	{6, "ATTR_28",         NULL,                                1, 0x28},
	{6, "ATTR_29",         NULL,                                1, 0x29},
	{6, "ATTR_45",         NULL,                                1, 0x45},
	{6, "ATTR_61",         NULL,                                1, 0x61},
	{6, "ATTR_62",         NULL,                                1, 0x62},
	{6, "ATTR_63",         NULL,                                1, 0x63},
	{6, "ATTR_64",         NULL,                                1, 0x64},
	{6, "ATTR_6A",         NULL,                                1, 0x6A},
	{6, "ATTR_6B",         NULL,                                1, 0x6B},
	{6, "ATTR_6C",         NULL,                                1, 0x6C},
	{6, "ATTR_70",         NULL,                                1, 0x70},
	{6, "ATTR_71",         NULL,                                1, 0x71},
	{6, "ATTR_73",         NULL,                                1, 0x73},
	{6, "ATTR_74",         NULL,                                1, 0x74},

	{0, NULL,              NULL,                                0, 0}
};

CODEPAGE_ATTRVALUE_NAME_LIST CodepageAttrvalueNames[] =
{
	{4, ".com/",           0, 0x85},
	{4, ".edu/",           0, 0x86},
	{4, ".net/",           0, 0x87},
	{4, ".org/",           0, 0x88},
	{4, "accept",          0, 0x89},
	{4, "bottom",          0, 0x8a},
	{4, "clear",           0, 0x8b},
	{4, "delete",          0, 0x8c},
	{4, "help",            0, 0x8d},
	{4, "http://",         0, 0x8e},
	{4, "http://www.",     0, 0x8f},
	{4, "https://",        0, 0x90},
	{4, "https://www.",    0, 0x91},
	{4, "middle",          0, 0x93},
	{4, "nowrap",          0, 0x94},
	{4, "onenterbackward", 0, 0x96},
	{4, "onenterforward",  0, 0x97},
	{4, "onpick",          0, 0x95},
	{4, "ontimer",         0, 0x98},
	{4, "options",         0, 0x99},
	{4, "password",        0, 0x9a},
	{4, "reset",           0, 0x9b},
	{4, "text",            0, 0x9d},
	{4, "top",             0, 0x9e},
	{4, "unknown",         0, 0x9f},
	{4, "wrap",            0, 0xa0},
	{4, "www.",            0, 0xa1},
	{0, NULL, 0, 0}
};


/**************************
 * Node Tree Construction *
 **************************/

/*
 * Function: NewNode
 *
 * Description:
 *
 *  Allocate and initialize a new node. This links the new node
 *  as the first child of the current node in the buffer. This causes
 *  child nodes to be linked in reverse order. If there is no current
 *  node, then the new node will be linked in as the first child at the
 *  top of the tree.
 *
 * Parameters:
 *
 *  buffer - WBXML buffer to link the new node into
 *  type   - Type of node to allocate
 *
 * Return value:
 *
 *  P_WBXML_NODE - A pointer to the newly allocated node.
 *
 */
static P_WBXML_NODE NewNode(P_WBXML_INFO buffer, WBXML_NODE_TYPE type)
{
	if (buffer)
	{
		P_WBXML_NODE newnode = malloc(sizeof(WBXML_NODE));

		if (newnode)
		{
			newnode->m_prev = NULL;
			newnode->m_child = NULL;

			if (buffer->m_curnode)
			{
				/* Insert this node as the first child of the current node */
				newnode->m_parent = buffer->m_curnode;
				newnode->m_next = buffer->m_curnode->m_child;

				if (buffer->m_curnode->m_child)
				{
					((P_WBXML_NODE)buffer->m_curnode->m_child)->m_prev = newnode;
				}

				buffer->m_curnode->m_child = newnode;
			}
			else
			{
				/* Insert this node at the top of the tree */
				newnode->m_parent = NULL;
				newnode->m_next = buffer->m_tree;
				
				if (buffer->m_tree)
				{
					buffer->m_tree->m_prev = newnode;
				}

				buffer->m_tree = newnode;
			}

			newnode->m_page = buffer->m_curpage;
			newnode->m_type = type;
			newnode->m_data = NULL;
		}
		else
		{
			ParseError(ERR_NOT_ENOUGH_MEMORY);
		}

		return newnode;
	}
	else
	{
		ParseError(ERR_INTERNAL_BAD_PARAM);
	}

	return NULL;
}

/*
 * Function: FreeNode
 *
 * Description:
 *
 *  Free a node, all its children and forward siblings.
 *
 * Parameters:
 *
 *  node - The node to free
 *
 */
static void FreeNode(P_WBXML_NODE node)
{
	if (node)
	{
		if (node->m_child)
		{
			FreeNode(node->m_child);
		}

		if (node->m_next)
		{
			FreeNode(node->m_next);
		}

		free(node);
	}
}

static void AddDTDNode(P_WBXML_INFO buffer, const WBXML_DTD_TYPE dtdnum, const WBXML_MB_U_INT32 index)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_DTD_TYPE);
	newnode->m_data = malloc(sizeof(DTD_NODE_DATA));
	memcpy( &( ((DTD_NODE_DATA*)newnode->m_data)->m_dtdnum ), &(dtdnum[0]), sizeof(WBXML_MB_U_INT32) );
	memcpy( &( ((DTD_NODE_DATA*)newnode->m_data)->m_index ), &(index[0]), sizeof(WBXML_MB_U_INT32) );
	dtd_id = (long) dtdnum[0];
}

static void AddStringTableNode(P_WBXML_INFO buffer, const P_WBXML_STRING_TABLE strings)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_STRING_TABLE);
	newnode->m_data = malloc(sizeof(WBXML_STRING_TABLE));
	memcpy( newnode->m_data, strings, sizeof(WBXML_STRING_TABLE) );
}

static void AddCodepageTagNode(P_WBXML_INFO buffer, WBXML_TAG tag)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_CODEPAGE_TAG);
	newnode->m_data = malloc(sizeof(WBXML_TAG));
	*((P_WBXML_TAG)newnode->m_data) = tag;
}

static void AddCodepageLiteralTagNode(P_WBXML_INFO buffer, WBXML_MB_U_INT32 index)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_CODEPAGE_LITERAL_TAG);
	newnode->m_data = malloc(sizeof(WBXML_MB_U_INT32));
	memcpy( ((P_WBXML_MB_U_INT32)newnode->m_data), &index, sizeof(WBXML_MB_U_INT32) );
}

static void AddAttrStartNode(P_WBXML_INFO buffer, WBXML_TAG tag)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_ATTRSTART);
	newnode->m_data = malloc(sizeof(WBXML_TAG));
	*((P_WBXML_TAG)newnode->m_data) = tag;
}

static void AddAttrStartLiteralNode(P_WBXML_INFO buffer, WBXML_MB_U_INT32 index)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_ATTRSTART_LITERAL);
	newnode->m_data = malloc(sizeof(WBXML_MB_U_INT32));
	memcpy( ((P_WBXML_MB_U_INT32)newnode->m_data), &index, sizeof(WBXML_MB_U_INT32) );
}

static void AddAttrValueNode(P_WBXML_INFO buffer, WBXML_TAG tag)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_ATTRVALUE);
	newnode->m_data = malloc(sizeof(WBXML_TAG));
	*((P_WBXML_TAG)newnode->m_data) = tag;
}

static void AddAttrEndNode(P_WBXML_INFO buffer)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_ATTREND);
	newnode->m_data = NULL;
}

static void AddStringNode(P_WBXML_INFO buffer, char* string)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_STRING);
	newnode->m_data = strdup(string);
}

static void AddVariableStringNode(P_WBXML_INFO buffer, char* string, WBXML_VARIABLE_TYPE type)
{
	/* TODO: add this node */
}

static void AddVariableIndexNode(P_WBXML_INFO buffer, char* string, WBXML_VARIABLE_TYPE type)
{
	/* TODO: add this node */
}


/****************
 * Flow Control *
 ****************/

void Message(char* msg)
{
  printf("%s\n", msg);
}

void ParseError(WBXML_PARSE_ERROR error)
{
  switch (error)
  {
    case ERR_END_OF_DATA:
      Message("Input stream is incomplete (EOF).");
      break;

    case ERR_INTERNAL_BAD_PARAM:
      Message("Internal error: Bad parameter.");
      break;

	case ERR_TAG_NOT_FOUND:
      Message("Tag not found.");
      break;
		
	case ERR_FILE_NOT_FOUND:
      Message("File not found.");
      break;

	case ERR_FILE_NOT_READ:
      Message("File read error.");
      break;

	case ERR_NOT_ENOUGH_MEMORY:
      Message("Not enough memory");
      break;

    default:
      Message("Unknown error.");
      break;
  }

  exit(error);
}

void ParseWarning(WBXML_PARSE_WARNING warning)
{
  switch (warning)
  {
    case WARN_FUTURE_EXPANSION_EXT_0:
      Message("Token EXT_0 encountered. This token is reserved for future expansion.");
      break;

    case WARN_FUTURE_EXPANSION_EXT_1:
      Message("Token EXT_1 encountered. This token is reserved for future expansion.");
      break;

    case WARN_FUTURE_EXPANSION_EXT_2:
      Message("Token EXT_2 encountered. This token is reserved for future expansion.");
      break;

    default:
      Message("Unknown warning.");
      break;
  }
}

WBXML_LENGTH BytesLeft(P_WBXML_INFO buffer)
{
  if (buffer)
  {
    WBXML_LENGTH bytesRead = (buffer->m_curpos - buffer->m_start);
    if (bytesRead >= buffer->m_length)
    {
      return 0;
    }
    else
    {
      return (buffer->m_length - bytesRead);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return 0;
}

BOOL IsTag(P_WBXML_INFO buffer, WBXML_TAG tag)
{
  BOOL result = FALSE;

  if (buffer)
  {
    if (BytesLeft(buffer) >= sizeof(WBXML_TAG))
    {
      result = ((*((WBXML_TAG*) buffer->m_curpos)) == tag);
    }
    else
    {
		/* No more data, so nope, not this tag */
      result = FALSE;
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return result;
}

BOOL IsCodepageTag(P_WBXML_INFO buffer, CP_TAG_TYPE type)
{
	WBXML_U_INT8 result = *(buffer->m_curpos);

	/* NOTE THAT THESE ARE NOT UNIQUE! */
	switch (type)
	{
		case CP_TAG_TAG:
			return TRUE;
		case CP_TAG_ATTRSTART:
			return ((result & 0x80) != 0x80);
		case CP_TAG_ATTRVALUE:
			return ((result & 0x80) == 0x80);
		default:
			return FALSE;
	}
}

BOOL Is_attrValue  (P_WBXML_INFO buffer)
{
	WBXML_INFO tmpbuffer;
	memcpy(&tmpbuffer, buffer, sizeof(WBXML_INFO));
	tmpbuffer.m_curpos += SWITCHPAGE_SIZE;

	return ((Is_switchPage(buffer) && IsCodepageTag(&tmpbuffer, CP_TAG_ATTRVALUE)) ||
		    IsCodepageTag(buffer, CP_TAG_ATTRVALUE) ||
			Is_string(buffer) ||
			Is_extension(buffer) ||
			Is_entity(buffer) ||
			Is_pi(buffer) ||
			Is_opaque(buffer));
}

BOOL Is_extension  (P_WBXML_INFO buffer)
{
	WBXML_INFO tmpbuffer;
	memcpy(&tmpbuffer, buffer, sizeof(WBXML_INFO));
	tmpbuffer.m_curpos += SWITCHPAGE_SIZE;

	return ((Is_switchPage(buffer) &&
		     (IsTag(&tmpbuffer, TAG_EXT_0) ||
		      IsTag(&tmpbuffer, TAG_EXT_1) ||
		      IsTag(&tmpbuffer, TAG_EXT_2) ||
		      IsTag(&tmpbuffer, TAG_EXT_T_0) ||
		      IsTag(&tmpbuffer, TAG_EXT_T_1) ||
		      IsTag(&tmpbuffer, TAG_EXT_T_2) ||
		      IsTag(&tmpbuffer, TAG_EXT_I_0) ||
		      IsTag(&tmpbuffer, TAG_EXT_I_1) ||
		      IsTag(&tmpbuffer, TAG_EXT_I_2))) ||
		    (IsTag(buffer, TAG_EXT_0) ||
		     IsTag(buffer, TAG_EXT_1) ||
		     IsTag(buffer, TAG_EXT_2) ||
		     IsTag(buffer, TAG_EXT_T_0) ||
		     IsTag(buffer, TAG_EXT_T_1) ||
		     IsTag(buffer, TAG_EXT_T_2) ||
		     IsTag(buffer, TAG_EXT_I_0) ||
		     IsTag(buffer, TAG_EXT_I_1) ||
		     IsTag(buffer, TAG_EXT_I_2)));
}

BOOL Is_string     (P_WBXML_INFO buffer)
{
	return (Is_inline(buffer) ||
		    Is_tableref(buffer));
}

BOOL Is_switchPage (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_SWITCH_PAGE);
}

BOOL Is_inline     (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_STR_I);
}

BOOL Is_tableref   (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_STR_T);
}

BOOL Is_entity     (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_ENTITY);
}

BOOL Is_pi         (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_PI);
}

BOOL Is_opaque     (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_OPAQUE);
}

BOOL Is_zero(P_WBXML_INFO buffer)
{
  BOOL result = FALSE;

  if (buffer) 
  {
    if (BytesLeft(buffer) >= 1) 
    {
      result = ((*buffer->m_curpos) == 0);
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return result;
}


/***********************
 * Basic Type Decoders *
 ***********************/

void Read_u_int8(P_WBXML_INFO buffer, P_WBXML_U_INT8 result)
{
  if (buffer && result)
  {
    if (BytesLeft(buffer) >= 1) 
    {
      *result = *(buffer->m_curpos);
      (buffer->m_curpos)++;
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }
}

void Read_mb_u_int32(P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result)
{
  if (buffer && result)
  {
    int i;
    for (i = 0; i < MAX_MB_U_INT32_BYTES; i++)
    {
      if (BytesLeft(buffer) >= 1)
      {
        (*result)[i] = *(buffer->m_curpos);
        (buffer->m_curpos)++;

        if ( !( (*result)[i] & 0x80 ) )
          break;
      }
      else
      {
        ParseError(ERR_END_OF_DATA);
      }
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }
}

void Read_bytes(P_WBXML_INFO buffer, WBXML_LENGTH length, P_WBXML_BYTES result)
{
  if (buffer && result)
  {
    if (BytesLeft(buffer) >= length) 
    {
      *result = (WBXML_BYTES) malloc(length*sizeof(unsigned char));
      memcpy(*result, buffer->m_curpos, length);
      buffer->m_curpos += length;
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }
}

void ReadFixedTag(P_WBXML_INFO buffer, WBXML_TAG tag)
{
  if (buffer)
  {
    if (BytesLeft(buffer) >= sizeof(WBXML_TAG))
    {
      if ((*((WBXML_TAG*) buffer->m_curpos)) == tag)
      {
        buffer->m_curpos += sizeof(WBXML_TAG);
      }
      else
      {
        ParseError(ERR_TAG_NOT_FOUND);
      }
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }
}

WBXML_TAG ReadCodepageTag (P_WBXML_INFO buffer, CP_TAG_TYPE type)
{
  WBXML_TAG tag = 0;

  if (buffer)
  {
    if (BytesLeft(buffer) >= sizeof(WBXML_TAG))
    {
      tag = *((WBXML_TAG*) buffer->m_curpos);

	  switch (type)
	  {
	    case CP_TAG_TAG:
		  buffer->m_curpos += sizeof(WBXML_TAG);
		  break;

		case CP_TAG_ATTRSTART:
		  if ((tag & 0x80) != 0x80)
		  {
		    buffer->m_curpos += sizeof(WBXML_TAG);
		  }
		  else
		  {
			  ParseError(ERR_TAG_NOT_FOUND);
		  }
		  break;

		case CP_TAG_ATTRVALUE:
		  if ((tag & 0x80) == 0x80)
		  {
		    buffer->m_curpos += sizeof(WBXML_TAG);
		  }
		  else
		  {
			  ParseError(ERR_TAG_NOT_FOUND);
		  }
		  break;

		default:
		  ParseError(ERR_TAG_NOT_FOUND);
		  break;
      }
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return tag;
}


/**************************
 * Basic Type Conversions *
 **************************/

long mb_u_int32_to_long(P_WBXML_MB_U_INT32 value)
{
  long result = 0;

  if (value)
  {
    int i;
    for (i = 0; i < MAX_MB_U_INT32_BYTES; i++)
    {
      result <<= 7;
      result |= ((*value)[i] & 0x7f);

      if ( !( (*value)[i] & 0x80 ) )
        break;
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return result;
}

static void OutputEncodedString(const unsigned char* str)
{
	/* Work our way down the string looking for illegal chars */
	while (*str != 0)
	{
		if ((*str < 0x20) || (*str > 0x7F))
		{
			/* Out of range... encode */
			printf("&#x%02x;", *str);
		}
		else
		{
			switch (*str)
			{
				case '<':
				case '>':
				case '&':
				case '\'':
				case '\"':
					/* Special symbol... encode */
					printf("&#x%2x", *str);
					break;

				default:
					printf("%c", *str);
					break;
			}
		}

		str++;
	}
}


/*******************************
 * Document Structure Decoders *
 *******************************/

void Read_start      (P_WBXML_INFO buffer)
{
  Read_version(buffer);
  Read_publicid(buffer);
  Read_charset(buffer);
  Read_strtbl(buffer);
  Read_body(buffer);
}

void Read_strtbl     (P_WBXML_INFO buffer)
{
  WBXML_STRING_TABLE result;
  Read_mb_u_int32(buffer, &(result.m_length));
  Read_bytes(buffer, mb_u_int32_to_long(&(result.m_length)), &(result.m_strings));

  AddStringTableNode(buffer, &result);
}

void Read_body       (P_WBXML_INFO buffer)
{
  while (Is_pi(buffer))
  {
    Read_pi(buffer);
  }

  Read_element(buffer);

  while (Is_pi(buffer))
  {
    Read_pi(buffer);
  }
}

void Read_element    (P_WBXML_INFO buffer)
{
  WBXML_TAG stagvalue = 0;

  if (Is_switchPage(buffer))
  {
    Read_switchPage(buffer);
  }

  stagvalue = Read_stag(buffer);

  /* move the current node down to this one in the tree */
  if (buffer->m_curnode)
	  buffer->m_curnode = buffer->m_curnode->m_child;
  else buffer->m_curnode = buffer->m_tree;

  if ((stagvalue & CODEPAGE_TAG_HAS_ATTRS) == CODEPAGE_TAG_HAS_ATTRS)
  {
    do
    {
      Read_attribute(buffer);

    } while (!IsTag(buffer, TAG_END));

    ReadFixedTag(buffer, TAG_END);

	AddAttrEndNode(buffer);
  }

  if ((stagvalue & CODEPAGE_TAG_HAS_CONTENT) == CODEPAGE_TAG_HAS_CONTENT)
  {
    while (!IsTag(buffer, TAG_END))
	{
      Read_content(buffer);
	}

    ReadFixedTag(buffer, TAG_END);
  }

  /* move the current node back up one */
  buffer->m_curnode = buffer->m_curnode->m_parent;
}

void Read_content    (P_WBXML_INFO buffer)
{
	if (Is_string(buffer))
	{
		Read_string(buffer);
	}
	else if (Is_extension(buffer))
	{
		Read_extension(buffer);
	}
	else if (Is_entity(buffer))
	{
		Read_entity(buffer);
	}
	else if (Is_pi(buffer))
	{
		Read_pi(buffer);
	}
	else if (Is_opaque(buffer))
	{
		Read_opaque(buffer);
	}
	else
	{
		/* Assume it is an element */
		Read_element(buffer);
	}
}

WBXML_TAG Read_stag       (P_WBXML_INFO buffer)
{
	if (IsCodepageTag(buffer, CP_TAG_TAG))
	{
		WBXML_TAG tag = ReadCodepageTag(buffer, CP_TAG_TAG);

		AddCodepageTagNode(buffer, tag);

		return tag;
	}
	else if (IsTag(buffer, TAG_LITERAL))
	{
		WBXML_MB_U_INT32 index;

		ReadFixedTag(buffer, TAG_LITERAL);
		Read_index(buffer, &index);

		AddCodepageLiteralTagNode(buffer, index);
	}
	else
	{
		ParseError(ERR_TAG_NOT_FOUND);
	}

	return 0;
}

void Read_attribute  (P_WBXML_INFO buffer)
{
	Read_attrStart(buffer);

	while (Is_attrValue(buffer))
	{
		Read_attrValue(buffer);
	}
}

void Read_attrStart  (P_WBXML_INFO buffer)
{
  if (Is_switchPage(buffer))
  {
	WBXML_TAG tag;
    Read_switchPage(buffer);
    tag = ReadCodepageTag(buffer, CP_TAG_ATTRSTART);

	AddAttrStartNode(buffer, tag);
  }
  else if (IsCodepageTag(buffer, CP_TAG_ATTRSTART))
  {
	WBXML_TAG tag;
    tag = ReadCodepageTag(buffer, CP_TAG_ATTRSTART);

	AddAttrStartNode(buffer, tag);
  }
  else if (IsTag(buffer, TAG_LITERAL))
  {
    WBXML_MB_U_INT32 index;

    ReadFixedTag(buffer, TAG_LITERAL);
    Read_index(buffer, &index);

	AddAttrStartLiteralNode(buffer, index);
  }
  else
  {
    ParseError(ERR_TAG_NOT_FOUND);
  }
}

void Read_attrValue  (P_WBXML_INFO buffer)
{
  if (Is_switchPage(buffer))
  {
	WBXML_TAG tag;
    Read_switchPage(buffer);
    tag = ReadCodepageTag(buffer, CP_TAG_ATTRVALUE);
	AddAttrValueNode(buffer, tag);
  }
  else if (IsCodepageTag(buffer, CP_TAG_ATTRVALUE))
  {
	WBXML_TAG tag;
    tag = ReadCodepageTag(buffer, CP_TAG_ATTRVALUE);
	AddAttrValueNode(buffer, tag);
  }
  else if (Is_string(buffer))
  {
    Read_string(buffer);
  }
  else if (Is_extension(buffer))
  {
    Read_extension(buffer);
  }
  else if (Is_entity(buffer))
  {
    Read_entity(buffer);
  }
  else if (Is_opaque(buffer))
  {
    Read_opaque(buffer);
  }
  else
  {
    ParseError(ERR_TAG_NOT_FOUND);
  }
}

void Read_extension  (P_WBXML_INFO buffer)
{
	if (Is_switchPage(buffer))
	{
		Read_switchPage(buffer);
	}

	if (IsTag(buffer, TAG_EXT_I_0))
	{
		char* str = NULL;

		ReadFixedTag(buffer, TAG_EXT_I_0);
		Read_termstr_rtn(buffer, &str);

		AddVariableStringNode(buffer, str, VAR_ESCAPED); 
	}
	else if (IsTag(buffer, TAG_EXT_I_1))
	{
		char* str = NULL;

		ReadFixedTag(buffer, TAG_EXT_I_1);
		Read_termstr_rtn(buffer, &str);

		AddVariableStringNode(buffer, str, VAR_UNESCAPED); 
	}
	else if (IsTag(buffer, TAG_EXT_I_2))
	{
		char* str = NULL;

		ReadFixedTag(buffer, TAG_EXT_I_2);
		Read_termstr_rtn(buffer, &str);

		AddVariableStringNode(buffer, str, VAR_UNCHANGED); 
	}
	else if (IsTag(buffer, TAG_EXT_T_0))
	{
		WBXML_MB_U_INT32 index;

		ReadFixedTag(buffer, TAG_EXT_T_0);
		Read_index(buffer, &index);

		AddVariableIndexNode(buffer, index, VAR_ESCAPED);
	}
	else if (IsTag(buffer, TAG_EXT_T_1))
	{
		WBXML_MB_U_INT32 index;

		ReadFixedTag(buffer, TAG_EXT_T_1);
		Read_index(buffer, &index);

		AddVariableIndexNode(buffer, index, VAR_UNESCAPED);
	}
	else if (IsTag(buffer, TAG_EXT_T_2))
	{
		WBXML_MB_U_INT32 index;

		ReadFixedTag(buffer, TAG_EXT_T_2);
		Read_index(buffer, &index);

		AddVariableIndexNode(buffer, index, VAR_UNCHANGED);
	}
	else if (IsTag(buffer, TAG_EXT_0))
	{
		ReadFixedTag(buffer, TAG_EXT_0);

		ParseWarning(WARN_FUTURE_EXPANSION_EXT_0);
	}
	else if (IsTag(buffer, TAG_EXT_1))
	{
		ReadFixedTag(buffer, TAG_EXT_1);

		ParseWarning(WARN_FUTURE_EXPANSION_EXT_1);
	}
	else if (IsTag(buffer, TAG_EXT_2))
	{
		ReadFixedTag(buffer, TAG_EXT_2);

		ParseWarning(WARN_FUTURE_EXPANSION_EXT_2);
	}
	else
	{
		ParseError(ERR_TAG_NOT_FOUND);
	}
}

void Read_string     (P_WBXML_INFO buffer)
{
	if (Is_inline(buffer))
	{
		Read_inline(buffer);
	}
	else if (Is_tableref(buffer))
	{
		Read_tableref(buffer);
	}
	else
	{
		ParseError(ERR_TAG_NOT_FOUND);
	}
}

void Read_switchPage (P_WBXML_INFO buffer)
{
  WBXML_U_INT8 pageindex;

  ReadFixedTag(buffer, TAG_SWITCH_PAGE);
  Read_pageindex(buffer, &pageindex);

  /* Use the new codepage */
  buffer->m_curpage = pageindex;
}

void Read_inline     (P_WBXML_INFO buffer)
{
	ReadFixedTag(buffer, TAG_STR_I);
	Read_termstr(buffer);
}

void Read_tableref   (P_WBXML_INFO buffer)
{
  WBXML_MB_U_INT32 index;

  ReadFixedTag(buffer, TAG_STR_T);
  Read_index(buffer, &index);
}

void Read_entity     (P_WBXML_INFO buffer)
{
	ReadFixedTag(buffer, TAG_ENTITY);
	Read_entcode(buffer);
}

void Read_entcode    (P_WBXML_INFO buffer)
{
	WBXML_MB_U_INT32 result;
	Read_mb_u_int32(buffer, &result);
}

void Read_pi         (P_WBXML_INFO buffer)
{
  ReadFixedTag(buffer, TAG_PI);
  Read_attrStart(buffer);
  
  while (Is_attrValue(buffer))
  {
    Read_attrValue(buffer);
  }

  ReadFixedTag(buffer, TAG_END);
}

void Read_opaque     (P_WBXML_INFO buffer)
{
  WBXML_MB_U_INT32 length;
  WBXML_BYTES      data;

  ReadFixedTag(buffer, TAG_OPAQUE);
  Read_length(buffer, &length);
  Read_bytes(buffer, mb_u_int32_to_long(&length), &data);
}

void Read_version    (P_WBXML_INFO buffer)
{
  WBXML_U_INT8 result;

  Read_u_int8(buffer, &result);
}

void Read_publicid   (P_WBXML_INFO buffer)
{
  if (Is_zero(buffer))
  {
    WBXML_MB_U_INT32 index;

    Read_index(buffer, &index);

	AddDTDNode(buffer, ZERO_WBXML_MB_U_INT32, index);
  }
  else
  {
    WBXML_MB_U_INT32 result;

    Read_mb_u_int32(buffer, &result);

	AddDTDNode(buffer, result, ZERO_WBXML_MB_U_INT32);
  }
}

void Read_charset    (P_WBXML_INFO buffer)
{
  WBXML_MB_U_INT32 result;

  Read_mb_u_int32(buffer, &result);
}

void Read_termstr_rtn(P_WBXML_INFO buffer, char** result)
{

#define STRING_BLOCK_SIZE 256

	int buflen = STRING_BLOCK_SIZE;
	char* strbuf = (char*) malloc(buflen);
	BOOL doubled = FALSE;
	int i = 0;

	if (!result)
		ParseError(ERR_INTERNAL_BAD_PARAM);

	while ( (BytesLeft(buffer) >= 1) && (*(buffer->m_curpos) != 0) )
	{
		if (i>=buflen)
		{
			buflen += STRING_BLOCK_SIZE;
			strbuf = realloc(strbuf, buflen);
		}

		if (*(buffer->m_curpos) != '$' || doubled == TRUE)
		{
			strbuf[i] = *(buffer->m_curpos);
			buffer->m_curpos++;
			i++;
			if (doubled == TRUE)
				doubled = FALSE;
		}
		else
		{
			strbuf[i] = *(buffer->m_curpos);
			i++;
			doubled = TRUE;
		}
	}

	strbuf[i] = 0;
	buffer->m_curpos++;

	if (*result)
		free(*result);

	*result = strbuf;
}

void Read_termstr    (P_WBXML_INFO buffer)
{
	char* strbuf = NULL;

	Read_termstr_rtn(buffer, &strbuf);

	AddStringNode(buffer, strbuf);

	free(strbuf);
}

void Read_index      (P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result)
{
  Read_mb_u_int32(buffer, result);
}

void Read_length     (P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result)
{
  Read_mb_u_int32(buffer, result);
}

void Read_zero       (P_WBXML_INFO buffer)
{
  WBXML_U_INT8 result;

  Read_u_int8(buffer, &result);

  if (result != (WBXML_U_INT8) 0)
  {
    ParseError(ERR_TAG_NOT_FOUND);
  }
}

void Read_pageindex  (P_WBXML_INFO buffer, P_WBXML_U_INT8 result)
{
  Read_u_int8(buffer, result);
}

static void Init(P_WBXML_INFO buffer)
{
	buffer->m_start = NULL;
	buffer->m_curpos = NULL;
	buffer->m_length = 0;
	buffer->m_tree = NULL;
	buffer->m_curnode = NULL;
	buffer->m_curpage = 0;
}

static size_t BufferLength(P_WBXML_INFO buffer)
{
	size_t ret;

	while (buffer->m_curpos != '\0')
		buffer->m_curpos++;

	ret = buffer->m_curpos - buffer->m_start;
	buffer->m_curpos = buffer->m_start;
	return ret;
}

static void Free(P_WBXML_INFO buffer)
{
	if (buffer->m_start)
	{
		free(buffer->m_start);
		buffer->m_start = NULL;
	}

	buffer->m_curpos = NULL;
	buffer->m_length = 0;

	FreeNode(buffer->m_tree);
	buffer->m_tree = NULL;
}

static long FileSize(FILE* file)
{
	long curpos = ftell(file);
	long endpos;
	fseek(file, 0, SEEK_END);
	endpos = ftell(file);
	fseek(file, curpos, SEEK_SET);

	return endpos;
}

static void ReadBinary(P_WBXML_INFO buffer, FILE* file)
{
	char buf[4096];
	int m = 1;
	long n;

	if (buffer && file)
	{
		if (file != stdin)
		{
			buffer->m_length = FileSize(file);
			buffer->m_start = (P_WBXML) malloc(buffer->m_length);
			buffer->m_curpos = buffer->m_start;

			if (!buffer->m_start)
			{
				fclose(file);
				ParseError(ERR_NOT_ENOUGH_MEMORY);
			}

			if (fread(buffer->m_start, 1, buffer->m_length, file) != buffer->m_length)
			{
				fclose(file);
				ParseError(ERR_FILE_NOT_READ);
			}
			else
			{
				fclose(file);
			}
		}
		else
		{
			while ((n = fread(buf, 1, sizeof(buf), file)) > 0)
			{
				buffer->m_start = (P_WBXML) realloc(buffer->m_start, sizeof(buf) * m);
				memcpy(buffer->m_start + (sizeof(buf) * (m - 1)), buf, sizeof(buf));
				m++;
			}
			buffer->m_length = BufferLength(buffer);
			buffer->m_curpos = buffer->m_start;
		}
				
	}
	else
	{
		ParseError(ERR_INTERNAL_BAD_PARAM);
	}
}

static const char* DTDTypeName(long dtdnum)
{
	int i = 0;

	/* Search the DTD list for a match */
	while (DTDTypeList[i].m_name)
	{
		if (DTDTypeList[i].m_id == dtdnum)
		{
			break;
		}

		i++;
	}

	return DTDTypeList[i].m_name;
}

static const char* CodepageTagName(WBXML_CODEPAGE page, WBXML_TAG tag)
{
	int i = 0;

	/* Strip flags off of the tag */
	tag = (WBXML_TAG) (tag & CODEPAGE_TAG_MASK);

	/* Search the tag list for a match */
	while (CodepageTagNames[i].m_name)
	{
		if ((CodepageTagNames[i].m_dtd_id == dtd_id) &&
			(CodepageTagNames[i].m_page == page) &&
			(CodepageTagNames[i].m_tag == tag))
		{
			break;
		}

		i++;
	}

	return CodepageTagNames[i].m_name;
}

static const char* CodepageAttrstartName(WBXML_CODEPAGE page, WBXML_TAG tag, char** value)
{
	int i = 0;

	/* Check Parameters */
	if (!value)
	{
		ParseError(ERR_INTERNAL_BAD_PARAM);
	}

	/* Search the tag list for a match */
	while (CodepageAttrstartNames[i].m_name)
	{
		if ((CodepageAttrstartNames[i].m_dtd_id == dtd_id) &&
			(CodepageAttrstartNames[i].m_page == page) &&
			(CodepageAttrstartNames[i].m_tag == tag))
		{
			break;
		}

		i++;
	}

	/* Duplicate the value because it may be concatenated to */
	if (CodepageAttrstartNames[i].m_valueprefix)
	{
		*value = strdup(CodepageAttrstartNames[i].m_valueprefix);
	}
	else
	{
		*value = NULL;
	}

	/* Return the tag name */
	return CodepageAttrstartNames[i].m_name;
}

static void CodepageAttrvalueName(WBXML_CODEPAGE page, WBXML_TAG tag, char** value)
{
	int i = 0;

	/* Check Parameters */
	if (!value)
	{
		ParseError(ERR_INTERNAL_BAD_PARAM);
	}

	/* Search the tag list for a match */
	while (CodepageAttrvalueNames[i].m_name)
	{
		if ((CodepageAttrvalueNames[i].m_dtd_id == dtd_id) &&
			(CodepageAttrvalueNames[i].m_page == page) &&
			(CodepageAttrvalueNames[i].m_tag == tag))
		{
			break;
		}

		i++;
	}

	/* concatenate the value */
	if (CodepageAttrvalueNames[i].m_name)
	{
		if (*value)
		{
			*value = realloc(*value, strlen(*value) + strlen(CodepageAttrvalueNames[i].m_name) + 1);
			strcat(*value, CodepageAttrvalueNames[i].m_name);
		}
		else
		{
			*value = strdup(CodepageAttrvalueNames[i].m_name);
		}
	}
}

static const char* GetStringTableString(P_WBXML_NODE node, long index)
{
	/* Find the string table node */

	P_WBXML_NODE pStringsNode = node;

	while (pStringsNode->m_parent)
	{
		pStringsNode = pStringsNode->m_parent;
	}

	while (pStringsNode->m_next)
	{
		pStringsNode = pStringsNode->m_next;
	}

	while (pStringsNode->m_prev && pStringsNode->m_type != NODE_STRING_TABLE)
	{
		pStringsNode = pStringsNode->m_prev;
	}

	if (pStringsNode->m_type != NODE_STRING_TABLE)
	{
		return "!!NO STRING TABLE!!";
	}

	/* Find the indexed string */

	if ((index >= 0) && (index < mb_u_int32_to_long(&((P_WBXML_STRING_TABLE)pStringsNode->m_data)->m_length)))
	{
		return (const char*) &(((P_WBXML_STRING_TABLE)pStringsNode->m_data)->m_strings[index]);
	}
	else
	{
		return "!!STRING TABLE INDEX TOO LARGE!!";
	}
}

static void DumpNode(P_WBXML_NODE node, int indent, BOOL *inattrs, BOOL hascontent, char** value)
{
	P_WBXML_NODE curnode = node->m_child;

	WBXML_TAG nodetype = 0;
	long dtdnum = 0;

	BOOL bAttributesFollow = FALSE;
	BOOL bHasContent = FALSE;

	int i;

	if (!(*inattrs))
	{
		for (i=0; i<indent; i++)
		{
			printf(" ");
		}
	}
	else
	{
		if ((node->m_type != NODE_ATTRVALUE) && (*value))
		{
			printf("=\"");
			OutputEncodedString((unsigned char*) *value);
			printf("\"");
			free(*value);
			*value = NULL;
		}
	}

	switch (node->m_type)
	{
		case NODE_DTD_TYPE:
			printf("<?xml version=\"1.0\"?>\n<!DOCTYPE wml PUBLIC ");

			dtdnum = mb_u_int32_to_long( &((DTD_NODE_DATA*)node->m_data)->m_dtdnum );
			if ( dtdnum == 0)
			{
				printf("\"%s\">\n\n", GetStringTableString(node, mb_u_int32_to_long(&((DTD_NODE_DATA*)node->m_data)->m_index)) );
			}
			else
			{
				printf("\"%s\">\n\n", DTDTypeName(dtdnum) );
			}
			break;

		case NODE_CODEPAGE_TAG:
			nodetype = *((P_WBXML_TAG)node->m_data);
			if ((nodetype & CODEPAGE_TAG_MASK) == nodetype)
			{
				printf("<%s/>\n", CodepageTagName(node->m_page, nodetype));
			}
			else
			{
				if ((nodetype & CODEPAGE_TAG_HAS_CONTENT) == CODEPAGE_TAG_HAS_CONTENT)
				{
					bHasContent = TRUE;
				}

				if ((nodetype & CODEPAGE_TAG_HAS_ATTRS) == CODEPAGE_TAG_HAS_ATTRS)
				{
					printf("<%s", CodepageTagName(node->m_page, nodetype));
					bAttributesFollow = TRUE;
				}
				else
				{
					printf("<%s>\n", CodepageTagName(node->m_page, nodetype));
				}
			}
			break;

		case NODE_CODEPAGE_LITERAL_TAG:
			printf("<%s>\n", GetStringTableString(node, mb_u_int32_to_long(((P_WBXML_MB_U_INT32)node->m_data))) );
			break;

		case NODE_ATTRSTART:
			printf(" %s", CodepageAttrstartName(node->m_page, *((P_WBXML_TAG)node->m_data), value) );
			break;

		case NODE_ATTRSTART_LITERAL:
			printf(" %s", GetStringTableString(node, mb_u_int32_to_long(((P_WBXML_MB_U_INT32)node->m_data))) );
			break;

		case NODE_ATTRVALUE:
			CodepageAttrvalueName(node->m_page, *((P_WBXML_TAG)node->m_data), value);
			break;

		case NODE_ATTREND:
			if (!hascontent)
			{
				printf("/");
			}
			printf(">\n");
			*inattrs = FALSE;
			break;

		case NODE_STRING:
			if (*inattrs)
			{
				/* concatenate the value */
				if (*value)
				{
					if (node->m_data)
					{
						*value = realloc(*value, strlen(*value) + strlen((char*) node->m_data) + 1);
						strcat(*value, (char*) node->m_data);
					}
				}
				else
				{
					if (node->m_data)
					{
						*value = strdup((char*) node->m_data);
					}
				}
			}
			else
			{
				OutputEncodedString((unsigned char*) node->m_data);
				printf("\n");
			}
			break;

		case NODE_VARIABLE_STRING:
			/* TODO: output variable string */
			break;

		case NODE_VARIABLE_INDEX:
			/* TODO: output variable string */
			break;

		default:
			break;
	}

	indent += INDENT_SIZE;

	if (curnode)
	{
		while (curnode->m_next) curnode = curnode->m_next;

		while (curnode)
		{
			DumpNode(curnode, indent, &bAttributesFollow, bHasContent, value);
			curnode = curnode->m_prev;
		}
	}

	indent -= INDENT_SIZE;

	/* Output the element end if we have one */
	if ((nodetype & CODEPAGE_TAG_HAS_CONTENT) == CODEPAGE_TAG_HAS_CONTENT)
	{
		for (i=0; i<indent; i++)
		{
			printf(" ");
		}

		switch (node->m_type)
		{
			case NODE_CODEPAGE_TAG:
				printf("</%s>\n", CodepageTagName(node->m_page, *((P_WBXML_TAG)node->m_data)) );
				break;

			case NODE_CODEPAGE_LITERAL_TAG:
				printf("</%s>\n", GetStringTableString(node, mb_u_int32_to_long(((P_WBXML_MB_U_INT32)node->m_data))) );
				break;

			default:
				break;
		}
	}
}

static void DumpNodes(P_WBXML_INFO buffer)
{
	P_WBXML_NODE curnode = buffer->m_tree;
	BOOL bAttrsFollow = FALSE;
	char* value = NULL;

	if (curnode)
	{
		while (curnode->m_next) curnode = curnode->m_next;

		while (curnode)
		{
			DumpNode(curnode, 0, &bAttrsFollow, FALSE, &value);
			curnode = curnode->m_prev;
		}
	}
}

int main(int argc, char** argv)
{
	WBXML_INFO buffer;
	FILE* file;

	if (argc < 2)
	{
		file = stdin;
	}
	else
	{
	        file = fopen(argv[1], "r");
		if (!file)
		{
			ParseError(ERR_FILE_NOT_FOUND);
		}
	}

    Init(&buffer);
	ReadBinary(&buffer, file);
	Read_start(&buffer);
	DumpNodes(&buffer);
	Free(&buffer);

	return 0;
}
