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
 * wml_definitions.h - definitions unique to WML compiler
 *
 * This file contains fefinitions for global tokens and structures containing 
 * element and attribute tokens for the code page 1.
 *
 *
 * Tuomas Luttinen for Wapit Ltd.
 */

/***********************************************************************
 * Declarations of global variables. 
 */

/*
 * Elements as defined by tag code page 0.
 */

static
wml_table_t wml_elements[] = {
  { "wml", 0x3F },
  { "card", 0x27 },
  { "do", 0x28 },
  { "onevent", 0x33 },
  { "head", 0x2C },
  { "template", 0x3B },
  { "access", 0x23 },
  { "meta", 0x30 },
  { "go", 0x2B },
  { "prev", 0x32 },
  { "refresh", 0x36 },
  { "noop", 0x31 },
  { "postfield", 0x21 },
  { "setvar", 0x3E },
  { "select", 0x37 },
  { "optgroup", 0x34 },
  { "option", 0x35 },
  { "input", 0x2F },
  { "fieldset", 0x2A },
  { "timer", 0x3C },
  { "img", 0x2E },
  { "anchor", 0x22 },
  { "a", 0x1C },
  { "table", 0x1F },
  { "tr", 0x1E },
  { "td", 0x1D },
  { "em", 0x29 },
  { "strong", 0x39 },
  { "b", 0x24 },
  { "i", 0x2D },
  { "u", 0x3D },
  { "big", 0x25 },
  { "small", 0x38 },
  { "p", 0x20 },
  { "br", 0x26 },
  { NULL }
};


/*
 * Attributes as defined by WAP-191-WML-20000219a 
 * section 14.3.3 Attribute Start Tokens
 */

static
wml_table3_t wml_attributes[] = {
  { "accept-charset", NULL, 0x05 },
  { "accesskey", NULL, 0x5E },
  { "align", NULL, 0x52 },
  { "align", "bottom", 0x06 },
  { "align", "center", 0x07 },
  { "align", "left", 0x08 },
  { "align", "middle", 0x09 },
  { "align", "right", 0x0A },
  { "align", "top", 0x0B },
  { "alt", NULL, 0x0C },
  { "cache-control", "no-cache", 0x64 },
  { "class", NULL, 0x54 },
  { "columns", NULL, 0x53 },
  { "content", NULL, 0x0D },
  { "content", "application/vnd.wap.wmlc;charset=", 0x5C },
  { "domain", NULL, 0x0F },
  { "emptyok", "false", 0x10 },
  { "emptyok", "true", 0x11 },
  { "enctype", NULL, 0x5F },
  { "enctype", "application/x-www-form-urlencoded", 0x60 },
  { "enctype", "multipart/form-data", 0x61 },
  { "format", NULL, 0x12 },
  { "forua", "false", 0x56 },
  { "forua", "true", 0x57 },
  { "height", NULL, 0x13 },
  { "href", NULL, 0x4A },
  { "href", "http://", 0x4B },
  { "href", "https://", 0x4C },
  { "hspace", NULL, 0x14 },
  { "http-equiv", NULL, 0x5A },
  { "http-equiv", "Content-Type", 0x5B },
  { "http-equiv", "Expires", 0x5D },
  { "id", NULL, 0x55 },
  { "ivalue", NULL, 0x15 },
  { "iname", NULL, 0x16 },
  { "label", NULL, 0x18 },
  { "localsrc", NULL, 0x19 },
  { "maxlength", NULL, 0x1A },
  { "method", "get", 0x1B },
  { "method", "post", 0x1C },
  { "mode", "nowrap", 0x1D },
  { "mode", "wrap", 0x1E },
  { "multiple", "false", 0x1F },
  { "multiple", "true", 0x20 },
  { "name", NULL, 0x21 },
  { "newcontext", "false", 0x22 },
  { "newcontext", "true", 0x23 },
  { "onenterbackward", NULL, 0x25 },
  { "onenterforward", NULL, 0x26 },
  { "onpick", NULL, 0x24 },
  { "ontimer", NULL, 0x27 },
  { "optional", "false", 0x28 },
  { "optional", "true", 0x29 },
  { "path", NULL, 0x2A },
  { "scheme", NULL, 0x2E },
  { "sendreferer", "false", 0x2F },
  { "sendreferer", "true", 0x30 },
  { "size", NULL, 0x31 },
  { "src", NULL, 0x32 },
  { "src", "http://", 0x58 },
  { "src", "https://", 0x59 },
  { "ordered", "true", 0x33 },
  { "ordered", "false", 0x34 },
  { "tabindex", NULL, 0x35 },
  { "title", NULL, 0x36 },
  { "type", NULL, 0x37 },
  { "type", "accept", 0x38 },
  { "type", "delete", 0x39 },
  { "type", "help", 0x3A },
  { "type", "password", 0x3B },
  { "type", "onpick", 0x3C },
  { "type", "onenterbackward", 0x3D },
  { "type", "onenterforward", 0x3E },
  { "type", "ontimer", 0x3F },
  { "type", "options", 0x45 },
  { "type", "prev", 0x46 },
  { "type", "reset", 0x47 },
  { "type", "text", 0x48 },
  { "type", "vnd.", 0x49 },
  { "value", NULL, 0x4D },
  { "vspace", NULL, 0x4E },
  { "width", NULL, 0x4F },
  { "xml:lang", NULL, 0x50 },
  { "xml:space", "preserve", 0x62 },
  { "xml:space", "default", 0x63 },
  { NULL }
};


/*
 * Attribute value codes.
 */

static
wml_table_t wml_attribute_values[] = {
  { "accept", 0x89 },
  { "bottom", 0x8A },
  { "clear", 0x8B },
  { "delete", 0x8C },
  { "help", 0x8D },
  { "middle", 0x93 },
  { "nowrap", 0x94 },
  { "onenterbackward", 0x96 },
  { "onenterforward", 0x97 },
  { "onpick", 0x95 },
  { "ontimer", 0x98 },
  { "options", 0x99 },
  { "password", 0x9A },
  { "reset", 0x9B },
  { "text", 0x9D },
  { "top", 0x9E },
  { "unknown", 0x9F },
  { "wrap", 0xA0 },
  { NULL }
};

/*
 * URL value codes.
 */

static
wml_table_t wml_URL_values[] = {
  { "www.", 0xA1 },
  { ".com/", 0x85 },
  { ".edu/", 0x86 },
  { ".net/", 0x87 },
  { ".org/", 0x88 },
  { "http://", 0x8E },
  { "http://www.", 0x8F },
  { "https://", 0x90 },
  { "https://www.", 0x91 },
  { "Www.", 0xA1 },
  { NULL }
};















