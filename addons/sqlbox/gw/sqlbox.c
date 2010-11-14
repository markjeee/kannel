/* ====================================================================
 * The Kannel Software License, Version 1.0
 *
 * Copyright (c) 2001-2004 Kannel Group
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
 * sqlbox.c - main program of the sqlbox
 */

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "gw/msg.h"
#include "gw/sms.h"
#include "gw/shared.h"
#include "gw/bb.h"
#include "sqlbox_sql.h"

/* our config */
static Cfg *cfg;
/* have we received restart cmd from bearerbox? */
static volatile sig_atomic_t restart_sqlbox = 0;
static volatile sig_atomic_t sqlbox_status;
#define SQL_DEAD 0
#define SQL_SHUTDOWN 1
#define SQL_RUNNING 2
static long sqlbox_port;
static int sqlbox_port_ssl = 0;
static long bearerbox_port;
static Octstr *bearerbox_host;
static int bearerbox_port_ssl = 0;
static Octstr *global_sender;

#ifndef HAVE_MSSQL
#ifndef HAVE_MYSQL
#ifndef HAVE_PGSQL
#ifndef HAVE_SDB
#ifndef HAVE_SQLITE
#ifndef HAVE_SQLITE3
#ifndef HAVE_ORACLE
#error You need support for at least one DB engine. Please recompile Kannel.
#endif
#endif
#endif
#endif
#endif
#endif
#endif
Octstr *sqlbox_id;

#define SLEEP_BETWEEN_SELECTS 1.0

typedef struct _boxc {
    Connection    *smsbox_connection;
    Connection    *bearerbox_connection;
    time_t    connect_time;
    Octstr        *client_ip;
    volatile sig_atomic_t alive;
    Octstr *boxc_id; /* identifies the connected smsbox instance */
} Boxc;

/*
 * Adding hooks to kannel check config
 *
 * Martin Conte.
 */

static int sqlbox_is_allowed_in_group(Octstr *group, Octstr *variable)
{
    Octstr *groupstr;

    groupstr = octstr_imm("group");

    #define OCTSTR(name) \
        if (octstr_compare(octstr_imm(#name), variable) == 0) \
        return 1;
    #define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
    #define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
    #include "sqlbox-cfg.def"

    return 0;
}

#undef OCTSTR
#undef SINGLE_GROUP
#undef MULTI_GROUP

static int sqlbox_is_single_group(Octstr *query)
{
    #define OCTSTR(name)
    #define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 1;
    #define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 0;
    #include "sqlbox-cfg.def"
    return 0;
}


/****************************************************************************
 * Character convertion.
 * 
 * The 'msgdata' is read from the DB table as URL-encoded byte stream, 
 * which we need to URL-decode to get the orginal message. We use this
 * approach to get rid of the table character dependancy of the DB systems.
 * The URL-encoded chars as a subset of ASCII which is typicall no problem
 * for any of the supported DB systems.
 */

static int charset_processing(Msg *msg) 
{
    gw_assert(msg->type == sms);

    /* URL-decode first */
    if (octstr_url_decode(msg->sms.msgdata) == -1)
        return -1;
    if (octstr_url_decode(msg->sms.udhdata) == -1)
        return -1;
        
    /* If a specific character encoding has been indicated by the
     * user, then make sure we convert to our internal representations. */
    if (octstr_len(msg->sms.charset)) {
    
        if (msg->sms.coding == DC_7BIT) {
            /* For 7 bit, convert to UTF-8 */
            if (charset_convert(msg->sms.msgdata, octstr_get_cstr(msg->sms.charset), "UTF-8") < 0)
                return -1;
        } 
        else if (msg->sms.coding == DC_UCS2) {
            /* For UCS-2, convert to UTF-16BE */
            if (charset_convert(msg->sms.msgdata, octstr_get_cstr(msg->sms.charset), "UTF-16BE") < 0) 
                return -1;
        }
    }
    
    return 0;
}


/*
 *-------------------------------------------------
 *  receiver thingies
 *-------------------------------------------------
 *
*/

/* read from either smsbox or bearerbox */

static Msg *read_from_box(Connection *conn, Boxc *boxconn)
{
    Msg *msg;

    while (boxconn->alive) {
	switch (read_from_bearerbox_real(conn, &msg, 1.0)) {
	case -1:
	    /* connection to bearerbox lost */
	    return NULL;
	    break;
	case  0:
	    /* all is well */
	    return msg;
	    break;
	case  1:
	    /* timeout */
	    break;
	}
    }

    return NULL;
}

/*
 *-------------------------------------------------
 *  sender thingies
 *-------------------------------------------------
 *
*/

/* send to either smsbox or bearerbox */

static int send_msg(Connection *conn, Boxc *boxconn, Msg *pmsg)
{
    Octstr *pack;

    pack = msg_pack(pmsg);

    if (pack == NULL)
        return -1;

    if (conn_write_withlen(conn, pack) == -1) {
        error(0, "Couldn't write Msg to box <%s>, disconnecting",
          octstr_get_cstr(boxconn->client_ip));
        octstr_destroy(pack);
        return -1;
    }
    octstr_destroy(pack);
    return 0;
}

static void smsbox_to_bearerbox(void *arg)
{
    Boxc *conn = arg;
    Msg *msg, *msg_escaped;

    /* remove messages from socket until it is closed */
    while (sqlbox_status == SQL_RUNNING && conn->alive) {

        //list_consume(suspended);    /* block here if suspended */

        msg = read_from_box(conn->smsbox_connection, conn);

        if (msg == NULL) {    /* garbage/connection lost */
            conn->alive = 0;
            break;
        }

        if (msg_type(msg) == sms) {
            debug("sqlbox", 0, "smsbox_to_bearerbox: sms received");
            msg_escaped = msg_duplicate(msg);
            gw_sql_save_msg(msg_escaped, octstr_imm("MT"));
            msg_destroy(msg_escaped);
        }

        send_msg(conn->bearerbox_connection, conn, msg);

        /* if this is an identification message from an smsbox instance */
        if (msg_type(msg) == admin && msg->admin.command == cmd_identify) {
            /*
             * any smsbox sends this command even if boxc_id is NULL,
             * but we will only consider real identified boxes
             */
            if (msg->admin.boxc_id != NULL) {

                /* and add the boxc_id into conn for boxc_status() output */
                conn->boxc_id = msg->admin.boxc_id;
                msg->admin.boxc_id = NULL;

                debug("sqlbox", 0, "smsbox_to_bearerbox: got boxc_id <%s> from <%s>",
                      octstr_get_cstr(conn->boxc_id),
                      octstr_get_cstr(conn->client_ip));
            }
        }
        msg_destroy(msg);
    }
    conn->alive = 0;
}

static Boxc *boxc_create(int fd, Octstr *ip, int ssl)
{
    Boxc *boxc;

    boxc = gw_malloc(sizeof(Boxc));
    boxc->smsbox_connection = conn_wrap_fd(fd, ssl);
    boxc->bearerbox_connection = NULL;
    boxc->client_ip = ip;
    boxc->alive = 1;
    boxc->connect_time = time(NULL);
    boxc->boxc_id = NULL;
    return boxc;
}

static void boxc_destroy(Boxc *boxc)
{
    if (boxc == NULL)
        return;

    /* do nothing to the lists, as they are only references */

    if (boxc->smsbox_connection)
        conn_destroy(boxc->smsbox_connection);
    if (boxc->bearerbox_connection)
        conn_destroy(boxc->bearerbox_connection);
    octstr_destroy(boxc->client_ip);
    octstr_destroy(boxc->boxc_id);
    gw_free(boxc);
}


static Boxc *accept_boxc(int fd, int ssl)
{
    Boxc *newconn;
    Octstr *ip;

    int newfd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    client_addr_len = sizeof(client_addr);

    newfd = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (newfd < 0)
        return NULL;

    ip = host_ip(client_addr);

    // if (is_allowed_ip(box_allow_ip, box_deny_ip, ip) == 0) {
        // info(0, "Box connection tried from denied host <%s>, disconnected",
                // octstr_get_cstr(ip));
        // octstr_destroy(ip);
        // close(newfd);
        // return NULL;
    // }
    newconn = boxc_create(newfd, ip, ssl);

    /*
     * check if the SSL handshake was successfull, otherwise
     * this is no valid box connection any more
     */
#ifdef HAVE_LIBSSL
     if (ssl && !conn_get_ssl(newconn->smsbox_connection))
        return NULL;
#endif

    if (ssl)
        info(0, "Client connected from <%s> using SSL", octstr_get_cstr(ip));
    else
        info(0, "Client connected from <%s>", octstr_get_cstr(ip));


    /* XXX TODO: do the hand-shake, baby, yeah-yeah! */

    return newconn;
}


static void bearerbox_to_smsbox(void *arg)
{
    Msg *msg, *msg_escaped;
    Boxc *conn = arg;

    while (sqlbox_status == SQL_RUNNING && conn->alive) {

        msg = read_from_box(conn->bearerbox_connection, conn);

        if (msg == NULL) {
            /* tell sqlbox to die */
            conn->alive = 0;
            debug("sqlbox", 0, "bearerbox_to_smsbox: connection to bearerbox died.");
            break;
        }
        if (msg_type(msg) == admin) {
            if (msg->admin.command == cmd_shutdown || msg->admin.command == cmd_restart) {
                /* tell sqlbox to die */
                conn->alive = 0;
                debug("sqlbox", 0, "bearerbox_to_smsbox: Bearerbox told us to shutdown.");
                break;
            }
        }

        if (msg_type(msg) == heartbeat) {
        // todo
            debug("sqlbox", 0, "bearerbox_to_smsbox: catch an heartbeat - we are alive");
            msg_destroy(msg);
            continue;
        }
        if (!conn->alive) {
            msg_destroy(msg);
            break;
        }
        if (msg_type(msg) == sms) {
            msg_escaped = msg_duplicate(msg);
            if (msg->sms.sms_type != report_mo)
                gw_sql_save_msg(msg_escaped, octstr_imm("MO"));
            else
                gw_sql_save_msg(msg_escaped, octstr_imm("DLR"));
            msg_destroy(msg_escaped);
        }
        send_msg(conn->smsbox_connection, conn, msg);
        msg_destroy(msg);
    }
    /* the client closes the connection, after that die in receiver */
    conn->alive = 0;
}

static void run_sqlbox(void *arg)
{
    int fd;
    Boxc *newconn;
    long sender;

    fd = (int)arg;
    newconn = accept_boxc(fd, sqlbox_port_ssl);
    if (newconn == NULL) {
        panic(0, "Socket accept failed");
        return;
    }
    newconn->bearerbox_connection = connect_to_bearerbox_real(bearerbox_host, bearerbox_port, bearerbox_port_ssl, NULL /* bb_our_host */);
    /* XXX add our_host if required */


    sender = gwthread_create(bearerbox_to_smsbox, newconn);
    if (sender == -1) {
        error(0, "Failed to start a new thread, disconnecting client <%s>",
              octstr_get_cstr(newconn->client_ip));
        //goto cleanup;
    }
    smsbox_to_bearerbox(newconn);
    gwthread_join(sender);
    boxc_destroy(newconn);
}

static void wait_for_connections(int fd, void (*function) (void *arg),
                                 List *waited)
{
    int ret;
    int timeout = 10; /* 10 sec. */

    gw_assert(function != NULL);

    while(sqlbox_status == SQL_RUNNING) {

        ret = gwthread_pollfd(fd, POLLIN, 1.0);
        if (sqlbox_status == SQL_SHUTDOWN) {
            if (ret == -1 || !timeout)
                    break;
                else
                    timeout--;
        }

        if (ret > 0) {
            gwthread_create(function, (void *)fd);
            gwthread_sleep(1.0);
        } else if (ret < 0) {
            if(errno==EINTR) continue;
            if(errno==EAGAIN) continue;
            error(errno, "wait_for_connections failed");
        }
    }
}

/*
 * Identify ourself to bearerbox for smsbox-specific routing inside bearerbox.
 * Do this even while no smsbox-id is given to unlock the sender thread in
 * bearerbox.
 */
static void identify_to_bearerbox(Boxc *conn)
{
    Msg *msg;

    msg = msg_create(admin);
    msg->admin.command = cmd_identify;
    msg->admin.boxc_id = octstr_duplicate(conn->boxc_id);
    send_msg(conn->bearerbox_connection, conn, msg);
    msg_destroy(msg);
}

static void bearerbox_to_sql(void *arg)
{
    Boxc *conn = (Boxc *)arg;
    Msg *msg, *mack;

    while (sqlbox_status == SQL_RUNNING && conn->alive) {
        msg = read_from_box(conn->bearerbox_connection, conn);

        if (msg == NULL) {    /* garbage/connection lost */
                    /* tell sqlbox to die */
            conn->alive = 0;
            sqlbox_status = SQL_SHUTDOWN;
            debug("sqlbox", 0, "bearerbox_to_sql: connection to bearerbox died.");
            break;
        }
            if (msg_type(msg) == heartbeat) {
                // todo
                    debug("sqlbox", 0, "bearerbox_to_sql: catch an heartbeat - we are alive");
                    msg_destroy(msg);
                    continue;
            }
        /* if this is an identification message from an smsbox instance */
        if (msg_type(msg) == admin && msg->admin.command == cmd_shutdown) {
                    /* tell sqlbox to die */
            conn->alive = 0;
            sqlbox_status = SQL_SHUTDOWN;
            debug("sqlbox", 0, "bearerbox_to_sql: Bearerbox told us to shutdown.");
            break;
        }
        if (msg_type(msg) == sms) {
            if (msg->sms.sms_type != report_mo)
                gw_sql_save_msg(msg, octstr_imm("MO"));
            else
                gw_sql_save_msg(msg, octstr_imm("DLR"));

	    /* create ack message */
	    mack = msg_create(ack);
	    mack->ack.nack = ack_success;
	    mack->ack.time = msg->sms.time;
	    uuid_copy(mack->ack.id, msg->sms.id);
	    send_msg(conn->bearerbox_connection, conn, mack);
	    msg_destroy(mack);

        }

        msg_destroy(msg);
    }
}

static void sql_to_bearerbox(void *arg)
{
    Boxc *boxc;
    Msg *msg;

    boxc = gw_malloc(sizeof(Boxc));
    boxc->bearerbox_connection = connect_to_bearerbox_real(bearerbox_host, bearerbox_port, bearerbox_port_ssl, NULL /* bb_our_host */);
    boxc->smsbox_connection = NULL;
    boxc->client_ip = NULL;
    boxc->alive = 1;
    boxc->connect_time = time(NULL);
    boxc->boxc_id = octstr_duplicate(sqlbox_id);
    if (boxc->bearerbox_connection == NULL) {
        boxc_destroy(boxc);
        return;
    }

    gwthread_create(bearerbox_to_sql, boxc);

    identify_to_bearerbox(boxc);

    while (sqlbox_status == SQL_RUNNING && boxc->alive) {
        if ((msg = gw_sql_fetch_msg()) != NULL) {
            if (charset_processing(msg) == -1) {
                error(0, "Could not charset process message, dropping it!");
                msg_destroy(msg);
                continue;
            }
            if (global_sender != NULL && (msg->sms.sender == NULL || octstr_len(msg->sms.sender) == 0)) {
                msg->sms.sender = octstr_duplicate(global_sender);
            }
            send_msg(boxc->bearerbox_connection, boxc, msg);
            gw_sql_save_msg(msg, octstr_imm("MT"));
        }
        else {
            gwthread_sleep(SLEEP_BETWEEN_SELECTS);
        }
    }

    boxc_destroy(boxc);
}

static void sqlboxc_run(void *arg)
{
    int fd;
    int port;

    /* we will use one thread for SQL sms injections */
    gwthread_create(sql_to_bearerbox, NULL);

    port = (int)arg;

    fd = make_server_socket(port, NULL);
        /* XXX add interface_name if required */

    if (fd < 0) {
        panic(0, "Could not open sqlbox port %d", port);
    }

    /*
     * infinitely wait for new connections;
     * to shut down the system, SIGTERM is send and then
     * select drops with error, so we can check the status
     */

    wait_for_connections(fd, run_sqlbox, NULL);

    /* close listen socket */
    close(fd);
}



/***********************************************************************
 * Main program. Configuration, signal handling, etc.
 */

static void signal_handler(int signum) {
    /* On some implementations (i.e. linuxthreads), signals are delivered
     * to all threads.  We only want to handle each signal once for the
     * entire box, and we let the gwthread wrapper take care of choosing
     * one.
     */
    if (!gwthread_shouldhandlesignal(signum))
        return;

    switch (signum) {
        case SIGINT:
            if (sqlbox_status == SQL_RUNNING) {
                error(0, "SIGINT received, aborting program...");
                sqlbox_status = SQL_SHUTDOWN;
            }
            break;

        case SIGHUP:
            warning(0, "SIGHUP received, catching and re-opening logs");
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


static void setup_signal_handlers(void) {
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGPIPE, &act, NULL);
}



static void init_sqlbox(Cfg *cfg)
{
    CfgGroup *grp;
    Octstr *logfile;
    long lvl;

    /* some default values */
    sqlbox_port_ssl = 0;
    bearerbox_port = BB_DEFAULT_SMSBOX_PORT;
    bearerbox_port_ssl = 0;
    logfile = NULL;
    lvl = 0;

    /*
     * first we take the port number in bearerbox and other values from the
     * core group in configuration file
    */

    grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"));
    if (cfg_get_integer(&bearerbox_port, grp, octstr_imm("bearerbox-port")) == -1)
        panic(0, "Missing or bad 'bearerbox-port' in sqlbox group");
#ifdef HAVE_LIBSSL
    cfg_get_bool(&bearerbox_port_ssl, grp, octstr_imm("smsbox-port-ssl"));
    conn_config_ssl(grp);
#endif

    grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"));
    if (grp == NULL)
        panic(0, "No 'sqlbox' group in configuration");

    bearerbox_host = cfg_get( grp, octstr_imm("bearerbox-host"));
    if (bearerbox_host == NULL)
        bearerbox_host = octstr_create(BB_DEFAULT_HOST);

    sqlbox_id = cfg_get(grp, octstr_imm("smsbox-id"));
    global_sender = cfg_get(grp, octstr_imm("global-sender"));

    if (cfg_get_integer(&sqlbox_port, grp, octstr_imm("smsbox-port")) == -1)
        sqlbox_port = 13005;
    /* setup logfile stuff */
    logfile = cfg_get(grp, octstr_imm("log-file"));

    cfg_get_integer(&lvl, grp, octstr_imm("log-level"));

    if (logfile != NULL) {
        info(0, "Starting to log to file %s level %ld",
            octstr_get_cstr(logfile), lvl);
        log_open(octstr_get_cstr(logfile), lvl, GW_NON_EXCL);
        octstr_destroy(logfile);
    }

    sql_type = sqlbox_init_sql(cfg);
    if (sql_type == NULL) {
        panic(0, "No proper SQL server defined.");
    }

    gw_sql_enter(cfg);

    sqlbox_status = SQL_RUNNING;
}

static int check_args(int i, int argc, char **argv) {
    if (strcmp(argv[i], "-H")==0 || strcmp(argv[i], "--tryhttp")==0) {
    //only_try_http = 1;
    } else {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    int cf_index;
    Octstr *filename;

    gwlib_init();

    cf_index = get_and_set_debugs(argc, argv, check_args);
    setup_signal_handlers();

    if (argv[cf_index] == NULL) {
        filename = octstr_create("sqlbox.conf");
    } else {
        filename = octstr_create(argv[cf_index]);
    }

    cfg = cfg_create(filename);

    /* Adding cfg-checks to core */

    cfg_add_hooks(sqlbox_is_allowed_in_group, sqlbox_is_single_group);

    if (cfg_read(cfg) == -1)
        panic(0, "Couldn't read configuration from `%s'.", octstr_get_cstr(filename));

    octstr_destroy(filename);

    report_versions("sqlbox");

    init_sqlbox(cfg);

    sqlboxc_run((void *)sqlbox_port);

    cfg_destroy(cfg);
    if (restart_sqlbox) {
        gwthread_sleep(1.0);
    }

    gw_sql_leave();
    gwlib_shutdown();

    if (restart_sqlbox)
        execvp(argv[0], argv);
    return 0;
}
