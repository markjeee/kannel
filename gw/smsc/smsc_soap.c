/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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
 * smsc_soap.c - Implementation of SOAP (XML over HTTP) as a Kannel module
 *
 * Oded Arbel,     m-Wise inc (oded@m-wise.com)
 * Dima Milentiev, m-Wise inc (dima@m-wise.com)
 *
 * ChangeLog:
 *
 * 20/02/2002: started - copied smsc_mam.c for starting
 * 25/02/2002: implemented MT sending
 * 09/05/2002: fixed problem crash when HTTP connection fails.
 *             send message back to bearerbox on HTTP failure instead of local queue
 * 19/05/2002: stripped leading + from international numbers
 * 20/05/2002: fixed previous change
 *             changed Transaction Id returned to support 64 bit integers
 * 27/05/2002: changed DLR creation to store the transaction ID instead of timestamp
 *              added parsing of human readable time in DLR
 * 28/05/2002: added multi thread sending support
 * 02/06/2002: changed validity computing to accept minutes instead of seconds
 * 04/06/2002: Changed callbacks to take into account that they might be called while the connection
 *              is dead.

 * 04/06/2002: Started to implement generic parsing engine.
 * 09/06/2002: Removed hardcoded XML generation and parsing
 * 22/07/2002: Removed wrong assignment of charset_convert return code to msg->sms.coding
 * 30/07/2002: fixed wrong format for year in soap_write_date
 *               additional debug and process for invalid charset conversion
 * 04/08/2002: forced chraset_conversion to/from UCS-2 to use big endianity
 *             added curly bracing support to XML data tokens
 * 04/09/2002: Added some debugging info
 * 05/09/2002: Changed dlr_add and http_start_request calls to support current CVS
 * 26/09/2002: Added soap_fetch_xml_data
 * 29/09/2002: Changed Ack/Nack to process case when Ack not return msg ID, move declaration to fix worning
 * 01/10/2002: started to change MO general
 * 07/10/2002: MT generalization
 * 
 * TODOs:
 *	- add a configuration option to the max number of messages a client can send, and use
 *	  and implement KeepAlive in the clients.
 *	- support XML generation through DTD
 *	- support XML parsing through DTD
 *
 *
 * Usage: add the following to kannel.conf:
 *
 *   group = smsc
 *   smsc = soap
 *   send-url = <URI>		- URI to send SOAP bubbles at (mandatory)
 *   receive-port = <number>	- port number to bind our server on (Default: disabled - MT only)
 *   xml-files = "MT.xml;MO.xml;DLR.xml" - XML templates for generation of MT messages 
 *                                         and MO and DLR responses
 *   xmlspec-files = "MT.spec;MO.spec;DLR.spec" - XML path spec files for parsing of MT ¥
 *                                                response and MO and DLR submission
 *   alt-charset = "character map" - charset in which a text message is received (default UTF-8)
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

#include "gwlib/gwlib.h"
#include "gwlib/http.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "msg.h"
#include "sms.h"
#include "dlr.h"

/* libxml include */
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

/* Defines and defaults */
#define SOAP_SLEEP_TIME				0.01
#define SOAP_MAX_MESSAGE_PER_ROUND 		1
#define SOAP_DEFAULT_SENDER_STRING		"Kannel"
#define SOAP_DEFAULT_VALIDITY           60

/* URIs for MOs and delivery reports */
#define SOAP_MO_URI	        "/mo"
#define SOAP_DLR_URI		"/dlr"

/* default reponses to HTTP queries */
#define SOAP_DEFAULT_MESSAGE				"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Error>No method by that name</Error>"
#define SOAP_ERROR_NO_DLR_MESSAGE			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Error>Sorry - no DLR for that MT</Error>"
#define SOAP_ERROR_DLR_MESSAGE				"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Error>Fatal error while trying to parse delivery report</Error>"
#define SOAP_ERROR_MO_MESSAGE				"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Error>Fatal error while trying to parse incoming MO</Error>"
#define SOAP_ERROR_NO_DATA_MESSAGE			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Error>No data received</Error>"
#define SOAP_ERROR_MALFORMED_DATA_MESSAGE	        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Error>Malformed data received</Error>"

/* map HTTP status codes to SOAP HTTP reply codes */
#define SOAP_ERROR_NO_DLR_CODE				HTTP_BAD_METHOD
#define SOAP_DEFAULT_CODE				HTTP_NOT_FOUND
#define SOAP_ERROR_DLR_CODE				HTTP_INTERNAL_SERVER_ERROR
#define SOAP_ERROR_MO_CODE				HTTP_INTERNAL_SERVER_ERROR
#define SOAP_ERROR_NO_DATA_CODE				HTTP_NOT_IMPLEMENTED
#define SOAP_ERROR_MALFORMED_DATA_CODE	                HTTP_BAD_GATEWAY
#define SOAP_QUERY_OK					HTTP_OK

/* compile time configuration defines */
#undef HUMAN_TIME

#define MIN_SOAP_CLIENTS            5
#define MAX_SOAP_CLIENTS            50
#define CLIENT_BUSY_TIME            5
#define CLIENT_TEARDOWN_TIME        600
#define CLIENT_BUSY_LOAD            5

#define SPEC_DEFAULT				"default"

/* private data store for the SOAP module */
typedef struct privdata {
    List *outgoing_queue;	/* queue to hold unsent messages */

    long listener_thread;	/* SOAP HTTP client and module managment */
    long server_thread; 	/* SOAP HTTP server */

    int shutdown;		/* Internal signal to shut down */
    int soap_server;	/* internal signal to shut down the server */

    long port;    	        /* listener port */
    int ssl;		/* flag whether to use SSL for the server */

    Octstr *uri;		/* URI to send MTs on */

    Octstr *allow_ip, *deny_ip; /* connection allowed mask */

    List* soap_client;  /* list to hold callers */

    Octstr* name;		/* connection name for use in private functions that want to do logging */

    /* SOAP configurtion */
    Octstr* form_variable; /* variable name used in post */
    int form_urlencoded; /* whether to send the data urlencoded or multipart */
    Octstr* alt_charset;   /* alt-charset to use */

    Octstr* mt_xml_file;
    Octstr* mt_spec_file;
    Octstr* mo_xml_file;
    Octstr* mo_spec_file;
    Octstr* dlr_xml_file;
    Octstr* dlr_spec_file;
    Octstr* mo_deps_file;
} PrivData;

/* struct to hold one HTTP client connection (I hope) */
typedef struct client_data {
    time_t last_access;
    unsigned long requests;
    HTTPCaller* caller;
} ClientData;

/* struct useful for the XML mapping routines */
typedef struct argument_map {
    Octstr* name;
    Octstr* path;
    Octstr* attribute;
    Octstr* sscan_type;
    void* store;
} ArgumentMap;

/* useful macros go here (some of these were ripped of other modules,
   so maybe its better to put them in a shared file) */
#define	O_DESTROY(a)	{ if(a) octstr_destroy(a); a=NULL; }

/*
 * SOAP module public API towards bearerbox
 */

/* module entry point - will also be defined in smscconn_p.h */
int smsc_soap_create(SMSCConn *conn, CfgGroup *cfg);
/* callback for bearerbox to add messages to our queue */
static int soap_add_msg_cb(SMSCConn *conn, Msg *sms);
/* callback for bearerbox to signal a shutdown */
static int soap_shutdown_cb(SMSCConn *conn, int finish_sending);
/* callback for bearerbox to signal us to start the connection */
static void soap_start_cb(SMSCConn *conn);
/* callback for bearerbox to signal us to drop the connection */
static void soap_stop_cb(SMSCConn *conn);
/* callback for bearerbox to query on the number of messages in our queue */
static long soap_queued_cb(SMSCConn *conn);
 
/*
 * SOAP module thread functions (created by smsc_soap_create())
 */

/* SOAP module thread for launching HTTP clients. */
static void soap_listener(void *arg);
/* SOAP HTTP server thread for incoming MO */
static void soap_server(void *arg);
 
/*
 * SOAP module internal protocol implementation functions
 */

/* start the loop to send all messages in the queue */
static void soap_send_loop(SMSCConn *conn);
/* function used to send a single MT message */
static void soap_send(PrivData *privdata, Octstr *xmlbuffer, Msg *msgid);
/* called to retrieve HTTP responses from the HTTP library */
static void soap_read_response(SMSCConn *conn);
/* format a messages structure as an XML buffer */
static Octstr *soap_format_xml(Octstr *xml_file, Msg *msg, PrivData *privdata);
/* parse a response from the SOAP server to get the message ID */

static long long soap_parse_response(PrivData *privdata, Octstr *xmlResponse);
/* parse an incoming MO xml */
static long soap_parse_mo(SMSCConn *conn, Octstr *request, Octstr **response);
/* parse an incoming derlivery report */
static long soap_parse_dlr(SMSCConn *conn, Octstr *request, Octstr **response);

/*
 * SOAP internal utility functions
 */
/* parse an integer out of a XML node */
int soap_xmlnode_get_long(xmlNodePtr cur, long *out);
/* parse an int64 out of a XML node */
int soap_xmlnode_get_int64(xmlNodePtr cur, long long *out);
/* parse a string out of a XML node */
int soap_xmlnode_get_octstr(xmlNodePtr cur, Octstr **out);
/* convert a one2one date format to epoch time */
time_t soap_read_date(Octstr *dateString);
/* convert a epoch time to one2one date format */
static Octstr *soap_write_date(time_t date);
/* start the SOAP server */
int soap_server_start(SMSCConn *conn);
/* stop the SOAP server */
static void soap_server_stop(PrivData *privdata);
/* create a new SOAP client caller */
static ClientData *soap_create_client_data();
/* destroy a SOAP client caller */
static void soap_destroy_client_data(void *data);
/* start an HTTP query */
static void soap_client_init_query(PrivData *privdata, List *headers, Octstr *data, Msg *msg);
/* return a caller from the pool that has responses waiting */
static ClientData *soap_client_have_response(List *client_list);
/* return data from a message according to its name */
static Octstr *soap_convert_token(Msg *msg, Octstr *name, PrivData *privdata);
/* convert a XML parsing spec file and a list of recognized keywords to an argument map */
List *soap_create_map(Octstr* spec, long count, char* keywords[], char* types[], void* storage[]);
/* destroy a map structure */
void soap_destroy_map(void *item);
/* map content in a XML structure to a list of variable using a spec file */
int soap_map_xml_data(xmlNodePtr xml, List* maps);
/* fetch content from the XML */
Octstr* soap_fetch_xml_data(xmlNodePtr xml, Octstr* path);

/* MO */
/* search and release dependences for keys */
long soap_release_dependences(Octstr* deps, List* lstmaps, Msg* msg, PrivData *privdata);
/* for appropriate <key> call function referenced by key_func_ind */
int soap_process_deps(int key_index, int key_func_ind, Msg* msg, PrivData *privdata);

/* <key>s specific functions */
int soap_msgtype_deps(int key_func_index, Msg* msg);
int soap_msgdata_deps(int key_func_index, Msg* msg, PrivData *privdata);
 
/* MT */
/* return index of functions alias in array of function aliases */
int soap_lookup_function(Octstr* funcname);

/* select function by index */
Octstr* soap_select_function(int index, Msg* msg, PrivData* privdata);

Octstr* soap_bouyg_content_attribute(Msg* msg);
Octstr* soap_mobitai_content_attribute(Msg* msg);
Octstr* soap_o2o_msgdata_attribute(Msg* msg, PrivData *privdata);
Octstr* soap_msgdata_attribute(Msg* msg, PrivData* privdata);
Octstr* soap_o2o_validity30_attribute(Msg* msg);
Octstr* soap_mobitai_validity_date_attribute(Msg* msg);
Octstr* soap_bouyg_validity_attribute(Msg* msg);
Octstr* soap_o2o_date_attribute(Msg* msg);
Octstr* soap_mobitai_date_attribute(Msg* msg);
Octstr* soap_rand_attribute(Msg* msg);
Octstr* soap_o2o_dlrmask_smsc_yn_attribute(Msg* msg);
Octstr* soap_o2o_dlrmask_success_01_attribute(Msg* msg);

/* searching 'key' in 'where' and return index of element or -1 */
int soap_get_index(List* where, Octstr* key, int map_index);


/**************************************************************************************
 * Implementation
 */

/*
 * function smsc_soap_create()
 *	called to create and initalize the module's internal data.
 *	if needed also will start the connection threads
 * Input: SMSCConn pointer to connection data, cfgGroup pointer to configuration data
 * Returns: status (0 = OK, -1 = failed)
 */
int smsc_soap_create(SMSCConn *conn, CfgGroup *cfg)
{
    PrivData *privdata;
    Octstr* temp = NULL;
    List* filenames = NULL;

    /* allocate and init internat data structure */
    privdata = gw_malloc(sizeof(PrivData));
    privdata->outgoing_queue = gwlist_create();
    /* privdata->pending_ack_queue = gwlist_create(); */

    privdata->shutdown = 0;
    privdata->soap_client = NULL;
    privdata->soap_server = 0;

    /* read configuration data */
    if (cfg_get_integer(&(privdata->port), cfg, octstr_imm("receive-port-ssl")) == -1)
        if (cfg_get_integer(&(privdata->port), cfg, octstr_imm("receive-port")) == -1)
            privdata->port = 0;
        else
            privdata->ssl = 0;
    else

        privdata->ssl = 1;

    privdata->uri = cfg_get(cfg, octstr_imm("send-url"));

    privdata->allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    if (privdata->allow_ip)
        privdata->deny_ip = octstr_create("*.*.*.*");
    else
        privdata->deny_ip = NULL;

    /* read XML configuration */
    privdata->form_variable = cfg_get(cfg, octstr_imm("form-variable"));
    cfg_get_bool(&(privdata->form_urlencoded), cfg, octstr_imm("form-urlencoded"));

    privdata->alt_charset = cfg_get(cfg, octstr_imm("alt-charset"));
    if (!privdata->alt_charset)
        privdata->alt_charset = octstr_create("utf-8");

    /* check validity of stuff */
    if (privdata->port <= 0 || privdata->port > 65535) {
        error(0, "invalid port definition for SOAP server (%ld) - aborting", 
              privdata->port);
        goto error;
    }

    if (!privdata->uri) {
        error(0, "invalid or missing send-url definition for SOAP - aborting.");
        goto error;
    }

    if (!privdata->form_variable) {
        error(0, "invalid or missing form variable name definition for SOAP - aborting.");
        goto error;
    }

    /* load XML templates and specs */
    filenames = octstr_split(temp = cfg_get(cfg,octstr_imm("xml-files")), 
                             octstr_imm(";"));
    octstr_destroy(temp);
    if (gwlist_len(filenames) < 3) {
        error(0,"SOAP: Not enough template files for XML generation, you need 3 - aborting"); 
        goto error;
    }
    if ( !(privdata->mt_xml_file = octstr_read_file(
            octstr_get_cstr(temp = gwlist_extract_first(filenames))))) {
        error(0,"SOAP: Can't load XML template for MT - aborting"); 
        goto error;

    }
    octstr_destroy(temp);
    if ( !(privdata->mo_xml_file = octstr_read_file(
            octstr_get_cstr(temp = gwlist_extract_first(filenames))))) {
        error(0,"SOAP: Can't load XML template for MO - aborting"); 
        goto error;
    }
    octstr_destroy(temp);
    if ( !(privdata->dlr_xml_file = octstr_read_file(
            octstr_get_cstr(temp = gwlist_extract_first(filenames))))) {

        error(0,"SOAP: Can't load XML template for DLR - aborting"); 
        goto error;
    }
    octstr_destroy(temp);
    gwlist_destroy(filenames, octstr_destroy_item);

    filenames = octstr_split(temp = cfg_get(cfg,octstr_imm("xmlspec-files")), 
                             octstr_imm(";"));
    octstr_destroy(temp);
    if (gwlist_len(filenames) < 4) {
        error(0,"Not enough spec files for XML parsing, you need 4 - aborting"); 
        goto error;
    }
    if ( !(privdata->mt_spec_file = octstr_read_file(
            octstr_get_cstr(temp = gwlist_extract_first(filenames))))) {
        error(0,"Can't load spec for MT parsing - aborting"); 
        goto error;
    }
    octstr_destroy(temp);
    if ( !(privdata->mo_spec_file = octstr_read_file(
            octstr_get_cstr(temp = gwlist_extract_first(filenames))))) {
        error(0,"SOAP: Can't load spec for MO parsing - aborting"); 
        goto error;
    }
    octstr_destroy(temp);
    if ( !(privdata->dlr_spec_file = octstr_read_file(
            octstr_get_cstr(temp = gwlist_extract_first(filenames))))) {
        error(0,"SOAP: Can't load spec for DLR parsing - aborting"); 
        goto error;
    }
    octstr_destroy(temp);

    if ( !(privdata->mo_deps_file = octstr_read_file(
            octstr_get_cstr(temp = gwlist_extract_first(filenames))))) {
        error(0,"SOAP: Can't load 'deps' file for MO processing - aborting"); 
        goto error;
    }
    octstr_destroy(temp);

    gwlist_destroy(filenames, octstr_destroy_item);

    debug("bb.soap.create",0,"Connecting to %s",
          octstr_get_cstr(privdata->uri));

    /* store private data struct in connection data */
    conn->data = privdata;

    /* state my name */
    conn->name = octstr_format("SOAP: %s", octstr_get_cstr(privdata->uri) );
    privdata->name = octstr_duplicate(conn->id);

    /* init status vars */
    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = time(NULL);

    /* set up call backs for bearerbox */
    conn->shutdown = soap_shutdown_cb;
    conn->queued = soap_queued_cb;
    conn->start_conn = soap_start_cb;
    conn->stop_conn = soap_stop_cb;
    conn->send_msg = soap_add_msg_cb;
  
    privdata->listener_thread = 0;
    privdata->server_thread = 0;
  
    /* check whether we can start right away */
    if (!conn->is_stopped)
        /* yes, we can */
        conn->status = SMSCCONN_CONNECTING;
    else
        conn->status = SMSCCONN_DISCONNECTED;

    /* any which way - start the connection thread */
    if ((privdata->listener_thread = gwthread_create(soap_listener, conn)) == -1) {
        error(0, "SOAP: soap_create, failed to spawn thread - aborting");
        goto error;
    }

    return 0; /* done - ok */

error:
    /* oh oh, problems */
    error(0, "SOAP: Failed to create SOAP smsc connection");

    /* release stuff */
    if (privdata != NULL) {
        gwlist_destroy(privdata->outgoing_queue, NULL);
        /* gwlist_destroy(privdata->pending_ack_queue, NULL); */

        O_DESTROY(privdata->uri);
        O_DESTROY(privdata->allow_ip);
        O_DESTROY(privdata->deny_ip);
        O_DESTROY(privdata->form_variable);
        O_DESTROY(privdata->alt_charset);
        O_DESTROY(privdata->name);
        O_DESTROY(privdata->mo_xml_file);
        O_DESTROY(privdata->dlr_xml_file);
        O_DESTROY(privdata->mt_xml_file);
        O_DESTROY(privdata->mo_spec_file);
        O_DESTROY(privdata->dlr_spec_file);
        O_DESTROY(privdata->mt_spec_file);
        O_DESTROY(privdata->mo_deps_file);
    }
    gw_free(privdata);
    octstr_destroy(temp);
    gwlist_destroy(filenames, octstr_destroy_item);

    /* notify bearerbox */
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;

    info(0, "exiting");
    return -1; /* I'm dead */
}


/**************************************************************************************
 * Callbacks
 */

/*
 * function soap_add_msg_cb()
 *	get a message and copy it to the queue. note that message must be copied
 *	as I don't know what bearerbox wants to do with it after I return
 * Input: SMSCConn connection state data, Msg to send
 * Returns: status - 0 on success, -1 on fail.
 */
static int soap_add_msg_cb(SMSCConn *conn, Msg *sms)
{

    PrivData *privdata = conn->data;
    Msg *copy;

    /* I'm dead and cannot take any calls at the moment, please don't leave a message */
    if (conn->status == SMSCCONN_DEAD)
        return -1;

    copy = msg_duplicate(sms); /* copy the message */
    gwlist_append(privdata->outgoing_queue, copy); /* put it in the queue */

    debug("bb.soap.add_msg",0,"SOAP[%s]: got a new MT from %s, list has now %ld MTs", 
          octstr_get_cstr(privdata->name), octstr_get_cstr(sms->sms.sender), 
          gwlist_len(privdata->outgoing_queue));

    gwthread_wakeup(privdata->listener_thread);

    return 0;
}


/*
 * function soap_shutdown_cb()
 *	called by bearerbox to signal the module to shutdown. sets the shutdown flags,
 *	wakes up the listener thread and exits (if we add more threads, we need to handle those too)
 * Input: SMSCConn connection state data, flag indicating whether we can finish sending messages
 *	in the queue first.
 * Returns: status - 0 on success, -1 on fail.
 */
static int soap_shutdown_cb(SMSCConn *conn, int finish_sending)
{
    PrivData *privdata = conn->data;
    long thread;

    /* I'm dead, there's really no point in killing me again, is it ? */
    if (conn->status == SMSCCONN_DEAD)
        return -1;

    debug("bb.soap.cb", 0, "SOAP[%s]: Shutting down SMSCConn, %s", 
          octstr_get_cstr(privdata->name), finish_sending ? "slow" : "instant");

    /* Documentation claims this would have been done by smscconn.c, 
     * but isn't when this code is being written.*/
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    /* Separate from why_killed to avoid locking, 
     * as why_killed may be changed from outside? */
    privdata->shutdown = 1;

    if (finish_sending == 0) {
        Msg *msg;
        while ((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL)
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);
    }

    thread = privdata->listener_thread;

    gwthread_wakeup(thread);
    gwthread_join(thread);

    return 0;
}


/*
 * function soap_start_cb()
 *	called by bearerbox when the module is allowed to start working
 * Input: SMSCConn connection state data
 * Returns: status - 0 on success, -1 on fail.
 */
static void soap_start_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;

    debug("smsc.soap.start", 0, "SOAP[%s]: start called", 
          octstr_get_cstr(privdata->name));

    /* set the status so that connection_thread will know what to do */
    conn->status = SMSCCONN_CONNECTING;

    /* start connection_thread, in case its not started. */
    if ((!privdata->listener_thread) &&
        ((privdata->listener_thread = gwthread_create(soap_listener, conn)) == -1)) {
        error(0, "SOAP: soap_start, failed to spawn thread - aborting");
        conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
        conn->status = SMSCCONN_DEAD;
        privdata->shutdown = 1;
        return;
    }
    /* gwthread_wakeup(privdata->listener_thread); */
    debug("smsc.soap.start",0,"SOAP[%s]: starting OK", 
          octstr_get_cstr(privdata->name));
}


/*
 * function soap_stop_cb()
 *	this function may be used to 'pause' the module. it should cause the connection
 *	to logout, but not to be destroyed, so it will be restarted later.
 * Input: SMSCConn connection state data
 */
static void soap_stop_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;

    /* I'm dead, its really too late to take a break now */
    if (conn->status == SMSCCONN_DEAD)
        return;

    debug("smsc.soap.stop", 0, "SOAP[%s]: stop called", 
          octstr_get_cstr(privdata->name));

    /* make connection thread disconnect */
    conn->status = SMSCCONN_DISCONNECTED;
}


/*
 * function soap_queued_cb()
 *	called by bearerbox to query the number of messages pending send.
 *	the number returned includes the number of messages sent, but for which no ACK was yet received.
 * Input: SMSCConn connection state data
 * Returns: number of messages still waiting to be sent
 */
static long soap_queued_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    long ret;

    /* I'm dead, so I have no queues - well there ! */
    if (conn->status == SMSCCONN_DEAD)
        return -1;

    ret = gwlist_len(privdata->outgoing_queue); 
    /* + gwlist_len(privdata->pending_ack_queue); */

    /* use internal queue as load, maybe something else later */
    conn->load = ret;

    return ret;
}


/**************************************************************************************
 * SOAP module thread functions (created by smsc_soap_create())
 */

/*
 * function soap_listener()
 *	entry point to the listenr thread. this thread listenes on the MO port (if
 *	needed, and is also reposnsible for invoking "MT threads" (HTTP clients) to
 *	to send MTs.
 * Input: SMSCConn connection state data
 */
static void soap_listener(void *arg)
{
    SMSCConn *conn = arg;
    PrivData *privdata = conn->data;
    Msg *msg = NULL;
    debug("bb.soap.listener",0,"SOAP[%s]: listener entering", 
          octstr_get_cstr(privdata->name));

    while (!privdata->shutdown) {

        /* check connection status */
        switch (conn->status) {
            case SMSCCONN_RECONNECTING:
            case SMSCCONN_CONNECTING:
                if (privdata->soap_server) {
                    soap_server_stop(privdata);
                }

                if (soap_server_start(conn)) {
                    privdata->shutdown = 1;
                    error(0, "SOAP[%s]: failed to start HTTP server!", 
                          octstr_get_cstr(privdata->name));
                    break;
                }

                mutex_lock(conn->flow_mutex);
                conn->status = SMSCCONN_ACTIVE;
                mutex_unlock(conn->flow_mutex);

                bb_smscconn_connected(conn);
                break;

            case SMSCCONN_DISCONNECTED:
                if (privdata->soap_server)
                    soap_server_stop(privdata);
                break;

            case SMSCCONN_ACTIVE:
                if (!privdata->soap_server) {
                    mutex_lock(conn->flow_mutex);
                    conn->status = SMSCCONN_RECONNECTING;
                    mutex_unlock(conn->flow_mutex);
                    break;
                }

                /* run the normal send/receive loop */
                if (gwlist_len(privdata->outgoing_queue) > 0) { /* we have messages to send */
                    soap_send_loop(conn); /* send any messages in queue */
                }
                break;

            case SMSCCONN_DEAD:
                /* this shouldn't happen here - 
                 * I'm the only one allowed to set SMSCCONN_DEAD */

            default:
                break;
        }

        soap_read_response(conn); /* collect HTTP responses */

        /* sleep for a while so I wont busy-loop */
        gwthread_sleep(SOAP_SLEEP_TIME); 
    }

    debug("bb.soap.connection",0,"SOAP[%s]: connection shutting down", 
          octstr_get_cstr(privdata->name));

    soap_server_stop(privdata);

    /* send all queued messages to bearerbox for recycling */
    debug("bb.soap.connection",0,"SOAP[%s]: sending messages back to bearerbox", 
          octstr_get_cstr(privdata->name));

    while ((msg = gwlist_extract_first(privdata->outgoing_queue)) != NULL)
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN, NULL);

    /* lock module public state data */
    mutex_lock(conn->flow_mutex);

    debug("bb.soap.connection",0,"SOAP[%s]: playing dead", 
          octstr_get_cstr(privdata->name));
    conn->status = SMSCCONN_DEAD; /* set state */

    /* destroy lists */
    debug("bb.soap.connection",0,"SOAP[%s]: don't need the queue anymore", 
          octstr_get_cstr(privdata->name));

    gwlist_destroy(privdata->outgoing_queue, NULL);
    /* gwlist_destroy(privdata->pending_ack_queue, NULL); */

    /* clear the soap client collection */
    debug("bb.soap.connection",0,"SOAP[%s]: tell caller to stop", 
          octstr_get_cstr(privdata->name));
    if (privdata->soap_client)
        gwlist_destroy(privdata->soap_client, soap_destroy_client_data);

    /* destroy private data stores */
    debug("bb.soap.connection",0,"SOAP[%s]: done with privdata", 
          octstr_get_cstr(privdata->name));
    O_DESTROY(privdata->uri);
    O_DESTROY(privdata->allow_ip);
    O_DESTROY(privdata->deny_ip);

    O_DESTROY(privdata->form_variable);

    O_DESTROY(privdata->alt_charset);
    O_DESTROY(privdata->name);
    O_DESTROY(privdata->mo_xml_file);

    O_DESTROY(privdata->dlr_xml_file);
    O_DESTROY(privdata->mt_xml_file);
    O_DESTROY(privdata->mo_spec_file);
    O_DESTROY(privdata->dlr_spec_file);
    O_DESTROY(privdata->mt_spec_file);
    O_DESTROY(privdata->mo_deps_file);

    gw_free(privdata);
    conn->data = NULL;

    mutex_unlock(conn->flow_mutex);

    debug("bb.soap.connection", 0, "SOAP: module has completed shutdown.");
    bb_smscconn_killed();
}


/*
 * function soap_server()
 *	server thread - accepts incoming MOs
 * Input: SMSCConn connection state data
 */
static void soap_server(void* arg)
{
    SMSCConn* conn = (SMSCConn*)arg;
    PrivData* privdata = conn->data;
    /* PrivData* privdata = (PrivData*)arg; */

    HTTPClient* remote_client = NULL;
    List *request_headers = NULL, *response_headers = NULL;
    List *cgivars = NULL;
    Octstr *client_ip = NULL, *request_uri = NULL, *request_body = NULL;
    Octstr *response_body = NULL;
    Octstr *timebuf = NULL;
    int http_response_status;

    debug("bb.soap.server",0,"SOAP[%s]: Server starting", 
          octstr_get_cstr(privdata->name));

    /* create basic headers */
    response_headers = http_create_empty_headers();
    http_header_add(response_headers, "Content-type","text/xml");
    /* http_header_add(response_headers, "Content-type","application/x-www-form-urlencoded"); */
    /* http_header_add(response_headers,"Connection", "Close"); */
    http_header_add(response_headers, "Server","Kannel");

    while (privdata->soap_server) {
        if ((remote_client = http_accept_request(privdata->port,
                              &client_ip, &request_uri, &request_headers, 
                              &request_body, &cgivars))) {

            debug("bb.soap.server",0,"SOAP[%s]: server got a request for "
                  "%s from %s, with body <%s>", octstr_get_cstr(privdata->name),
                  octstr_get_cstr(request_uri),octstr_get_cstr(client_ip),
                  request_body ? octstr_get_cstr(request_body) : "<null>");

            /* parse request */
            if (!octstr_compare(request_uri,octstr_imm(SOAP_MO_URI))) {
                /* this is an incoming MO */
                if ((http_response_status = 
                        soap_parse_mo(conn,request_body, &response_body)) == -1) {
                    /* fatal error parsing MO */
                    error(0,"SOAP[%s]: fatal error parsing MO", 
                          octstr_get_cstr(privdata->name));
                    response_body = octstr_create(SOAP_ERROR_MO_MESSAGE);
                    http_response_status = SOAP_ERROR_MO_CODE;
                }
            } else if (!octstr_compare(request_uri,octstr_imm(SOAP_DLR_URI))) {
                /* a delivery report */
                if ((http_response_status = 
                        soap_parse_dlr(conn,request_body, &response_body)) == -1) {
                    /* fatal error parsing MO */
                    error(0,"SOAP[%s]: fatal error parsing DLR", 
                          octstr_get_cstr(privdata->name));
                    response_body = octstr_create(SOAP_ERROR_DLR_MESSAGE);
                    http_response_status = SOAP_ERROR_DLR_CODE;
                }
            } else {
                /* unknown command send default message */
                response_body = octstr_create(SOAP_DEFAULT_MESSAGE);
                http_response_status = SOAP_DEFAULT_CODE;
            }

            /* create response */
            /*
            response_body = octstr_create("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE SMSCACCESS_REPLY SYSTEM \"http://superion/~oded/smsc_reply-1_0.dtd\">\n"
            "<SMSCACCESS_REPLY>\n"
            "   <SUBSCRIBER>447951718145</SUBSCRIBER>\n"
            "   <DATE_RECEIVED>22/01/2002:15:12</DATE_RECEIVED>\n"
            "   <RETURN_CODE>00</RETURN_CODE>\n"
            "</SMSCACCESS_REPLY>\n");
            */

            /* encode date in headers */

            timebuf = date_format_http(time(NULL));
            /* http_header_add(response_headers, "Date", octstr_get_cstr(timebuf)); */
            O_DESTROY(timebuf);
            /* http_header_dump(response_headers); */

            /* send response back to client */
            http_send_reply(remote_client,http_response_status,response_headers, response_body);

            /* destroy response data */
            /* http_destroy_headers(response_headers); */
            O_DESTROY(response_body);

            /* destroy request data */
            O_DESTROY(request_uri);
            O_DESTROY(request_body);
            O_DESTROY(client_ip);

            http_destroy_headers(request_headers);
            gwlist_destroy(cgivars, NULL);
        }

        gwthread_sleep(SOAP_SLEEP_TIME);
    }

    debug("bb.soap.server",0,"SOAP[%s]: server going down", 
          octstr_get_cstr(privdata->name));
    /* privdata->server_thread = 0; */
}


/**************************************************************************************
 * SOAP module internal protocol implementation functions
 */

/*
 * function soap_send_loop()
 *	called when there are messages in the queue waiting to be sent
 * Input: SMSCConn connection state data
 */
static void soap_send_loop(SMSCConn* conn)
{
    PrivData* privdata = conn->data;
    Msg *msg;
    Octstr* xmldata = NULL;
    int counter = 0;
 
    debug("bb.soap.client",0,"SOAP[%s]: client - entering", 
          octstr_get_cstr(privdata->name));

    while ((counter < SOAP_MAX_MESSAGE_PER_ROUND) && 
            (msg = gwlist_extract_first(privdata->outgoing_queue))) { 
        /* as long as we have some messages */
        ++counter;

        if (uuid_is_null(msg->sms.id))   /* generate a message id */
            uuid_generate(msg->sms.id);

        /* format the messages as a character buffer to send */
        if (!(xmldata = soap_format_xml(privdata->mt_xml_file, msg, privdata))) {
            debug("bb.soap.client",0,"SOAP[%s]: client - failed to format message for sending", 
                  octstr_get_cstr(privdata->name));
            bb_smscconn_send_failed(conn, msg,
	                SMSCCONN_FAILED_MALFORMED, octstr_create("MALFORMED"));
            continue;
        }

        debug("bb.soap.client",0,"SOAP[%s]: client - Sending message <%s>",
              octstr_get_cstr(privdata->name), octstr_get_cstr(msg->sms.msgdata));
        if (xmldata)
            debug("bb.soap.client",0,"SOAP[%s]: data dump: %s",
                  octstr_get_cstr(privdata->name), octstr_get_cstr(xmldata));

        /* send to the server */
        soap_send(privdata, xmldata, msg);

        /* store in the second queue so that soap_read_response will know what to do */
        /* gwlist_append(privdata->pending_ack_queue,msg); */

        /* don't need this anymore */
        O_DESTROY(xmldata);
    }
}


/*
 * function soap_format_xml()
 *	fill in the fields in a XML template with data from a message
 * Input: Octstr containing an XML template,  Msg structure
 * Returns: Octstr xml formated data or NULL on error
 */
static Octstr *soap_format_xml(Octstr *xml_file, Msg *msg, PrivData *privdata)
{
    Octstr *xml;
    long t;
    long start = -1;
    int curly_enclose = 0;

    xml = octstr_create("");

    for (t = 0; t < octstr_len(xml_file); ++t) {
        unsigned char c;
         
        if ((c = octstr_get_char(xml_file,t)) == '%') {
            /* found start of token */
            start = t+1;
            continue;
        }

        if (c == '{' && start == t) { /* the token is enclosed in curlys */
            ++start; /* make sure the token is read from the next char */
            curly_enclose=1;
        }

        if (start < 0)
            octstr_append_char(xml,c);
        else if (
            (curly_enclose && (c == '}')) /* end of token in case of curly enclosure */
            ||
            (!curly_enclose && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                                 (c >= '0' && c <= '9') || c == '_'))) {
            /* found end of token */
            Octstr *data, *token;
          
            token = octstr_copy(xml_file,start,(t-start));
            if ((data = soap_convert_token(msg, token, privdata))) {
                octstr_append(xml, data);
                octstr_destroy(data);
            } else {
                error(0,"SOAP: format_xml - failed to format token %s using message",
                      octstr_get_cstr(token));
                octstr_destroy(token);
                octstr_destroy(xml);
                return NULL;

            }
            octstr_destroy(token);
            start = -1;
            if (!curly_enclose)
                /* I want to get that char again, to let the normal behaviour 
                 * deal with it - only if it's not the ending curly */
                --t; 
            else
                curly_enclose = 0;
        }
    }

    return xml;
}


/*
 * function soap_send()
 *	send an XML buffer using POST to the SOAP server.
 * Input: PrivData connection state, Octstr XML formatted data buffer, 
 *        Message pointer to store with request
 */
static void soap_send(PrivData* privdata, Octstr* xmlbuffer, Msg* msg)
{
    List *requestHeaders;
    Octstr* postdata;

    /* create request headers */
    requestHeaders = http_create_empty_headers();
    http_header_add(requestHeaders, "User-Agent", "Kannel " GW_VERSION);

    if (privdata->form_urlencoded) {
        http_header_add(requestHeaders, "Content-Type", "application/x-www-form-urlencoded");
        postdata = octstr_format("%S=%E", privdata->form_variable, xmlbuffer);
    } else {
        http_header_add(requestHeaders, "Content-Type", "multipart/form-data, boundary=AaB03x");
        postdata = octstr_format("--AaB03x\r\n"
                                 "content-disposition: form-data; name=\"%S\"\r\n\r\n%S",
                                 privdata->form_variable, xmlbuffer);
    }

    /* send the request along */
    soap_client_init_query(privdata, requestHeaders, postdata, msg);

    O_DESTROY(postdata);

    /* done with that */
    http_destroy_headers(requestHeaders);

    return;

}


/*
 * function soap_read_response()
 *	check my HTTP caller for responses and act on them

 * Input: PrivData connection state
 **/
static void soap_read_response(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    Msg* msg;
    Octstr *responseBody, *responseURL;
    List* responseHeaders;
    int responseStatus;
    long long msgID;
    ClientData* cd;

    /* don't get in here unless I have some callers */
    /* (I shouldn't have one before I start sending messages) */
    if (!gwlist_len(privdata->soap_client))
        return;


    /* see if we have any responses pending */
    if (!(cd = soap_client_have_response(privdata->soap_client)))
        return;

    cd->requests--;
    msg = http_receive_result(cd->caller, &responseStatus, &responseURL, &responseHeaders, &responseBody);

    if (!msg) /* no responses here */
    {
        debug("bb.soap.read_response",0,"SOAP[%s]: sorry, no response", octstr_get_cstr(privdata->name));
        return;
    }

    if (responseStatus == -1) {
        debug("bb.soap.read_response",0,"SOAP[%s]: HTTP connection failed - blame the server (requeing msg)",
              octstr_get_cstr(privdata->name));
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_MALFORMED, octstr_create("MALFORMED"));
        /*    bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_TEMPORARILY); */
        /*      gwlist_append(privdata->outgoing_queue, msg); */
        return;
    }

    debug("bb.soap.read_response",0,"SOAP[%s]: got a response %d= %s",
          octstr_get_cstr(privdata->name), responseStatus, responseBody?octstr_get_cstr(responseBody):octstr_get_cstr(octstr_imm("NULL")));


    /* got a message from HTTP, parse it */
    if ( (msgID = soap_parse_response(privdata, responseBody)) >= 0)
    { /* ack with msg ID */
        char tmpid[30];

        /*
         * XXX UUID is used, fix this. 

        if (msgID == 0)
            msgID = msg->sms.id;
        */

        sprintf(tmpid,"%lld",msgID);
        debug("bb.soap.read_response",0,"SOAP[%s]: ACK - id: %lld", octstr_get_cstr(privdata->name), msgID);

        dlr_add(conn->id, octstr_imm(tmpid), msg, 0);

        /* send msg back to bearerbox for recycling */
        bb_smscconn_sent(conn, msg, NULL);
    }

    else { /* nack */
        debug("bb.soap.read_response",0,"SOAP[%s]: NACK", octstr_get_cstr(privdata->name));

        /* send msg back to bearerbox for recycling */
        bb_smscconn_send_failed(conn, msg,
	            SMSCCONN_FAILED_MALFORMED, octstr_create("MALFORMED"));
    }

    http_destroy_headers(responseHeaders);
    O_DESTROY(responseBody);
    O_DESTROY(responseURL);
}

/*
 * function soap_parse_response()
 *	parse the response from the server to find the message ID
 * Input: Connection session data, Octstr xml buffer
 * Returns: message ID parsed or -1 if parsing failed (for example - a NACK received)
 *
 * Possible bug : I use gwlist_get() liberaly here, after checking that I have enough items,
 *                but if gwlist_get() returns NULL for an empty item, things might break - and
 *                not in a nice way.
 **/
static long long soap_parse_response(PrivData* privdata, Octstr* xmlResponse)
{
    long long msgID = -1;
    long responseStatus = -1;
    xmlDocPtr responseDoc;
    xmlNodePtr root;
    List* maps;
    char* keywords[] = { "id", "result" };
    char* sscans[] = { "%lld", "%ld" };
    void* pointers[] = { &msgID, &responseStatus };

    if (!xmlResponse)
        return -1;
    /* FIXME: do something here */

    /* parse XML */
    if ( !(responseDoc = xmlParseDoc((xmlChar *)octstr_get_cstr(xmlResponse))) ) {
        error(0,"SOAP[%s]: couldn't parse XML response [ %s ] in MT parsing",
              octstr_get_cstr(privdata->name), octstr_get_cstr(xmlResponse));
        return -1;
    }

    /* get root element */
    if ( ! (root = xmlDocGetRootElement(responseDoc)) ) {
        error(0,"SOAP[%s]: couldn't get XML root element in MT parsing",
              octstr_get_cstr(privdata->name));
        xmlFreeDoc(responseDoc);
        return -1;
    }

    /* create the argument map */
    maps = soap_create_map(privdata->mt_spec_file, 2, keywords, sscans, pointers);

    /* run the map and the xml through the parser */
    if (soap_map_xml_data(root, maps) < 2) {
        error(0,"SOAP[%s]: failed to map all the arguments from the XML data",
              octstr_get_cstr(privdata->name));
    }

    gwlist_destroy(maps, soap_destroy_map);

    /* done with the document */
    xmlFreeDoc(responseDoc);

    if (msgID == -1) {
        if (responseStatus == 0) {   /* success without msg ID */
            warning(0, "SOAP[%s]: parse_response - the protocol does not support message ID",
                    octstr_get_cstr(privdata->name));
            return 0;
        }
        else {
            error(0,"SOAP[%s]: parse_response - response code isn't 0 ! (%ld)",
                  octstr_get_cstr(privdata->name), responseStatus);
            return -1;  /* Nack */
        }
    }
    else
        return msgID;   /* success with msg ID */

}


/*
 * function soap_parse_mo()
 *	parse an incoming MO xml request, build a message from it and sent it.
 *	also generate the reponse text and status code
 * Input: module public state data, request body
 * Output: response body
 * Returns: HTTP status code on successful parse or -1 on failure
 **/
static long soap_parse_mo(SMSCConn *conn, Octstr *request, Octstr **response)
{
    PrivData *privdata = conn->data;
    xmlDocPtr requestDoc;
    xmlNodePtr root;

    Msg* msg;
    int pos = 0;

    long res = -1;

    List* maps;
    char receiver[30], sender[30], msgtype[30], msgdata[255], date[30];
    long long msgid = -1;
    char* keywords[] = { "receiver", "sender", "msgtype", "msgdata", "date", "id" };
    char* sscans[] = { "%s", "%s", "%s", "%s", "%s", "%lld" };
    void* pointers[] = { &receiver, &sender, &msgtype, &msgdata, &date, &msgid };

    receiver[0] = sender[0] = msgtype[0] = msgdata[0] = date[0] = '\0';

    if (!response) /* how am I supposed to return a response now ? */
        return -1;

    if (!request) {
        *response = octstr_create(SOAP_ERROR_NO_DATA_MESSAGE);
        return SOAP_ERROR_NO_DATA_CODE;
    }

    /* find the POST parameter name */
    if ( (pos = octstr_search_char(request,'=',0)) < 0) {
        /* didn't find it - */
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    /* cut of the parameter name - I'm not really interested in it */
    octstr_delete(request,0,pos+1);

    /* decode the URL encoded data */
    if (octstr_url_decode(request) < 0) {
        /* probably not URL encoded */

        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    debug("bb.soap.parse_mo",0,"SOAP[%s]: parse_mo  - MO request dump <%s>", octstr_get_cstr(privdata->name),octstr_get_cstr(request));

    /* parse XML */
    if ( !(requestDoc = xmlParseDoc((xmlChar *)octstr_get_cstr(request))) ) {
        error(0,"SOAP[%s]: parse_mo couldn't parse XML response", octstr_get_cstr(privdata->name));
        return -1;
    }

    /* get root element */
    if ( ! (root = xmlDocGetRootElement(requestDoc)) ) {
        error(0,"SOAP[%s]: parse_mo couldn't get XML root element for request", octstr_get_cstr(privdata->name));
        xmlFreeDoc(requestDoc);
        return -1;
    }

    /* create the argument map */
    maps = soap_create_map(privdata->mo_spec_file, 6, keywords, sscans, pointers);


    /* run the map and the xml through the parser */
    if (soap_map_xml_data(root, maps) < gwlist_len(maps)) {
        error(0,"SOAP[%s]: parse_mo failed to map all the arguments from the XML data",
              octstr_get_cstr(privdata->name));
    }

    /* done with the document */
    xmlFreeDoc(requestDoc);

    if (strlen(receiver) == 0) {
        error(0,"SOAP: parse_mo - failed to get receiver");
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    if (strlen(sender) == 0) {
        error(0,"SOAP: parse_mo - failed to get sender");
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    if (strlen(msgdata) == 0) {
        error(0,"SOAP: parse_mo - failed to get message content");
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);

        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    /* create me a message to store data in it */

    msg = msg_create(sms);

    /*
     * XXX UUID is used, fix this. 

    if (msgid == -1) {
        error(0,"SOAP: parse_mo - failed to get message ID, generate by itself");
        msg->sms.id = gw_generate_id(); 
    }
    */

    /* fill in the fields from the parsed arguments */
    msg->sms.sender = octstr_create(sender);
    msg->sms.receiver = octstr_create(receiver);
    /*
     * XXX UUID is used, fix this. 
    msg->sms.id = msgid;
    */
    msg->sms.msgdata = octstr_create(msgdata);

    /* special processing and refill appropriate fields */
    if (privdata->mo_deps_file) {
        if ((res = soap_release_dependences(privdata->mo_deps_file, maps, msg, privdata))!=0)
            error(0,"SOAP: parse_mo - failed to release all dependences");
    }
    gwlist_destroy(maps, soap_destroy_map);

    /* fill in the date */
    if (strlen(date)) {
        struct universaltime tm;
        Octstr* temp = octstr_create(date);
        if (date_parse_iso(&tm, temp))
            /* failed to parse the date */
            msg->sms.time = time(NULL);
        else
            msg->sms.time = date_convert_universal(&tm);
        octstr_destroy(temp);
    } else
        msg->sms.time = time(NULL);

    /*
        / * check message data type - B stands for "base 64 encoded" in Team Mobile * /
        if (!strcmp(msgtype, "B")) {
            octstr_base64_to_binary(msg->sms.msgdata);
            msg->sms.coding = DC_8BIT;

        } else if (!strcmp(msgtype, "binary")) {
            octstr_hex_to_binary(msg->sms.msgdata);
            msg->sms.coding = DC_8BIT;
        } else {
    */

    /* not gonna play this game - just convert from whatever alt_charset is set to, to UCS-2
        / * scan message for unicode chars (UTF-8 encoded) * /
        pos = 0;
        while (pos < octstr_len(msg->sms.msgdata)) {
    	if (octstr_get_char(msg->sms.msgdata,pos) & 128)
    	    break;
    	++pos;
        }

        if (pos < octstr_len(msg->sms.msgdata)) {
            / * message has some unicode - we need to convert to UCS-2 first * /
            Octstr* temp = msg->sms.msgdata;

            msg->sms.coding = DC_UCS2;
       	if (charset_from_utf8(temp, &(msg->sms.msgdata), octstr_imm("UCS-2")) < 0) {
                error(0,"SOAP[%s]: parse_mo couldn't convert msg text from UTF-8 to UCS-2. leaving as is.", octstr_get_cstr(privdata->name));
                O_DESTROY(msg->sms.msgdata);
                msg->sms.msgdata = octstr_duplicate(temp);
    	    / *  set coding to 8bit and hope for the best * /
    	    msg->sms.coding = DC_8BIT;

            }
            octstr_destroy(temp);
        } else
             / * not unicode : 7bit * /
             msg->sms.coding = DC_7BIT;
    */

    /* if it's not binary, then assume unicode and convert from alt_charset to UCS-2 */
    /*        msg->sms.coding = DC_UCS2;



            if (!octstr_case_compare(privdata->alt_charset, octstr_imm("UCS-2")))  {
                int ret = 0;
                debug("bb.soap.parse_mo",0,"SOAP[%s]: converting from %s to UCS-2BE",
                        octstr_get_cstr(privdata->name), octstr_get_cstr(privdata->alt_charset));
                ret = charset_convert(msg->sms.msgdata, octstr_get_cstr(privdata->alt_charset), "UCS-2BE");


                if (ret == -1) {
                    error(2,"SOAP[%s]: Error converting MO data from %s to unicode",
                            octstr_get_cstr(privdata->name),  octstr_get_cstr(privdata->alt_charset));
                } else if (ret != 0) {
                    debug("bb.soap.parse_mo",1,"SOAP[%s]: charset_convert made %d irreversable transformations",
                            octstr_get_cstr(privdata->name), ret);
                }
            }
            msg->sms.charset = octstr_create("UCS-2");
        }
        debug("bb.soap.parse_mo",0,"SOAP[%s]: message decoded -", octstr_get_cstr(privdata->name));
        octstr_dump(msg->sms.msgdata,0);
    */

    /* check that we have all the fields necessary */
    if (!(msg->sms.sender)
            ||
            !(msg->sms.msgdata)) {
        /* generate error message */
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    /* setup defaults */
    if (msg->sms.time <= 0)
        msg->sms.time = time(NULL);

    if (!msg->sms.receiver)
        msg->sms.receiver = octstr_create(SOAP_DEFAULT_SENDER_STRING);

    if (!msg->sms.smsc_id)
        msg->sms.smsc_id = octstr_duplicate(conn->id);

    *response = soap_format_xml(privdata->mo_xml_file,msg,privdata);
    if (*response)
        debug("bb.soap.reponse_dlr",0,"SOAP[%s]: data dump: %s", octstr_get_cstr(privdata->name), octstr_get_cstr(*response));

    bb_smscconn_receive(conn,msg);

    return SOAP_QUERY_OK;
}

/*
 * function soap_parse_dlr()
 *	parse an incoming DLR xml request, build a message from it and sent it.
 *	also generate the reponse text and status code
 * Input: module public state data, request body
 * Output: response body
 * Returns: HTTP status code on successful parse or -1 on failure
 **/
static long soap_parse_dlr(SMSCConn *conn, Octstr *request, Octstr **response)
{
    PrivData *privdata = conn->data;
    xmlDocPtr requestDoc;
    xmlNodePtr root;
    Msg* dlrmsg = NULL;
    long dlrtype;
    int pos;

    List* maps;
    char receiver[30], soapdate[30], msgid[30];
    long result = -1;
    char* keywords[] = { "receiver", "soapdate", "id", "result" };

    char* sscans[] = { "%s", "%s", "%s", "%ld" };
    void* pointers[] = { &receiver, &soapdate, &msgid, &result };

    receiver[0] = soapdate[0] = msgid[0] = '\0';

    if (!response) /* how am I supposed to return a response now ? */
        return -1;

    if (!request) {
        *response = octstr_create(SOAP_ERROR_NO_DATA_MESSAGE);
        return SOAP_ERROR_NO_DATA_CODE;
    }

    /* find the POST parameter name */
    if ( (pos = octstr_search_char(request,'=',0)) < 0) {
        /* didn't find it - */
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    /* cut of the parameter name - I'm not really interested in it */
    octstr_delete(request,0,pos+1);

    /* decode the URL encoded data */
    if (octstr_url_decode(request) < 0) {
        /* probably not URL encoded */
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    debug("bb.soap.parse_dlr",0,"SOAP[%s]: parse_dlr  - DLR request dump <%s>", octstr_get_cstr(privdata->name),octstr_get_cstr(request));

    /* parse XML */

    if ( !(requestDoc = xmlParseDoc((xmlChar *)octstr_get_cstr(request))) ) {
        error(0,"SOAP[%s]: parse_dlr couldn't parse XML response", octstr_get_cstr(privdata->name));
        return -1;
    }

    /* get root element */
    if ( ! (root = xmlDocGetRootElement(requestDoc)) ) {

        error(0,"SOAP[%s]: parse_dlr couldn't get XML root element for request", octstr_get_cstr(privdata->name));
        xmlFreeDoc(requestDoc);
        return -1;
    }

    /* create the argument map */

    maps = soap_create_map(privdata->dlr_spec_file, 4, keywords, sscans, pointers);

    /* run the map and the xml through the parser */
    if (soap_map_xml_data(root, maps) < 4) {
        error(0,"SOAP[%s]: parse_dlr failed to map all the arguments from the XML data",

              octstr_get_cstr(privdata->name));
    }

    gwlist_destroy(maps, soap_destroy_map);

    /* done with the document */
    xmlFreeDoc(requestDoc);

    if (strlen(msgid) == 0) {
        error(0,"SOAP: parse_dlr - failed to get message ID");
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    if (result == -1) {

        error(0,"SOAP: parse_dlr - failed to get delivery code");
        *response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
        return SOAP_ERROR_MALFORMED_DATA_CODE;
    }

    /* we not need it because receiver now is constant string "receiver"
      if (strlen(receiver) == 0) {
           	error(0,"SOAP: parse_dlr - failed to get receiver");
    	*response = octstr_create(SOAP_ERROR_MALFORMED_DATA_MESSAGE);
    	return SOAP_ERROR_MALFORMED_DATA_CODE;
        }
    */
    /* log the delivery code - this could be used to determine dlrtype (or so I hope) */
    debug("bb.soap.parse_dlr",0,"SOAP[%s]: parse_dlr DELIVERY_CODE : %ld", octstr_get_cstr(privdata->name),result);
    if (result == 0)
        dlrtype = DLR_SUCCESS;
    else
        dlrtype = DLR_FAIL;

    /* fetch the DLR */

    dlrmsg = dlr_find(conn->id, octstr_imm(msgid), octstr_imm("receiver"), /* destination */
                      dlrtype, 0);

    if (!dlrmsg) {
        error(0,"SOAP[%s]: parse_dlr invoked (%ld), but no DLR found for MsgID %s", octstr_get_cstr(privdata->name),dlrtype,msgid);
        *response = octstr_create(SOAP_ERROR_NO_DLR_MESSAGE);
        return SOAP_ERROR_NO_DLR_CODE;
    }

    debug("bb.soap.parse_dlr",0,"SOAP[%s]: parse_dlr found dlr", octstr_get_cstr(privdata->name));
    octstr_destroy(dlrmsg->sms.msgdata);
    switch (dlrtype) { /* change message according to DLR type */
        case DLR_SUCCESS:
            dlrmsg->sms.msgdata = octstr_create("Delivered");
            break;
        case DLR_BUFFERED:
            dlrmsg->sms.msgdata = octstr_create("Buffered");
            break;
        case DLR_FAIL:
            dlrmsg->sms.msgdata = octstr_create("Failed");
            break;
        default:
            break;

    }

    /*
    if (dlrmsg->sms.receiver) {
        octstr_destroy(dlrmsg->sms.sender);
        dlrmsg->sms.sender = dlrmsg->sms.receiver;
    }
    dlrmsg->sms.receiver = octstr_create(receiver);
    dlrmsg->sms.id = strtol(msgid, NULL, 10);
    */

    debug("bb.soap.parse_dlr",0,"SOAP[%s]: parse_dlr sent dlr <%s>", octstr_get_cstr(privdata->name),octstr_get_cstr(dlrmsg->sms.msgdata));


    *response = soap_format_xml(privdata->dlr_xml_file, dlrmsg, privdata);
    if (*response)
        debug("bb.soap.reponse_dlr",0,"SOAP[%s]: data dump: %s", octstr_get_cstr(privdata->name), octstr_get_cstr(*response));

    /* send to bearerbox */
    bb_smscconn_receive(conn, dlrmsg);

    return SOAP_QUERY_OK;

}


/*
 * SOAP internal utility functions
 **/

/*
 * function soap_xmlnode_get_long()
 *	parse the content of an XML node and return it as an integer
 * Input: xmlNodePtr to node
 * Output: long parsed
 * Returns: 0 on success, -1 on failure
 **/
int soap_xmlnode_get_long(xmlNodePtr cur, long* out)
{
    xmlChar* nodeContent;
    char* endPointer;

    if (!out)  /* sanity check */
        return -1;

    /* get content of tag */
    if (!(nodeContent = xmlNodeGetContent(cur))) {
        error(0,"SOAP: get_long - xml Node has content !");
        return -1;
    }

    /* read the content into output */
    *out = strtol((char *)nodeContent,&endPointer,10);
    xmlFree(nodeContent);

    if (endPointer == (char*)nodeContent) {
        error(0,"SOAP: get_long - node has non-numeric content <%s>", nodeContent);
        return -1;
    }

    return 0;
}

/*
 * function soap_xmlnode_get_int64()
 *	parse the content of an XML node and return it as an long long
 * Input: xmlNodePtr to node
 * Output: long parsed
 * Returns: 0 on success, -1 on failure
 **/
int soap_xmlnode_get_int64(xmlNodePtr cur, long long* out)
{
    xmlChar* nodeContent;
    char* endPointer;

    if (!out)  /* sanity check */
        return -1;

    /* get content of tag */
    if (!(nodeContent = xmlNodeGetContent(cur))) {
        error(0,"SOAP: get_long - xml Node has content !");
        return -1;
    }


    /* read the content into output */
    *out = strtoll((char *)nodeContent,&endPointer,10);
    xmlFree(nodeContent);

    if (endPointer == (char*)nodeContent) {
        error(0,"SOAP: get_long - node has non-numeric content <%s>", nodeContent);
        return -1;
    }

    return 0;
}

/*
 * function soap_xmlnode_get_octstr()
 *	parse the content of an XML node and return it as an Octstr*
 * Input: xmlNodePtr to node
 * Output: Octstr to feel with data
 * Returns: 0 on success, -1 on failure
 **/
int soap_xmlnode_get_octstr(xmlNodePtr cur, Octstr **out)
{
    xmlChar* nodeContent;

    if (!out)  /* sanity check */
        return -1;

    /* get content of tag */
    if (!(nodeContent = xmlNodeGetContent(cur))) {
        error(0,"SOAP: get_octstr - xml Node has content !");
        return -1;
    }

    /* store the content into output */
    *out = octstr_create((char *)nodeContent);
    xmlFree(nodeContent);

    if (*out)
        return 0;
    else
        return -1;


}

/*
 * function soap_read_date()
 *	convert a date string in one2one obiquis format (%Y/%M/%d:%h:%m) to epoch time
 * Input: Octstr date
 * Returns: epoch time on success or -1 on failure
 **/
time_t soap_read_date(Octstr* dateString)
{
    int pos, count;
    struct universaltime stTime;
    long arTime[5];


    if (!dateString) /* sanity check */
        return -1;

    pos = count = 0;
    /* tricky control structures are my favourite among complicated expressions ;-) */
    while (count < 5 && pos < octstr_len(dateString) &&
            (pos = octstr_parse_long(&(arTime[count++]),dateString, pos,10)) && pos != -1)
        ++pos;

    if (count < 5) {
        /* error parsing the date */
        debug("bb.soap.read_date",0,"read_date failed parsing the date value <%s>", octstr_get_cstr(dateString));
        return -1;
    }

    stTime.day = arTime[0];
    stTime.month = arTime[1];
    stTime.year = arTime[2];
    stTime.hour = arTime[3];
    stTime.minute = arTime[4];
    stTime.second = 0;
    return date_convert_universal(&stTime);
}

/*
 * function soap_write_date()
 *	convert an epoch time value to a date string in one2one obiquis format (%Y/%M/%d:%h:%m)
 * Input: time_t epoch time
 * Returns: an Octstr containing the date - this must be freed by the caller
 **/
static Octstr* soap_write_date(time_t date)
{
    struct tm date_parts;
    Octstr* out;

    if (date < 0)
        /* sanity check - I don't think it should ever happen, but I don't want to get
           support calls at 2am because some gateway in the UK went bananas. */
        return octstr_create("ERROR");

    /* split up epoch time to elements */
    gmtime_r(&date, &date_parts);

    out = octstr_format("%d/%02d/%02d:%02d:%02d",
                        date_parts.tm_year + 1900, date_parts.tm_mon + 1, date_parts.tm_mday, date_parts.tm_hour, date_parts.tm_min);

    /* again */
    if (out)
        return out;
    else
        return octstr_create("ERROR");
    /* assuming octstr_create never fails, unlike octstr_format. this is not the case currently (both cannot fail), but it may change */
}


/*
 * function soap_server_start()
 *	init and start the SOAP HTTP server
 * Input: Module public connection state data
 * Returns: 0 on success, -1 on failure
 **/
int soap_server_start(SMSCConn *conn)
{
    PrivData* privdata = conn->data;

    debug("bb.soap.server_stop",0,"SOAP[%s]: Starting HTTP server", octstr_get_cstr(privdata->name));
    /* start the HTTP server */
    if (http_open_port(privdata->port,privdata->ssl)) {
        return -1;
    }


    /* raise server flag */
    privdata->soap_server = 1;

    if ( (privdata->server_thread = gwthread_create(soap_server, conn)) == -1)
    {
        error(0, "SOAP[%s]: server_start failed to create server thread!", octstr_get_cstr(privdata->name));
        http_close_port(privdata->port);
        return -1;
    }


    return 0;
}

/*
 * function soap_server_stop()
 *	tears down and stops the SOAP HTTP server
 * Input: Module connection state data
 **/
static void soap_server_stop(PrivData* privdata)
{
    /*    time_t start = time(NULL); */

    debug("bb.soap.server_stop",0,"SOAP[%s]: Stopping HTTP server", octstr_get_cstr(privdata->name));
    /* signal the server thread to stop */
    privdata->soap_server = 0;

    /* close the http server thread */
    http_close_port(privdata->port);

    if (privdata->server_thread) {
        gwthread_wakeup(privdata->server_thread);
        gwthread_join(privdata->server_thread);
        privdata->server_thread = 0;
    }


    /*
    / * wait upto 5 minutes for our server thread to shutdown * /
    while (privdata->server_thread &&  (start + 300 > time(NULL)))
    gwthread_sleep(SOAP_SLEEP_TIME);


    if (privdata->server_thread) {
    error(0,"SOAP[%s]: our server refuses to die!", octstr_get_cstr(privdata->name));
    privdata->server_thread = 0; / * dump it either way * /

    }*/

    debug("bb.soap.server_stop",0,"SOAP[%s]: Done stopping HTTP server", octstr_get_cstr(privdata->name));
}


/*
 * function soap_create_client_data()
 *	creates a new SOAP client data structure and caller
 * Returns: an initialized client data structure with a live caller

 **/
static ClientData* soap_create_client_data()
{

    ClientData *cd = gw_malloc(sizeof(ClientData));

    cd->last_access = 0;
    cd->requests = 0;
    cd->caller = http_caller_create();

    return cd;
}


/*
 * function soap_client_init_query()
 *	start an HTTP query, load balance callers, and manage caller pool
 * Input: Module state, list of headers to send, data to send, message to store
 **/
static void soap_client_init_query(PrivData* privdata, List* headers, Octstr* data, Msg* msg)
{
    ClientData *cur_client = NULL;
    long index;


    /* no list yet, generate one */
    if (!privdata->soap_client)
        privdata->soap_client = gwlist_create();

    /* I'm going to change the list, so lock it */
    gwlist_lock(privdata->soap_client);

    /* find the next live caller */
    for (index = gwlist_len(privdata->soap_client) - 1 ; index >= 0; --index) {
        cur_client = gwlist_get(privdata->soap_client, index);
        if (
            cur_client->last_access + CLIENT_BUSY_TIME < time(NULL)
            &&
            cur_client->requests < CLIENT_BUSY_LOAD
        ) {
            debug("bb.soap.init_query",0,"SOAP[%s]: init_query getting a client",octstr_get_cstr(privdata->name));

            /* client is not busy - get it */
            gwlist_delete(privdata->soap_client, index, 1);
            break;
        }
        cur_client = NULL;
    }

    if (!cur_client) {
        if (gwlist_len(privdata->soap_client) > MAX_SOAP_CLIENTS) {
            debug("bb.soap.init_query",0,"SOAP[%s]: init_query all clients are busy, getting the first client",octstr_get_cstr(privdata->name));
            /* query not dispatched, and we have the max number of callers -
               grab the first caller (least used) from the list */
            cur_client = gwlist_extract_first(privdata->soap_client);
        } else {
            /* query not dispatched, and we don't have enough callers -
               start a new one */
            debug("bb.soap.init_query",0,"SOAP[%s]: init_query creates a new client",octstr_get_cstr(privdata->name));
            cur_client = soap_create_client_data();
        }
    }

    /* dispatch query to selected client */
    http_start_request(cur_client->caller, HTTP_METHOD_POST, privdata->uri, headers, data, 1, msg, NULL);
    cur_client->requests++;
    cur_client->last_access = time(NULL);
    gwlist_append(privdata->soap_client, cur_client);
    gwlist_unlock(privdata->soap_client);
}


/*
 * function soap_destroy_client_data()
 *	destroy a SOAP client caller
 * Input: pointer to a client data structure with a live caller
 **/
static void soap_destroy_client_data(void* data)
{
    ClientData *cd = (ClientData*) data;

    /* signal the caller to stop and then kill it */
    if (cd->caller) {
        http_caller_signal_shutdown(cd->caller);
        http_caller_destroy(cd->caller);
    }
}

/*
 * function soap_client_have_response()
 *	return a caller from the pool that has responses waiting
 * Input: ClientData pool
 * Returns: a client data structure that has a caller with responses waiting,
 *  or NULL if none are found
 **/
static ClientData* soap_client_have_response(List* client_list)
{
    long index;
    ClientData* cd;

    if (!client_list)
        return NULL;

    /* lock the list so nobody removes or adds clients while I'm looping on the list */
    gwlist_lock(client_list);

    for (index = gwlist_len(client_list) - 1; index >= 0; --index) {
        cd = gwlist_get(client_list,index);
        if (gwlist_len(cd->caller)) {

            gwlist_unlock(client_list);
            return gwlist_get(client_list, index);
        }
    }

    gwlist_unlock(client_list);
    return NULL;
}

/*
 * function soap_convert_token()
 *	convert a member of the message structure and return it as octstr
 * Input: member name
 * Returns: an Octstr containing the content of the data member from the message structure
 *  or NULL if an error occured.
 **/
static Octstr* soap_convert_token(Msg* msg, Octstr* name, PrivData* privdata)

{
    char buf[20];
    int index;

    if ( (index=soap_lookup_function(name)) >= 0 )
        return soap_select_function(index, msg, privdata);


#define INTEGER(fieldname) \
        if (!octstr_str_compare(name, #fieldname)) { \
                sprintf(buf,"%ld", p->fieldname); \
                return octstr_create(buf); \
        }
#define INT64(fieldname) \
        if (!octstr_str_compare(name, #fieldname)) { \
                sprintf(buf,"%lld", p->fieldname); \
                return octstr_create(buf); \
        }
#define OCTSTR(fieldname) \
        if (!octstr_str_compare(name, #fieldname)) \
                return octstr_duplicate(p->fieldname);
#define UUID(fieldname) 
#define VOID(fieldname)

#define MSG(type, stmt) \
        case type: { struct type *p = &msg->type; stmt } break;

    switch (msg->type) {
#include "msg-decl.h"
        default:


            error(0, "SOAP: Internal error: unknown message type %d", msg->type);
            return NULL;
    }

    error(0,"SOAP: soap_convert_token, can't find token named <%s>", octstr_get_cstr(name));
    return NULL;
}

/*
 * function soap_create_map()
 *	convert a XML parsing spec file and a list of recognized keywords to an argument map
 * Input: XML parsing spec buffer and lists of keywords, types and pointers
 * Returns: number of variables successfuly mapped
 **/
List* soap_create_map(Octstr* spec, long count, char* keywords[], char* types[], void* storage[])
{
    List *parse_items, *out;

    out = gwlist_create();

    /* read the list of items from the spec file */
    parse_items = octstr_split(spec, octstr_imm("\n"));

    while (gwlist_len(parse_items)) {
        ArgumentMap* map;
        int index;
        Octstr* temp = gwlist_extract_first(parse_items);
        List* item = octstr_split_words(temp);


        /* make sure we have at least two things in the item : a keyword and a path */
        if (gwlist_len(item) < 2) {
            debug("bb.soap.parse_create_map",0,"SOAP: broken spec file line <%s> in soap_create_map",
                  octstr_get_cstr(temp));
            octstr_destroy(temp);
            gwlist_destroy(item, octstr_destroy_item);
            continue;
        }

        /* check that the keyword matches something in the list of keywords */
        for (index = 0; index < count; ++index) {
            if (!octstr_str_compare(gwlist_get(item,0), keywords[index])) {
                /* allocate the structure */
                map = gw_malloc(sizeof(ArgumentMap));
                map->name = gwlist_extract_first(item);
                map->path = gwlist_extract_first(item);
                map->attribute = gwlist_extract_first(item); /* could be NULL, but that is ok */
                map->sscan_type = octstr_create(types[index]);
                map->store = storage[index];
                gwlist_append(out, map);
                break;
            }
        }

        /* destroy temporary variables; */
        gwlist_destroy(item, octstr_destroy_item);
        octstr_destroy(temp);
    }

    gwlist_destroy(parse_items, octstr_destroy_item);

    return out;
}

/*
 * function soap_destroy_map()
 *	destroy a map structure. used in gwlist_destroy(calls);
 * Input: pointer to a map structure;
 **/
void soap_destroy_map(void *item)
{
    ArgumentMap* map = item;
    octstr_destroy(map->name);
    octstr_destroy(map->path);
    octstr_destroy(map->attribute);
    octstr_destroy(map->sscan_type);
    gw_free(map);
}

/*
 * function soap_fetch_xml_data()
 *      return the value of an XML element.
 * Input: pointer to root of XML to search under, path specified in one of three forms
 *      a) <path to tag> - will return the content of this tag
 *      b) <path to tag>,<attribute name> - will return the value of this attribute
 *      c) "<fixed value>" - will return the given value as it is
 * Returns: content if found or NULL
 **/
Octstr* soap_fetch_xml_data(xmlNodePtr xml, Octstr* path)
{
    Octstr *temp, *xml_path, *attr_name = NULL;
    List* path_elements;
    unsigned char c;
    xmlNodePtr parent, node;
    int index;


    /* sanity check */
    if (!octstr_len(path) || !xml)
        return NULL;


    /* stop here for case (c) */
    if (((c = octstr_get_char(path, 0)) == '"' || c == '\'') &&
            (octstr_get_char(path, octstr_len(path)-1) == c))
        return octstr_copy(path, 1, octstr_len(path) - 2);

    /* split into XML path and attribute name */
    path_elements = octstr_split(path, octstr_imm(","));
    xml_path = gwlist_get(path_elements,0);
    if (gwlist_len(path_elements) > 1) /* case (b), we have an attribute */
        attr_name = gwlist_get(path_elements,1);
    gwlist_destroy(path_elements, NULL);

    /* split path into parts */
    path_elements = octstr_split(xml_path, octstr_imm("/"));

    /* walk the message tree down the path */
    parent = NULL;
    node = xml;
    index = 0;
    while (index < gwlist_len(path_elements)) {
        int found = 0;
        /* get the next path element */
        temp = gwlist_get(path_elements, index);
        do {
            if (!octstr_str_compare(temp,(char *)node->name)) {
                /* found what we're looking for */
                if (!(node->xmlChildrenNode) && index < (gwlist_len(path_elements)-1)) {
                    /* while this is indeed the item we are looking for, it's not the end
                     * of the path, and this item has no children */
                    debug("bb.soap.fetch_xml_data",0,"SOAP: fetch_xml - error parsing XML, "
                          "looking for <%s>, but element <%s> has no children",
                          octstr_get_cstr(xml_path), octstr_get_cstr(temp));
                } else {
                    ++index; /* go down the path */
                    parent = node; /* remember where I came from */
                    node = node->xmlChildrenNode; /* trace into the node */
                    ++found; /* remember that I found it */
                    break; /* escape to the next level */
                }
            }
            /* get the next node on this level - this runs if the current node is not in the path */
        } while ((node = node->next));

        if (!found) {
            /* didn't find anything - back track */
            node = parent;
            parent = node->parent;
            if (--index < 0)
                /* I backtracked too much up the tree, nowhere to go to */
                break; /* out of the main loop with nothing to show for it */

            if (!(node = node->next))

                /* after back tracking, go over to the next sibling of the node I just
                 * finished searching under, or bail out if no more siblings */
                break;
        }
    }

    /* coming here there are two options:
     * 1) we looped over all the tree, but did not succeed in traveling the
     *    requested path - index not pointing past the list of path elements - */
    if (index < gwlist_len(path_elements)) {
        /* didn't find the full path */
        debug("bb.soap.map_xml_data",0,"SOAP: fetch_xml - path <%s> cannot be traveled in input XML",
              octstr_get_cstr(xml_path));
        gwlist_destroy(path_elements, octstr_destroy_item);
        octstr_destroy(xml_path);
        octstr_destroy(attr_name);
        return NULL;
    }


    /* 2) index is pointing past the end of the path and the correct node
     * is stored in parent */
    if (attr_name) { /* The caller wants to get an attribute */
        xmlChar* content;
        content = xmlGetProp(parent, (xmlChar *)octstr_get_cstr(attr_name));
        if (content)
            temp = octstr_create((char *)content);
        else /* dont treat an empty or non-existant attribute as an error right away */
            temp = octstr_create("");
        xmlFree(content);
    } else { /* the caller wants to get the content */
        xmlChar* content;
        content = xmlNodeGetContent(parent);
        if (content)
            temp = octstr_create((char *)content);
        else /* don't treat an empty tag an error right away */
            temp = octstr_create("");
        xmlFree(content);
    }

    gwlist_destroy(path_elements, octstr_destroy_item);
    octstr_destroy(xml_path);
    octstr_destroy(attr_name);

    return temp;
}

/*
 * function soap_map_xml_data()
 *	maps content of an XML structure to a list of variables using a map
 * Input: XML document and an argument map
 * Returns: number of variables successfuly mapped
 **/
int soap_map_xml_data(xmlNodePtr xml, List* maps)
{
    int mapindex = 0, args = 0;
    xmlNodePtr node, parent;

    /* step through the items on the map */
    while (mapindex < gwlist_len(maps)) {

        Octstr* temp;

        int index = 0;
        ArgumentMap* map = gwlist_get(maps,mapindex);
        /* split the path elements */
        List* path_elements = octstr_split(map->path, octstr_imm("/"));

        /* walk the message tree down the path */
        parent = NULL;
        node = xml;
        while (index < gwlist_len(path_elements)) {
            int found = 0;
            /* get the next path element */
            temp = gwlist_get(path_elements, index);
            do {
                if (!octstr_str_compare(temp,(char *)node->name)) {
                    /* found what we're looking for */
                    if (!(node->xmlChildrenNode) && index < (gwlist_len(path_elements)-1)) {
                        /* while this is indeed the item we are looking for, it's not the end
                           of the path, and this item has no children */
                        debug("bb.soap.map_xml_data",0,"SOAP: error parsing XML, looking for <%s>, but element <%s> has no children",
                              octstr_get_cstr(map->path), octstr_get_cstr(temp));
                    } else {
                        ++index; /* go down the path */
                        parent = node; /* remember where I came from */
                        node = node->xmlChildrenNode; /* trace into the node */
                        ++found;
                        break; /* escape to the next level */
                    }
                }
            } while ((node = node->next));

            if (!found) {
                /* didn't find anything - back track */
                node = parent;
                if (parent==NULL) /* first tag not found, quickly go out ! */
                    return 0;

                parent = node->parent;
                if (--index < 0)
                    /* I backtracked too much up the tree, nowhere to go to */
                    break;

                if (!(node = node->next))
                    /* no more childs under the main tree to look under, abort */
                    break;

            }
        }


        if (index < gwlist_len(path_elements)) {
            /* didn't find the full path */
            debug("bb.soap.map_xml_data",0,"SOAP: didn't find element for keyword <%s> in XML data",
                  octstr_get_cstr(map->name));
            gwlist_destroy(path_elements, octstr_destroy_item);
            ++mapindex;
            continue;
        }

        /* found the correct node (it's stored in parent) */
        if (map->attribute) {

            /* The user wants to get an attribute */
            xmlChar* content;
            content = xmlGetProp(parent, (xmlChar *)octstr_get_cstr(map->attribute));
            if (content)
                temp = octstr_create((char *)content);
            else /* dont treat an empty or non-existant attribute as an error right away */
                temp = octstr_create("");
            xmlFree(content);
        } else {
            /* the user wants to get the content */
            xmlChar* content;
            content = xmlNodeGetContent(parent);
            if (content)
                temp = octstr_create((char *)content);
            else /*  don't treat an empty tag an error right away */

                temp = octstr_create("");
            xmlFree(content);
        }

        /* parse the content using sscan_type from the map */
        octstr_strip_blanks(temp);
        if (!octstr_str_compare(map->sscan_type,"%s")) {
            /* special processing of %s - this means the whole string, while sscanf stops at spaces */
            strcpy(map->store,octstr_get_cstr(temp));

            ++args;
        } else {
            if (!sscanf(octstr_get_cstr(temp), octstr_get_cstr(map->sscan_type), map->store)) {
                debug("bb.soap.map_xml_data",0,"SOAP: failed to scan content '%s' for '%s' in xml parsing",
                      octstr_get_cstr(temp), octstr_get_cstr(map->sscan_type));
            } else {
                ++args;
            }
        }


        /* done for this item */
        octstr_destroy(temp);
        gwlist_destroy(path_elements, octstr_destroy_item);
        ++mapindex;
    }
    return args;
}

/*
 * function soap_release_dependences()
 * check for each key if we need specific convertation and do it
 * Input:  specification of dependences to convert, map with all keys, msg structure to change values
 * Returns: error code or 0 on success
 **/
long soap_release_dependences(Octstr* file_deps, List* lstmaps, Msg* msg, PrivData *privdata)
{
    List *issues;
    long i, j, key_index, key_deps_index, map_index;
    int res, k;
    List *issue_items, *header_item;
    int key_func_index;
    ArgumentMap* map;
    Octstr *header, *key, *key_deps;
    Octstr *func_alias = NULL, *block;

    /* follows  keys and funcs identifiers must be
     * the same as in a 'deps' file
     **/


    /*	structure of file_deps;
     *
     *	<key> <key_deps>
     *	<key_deps_value> <function_alias>
     *	<key_deps_value> <function_alias>
     *	;
     */

    /* ADD HERE: */

    char* funcs[][5] = {    /* functions aliasis used in mo.deps file */
                           {"text","binary","unicode","default"},	                  /*  msgtype */
                           {"set_iso","64_binary","hex_binary","unicode","default"}  /* msgdata */
                       };


    issues = octstr_split(file_deps, octstr_imm(";"));					/* get paragraphs */

    if (gwlist_len(issues) == 0) {
        error(0, "SOAP: soap_release_dependences, empty or broken 'deps' file");
        return -1;
    }

    for (i=0; i<gwlist_len(issues); ++i)						            /* loop paragraphs */
    {
        block = gwlist_get(issues, i);
        octstr_strip_crlfs(block);
        octstr_strip_blanks(block);

        issue_items = octstr_split(block, octstr_imm("\n"));
        if (gwlist_len(issue_items) < 2) {
            error(0, "SOAP: soap_release_dependences, broken file 'deps' can't find any definition for <key>");
            gwlist_destroy(issue_items, octstr_destroy_item);
            gwlist_destroy(issues, octstr_destroy_item);
            return -1;
        }


        header = gwlist_extract_first(issue_items);
        header_item = octstr_split_words(header);				/* header content */
        O_DESTROY(header);

        if (gwlist_len(header_item) < 2) {
            error(0, "SOAP: soap_release_dependences, broken 'deps' file in <key> <key_deps> part");
            gwlist_destroy(header_item, octstr_destroy_item);
            gwlist_destroy(issue_items, octstr_destroy_item);
            gwlist_destroy(issues, octstr_destroy_item);
            return -1;
        }

        key      = gwlist_get(header_item, 0);
        key_deps = gwlist_get(header_item, 1);
        key_index      = soap_get_index(lstmaps, key, 0);      /* search key_index */
        key_deps_index = soap_get_index(lstmaps, key_deps, 0); /* search key_deps_index, from what depends */

        if (key_index == -1 || key_deps_index == -1) {
            gwlist_destroy(header_item, octstr_destroy_item);
            gwlist_destroy(issue_items, octstr_destroy_item);
            gwlist_destroy(issues, octstr_destroy_item);
            return -1;
        }

        map_index = soap_get_index(lstmaps, key_deps, 1); /* get index for map->name==key_deps */
        map = gwlist_get(lstmaps, map_index);

        /* search <function_identifier> and if not found try to set default */
        for (j=0; j < gwlist_len(issue_items); ++j) {

            Octstr *tmp = gwlist_get(issue_items, j);
            List *row = octstr_split_words(tmp);

            if (!octstr_str_compare(gwlist_get(row, 0), map->store)) {
                func_alias = octstr_duplicate(gwlist_get(row, 1));
                gwlist_destroy(row, octstr_destroy_item);
                break;
            }

            if (j==gwlist_len(issue_items)-1) {
                error(0, "SOAP: soap_release_dependences, \
                      can't find function_alias for <%s> in 'deps' file, set default", (char*)map->store);
                func_alias = octstr_create(SPEC_DEFAULT);
            }
            gwlist_destroy(row, octstr_destroy_item);
        }

        key_func_index = -1;
        /* searching index of function by its alias */
        for (k=0; k < sizeof(funcs[key_index])/sizeof(funcs[key_index][0]); ++k)
        {
            if (!octstr_str_compare(func_alias, funcs[key_index][k])) {
                key_func_index = k;
                break;
            }
        }
        if (key_func_index==-1)
            error(0, "SOAP: soap_release_dependences, can't find function for alias <%s>", octstr_get_cstr(func_alias));

        O_DESTROY(func_alias);

        gwlist_destroy(header_item, octstr_destroy_item);
        gwlist_destroy(issue_items, octstr_destroy_item);

        /* which field has deps, which func need be called, msg need be changed */
        if ((res=soap_process_deps(key_index, key_func_index, msg, privdata)) < 0)
            error(0, "SOAP: soap_release_dependences, error processing dependent value");
    }
    gwlist_destroy(issues, octstr_destroy_item);


    return 0; /* OK */
}

/*
 * function soap_process_deps
 * select function for selected <key>
 **/
int soap_process_deps(int key_index, int key_func_ind, Msg* msg, PrivData *privdata)
{
    /* ADD HERE: MO 'key' functions */
    switch (key_index)
    {
        case 0:
            return soap_msgtype_deps(key_func_ind, msg);
        case 1:
            return soap_msgdata_deps(key_func_ind, msg, privdata);
        default:
            return -1;
    }
    return -1;
}

/*
* function soap_msgtype_deps
* release dependences fot all types of specific coding
**/
int soap_msgtype_deps(int key_func_index, Msg* msg)
{
    /* {"text","binary","unicode","default"}	msgtype  */

    /* ADD HERE: see order in funcs[][] */
    switch (key_func_index)
    {
        case 0:									/* "text" */
            msg->sms.coding = DC_7BIT;
            break;
        case 1:									/* "binary" */
            msg->sms.coding = DC_8BIT;
            break;

        case 2:									/* "unicode" */
        case 3:									/* "default" == unicode */
            msg->sms.coding = DC_UCS2;
            break;
        default:
            /* out of range */
            error(0, "SOAP: soap_msgtype_deps, unknown index %d", key_func_index);
            return -1;
    }
    return 0;
}

int soap_msgdata_deps(int key_func_index, Msg* msg, PrivData *privdata)
{
    int ret = 0;
    /* {"set_iso","64_binary","hex_binary","unicode","default"}  msgdata */

    /* ADD HERE: */
    switch (key_func_index)
    {
        case 0:       /* "set_iso" */
            msg->sms.charset = octstr_create("ISO-8859-1");
            break;
        case 1:      /* "64_binary" */
            octstr_base64_to_binary(msg->sms.msgdata);
            break;
        case 2:			 /* "hex_binary" */
            octstr_hex_to_binary(msg->sms.msgdata);
            break;

        case 3:			/* "unicode" */
        case 4:			/* "default" */

            if (!octstr_case_compare(privdata->alt_charset, octstr_imm("UCS-2"))) {
                debug("bb.soap.msgdata_deps",0,"SOAP[%s]: converting from %s to UCS-2BE",
                      octstr_get_cstr(privdata->name), octstr_get_cstr(privdata->alt_charset));
                ret = charset_convert(msg->sms.msgdata, octstr_get_cstr(privdata->alt_charset), "UCS-2BE");

                if (ret == -1) {

                    error(2,"SOAP[%s]: Error converting MO data from %s to unicode",
                          octstr_get_cstr(privdata->name), octstr_get_cstr(privdata->alt_charset));
                }
            }
            else if (ret != 0) {
                debug("bb.soap.parse_mo",1,"SOAP[%s]: charset_convert made %d irreversable transformations",
                      octstr_get_cstr(privdata->name), ret);
            }
            msg->sms.charset = octstr_create("UCS-2");

            debug("bb.soap.parse_mo",0,"SOAP[%s]: message decoded -", octstr_get_cstr(privdata->name));
            octstr_dump(msg->sms.msgdata, 0);
            break;

        default:
            /* out of range */

            error(0, "SOAP: soap_msgdata_deps, unknown index %d", key_func_index);
            return -1;
    }
    return 0;
}


/* MT spec processing */
/* return index of function alias in array of aliasis for functions
*  used in MT XML file to fill up specific parameters in XML doc
*/
int soap_lookup_function(Octstr* funcname)
{
    int i;

    /* ADD HERE: XML functions aliasis */
    char *aliasis[] = {
                          "bouyg_content", "mobitai_content",
                          "o2o_msgdata", "msgdata",
                          "o2o_validity30", "mobitai_validity_date", "bouyg_validity",
                          "o2o_date", "mobitai_date", "rand",
                          "o2o_dlrmask_smsc_yn", "o2o_dlrmask_success_01"
                      };

    for (i=0; i<sizeof(aliasis)/sizeof(aliasis[0]); ++i)
    {
        if (!octstr_str_compare(funcname, aliasis[i]))
            return i;
    }
    return -1;
}

/*
* select function by index
* follow the order defined in array of aliasis in look_up_function()
* return Octstr value
*/
Octstr* soap_select_function(int index, Msg* msg, PrivData* privdata)
{
    /* ADD HERE: XML functions  */
    switch (index)
    {
        case 0:
            return soap_bouyg_content_attribute(msg);
        case 1:
            return soap_mobitai_content_attribute(msg);
        case 2:
            return soap_o2o_msgdata_attribute(msg, privdata);
        case 3:
            return soap_msgdata_attribute(msg, privdata);			/* mobitai/bouyg */
        case 4:
            return soap_o2o_validity30_attribute(msg);
        case 5:
            return soap_mobitai_validity_date_attribute(msg);
        case 6:
            return soap_bouyg_validity_attribute(msg);
        case 7:
            return soap_o2o_date_attribute(msg);
        case 8:
            return soap_mobitai_date_attribute(msg);
        case 9:
            return soap_rand_attribute(msg);
        case 10:
            return soap_o2o_dlrmask_smsc_yn_attribute(msg);
        case 11:
            return soap_o2o_dlrmask_success_01_attribute(msg);
        default:
            error(0,"SOAP: soap_select_function can't find function");
            return NULL;
    }
}

/* set of MT msg structure to XML specific converting functions */

Octstr* soap_bouyg_content_attribute(Msg* msg)
{

    if (msg->sms.coding == DC_8BIT)
        return octstr_create("D");
    else
        return octstr_create("A");
}

Octstr* soap_mobitai_content_attribute(Msg* msg)
{
    if (msg->sms.coding == DC_8BIT)
        return octstr_create("binary");
    else
        return octstr_create("text");

}

Octstr* soap_o2o_msgdata_attribute(Msg* msg, PrivData *privdata)
{
    Octstr *data, *res, *udhres;
    int ret;

    data = octstr_duplicate(msg->sms.msgdata);

    if (msg->sms.coding == DC_8BIT) {
        debug("bb.soap.o2o_msgdata_attribute",0,"SOAP: base 64 encoding");
        octstr_binary_to_base64(data);
        res = octstr_format("<Control_Data>%S</Control_Data>", data);

        if (octstr_len(msg->sms.udhdata) > 0) { /* add UDH */
            O_DESTROY(data);
            data = octstr_duplicate(msg->sms.udhdata);
            debug("bb.soap.o2o_msgdata_attribute",0,"SOAP: UDH base 64 encoding");
            octstr_binary_to_base64(data);
            udhres = octstr_format("<UDH>%S</UDH>", data);
            octstr_append(res, udhres);
            O_DESTROY(udhres);
        }
        else {
            error(0, "SOAP: o2o_msgdata_attribute, UDH not defined");
            udhres = octstr_create("<UDH></UDH>");
            octstr_append(res, udhres);
            O_DESTROY(udhres);
        }
        O_DESTROY(data);
        return res;
    }
    else if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) {
        /* convert message data to target encoding */
        debug("bb.soap.o2o_msgdata_attribute", 0, "SOAP: converting from UTF-8 to %s", octstr_get_cstr(privdata->alt_charset));
        ret = charset_convert(data, "UTF-8", octstr_get_cstr(privdata->alt_charset));
        if (ret == -1) {
            error(0,"SOAP: soap_o2o_msgdata_attribute, charset_convert failed");
            octstr_dump(msg->sms.msgdata, 0);
            O_DESTROY(data);
            return NULL;
        }
        debug("bb.soap.o2o_msgdata_attribute",0,"SOAP: converting to HTML entities");
        octstr_convert_to_html_entities(data);
        res = octstr_format("<Message_Text>%S</Message_Text>", data);
        O_DESTROY(data);
        return res;
    }
    else if (msg->sms.coding == DC_UCS2) {
        /* convert message data to target encoding */
        debug("bb.soap.o2o_msgdata_attribute", 0, "converting from USC-2 to %s", octstr_get_cstr(privdata->alt_charset));
        ret = charset_convert(msg->sms.msgdata, "UCS-2BE", octstr_get_cstr(privdata->alt_charset));
        if (ret == -1) {
            error(0,"SOAP: soap_o2o_msgdata_attribute, charset_convert failed");

            octstr_dump(msg->sms.msgdata, 0);
            O_DESTROY(data);
            return NULL;
        }
        res = octstr_format("<Message_Text>%s</Message_Text>", data);
        O_DESTROY(data);
        return res;
    }

    else {
        error(0,"SOAP: soap_o2o_msgdata_attribute, unknown coding: %ld", msg->sms.coding);
        O_DESTROY(data);
        return NULL;

    }
}

/* mobitai/bouyg */
Octstr* soap_msgdata_attribute(Msg* msg, PrivData* privdata)
{
    Octstr *data, *udhdata;
    int ret;


    data = octstr_duplicate(msg->sms.msgdata);

    if (msg->sms.coding == DC_8BIT) {
        udhdata = octstr_duplicate(msg->sms.udhdata);
        octstr_append(udhdata, data);
        octstr_binary_to_hex(udhdata, 1);
        O_DESTROY(data);
        return udhdata;
    }
    else if (msg->sms.coding == DC_7BIT || msg->sms.coding == DC_UNDEF) {
        /* convert message data to target encoding */
        debug("bb.soap.msgdata_attribute", 0, "SOAP: converting from UTF-8 to %s", octstr_get_cstr(privdata->alt_charset));
        ret = charset_convert(data, "UTF-8", octstr_get_cstr(privdata->alt_charset));
        if (ret == -1) {
            error(0,"SOAP: soap_msgdata_attribute, charset_convert failed");
            octstr_dump(msg->sms.msgdata, 0);
            O_DESTROY(data);
            return NULL;
        }
        debug("bb.soap.msgdata_attribute",0,"SOAP: converting to HTML entities");
        octstr_convert_to_html_entities(data);
        return data;
    }
    else if (msg->sms.coding == DC_UCS2) {
        /* convert message data to target encoding */
        debug("bb.soap.msgdata_attribute", 0, "converting from USC-2 to %s", octstr_get_cstr(privdata->alt_charset));
        ret = charset_convert(data, "UCS-2BE", octstr_get_cstr(privdata->alt_charset));
        if (ret == -1) {
            error(0,"SOAP: soap_msgdata_attribute, charset_convert failed");

            octstr_dump(data, 0);
            O_DESTROY(data);
            return NULL;
        }
        return data;
    }
    else {
        error(0,"SOAP: soap_msgdata_attribute, unknown coding: %ld", msg->sms.coding);
        O_DESTROY(data);
        return NULL;
    }
}

/* validity in 30 minutes increment */
Octstr* soap_o2o_validity30_attribute(Msg* msg)
{
    return octstr_format("%ld",(msg->sms.validity != SMS_PARAM_UNDEFINED ? (msg->sms.validity - time(NULL))/60 : SOAP_DEFAULT_VALIDITY) / 30);
}

/* date on which the message's validity expires */
Octstr* soap_mobitai_validity_date_attribute(Msg* msg)
{
    return date_create_iso(msg->sms.validity);
}

/* validity in seconds */
Octstr* soap_bouyg_validity_attribute(Msg* msg)
{
    return octstr_format("%d", msg->sms.validity - time(NULL));
}

Octstr* soap_o2o_date_attribute(Msg* msg)
{
    return soap_write_date(msg->sms.time);
}


/* TIMESTAMP */
Octstr* soap_mobitai_date_attribute(Msg* msg)
{
    return date_create_iso(msg->sms.time);
}

Octstr* soap_rand_attribute(Msg* msg)
{
    return octstr_format("%d",gw_rand());
}

/* "Y" for any of the SMSC generated DLRs, "N" otherwise */
Octstr* soap_o2o_dlrmask_smsc_yn_attribute(Msg* msg)
{
    return octstr_create(DLR_IS_ENABLED_SMSC(msg->sms.dlr_mask) ? "Y" : "N");
}

/* "1" for any of the SMSC generated DLRs, "0" otherwise */
Octstr* soap_o2o_dlrmask_success_01_attribute(Msg* msg)
{
    return octstr_create( DLR_IS_SUCCESS(msg->sms.dlr_mask) ? "0" : "1");
}

/*
 * looking for key in the map names collection to find key_index
 * if map_index==1 i like to know index in the map 'where' for name 'key'
 */
int soap_get_index(List* where, Octstr* key, int map_index)
{
    int i, j;
    ArgumentMap* map;

    /* ADD HERE: */
    /* key and key_deps values as defined in mo.deps */
    char* funcs_deps[] = {
                             "msgtype", "msgdata"
                         };

    for (i=0; i < gwlist_len(where); ++i) {
        map = gwlist_get(where, i);
        if (!octstr_compare(map->name, key)) {
            if (map_index==1) /* return index from the list where found name */
                return i;

            for (j=0; j < sizeof(funcs_deps)/sizeof(funcs_deps[0]); ++j) {
                if (!octstr_str_compare(map->name, funcs_deps[j]))
                    return j;
            }
        }
    }
    error(0, "SOAP: soap_get_index, broken 'deps' file, can't find key <%s> ", octstr_get_cstr(key));
    return -1;
}


