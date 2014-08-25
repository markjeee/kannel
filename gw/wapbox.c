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
 * wapbox.c - main program for wapbox
 *
 * This module contains the main program for the WAP box of the WAP gateway.
 * See the architecture documentation for details.
 */

#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "shared.h"
#include "wml_compiler.h"
#include "heartbeat.h"
#include "wap/wap.h"
#include "wap-appl.h"
#include "wap-maps.h"
#include "wap_push_ota.h"
#include "wap_push_ppg.h"
#include "gw/msg.h"
#include "bb.h"
#include "sms.h"
#ifdef HAVE_WTLS_OPENSSL
#include <openssl/x509.h>
#include "wap/wtls.h"
#include "gwlib/pki.h"
#endif
#include "radius/radius_acct.h"

static void config_reload(int reload);
static long logfilelevel=-1;

enum {
    CONNECTIONLESS_PORT = 9200,
    CONNECTION_ORIENTED_PORT = 9201,
    WTLS_CONNECTIONLESS_PORT = 9202,
    WTLS_CONNECTION_ORIENTED_PORT = 9203
};

enum { DEFAULT_TIMER_FREQ = 1};

static Octstr *bearerbox_host;
static long bearerbox_port = BB_DEFAULT_WAPBOX_PORT;
static int bearerbox_ssl = 0;
static Counter *sequence_counter = NULL;
static long timer_freq = DEFAULT_TIMER_FREQ;
static Octstr *config_filename;

/* use strict XML parsing or relaxed */
static int wml_xml_strict = 1;

/* smart error messaging related globals */
int wsp_smart_errors = 0;
Octstr *device_home = NULL;

/* Controlling segmentation of sms messages sent by wapbox (push related).*/
int concatenation = 1;
long max_messages = 10;

#ifdef HAVE_WTLS_OPENSSL
extern RSA* private_key;
extern X509* x509_cert;
extern void wtls_secmgr_init();
#endif

static Cfg *init_wapbox(Cfg *cfg)
{
    CfgGroup *grp;
    Octstr *s;
    Octstr *logfile;
    int lf, m;
    long value;

    lf = m = 1;

    cfg_dump(cfg);
    
    /*
     * Extract info from the core group.
     */
    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    if (grp == NULL)
    	panic(0, "No 'core' group in configuration.");
    
    if (cfg_get_integer(&bearerbox_port,grp,octstr_imm("wapbox-port")) == -1)
        panic(0, "No 'wapbox-port' in core group");
#ifdef HAVE_LIBSSL
    cfg_get_bool(&bearerbox_ssl, grp, octstr_imm("wapbox-port-ssl"));
#endif /* HAVE_LIBSSL */
    
    /* load parameters that could be later reloaded */
    config_reload(0);
    
    conn_config_ssl(grp);

    /*
     * And the rest of the pull info comes from the wapbox group.
     */
    grp = cfg_get_single_group(cfg, octstr_imm("wapbox"));
    if (grp == NULL)
        panic(0, "No 'wapbox' group in configuration.");
    
    bearerbox_host = cfg_get(grp, octstr_imm("bearerbox-host"));
    if (cfg_get_integer(&timer_freq, grp, octstr_imm("timer-freq")) == -1)
        timer_freq = DEFAULT_TIMER_FREQ;

    logfile = cfg_get(grp, octstr_imm("log-file"));
    if (logfile != NULL) {
        log_open(octstr_get_cstr(logfile), logfilelevel, GW_NON_EXCL);
        info(0, "Starting to log to file %s level %ld", 
             octstr_get_cstr(logfile), logfilelevel);
    }
    octstr_destroy(logfile);

    if ((s = cfg_get(grp, octstr_imm("syslog-level"))) != NULL) {
        long level;
        Octstr *facility;
        if ((facility = cfg_get(grp, octstr_imm("syslog-facility"))) != NULL) {
            log_set_syslog_facility(octstr_get_cstr(facility));
            octstr_destroy(facility);
        }
        if (octstr_compare(s, octstr_imm("none")) == 0) {
            log_set_syslog(NULL, 0);
            debug("wap", 0, "syslog parameter is none");
        } else if (octstr_parse_long(&level, s, 0, 10) > 0) {
            log_set_syslog("wapbox", level);
            debug("wap", 0, "syslog parameter is %ld", level);
        }
        octstr_destroy(s);
    } else {
        log_set_syslog(NULL, 0);
        debug("wap", 0, "no syslog parameter");
    }

    /* determine which timezone we use for access logging */
    if ((s = cfg_get(grp, octstr_imm("access-log-time"))) != NULL) {
        lf = (octstr_case_compare(s, octstr_imm("gmt")) == 0) ? 0 : 1;
        octstr_destroy(s);
    }

    /* should predefined markers be used, ie. prefixing timestamp */
    cfg_get_bool(&m, grp, octstr_imm("access-log-clean"));

    /* open access-log file */
    if ((s = cfg_get(grp, octstr_imm("access-log"))) != NULL) {
        info(0, "Logging accesses to '%s'.", octstr_get_cstr(s));
        alog_open(octstr_get_cstr(s), lf, m ? 0 : 1);
        octstr_destroy(s);
    }

    if (cfg_get_integer(&value, grp, octstr_imm("http-timeout")) == 0)
       http_set_client_timeout(value);

    /* configure the 'wtls' group */
#if (HAVE_WTLS_OPENSSL)
    /* Load up the necessary keys */
    grp = cfg_get_single_group(cfg, octstr_imm("wtls"));
  
    if (grp != NULL) {
        if ((s = cfg_get(grp, octstr_imm("certificate-file"))) != NULL) {
            if (octstr_compare(s, octstr_imm("none")) == 0) {
                debug("bbox", 0, "certificate file not set");
            } else {
                /* Load the certificate into the necessary parameter */
                get_cert_from_file(s, &x509_cert);
                gw_assert(x509_cert != NULL);
                debug("bbox", 0, "certificate parameter is %s",
                   octstr_get_cstr(s));
            }
            octstr_destroy(s);
        } else
            panic(0, "No 'certificate-file' in wtls group");

        if ((s = cfg_get(grp, octstr_imm("privatekey-file"))) != NULL) {
            Octstr *password;
            password = cfg_get(grp, octstr_imm("privatekey-password"));
            if (octstr_compare(s, octstr_imm("none")) == 0) {
                debug("bbox", 0, "privatekey-file not set");
            } else {
                /* Load the private key into the necessary parameter */
                get_privkey_from_file(s, &private_key, password);
                gw_assert(private_key != NULL);
                debug("bbox", 0, "certificate parameter is %s",
                   octstr_get_cstr(s));
            }
            if (password != NULL)
                octstr_destroy(password);
            octstr_destroy(s);
        } else
            panic(0, "No 'privatekey-file' in wtls group");
    }
#endif

    /*
     * Check if we have a 'radius-acct' proxy group and start the
     * corresponding thread for the proxy.
     */
    grp = cfg_get_single_group(cfg, octstr_imm("radius-acct"));
    if (grp) {
        radius_acct_init(grp);
    }

    /*
     * We pass ppg configuration groups to the ppg module.
     */   
    grp = cfg_get_single_group(cfg, octstr_imm("ppg"));
    if (grp == NULL) { 
        cfg_destroy(cfg);
        return NULL;
    }

    return cfg;
}


static void signal_handler(int signum) 
{
    /* 
     * On some implementations (i.e. linuxthreads), signals are delivered
     * to all threads.  We only want to handle each signal once for the
     * entire box, and we let the gwthread wrapper take care of choosing
     * one. */
    if (!gwthread_shouldhandlesignal(signum))
        return;
    
    switch (signum) {
        case SIGINT:
        case SIGTERM:
            if (program_status != shutting_down) {
                error(0, "SIGINT or SIGTERM received, let's die.");
                program_status = shutting_down;
                break;
            }
            break;
    
        case SIGHUP:
            warning(0, "SIGHUP received, catching and re-opening logs");
            config_reload(1);
            log_reopen();
            alog_reopen();
            break;
    
        /* 
         * It would be more proper to use SIGUSR1 for this, but on some
         * platforms that's reserved by the pthread support. 
         */
        case SIGQUIT:
            warning(0, "SIGQUIT received, reporting memory usage.");
            gw_check_leaks();
            break;
        }
}


static void setup_signal_handlers(void) 
{
    struct sigaction act;
    
    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGPIPE, &act, NULL);
}

/*
 * We create wdp_datagram for IP traffic and sms for SMS traffic. 
 */
static Msg *pack_ip_datagram(WAPEvent *dgram)
{
    Msg *msg;
    WAPAddrTuple *tuple;

    msg = msg_create(wdp_datagram);
    tuple = dgram->u.T_DUnitdata_Req.addr_tuple;
    msg->wdp_datagram.source_address =
        octstr_duplicate(tuple->local->address);
    msg->wdp_datagram.source_port =
        dgram->u.T_DUnitdata_Req.addr_tuple->local->port;
    msg->wdp_datagram.destination_address =
        octstr_duplicate(tuple->remote->address);
    msg->wdp_datagram.destination_port =
        dgram->u.T_DUnitdata_Req.addr_tuple->remote->port;
    msg->wdp_datagram.user_data =
        octstr_duplicate(dgram->u.T_DUnitdata_Req.user_data);

   return msg;
}

/*
 * Format for port UDH is defined in wdp, appendix A. It is %06%05%04
 * %dest port high hex%dest port low%hex source port high hex%source port low
 * hex. (Unsecure) push client port itself is 2948.
 */
static Octstr *pack_udhdata(WAPAddrTuple *tuple)
{
    int source_port,
        dest_port;
    Octstr *udh;
    
    source_port = tuple->local->port;
    dest_port = tuple->remote->port;  
    
    udh = octstr_create("");
    octstr_format_append(udh, "%c", 6);
    octstr_format_append(udh, "%c", 5);
    octstr_format_append(udh, "%c", 4);
    octstr_format_append(udh, "%c", (dest_port >> 8) & 0xff);
    octstr_format_append(udh, "%c", dest_port & 0xff);
    octstr_format_append(udh, "%c", (source_port >> 8) & 0xff);
    octstr_format_append(udh, "%c", source_port & 0xff);

    return udh;
}

/*
 * We send a normal 8-bit unconcatenated binary  message with an udh. Caller 
 * must do segmentation before calling this function.
 *
 * Note: we have hardcoded validity period here. We must eventually use push
 * control document to fill this field. 
 */
static Msg *pack_sms_datagram(WAPEvent *dgram)
{
    Msg *msg;
    WAPAddrTuple *tuple;

    msg = msg_create(sms);
    tuple = dgram->u.T_DUnitdata_Req.addr_tuple;
    msg->sms.sender = octstr_duplicate(tuple->local->address);
    msg->sms.receiver = octstr_duplicate(tuple->remote->address);
    msg->sms.udhdata = pack_udhdata(tuple);
    msg->sms.msgdata = octstr_duplicate(dgram->u.T_DUnitdata_Req.user_data);
    msg->sms.time = time(NULL);
    if (dgram->u.T_DUnitdata_Req.smsc_id != NULL)
        msg->sms.smsc_id = octstr_duplicate(dgram->u.T_DUnitdata_Req.smsc_id);
    else
        msg->sms.smsc_id = NULL;
    msg->sms.dlr_mask = dgram->u.T_DUnitdata_Req.dlr_mask;
    if (dgram->u.T_DUnitdata_Req.smsbox_id != NULL)
        msg->sms.boxc_id = octstr_duplicate(dgram->u.T_DUnitdata_Req.smsbox_id);
    else
        msg->sms.boxc_id = NULL;
    if (dgram->u.T_DUnitdata_Req.dlr_url != NULL)
        msg->sms.dlr_url = octstr_duplicate(dgram->u.T_DUnitdata_Req.dlr_url);
    else
        msg->sms.dlr_url = NULL;
    msg->sms.sms_type = mt_push;
    msg->sms.mwi = MWI_UNDEF;
    msg->sms.coding = DC_8BIT;
    msg->sms.mclass = MC_UNDEF;
    msg->sms.validity = time(NULL) + 1440;
    msg->sms.deferred = SMS_PARAM_UNDEFINED;
    if (dgram->u.T_DUnitdata_Req.service_name != NULL)
        msg->sms.service = octstr_duplicate(dgram->u.T_DUnitdata_Req.service_name);
    
    return msg;   
}

/*
 * Possible address types
 */

enum {
    ADDR_IPV4 = 0,
    ADDR_PLMN = 1,
    ADDR_USER = 2,
    ADDR_IPV6 = 3,
    ADDR_WINA = 4
};

/*
 * Send IP datagram as it is, segment SMS datagram if necessary.
 */
static void dispatch_datagram(WAPEvent *dgram)
{
    Msg *msg, *part;
    List *sms_datagrams;
    static unsigned long msg_sequence = 0L;   /* Used only by this function */

    msg = part = NULL;
    sms_datagrams = NULL;

    if (dgram == NULL) {
        error(0, "WDP: dispatch_datagram received empty datagram, ignoring.");
    } 
    else if (dgram->type != T_DUnitdata_Req) {
        warning(0, "WDP: dispatch_datagram received event of unexpected type.");
        wap_event_dump(dgram);
    } 
    else if (dgram->u.T_DUnitdata_Req.address_type == ADDR_IPV4) {
#ifdef HAVE_WTLS_OPENSSL
      if (dgram->u.T_DUnitdata_Req.addr_tuple->local->port >= WTLS_CONNECTIONLESS_PORT)
         wtls_dispatch_resp(dgram);
      else
#endif /* HAVE_WTLS_OPENSSL */
      {
	   msg = pack_ip_datagram(dgram);
       write_to_bearerbox(msg);
      }
    } else {
        msg_sequence = counter_increase(sequence_counter) & 0xff;
        msg = pack_sms_datagram(dgram);
        sms_datagrams = sms_split(msg, NULL, NULL, NULL, NULL, concatenation, 
                                  msg_sequence, max_messages, MAX_SMS_OCTETS);
        debug("wap",0,"WDP (wapbox): delivering %ld segments to bearerbox",
              gwlist_len(sms_datagrams));
        while ((part = gwlist_extract_first(sms_datagrams)) != NULL) {
	       write_to_bearerbox(part);
        }

        gwlist_destroy(sms_datagrams, NULL);
        msg_destroy(msg);
    }
    wap_event_destroy(dgram);

}


/*
 * Reloading functions
 */

static void reload_int(int reload, Octstr *desc, long *o, long *n)
{
    if (reload && *o != *n) {
        info(0, "Reloading int '%s' from %ld to %ld", 
             octstr_get_cstr(desc), *o, *n);
        *o = *n;
    }
}

static void reload_bool(int reload, Octstr *desc, int *o, int *n)
{
    if (reload && *o != *n) {
        info(0, "Reloading bool '%s' from %s to %s", 
             octstr_get_cstr(desc), 
             (*o ? "yes" : "no"), (*n ? "yes" : "no"));
        *o = *n;
    }
}


/*
 * Read all reloadable configuration directives
 */
static void config_reload(int reload) {
    Cfg *cfg;
    CfgGroup *grp;
    List *groups;
    long map_url_max;
    Octstr *s;
    long i;
    long new_value;
    int new_bool;
    Octstr *http_proxy_host;
    Octstr *http_interface_name;
    long http_proxy_port;
    int http_proxy_ssl = 0;
    List *http_proxy_exceptions;
    Octstr *http_proxy_username;
    Octstr *http_proxy_password;
    Octstr *http_proxy_exceptions_regex;
    int warn_map_url = 0;

    /* XXX TO-DO: if(reload) implement wapbox.suspend/mutex.lock */
    
    if (reload)
        debug("config_reload", 0, "Reloading configuration");

    /* 
     * NOTE: we could lstat config file and only reload if it was modified, 
     * but as we have a include directive, we don't know every file's
     * timestamp at this point
     */

    cfg = cfg_create(config_filename);

    if (cfg_read(cfg) == -1) {
        warning(0, "Couldn't %sload configuration from `%s'.", 
                   (reload ? "re" : ""), octstr_get_cstr(config_filename));
        return;
    }

    grp = cfg_get_single_group(cfg, octstr_imm("core"));

    http_proxy_host = cfg_get(grp, octstr_imm("http-proxy-host"));
    http_proxy_port =  -1;
    cfg_get_integer(&http_proxy_port, grp, octstr_imm("http-proxy-port"));
#ifdef HAVE_LIBSSL
    cfg_get_bool(&http_proxy_ssl, grp, octstr_imm("http-proxy-ssl"));
#endif /* HAVE_LIBSSL */
    http_proxy_username = cfg_get(grp, octstr_imm("http-proxy-username"));
    http_proxy_password = cfg_get(grp, octstr_imm("http-proxy-password"));
    http_proxy_exceptions = cfg_get_list(grp, octstr_imm("http-proxy-exceptions"));
    http_proxy_exceptions_regex = cfg_get(grp, octstr_imm("http-proxy-exceptions-regex"));
    if (http_proxy_host != NULL && http_proxy_port > 0) {
        http_use_proxy(http_proxy_host, http_proxy_port, http_proxy_ssl,
                       http_proxy_exceptions, http_proxy_username, 
                       http_proxy_password, http_proxy_exceptions_regex);
    }
    octstr_destroy(http_proxy_host);
    octstr_destroy(http_proxy_username);
    octstr_destroy(http_proxy_password);
    octstr_destroy(http_proxy_exceptions_regex);
    gwlist_destroy(http_proxy_exceptions, octstr_destroy_item);

    grp = cfg_get_single_group(cfg, octstr_imm("wapbox"));
    if (grp == NULL) {
        warning(0, "No 'wapbox' group in configuration.");
        return;
    }
    
    if (cfg_get_integer(&new_value, grp, octstr_imm("log-level")) != -1) {
        reload_int(reload, octstr_imm("log level"), &logfilelevel, &new_value);
        logfilelevel = new_value;
        log_set_log_level(new_value);
    }

    /* Configure interface name for http requests */
    http_interface_name = cfg_get(grp, octstr_imm("http-interface-name"));
    if (http_interface_name != NULL) {
        http_set_interface(http_interface_name);
        octstr_destroy(http_interface_name);
    }

    /* 
     * users may define 'smart-errors' to have WML decks returned with
     * error information instead of signaling using the HTTP reply codes
     */
    cfg_get_bool(&new_bool, grp, octstr_imm("smart-errors"));
    reload_bool(reload, octstr_imm("smart error messaging"), &wsp_smart_errors, &new_bool);

    /* decide if our XML parser within WML compiler is strict or relaxed */
    cfg_get_bool(&new_bool, grp, octstr_imm("wml-strict"));
    reload_bool(reload, octstr_imm("XML within WML has to be strict"), 
                &wml_xml_strict, &new_bool);
    if (!wml_xml_strict)
        warning(0, "'wml-strict' config directive has been set to no, "
                   "this may make you vulnerable against XML bogus input.");

    if (cfg_get_bool(&new_bool, grp, octstr_imm("concatenation")) == 1)
        reload_bool(reload, octstr_imm("concatenation"), &concatenation, &new_bool);
    else
        concatenation = 1;

    if (cfg_get_integer(&new_value, grp, octstr_imm("max-messages")) != -1) {
        max_messages = new_value;
        reload_int(reload, octstr_imm("max messages"), &max_messages, &new_value);
    }

    /* configure URL mappings */
    map_url_max = -1;
    cfg_get_integer(&map_url_max, grp, octstr_imm("map-url-max"));
    if (map_url_max > 0)
        warn_map_url = 1;

    if (reload) { /* clear old map */
        wap_map_destroy();
        wap_map_user_destroy();
    }
	
    if ((device_home = cfg_get(grp, octstr_imm("device-home"))) != NULL) {
        wap_map_url_config_device_home(octstr_get_cstr(device_home));
    }
    if ((s = cfg_get(grp, octstr_imm("map-url"))) != NULL) {
        warn_map_url = 1;
        wap_map_url_config(octstr_get_cstr(s));
        octstr_destroy(s);
    }
    debug("wap", 0, "map_url_max = %ld", map_url_max);

    for (i = 0; i <= map_url_max; i++) {
        Octstr *name;
        name = octstr_format("map-url-%d", i);
        if ((s = cfg_get(grp, name)) != NULL)
            wap_map_url_config(octstr_get_cstr(s));
        octstr_destroy(name);
    }

    /* warn the user that he/she should use the new wap-url-map groups */
    if (warn_map_url)
        warning(0, "'map-url' config directive and related are deprecated, "
                   "please use wap-url-map group");

    /* configure wap-url-map */
    groups = cfg_get_multi_group(cfg, octstr_imm("wap-url-map"));
    while (groups && (grp = gwlist_extract_first(groups)) != NULL) {
        Octstr *name, *url, *map_url, *send_msisdn_query;
        Octstr *send_msisdn_header, *send_msisdn_format;
        int accept_cookies;

        name = cfg_get(grp, octstr_imm("name"));
        url = cfg_get(grp, octstr_imm("url"));
        map_url = cfg_get(grp, octstr_imm("map-url"));
        send_msisdn_query = cfg_get(grp, octstr_imm("send-msisdn-query"));
        send_msisdn_header = cfg_get(grp, octstr_imm("send-msisdn-header"));
        send_msisdn_format = cfg_get(grp, octstr_imm("send-msisdn-format"));
        accept_cookies = -1;
        cfg_get_bool(&accept_cookies, grp, octstr_imm("accept-cookies"));

        wap_map_add_url(name, url, map_url, send_msisdn_query, send_msisdn_header,
                        send_msisdn_format, accept_cookies);

        info(0, "Added wap-url-map <%s> with url <%s>, map-url <%s>, "
                "send-msisdn-query <%s>, send-msisdn-header <%s>, "
                "send-msisdn-format <%s>, accept-cookies <%s>", 
             octstr_get_cstr(name), octstr_get_cstr(url), 
             octstr_get_cstr(map_url), octstr_get_cstr(send_msisdn_query), 
             octstr_get_cstr(send_msisdn_header), 
             octstr_get_cstr(send_msisdn_format), (accept_cookies ? "yes" : "no"));
    }
    gwlist_destroy(groups, NULL);

    /* configure wap-user-map */
    groups = cfg_get_multi_group(cfg, octstr_imm("wap-user-map"));
    while (groups && (grp = gwlist_extract_first(groups)) != NULL) {
        Octstr *name, *user, *pass, *msisdn;

        name = cfg_get(grp, octstr_imm("name"));
        user = cfg_get(grp, octstr_imm("user"));
        pass = cfg_get(grp, octstr_imm("pass"));
        msisdn = cfg_get(grp, octstr_imm("msisdn"));
           
        wap_map_add_user(name, user, pass, msisdn);

        info(0,"Added wap-user-map <%s> with credentials <%s:%s> "
               "and MSISDN <%s>", octstr_get_cstr(name),
             octstr_get_cstr(user), octstr_get_cstr(pass),
             octstr_get_cstr(msisdn));
    }
    gwlist_destroy(groups, NULL);

    cfg_destroy(cfg);
    /* XXX TO-DO: if(reload) implement wapbox.resume/mutex.unlock */
}

int main(int argc, char **argv) 
{
    int cf_index;
    int restart = 0;
    Msg *msg;
    Cfg *cfg;
    double heartbeat_freq =  DEFAULT_HEARTBEAT;
    
    gwlib_init();
    cf_index = get_and_set_debugs(argc, argv, NULL);
    
    setup_signal_handlers();
    
    if (argv[cf_index] == NULL)
        config_filename = octstr_create("kannel.conf");
    else
        config_filename = octstr_create(argv[cf_index]);
    cfg = cfg_create(config_filename);

    if (cfg_read(cfg) == -1)
        panic(0, "Couldn't read configuration from `%s'.", octstr_get_cstr(config_filename));

    report_versions("wapbox");

    cfg = init_wapbox(cfg);

    info(0, "------------------------------------------------------------");
    info(0, GW_NAME " wapbox version %s starting up.", GW_VERSION);
    
    sequence_counter = counter_create();
    wsp_session_init(&wtp_resp_dispatch_event,
                     &wtp_initiator_dispatch_event,
                     &wap_appl_dispatch,
                     &wap_push_ppg_dispatch_event);
    wsp_unit_init(&dispatch_datagram, &wap_appl_dispatch);
    wsp_push_client_init(&wsp_push_client_dispatch_event, 
                         &wtp_resp_dispatch_event);
    
    if (cfg)
        wtp_initiator_init(&dispatch_datagram, &wsp_session_dispatch_event,
                           timer_freq);

    wtp_resp_init(&dispatch_datagram, &wsp_session_dispatch_event,
                  &wsp_push_client_dispatch_event, timer_freq);
    wap_appl_init(cfg);

#if (HAVE_WTLS_OPENSSL)
    wtls_secmgr_init();
    wtls_init(&write_to_bearerbox);
#endif
    
    if (cfg) {
        wap_push_ota_init(&wsp_session_dispatch_event, 
                          &wsp_unit_dispatch_event);
        wap_push_ppg_init(&wap_push_ota_dispatch_event, &wap_appl_dispatch, 
                          cfg);
    }
		
    wml_init(wml_xml_strict);
    
    if (bearerbox_host == NULL)
    	bearerbox_host = octstr_create(BB_DEFAULT_HOST);
    connect_to_bearerbox(bearerbox_host, bearerbox_port, bearerbox_ssl, NULL
		    /* bearerbox_our_port */);

    if (cfg)
        wap_push_ota_bb_address_set(bearerbox_host);
	    
    program_status = running;
    if (0 > heartbeat_start(write_to_bearerbox, heartbeat_freq, 
    	    	    	    	       wap_appl_get_load)) {
        info(0, GW_NAME "Could not start heartbeat.");
    }

    while (program_status != shutting_down) {
	WAPEvent *dgram;
        int ret;

        /* block infinite for reading messages */
        ret = read_from_bearerbox(&msg, INFINITE_TIME);
        if (ret == -1) {
            error(0, "Bearerbox is gone, restarting");
            program_status = shutting_down;
            restart = 1;
            break;
        } else if (ret == 1) /* timeout */
            continue;
        else if (msg == NULL) /* just to be sure, may not happens */
            break;
	if (msg_type(msg) == admin) {
	    if (msg->admin.command == cmd_shutdown) {
		info(0, "Bearerbox told us to die");
		program_status = shutting_down;
	    } else if (msg->admin.command == cmd_restart) {
		info(0, "Bearerbox told us to restart");
		restart = 1;
		program_status = shutting_down;
	    }
	    /*
	     * XXXX here should be suspend/resume, add RSN
	     */
	} else if (msg_type(msg) == wdp_datagram) {
        switch (msg->wdp_datagram.destination_port) {
        case CONNECTIONLESS_PORT:
        case CONNECTION_ORIENTED_PORT:
	    	dgram = wap_event_create(T_DUnitdata_Ind);
	    	dgram->u.T_DUnitdata_Ind.addr_tuple = wap_addr_tuple_create(
				msg->wdp_datagram.source_address,
				msg->wdp_datagram.source_port,
				msg->wdp_datagram.destination_address,
				msg->wdp_datagram.destination_port);
	    	dgram->u.T_DUnitdata_Ind.user_data = msg->wdp_datagram.user_data;
	    	msg->wdp_datagram.user_data = NULL;

          	wap_dispatch_datagram(dgram); 
			break;
        case WTLS_CONNECTIONLESS_PORT:
        case WTLS_CONNECTION_ORIENTED_PORT:
#if (HAVE_WTLS_OPENSSL)
            dgram = wtls_unpack_wdp_datagram(msg);
            if (dgram != NULL)
                wtls_dispatch_event(dgram);
#endif
			break;
        default:
                panic(0,"Bad packet received! This shouldn't happen!");
                break;
        } 
	} else {
	    warning(0, "Received other message than wdp/admin, ignoring!");
	}
	msg_destroy(msg);
    }

    info(0, GW_NAME " wapbox terminating.");
    
    program_status = shutting_down;
    heartbeat_stop(ALL_HEARTBEATS);
    counter_destroy(sequence_counter);

    if (cfg)
        wtp_initiator_shutdown();

    wtp_resp_shutdown();
    wsp_push_client_shutdown();
    wsp_unit_shutdown();
    wsp_session_shutdown();
    wap_appl_shutdown();
    radius_acct_shutdown();

    if (cfg) {
        wap_push_ota_shutdown();
        wap_push_ppg_shutdown();
    }

    wml_shutdown();
    close_connection_to_bearerbox();
    alog_close();
    wap_map_destroy();
    wap_map_user_destroy();
    octstr_destroy(device_home);
    octstr_destroy(bearerbox_host);
    octstr_destroy(config_filename);

    /*
     * Just sleep for a while to get bearerbox chance to restart.
     * Otherwise we will fail while trying to connect to bearerbox!
     */
    if (restart) {
        gwthread_sleep(10.0);
        /* now really restart */
        restart_box(argv);
    }

    log_close_all();
    gwlib_shutdown();

    return 0;
}

