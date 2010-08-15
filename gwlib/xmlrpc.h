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
 * xmlrpc.h - XML-RPC functions
 *
 * Functions to handle XML-RPC structure - building and parsing
 *
 * XML-RPC is HTTP-based XML defination to handle remote procedure calls,
 * and is defined in http://www.xml-rpc.org
 *
 * The current implementation is not yet ready (it does not, for example,
 *  do any parsing nor building the tree), and is not used for any real use,
 * yet, but probably future interfaces might be able to use this, too
 *
 *
 * Kalle Marjola 2001 for project Kannel
 * Robert Ga³ach <robert.galach@my.tenbit.pl>
 */

#ifndef __XMLRPC_H
#define __XMLRPC_H

#include "gwlib/gwlib.h"

/*
 * types and structures defined by www.xml-rpc.com
 */
typedef struct xmlrpc_document XMLRPCDocument;
typedef struct xmlrpc_value XMLRPCValue;
typedef struct xmlrpc_scalar XMLRPCScalar;

enum { 
    xr_undefined, xr_scalar, xr_array, xr_struct, 
    xr_string, xr_int, xr_bool, xr_double, xr_date, xr_base64, 
    xr_methodcall, xr_methodresponse 
};

/*
 * status codes while parsing
 */
enum {
    XMLRPC_COMPILE_OK,
    XMLRPC_XMLPARSE_FAILED,
    XMLRPC_PARSING_FAILED
};



/*** DOCUMENTS ***/

/* Create new XMLRPCDocument object of undefined_type  */
XMLRPCDocument *xmlrpc_doc_create(void);
/* Create new MethodCall with given name */
XMLRPCDocument *xmlrpc_doc_create_call(Octstr *name);
/* Create new MethodResponse */
XMLRPCDocument *xmlrpc_doc_create_response(void);
/* Create new fault MethodResponse with given code and fault string */
XMLRPCDocument *xmlrpc_doc_create_faultresponse(long faultcode, Octstr *faultstring);

/* Create new XMLRPCDocument object from given body of text/xml, 
 * d_type is expected document type: xr_methodcall, xr_methodresponse
   or xr_undefined if any
 */
XMLRPCDocument *xmlrpc_doc_parse(Octstr *post_body, int d_type);

/* Destroy XMLRPCDocument object */
void xmlrpc_doc_destroy(XMLRPCDocument *xrdoc, int d_type);

/* Add a scalar param to XMLRPCDocument object.
 * d_type is expected document type: xr_methodcall or xr_methodresponse.
 * Return 0 if ok or -1 if something wrong (e.g. xrdoc is null or faultresponse)
 */
int xmlrpc_doc_add_scalar(XMLRPCDocument *xrdoc, int d_type, int type, void *arg);

/* Add given XMLRPCValue param to XMLRPCDocument object.
 * d_type is expected document type: xr_methodcall or xr_methodresponse.
 * Return 0 if ok or -1 if something wrong.
 * NOTE that value is NOT duplicated 
 */
int xmlrpc_doc_add_value(XMLRPCDocument *xrdoc, int d_type, XMLRPCValue *value);

/* Create Octstr (text/xml string) out of given XMLRPCDocument. 
 * d_type is expected document type. 
 * level is the indent width.
 * Caller must free returned Octstr.
 */
Octstr *xmlrpc_doc_print(XMLRPCDocument *xrdoc, int d_type, int level);

/* Send XMLRPCDocument to given url with given headers. 
 * d_type is expected document type. 
 * Note: adds XML-RPC specified headers into given list if needed. 
 *       and if NULL when this function called, automatically generated
 *
 * Return 0 if all went fine, -1 if failure. As user reference, uses *void
 */
int xmlrpc_doc_send(XMLRPCDocument *xrdoc, int d_type, HTTPCaller *http_ref,
		    Octstr *url, List *headers, void *ref);


/*** METHOD CALLS ***/

/* Create new MethodCall with given name and no params */
#define xmlrpc_create_call(method) \
            xmlrpc_doc_create_call(method)

/* Create new MethodCall from given body of text/xml */
#define xmlrpc_parse_call(post_body) \
            xmlrpc_doc_parse(post_body, xr_methodcall)

/* Destroy MethodCall */
#define xmlrpc_destroy_call(call) \
            xmlrpc_doc_destroy(call, xr_methodcall)

/* Add a scalar param to MethodCall. 
 * type is scalar type: xr_string, xr_int, xr_bool, xr_double, xr_date or xr_base64
 * arg is pointer to value of given type: Octstr*, long*, int*, double*, Octstr* or Octstr*
 * respectively
 * Return 0 if ok or -1 if something wrong.
 */
#define xmlrpc_add_call_scalar(call, type, arg) \
            xmlrpc_doc_add_scalar(call, xr_methodcall, type, arg)

/* Add given XMLRPCValue param to MethodCall.
 * Return 0 if ok or -1 if something wrong.
 * NOTE: value is NOT duplicated 
 */
#define xmlrpc_add_call_value(call, value) \
            xmlrpc_doc_add_value(call, xr_methodcall, value)

/* Create Octstr (text/xml string) out of given MethodCall. Caller
 * must free returned Octstr 
 */
#define xmlrpc_print_call(call) \
            xmlrpc_doc_print(call, xr_methodcall, 0)

/* Send MethodCall to given url with given headers. 
 * d_type is expected document type. 
 * Note: adds XML-RPC specified headers into given list if needed. 
 *       and if NULL when this function called, automatically generated
 *
 * Return 0 if all went fine, -1 if failure. As user reference, uses *void
 */
#define xmlrpc_send_call(call,http_ref, url, headers, ref) \
            xmlrpc_doc_send(call, xr_methodcall, http_ref, url, headers, ref)

/* Return the name of the method requested or NULL if document is not method call */
Octstr *xmlrpc_get_call_name(XMLRPCDocument *call);



/*** METHOD RESPONSES ***/

/* Create a new MethodResponse with no param value */
#define xmlrpc_create_response() \
            xmlrpc_doc_create_response()

/* Create a new fault MethodResponse with given faultcode and faultstring */
#define xmlrpc_create_faultresponse(faultcode, faultstring) \
            xmlrpc_doc_create_faultresponse(faultcode, faultstring)

/* Create a new MethodResponse from given text/xml string */
#define xmlrpc_parse_response(post_body) \
            xmlrpc_doc_parse(post_body, xr_methodresponse)

/* Destroy MethodResponse */
#define xmlrpc_destroy_response(response) \
            xmlrpc_doc_destroy(response, xr_methodresponse)

/* Add a scalar param to MethodResponse. 
 * type is scalar type: xr_string, xr_int, xr_bool, xr_double, xr_date or xr_base64
 * arg is pointer to value of given type: Octstr*, long*, int*, double*, Octstr* or Octstr*
 * respectively
 * Return 0 if ok or -1 if something wrong.
 */
#define xmlrpc_add_response_scalar(response, type, arg) \
            xmlrpc_doc_add_scalar(response, xr_methodresponse, type, arg)

/* Add given XMLRPCValue param to MethodResponse.
 * Return 0 if ok or -1 if something wrong.
 * NOTE: value is NOT duplicated 
 */
#define xmlrpc_add_response_value(response, value) \
            xmlrpc_doc_add_value(response, xr_methodresponse, value)

/* Create Octstr (text/xml string) out of given MethodCall. Caller
 * must free returned Octstr 
 */
#define xmlrpc_print_response(response) \
            xmlrpc_doc_print(response, xr_methodresponse, 0)

/* Send MethodResponse to given url with given headers. 
 * d_type is expected document type. 
 * Note: adds XML-RPC specified headers into given list if needed. 
 *       and if NULL when this function called, automatically generated
 *
 * Return 0 if all went fine, -1 if failure. As user reference, uses *void
 */
#define xmlrpc_send_response(response, http_ref, url, headers, ref) \
            xmlrpc_doc_send(call, xr_methodresponse, http_ref, url, headers, ref)



/*** PARAMS HANDLING ***/

/* Return -1 if XMLRPCDocument can't have params or number of params */
int xmlrpc_count_params(XMLRPCDocument *xrdoc);

/* Return i'th MethodCall/MethodResponse param 
 * or NULL if something wrong
 */
XMLRPCValue *xmlrpc_get_param(XMLRPCDocument *xrdoc, int i);

/* Return type of i'th MethodCall/MethodResponse param: xr_scalar, xr_array or xr_struct
 * or -1 if no param
 */
int xmlrpc_get_type_param(XMLRPCDocument *xrdoc, int i);

/* Return content of i'th MethodCall/MethodResponse param: 
 * XMLRPCScalar if xr_scalar, List of XMLRPCValues if xr_array 
 * or Dict of XMLRPCValues if xr_struct (member names as keys)
 * or NULL if no param
 */
void *xmlrpc_get_content_param(XMLRPCDocument *xrdoc, int i);

/* Identify d_type of given XMLRPCDocument and add a scalar param. 
 * type is scalar type: xr_string, xr_int, xr_bool, xr_double, xr_date or xr_base64
 * arg is pointer to value of given type: Octstr*, long*, int*, double*, Octstr* or Octstr*
 * respectively
 * Return 0 if ok or -1 if something wrong.
 */
#define xmlrpc_add_scalar_param(xrdoc, type, arg) \
            xmlrpc_doc_add_scalar(xrdoc, xr_undefined, type, arg)

/* Identify d_type of given XMLRPCDocument and add XMLRPCValue param.
 * Return 0 if ok or -1 if something wrong.
 * NOTE: value is NOT duplicated 
 */
#define xmlrpc_add_param(xrdoc, value) \
            xmlrpc_doc_add_value(xrdoc, xr_undefined, value)



/*** VALUES HANDLING ***/

/* Create a new XMLRPCValue object of undefined type */ 
XMLRPCValue *xmlrpc_value_create(void);

/* Destroy given XMLRPCValue object */
void xmlrpc_value_destroy(XMLRPCValue *val);

/* Wrapper for destroy */
void xmlrpc_value_destroy_item(void *val);

/* Set type of XMLRPCValue: xr_scalar, xr_array or xr_struct 
 * Return 0 if ok or -1 if something wrong.
 */
int xmlrpc_value_set_type(XMLRPCValue *val, int v_type);

/* Set XMLRPCValue content:
 * XMLRPCScalar if xr_scalar, List of XMLRPCValues if xr_array 
 * or Dict of XMLRPCValues if xr_struct (member names as keys)
 * Return 0 if ok or -1 if something wrong.
 */
int xmlrpc_value_set_content(XMLRPCValue *val, void *content);

/* Return type of XMLRPCValue: xr_scalar, xr_array or xr_struct */
int xmlrpc_value_get_type(XMLRPCValue *val);

/* Return leaf type of XMLRPCValue: 
 * as above, but if value is xr_scalar return type of scalar
 */
int xmlrpc_value_get_type_smart(XMLRPCValue *val);

/* Return XMLRPCValue content:
 * XMLRPCScalar if xr_scalar, List of XMLRPCValues if xr_array 
 * or Dict of XMLRPCValues if xr_struct (member names as keys)
 * or NULL if something wrong.
 */
void *xmlrpc_value_get_content(XMLRPCValue *val);

/* Create Octstr (text/xml string) out of given XMLRPCValue. Caller
 * must free returned Octstr 
 */
Octstr *xmlrpc_value_print(XMLRPCValue *val, int level);


/*** STRUCT VALUE HANDLING ***/

/* Create a new XMLRPCValue object of xr_struct type. 
 * size is expected number of struct members
 */ 
XMLRPCValue *xmlrpc_create_struct_value(int size);

/* Return -1 if not a struct or number of members */
long xmlrpc_count_members(XMLRPCValue *xrstruct);

/* Add member with given name and value to the struct */
int xmlrpc_add_member(XMLRPCValue *xrstruct, Octstr *name, XMLRPCValue *value);

/* Add member with given name and scalar value built with type and arg to the struct */
int xmlrpc_add_member_scalar(XMLRPCValue *xrstruct, Octstr *name, int type, void *arg);

/* Return value of member with given name or NULL if not found */
XMLRPCValue *xmlrpc_get_member(XMLRPCValue *xrstruct, Octstr *name);

/* Return type of member with given name (xr_scalar, xr_array or xr_struct)
 * or -1 if not found 
 */
int xmlrpc_get_member_type(XMLRPCValue *xrstruct, Octstr *name);

/* Return content of member with given name:
 * XMLRPCScalar if xr_scalar, List of XMLRPCValues if xr_array 
 * or Dict of XMLRPCValues if xr_struct (member names as keys)
 * or NULL if not found.
 */
void *xmlrpc_get_member_content(XMLRPCValue *xrstruct, Octstr *name);


/* Create Octstr (text/xml string) out of struct. Caller
 * must free returned Octstr.
 */
Octstr *xmlrpc_print_struct(Dict *members,  int level);


/*** ARRAY VALUE HANDLING ***/

/* Create a new XMLRPCValue object of xr_array type. */ 
XMLRPCValue *xmlrpc_create_array_value(void);

/* Return -1 if not an array or number of elements */
int xmlrpc_count_elements(XMLRPCValue *xrarray);

/* Add XMLRPCValue element to the end of array */
int xmlrpc_add_element(XMLRPCValue *xrarray, XMLRPCValue *value);

/* Build scalar XMLRPCValue with type and arg, 
 *and add this element to the end of array 
 */
int xmlrpc_add_element_scalar(XMLRPCValue *xrarray, int type, void *arg);

/* Return value of i'th element in array or NULL if something wrong*/
XMLRPCValue *xmlrpc_get_element(XMLRPCValue *xrarray, int i);

/* Return type of i'th element in array (xr_scalar, xr_array or xr_struct)
 * or -1 if not found 
 */
int xmlrpc_get_element_type(XMLRPCValue *xrarray, int i);

/* Return content of i'th element:
 * XMLRPCScalar if xr_scalar, List of XMLRPCValues if xr_array 
 * or Dict of XMLRPCValues if xr_struct (member names as keys)
 * or NULL if not found.
 */
void *xmlrpc_get_element_content(XMLRPCValue *xrarray, int i);

/* Create Octstr (text/xml string) out of array. Caller
 * must free returned Octstr.
 */
Octstr *xmlrpc_print_array(List *elements,  int level);


/*** SCALAR HANDLING ***/

/* Create a new scalar of given type and value
 * (which must be in right format) 
 * type is scalar type: xr_string, xr_int, xr_bool, xr_double, xr_date or xr_base64
 * arg is pointer to value of given type: Octstr*, long*, int*, double*, Octstr* or Octstr*
 * respectively
 * Return NULL if something wrong.
 */
XMLRPCScalar *xmlrpc_scalar_create(int type, void *arg);

/* Destroy XMLRPCScalar */
void xmlrpc_scalar_destroy(XMLRPCScalar *scalar);

/* Return type of scalar or -1 if scalar is NULL */
int xmlrpc_scalar_get_type(XMLRPCScalar *scalar);

/* Return content of scalar 
 * s_type is expected type of scalar
 */
void *xmlrpc_scalar_get_content(XMLRPCScalar *scalar, int s_type);

/* Create Octstr (text/xml string) out of scalar. Caller
 * must free returned Octstr.
 */
Octstr *xmlrpc_scalar_print(XMLRPCScalar *scalar, int level);

/* Wrappers to get scalar content of proper type 
 * NOTE: returned values are copies, caller must free returned Octstr
 */
#define xmlrpc_scalar_get_double(scalar) \
            *(double *)xmlrpc_scalar_get_content(scalar, xr_double)

#define xmlrpc_scalar_get_int(scalar) \
            *(long *)xmlrpc_scalar_get_content(scalar, xr_int)

#define xmlrpc_scalar_get_bool(scalar) \
            *(int *)xmlrpc_scalar_get_content(scalar, xr_bool)

#define xmlrpc_scalar_get_date(scalar) \
            octstr_duplicate((Octstr *)xmlrpc_scalar_get_content(scalar, xr_date))

#define xmlrpc_scalar_get_string(scalar) \
            octstr_duplicate((Octstr *)xmlrpc_scalar_get_content(scalar, xr_string))

#define xmlrpc_scalar_get_base64(scalar) \
            octstr_duplicate((Octstr *)xmlrpc_scalar_get_content(scalar, xr_base64))


/*** SCALAR VALUE HANDLING ***/

/* Create XMLRPCScalar with type and arg, 
 * and then create XMLRPCValue with xr_scalar type and 
 * created XMLRPCScalar as content
 */
XMLRPCValue *xmlrpc_create_scalar_value(int type, void *arg);

/* As above, but scalar is xr_double type */
XMLRPCValue *xmlrpc_create_double_value(double val);

/* As above, but scalar is xr_int type */
XMLRPCValue *xmlrpc_create_int_value(long val);

/* As above, but scalar is xr_string type */
XMLRPCValue *xmlrpc_create_string_value(Octstr *val);

/* Return type of scalar in given XMLRPCValue */
#define xmlrpc_get_scalar_value_type(value) \
            xmlrpc_scalar_get_type(xmlrpc_value_get_content(value))
            
/* Wrappers to get scalar content of proper type from XMLRPCValue */
#define xmlrpc_get_double_value(value) \
            xmlrpc_scalar_get_double(xmlrpc_value_get_content(value))
#define xmlrpc_get_int_value(value) \
            xmlrpc_scalar_get_int(xmlrpc_value_get_content(value))
#define xmlrpc_get_string_value(value) \
            xmlrpc_scalar_get_string(xmlrpc_value_get_content(value))
#define xmlrpc_get_base64_value(value) \
            xmlrpc_scalar_get_base64(xmlrpc_value_get_content(value))


/*** FAULT HANDLING ***/

/* Return 1 if XMLRPCDocument is fault MethodResponse */
int xmlrpc_is_fault(XMLRPCDocument *response);

/* Return faultcode from fault MethodResponse 
 * or -1 if XMLRPCDocument is not valid fault MethodResponse 
 */
long xmlrpc_get_faultcode(XMLRPCDocument *faultresponse);

/* Return faultstring from fault MethodResponse 
 * or NULL if XMLRPCDocument is not valid fault MethodResponse 
 */
Octstr *xmlrpc_get_faultstring(XMLRPCDocument *faultresponse);



/*** PARSE STATUS HANDLING***/

/* 
 * Check if parsing had any errors, return status code of parsing by
 * returning one of the following: 
 *   XMLRPC_COMPILE_OK,
 *   XMLRPC_XMLPARSE_FAILED,
 *   XMLRPC_PARSING_FAILED
 *   -1 if call has been NULL
 */
int xmlrpc_parse_status(XMLRPCDocument *xrdoc);

/* Return parser error string if parse_status != XMLRPC_COMPILE_OK */
/* return NULL if no error occured or no error string was available */
Octstr *xmlrpc_parse_error(XMLRPCDocument *xrdoc);

#endif
