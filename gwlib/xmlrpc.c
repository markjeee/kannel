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
 * gwlib/xmlrpc.c: functions to handle XML-RPC structure - building and parsing
 *
 * XML-RPC is HTTP-based XML defination to handle remote procedure calls,
 * and is defined at http://www.xml-rpc.org
 *
 * Kalle Marjola 2001 for project Kannel
 * Stipe Tolj <stolj@wapme.de>
 * Robert Gaach <robert.galach@my.tenbit.pl> 
 */
 
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/debugXML.h>
#include <libxml/encoding.h>

#include "gwlib/gwlib.h"
#include "gwlib/xmlrpc.h"

#define XR_ENABLE_EMPTY_STRING_VALUES

#define OPTIMAL_STRUCT_SIZE 7

typedef struct xmlrpc_methodresponse XMLRPCMethodResponse;
typedef struct xmlrpc_member XMLRPCMember;
typedef struct xmlrpc_methodcall XMLRPCMethodCall;
typedef struct xmlrpc_fault XMLRPCFault;

typedef struct xmlrpc_table_t xmlrpc_table_t;
typedef struct xmlrpc_2table_t xmlrpc_2table_t;


struct xmlrpc_methodcall {
    Octstr *method_name;
    List *params;         /* List of XMLRPCValues */
};

struct xmlrpc_methodresponse {
    XMLRPCValue *param;         /* Param value */
    XMLRPCFault *fault;         /* ..or this */
};

struct xmlrpc_fault {
    long f_code;                /* Fault code */
    Octstr *f_string;           /* and description */
};

struct xmlrpc_document {
    int d_type;           /* enum here */
    int parse_status;     /* enum here */
    Octstr *parse_error;  /* error string in case of parsing error */
    XMLRPCMethodCall *methodcall;
    XMLRPCMethodResponse *methodresponse;
};

struct xmlrpc_value {
    int v_type;         /* enum here */
    XMLRPCScalar *v_scalar;
    List *v_array;     /* List of XMLRPCValues */
    Dict *v_struct;    /* Dict of XMLRPCValues */
};

struct xmlrpc_member {  /* member of struct */
    Octstr *name;
    XMLRPCValue *value;
};

struct xmlrpc_scalar {
    int s_type;         /* enum here */
    Octstr *s_str;
    long s_int;
    int s_bool;
    double s_double;
    Octstr *s_date;
    Octstr *s_base64;
};

struct xmlrpc_table_t {
    char *name;
};

struct xmlrpc_2table_t {
    char *name;
    int s_type; /* enum here */
};

static xmlrpc_table_t methodcall_elements[] = {
    { "METHODNAME" },
    { "PARAMS" }
};

static xmlrpc_table_t methodresponse_elements[] = {
    { "FAULT" },
    { "PARAMS" }
};

static xmlrpc_table_t params_elements[] = {
    { "PARAM" }
};

static xmlrpc_table_t param_elements[] = {
    { "VALUE" }
};

static xmlrpc_2table_t value_elements[] = {
    { "I4", xr_int },
    { "INT", xr_int },
    { "BOOLEAN", xr_bool },
    { "STRING", xr_string },
    { "DOUBLE", xr_double },
    { "DATETIME.ISO8601", xr_date },
    { "BASE64", xr_base64 },
    { "STRUCT", xr_struct },
    { "ARRAY", xr_array }
};

static xmlrpc_table_t struct_elements[] = {
    { "MEMBER" }
};

static xmlrpc_table_t member_elements[] = {
    { "NAME" },
    { "VALUE" }
};

static xmlrpc_table_t array_elements[] = {
    { "DATA" }
};

static xmlrpc_table_t data_elements[] = {
    { "VALUE" }
};

static xmlrpc_table_t fault_elements[] = {
    { "VALUE" }
};

#define NUMBER_OF_METHODCALL_ELEMENTS \
    sizeof(methodcall_elements)/sizeof(methodcall_elements[0])
#define NUMBER_OF_METHODRESPONSE_ELEMENTS \
    sizeof(methodresponse_elements)/sizeof(methodresponse_elements[0])
#define NUMBER_OF_PARAMS_ELEMENTS \
    sizeof(params_elements)/sizeof(params_elements[0])
#define NUMBER_OF_PARAM_ELEMENTS \
    sizeof(param_elements)/sizeof(param_elements[0])
#define NUMBER_OF_VALUE_ELEMENTS \
    sizeof(value_elements)/sizeof(value_elements[0])
#define NUMBER_OF_STRUCT_ELEMENTS \
    sizeof(struct_elements)/sizeof(struct_elements[0])
#define NUMBER_OF_MEMBER_ELEMENTS \
    sizeof(member_elements)/sizeof(member_elements[0])
#define NUMBER_OF_ARRAY_ELEMENTS \
    sizeof(array_elements)/sizeof(array_elements[0])
#define NUMBER_OF_DATA_ELEMENTS \
    sizeof(data_elements)/sizeof(data_elements[0])
#define NUMBER_OF_FAULT_ELEMENTS \
    sizeof(fault_elements)/sizeof(fault_elements[0])


/* --------------------------------------
 * internal parser function declarations
 */
 
static int parse_document(xmlDocPtr document, XMLRPCDocument *xrdoc);
static int parse_methodcall(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, XMLRPCMethodCall *methodcall);
static int parse_methodcall_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, XMLRPCMethodCall *methodcall);
static int parse_methodresponse(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, XMLRPCMethodResponse *methodresponse, 
                int* n);
static int parse_methodresponse_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, XMLRPCMethodResponse *methodresponse);
static int parse_params(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc, 
                List *params);
static int parse_params_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, List *params);
static int parse_param(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc, 
                List *params, int *n);
static int parse_param_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, List *params);
static int parse_value(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc, 
                XMLRPCValue *value);
static int parse_value_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, XMLRPCValue *xrvalue);
static int parse_struct(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc, 
                Dict *members);
static int parse_struct_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, Dict *members);
static int parse_member(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc, 
                XMLRPCMember *member);
static int parse_member_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, XMLRPCMember *member);
static int parse_array(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc, 
                List *elements);
static int parse_array_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, List *elements);
static int parse_data(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc, 
                List *elements);
static int parse_data_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, List *elements);
static int parse_fault(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc, 
                XMLRPCFault *fault);
static int parse_fault_element(xmlDocPtr doc, xmlNodePtr node, 
                XMLRPCDocument *xrdoc, XMLRPCFault *fault);


/*-------------------------------------
 * MethodCall
 */

static XMLRPCMethodCall *xmlrpc_call_create(Octstr *name)
{
    XMLRPCMethodCall *nmsg = gw_malloc(sizeof(XMLRPCMethodCall));

    nmsg->method_name = octstr_duplicate(name);
    nmsg->params = gwlist_create();
    
    return nmsg;
}

static void xmlrpc_call_destroy(XMLRPCMethodCall *call)
{
    if (call == NULL)
        return;

    octstr_destroy(call->method_name);
    gwlist_destroy(call->params, xmlrpc_value_destroy_item);

    gw_free(call);
}

static Octstr *xmlrpc_call_get_name(XMLRPCMethodCall *call)
{
    return call->method_name;
}

static int xmlrpc_call_add_param(XMLRPCMethodCall *method, XMLRPCValue *value)
{
    if (method == NULL || value == NULL)
        return -1;

    gwlist_produce(method->params, value);
    return 0;
}

static Octstr *xmlrpc_call_print(XMLRPCMethodCall *call, int level)
{
    Octstr *body, *os_value;
    XMLRPCValue *val;
    long i;

    if (call == NULL || call->method_name == NULL)
        return NULL;

    body = octstr_format("%*s<methodCall>\n"
                         "%*s<methodName>%S</methodName>\n",
                         level, "", level + 2, "", call->method_name);

    gwlist_lock(call->params);
    if (gwlist_len(call->params) > 0) {
        octstr_format_append(body, "%*s<params>\n", level + 2, "");
        for (i = 0; i < gwlist_len(call->params); i++) {
            val = gwlist_get(call->params, i);
            os_value = xmlrpc_value_print(val, level + 6);

            if (os_value == NULL) {
                error(0, "XMLRPC: Could not print method call, param %ld malformed", i);
                octstr_destroy(body);
                return NULL;
            }
            octstr_format_append(body, "%*s<param>\n%S%*s</param>\n", 
                                 level + 4, "", os_value, level + 4, "");
            octstr_destroy(os_value);
        }
        octstr_format_append(body, "%*s</params>\n", level + 2, "");
    }
    gwlist_unlock(call->params);
    octstr_format_append(body, "%*s</methodCall>\n", level, "");

    return body;
}


/*-------------------------------------
 * XMLRPCFault
 */

static XMLRPCFault *xmlrpc_fault_create(long fcode, Octstr *fstring)
{
    XMLRPCFault *fault = gw_malloc(sizeof(XMLRPCFault));
    
    fault->f_code = fcode;
    fault->f_string = octstr_duplicate(fstring);
    
    return fault;
}

static void xmlrpc_fault_destroy(XMLRPCFault *fault)
{
    if (fault == NULL) return;
    
    octstr_destroy(fault->f_string);
    gw_free(fault);
}

static long xmlrpc_fault_get_code(XMLRPCFault *fault)
{
    if (fault == NULL) return -1;
    
    return fault->f_code;
}

static Octstr *xmlrpc_fault_get_string(XMLRPCFault *fault)
{
    if (fault == NULL) return NULL;
    
    return fault->f_string;
}

static Octstr *xmlrpc_fault_print(XMLRPCFault *fault, int level)
{
    Octstr *os;
    
    if (fault == NULL) return NULL;
    
    os = octstr_format("%*s<fault>\n%*s<value>\n"
                         "%*s<struct>\n"
                           "%*s<member>\n"
                             "%*s<name>faultCode</name>\n"
                             "%*s<value><int>%ld</int></value>\n"
                           "%*s</member>\n"
                           "%*s<member>\n"
                             "%*s<name>faultString</name>\n"
                             "%*s<value><string>%S</string></value>\n"
                           "%*s</member>\n"
                           "%*s</struct>\n"
                       "%*s</value>\n%*s</fault>\n",
                       level, "", level+2, "", level+4, "", level+6, "", 
                       level+8, "", level+8, "",
                       fault->f_code,
                       level+6, "", level+6, "", level+8, "", level+8, "",
                       (fault->f_string == NULL ? octstr_imm("/") : fault->f_string),
                       level+6, "", level+4, "", level+2, "", level, "");

    return os;
}


/*-------------------------------------
 * MethodResponse
 */


static XMLRPCMethodResponse *xmlrpc_response_create(void)
{
    XMLRPCMethodResponse *nmsg = gw_malloc(sizeof(XMLRPCMethodResponse));

    nmsg->param = NULL;
    nmsg->fault = NULL;
    
    return nmsg;
}

static void xmlrpc_response_destroy(XMLRPCMethodResponse *response)
{
    if (response == NULL)
        return;

    xmlrpc_value_destroy(response->param);
    xmlrpc_fault_destroy(response->fault);

    gw_free(response);
}

static int xmlrpc_response_add_param(XMLRPCMethodResponse *response, XMLRPCValue *value)
{
    if (response == NULL || value == NULL)
        return -1;

    if (response->param != NULL) {
        error(0, "XMLRPC: Method Response may contain only one param.");
        return -1;
    }
    if (response->fault != NULL) {
        error(0, "XMLRPC: Fault Response may not contain any param.");
        return -1;
    }
    
    response->param = value;
    return 0;
}

static int xmlrpc_response_is_fault(XMLRPCMethodResponse *response)
{
    if (response == NULL || response->fault == NULL)
        return 0;
    
    return 1;
}

static long xmlrpc_response_get_faultcode(XMLRPCMethodResponse *faultresponse)
{
    if (! xmlrpc_response_is_fault(faultresponse)) {
        error(0, "XMLRPC response is not fault response.");
        return -1;
    }
    
    return xmlrpc_fault_get_code(faultresponse->fault);
}

static Octstr *xmlrpc_response_get_faultstring(XMLRPCMethodResponse *faultresponse)
{
    if (! xmlrpc_response_is_fault(faultresponse)) {
        error(0, "XMLRPC response is not fault response.");
        return NULL;
    }
    
    return xmlrpc_fault_get_string(faultresponse->fault);
}


static Octstr *xmlrpc_response_print(XMLRPCMethodResponse *response, int level)
{
    Octstr *body = NULL, *os_value = NULL;

    if (response->fault == NULL && response->param != NULL) {
        os_value = xmlrpc_value_print(response->param, level + 6);

        body = octstr_format("%*s<methodResponse>\n"
                             "%*s<params>\n%*s<param>\n"
                             "%S"
                             "%*s</param>\n%*s</params>\n"
                             "%*s</methodResponse>\n",
                             level, "", level+2, "", level+4, "", os_value, 
                             level+4, "", level+2, "", level, "");
    } 
    else if (response->fault != NULL && response->param == NULL) {
        os_value = xmlrpc_fault_print(response->fault, level + 2);

        body = octstr_format("%*s<methodResponse>\n"
                             "%S"
                             "%*s</methodResponse>\n",
                             level, "", os_value, level, "");
    }
    
    octstr_destroy(os_value);
    return body;
}


/*-------------------------------------
 * Document
 */

XMLRPCDocument *xmlrpc_doc_create(void)
{
    XMLRPCDocument *xrdoc = gw_malloc(sizeof(XMLRPCDocument));
    
    xrdoc->d_type = xr_undefined;
    xrdoc->parse_status = XMLRPC_COMPILE_OK;
    xrdoc->parse_error = NULL;
    xrdoc->methodcall = NULL;
    xrdoc->methodresponse = NULL;
    
    return xrdoc;
}

XMLRPCDocument *xmlrpc_doc_create_call(Octstr *name)
{
    XMLRPCDocument *xrdoc;
    
    xrdoc = xmlrpc_doc_create();
    xrdoc->d_type = xr_methodcall;
    xrdoc->methodcall = xmlrpc_call_create(name);
    
    return xrdoc;
}

XMLRPCDocument *xmlrpc_doc_create_response(void)
{
    XMLRPCDocument *xrdoc;
    
    xrdoc = xmlrpc_doc_create();
    xrdoc->d_type = xr_methodresponse;
    xrdoc->methodresponse = xmlrpc_response_create();
    
    return xrdoc;
}

XMLRPCDocument *xmlrpc_doc_create_faultresponse(long faultcode, Octstr *faultstring)
{
    XMLRPCDocument *xrdoc;
    XMLRPCMethodResponse *response;
    
    xrdoc = xmlrpc_doc_create_response();

    response = xrdoc->methodresponse;
    response->fault = xmlrpc_fault_create(faultcode, faultstring);

    return xrdoc;
}

XMLRPCDocument *xmlrpc_doc_parse(Octstr *post_body, int d_type)
{
    XMLRPCDocument *xrdoc = xmlrpc_doc_create();
    xmlDocPtr pDoc;
    size_t size;
    char *body;

    if (post_body == NULL) {
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_create("XMLRPC: (null) XML document given.");
        return xrdoc;
    }    
    xrdoc->d_type = d_type;
    
    octstr_strip_blanks(post_body);
    octstr_shrink_blanks(post_body);
    size = octstr_len(post_body);
    body = octstr_get_cstr(post_body);

    /* parse XML document to a XML tree */
    pDoc = xmlParseMemory(body, size);
    if (!pDoc) {
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_create("XMLRPC: not valid XML document given.");
        return xrdoc;
    }
    parse_document(pDoc, xrdoc);
    xmlFreeDoc(pDoc);
    
    return xrdoc;
}

/* Destroy XMLRPCDocument object */
void xmlrpc_doc_destroy(XMLRPCDocument *xrdoc, int d_type)
{
    if (xrdoc == NULL) 
        return;
    
    if (xrdoc->d_type != d_type)
        warning(0, "Destroying document with different type then given.");
    
    xmlrpc_call_destroy(xrdoc->methodcall);
    xmlrpc_response_destroy(xrdoc->methodresponse);
    octstr_destroy(xrdoc->parse_error);
    
    gw_free(xrdoc);
}

/* Add given <value> param to XMLRPCDocument object. 
 * Return 0 if ok or -1 if something wrong (e.g. xrdoc is null or faultresponse)
 */
int xmlrpc_doc_add_value(XMLRPCDocument *xrdoc, int d_type, XMLRPCValue *value)
{
    if (xrdoc == NULL) 
        return -1;

    if (xrdoc->d_type != d_type && d_type != xr_undefined) {
        error(0, "Wrong xmlrpc document type. Param not added.");
        return -1;
    }
    if (xrdoc->d_type == xr_methodresponse) {
        if (xmlrpc_response_add_param(xrdoc->methodresponse, value) < 0) 
            return -1;
    } 
    else if (xrdoc->d_type == xr_methodcall) {
        if (xmlrpc_call_add_param(xrdoc->methodcall, value) < 0) 
            return -1;
    }
    else {
        error(0, "Unknown xmlrpc document type. Param not added.");
        return -1;
    }
    return 0;
}

/* Add a scalar param to MethodCall/MethodResponse. 
 * Return 0 if ok or -1 if something wrong (e.g. xrdoc is null or faultresponse)
 */
int xmlrpc_doc_add_scalar(XMLRPCDocument *xrdoc, int d_type, int type, void *arg)
{
    XMLRPCValue *param;
    
    param = xmlrpc_create_scalar_value(type, arg);
    if (xmlrpc_doc_add_value(xrdoc, d_type, param) < 0) {
        xmlrpc_value_destroy(param);
        return -1;
    }
    return 0;
}


/* Create Octstr (text/xml string) out of given XMLRPCDocument. Caller
 * must free returned Octstr */
Octstr *xmlrpc_doc_print(XMLRPCDocument *xrdoc, int d_type, int level)
{
    Octstr *body = NULL, *pref = NULL;

    if (xrdoc == NULL) 
        return NULL;

    if (xrdoc->d_type != d_type) {
        error(0, "Wrong xmlrpc document type.");
        return NULL;
    }
    if (xrdoc->d_type == xr_methodresponse) {
        body = xmlrpc_response_print(xrdoc->methodresponse, level);
    } 
    else if (xrdoc->d_type == xr_methodcall) {
        body = xmlrpc_call_print(xrdoc->methodcall, level);
    }
    else {
        error(0, "Unknown xmlrpc document type.");
    }

    if (body != NULL) {
        pref = octstr_format("%*s<?xml version=\"1.0\"?>\n", level, "");
        octstr_insert(body, pref, 0);
        octstr_destroy(pref);
    }
    return body;
}

/* Send XMLRPCDocument to given URL with given Headers. 
 * Return 0 if all went fine, -1 if failure. As user reference, uses *void
 */
int xmlrpc_doc_send(XMLRPCDocument *xrdoc, int d_type, HTTPCaller *http_ref,
                    Octstr *url, List *headers, void *ref)
{
    Octstr *body;
    if (http_ref == NULL || xrdoc == NULL)
        return -1;
    
    if (xrdoc->d_type != d_type) {
        error(0, "Wrong xmlrpc document type.");
        return -1;
    }
    
    if (headers == NULL)
        headers = gwlist_create();
    
    http_header_remove_all(headers, "Content-Type");
    http_header_add(headers, "Content-Type", "text/xml");

    /* 
     * XML-RPC specs say we at least need Host and User-Agent 
     * HTTP headers to be defined.
     * These are set anyway within gwlib/http.c:build_request()
     */
    body = xmlrpc_doc_print(xrdoc, d_type, 0);

    http_start_request(http_ref, HTTP_METHOD_POST, 
                       url, headers, body, 0, ref, NULL);
    
    octstr_destroy(body);
    /* XXX: should headers be destroyed here? */
    /*http_destroy_headers(headers); */
    return 0;
}


/*-------------------------------------
 * XMLRPCValue
 */


/* Create new value. Set type of it to xr_undefined, so it must be
 * set laterwards to correct one
 */
XMLRPCValue *xmlrpc_value_create(void)
{
    XMLRPCValue *val = gw_malloc(sizeof(XMLRPCValue));

    val->v_type = xr_undefined;
    val->v_scalar = NULL;
    val->v_array = NULL;
    val->v_struct = NULL;
    return val;
}

/* Destroy value with its information, recursively if need to */
void xmlrpc_value_destroy(XMLRPCValue *val)
{
    if (val == NULL)
        return;

    switch(val->v_type) {
        case xr_scalar:
            xmlrpc_scalar_destroy(val->v_scalar);
            break;
        case xr_array:
            gwlist_destroy(val->v_array, xmlrpc_value_destroy_item);
            break;
        case xr_struct:
            dict_destroy(val->v_struct);
            break;
    }
    gw_free(val);
}

/* wrapper to destroy to be used with list */
void xmlrpc_value_destroy_item(void *val)
{
    xmlrpc_value_destroy(val);
}

int xmlrpc_value_set_type(XMLRPCValue *val, int v_type)
{
    if (val == NULL)
        return -1;

    switch(v_type) {
        case xr_scalar:
        case xr_array:
        case xr_struct:
            val->v_type = v_type;
            break;
        default:
            error(0, "XMLRPC: value type not supported.");
            return -1;
    }
    
    return 0;
}

int xmlrpc_value_set_content(XMLRPCValue *val, void *content)
{
    if (val == NULL)
        return -1;

    switch(val->v_type) {
        case xr_scalar:
            val->v_scalar = (XMLRPCScalar *)content;
            break;
        case xr_array:
            val->v_array  = (List *)content;
            break;
        case xr_struct:
            val->v_struct = (Dict *)content;
            break;
        default:
            error(0, "XMLRPC: value type not supported.");
            return -1;
    }
    
    return 0;
}

int xmlrpc_value_get_type(XMLRPCValue *val)
{
    if (val == NULL)
        return -1;
        
    return val->v_type;
}

int xmlrpc_value_get_type_smart(XMLRPCValue *val)
{
    int type = xmlrpc_value_get_type(val);
    if (type == xr_scalar) 
        return xmlrpc_get_scalar_value_type(val);

    return type;
}

void *xmlrpc_value_get_content(XMLRPCValue *val)
{
    if (val == NULL)
        return NULL;
        
    switch(val->v_type) {
        case xr_scalar:
            return val->v_scalar;
        case xr_array:
            return val->v_array;
        case xr_struct:
            return val->v_struct;
        default:
            error(0, "XMLRPC: value type not supported.");
            return NULL;
    }
}

Octstr *xmlrpc_value_print(XMLRPCValue *val, int level)
{
    Octstr *body = NULL, *os = NULL;

    if (val == NULL)
        return NULL;
    
    switch(val->v_type) {
        case xr_scalar:
            os = xmlrpc_scalar_print(val->v_scalar, level+2);
           break;
        case xr_struct:
            os = xmlrpc_print_struct(val->v_struct, level+2);
            break;
        case xr_array:
            os = xmlrpc_print_array(val->v_array, level+2);
            break;
        default:
            return NULL;
    }

    if (os != NULL) {
        body = octstr_format("%*s<value>\n%S%*s</value>\n",
                             level, "", os, level, "");
        octstr_destroy(os);
    }
    
    return body;
}


/*-------------------------------------
 * XMLRPCScalar
 */


/* Create new scalar of given type with given argument */
XMLRPCScalar *xmlrpc_scalar_create(int type, void *arg)
{
    XMLRPCScalar *scalar = gw_malloc(sizeof(XMLRPCScalar));

    scalar->s_type = type;
    scalar->s_int = 0;
    scalar->s_bool = 0;
    scalar->s_double = 0.0;
    scalar->s_str = NULL;
    scalar->s_date = NULL;
    scalar->s_base64 = NULL;
    
    if (arg == NULL) {
#ifdef XR_ENABLE_EMPTY_STRING_VALUES
        if (scalar->s_type != xr_string) {
#endif
            error(0,"XML-RPC: scalar value may not be null!");
            xmlrpc_scalar_destroy(scalar);
            return NULL;
#ifdef XR_ENABLE_EMPTY_STRING_VALUES
        }
#endif
    }
    switch (type) {
        case xr_int:
            if (arg != NULL) 
                scalar->s_int = *(long*)arg;
            break;
        case xr_bool:
            if (arg != NULL) 
                scalar->s_bool = *(int*)arg;
            break;
        case xr_double:
            if (arg != NULL) 
                scalar->s_double = *(double*)arg;
            break;
        case xr_string:
            scalar->s_str = octstr_duplicate((Octstr *)arg);
            break;
        case xr_date:
            scalar->s_date = octstr_duplicate((Octstr *)arg);
            break;
        case xr_base64:
            scalar->s_base64 = octstr_duplicate((Octstr *)arg);
            break;
        default:
            error(0,"XML-RPC: scalar type not supported!");
            xmlrpc_scalar_destroy(scalar);
            return NULL;
    }
    return scalar;
}


/* Destroy scalar */
void xmlrpc_scalar_destroy(XMLRPCScalar *scalar)
{
    if (scalar == NULL)
        return;

    octstr_destroy(scalar->s_str);
    octstr_destroy(scalar->s_date);
    octstr_destroy(scalar->s_base64);
    
    gw_free(scalar);
}

int xmlrpc_scalar_get_type(XMLRPCScalar *scalar)
{
    if (scalar == NULL)
        return -1;
    return scalar->s_type;
}

void *xmlrpc_scalar_get_content(XMLRPCScalar *scalar, int s_type)
{
    if (scalar == NULL)
        return NULL;
    if (scalar->s_type != s_type) {
        error(0, "XMLRPC: Scalar content request with bogus type");
        return NULL;
    }
    switch (scalar->s_type) {
        case xr_int:     return &(scalar->s_int);
        case xr_bool:    return &(scalar->s_bool);
        case xr_double:  return &(scalar->s_double);
        case xr_string:  return scalar->s_str;
        case xr_date:    return scalar->s_date;
        case xr_base64:  return scalar->s_base64;
        default:
            error(0,"XML-RPC: scalar type not supported!");
            return NULL;
    }
}

Octstr *xmlrpc_scalar_print(XMLRPCScalar *scalar, int level)
{
    Octstr *os = NULL;
    
    if (scalar == NULL)
        return NULL;

    switch (scalar->s_type) {
        case xr_int:
            os = octstr_format("%*s<int>%ld</int>\n", 
                               level, "", scalar->s_int);
            break;
        case xr_bool:
            os = octstr_format("%*s<bool>%d</bool>\n", 
                               level, "", scalar->s_bool);
            break;
        case xr_double:
            os = octstr_format("%*s<double>%d</double>\n", 
                                 level, "", scalar->s_double);
            break;
        case xr_string:
            if (scalar->s_str == NULL) {
#ifdef XR_ENABLE_EMPTY_STRING_VALUES
                os = octstr_format("%*s<string></string>\n", 
                                   level, "");
#endif
            } else {
                Octstr *tmp = octstr_duplicate(scalar->s_str);
                octstr_convert_to_html_entities(tmp);
                os = octstr_format("%*s<string>%S</string>\n", 
                                   level, "", tmp);
                octstr_destroy(tmp);
            }
            break;
        case xr_date:
            os = octstr_format("%*s<datetime.iso8601>%S</datetime.iso8601>\n", 
                               level, "", scalar->s_date);
            break;
        case xr_base64:
            os = octstr_format("%*s<base64>%S</base64>\n", 
                               level, "", scalar->s_base64);
            break;
    }
    return os;    
}


/*-------------------------------------
 * XMLRPCMember - internal functions
 */

/* Create new member with undefined name and value */
static XMLRPCMember *xmlrpc_member_create(void)
{
    XMLRPCMember *member = gw_malloc(sizeof(XMLRPCMember));

    member->name = NULL;
    member->value = NULL;

    return member;
}

/* Destroy member and if destroy_value != 0 destroy its content */
static void xmlrpc_member_destroy(XMLRPCMember *member, int destroy_value)
{
    if (member == NULL)
        return;

    octstr_destroy(member->name);
    if (destroy_value == 1)
        xmlrpc_value_destroy(member->value);

    gw_free(member);
}



/*-------------------------------------------------
 * Utilities to make things easier
 */

Octstr *xmlrpc_get_call_name(XMLRPCDocument *call)
{
    if (call == NULL || call->methodcall == NULL)
        return NULL;
    return xmlrpc_call_get_name(call->methodcall);
}

/*** PARAMS HANDLING ***/
int xmlrpc_count_params(XMLRPCDocument *xrdoc)
{
    if (xrdoc == NULL)
        return -1;
    if (xrdoc->d_type == xr_methodcall && xrdoc->methodcall != NULL)
        return gwlist_len(xrdoc->methodcall->params);
    else if (xrdoc->d_type == xr_methodresponse && xrdoc->methodresponse != NULL)
        return (xrdoc->methodresponse->param != NULL ? 1 : 0);
    
    return -1;
}

XMLRPCValue *xmlrpc_get_param(XMLRPCDocument *xrdoc, int i)
{
    if (xrdoc == NULL)
        return NULL;
    if (xrdoc->d_type == xr_methodcall && xrdoc->methodcall != NULL) 
        return gwlist_len(xrdoc->methodcall->params) > i ? gwlist_get(xrdoc->methodcall->params, i) : NULL;
    else if (xrdoc->d_type == xr_methodresponse && xrdoc->methodresponse != NULL
             && i == 0)
        return xrdoc->methodresponse->param;
    
    return NULL;
}

int xmlrpc_get_type_param(XMLRPCDocument *xrdoc, int i)
{
    XMLRPCValue *param = xmlrpc_get_param(xrdoc, i);
    
    return xmlrpc_value_get_type(param);
}

void *xmlrpc_get_content_param(XMLRPCDocument *xrdoc, int i)
{
    XMLRPCValue *param = xmlrpc_get_param(xrdoc, i);
    
    return xmlrpc_value_get_content(param);
}

/*** STRUCT VALUE HANDLING ***/
XMLRPCValue *xmlrpc_create_struct_value(int count_members)
{
    XMLRPCValue *value = xmlrpc_value_create();
    int len = (count_members > 0 ? count_members : OPTIMAL_STRUCT_SIZE);
    value->v_type = xr_struct;
    value->v_struct = dict_create(len, xmlrpc_value_destroy_item);

    return value;
}

long xmlrpc_count_members(XMLRPCValue *xrstruct)
{
    if (xrstruct == NULL || xrstruct->v_type != xr_struct)
        return -1;
    return dict_key_count(xrstruct->v_struct);
}

int xmlrpc_add_member(XMLRPCValue *xrstruct, Octstr *name, XMLRPCValue *value)
{
    if (xrstruct == NULL || xrstruct->v_type != xr_struct 
        || name == NULL || value == NULL)
        return -1;
    
    return dict_put_once(xrstruct->v_struct, name, value);
}

int xmlrpc_add_member_scalar(XMLRPCValue *xrstruct, Octstr *name, int type, void *arg)
{
    XMLRPCValue *value = xmlrpc_create_scalar_value(type, arg);
    int status;
    
    status = xmlrpc_add_member(xrstruct, name, value);
    if (status < 0)
        xmlrpc_value_destroy(value);
        
    return status;
}

XMLRPCValue *xmlrpc_get_member(XMLRPCValue *xrstruct, Octstr *name)
{
    if (xrstruct == NULL || xrstruct->v_type != xr_struct || name == NULL)
        return NULL;
    
    return dict_get(xrstruct->v_struct, name);
}

int xmlrpc_get_member_type(XMLRPCValue *xrstruct, Octstr *name)
{
    XMLRPCValue *value = xmlrpc_get_member(xrstruct, name);
    
    return xmlrpc_value_get_type(value);
}

void *xmlrpc_get_member_content(XMLRPCValue *xrstruct, Octstr *name)
{
    XMLRPCValue *value = xmlrpc_get_member(xrstruct, name);
    
    return xmlrpc_value_get_content(value);
}

Octstr *xmlrpc_print_struct(Dict *v_struct,  int level)
{
    Octstr *body, *os_val, *key;
    List *keys;
    XMLRPCValue *member_val;

    if (v_struct == NULL || dict_key_count(v_struct) == 0)
        return NULL;
    
    keys = dict_keys(v_struct);
    body = octstr_format("%*s<struct>\n", level, "");

    while ((key = gwlist_consume(keys)) != NULL) {
        member_val = dict_get(v_struct, key);
        os_val = xmlrpc_value_print(member_val, level+4);
        if (os_val == NULL) {
            gwlist_destroy(keys, octstr_destroy_item);
            octstr_destroy(key);
            octstr_destroy(body);
            return NULL;
        }
        octstr_format_append(body, "%*s<member>\n"
                                     "%*s<name>%S</name>\n%S"
                                   "%*s</member>\n",
                                   level+2, "", level+4, "",
                                   key, os_val,
                                   level+2, "");
        octstr_destroy(key);
        octstr_destroy(os_val);
    }
    gwlist_destroy(keys, octstr_destroy_item);
    octstr_format_append(body, "%*s</struct>\n", level, "");
    
    return body;
}

/*** ARRAY VALUE HANDLING ***/
XMLRPCValue *xmlrpc_create_array_value(void)
{
    XMLRPCValue *value = xmlrpc_value_create();
    value->v_type = xr_array;
    value->v_array = gwlist_create();

    return value;
}

int xmlrpc_count_elements(XMLRPCValue *xrarray)
{
    if (xrarray == NULL || xrarray->v_type != xr_array)
        return -1;
    
    return  gwlist_len(xrarray->v_array);
}

int xmlrpc_add_element(XMLRPCValue *xrarray, XMLRPCValue *value)
{
    if (xrarray == NULL || xrarray->v_type != xr_array || value == NULL)
        return -1;
    
    gwlist_produce(xrarray->v_array, value);
    return 1;
}

int xmlrpc_add_element_scalar(XMLRPCValue *xrarray, int type, void *arg)
{
    XMLRPCValue *value = xmlrpc_create_scalar_value(type, arg);
    int status;
    
    status = xmlrpc_add_element(xrarray, value);
    if (status < 0)
        xmlrpc_value_destroy(value);
        
    return status;
}

XMLRPCValue *xmlrpc_get_element(XMLRPCValue *xrarray, int i)
{
    if (xrarray == NULL || xrarray->v_type != xr_array || i < 0)
        return NULL;
    
    return gwlist_get(xrarray->v_array, i);
}

int xmlrpc_get_element_type(XMLRPCValue *xrarray, int i)
{
    XMLRPCValue *value = xmlrpc_get_element(xrarray, i);
    
    return xmlrpc_value_get_type(value);
}

void *xmlrpc_get_element_content(XMLRPCValue *xrarray, int i)
{
    XMLRPCValue *value = xmlrpc_get_element(xrarray, i);
    
    return xmlrpc_value_get_content(value);
}

Octstr *xmlrpc_print_array(List *v_array,  int level)
{
    Octstr *body, *os_element;
    XMLRPCValue *element = NULL;
    int i;
    
    if (v_array == NULL)
        return NULL;
    
    body = octstr_format("%*s<array>\n%*s<data>\n", level, "", level+2, "");

    for(i = 0; i < gwlist_len(v_array); i++) {
        element = gwlist_get(v_array, i);
        os_element = xmlrpc_value_print(element, level+4);
        if (os_element == NULL) {
            octstr_destroy(body);
            return NULL;
        }
        
        octstr_append(body, os_element);
        octstr_destroy(os_element);
    }
    octstr_format_append(body, "%*s</data>\n%*s</array>\n", 
                         level+2, "", level, "");
    
    return body;
}


/*** SCALAR VALUE HANDLING ***/
XMLRPCValue *xmlrpc_create_scalar_value(int type, void *arg)
{
    XMLRPCValue *value = xmlrpc_value_create();
    value->v_type = xr_scalar;
    value->v_scalar = xmlrpc_scalar_create(type, arg);

    return value;
}

XMLRPCValue *xmlrpc_create_double_value(double val)
{
    return xmlrpc_create_scalar_value(xr_double, &val);
}

XMLRPCValue *xmlrpc_create_int_value(long val)
{
    return xmlrpc_create_scalar_value(xr_int, &val);
}

XMLRPCValue *xmlrpc_create_string_value(Octstr *val)
{
    return xmlrpc_create_scalar_value(xr_string, val);
}


/*** FAULT HANDLING ***/
int xmlrpc_is_fault(XMLRPCDocument *response)
{
    if (response == NULL || response->d_type != xr_methodresponse)
        return 0;
    
    return xmlrpc_response_is_fault(response->methodresponse);
}

long xmlrpc_get_faultcode(XMLRPCDocument *faultresponse)
{
    if (! xmlrpc_is_fault(faultresponse)) {
        error(0, "XMLRPC object is not fault response.");
        return -1;
    }
    
    return xmlrpc_response_get_faultcode(faultresponse->methodresponse);
}

Octstr *xmlrpc_get_faultstring(XMLRPCDocument *faultresponse)
{
    if (! xmlrpc_is_fault(faultresponse)) {
        error(0, "XMLRPC object is not fault response.");
        return NULL;
    }
    
    return xmlrpc_response_get_faultstring(faultresponse->methodresponse);
}


/*** PARSE STATUS HANDLING***/

int xmlrpc_parse_status(XMLRPCDocument *xrdoc)
{
    if (xrdoc == NULL)
        return -1;

    return xrdoc->parse_status;
}

Octstr *xmlrpc_parse_error(XMLRPCDocument *xrdoc) 
{
    if (xrdoc == NULL)
        return NULL;
    
    return octstr_duplicate(xrdoc->parse_error);
}


/*-------------------------------------------------
 * Internal parser functions
 */

static int parse_document(xmlDocPtr document, XMLRPCDocument *xrdoc)
{
    xmlNodePtr node;
    Octstr *name;
    int n = 0, status = 0;
    
    node = xmlDocGetRootElement(document);

    /*
     * check if this is at least a valid root element
     */
    if (node == NULL || node->name == NULL) {
        error(0, "XMLRPC: XML document - not valid root node!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }
    
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }
    if ((xrdoc->d_type == xr_methodcall || xrdoc->d_type == xr_undefined)
        && octstr_case_compare(name, octstr_imm("METHODCALL")) == 0) {
        
        xrdoc->d_type = xr_methodcall;
        xrdoc->methodcall = xmlrpc_call_create(NULL);
        octstr_destroy(name);
        
        status = parse_methodcall(document, node->xmlChildrenNode, xrdoc, xrdoc->methodcall);
        if (status < 0) {
            xmlrpc_call_destroy(xrdoc->methodcall);
            xrdoc->methodcall = NULL;
            if (xrdoc->parse_status == XMLRPC_COMPILE_OK) {
                xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
                xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
            }
        } 
        else if ((xrdoc->methodcall->method_name) == NULL) {
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: <methodName> tag expected!");
            status = -1;
        }
        return status;
    } else if ((xrdoc->d_type == xr_methodresponse || xrdoc->d_type == xr_undefined)
               && octstr_case_compare(name, octstr_imm("METHODRESPONSE")) == 0) {
        
        xrdoc->d_type = xr_methodresponse;
        xrdoc->methodresponse = xmlrpc_response_create();
        octstr_destroy(name);
        
        status = parse_methodresponse(document, node->xmlChildrenNode, 
                                      xrdoc, xrdoc->methodresponse, &n);
        if (status < 0) {
            xmlrpc_response_destroy(xrdoc->methodresponse);
            xrdoc->methodresponse = NULL;
        }
        return status;
    } else {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: wrong root element <%s>, "
                                           "<%s> expected!", 
                                           octstr_get_cstr(name), 
                                           (xrdoc->d_type == xr_methodcall ? 
                                               "methodCall" : "methodResponse"));
        octstr_destroy(name);
        return -1;
    }
}

static int parse_methodcall(xmlDocPtr doc, xmlNodePtr node, XMLRPCDocument *xrdoc,
                            XMLRPCMethodCall *methodcall)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            
            status = parse_methodcall_element(doc, node, xrdoc, methodcall);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown XML node "
                                               "in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
        return parse_methodcall(doc, node->next, xrdoc, methodcall);
        
    return status;
}

static int parse_methodcall_element(xmlDocPtr doc, xmlNodePtr node, 
                                    XMLRPCDocument *xrdoc, XMLRPCMethodCall *methodcall)
{
    Octstr *name;
    xmlChar *content_buff;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML methodcall element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_METHODCALL_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(methodcall_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_METHODCALL_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' in XML source "
                                           "at level <methodCall>", octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   methodName [0]
     *   params     [1]
     */
    if (i == 0) {
        /* this has been the <methodName> tag */
        if (methodcall->method_name == NULL) {
            /*only one <methodName> tag allowed*/
            content_buff = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (content_buff != NULL) {
                methodcall->method_name = octstr_create(content_buff);
                xmlFree(content_buff);
            } else {
                xrdoc->parse_status = XMLRPC_PARSING_FAILED;
                xrdoc->parse_error = octstr_format("XML-RPC compiler: empty tag <methodName> in XML source "
                                           "at level <methodCall>");
                return -1;
            }
        } else {
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: duplicated tag <methodName> in XML source "
                                               "at level <methodCall>");
            octstr_destroy(name);
            return -1;
        }
    } else {
        /* 
         * ok, this has to be an <params> tag, otherwise we would 
         * have returned previosly
         */
        return parse_params(doc, node->xmlChildrenNode, xrdoc, methodcall->params);
    }
    return 0;
} 

static int parse_methodresponse(xmlDocPtr doc, xmlNodePtr node, 
                            XMLRPCDocument *xrdoc, XMLRPCMethodResponse *methodresponse, int* n)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            if (*n > 0) {
                xrdoc->parse_status = XMLRPC_PARSING_FAILED;
                xrdoc->parse_error = octstr_format("XML-RPC compiler: unexpected XML node <%s> "
                                                   "in the XML-RPC source.", node->name);
                return -1;
            }
            status = parse_methodresponse_element(doc, node, xrdoc, methodresponse);
            (*n)++;
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown XML node "
                                               "in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL) {
        if (parse_methodresponse(doc, node->next, xrdoc, methodresponse, n) == -1) {
            return -1;
        }
    }

    return status;
}

static int parse_methodresponse_element(xmlDocPtr doc, xmlNodePtr node, 
                                    XMLRPCDocument *xrdoc, XMLRPCMethodResponse *methodresponse)
{
    Octstr *name;
    size_t i;
    int status;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML methodResponse element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_METHODRESPONSE_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(methodresponse_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_METHODRESPONSE_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' in XML source "
                                           "at level <methodResponse>", octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   fault  [0]
     *   params [1]
     */
    if (i == 0) {
        /* this has been the <fault> tag */
        methodresponse->fault = xmlrpc_fault_create(0, NULL);
        return parse_fault(doc, node->xmlChildrenNode, xrdoc, methodresponse->fault);
    } else {
        /* 
         * ok, this has to be an <params> tag, otherwise we would 
         * have returned previosly
         */
        List *params = gwlist_create();;
        status = parse_params(doc, node->xmlChildrenNode, xrdoc, params);
        if (status < 0) return -1;
        if (gwlist_len(params) != 1) {
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler:wrong number of params "
                                               "at level <methodResponse>");
            gwlist_destroy(params, xmlrpc_value_destroy_item);
            return -1;
        }
        methodresponse->param = gwlist_consume(params);
        gwlist_destroy(params, xmlrpc_value_destroy_item);
        return status;
    }

}

static int parse_params(xmlDocPtr doc, xmlNodePtr node, 
                        XMLRPCDocument *xrdoc, List *params)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_params_element(doc, node, xrdoc, params);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown XML node in XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
           if (parse_params(doc, node->next, xrdoc, params) == -1)
            return -1;

    return status;
}

static int parse_params_element(xmlDocPtr doc, xmlNodePtr node, 
                                XMLRPCDocument *xrdoc, List *params)
{
    Octstr *name;
    size_t i;
    int n = 0;

    /*
     * check if the element is allowed at this level
     * within <params> we only have one or more <param>
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML params element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }
    
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_PARAMS_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(params_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_PARAMS_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                           "in XML source at level <params>", 
                                           octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   param [0]
     */
    if (i == 0) {
        /* this has been a <param> tag */
        if (parse_param(doc, node->xmlChildrenNode, xrdoc, params, &n) == -1)
            return -1;
    } else {
        /* we should never be here */
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_params!");
        return -1;
    }
    return 0;
}

static int parse_param(xmlDocPtr doc, xmlNodePtr node, 
                       XMLRPCDocument *xrdoc, List *params, int *n)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:

            /* a <param> can only have one value element type */
            if ((*n) > 0) {
                xrdoc->parse_status = XMLRPC_PARSING_FAILED;
                xrdoc->parse_error = octstr_format("XML-RPC compiler: param may only have one value!");
                return -1;
            }

            status = parse_param_element(doc, node, xrdoc, params);
            (*n)++;
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }
   
    if (node->next != NULL)
        if (parse_param(doc, node->next, xrdoc, params, n) == -1)
            return -1;

    return status;
}

static int parse_param_element(xmlDocPtr doc, xmlNodePtr node, 
                               XMLRPCDocument *xrdoc, List *params)
{
    Octstr *name;
    size_t i;
    XMLRPCValue *value;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML param element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_PARAM_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(param_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_PARAM_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                           "in XML source at level <param>", 
                                           octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   value [0]
     */
    if (i == 0) {
        /* this has been a <param> tag */
        value = xmlrpc_value_create();
        if (parse_value(doc, node->xmlChildrenNode, xrdoc, value) == -1) {
            xmlrpc_value_destroy(value);
            return -1;
        }
        gwlist_append(params, value);
    } else {
        /* we should never be here */
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_param!");
        return -1;
    }
    return 0;
}

static int parse_value(xmlDocPtr doc, xmlNodePtr node, 
                       XMLRPCDocument *xrdoc, XMLRPCValue *value)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_value_element(doc, node, xrdoc, value);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
        if (parse_value(doc, node->next, xrdoc, value) == -1)
            return -1;

    return status;
}

static int parse_value_element(xmlDocPtr doc, xmlNodePtr node, 
                               XMLRPCDocument *xrdoc, XMLRPCValue *xrvalue)
{
    Octstr *name;
    Octstr *value = NULL;
    xmlChar *content_buff;
    long lval = 0;
    double dval = 0.0;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML value element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_VALUE_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(value_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_VALUE_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                           "in XML source at level <value>", 
                                           octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);


    content_buff = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (content_buff != NULL) {
        value = octstr_create(content_buff);
        xmlFree(content_buff);
    }

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   i4, int
     *   boolean
     *   string
     *   double
     *   dateTime.iso8601
     *   base64
     *   struct
     *   array
     */
    switch (value_elements[i].s_type) {
        /*
         * scalar types
         */
        case xr_int:   case xr_bool:
        case xr_double:
        case xr_date:  case xr_base64:
#ifndef XR_ENABLE_EMPTY_STRING_VALUES
        case xr_string:
#endif
            if (value == NULL) {
                xrdoc->parse_status = XMLRPC_PARSING_FAILED;
                xrdoc->parse_error = octstr_format("XML-RPC compiler: no value for '%s'", 
                                           node->name);
                return -1;
            }
            break;
    }

    switch (value_elements[i].s_type) {
        
        /*
         * scalar types
         */
        case xr_int:
            if (value != NULL && octstr_parse_long(&lval, value, 0, 10) < 0) {
                xrdoc->parse_status = XMLRPC_PARSING_FAILED;
                xrdoc->parse_error = octstr_format("XML-RPC compiler: could not parse int value '%s'", 
                                                   octstr_get_cstr(value));
                octstr_destroy(value);
                return -1;
            }
            xrvalue->v_type = xr_scalar;
            xrvalue->v_scalar = xmlrpc_scalar_create(xr_int, (void *) &lval);
            break;
        
        case xr_bool:
            if (value != NULL && octstr_parse_long(&lval, value, 0, 10) < 0) {
                xrdoc->parse_status = XMLRPC_PARSING_FAILED;
                xrdoc->parse_error = octstr_format("XML-RPC compiler: could not parse boolean value '%s'", 
                                                   octstr_get_cstr(value));
                octstr_destroy(value);
                return -1;
            }
            xrvalue->v_type = xr_scalar;
            xrvalue->v_scalar = xmlrpc_scalar_create(xr_bool, (void *) &lval);
            break;

        case xr_double:
            if (value != NULL && octstr_parse_double(&dval, value, 0) < 0) {
                xrdoc->parse_status = XMLRPC_PARSING_FAILED;
                xrdoc->parse_error = octstr_format("XML-RPC compiler: could not parse double value '%s'", 
                                                   octstr_get_cstr(value));
                octstr_destroy(value);
                return -1;
            }
            xrvalue->v_type = xr_scalar;
            xrvalue->v_scalar = xmlrpc_scalar_create(xr_double, (void *) &dval);
            break;

        case xr_string:
            xrvalue->v_type = xr_scalar;
            xrvalue->v_scalar = xmlrpc_scalar_create(xr_string, (void *) value);
            break;

        case xr_date:
            xrvalue->v_type = xr_scalar;
            xrvalue->v_scalar = xmlrpc_scalar_create(xr_date, (void *) value);
            break;

        case xr_base64:
            xrvalue->v_type = xr_scalar;
            xrvalue->v_scalar = xmlrpc_scalar_create(xr_base64, (void *) value);
            break;

        case xr_struct:
            xrvalue->v_type = xr_struct;
            xrvalue->v_struct = dict_create(OPTIMAL_STRUCT_SIZE, xmlrpc_value_destroy_item);

            if (parse_struct(doc, node->xmlChildrenNode, xrdoc, xrvalue->v_struct) == -1) {
                octstr_destroy(value);
                return -1;
            }
            break;

        case xr_array:
            xrvalue->v_type = xr_array;
            xrvalue->v_array = gwlist_create();

            if (parse_array(doc, node->xmlChildrenNode, xrdoc, xrvalue->v_array) == -1) {
                xrdoc->parse_status = XMLRPC_PARSING_FAILED; 
                xrdoc->parse_error = octstr_format("XML-RPC compiler: could not parse array"); 
                octstr_destroy(value);
                return -1;
            }
            break;

        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_value!");
            return -1;
    }

    octstr_destroy(value);
    return 0;
}

static int parse_struct(xmlDocPtr doc, xmlNodePtr node, 
                        XMLRPCDocument *xrdoc, Dict *members)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_struct_element(doc, node, xrdoc, members);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
           if (parse_struct(doc, node->next, xrdoc, members) == -1)
            return -1;

    return status;
}

static int parse_struct_element(xmlDocPtr doc, xmlNodePtr node, 
                                XMLRPCDocument *xrdoc, Dict *members)
{
    Octstr *name;
    size_t i;
    XMLRPCMember *member;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML struct element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }
    
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_STRUCT_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(struct_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_STRUCT_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                           "in XML source at level <struct>", 
                                           octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   member [0]
     */
    if (i == 0) {
        /* this has been a <member> tag */
        member = xmlrpc_member_create();
        if (parse_member(doc, node->xmlChildrenNode, xrdoc, member) == -1) {
            xmlrpc_member_destroy(member, 1);
            return -1;
        }
        if (! dict_put_once(members, member->name, member->value)) {
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: at least two members have same name.");
            xmlrpc_member_destroy(member, 1);
            return -1;
        }
        xmlrpc_member_destroy(member, 0);
    } else {
        /* we should never be here */
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_struct!");
        return -1;
    }
    return 0;
}

static int parse_member(xmlDocPtr doc, xmlNodePtr node, 
                        XMLRPCDocument *xrdoc, XMLRPCMember *member)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_member_element(doc, node, xrdoc, member);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
           if (parse_member(doc, node->next, xrdoc, member) == -1)
            return -1;

    return status;
}

static int parse_member_element(xmlDocPtr doc, xmlNodePtr node, 
                                XMLRPCDocument *xrdoc, XMLRPCMember *member)
{
    Octstr *name;
    xmlChar *content_buff;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML member element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_MEMBER_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(member_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_MEMBER_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                           "in XML source at level <member>", 
                                           octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   name [0]
     *   value [1]
     */
    if (i == 0) {
        /* this has been a <name> tag */
        if (member->name != NULL) {
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: duplicated tag '<name>' "
                                               "in XML source at level <member>");
            return -1;
        }
        content_buff = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        if (content_buff != NULL) {
            member->name = octstr_create(content_buff);
            xmlFree(content_buff);
        } else {
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: empty tag <name> in XML source "
                                          "at level <member>");
            return -1;
        }
    } else {
        member->value = xmlrpc_value_create();
        if (parse_value(doc, node->xmlChildrenNode, xrdoc, member->value) == -1) {
            xmlrpc_value_destroy(member->value);
            member->value = NULL;
            return -1;    
        }
    }
    return 0;
}

static int parse_array(xmlDocPtr doc, xmlNodePtr node, 
                        XMLRPCDocument *xrdoc, List *elements)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_array_element(doc, node, xrdoc, elements);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
           if (parse_array(doc, node->next, xrdoc, elements) == -1)
            return -1;

    return status;
}

static int parse_array_element(xmlDocPtr doc, xmlNodePtr node, 
                                XMLRPCDocument *xrdoc, List *elements)
{
    Octstr *name;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML array element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_ARRAY_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(array_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_ARRAY_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                           "in XML source at level <array>", 
                                           octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   data [0]
     */
    if (i == 0) {
        /* this has been a <data> tag */
        if (parse_data(doc, node->xmlChildrenNode, xrdoc, elements) == -1)
            return -1;
            
    } else {
        /* we should never be here */
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_array!");
        return -1;
    }
    return 0;
}

static int parse_data(xmlDocPtr doc, xmlNodePtr node, 
                      XMLRPCDocument *xrdoc, List *elements)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_data_element(doc, node, xrdoc, elements);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
           if (parse_data(doc, node->next, xrdoc, elements) == -1)
            return -1;

    return status;
}

static int parse_data_element(xmlDocPtr doc, xmlNodePtr node, 
                              XMLRPCDocument *xrdoc, List *elements)
{
    Octstr *name;
    XMLRPCValue *value;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML data element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_DATA_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(data_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_DATA_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                           "in XML source at level <data>", 
                                           octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *  value [0]
     */
    if (i == 0) {
        /* this has been a <value> tag */
        value = xmlrpc_value_create();
        if (parse_value(doc, node->xmlChildrenNode, xrdoc, value) == -1) {
            xmlrpc_value_destroy(value);
            return -1;
        }
        gwlist_append(elements, value);
            
    } else {
        /* we should never be here */
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_array!");
        return -1;
    }
    return 0;
}

static int parse_fault(xmlDocPtr doc, xmlNodePtr node, 
                       XMLRPCDocument *xrdoc, XMLRPCFault *fault)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_fault_element(doc, node, xrdoc, fault);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
           /*
            * XML has also many other node types, these are not needed with 
            * XML-RPC. Therefore they are assumed to be an error.
            */
        default:
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
           if (parse_fault(doc, node->next, xrdoc, fault) == -1)
            return -1;

    return status;
}

static int parse_fault_element(xmlDocPtr doc, xmlNodePtr node, 
                               XMLRPCDocument *xrdoc, XMLRPCFault *fault)
{
    Octstr *name; 
    XMLRPCValue *value, *v_code, *v_string;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    if (node->name == NULL) {
        error(0, "XMLRPC: XML fault element nodes without name!");
        xrdoc->parse_status = XMLRPC_XMLPARSE_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: bad XML");
        return -1;
    }

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_FAULT_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(fault_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_FAULT_ELEMENTS) {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                           "in XML source at level <fault>", 
                                           octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   value [0]
     */
    if (i == 0) {
        /* this has been a <value> tag */
        value = xmlrpc_value_create();
        if (parse_value(doc, node->xmlChildrenNode, xrdoc, value) == -1) {
            xmlrpc_value_destroy(value);
            return -1;    
        }
        /* must be :
         *     <struct>
         *         <member>
         *             <name>faultCode</name>
         *             <value><int> ... </int></value>
         *         </member>
         *         <member>
         *             <name>faultString</name>
         *             <value><string> ... </string></value>
         *         </member>
         *     </struct> 
         */
        if (xmlrpc_value_get_type(value) != xr_struct ||
            (v_code = xmlrpc_get_member(value, octstr_imm("faultCode"))) == NULL ||
            xmlrpc_value_get_type_smart(v_code) != xr_int ||
            (v_string = xmlrpc_get_member(value, octstr_imm("faultString"))) == NULL ||
            xmlrpc_value_get_type_smart(v_string) != xr_string ||
            xmlrpc_count_members(value) != 2) {
            
            xrdoc->parse_status = XMLRPC_PARSING_FAILED;
            xrdoc->parse_error = octstr_format("XML-RPC compiler: bogus value "
                                               "in XML source at level <fault>");
            xmlrpc_value_destroy(value);
            return -1;
        }
        
        fault->f_code = xmlrpc_scalar_get_int((XMLRPCScalar *) xmlrpc_value_get_content(v_code));
        fault->f_string = xmlrpc_scalar_get_string((XMLRPCScalar *) xmlrpc_value_get_content(v_string));
        
        xmlrpc_value_destroy(value);
    } else {
        xrdoc->parse_status = XMLRPC_PARSING_FAILED;
        xrdoc->parse_error = octstr_format("XML-RPC compiler: duplicated tag '<name>' "
                                           "in XML source at level <member>");
        return -1;
    }
    return 0;
}
