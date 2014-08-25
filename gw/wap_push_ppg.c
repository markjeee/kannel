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
 * wap_push_ppg.c: General logic of a push proxy gateway.
 *
 * This module implements following WAP Forum specifications:
 *      WAP-151-PPGService-19990816-a (called afterwards PPG),
 *      WAP-164-PAP-19991108-a (PAP),
 *      WAP-164_100-PAP-20000218-a (PAP implementation note).
 * 
 * We refer following WAP Forum specifications:
 *      WAP-145-PushMessage-19990816-a (push message)
 *      WAP-200-WDP-20001212-a (WDP)
 *      WAP-203-WSP-20000504-a (WSP)
 *      WAP-189-PushOTA-20000217-a (OTA).
 *
 * In addition, RFCs 1521 and 2045 are referred.
 *
 * By Aarno SyväŠnen for Wapit Ltd, Wiral Ltd and Global Networks Inc. 
 */

#include <time.h>
#include <ctype.h>

#include "wap_push_ppg.h"
#include "wap/wap_events.h"
#include "wap/wsp_caps.h"
#include "wml_compiler.h"
#include "wap-appl.h"
#include "wap/wsp.h"
#include "wap/wsp_strings.h"
#include "wap_push_si_compiler.h"
#include "wap_push_sl_compiler.h"
#include "wap_push_pap_compiler.h"
#include "wap_push_pap_mime.h"
#include "wap_push_ppg_pushuser.h"

enum {
    TIME_EXPIRED = 0,
    TIME_TOO_EARLY = 1,
    NO_CONSTRAINTS = 2
};

/*
 * Default values for configuration variables
 */
enum {
    DEFAULT_HTTP_PORT = 8080,
    NO_HTTPS_PORT = -1,
    DEFAULT_NUMBER_OF_PUSHES = 100,
    PI_TRUSTED = 1,
    SSL_CONNECTION_OFF = 0,
    DEFAULT_NUMBER_OF_USERS = 1024,
    USER_CONFIGURATION_NOT_ADDED = 0
};

enum { USER_CONFIGURATION_ADDED = 1 };

#define DEFAULT_PPG_URL "/wappush"

/*****************************************************************************
 *
 * Internal data structures
 *
 * Give the status of the push ppg module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum {limbo, running, terminating} run_status = limbo;

/*
 * The external event queue for this module
 */
static List *ppg_queue = NULL;

/*
 * The internal event queue for this module (allowing listening of many ports)
 */
static List *pap_queue = NULL;

/*
 * List of ppg session machines (it is, of currently active sessions)
 */
static List *ppg_machines = NULL;

/*
 * List of currently active unit pushes (we need a threadsafe storage for them,
 * because pushes can be cancelled and queried):
 */
static List *ppg_unit_pushes = NULL;

/*
 * Counter to store our internal push id.
 */
static Counter *push_id_counter = NULL;

/*
 * We need a mapping between HTTPClient structures, used by http library, and
 * push ids, used by ppg. 
 */
static Dict *http_clients = NULL;

/*
 * Mapping between urls used by pi and push ids used by ppg.
 */
static Dict *urls = NULL;

/*
 * Push content packed for compilers (wml, si, sl, co).
 */
struct content {
    Octstr *body;
    Octstr *type;
    Octstr *charset;
};

static wap_dispatch_func_t *dispatch_to_ota;
static wap_dispatch_func_t *dispatch_to_appl;

/*
 * Configurable variables of ppg core group (for general configuration of a 
 * ppg), with some default values.
 */

static Octstr *ppg_url = NULL ;
static long ppg_port = DEFAULT_HTTP_PORT;

#ifdef HAVE_LIBSSL
static long ppg_ssl_port = NO_HTTPS_PORT;
#endif

static long number_of_pushes = DEFAULT_NUMBER_OF_PUSHES;
static int trusted_pi = PI_TRUSTED;
static long number_of_users = DEFAULT_NUMBER_OF_USERS;
static Octstr *ppg_deny_ip = NULL;
static Octstr *ppg_allow_ip = NULL; 
static int user_configuration = USER_CONFIGURATION_NOT_ADDED;
static Octstr *global_sender = NULL;
static Octstr *ppg_default_smsc = NULL; 
#ifdef HAVE_LIBSSL
static Octstr *ssl_server_cert_file = NULL;
static Octstr *ssl_server_key_file = NULL;
#endif
static Octstr *ppg_dlr_url = NULL;
static Octstr *ppg_smsbox_id = NULL; 
static Octstr *service_name = NULL;

struct PAPEvent {
    HTTPClient *client;
    Octstr *ip; 
    Octstr *url;
    List *push_headers; 
    Octstr *mime_content;
    List *cgivars;
};

typedef struct PAPEvent PAPEvent;


/*****************************************************************************
 *
 * Prototypes of internal functions
 *
 * Event handling
 */
static void ota_read_thread(void *arg);
static void http_read_thread(void *arg);

#ifdef HAVE_LIBSSL
static void https_read_thread(void *arg);
#endif

static void handle_internal_event(WAPEvent *e);
static void pap_request_thread(void *arg);
static int handle_push_message(HTTPClient **c, WAPEvent *ppg_event, int status);
static PAPEvent *pap_event_create(Octstr *ip, Octstr *url, List *push_headers, 
                                  Octstr *mime_content, List *cgivars,
                                  HTTPClient *client);
static void pap_event_destroy(PAPEvent *p);
static void pap_event_destroy_item(void *p);
static void pap_event_unpack(PAPEvent *p, Octstr **ip, Octstr **url, 
                             List **push_headers, Octstr **mime_content, 
                             List **cgivars, HTTPClient **client);

/*
 * Constructors and destructors for machines.
 */
static PPGSessionMachine *session_machine_create(WAPAddrTuple *tuple, 
                                                     WAPEvent *e);
static void session_machine_destroy(void *p);
static PPGPushMachine *push_machine_create(WAPEvent *e, 
    WAPAddrTuple *tuple);
static void push_machine_destroy(void *pm);
static void push_machines_list_destroy(List *pl);

/*
 * Communicating other modules (ota and appl)
 */
static void create_session(WAPEvent *e, PPGPushMachine *pm);
static void request_confirmed_push(long last, PPGPushMachine *pm, 
                                   PPGSessionMachine *sm);
static void request_unit_push(long last, PPGPushMachine *pm);
static void request_push(long last, PPGPushMachine *sm);
static int response_push_connection(WAPEvent *e, PPGSessionMachine *sm);
static HTTPClient *response_push_message(PPGPushMachine *pm, long code, 
                                         int status);

/*
 * Functions to find machines using various identifiers, and related help 
 * functions.
 */
static PPGSessionMachine *session_find_using_pi_client_address(Octstr *addr);
static PPGPushMachine *find_ppg_push_machine_using_pid(PPGSessionMachine *sm, 
                                                   long pid);
static PPGPushMachine *find_ppg_push_machine_using_pi_push_id(
    PPGSessionMachine *sm, Octstr *pi_push_id);
static PPGPushMachine *find_unit_ppg_push_machine_using_pi_push_id(
    Octstr *pi_push_id);
static int push_has_pi_push_id(void *a, void *b);
static int push_has_pid(void *a, void *b);
static int session_has_pi_client_address(void *a, void *b);
static int session_has_addr(void *a, void *b);
static int session_has_sid(void *a, void *b);

/*
 * Main logic of PPG.
 */
static int check_capabilities(List *requested, List *assumed);
static int transform_message(WAPEvent **e, WAPAddrTuple **tuple, 
                             List *push_headers, int connected, Octstr **type);
static long check_x_wap_application_id_header(List **push_headers);
static int pap_convert_content(struct content *content);
static int pap_get_content(struct content *content);
static int select_bearer_network(WAPEvent **e);
static int delivery_time_constraints(WAPEvent *e, PPGPushMachine *pm);
static void deliver_confirmed_push(long last, PPGPushMachine *pm, 
                                   PPGSessionMachine *sm);
static PPGPushMachine *deliver_unit_push(long last, PPGPushMachine *pm,
    PPGSessionMachine *sm, int session_exists);
static int store_push_data(PPGPushMachine **pm, PPGSessionMachine *sm, 
                           WAPEvent *e, WAPAddrTuple *tuple, int cless);
static PPGPushMachine *update_push_data_with_attribute(PPGSessionMachine **sm, 
    PPGPushMachine *pm, long reason, long status);
static void remove_push_data(PPGSessionMachine *sm, PPGPushMachine *pm, 
                             int cless);
static void remove_session_data(PPGSessionMachine *sm, int status);
static void remove_pushless_session(PPGSessionMachine *sm);
static PPGSessionMachine *store_session_data(PPGSessionMachine *sm,
    WAPEvent *e, WAPAddrTuple *tuple, int *session_exists);
static PPGSessionMachine *update_session_data_with_headers(
    PPGSessionMachine *sm, PPGPushMachine *pm);
static void deliver_pending_pushes(PPGSessionMachine *sm, int last);
static PPGPushMachine *abort_delivery(PPGSessionMachine *sm, int status);
static PPGSessionMachine *update_session_data(PPGSessionMachine *sm, long sid,
                                              long port, List *caps);
static int confirmation_requested(WAPEvent *e);
static int cless_accepted(WAPEvent *e, PPGSessionMachine *sm);

/*
 * Header functions
 */
static int headers_acceptable(List *push_headers, Octstr **content_header);
static int type_is(Octstr *content_header, char *required_type);
static int get_mime_boundary(List *push_headers, Octstr *content_header, 
                             Octstr **boundary);
static void change_header_value(List **push_headers, char *name, char *value);
static void remove_mime_headers(List **push_headers);
static void remove_link_headers(List **push_headers);
static void remove_x_kannel_headers(List **push_headers);

/*
 * Communicating with pi.
 */
static void send_bad_message_response(HTTPClient **c, Octstr *body_fragment,
                                      int code, int status);
static HTTPClient *send_push_response(WAPEvent *e, int status);
static void send_to_pi(HTTPClient **c, Octstr *reply_body, int status);
static void tell_fatal_error(HTTPClient **c, WAPEvent *e, Octstr *url, 
                             int status, int code);

/*
 * PPG core authentication (not related to any particular user).
 */
static int read_ppg_config(Cfg *cfg);
static int ip_allowed_by_ppg(Octstr *ip);

/*
 * Interface to various compilers
 */
static Octstr *convert_wml_to_wmlc(struct content *content);
static Octstr *convert_si_to_sic(struct content *content);
static Octstr *convert_sl_to_slc(struct content *content);

/*
 * Setting values for controlling sms level. (Pap control document enables 
 * some control, but not enough.)
 */

static Octstr *set_smsc_id(List *headers, Octstr *username, int trusted_pi);
static Octstr *set_dlr_url(List *headers, Octstr *username, int trusted_pi);
static long set_dlr_mask(List *headers, Octstr *dlr_url);
static Octstr *set_smsbox_id(List *headers, Octstr *username, int trusted_pi);
static Octstr *set_service_name(void);

/*
 * Various utility functions
 */
static Octstr *set_time(void);
static int deliver_before_test_cleared(Octstr *before, struct tm now);
static int deliver_after_test_cleared(Octstr *after, struct tm now);
static void session_machine_assert(PPGSessionMachine *sm);
static void push_machine_assert(PPGPushMachine *pm);
static Octstr *tell_ppg_name(void);
static Octstr *describe_code(long code);
static long ota_abort_to_pap(long reason);
static int content_transformable(List *push_headers);
static WAPAddrTuple *set_addr_tuple(Octstr *address, long cliport, long servport,
                                    long address_type, List *push_headers);
static WAPAddrTuple *addr_tuple_change_cliport(WAPAddrTuple *tuple, long port);
static void initialize_time_item_array(long time_data[], struct tm now);
static int date_item_compare(Octstr *before, long time_data, long pos);
static long parse_appid_header(Octstr **assigned_code);
static Octstr *escape_fragment(Octstr *fragment);
static int coriented_deliverable(long code);
static int is_phone_number(long type_of_address);
static void replace_octstr_char(Octstr *os1, Octstr *os2, long *pos);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS
 */

enum {
    TYPE_HTTP = 0,
    TYPE_HTTPS = 1
};

void wap_push_ppg_init(wap_dispatch_func_t *ota_dispatch, 
                       wap_dispatch_func_t *appl_dispatch, Cfg *cfg)
{
    user_configuration = read_ppg_config(cfg);
    if (user_configuration != USER_CONFIGURATION_NOT_ADDED) {
        ppg_queue = gwlist_create();
        gwlist_add_producer(ppg_queue);
        pap_queue = gwlist_create();
        gwlist_add_producer(pap_queue);
        push_id_counter = counter_create();
        ppg_machines = gwlist_create();
        ppg_unit_pushes = gwlist_create();

        dispatch_to_ota = ota_dispatch;
        dispatch_to_appl = appl_dispatch;

        http_open_port(ppg_port, TYPE_HTTP);
#ifdef HAVE_LIBSSL
        if (ppg_ssl_port != NO_HTTPS_PORT)
            http_open_port(ppg_ssl_port, TYPE_HTTPS);
#endif
        http_clients = dict_create(number_of_pushes, NULL);
        urls = dict_create(number_of_pushes, NULL);

        gw_assert(run_status == limbo);
        run_status = running;
        gwthread_create(ota_read_thread, NULL);
        gwthread_create(http_read_thread, NULL);
#ifdef HAVE_LIBSSL
        if (ppg_ssl_port != NO_HTTPS_PORT) 
            gwthread_create(https_read_thread, NULL);
#endif
        gwthread_create(pap_request_thread, NULL);
    }
}

void wap_push_ppg_shutdown(void)
{
     if (user_configuration != USER_CONFIGURATION_NOT_ADDED) {
         gw_assert(run_status == running);
         run_status = terminating;
         gwlist_remove_producer(ppg_queue);
         gwlist_remove_producer(pap_queue);
         octstr_destroy(ppg_url);
         ppg_url = NULL;
         http_close_all_ports();
         dict_destroy(http_clients);
         dict_destroy(urls);
         wap_push_ppg_pushuser_list_destroy();
         octstr_destroy(ppg_deny_ip);
         octstr_destroy(ppg_allow_ip);
         octstr_destroy(global_sender);
         octstr_destroy(service_name);
         octstr_destroy(ppg_default_smsc);
         octstr_destroy(ppg_dlr_url);
         octstr_destroy(ppg_smsbox_id);

         gwthread_join_every(http_read_thread);
#ifdef HAVE_LIBSSL
         if (ppg_ssl_port != NO_HTTPS_PORT)
            gwthread_join_every(https_read_thread);
#endif
         gwthread_join_every(ota_read_thread);
         gwthread_join_every(pap_request_thread);

         gwlist_destroy(ppg_queue, wap_event_destroy_item);
         gwlist_destroy(pap_queue, pap_event_destroy_item);
         counter_destroy(push_id_counter);
     
         debug("wap.push.ppg", 0, "PPG: %ld push session machines left.",
               gwlist_len(ppg_machines));
         gwlist_destroy(ppg_machines, session_machine_destroy);

         debug("wap_push_ppg", 0, "PPG: %ld unit pushes left", 
               gwlist_len(ppg_unit_pushes));
         gwlist_destroy(ppg_unit_pushes, push_machine_destroy);
     }
}

void wap_push_ppg_dispatch_event(WAPEvent *e)
{
    gw_assert(run_status == running);
    gwlist_produce(ppg_queue, e);
}

/*
 * We cannot know port the client is using when it establish the connection.
 * However, we must link session creation with a pending push request. Only
 * data available is the client address, so we check it here.
 * Return non-NULL (pointer to the session machine found), if we have one.
 */
PPGSessionMachine *wap_push_ppg_have_push_session_for(WAPAddrTuple *tuple)
{
    PPGSessionMachine *sm;

    gw_assert(tuple);
    sm = gwlist_search(ppg_machines, tuple->remote->address, session_has_addr);

    return sm;
}

/*
 * Now initiators are identified by their session id. Return non-NULL (pointer
 * to the session machine found), if we have one. This function are used after 
 * wsp has indicated session establishment, giving us a session id.
 */
PPGSessionMachine *wap_push_ppg_have_push_session_for_sid(long sid)
{
    PPGSessionMachine *sm;

    gw_assert(sid >= 0);
    sm = gwlist_search(ppg_machines, &sid, session_has_sid);

    return sm;
}

/*****************************************************************************
 *
 * INTERNAL FUNCTIONS
 *
 * Read general ppg configuration and configuration specific for users (to the
 * list 'users').
 * Return 1 when an user ppg group is present, 0 otherwise (and panic
 * if we have not trusted ppg and no user groups).
 */

static int read_ppg_config(Cfg *cfg)
{
     CfgGroup *grp;
     List *list;

     if (cfg == NULL)
         return USER_CONFIGURATION_NOT_ADDED;

     grp = cfg_get_single_group(cfg, octstr_imm("ppg"));
     if ((ppg_url = cfg_get(grp, octstr_imm("ppg-url"))) == NULL)
         ppg_url = octstr_imm("/wappush");
     cfg_get_integer(&ppg_port, grp, octstr_imm("ppg-port"));
     cfg_get_integer(&number_of_pushes, grp, octstr_imm("concurrent-pushes"));
     cfg_get_bool(&trusted_pi, grp, octstr_imm("trusted-pi"));
     cfg_get_integer(&number_of_users, grp, octstr_imm("users"));
     ppg_deny_ip = cfg_get(grp, octstr_imm("ppg-deny-ip"));
     ppg_allow_ip = cfg_get(grp, octstr_imm("ppg-allow-ip"));
     if ((global_sender = cfg_get(grp, octstr_imm("global-sender"))) == NULL)
         global_sender = octstr_format("%s", "1234");
     ppg_default_smsc = cfg_get(grp, octstr_imm("default-smsc"));
     ppg_dlr_url = cfg_get(grp, octstr_imm("default-dlr-url"));
     ppg_smsbox_id = cfg_get(grp, octstr_imm("ppg-smsbox-id"));
     if ((service_name = cfg_get(grp, octstr_imm("service-name"))) == NULL)
         service_name = octstr_format("%s", "ppg");
   
#ifdef HAVE_LIBSSL
     cfg_get_integer(&ppg_ssl_port, grp, octstr_imm("ppg-ssl-port"));
     ssl_server_cert_file = cfg_get(grp, octstr_imm("ssl-server-cert-file"));
     ssl_server_key_file = cfg_get(grp, octstr_imm("ssl-server-key-file"));
     if (ppg_ssl_port != NO_HTTPS_PORT) {
        if (ssl_server_cert_file == NULL || ssl_server_key_file == NULL) 
            panic(0, "cannot continue without server cert and/or key files");
        use_global_server_certkey_file(ssl_server_cert_file, ssl_server_key_file);
     }        
     octstr_destroy(ssl_server_cert_file);
     octstr_destroy(ssl_server_key_file);
#endif

     /* If pi is trusted, ignore possible user groups. */
     if (trusted_pi) {
        cfg_destroy(cfg);
        return USER_CONFIGURATION_ADDED;
     }

     /* But if it is not, we cannot continue without user groups.*/
     if ((list = cfg_get_multi_group(cfg, octstr_imm("wap-push-user")))
              == NULL) {
         panic(0, "No user group but ppg not trusted, stopping");
         gwlist_destroy(list, NULL);
         cfg_destroy(cfg); 
         return USER_CONFIGURATION_NOT_ADDED;
     }
    
     if (!wap_push_ppg_pushuser_list_add(list, number_of_pushes, 
                                         number_of_users)) {
         panic(0, "unable to create users configuration list, exiting");
         return USER_CONFIGURATION_NOT_ADDED;     
     }  

     cfg_destroy(cfg); 
     return USER_CONFIGURATION_ADDED;
}

static int ip_allowed_by_ppg(Octstr *ip)
{
    if (ip == NULL)
        return 0;    

    if (trusted_pi)
        return 1;

    if (ppg_deny_ip == NULL && ppg_allow_ip == NULL) {
        warning(0, "Your ppg core configuration lacks allowed and denied" 
                   " ip lists");
        return 1;
    }

    if (ppg_deny_ip)
        if (octstr_compare(ppg_deny_ip, octstr_imm("*.*.*.*")) == 0) {
            panic(0, "Your ppg core configuration deny all ips, exiting");
            return 0;
        }

    if (ppg_allow_ip)
        if (octstr_compare(ppg_allow_ip, octstr_imm("*.*.*.*")) == 0) {
            warning(0, "Your ppg core configuration allow all ips");
            return 1;
        }

    if (ppg_deny_ip)
        if (wap_push_ppg_pushuser_search_ip_from_wildcarded_list(ppg_deny_ip, ip, 
	        octstr_imm(";"), octstr_imm("."))) {
            error(0, "ip found from denied list");
            return 0;
        }

    if (ppg_allow_ip)
        if (wap_push_ppg_pushuser_search_ip_from_wildcarded_list(ppg_allow_ip, ip, 
	        octstr_imm(";"), octstr_imm("."))) {
            debug("wap.push.ppg.pushuser", 0, "PPG: ip_allowed_by_ppg: ip found"
                  " from allowed list");
            return 1;
        }

    warning(0, "did not found ip from any of core lists, deny it");
    return 0;
}

/*
 * Event handling functions
 */
static void ota_read_thread (void *arg)
{
    WAPEvent *e;

    while (run_status == running && (e = gwlist_consume(ppg_queue)) != NULL) {
        handle_internal_event(e);
    } 
}

/*
 * Pap event functions handle only copy pointers. They do not allocate memory.
 */
static PAPEvent *pap_event_create(Octstr *ip, Octstr *url, List *push_headers, 
                                  Octstr *mime_content, List *cgivars,
                                  HTTPClient *client)
{
    PAPEvent *p;

    p = gw_malloc(sizeof(PAPEvent));
    p->ip = ip;
    p->url = url;
    p->push_headers = push_headers;
    p->mime_content = mime_content;
    p->cgivars = cgivars;
    p->client = client;

    return p;
}

static void pap_event_destroy(PAPEvent *p)
{
    if (p == NULL)
        return;

    gw_free(p);
}

static void pap_event_destroy_item(void *p)
{
    pap_event_destroy(p);
}

static void pap_event_unpack(PAPEvent *p, Octstr **ip, Octstr **url, 
                             List **push_headers, Octstr **mime_content, 
                             List **cgivars, HTTPClient **client)
{
    *ip = p->ip;
    *url = p->url;
    *push_headers = p->push_headers;
    *mime_content = p->mime_content;
    *cgivars = p->cgivars;
    *client = p->client;
}

static void http_read_thread(void *arg)
{
    PAPEvent *p;
    Octstr *ip; 
    Octstr *url; 
    List *push_headers;
    Octstr *mime_content; 
    List *cgivars;
    HTTPClient *client;

    while (run_status == running) {
        client = http_accept_request(ppg_port, &ip, &url, &push_headers, 
                                     &mime_content, &cgivars);
        if (client == NULL) 
	        break;

        p = pap_event_create(ip, url, push_headers, mime_content, cgivars,
                             client);
        gwlist_produce(pap_queue, p);
    }
}

#ifdef HAVE_LIBSSL
static void https_read_thread(void *arg)
{
    PAPEvent *p;
    Octstr *ip; 
    Octstr *url; 
    List *push_headers;
    Octstr *mime_content; 
    List *cgivars;
    HTTPClient *client;

    while (run_status == running) {
        client = http_accept_request(ppg_ssl_port, &ip, &url, &push_headers, 
                                     &mime_content, &cgivars); 
        if (client == NULL) 
	    break;
        
        p = pap_event_create(ip, url, push_headers, mime_content, cgivars, 
                             client);
        gwlist_produce(pap_queue, p);
    }
}
#endif

/*
 * Authorization failure as such causes a challenge to the client (as required 
 * by rfc 2617, chapter 1).
 * We store HTTPClient data structure corresponding a given push id, so that 
 * we can send responses to the rigth address.
 * Pap chapter 14.4.1 states that we must return http status 202 after we have 
 * accepted PAP message, even if it is unparsable. So only the non-existing 
 * service error and some authorisation failures are handled at HTTP level. 
 * When a phone number was unacceptable, we return a PAP level error, because
 * we cannot know this error before parsing the document.
 */

static void pap_request_thread(void *arg)
{
    WAPEvent *ppg_event;
    PAPEvent *p;
    size_t push_len;
    Octstr *pap_content = NULL;
    Octstr *push_data = NULL;
    Octstr *rdf_content = NULL;
    Octstr *mime_content = NULL;
    Octstr *plos = NULL;               /* a temporary variable*/
    Octstr *boundary = NULL;
    Octstr *content_header = NULL;     /* Content-Type MIME header */
    Octstr *url = NULL;
    Octstr *ip = NULL;
    Octstr *not_found = NULL;
    Octstr *username = NULL;
    int compiler_status,
        http_status;
    List *push_headers,                /* MIME headers themselves */
         *content_headers,             /* Headers from the content entity, see
                                          pap chapters 8.2, 13.1. Rfc 2045 
                                          grammar calls these MIME-part-hea-
                                          ders */
         *cgivars;
    HTTPClient *client;
    Octstr *dlr_url;
    
    http_status = 0;
    url = ip = mime_content = username = NULL;                
  
    while (run_status == running && (p = gwlist_consume(pap_queue)) != NULL) {

        http_status = HTTP_NOT_FOUND;
        pap_event_unpack(p, &ip, &url, &push_headers, &mime_content, 
                         &cgivars, &client);      

        if (octstr_compare(url, ppg_url) != 0) {
            error(0,  "Request <%s> from <%s>: service not found", 
                  octstr_get_cstr(url), octstr_get_cstr(ip));
            debug("wap.push.ppg", 0, "your configuration uses %s",
                  octstr_get_cstr(ppg_url));           
            not_found = octstr_imm("Service not specified\n");
            http_send_reply(client, http_status, push_headers, not_found);
            goto ferror;
        }

        http_status = HTTP_UNAUTHORIZED;
   
        if (!ip_allowed_by_ppg(ip)) {
            error(0,  "Request <%s> from <%s>: ip forbidden, closing the"
                  " client", octstr_get_cstr(url), octstr_get_cstr(ip));
            http_close_client(client);
            goto ferror; 
        }

        if (!trusted_pi && user_configuration) {
	    if (!wap_push_ppg_pushuser_authenticate(client, cgivars, ip, 
                                                    push_headers, &username)) {
	             error(0,  "Request <%s> from <%s>: authorisation failure",
                       octstr_get_cstr(url), octstr_get_cstr(ip));
                 goto ferror;
            }
        } else {                        /* Jörg, this wont disappear again */
	    username = octstr_imm("");
	}

        http_status = HTTP_ACCEPTED;
        info(0, "PPG: Accept request <%s> from <%s>", octstr_get_cstr(url), 
             octstr_get_cstr(ip));
        
        if (octstr_len(mime_content) == 0) {
	    warning(0, "PPG: No MIME content received, the request"
                    " unacceptable");
            send_bad_message_response(&client, octstr_imm("No MIME content"), 
                                      PAP_BAD_REQUEST, http_status);
            if (client == NULL)
	            break;
            goto ferror;
        }

        if (!push_headers) {
            warning(0, "PPG: No push headers received , the request"
                    " unacceptable");
            send_bad_message_response(&client, octstr_imm("No push headers"), 
                                      PAP_BAD_REQUEST, http_status);
            if (client == NULL)
	            break;
            goto ferror;
        }
        octstr_destroy(ip);
        
        http_remove_hop_headers(push_headers);
        remove_mime_headers(&push_headers);
        remove_link_headers(&push_headers);

        if (!headers_acceptable(push_headers, &content_header)) {
	        warning(0,  "PPG: Unparsable push headers, the request"
                    " unacceptable");
            send_bad_message_response(&client, content_header, PAP_BAD_REQUEST,
                                      http_status);
            if (client == NULL)
	            break;
	        goto herror;
        }
        
        if (get_mime_boundary(push_headers, content_header, &boundary) == -1) {
	        warning(0, "PPG: No MIME boundary, the request unacceptable");
            send_bad_message_response(&client, content_header, PAP_BAD_REQUEST,
                                      http_status);
            if (client == NULL) 
	            break;
	        goto berror;
        }

        gw_assert(mime_content);
        if (!mime_parse(boundary, mime_content, &pap_content, &push_data, 
                        &content_headers, &rdf_content)) {
            send_bad_message_response(&client, mime_content, PAP_BAD_REQUEST,
                                      http_status);
            if (client == NULL)
	            break;
            warning(0, "PPG: unable to parse mime content, the request"
                    " unacceptable");
            goto clean;
        } else {
	        debug("wap.push.ppg", 0, "PPG: http_read_thread: pap multipart"
                  " accepted");
        }
        
        push_len = octstr_len(push_data); 
        http_header_remove_all(push_headers, "Content-Type");
	http_append_headers(push_headers, content_headers);
        change_header_value(&push_headers, "Content-Length", 
            octstr_get_cstr(plos = octstr_format("%d", push_len)));
        octstr_destroy(plos);
        octstr_destroy(content_header);
	http_destroy_headers(content_headers);

        ppg_event = NULL;
        if ((compiler_status = pap_compile(pap_content, &ppg_event)) == -2) {
	    send_bad_message_response(&client, pap_content, PAP_BAD_REQUEST,
                                       http_status);
            if (client == NULL)
	        break;
            warning(0, "PPG: pap control entity erroneous, the request" 
                    " unacceptable");
            goto no_compile;
        } else if (compiler_status == -1) {
            send_bad_message_response(&client, pap_content, PAP_BAD_REQUEST,
                                      http_status);
            if (client == NULL)
	        break;
            warning(0, "PPG: non implemented pap feature requested, the"
                    " request unacceptable");
            goto no_compile;
        } else {
	    if (!dict_put_once(http_clients, 
		    ppg_event->u.Push_Message.pi_push_id, client)) {
                warning(0, "PPG: duplicate push id, the request unacceptable");
	        tell_fatal_error(&client, ppg_event, url, http_status, 
                                 PAP_DUPLICATE_PUSH_ID);
                if (client == NULL)
	            break;
                goto not_acceptable;
	    } 

            dict_put(urls, ppg_event->u.Push_Message.pi_push_id, url); 
 
            if (is_phone_number(ppg_event->u.Push_Message.address_type)) {
                if (!trusted_pi && user_configuration && 
                        !wap_push_ppg_pushuser_client_phone_number_acceptable(
                        username, ppg_event->u.Push_Message.address_value)) {
                    tell_fatal_error(&client, ppg_event, url, http_status, 
                                    PAP_FORBIDDEN);
                    if (client == NULL)
	                break;
	                goto not_acceptable;
	            }   
            }        
            
            debug("wap.push.ppg", 0, "PPG: http_read_thread: pap control"
                  " entity compiled ok");
            ppg_event->u.Push_Message.push_data = octstr_duplicate(push_data);
            ppg_event->u.Push_Message.smsc_id = set_smsc_id(push_headers, username,
                                                            trusted_pi);
            dlr_url = set_dlr_url(push_headers, username, trusted_pi);
            ppg_event->u.Push_Message.dlr_url = dlr_url;
            ppg_event->u.Push_Message.dlr_mask = set_dlr_mask(push_headers, dlr_url);
            ppg_event->u.Push_Message.smsbox_id = set_smsbox_id(push_headers, username,
                                                                trusted_pi);
            ppg_event->u.Push_Message.service_name = set_service_name();
            remove_x_kannel_headers(&push_headers);
            ppg_event->u.Push_Message.push_headers = http_header_duplicate(push_headers);
            
            if (!handle_push_message(&client, ppg_event, http_status)) {
	        if (client == NULL)
		    break;
                goto no_transform;
            }
        }

        pap_event_destroy(p);
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(username);
        octstr_destroy(mime_content);
        octstr_destroy(pap_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(boundary);
        boundary = rdf_content = push_data = pap_content = mime_content = username = NULL;
        continue;

no_transform:
        pap_event_destroy(p);
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(username);
        octstr_destroy(mime_content);
        octstr_destroy(pap_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(boundary);
        boundary = rdf_content = push_data = pap_content = mime_content = username = NULL;
        continue;

no_compile:
        pap_event_destroy(p);
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(username);
        octstr_destroy(mime_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(boundary);
        octstr_destroy(url);
        url = boundary = rdf_content = push_data = mime_content = username = NULL;
        continue;

not_acceptable:
        pap_event_destroy(p);
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(username);
        octstr_destroy(mime_content);
        octstr_destroy(pap_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(boundary);
        octstr_destroy(url);
        url = boundary = rdf_content = push_data = pap_content = mime_content = username = NULL;
        continue;

clean:
        pap_event_destroy(p);
        http_destroy_headers(push_headers);
        http_destroy_headers(content_headers);
        octstr_destroy(pap_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(content_header);
        octstr_destroy(boundary);
        octstr_destroy(url);
        url = boundary = content_header = rdf_content = push_data = pap_content = NULL;
        continue;

ferror:
        pap_event_destroy(p);
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(username);
        octstr_destroy(url);
        octstr_destroy(ip);
        octstr_destroy(mime_content);
        mime_content = ip = url = username = NULL;
        continue;

herror:
        pap_event_destroy(p);
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(username);
        octstr_destroy(url);
        url = username = NULL;
        continue;

berror:
        pap_event_destroy(p);
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(username);
        octstr_destroy(mime_content);
        octstr_destroy(content_header);
        octstr_destroy(boundary);
        octstr_destroy(url);
        url = boundary = content_header = mime_content = username = NULL;
        continue;
    }
}

/*
 * Operations needed when push proxy gateway receives a new push message are 
 * defined in ppg Chapter 6. We create machines when error, too, because we 
 * must then have a reportable message error state.
 * Output: current HTTP Client state.
 * Return 1 if the push content was OK, 0 if it was not transformable.
 */

static int handle_push_message(HTTPClient **c, WAPEvent *e, int status)
{
    int cless,
        session_exists,
        bearer_supported,
        dummy,
        constraints,
        message_transformable,
        coriented_possible;

    long coded_appid_value;

    PPGPushMachine *pm;
    PPGSessionMachine *sm;
    WAPAddrTuple *tuple=NULL;
    Octstr *cliaddr=NULL;
    Octstr *type=NULL;

    List *push_headers;
   
    push_headers = e->u.Push_Message.push_headers;
    cliaddr = e->u.Push_Message.address_value;
    session_exists = 0;

    sm = session_find_using_pi_client_address(cliaddr);
    coded_appid_value = check_x_wap_application_id_header(&push_headers);
    cless = cless_accepted(e, sm);
    message_transformable = transform_message(&e, &tuple, push_headers, cless, 
                                              &type);

    if (!sm && !cless) {
        sm = store_session_data(sm, e, tuple, &session_exists); 
    }

    if (!store_push_data(&pm, sm, e, tuple, cless)) {
        warning(0, "PPG: handle_push_message: duplicate push id");
        *c = response_push_message(pm, PAP_DUPLICATE_PUSH_ID, status);
        goto no_start;
    }
    
    if (!message_transformable) {
	pm = update_push_data_with_attribute(&sm, pm, 
        PAP_TRANSFORMATION_FAILURE, PAP_UNDELIVERABLE1);  
        if (tuple != NULL)   
	    *c = response_push_message(pm, PAP_TRANSFORMATION_FAILURE, status);
        else
	    *c = response_push_message(pm, PAP_ADDRESS_ERROR, status);
        goto no_transformation;
    }
    
    dummy = 0;
    pm = update_push_data_with_attribute(&sm, pm, dummy, PAP_PENDING);
    
    bearer_supported = select_bearer_network(&e);
    if (!bearer_supported) {
        pm = update_push_data_with_attribute(&sm, pm, dummy, 
            PAP_UNDELIVERABLE2);
        *c = response_push_message(pm, PAP_REQUIRED_BEARER_NOT_AVAILABLE, status);
	    goto no_start;
    }
    
    if ((constraints = delivery_time_constraints(e, pm)) == TIME_EXPIRED) {
        pm = update_push_data_with_attribute(&sm, pm, PAP_FORBIDDEN, 
                                             PAP_EXPIRED);
        *c = response_push_message(pm, PAP_FORBIDDEN, status);
	    goto no_start;
    }

/*
 * If time is to early for delivering the push message, we do not remove push
 * data. We response PI here, so that "accepted for processing" means "no 
 * error messages to come".
 */ 

    *c = response_push_message(pm, PAP_ACCEPTED_FOR_PROCESSING, status);
    info(0, "PPG: handle_push_message: push message accepted for processing");

    if (constraints == TIME_TOO_EARLY)
	    goto store_push;

    if (constraints == NO_CONSTRAINTS) {
	http_header_mark_transformation(pm->push_headers, pm->push_data, type);
        if (sm)
            sm = update_session_data_with_headers(sm, pm); 

        if (!confirmation_requested(e)) {
            pm = deliver_unit_push(NOT_LAST, pm, sm, session_exists);
            goto unit_push_delivered;
	} 
	      
        if (session_exists) {
            deliver_confirmed_push(NOT_LAST, pm, sm);   
        } else { 
            coriented_possible = coriented_deliverable(coded_appid_value); 
	    http_header_remove_all(e->u.Push_Message.push_headers, 
                                   "Content-Type");  
            if (coriented_possible) {
                create_session(e, pm);
            } else {
                warning(0, "PPG: handle_push_message: wrong app id for confirmed"
                        " push session creation");
                *c = response_push_message(pm, PAP_BAD_REQUEST, status);
            }
        }
    }

    wap_addr_tuple_destroy(tuple);
    octstr_destroy(type);
    wap_event_destroy(e);
    return 1;

unit_push_delivered:
    wap_addr_tuple_destroy(tuple);
    remove_push_data(sm, pm, cless);
    octstr_destroy(type);
    wap_event_destroy(e);
    return 1;

store_push:
    wap_addr_tuple_destroy(tuple);
    octstr_destroy(type);
    wap_event_destroy(e);
    return 1;

no_transformation:
    wap_addr_tuple_destroy(tuple);
    remove_push_data(sm, pm, cless);
    if (sm)
        remove_pushless_session(sm);
    wap_event_destroy(e);
    return 0;

no_start:
    wap_addr_tuple_destroy(tuple);
    octstr_destroy(type);
    remove_push_data(sm, pm, cless);
    if (sm)
        remove_pushless_session(sm);
    wap_event_destroy(e);
    return 1;
}

/*
 * These events come from OTA layer
 */
static void handle_internal_event(WAPEvent *e)
{
    long sid,
         pid,
         reason,
         port;
    int http_status;
    PPGPushMachine *pm;
    PPGSessionMachine *sm;
    WAPAddrTuple *tuple;
    List *caps;
        
    http_status = HTTP_OK;
    switch (e->type) {
/*
 * Pap, Chapter 11.1.3 states that if client is incapable, we should abort the
 * push and inform PI. We do this here.
 * In addition, we store session id used as an alias for address tuple and do
 * all pushes pending for this initiator (or abort them).
 */
    case Pom_Connect_Ind:
         debug("wap.push.ppg", 0, "PPG: handle_internal_event: connect"
               " indication from OTA");
         sid = e->u.Pom_Connect_Ind.session_id;
         tuple = e->u.Pom_Connect_Ind.addr_tuple;
         port = tuple->remote->port;
         caps = e->u.Pom_Connect_Ind.requested_capabilities;

         sm = wap_push_ppg_have_push_session_for(tuple);
         sm = update_session_data(sm, sid, port, caps);
        
         if (!response_push_connection(e, sm)) {
	     pm = abort_delivery(sm, http_status);
             wap_event_destroy(e);
             return;
         }

/* 
 * hard-coded until we have bearer control implemented
 */
         deliver_pending_pushes(sm, NOT_LAST);  
         wap_event_destroy(e);
    break;

    case Pom_Disconnect_Ind:
        debug("wap.push.ppg", 0, "PPG: handle_internal_event: disconnect"
              " indication from OTA");
        sm = wap_push_ppg_have_push_session_for_sid(
                 e->u.Pom_Disconnect_Ind.session_handle);
        remove_session_data(sm, http_status);
        wap_event_destroy(e);
    break;

/*
 * Only the client can close a session. So we leave session open, even when 
 * there are no active pushes. Note that we do not store PAP attribute very
 * long time. Point is that result notification message, if asked, will rep-
 * ort this fact to PI, after which there is no need to store it any more.
 */
    case Po_ConfirmedPush_Cnf:
        debug("wap.push.ppg", 0, "PPG: handle_internal_event: push"
              " confirmation from OTA");
        sid = e->u.Po_ConfirmedPush_Cnf.session_handle;
        pid = e->u.Po_ConfirmedPush_Cnf.server_push_id;

        sm = wap_push_ppg_have_push_session_for_sid(sid);
        pm = find_ppg_push_machine_using_pid(sm, pid);
        pm = update_push_data_with_attribute(&sm, pm, PAP_CONFIRMED, 
                                             PAP_DELIVERED2);
        wap_event_destroy(e);
        remove_push_data(sm, pm, 0);
    break;

/*
 * Again, PAP attribute will be reported to PI by using result notification.
 */
    case Po_PushAbort_Ind:
        debug("wap.push.ppg", 0, "PPG: handle_internal_event: abort"
              " indication from OTA");
        sid = e->u.Po_PushAbort_Ind.session_handle;
        pid = e->u.Po_PushAbort_Ind.push_id;

        sm = wap_push_ppg_have_push_session_for_sid(sid);
        pm = find_ppg_push_machine_using_pid(sm, pid);
        session_machine_assert(sm);
        push_machine_assert(pm);
        reason = e->u.Po_PushAbort_Ind.reason;
        pm = update_push_data_with_attribute(&sm, pm, reason, PAP_ABORTED);
        remove_session_data(sm, http_status);
        wap_event_destroy(e);
    break;

/*
 * FIXME TRU: Add timeout (a mandatory feature!)
 */
    default:
        debug("wap.ppg", 0, "PPG: handle_internal_event: an unhandled event");
        wap_event_dump(e);
        wap_event_destroy(e);
    break;
    }
}

/*
 * Functions related to various ppg machine types.
 *
 * We do not set session id here: it is told to us by wsp.
 */
static PPGSessionMachine *session_machine_create(WAPAddrTuple *tuple, 
                                                 WAPEvent *e)
{
    PPGSessionMachine *m;

    gw_assert(e->type == Push_Message);

    m = gw_malloc(sizeof(PPGSessionMachine));
    
    #define INTEGER(name) m->name = 0;
    #define OCTSTR(name) m->name = NULL;
    #define ADDRTUPLE(name) m->name = NULL;
    #define PUSHMACHINES(name) m->name = gwlist_create();
    #define CAPABILITIES(name) m->name = NULL;
    #define MACHINE(fields) fields
    #include "wap_ppg_session_machine.def"

    m->pi_client_address = octstr_duplicate(e->u.Push_Message.address_value);
    m->addr_tuple = wap_addr_tuple_duplicate(tuple);
    m->assumed_capabilities = 
        wsp_cap_duplicate_list(e->u.Push_Message.pi_capabilities);
    m->preferconfirmed_value = PAP_CONFIRMED;    

    gwlist_append(ppg_machines, m);
    debug("wap.push.ppg", 0, "PPG: Created PPGSessionMachine %ld",
          m->session_id);

    return m;
}

static void session_machine_destroy(void *p)
{
    PPGSessionMachine *sm;

    if (p == NULL)
        return;

    sm = p;
    debug("wap.push.ppg", 0, "PPG: destroying PPGSEssionMachine %ld", 
          sm->session_id);
    
    #define OCTSTR(name) octstr_destroy(sm->name);
    #define ADDRTUPLE(name) wap_addr_tuple_destroy(sm->name);
    #define INTEGER(name) sm->name = 0;
    #define PUSHMACHINES(name) push_machines_list_destroy(sm->name);
    #define CAPABILITIES(name) wsp_cap_destroy_list(sm->name);
    #define MACHINE(fields) fields
    #include "wap_ppg_session_machine.def"
    gw_free(sm);
}

/*
 * FIXME: PPG's trust policy (flags authenticated and trusted).
 * We return pointer to the created push machine and push id it uses.
 */
static PPGPushMachine *push_machine_create(WAPEvent *e, WAPAddrTuple *tuple)
{
    PPGPushMachine *m;

    m = gw_malloc(sizeof(PPGPushMachine));

    #define INTEGER(name) m->name = 0;
    #define OCTSTR(name) m->name = NULL;
    #define OPTIONAL_OCTSTR(name) m->name = NULL;
    #define ADDRTUPLE(name) m->name = NULL;
    #define CAPABILITIES m->name = NULL;
    #define HTTPHEADER(name) m->name = NULL;
    #define MACHINE(fields) fields
    #include "wap_ppg_push_machine.def"

    m->addr_tuple = wap_addr_tuple_duplicate(tuple);
    m->pi_push_id = octstr_duplicate(e->u.Push_Message.pi_push_id);
    m->push_id = counter_increase(push_id_counter);
    m->delivery_method = e->u.Push_Message.delivery_method;
    m->deliver_after_timestamp = 
        octstr_duplicate(e->u.Push_Message.deliver_after_timestamp);
    m->priority = e->u.Push_Message.priority;
    m->push_headers = http_header_duplicate(e->u.Push_Message.push_headers);
    m->push_data = octstr_duplicate(e->u.Push_Message.push_data);

    m->address_type = e->u.Push_Message.address_type;
    if (e->u.Push_Message.smsc_id != NULL)
        m->smsc_id = octstr_duplicate(e->u.Push_Message.smsc_id);
    else
        m->smsc_id = NULL;
    if (e->u.Push_Message.dlr_url != NULL)
        m->dlr_url = octstr_duplicate(e->u.Push_Message.dlr_url);                                 
    else
        m->dlr_url = NULL;
    m->dlr_mask = e->u.Push_Message.dlr_mask;
    if (e->u.Push_Message.smsbox_id != NULL)
        m->smsbox_id = octstr_duplicate(e->u.Push_Message.smsbox_id);
    else
        m->smsbox_id = NULL;
    m->service_name = octstr_duplicate(e->u.Push_Message.service_name);

    m->progress_notes_requested = e->u.Push_Message.progress_notes_requested;
    if (e->u.Push_Message.progress_notes_requested)
        m->ppg_notify_requested_to = 
            octstr_duplicate(e->u.Push_Message.ppg_notify_requested_to);

    debug("wap.push.ppg", 0, "PPG: push machine %ld created", m->push_id);

    return m;
}

/*
 * Contrary to the normal Kannel style, we do not remove from a list here. 
 * That is because we now have two different push lists.
 */
static void push_machine_destroy(void *p)
{
    PPGPushMachine *pm;

    if (p == NULL)
        return;

    pm = p;

    debug("wap.push.ppg", 0, "PPG: destroying push machine %ld", 
          pm->push_id); 
    #define OCTSTR(name) octstr_destroy(pm->name);
    #define OPTIONAL_OCTSTR(name) octstr_destroy(pm->name);
    #define INTEGER(name)
    #define ADDRTUPLE(name) wap_addr_tuple_destroy(pm->name);
    #define CAPABILITIES(name) wap_cap_destroy_list(pm->name);
    #define HTTPHEADER(name) http_destroy_headers(pm->name);
    #define MACHINE(fields) fields
    #include "wap_ppg_push_machine.def"

    gw_free(p);
}

static void push_machines_list_destroy(List *machines)
{
    if (machines == NULL)
        return;

    gwlist_destroy(machines, push_machine_destroy);
}

static int session_has_addr(void *a, void *b)
{
    Octstr *cliaddr;
    PPGSessionMachine *sm;

    cliaddr = b;
    sm = a;
    
    return octstr_compare(sm->addr_tuple->remote->address, cliaddr) == 0;
}

static int session_has_sid(void *a, void *b)
{
     PPGSessionMachine *sm;
     long *sid;

     sid = b;
     sm = a;

     return *sid == sm->session_id;
}

/*
 * Here session machine address tuples have connection-oriented ports, because
 * these are used when establishing the connection an doing pushes. But session
 * creation request must be to the the connectionless push port of the client.
 * So we change ports here.
 */
static void create_session(WAPEvent *e, PPGPushMachine *pm)
{
    WAPEvent *ota_event;
    List *push_headers;
    Octstr *smsc_id;
    Octstr *dlr_url;
    Octstr *smsbox_id;
    Octstr *service_name;

    gw_assert(e->type == Push_Message);
    push_machine_assert(pm);
    
    push_headers = http_header_duplicate(e->u.Push_Message.push_headers);
    smsc_id = octstr_duplicate(e->u.Push_Message.smsc_id);
    dlr_url = octstr_duplicate(e->u.Push_Message.dlr_url);
    smsbox_id = octstr_duplicate(e->u.Push_Message.smsbox_id);
    service_name = octstr_duplicate(e->u.Push_Message.service_name);

    ota_event = wap_event_create(Pom_SessionRequest_Req);
    ota_event->u.Pom_SessionRequest_Req.addr_tuple =
        addr_tuple_change_cliport(pm->addr_tuple,
                                  CONNECTIONLESS_PUSH_CLIPORT);
    ota_event->u.Pom_SessionRequest_Req.push_headers = push_headers;
    ota_event->u.Pom_SessionRequest_Req.push_id = pm->push_id;
    ota_event->u.Pom_SessionRequest_Req.address_type = pm->address_type;
    if (smsc_id != NULL)
        ota_event->u.Pom_SessionRequest_Req.smsc_id = smsc_id;
    else
        ota_event->u.Pom_SessionRequest_Req.smsc_id = NULL;
    if (dlr_url != NULL)
        ota_event->u.Pom_SessionRequest_Req.dlr_url = dlr_url;                                    
    else
        ota_event->u.Pom_SessionRequest_Req.dlr_url = NULL;
    ota_event->u.Pom_SessionRequest_Req.dlr_mask = e->u.Push_Message.dlr_mask;
    if (smsbox_id != NULL)
        ota_event->u.Pom_SessionRequest_Req.smsbox_id = smsbox_id;
    else
        ota_event->u.Pom_SessionRequest_Req.smsbox_id = NULL;
    ota_event->u.Pom_SessionRequest_Req.service_name = service_name;
        
    dispatch_to_ota(ota_event);
}

/*
 * We store data to push machine, because it is possible that we do not have
 * a session when push request happens.
 */
static void request_confirmed_push(long last, PPGPushMachine *pm, 
                                   PPGSessionMachine *sm)
{
    WAPEvent *ota_event;
    List *push_headers;

    gw_assert(last == 0 || last == 1);
    push_machine_assert(pm);
    session_machine_assert(sm);
    
    push_headers = http_header_duplicate(pm->push_headers);

    ota_event = wap_event_create(Po_ConfirmedPush_Req);
    ota_event->u.Po_ConfirmedPush_Req.server_push_id = pm->push_id;
    ota_event->u.Po_ConfirmedPush_Req.push_headers = push_headers;
    ota_event->u.Po_ConfirmedPush_Req.authenticated = pm->authenticated;
    ota_event->u.Po_ConfirmedPush_Req.trusted = pm->trusted;
    ota_event->u.Po_ConfirmedPush_Req.last = last;
 
    if (pm->push_data != NULL)
        ota_event->u.Po_ConfirmedPush_Req.push_body = 
            octstr_duplicate(pm->push_data);
    else
        ota_event->u.Po_ConfirmedPush_Req.push_body = NULL;

    ota_event->u.Po_ConfirmedPush_Req.session_handle = sm->session_id;
    debug("wap.push.ota", 0, "PPG: confirmed push request to OTA");
    
    dispatch_to_ota(ota_event);
}

/*
 * There is to types of unit push requests: requesting ip services and sms 
 * services. Address type tells the difference.
 */
static void request_unit_push(long last, PPGPushMachine *pm)
{
    WAPEvent *ota_event;
    List *push_headers;

    gw_assert(last == 0 || last == 1);
    push_machine_assert(pm);
    
    push_headers = http_header_duplicate(pm->push_headers);

    ota_event = wap_event_create(Po_Unit_Push_Req);
    ota_event->u.Po_Unit_Push_Req.addr_tuple = 
        wap_addr_tuple_duplicate(pm->addr_tuple);
    ota_event->u.Po_Unit_Push_Req.push_id = pm->push_id;
    ota_event->u.Po_Unit_Push_Req.push_headers = push_headers;
    ota_event->u.Po_Unit_Push_Req.authenticated = pm->authenticated;
    ota_event->u.Po_Unit_Push_Req.trusted = pm->trusted;
    ota_event->u.Po_Unit_Push_Req.last = last;

    ota_event->u.Po_Unit_Push_Req.address_type = pm->address_type;
    if (pm->smsc_id != NULL)
        ota_event->u.Po_Unit_Push_Req.smsc_id = octstr_duplicate(pm->smsc_id);
    else
        ota_event->u.Po_Unit_Push_Req.smsc_id = NULL;
    if (pm->dlr_url != NULL)
        ota_event->u.Po_Unit_Push_Req.dlr_url = octstr_duplicate(pm->dlr_url);
    else
        ota_event->u.Po_Unit_Push_Req.dlr_url = NULL;
    ota_event->u.Po_Unit_Push_Req.dlr_mask = pm->dlr_mask;
    if (pm->smsbox_id != NULL)   
        ota_event->u.Po_Unit_Push_Req.smsbox_id = octstr_duplicate(pm->smsbox_id);
    else
        ota_event->u.Po_Unit_Push_Req.smsbox_id = NULL;
    if (pm->service_name != NULL)    
        ota_event->u.Po_Unit_Push_Req.service_name = octstr_duplicate(pm->service_name);

    ota_event->u.Po_Unit_Push_Req.push_body = octstr_duplicate(pm->push_data);

    dispatch_to_ota(ota_event);
    debug("wap.push.ppg", 0, "PPG: OTA request for unit push");
}

static void request_push(long last, PPGPushMachine *pm)
{
    WAPEvent *ota_event;
    List *push_headers;

    gw_assert(last == 0 || last == 1);
    push_machine_assert(pm);
    
    push_headers = http_header_duplicate(pm->push_headers);

    ota_event = wap_event_create(Po_Push_Req);
    ota_event->u.Po_Push_Req.push_headers = push_headers;
    ota_event->u.Po_Push_Req.authenticated = pm->authenticated;
    ota_event->u.Po_Push_Req.trusted = pm->trusted;
    ota_event->u.Po_Push_Req.last = last;

    if (pm->push_data != NULL)
        ota_event->u.Po_Push_Req.push_body = 
            octstr_duplicate(pm->push_data);
    else
        ota_event->u.Po_Push_Req.push_body = NULL;        

    ota_event->u.Po_Push_Req.session_handle = pm->session_id;
    debug("wap.push.ppg", 0, "PPG: OTA request for push");
    
    dispatch_to_ota(ota_event);
}


/*
 * According to pap, Chapter 11, capabilities can be 
 *    
 *                a) queried by PI
 *                b) told to PI when a client is subscribing
 *                c) assumed
 *
 * In case c) we got capabilities from third part of the push message (other
 * cases PI knows what it is doing), and we check is the client capable to
 * handle the message.
 * Requested capabilities are client capabilities, assumed capabilities are
 * PI capabilities. If there is no assumed capabilities, PI knows client capab-
 * ilities by method a) or method b).
 * Returns 1, if the client is capable, 0 when it is not.
 */

static int response_push_connection(WAPEvent *e, PPGSessionMachine *sm)
{
    WAPEvent *appl_event;

    gw_assert(e->type == Pom_Connect_Ind);

    if (sm->assumed_capabilities != NULL && check_capabilities(
            e->u.Pom_Connect_Ind.requested_capabilities, 
            sm->assumed_capabilities) == 0)
       return 0;

    appl_event = wap_event_create(Pom_Connect_Res);
    appl_event->u.Pom_Connect_Res.negotiated_capabilities = 
        wsp_cap_duplicate_list(e->u.Pom_Connect_Ind.requested_capabilities);
    appl_event->u.Pom_Connect_Res.session_id = e->u.Pom_Connect_Ind.session_id;

    dispatch_to_appl(appl_event);

    return 1;
}

/*
 * Push response, from pap, Chapter 9.3. 
 * Inputs error code, in PAP format.
 * Return the current value of HTTPClient.
 */
static HTTPClient *response_push_message(PPGPushMachine *pm, long code, int status)
{
    WAPEvent *e;
    HTTPClient *c;

    push_machine_assert(pm);

    e = wap_event_create(Push_Response);
    e->u.Push_Response.pi_push_id = octstr_duplicate(pm->pi_push_id);
    e->u.Push_Response.sender_name = tell_ppg_name();
    e->u.Push_Response.reply_time = set_time();
    e->u.Push_Response.code = code;
    e->u.Push_Response.desc = describe_code(code);

    c = send_push_response(e, status);

    return c;
}


static int check_capabilities(List *requested, List *assumed)
{
    int is_capable;

    is_capable = 1;

    return is_capable;
}

/*
 * Time of creation of the response (pap, chapter 9.3). We convert UNIX time 
 * to ISO8601, it is, YYYY-MM-DDThh:mm:ssZ, T and Z being literal strings (we
 * use gw_gmtime to turn UNIX time to broken time).
 */
static Octstr *set_time(void)
{
    Octstr *current_time;
    struct tm now;

    now = gw_gmtime(time(NULL));
    current_time = octstr_format("%04d-%02d-%02dT%02d:%02d:%02dZ", 
                                 now.tm_year + 1900, now.tm_mon + 1, 
                                 now.tm_mday, now.tm_hour, now.tm_min, 
                                 now.tm_sec);

    return current_time;
}

static void session_machine_assert(PPGSessionMachine *sm)
{
    gw_assert(sm);
    gw_assert(sm->session_id >= 0);
    gw_assert(sm->addr_tuple);
    gw_assert(sm->pi_client_address);
}

static void push_machine_assert(PPGPushMachine *pm)
{
    gw_assert(pm);
    gw_assert(pm->pi_push_id);
    gw_assert(pm->push_id >= 0);
    gw_assert(pm->session_id >= 0);
    gw_assert(pm->addr_tuple);
    gw_assert(pm->trusted == 1 || pm->trusted == 0);
    gw_assert(pm->authenticated  == 1 || pm->authenticated == 0);
}

/*
 * Message transformations performed by PPG are defined in ppg, 6.1.2.1. Ppg,
 * chapter 6.1.1, states that we MUST reject a push having an erroneous PAP
 * push message element. So we must validate it even when we do not compile
 * it.
 * If message content was not si or sl, we pass it without modifications.
 * We do not do any (formally optional, but phones may disagree) header 
 * conversions to the binary format here, these are responsibility of our OTA 
 * module (gw/wap_push_ota.c). 
 * FIXME: Remove all headers which default values are known to the client. 
 *
 * Return 
 *    a) message, either transformed or not (if there is no-transform cache 
 *       directive, wml code is erroneous or content was not si or sl.) 
 *    b) The transformed gw address. Use here global-sender, when the bearer
 *       is SMS (some SMS centers would require this).
 *    c) the transformed message content type
 *
 * Returned flag tells was the transformation (if any) successful or not. Error 
 * flag is returned when there is no push headers, there is no Content-Type header
 * or push content does not compile. We should have checked existence of push 
 * headers earlier, but let us be careful.
 */
static int transform_message(WAPEvent **e, WAPAddrTuple **tuple, 
                             List *push_headers, int cless_accepted, Octstr **type)
{
    int message_deliverable;
    struct content content;
    Octstr *cliaddr;
    long cliport,
         servport,
         address_type;

    gw_assert((**e).type == Push_Message);
    if ((**e).u.Push_Message.push_headers == NULL)
        goto herror;

    cliaddr = (**e).u.Push_Message.address_value;
    push_headers = (**e).u.Push_Message.push_headers;

    if (!cless_accepted) {
        cliport = CONNECTED_CLIPORT;
        servport = CONNECTED_SERVPORT;
    } else {
        cliport = CONNECTIONLESS_PUSH_CLIPORT;
        servport = CONNECTIONLESS_SERVPORT;
    }
    
    address_type = (**e).u.Push_Message.address_type;
    *tuple = set_addr_tuple(cliaddr, cliport, servport, address_type, push_headers);

    if (!content_transformable(push_headers)) 
        goto no_transform;

    content.charset = NULL;
    content.type = NULL;

    content.body = (**e).u.Push_Message.push_data; 
    if (content.body == NULL)
        goto no_transform;

    content.type = http_header_find_first(push_headers, "Content-Transfer-Encoding");
    if (content.type) {
	octstr_strip_blanks(content.type);
	debug("wap.push.ppg", 0, "PPG: Content-Transfer-Encoding is \"%s\"",
	      octstr_get_cstr (content.type));
	message_deliverable = pap_get_content(&content);
	
        if (message_deliverable) {
	    change_header_value(&push_headers, "Content-Transfer-Encoding", 
                                "binary");
	} else {
	    goto error;
	}
    }

    octstr_destroy(content.type);
    http_header_get_content_type(push_headers, &content.type, &content.charset);   
    message_deliverable = pap_convert_content(&content);

    if (content.type == NULL)
        goto error;

    if (message_deliverable) {
        *type = content.type;        
    } else {
        goto error;
    }

    (**e).u.Push_Message.push_data = content.body;
    octstr_destroy(content.charset);

    debug("wap.push.ppg", 0, "PPG: transform_message: push message content"
          " and headers valid");
    return 1;

herror:
    warning(0, "PPG: transform_message: no push headers, cannot accept");
    octstr_destroy(content.type);
    return 0;

error:
    warning(0, "PPG: transform_message: push content erroneous, cannot"
            " accept");
    octstr_destroy(content.type);
    octstr_destroy(content.charset);
    return 0;

no_transform:
    warning(0, "PPG: transform_message: push content non transformable");
    return 1;
}

/*
 * Transform X-WAP-Application headers as per ppg 6.1.2.1. Note that missing
 * header means that wml.ua is assumed.
 * Return coded value (starting with 0), when the id was not wml.ua
 *        -1, when id was wml.ua (or no application id was present)
 *        -2, when error
 */
static long check_x_wap_application_id_header(List **push_headers)
{
    Octstr *appid_content;
    long coded_value;
    Octstr *cos;
    
    if (*push_headers == NULL)
        return -2;

    appid_content = http_header_find_first(*push_headers, 
        "X-WAP-Application-Id");
    
    if (appid_content == NULL) {
        octstr_destroy(appid_content);
        return -1;
    }

    if ((coded_value = parse_appid_header(&appid_content)) < 0) {
        octstr_destroy(appid_content);
        return -2;
    }
        
    if (coded_value == 2) {
        octstr_destroy(appid_content);
        http_header_remove_all(*push_headers, "X-WAP-Application-Id");
        return -1;
    }

    cos = octstr_format("%ld", coded_value);
    http_header_remove_all(*push_headers, "X-WAP-Application-Id");
    http_header_add(*push_headers, "X-WAP-Application-Id", octstr_get_cstr(cos));
    
    octstr_destroy(appid_content); 
    octstr_destroy(cos);
    
    return coded_value;  
}

/*
 * Check do we have a no-transform cache directive amongst the headers.
 */
static int content_transformable(List *push_headers)
{
    List *cache_directives;
    long i;
    Octstr *header_name, 
           *header_value;

    gw_assert(push_headers);

    cache_directives = http_header_find_all(push_headers, "Cache-Control");
    if (gwlist_len(cache_directives) == 0) {
        http_destroy_headers(cache_directives);
        return 1;
    }

    i = 0;
    while (i < gwlist_len(cache_directives)) {
        http_header_get(cache_directives, i, &header_name, &header_value);
        if (octstr_compare(header_value, octstr_imm("no-transform")) == 0) {
            http_destroy_headers(cache_directives);
            octstr_destroy(header_name);
            octstr_destroy(header_value);
	    return 0;
        }
        ++i;
    }

    http_destroy_headers(cache_directives);
    octstr_destroy(header_name);
    octstr_destroy(header_value);
 
    return 1;
}

/*
 * Convert push content to compact binary format (this can be wmlc, sic, slc
 * or coc). Current status wml, sl and si compiled, others passed.
 */
static Octstr *convert_wml_to_wmlc(struct content *content)
{
    Octstr *wmlc;

    if (wml_compile(content->body, content->charset, &wmlc, NULL) == 0)
        return wmlc;
    warning(0, "PPG: wml compilation failed");
    return NULL;
}

static Octstr *convert_si_to_sic(struct content *content)
{
    Octstr *sic;

    if (si_compile(content->body, content->charset, &sic) == 0)
        return sic;
    warning(0, "PPG: si compilation failed");
    return NULL;
}

static Octstr *convert_sl_to_slc(struct content *content)
{
    Octstr *slc;

    if (sl_compile(content->body, content->charset, &slc) == 0)
        return slc;
    warning(0, "PPG: sl compilation failed");
    return NULL;
}


static Octstr *extract_base64(struct content *content)
{
    Octstr *orig = octstr_duplicate(content->body);
    octstr_base64_to_binary(orig);
    return orig;
}

static struct {
    char *type;
    char *result_type;
    Octstr *(*convert) (struct content *);
} converters[] = {
    { "text/vnd.wap.wml",
      "application/vnd.wap.wmlc",
      convert_wml_to_wmlc },
    { "text/vnd.wap.si",
      "application/vnd.wap.sic",
      convert_si_to_sic },
    { "text/vnd.wap.sl",
      "application/vnd.wap.slc",
      convert_sl_to_slc}
};

#define NUM_CONVERTERS ((long) (sizeof(converters) / sizeof(converters[0])))

static struct {
    char *transfer_encoding;
    Octstr *(*extract) (struct content *);
} extractors[] = {
    { "base64",
      extract_base64 }
};

#define NUM_EXTRACTORS ((long) (sizeof(extractors) / sizeof(extractors[0])))

/*
 * Compile wap defined contents, accept others without modifications. Push
 * message 6.3 states that push content can be any MIME accepted content type.
 */
static int pap_convert_content(struct content *content)
{
    long i;
    Octstr *new_body;

    for (i = 0; i < NUM_CONVERTERS; i++) {
        if (octstr_compare(content->type, 
	        octstr_imm(converters[i].type)) == 0) {
	    new_body = converters[i].convert(content);
            if (new_body == NULL)
	        return 0;
            octstr_destroy(content->body);
            content->body = new_body;
            octstr_destroy(content->type); 
            content->type = octstr_create(converters[i].result_type);
            return 1;
        }
    }

    return 1;
}

/*
 * MIME specifies a number of content transfer encodings.
 * This function tries to get the original version of the
 * content.
 */
static int pap_get_content(struct content *content)
{
    long i;
    Octstr *new_body;

    for (i = 0; i < NUM_EXTRACTORS; i++) {
        if (octstr_case_compare(content->type, 
	        octstr_imm(extractors[i].transfer_encoding)) == 0) {
	    
	        new_body = extractors[i].extract(content);
            if (new_body == NULL)
	        return 0;
            octstr_destroy(content->body);
            content->body = new_body;
	    octstr_destroy(content->type);
	    content->type = NULL;
            return 1;
        }
    }

    return 1;
}

/*
 * Bearer and network types are defined in wdp, Appendix C. Any means any net-
 * work supporting IPv4 or IPv6.
 */
static char *bearers[] = {
   "Any",
   "SMS",
   "CSD",
   "GPRS",
   "Packet Data",
   "CDPD"
};

#define NUMBER_OF_BEARERS sizeof(bearers)/sizeof(bearers[0])

static char *networks[] = {
    "Any", 
    "GSM",
    "IS-95 CDMA",
    "ANSI-136",
    "AMPS",
    "PDC",
    "IDEN", 
    "PHS",   
    "TETRA"
};

#define NUMBER_OF_NETWORKS sizeof(networks)/sizeof(networks[0])

/*
 * We support networks using IP as a bearer and GSM using SMS as bearer, so we
 * must reject others. Default bearer is IP, it is (currently) not-SMS. After
 * the check we change meaning of the bearer_required-attribute: it will tell 
 * do we use WAP over SMS.
 */
int select_bearer_network(WAPEvent **e)
{
    Octstr *bearer,
           *network;
    int bearer_required,
        network_required;
    size_t i, 
           j;

    gw_assert((**e).type == Push_Message);

    bearer_required = (**e).u.Push_Message.bearer_required;
    network_required = (**e).u.Push_Message.network_required;
    bearer = octstr_imm("Any");
    network = octstr_imm("Any");
    
    if (!bearer_required || !network_required)
        return 1;

    if (bearer_required)
        bearer = (**e).u.Push_Message.bearer;
    if (network_required)
        network = (**e).u.Push_Message.network;
    
    for (i = 0; i < NUMBER_OF_NETWORKS ; ++i) {
        if (octstr_case_compare(network, octstr_imm(networks[i])) == 0)
	        break;
    }
    for (j = 0; j < NUMBER_OF_BEARERS ; ++j) {
        if (octstr_case_compare(bearer, octstr_imm(bearers[j])) == 0)
	        break;
    }
    if (i == NUMBER_OF_NETWORKS || j == NUMBER_OF_BEARERS)
        return 0;

    return 1;
}

static int session_has_pi_client_address(void *a, void *b)
{
    Octstr *caddr;
    PPGSessionMachine *sm;

    caddr = b;
    sm = a;

    return octstr_compare(caddr, sm->pi_client_address) == 0;
}

/*
 * PI client address is composed of a client specifier and a PPG specifier (see
 * ppg, chapter 7). So it is equivalent with gw address quadruplet.
 */
PPGSessionMachine *session_find_using_pi_client_address(Octstr *caddr)
{
    PPGSessionMachine *sm;
    
    sm = gwlist_search(ppg_machines, caddr, session_has_pi_client_address);

    return sm;
}

/*
 * Give PPG a human readable name.
 */
static Octstr *tell_ppg_name(void)
{
     return octstr_format("%S; WAP/1.3 (" GW_NAME "/%s)", get_official_name(), 
                          GW_VERSION);
}

/*
 * Delivery time constraints are a) deliver before and b) deliver after. It is
 * possible that service required is after some time and before other. So we 
 * test first condition a). Note that 'now' satisfy both constraints. 
 * Returns: 0 delivery time expired
 *          1 too early to send the message
 *          2 no constraints
 */
static int delivery_time_constraints(WAPEvent *e, PPGPushMachine *pm)
{
    Octstr *before,
           *after;
    struct tm now;
   
    gw_assert(e->type == Push_Message);
    
    before = e->u.Push_Message.deliver_before_timestamp;
    after = pm->deliver_after_timestamp;
    now = gw_gmtime(time(NULL));

    if (!deliver_before_test_cleared(before, now)) {
        info(0, "PPG: delivery deadline expired, dropping the push message");
        return 0;
    }

    if (!deliver_after_test_cleared(after, now)) {
        debug("wap.push.ppg", 0, "PPG: too early to push the message,"
              " waiting");
        return 1;
    }

    return 2;
}

/*
 * Give verbose description of the result code. Conversion table for descrip-
 * tion.
 */
struct description_t {
    long reason;
    char *description;
};

typedef struct description_t description_t;

static description_t pap_desc[] = {
    { PAP_OK, "The request succeeded"},
    { PAP_ACCEPTED_FOR_PROCESSING, "The request has been accepted for"
                                   " processing"},
    { PAP_BAD_REQUEST, "Not understood due to malformed syntax"},
    { PAP_FORBIDDEN, "Request was refused"},
    { PAP_ADDRESS_ERROR, "The client specified not recognised"},
    { PAP_CAPABILITIES_MISMATCH, "Capabilities assumed by PI were not"
                                 "  acceptable for the client specified"},
    { PAP_DUPLICATE_PUSH_ID, "Push id supplied was not unique"},
    { PAP_INTERNAL_SERVER_ERROR, "Server could not fulfill the request due"
                                 " to an internal error"},
    { PAP_TRANSFORMATION_FAILURE, "PPG was unable to perform a transformation"
                                  " of the message"},
    { PAP_REQUIRED_BEARER_NOT_AVAILABLE, "Required bearer not available"},
    { PAP_SERVICE_FAILURE, "The service failed. The client may re-attempt"
                           " the operation"},
    { PAP_CLIENT_ABORTED, "The client aborted the operation. No reason given"},
    { WSP_ABORT_USERREQ, "Wsp requested abort"},
    { WSP_ABORT_USERRFS, "Wsp refused push message. Do not try again"},
    { WSP_ABORT_USERPND, "Push message cannot be delivered to intended"
                         " destination by the wsp"},
    { WSP_ABORT_USERDCR, "Push message discarded due to resource shortage in"
                         " wsp"},
    { WSP_ABORT_USERDCU, "Content type of the push message cannot be"
                         " processed by the wsp"}
};

static size_t desc_tab_size = sizeof(pap_desc) / sizeof(pap_desc[0]);
    
static Octstr *describe_code(long code)
{
    Octstr *desc;
    size_t i;

    for (i = 0; i < desc_tab_size; i++) {
        if (pap_desc[i].reason == code) {
            desc = octstr_create(pap_desc[i].description);
            return desc;
        }
    }

    return octstr_imm("unknown PAP code");
}

/*
 * Remove push data from the list of connectionless pushes, if cless is true, 
 * otherwise from the list of pushes belonging to session machine sm.
 */
static void remove_push_data(PPGSessionMachine *sm, PPGPushMachine *pm, 
                             int cless)
{
    push_machine_assert(pm);

    if (cless) {
        gwlist_delete_equal(ppg_unit_pushes, pm);
    } else {
        session_machine_assert(sm);
        gwlist_delete_equal(sm->push_machines, pm);
    }

    push_machine_destroy(pm);
}

/*
 * If cless is true, store push to the list connectionless pushes, otherwise 
 * in the push list of the session machine sm.
 * We must create a push machine even when an error occurred, because this is
 * used for storing the relevant pap error state and other data for this push.
 * There should not be any duplicate push ids here (this is tested by http_
 * read_thread), but let us be carefull.
 * Return a pointer the push machine newly created and a flag telling was the
 * push id duplicate. 
 */
static int store_push_data(PPGPushMachine **pm, PPGSessionMachine *sm, 
                           WAPEvent *e, WAPAddrTuple *tuple, int cless)
{ 
    Octstr *pi_push_id;  
    int duplicate_push_id;
    
    gw_assert(e->type == Push_Message);

    pi_push_id = e->u.Push_Message.pi_push_id;

    duplicate_push_id = 0;
    if (((!cless) && 
       (find_ppg_push_machine_using_pi_push_id(sm, pi_push_id) != NULL)) ||
       ((cless) && 
       (find_unit_ppg_push_machine_using_pi_push_id(pi_push_id) != NULL)))
       duplicate_push_id = 1;

    *pm = push_machine_create(e, tuple);
    
    if (duplicate_push_id)
        return !duplicate_push_id;
    
    if (!cless) {
       gwlist_append(sm->push_machines, *pm);
       debug("wap.push.ppg", 0, "PPG: store_push_data: push machine %ld"
             " appended to push list of sm machine %ld", (*pm)->push_id, 
             sm->session_id);
    } else {
       gwlist_append(ppg_unit_pushes, *pm);
       debug("wap.push.ppg", 0, "PPG: store_push_data: push machine %ld"
             " appended to unit push list", (*pm)->push_id);
    }

    return !duplicate_push_id;
}

/*
 * Deliver confirmed push. Note that if push is confirmed, PAP attribute is up-
 * dated only after an additional event (confirmation, abort or time-out). 
 */
static void deliver_confirmed_push(long last, PPGPushMachine *pm, 
                                   PPGSessionMachine *sm)
{
    request_confirmed_push(last, pm, sm);
}

/*
 * Ppg, chapter 6.1.2.2 , subchapter delivery, says that if push is unconform-
 * ed, we can use either Po-Unit-Push.req or Po-Push.req primitive. We use Po-
 * Push.req, if have an already established session (other words, sm != NULL).
 * In addition, update PAP attribute. Return pointer to the updated push mach-
 * ine.
 */
static PPGPushMachine *deliver_unit_push(long last, PPGPushMachine *pm, 
    PPGSessionMachine *sm, int session_exists)
{
    push_machine_assert(pm);
    
    if (!session_exists)
        request_unit_push(last, pm);
    else
        request_push(last, pm);

    pm = update_push_data_with_attribute(&sm, pm, PAP_UNCONFIRMED, 
                                         PAP_DELIVERED1);
    info(0, "PPG: unconfirmed push delivered to OTA");

    return pm;
}

/*
 * Deliver all pushes queued by session machine sm (it is, make a relevant OTA
 * request). Update PAP attribute, if push is unconfirmed.
 */
static void deliver_pending_pushes(PPGSessionMachine *sm, int last)
{
    PPGPushMachine *pm;    
    long i;

    session_machine_assert(sm);
    gw_assert(gwlist_len(sm->push_machines) > 0);

    i = 0;
    while (i < gwlist_len(sm->push_machines)) {
        pm = gwlist_get(sm->push_machines, i);
        push_machine_assert(pm);

        if (pm->delivery_method == PAP_UNCONFIRMED) {
            request_push(last, pm); 
            pm = update_push_data_with_attribute(&sm, pm, PAP_UNCONFIRMED, 
                 PAP_DELIVERED1);
            remove_push_data(sm, pm, sm == NULL);
        } else {
	        request_confirmed_push(last, pm, sm);
            ++i;
        }
    }
}     

/*
 * Abort all pushes queued by session machine sm. In addition, update PAP
 * attribute and notify PI.
 */
static PPGPushMachine *abort_delivery(PPGSessionMachine *sm, int status)
{
    PPGPushMachine *pm;
    long reason,
         code;

    session_machine_assert(sm);

    pm = NULL;
    reason = PAP_ABORT_USERPND;
    code = PAP_CAPABILITIES_MISMATCH;
    
    while (gwlist_len(sm->push_machines) > 0) {
        pm = gwlist_get(sm->push_machines, 0);
        push_machine_assert(pm);

        pm = update_push_data_with_attribute(&sm, pm, reason, PAP_ABORTED);
        response_push_message(pm, code, status);

        remove_push_data(sm, pm, sm == NULL);
    }

    return pm;
}

/*
 * Remove a session, even if it have active pushes. These are aborted, and we
 * must inform PI about this. Client abort codes are defined in pap, 9.14.5,
 * which refers to wsp, Appendix A, table 35.
 */
static void remove_session_data(PPGSessionMachine *sm, int status)
{
    long code;
    PPGPushMachine *pm;

    session_machine_assert(sm);

    code = PAP_ABORT_USERPND;
    
    while (gwlist_len(sm->push_machines) > 0) {
        pm = gwlist_get(sm->push_machines, 0);
        response_push_message(pm, code, status);
        remove_push_data(sm, pm, sm == NULL);
    }

    gwlist_delete_equal(ppg_machines, sm);
    session_machine_destroy(sm);
}

/*
 * Remove session, if it has no active pushes.
 */
static void remove_pushless_session(PPGSessionMachine *sm)
{
    session_machine_assert(sm);

    if (gwlist_len(sm->push_machines) == 0) {
        gwlist_delete_equal(ppg_machines, sm);
        session_machine_destroy(sm);
    }
}

/*
 * If session machine not exist, create a session machine and store session 
 * data. If session exists, ignore. 
 * Return pointer to the session machine, and a flag did we have a session 
 * before executing this function. (Session data is needed to implement the 
 * PAP attribute. It does not mean that a session exists.)
 */
static PPGSessionMachine *store_session_data(PPGSessionMachine *sm,
    WAPEvent *e, WAPAddrTuple *tuple, int *session_exists)
{
    gw_assert(e->type == Push_Message);

    if (sm == NULL) {
        sm = session_machine_create(tuple, e);
        *session_exists = 0;
    } else
        *session_exists = 1;
    
    return sm;
}

static PPGSessionMachine *update_session_data_with_headers(
    PPGSessionMachine *sm, PPGPushMachine *pm)
{
    gwlist_delete_matching(sm->push_machines, &pm->push_id, push_has_pid);
    gwlist_append(sm->push_machines, pm);

    return sm;
}

/*
 * Ppg 6.1.2.2, subchapter delivery, states that if the delivery method is not
 * confirmed or unconfirmed, PPG may select an implementation specific type of
 * the  primitive. We use an unconfirmed push, if QoS is notspecified, and 
 * confirmed one, when it is preferconfirmed (we do support confirmed push).
 */
static int confirmation_requested(WAPEvent *e)
{
    gw_assert(e->type == Push_Message);

    return e->u.Push_Message.delivery_method == PAP_CONFIRMED || 
           e->u.Push_Message.delivery_method == PAP_PREFERCONFIRMED;
}

static int push_has_pid(void *a, void *b)
{
    long *pid;
    PPGPushMachine *pm;

    pid = b;
    pm = a;
    
    return *pid == pm->push_id;
}

static PPGPushMachine *find_ppg_push_machine_using_pid(PPGSessionMachine *sm, 
                                                   long pid)
{
    PPGPushMachine *pm;

    gw_assert(pid >= 0);
    session_machine_assert(sm);

    pm = gwlist_search(sm->push_machines, &pid, push_has_pid);

    return pm;
}

static int push_has_pi_push_id(void *a, void *b)
{
    Octstr *pi_push_id;
    PPGPushMachine *pm;

    pi_push_id = b;
    pm = a;

    return octstr_compare(pm->pi_push_id, pi_push_id) == 0;
}

static PPGPushMachine *find_ppg_push_machine_using_pi_push_id(
    PPGSessionMachine *sm, Octstr *pi_push_id)
{
    PPGPushMachine *pm;

    gw_assert(pi_push_id);
    session_machine_assert(sm);

    pm = gwlist_search(sm->push_machines, pi_push_id, push_has_pi_push_id);

    return pm;
}

static PPGPushMachine *find_unit_ppg_push_machine_using_pi_push_id(
    Octstr *pi_push_id)
{
    PPGPushMachine *pm;

    gw_assert(pi_push_id);
    pm = gwlist_search(ppg_unit_pushes, pi_push_id, push_has_pi_push_id);

    return pm;
}

/*
 * Store a new value of the push attribute into a push machine. It is to be 
 * found from the list of unit pushes, if connectionless push was asked 
 * (sm == NULL), otherwise from the the push list of the session machine sm. 
 * Returns updated push machine and session machine (this one has an updated
 * push machines list). 
 */
static PPGPushMachine *update_push_data_with_attribute(PPGSessionMachine **sm, 
    PPGPushMachine *qm, long reason, long status)
{
    push_machine_assert(qm);
   
    switch (status) {
    case PAP_UNDELIVERABLE1:
         qm->message_state = PAP_UNDELIVERABLE;
         qm->code = PAP_BAD_REQUEST;
    break;

    case PAP_UNDELIVERABLE2:
        qm->code = reason;
        qm->message_state = PAP_UNDELIVERABLE;
        qm->desc = describe_code(reason);
    break;

    case PAP_ABORTED:
        qm->message_state = status;
        qm->code = ota_abort_to_pap(reason);
        qm->event_time = set_time();
        qm->desc = describe_code(reason);
    break;

    case PAP_DELIVERED1:
        qm->message_state = PAP_DELIVERED;
        qm->delivery_method = PAP_UNCONFIRMED;
        qm->event_time = set_time();
    break;

    case PAP_DELIVERED2:
        qm->message_state = PAP_DELIVERED;
        qm->delivery_method = PAP_CONFIRMED;
        qm->event_time = set_time();
    break;

    case PAP_EXPIRED:
        qm->message_state = PAP_EXPIRED;
        qm->event_time = set_time();
        qm->desc = describe_code(reason);
    break;

    case PAP_PENDING:
        qm->message_state = PAP_PENDING;
    break;

    default:
        error(0, "WAP_PUSH_PPG: update_push_data_with_attribute: Non"
              " existing push machine status: %ld", status);
    break;
    }

    if (*sm != NULL){
        gwlist_delete_matching((**sm).push_machines, &qm->push_id, push_has_pid);
        gwlist_append((**sm).push_machines, qm);
        gwlist_delete_equal(ppg_machines, *sm);
        gwlist_append(ppg_machines, *sm);
    } else {
        gwlist_delete_matching(ppg_unit_pushes, &qm->push_id, push_has_pid);
        gwlist_append(ppg_unit_pushes, qm);
    }

    return qm;
}

/*
 * Store session id, client port and caps list received from application layer.
 */
static PPGSessionMachine *update_session_data(PPGSessionMachine *m, 
                                              long sid, long port, List *caps)
{
    session_machine_assert(m);
    gw_assert(sid >= 0);

    m->session_id = sid;
    m->addr_tuple->remote->port = port;
    m->client_capabilities = wsp_cap_duplicate_list(caps);

    gwlist_delete_equal(ppg_machines, m);
    gwlist_append(ppg_machines, m);

    return m;
}

/*
 * Convert OTA abort codes (ota 6.3.3) to corresponding PAP status codes. These
 * are defined in pap 9.14.5.
 */
static long ota_abort_to_pap(long reason)
{
    long offset;

    offset = reason - 0xEA;
    reason = 5026 + offset;

    return reason;
}

/*
 * Accept connectionless push when PI wants connectionless push and there is 
 * no sessions open.
 */
static int cless_accepted(WAPEvent *e, PPGSessionMachine *sm)
{
    gw_assert(e->type == Push_Message);
    return (e->u.Push_Message.delivery_method == PAP_UNCONFIRMED ||
           e->u.Push_Message.delivery_method == PAP_NOT_SPECIFIED) &&
           (sm == NULL);
}

/*
 * Application ids start with 0 and -1 means that default (wml.ua) was used.
 */
static int coriented_deliverable(long appid_code)
{
    return appid_code > -1;
}

/*
 * Compare PAP message timestamp, in PAP message format, and stored in octstr,
 * to gm (UTC) broken time. Return true, if before is after now, or if the 
 * service in question was not requested by PI. PAP time format is defined in 
 * pap, chapter 9.2.
 */

static void initialize_time_item_array(long time_data[], struct tm now) 
{
    time_data[0] = now.tm_year + 1900;
    time_data[1] = now.tm_mon + 1;
    time_data[2] = now.tm_mday;
    time_data[3] = now.tm_hour;
    time_data[4] = now.tm_min;
    time_data[5] = now.tm_sec;
}

static int date_item_compare(Octstr *condition, long time_data, long pos)
{
    long data;

    if (octstr_parse_long(&data, condition, pos, 10) < 0) {
        return 0;
    }
    if (data < time_data) {
        return -1;
    }
    if (data > time_data) {
        return 1;
    }

    return 0;
}

/*
 * We do not accept timestamps equalling now. Return true, if the service was
 * not requested.
 */
static int deliver_before_test_cleared(Octstr *before, struct tm now)
{  
    long time_data[6];
    long j;

    if (before == NULL)
        return 1;

    initialize_time_item_array(time_data, now);
    if (date_item_compare(before, time_data[0], 0) == 1)
        return 1;
    if (date_item_compare(before, time_data[0], 0) == -1)
        return 0;

    for (j = 5; j < octstr_len(before); j += 3) {
        if (date_item_compare(before, time_data[(j-5)/3 + 1], j) == 1)
            return 1;
        if (date_item_compare(before, time_data[(j-5)/3 + 1], j) == -1)
            return 0;
    }

    return 0;
}

/* 
 * Ditto. Return true, if after is before now (or the service was not request-
 * ed). Do not accept timestamps equalling now.
 */
static int deliver_after_test_cleared(Octstr *after, struct tm now)
{
    long time_data[6];
    long j;

    if (after == NULL)
        return 1;
    
    initialize_time_item_array(time_data, now);
    if (date_item_compare(after, time_data[0], 0) == -1)
        return 1;
    if (date_item_compare(after, time_data[0], 0) == 1)
        return 0;

    for (j = 5; j < octstr_len(after); j += 3) {
        if (date_item_compare(after, time_data[(j-5)/3 + 1], j) == -1)
            return 1;
        if (date_item_compare(after, time_data[(j-5)/3 + 1], j) == 1)
            return 0;
    }

    return 0;
}

/*
 * We exchange here server and client addresses and ports, because our WDP,
 * written for pull, exchange them, too. Similarly server address INADDR_ANY is
 * used for compability reasons, when the bearer is ip. When it is SMS, the
 * server address is global-sender.
 */
static WAPAddrTuple *set_addr_tuple(Octstr *address, long cliport, long servport, 
                                    long address_type, List *push_headers)
{
    Octstr *cliaddr;
    Octstr *from = NULL;
    WAPAddrTuple *tuple;
    
    gw_assert(address);

    if (address_type == ADDR_PLMN) {
        from = http_header_value(push_headers, octstr_imm("X-Kannel-From"));
        cliaddr = from ? from : global_sender;
    } else {
        cliaddr = octstr_imm("0.0.0.0");
    }

    tuple = wap_addr_tuple_create(address, cliport, cliaddr, servport);

    octstr_destroy(from);
    http_header_remove_all(push_headers, "X-Kannel-From");

    return tuple;
}

/*
 * We are not interested about parsing URI fully - we check only does it cont-
 * ain application id reserved by WINA or the part containing assigned code. 
 * Otherwise (regardless of it being an URI or assigned code) we simply pass 
 * it forward. 
 * These are defined by WINA at http://www.openmobilealliance.org/tech/
 * omna/omna-push-app-id.htm. We recognize both well-known and registered
 * values. X-wap-application is not added, it is considired a default.
 */

static char *wina_uri[] =
{   "*",
    "push.sia",
    "wml.ua",
    "wta.ua", 
    "mms.ua", 
    "push.syncml", 
    "loc.ua",
    "syncml.dm",
    "drm.ua",
    "emn.ua",
    "wv.ua",
    "x-wap-microsoft:localcontent.ua",
    "x-wap-microsoft:IMclient.ua",
    "x-wap-docomo:imode.mail.ua",
    "x-wap-docomo:imode.mr.ua",
    "x-wap-docomo:imode.mf.ua",
    "x-motorola:location.ua",
    "x-motorola:now.ua",
    "x-motorola:otaprov.ua",
    "x-motorola:browser.ua",
    "x-motorola:splash.ua",
    "x-wap-nai:mvsw.command",
    "x-wap-openwave:iota.ua",
    "x-wap-docomo:imode.mail2.ua",
    "x-oma-nec:otaprov.ua",
    "x-oma-nokia:call.ua"
};

#define NUMBER_OF_WINA_URIS sizeof(wina_uri)/sizeof(wina_uri[0])

/*
 * X-Wap-Application-Id header is defined in Push Message, chapter 6.2.2.1.
 * First check do we a header with an app-encoding field and a coded value. 
 * If not, try to find push application id from table of wina approved values.
 * Return coded value value of application id in question, or an error code:
 *        -1, no coded value (but defaults may be applied)
 *         0, * (meaning any application acceptable)
 *         greater or equal as 1: code for this application id 
 */
static long parse_appid_header(Octstr **appid_content)
{
    long pos,
         coded_value;
    size_t i;

    if ((pos = octstr_search(*appid_content, octstr_imm(";"), 0)) >= 0) {
        octstr_delete(*appid_content, pos, 
                      octstr_len(octstr_imm(";app-encoding=")));
        octstr_delete(*appid_content, 0, pos);         /* the URI part */
	    return -1;
    } 

    i = 0;
    while (i < NUMBER_OF_WINA_URIS) {
        if ((pos = octstr_case_search(*appid_content, 
                octstr_imm(wina_uri[i]), 0)) >= 0)
            break;
        ++i;
    }

    if (i == NUMBER_OF_WINA_URIS) {
        octstr_destroy(*appid_content);
        *appid_content = octstr_format("%ld", 2);      /* assigned number */
        return -1;                                     /* for wml ua */
    }
    
    octstr_delete(*appid_content, 0, pos);             /* again the URI */
    if ((coded_value = wsp_string_to_application_id(*appid_content)) >= 0) {
        octstr_destroy(*appid_content);
        *appid_content = octstr_format("%ld", coded_value);
        return coded_value;
    }

    return -1;
}

static WAPAddrTuple *addr_tuple_change_cliport(WAPAddrTuple *tuple, long port)
{
    WAPAddrTuple *dubble;

    if (tuple == NULL)
        return NULL;

    dubble = wap_addr_tuple_create(tuple->remote->address,
                                   port,
                                   tuple->local->address,
                                   tuple->local->port);

    return dubble;
}

/*
 * Pi uses multipart/related content type when communicating with ppg. (see 
 * pap, Chapter 8) and subtype application/xml.
 * Check if push headers are acceptable according this rule. In addition, 
 * return the field value of Content-Type header, if any and error string if
 * none (this string is used by send_bad_message_response).
 */
static int headers_acceptable(List *push_headers, Octstr **content_header)
{
    gw_assert(push_headers);
    *content_header = http_header_find_first(push_headers, "Content-Type");

    if (*content_header == NULL) {
        *content_header = octstr_format("%s", "no content type header found");
        goto error;
    }
    
    if (!type_is(*content_header, "multipart/related")) {
        goto error;
    }

    if (!type_is(*content_header, "application/xml")) {
        goto error;
    }

    return 1;

error:
    warning(0, "PPG: headers_acceptable: got unacceptable push headers");
    return 0;
}

/*
 * Content-Type header field is defined in rfc 1521, chapter 4. We are looking
 * after type multipart/related or "multipart/related" and parameter 
 * type=application/xml or type="application/xml", as required by pap, chapter
 * 8.
 */
static int type_is(Octstr *content_header, char *name)
{
    Octstr *quoted_type,
           *osname;

    osname = octstr_imm(name);
    if (octstr_case_search(content_header, osname, 0) >= 0)
        return 1;

    quoted_type = octstr_format("\"%S\"", osname);

    if (octstr_case_search(content_header, quoted_type, 0) >= 0) {
        octstr_destroy(quoted_type);
        return 1;
    }

    octstr_destroy(quoted_type);
    return 0;
}

/*
 * Again looking after a parameter, this time of type boundary=XXX or boundary=
 * "XXX".
 */
static int get_mime_boundary(List *push_headers, Octstr *content_header, 
                             Octstr **boundary)
{
    long pos;
    Octstr *bos;
    int c, quoted = 0;
    long bstart;

    pos = 0;
    if ((pos = octstr_case_search(content_header, 
                                  bos = octstr_imm("boundary="), 0)) < 0) {
        warning(0, "PPG: get_mime_boundary: no boundary specified");
        return -1;
    }

    pos += octstr_len(bos);
    if (octstr_get_char(content_header, pos) == '"') {
        ++pos;
        quoted = 1;
    }
    
    bstart = pos;
    while ((c = octstr_get_char(content_header, pos)) != -1) {
        if (c == ';' || (quoted && c == '"') || (!quoted && c == ' '))
             break;
        ++pos;
    }
    *boundary = octstr_copy(content_header, bstart, pos - bstart);

    return 0;
}

static void change_header_value(List **push_headers, char *name, char *value)
{
    http_header_remove_all(*push_headers, name);
    http_header_add(*push_headers, name, value);
}

/*
 * Some application level protocols may use MIME headers. This may cause problems
 * to them. (MIME version is a mandatory header).
 */
static void remove_mime_headers(List **push_headers)
{
    http_header_remove_all(*push_headers, "MIME-Version");
}

/*
 * There are headers used only for HTTP POST pi->ppg. (For debugging, mainly.)
 */
static void remove_link_headers(List **push_headers)
{
    http_header_remove_all(*push_headers, "Host");
}

/*
 * X-Kannel headers are used to control Kannel ppg.
 */
static void remove_x_kannel_headers(List **push_headers)
{
    http_header_remove_all(*push_headers, "X-Kannel-SMSC");
    http_header_remove_all(*push_headers, "X-Kannel-DLR-Url");
    http_header_remove_all(*push_headers, "X-Kannel-DLR-Mask");
    http_header_remove_all(*push_headers, "X-Kannel-Smsbox-Id");
}

/*
 * Badmessage-response element is redefined in pap, implementation note, 
 * chapter 5. Do not add to the document a fragment being NULL or empty.
 * Return current status of HTTPClient.
 */
static void send_bad_message_response(HTTPClient **c, Octstr *fragment, 
                                      int code, int status)
{
    Octstr *reply_body;

    reply_body = octstr_format("%s", 
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP 1.0//EN\""
                   " \"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
        "<pap>"
             "<badmessage-response code=\"");
    octstr_format_append(reply_body, "%d", code);
    octstr_format_append(reply_body, "%s", "\""
                  " desc=\"");
    octstr_format_append(reply_body, "%s", "Not understood due to malformed"
                                           " syntax");
    octstr_format_append(reply_body, "%s", "\"");

    if (fragment != NULL && octstr_len(fragment) != 0) {
        octstr_format_append(reply_body, "%s", " bad-message-fragment=\"");
        octstr_format_append(reply_body, "%S", escape_fragment(fragment));
        octstr_format_append(reply_body, "%s", "\"");
    }

    octstr_format_append(reply_body, "%s", ">"
              "</badmessage-response>"
         "</pap>");

    debug("wap.push.ppg", 0, "PPG: send_bad_message_response: telling pi");
    send_to_pi(c, reply_body, status);

    octstr_destroy(fragment);
}

/*
 * Push response is defined in pap, chapter 9.3. Mapping between push ids and
 * http clients is done by using http_clients. We remove (push id, http client)
 * pair from the dictionary after the mapping has been done.
 * Return current status of HTTPClient.
 */
static HTTPClient *send_push_response(WAPEvent *e, int status)
{
    Octstr *reply_body,
           *url;
    HTTPClient *c;

    gw_assert(e->type == Push_Response);
    url = dict_get(urls, e->u.Push_Response.pi_push_id);
    dict_remove(urls, e->u.Push_Response.pi_push_id);

    reply_body = octstr_format("%s", 
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP 1.0//EN\""
                   " \"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
        "<pap>"
             "<push-response push-id=\"");
    octstr_format_append(reply_body, "%S", e->u.Push_Response.pi_push_id);
    octstr_format_append(reply_body, "%s", "\""); 

    if (e->u.Push_Response.sender_name != NULL) {
        octstr_format_append(reply_body, "%s",
                   " sender-name=\"");
        octstr_format_append(reply_body, "%S", 
            e->u.Push_Response.sender_name);
        octstr_format_append(reply_body, "%s", "\"");
    }

    if (e->u.Push_Response.reply_time != NULL) {
        octstr_format_append(reply_body, "%s",
                   " reply-time=\"");
        octstr_format_append(reply_body, "%S", 
            e->u.Push_Response.reply_time);
        octstr_format_append(reply_body, "%s", "\"");
    }

    if (url != NULL) {
        octstr_format_append(reply_body, "%s",
                   " sender-address=\"");
        octstr_format_append(reply_body, "%S", url);
        octstr_format_append(reply_body, "%s", "\"");
    }

    octstr_format_append(reply_body, "%s", ">"
             "<response-result code =\"");
    octstr_format_append(reply_body, "%d", e->u.Push_Response.code);
    octstr_format_append(reply_body, "%s", "\"");

    if (e->u.Push_Response.desc != NULL) {
        octstr_format_append(reply_body, "%s", " desc=\"");
        octstr_format_append(reply_body, "%S", e->u.Push_Response.desc);
        octstr_format_append(reply_body, "\"");
    }
    
    octstr_format_append(reply_body, "%s", ">"
              "</response-result>"
              "</push-response>"
          "</pap>");

    octstr_destroy(url);

    c = dict_get(http_clients, e->u.Push_Response.pi_push_id);
    dict_remove(http_clients, e->u.Push_Response.pi_push_id);

    debug("wap.push.ppg", 0, "PPG: send_push_response: telling pi");
    send_to_pi(&c, reply_body, status);

    wap_event_destroy(e);
    return c;
}

/*
 * Ppg notifies pi about duplicate push id by sending a push response document
 * to it. Note that we never put the push id to the dict in this case.
 * Return current c value.
 */
static void tell_fatal_error(HTTPClient **c, WAPEvent *e, Octstr *url, 
                             int status, int code)
{
    Octstr *reply_body,
           *dos,                      /* temporaries */
           *tos,
           *sos;

    gw_assert(e->type == Push_Message);
    reply_body = octstr_format("%s", 
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP 1.0//EN\""
                   " \"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
        "<pap>"
             "<push-response push-id=\"");
    octstr_format_append(reply_body, "%S", e->u.Push_Message.pi_push_id);
    octstr_format_append(reply_body, "%s", "\""); 

    octstr_format_append(reply_body, "%s",
                   " sender-name=\"");
    octstr_format_append(reply_body, "%S", tos = tell_ppg_name());
    octstr_format_append(reply_body, "%s", "\"");

    octstr_format_append(reply_body, "%s",
                   " reply-time=\"");
    octstr_format_append(reply_body, "%S", sos = set_time());
    octstr_format_append(reply_body, "%s", "\"");

    octstr_format_append(reply_body, "%s",
                   " sender-address=\"");
    octstr_format_append(reply_body, "%S", url);
    octstr_format_append(reply_body, "%s", "\"");

    octstr_format_append(reply_body, "%s", ">"
             "<response-result code =\"");
    octstr_format_append(reply_body, "%d", code);
    octstr_format_append(reply_body, "%s", "\"");

    octstr_format_append(reply_body, "%s", " desc=\"");
    octstr_format_append(reply_body, "%S", dos = describe_code(code));
    octstr_format_append(reply_body, "\"");

    octstr_format_append(reply_body, "%s", ">"
              "</response-result>"
              "</push-response>"
         "</pap>");

    debug("wap.push.ppg", 0, "PPG: tell_fatal_error: %s", octstr_get_cstr(dos));
    send_to_pi(c, reply_body, status);

    octstr_destroy(dos);
    octstr_destroy(tos);
    octstr_destroy(sos);
    wap_event_destroy(e);
}

/*
 * Does the HTTP reply to pi. Test has HTTPClient disappeared.
 * Return current c value.
 */
static void send_to_pi(HTTPClient **c, Octstr *reply_body, int status) {
    List *reply_headers;

    reply_headers = http_create_empty_headers();
    http_header_add(reply_headers, "Content-Type", "application/xml");

    if (*c != NULL)
        http_send_reply(*c, status, reply_headers, reply_body);

    octstr_destroy(reply_body);
    http_destroy_headers(reply_headers);
}

/*
 * Escape characters not allowed in the value of an attribute. Pap does not 
 * define escape sequences for message fragments; try common xml ones.
 */

static Octstr *escape_fragment(Octstr *fragment)
{
    long i;
    int c;

    i = 0;
    while (i < octstr_len(fragment)) {
        if ((c = octstr_get_char(fragment, i)) == '"') {
            replace_octstr_char(fragment, octstr_imm("&quot;"), &i);
        } else if (c == '<') {
            replace_octstr_char(fragment, octstr_imm("&lt;"), &i);
        } else if (c == '>') {
            replace_octstr_char(fragment, octstr_imm("&gt;"), &i);
        } else if (c == '&') {
            replace_octstr_char(fragment, octstr_imm("&amp;"), &i);
        }
        ++i;
    }

    return fragment;
}

static int is_phone_number(long address_type)
{
    return address_type == ADDR_PLMN;
}

static void replace_octstr_char(Octstr *os1, Octstr *os2, long *pos)
{
    octstr_delete(os1, *pos, 1);
    octstr_insert(os1, os2, *pos);
    *pos += octstr_len(os2) - 1;
}

/* 
 * Check if we have an explicit routing information 
 *       a) first check x-kannel header
 *       b) then ppg user specific routing (if there is any groups; we have none,
 *          if pi is trusted)
 *       c) then global ppg routing
 *       d) if all these failed, return NULL
 */
static Octstr *set_smsc_id(List *headers, Octstr *username, int trusted_pi)
{
    Octstr *smsc_id = NULL;

    smsc_id = http_header_value(headers, octstr_imm("X-Kannel-SMSC"));
    if (smsc_id) {
        return smsc_id;
    }

    if (!trusted_pi)
        smsc_id = wap_push_ppg_pushuser_smsc_id_get(username);

    smsc_id = smsc_id ? 
        smsc_id : (ppg_default_smsc ? octstr_duplicate(ppg_default_smsc) : NULL);

    return smsc_id;
}

/* 
 * Checking for dlr url, using  following order:
 *       a) first check X-Kannel header  
 *       b) then ppg user specific dlr url (if there is any user group; if pi is
 *          trusted, there are none.)
 *       c) then global ppg dlr url
 *       d) if all these failed, return NULL
 */
static Octstr *set_dlr_url(List *headers, Octstr *username, int trusted_pi)
{
    Octstr *dlr_url = NULL;

    dlr_url = http_header_value(headers, octstr_imm("X-Kannel-DLR-Url"));
    if (dlr_url) {
        return dlr_url;
    }

    if (!trusted_pi)
        dlr_url = wap_push_ppg_pushuser_dlr_url_get(username);

    dlr_url = dlr_url ?
        dlr_url : (ppg_dlr_url ? octstr_duplicate(ppg_dlr_url) : NULL);

    return dlr_url;
}

/* 
 * Checking for dlr mask. Mask without dlr url is of course useless.
 * We reject (some) non-meaningfull values of dlr_mask. Value indic-
 * ating rejection is 0.
 */
static long set_dlr_mask(List *headers, Octstr *dlr_url)
{
    Octstr *dlrmaskos;
    long dlr_mask;
    long masklen;    

    dlrmaskos = http_header_value(headers, octstr_imm("X-Kannel-DLR-Mask"));
    if (dlrmaskos == NULL) { 
        return 0;
    }

    if ((masklen = octstr_parse_long(&dlr_mask, dlrmaskos, 0, 10)) != -1 &&
             masklen == octstr_len(dlrmaskos) &&
             dlr_mask >= -1 && dlr_mask <= 31) {
         octstr_destroy(dlrmaskos);
         return dlr_mask;
    }

    warning(0, "unparsable dlr mask, rejected");
    octstr_destroy(dlrmaskos);
    return 0;
}

/*
 * Checking for dlr smsbox id, using  following order:
 *       a) first check X-Kannel header
 *       b) then ppg user specific smsbox id, if there is any group; if pi
 *          is trusted, there are none
 *       c) then global ppg smsbox id
 *       d) if all these failed, return NULL
 */

static Octstr *set_smsbox_id(List *headers, Octstr *username, int trusted_pi)
{
    Octstr *smsbox_id = NULL;

    smsbox_id = http_header_value(headers, octstr_imm("X-Kannel-Smsbox-Id"));
    if (smsbox_id != NULL) {
        return smsbox_id;
    }

    if (!trusted_pi)
        smsbox_id = wap_push_ppg_pushuser_smsbox_id_get(username);

    smsbox_id = smsbox_id ?
        smsbox_id : (ppg_smsbox_id ? octstr_duplicate(ppg_smsbox_id) : NULL);

    return smsbox_id;

}

/*
 * Service is ppg core group only configuration variable
 */ 
static Octstr *set_service_name(void)
{
    return octstr_duplicate(service_name);
}




