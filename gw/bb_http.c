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
 * bb_http.c : bearerbox http adminstration commands
 *
 * NOTE: this is a special bearerbox module - it does call
 *   functions from core module! (other modules are fully
 *    encapsulated, and only called outside)
 *
 * Kalle Marjola <rpr@wapit.com> 2000 for project Kannel
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "bearerbox.h"

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;

/* our own thingies */

static volatile sig_atomic_t httpadmin_running;

static long	ha_port;
static Octstr *ha_interface;
static Octstr *ha_password;
static Octstr *ha_status_pw;
static Octstr *ha_allow_ip;
static Octstr *ha_deny_ip;


/*---------------------------------------------------------
 * static functions
 */

/*
 * check if the password matches. Return NULL if
 * it does (or is not required)
 */
static Octstr *httpd_check_authorization(List *cgivars, int status)
{
    Octstr *password;
    static double sleep = 0.01;

    password = http_cgi_variable(cgivars, "password");

    if (status) {
	if (ha_status_pw == NULL)
	    return NULL;

	if (password == NULL)
	    goto denied;

	if (octstr_compare(password, ha_password)!=0
	    && octstr_compare(password, ha_status_pw)!=0)
	    goto denied;
    }
    else {
	if (password == NULL || octstr_compare(password, ha_password)!=0)
	    goto denied;
    }
    sleep = 0.0;
    return NULL;	/* allowed */
denied:
    gwthread_sleep(sleep);
    sleep += 1.0;		/* little protection against brute force
				 * password cracking */
    return octstr_create("Denied");
}

/*
 * check if we still have time to do things
 */
static Octstr *httpd_check_status(void)
{
    if (bb_status == BB_SHUTDOWN || bb_status == BB_DEAD)
	return octstr_create("Avalanche has already started, too late to "
	    	    	     "save the sheeps");
    return NULL;
}
    
static Octstr *httpd_status(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 1))!= NULL) return reply;
    return bb_print_status(status_type);
}

static Octstr *httpd_store_status(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 1))!= NULL) return reply;
    return store_status(status_type);
}

static Octstr *httpd_loglevel(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *level;
    int new_loglevel;
    
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;
 
    /* check if new loglevel is given */
    level = http_cgi_variable(cgivars, "level");
    if (level) {
        new_loglevel = atoi(octstr_get_cstr(level));
        log_set_log_level(new_loglevel);
        return octstr_format("log-level set to %d", new_loglevel);
    }
    else {
        return octstr_create("New level not given");
    }
}

static Octstr *httpd_shutdown(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if (bb_status == BB_SHUTDOWN)
	bb_status = BB_DEAD;
    else {
	bb_shutdown();
        gwthread_wakeup(MAIN_THREAD_ID);
    }
    return octstr_create("Bringing system down");
}

static Octstr *httpd_isolate(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    if (bb_isolate() == -1)
	return octstr_create("Already isolated");
    else
	return octstr_create(GW_NAME " isolated from message providers");
}

static Octstr *httpd_suspend(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    if (bb_suspend() == -1)
	return octstr_create("Already suspended");
    else
	return octstr_create(GW_NAME " suspended");
}

static Octstr *httpd_resume(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;
 
    if (bb_resume() == -1)
	return octstr_create("Already running");
    else
	return octstr_create("Running resumed");
}

static Octstr *httpd_restart(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;
 
    if (bb_status == BB_SHUTDOWN) {
        bb_status = BB_DEAD;
        gwthread_wakeup_all();
        return octstr_create("Trying harder to restart");
    }
    bb_restart();
    return octstr_create("Restarting.....");
}

static Octstr *httpd_flush_dlr(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    if (bb_flush_dlr() == -1)
	return octstr_create("Suspend " GW_NAME " before trying to flush DLR queue");
    else
	return octstr_create("DLR queue flushed");
}

static Octstr *httpd_stop_smsc(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *smsc;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the smsc id is given */
    smsc = http_cgi_variable(cgivars, "smsc");
    if (smsc) {
        if (bb_stop_smsc(smsc) == -1)
            return octstr_format("Could not shut down smsc-id `%s'", octstr_get_cstr(smsc));
        else
            return octstr_format("SMSC `%s' shut down", octstr_get_cstr(smsc));
    } else
        return octstr_create("SMSC id not given");
}

static Octstr *httpd_remove_smsc(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *smsc;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the smsc id is given */
    smsc = http_cgi_variable(cgivars, "smsc");
    if (smsc) {
        if (bb_remove_smsc(smsc) == -1)
            return octstr_format("Could not remove smsc-id `%s'", octstr_get_cstr(smsc));
        else
            return octstr_format("SMSC `%s' removed", octstr_get_cstr(smsc));
    } else
        return octstr_create("SMSC id not given");
}

static Octstr *httpd_add_smsc(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *smsc;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the smsc id is given */
    smsc = http_cgi_variable(cgivars, "smsc");
    if (smsc) {
        if (bb_add_smsc(smsc) == -1)
            return octstr_format("Could not add smsc-id `%s'", octstr_get_cstr(smsc));
        else
            return octstr_format("SMSC `%s' added", octstr_get_cstr(smsc));
    } else
        return octstr_create("SMSC id not given");
}

static Octstr *httpd_restart_smsc(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *smsc;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the smsc id is given */
    smsc = http_cgi_variable(cgivars, "smsc");
    if (smsc) {
        if (bb_restart_smsc(smsc) == -1)
            return octstr_format("Could not re-start smsc-id `%s'", octstr_get_cstr(smsc));
        else
            return octstr_format("SMSC `%s' re-started", octstr_get_cstr(smsc));
    } else
        return octstr_create("SMSC id not given");
}

static Octstr *httpd_reload_lists(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;
 
    if (bb_reload_lists() == -1)
        return octstr_create("Could not re-load lists");
    else
        return octstr_create("Black/white lists re-loaded");
}

static Octstr *httpd_remove_message(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *message_id;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the smsc id is given */
    message_id = http_cgi_variable(cgivars, "id");
    if (message_id) {
        if (octstr_len(message_id) != UUID_STR_LEN)
            return octstr_format("Message id should be %d characters long", UUID_STR_LEN);
        if (bb_remove_message(message_id) == -1)
            return octstr_format("Could not remove message id `%s'", octstr_get_cstr(message_id));
        else
            return octstr_format("Message id `%s' removed", octstr_get_cstr(message_id));
    } else
        return octstr_create("Message id not given");
}

/* Known httpd commands and their functions */
static struct httpd_command {
    const char *command;
    Octstr * (*function)(List *cgivars, int status_type);
} httpd_commands[] = {
    { "status", httpd_status },
    { "store-status", httpd_store_status },
    { "log-level", httpd_loglevel },
    { "shutdown", httpd_shutdown },
    { "suspend", httpd_suspend },
    { "isolate", httpd_isolate },
    { "resume", httpd_resume },
    { "restart", httpd_restart },
    { "flush-dlr", httpd_flush_dlr },
    { "stop-smsc", httpd_stop_smsc },
    { "start-smsc", httpd_restart_smsc },
    { "add-smsc", httpd_add_smsc },
    { "remove-smsc", httpd_remove_smsc },
    { "reload-lists", httpd_reload_lists },
    { "remove-message", httpd_remove_message },
    { NULL , NULL } /* terminate list */
};

static void httpd_serve(HTTPClient *client, Octstr *ourl, List *headers,
    	    	    	Octstr *body, List *cgivars)
{
    Octstr *reply, *final_reply, *url;
    char *content_type;
    char *header, *footer;
    int status_type;
    int i;
    long pos;

    reply = final_reply = NULL; /* for compiler please */
    url = octstr_duplicate(ourl);

    /* Set default reply format according to client
     * Accept: header */
    if (http_type_accepted(headers, "text/vnd.wap.wml")) {
	status_type = BBSTATUS_WML;
	content_type = "text/vnd.wap.wml";
    }
    else if (http_type_accepted(headers, "text/html")) {
	status_type = BBSTATUS_HTML;
	content_type = "text/html";
    }
    else if (http_type_accepted(headers, "text/xml")) {
	status_type = BBSTATUS_XML;
	content_type = "text/xml";
    } else {
	status_type = BBSTATUS_TEXT;
	content_type = "text/plain";
    }

    /* kill '/cgi-bin' prefix */
    pos = octstr_search(url, octstr_imm("/cgi-bin/"), 0);
    if (pos != -1)
        octstr_delete(url, pos, 9);
    else if (octstr_get_char(url, 0) == '/')
        octstr_delete(url, 0, 1);

    /* look for type and kill it */
    pos = octstr_search_char(url, '.', 0);
    if (pos != -1) {
        Octstr *tmp = octstr_copy(url, pos+1, octstr_len(url) - pos - 1);
        octstr_delete(url, pos, octstr_len(url) - pos);

        if (octstr_str_compare(tmp, "txt") == 0)
            status_type = BBSTATUS_TEXT;
        else if (octstr_str_compare(tmp, "html") == 0)
            status_type = BBSTATUS_HTML;
        else if (octstr_str_compare(tmp, "xml") == 0)
            status_type = BBSTATUS_XML;
        else if (octstr_str_compare(tmp, "wml") == 0)
            status_type = BBSTATUS_WML;

        octstr_destroy(tmp);
    }

    for (i=0; httpd_commands[i].command != NULL; i++) {
        if (octstr_str_compare(url, httpd_commands[i].command) == 0) {
            reply = httpd_commands[i].function(cgivars, status_type);
            break;
        }
    }

    /* check if command found */
    if (httpd_commands[i].command == NULL) {
        char *lb = bb_status_linebreak(status_type);
	reply = octstr_format("Unknown command `%S'.%sPossible commands are:%s",
            ourl, lb, lb);
        for (i=0; httpd_commands[i].command != NULL; i++)
            octstr_format_append(reply, "%s%s", httpd_commands[i].command, lb);
    }

    gw_assert(reply != NULL);

    if (status_type == BBSTATUS_HTML) {
	header = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\n"
 	    "<html>\n<title>" GW_NAME "</title>\n<body>\n<p>";
	footer = "</p>\n</body></html>\n";
	content_type = "text/html";
    } else if (status_type == BBSTATUS_WML) {
	header = "<?xml version=\"1.0\"?>\n"
            "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" "
            "\"http://www.wapforum.org/DTD/wml_1.1.xml\">\n"
            "\n<wml>\n <card>\n  <p>";
	footer = "  </p>\n </card>\n</wml>\n";
	content_type = "text/vnd.wap.wml";
    } else if (status_type == BBSTATUS_XML) {
	header = "<?xml version=\"1.0\"?>\n"
            "<gateway>\n";
        footer = "</gateway>\n";
    } else {
	header = "";
	footer = "";
	content_type = "text/plain";
    }
    final_reply = octstr_create(header);
    octstr_append(final_reply, reply);
    octstr_append_cstr(final_reply, footer);
    
    /* debug("bb.http", 0, "Result: '%s'", octstr_get_cstr(final_reply));
     */
    http_destroy_headers(headers);
    headers = gwlist_create();
    http_header_add(headers, "Content-Type", content_type);

    http_send_reply(client, HTTP_OK, headers, final_reply);

    octstr_destroy(url);
    octstr_destroy(ourl);
    octstr_destroy(body);
    octstr_destroy(reply);
    octstr_destroy(final_reply);
    http_destroy_headers(headers);
    http_destroy_cgiargs(cgivars);
}

static void httpadmin_run(void *arg)
{
    HTTPClient *client;
    Octstr *ip, *url, *body;
    List *headers, *cgivars;

    while(bb_status != BB_DEAD) {
	if (bb_status == BB_SHUTDOWN)
	    bb_shutdown();
    	client = http_accept_request(ha_port, &ip, &url, &headers, &body, 
	    	    	    	     &cgivars);
	if (client == NULL)
	    break;
	if (is_allowed_ip(ha_allow_ip, ha_deny_ip, ip) == 0) {
	    info(0, "HTTP admin tried from denied host <%s>, disconnected",
		 octstr_get_cstr(ip));
	    http_close_client(client);
	    continue;
	}
        httpd_serve(client, url, headers, body, cgivars);
	octstr_destroy(ip);
    }

    httpadmin_running = 0;
}


/*-------------------------------------------------------------
 * public functions
 *
 */

int httpadmin_start(Cfg *cfg)
{
    CfgGroup *grp;
    int ssl = 0; 
#ifdef HAVE_LIBSSL
    Octstr *ssl_server_cert_file;
    Octstr *ssl_server_key_file;
#endif /* HAVE_LIBSSL */
    
    if (httpadmin_running) return -1;


    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    if (cfg_get_integer(&ha_port, grp, octstr_imm("admin-port")) == -1)
	panic(0, "Missing admin-port variable, cannot start HTTP admin");

    ha_interface = cfg_get(grp, octstr_imm("admin-interface"));
    ha_password = cfg_get(grp, octstr_imm("admin-password"));
    if (ha_password == NULL)
	panic(0, "You MUST set HTTP admin-password");
    
    ha_status_pw = cfg_get(grp, octstr_imm("status-password"));

    ha_allow_ip = cfg_get(grp, octstr_imm("admin-allow-ip"));
    ha_deny_ip = cfg_get(grp, octstr_imm("admin-deny-ip"));

#ifdef HAVE_LIBSSL
    cfg_get_bool(&ssl, grp, octstr_imm("admin-port-ssl"));
    
    /*
     * check if SSL is desired for HTTP servers and then
     * load SSL client and SSL server public certificates 
     * and private keys
     */    
    ssl_server_cert_file = cfg_get(grp, octstr_imm("ssl-server-cert-file"));
    ssl_server_key_file = cfg_get(grp, octstr_imm("ssl-server-key-file"));
    if (ssl_server_cert_file != NULL && ssl_server_key_file != NULL) {
        /* we are fine here, the following call is now in conn_config_ssl(),
         * so there is no reason to do this twice.

        use_global_server_certkey_file(ssl_server_cert_file, 
            ssl_server_key_file);
        */
    } else if (ssl) {
	   panic(0, "You MUST specify cert and key files within core group for SSL-enabled HTTP servers!");
    }

    octstr_destroy(ssl_server_cert_file);
    octstr_destroy(ssl_server_key_file);
#endif /* HAVE_LIBSSL */

    http_open_port_if(ha_port, ssl, ha_interface);

    if (gwthread_create(httpadmin_run, NULL) == -1)
	panic(0, "Failed to start a new thread for HTTP admin");

    httpadmin_running = 1;
    return 0;
}


void httpadmin_stop(void)
{
    http_close_all_ports();
    gwthread_join_every(httpadmin_run);
    octstr_destroy(ha_interface);    
    octstr_destroy(ha_password);
    octstr_destroy(ha_status_pw);
    octstr_destroy(ha_allow_ip);
    octstr_destroy(ha_deny_ip);
    ha_password = NULL;
    ha_status_pw = NULL;
    ha_allow_ip = NULL;
    ha_deny_ip = NULL;
}
