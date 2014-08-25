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
 * test_dbpool.c - test DBPool objects
 *
 * Stipe Tolj <stolj@wapme.de>
 * Alexander Malysh <amalysh@kannel.org>
 */
             
#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"

#ifdef HAVE_DBPOOL

#define MAX_THREADS 1024

static void help(void)
{
    info(0, "Usage: test_dbpool [options] ...");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-h hostname");
    info(0, "    hostname to connect to");
    info(0, "-u username");
    info(0, "    username to use for the login credentials");
    info(0, "-p password");
    info(0, "    password to use for the login credentials");
    info(0, "-d database");
    info(0, "    database to connect to (for oracle tnsname) or file to open (for sqlite)");
    info(0, "-s number");
    info(0, "    size of the database connection pool (default: 5)");
    info(0, "-q number");
    info(0, "    run a set of queries on the database connection pool (default: 100)");
    info(0, "-t number");
    info(0, "    how many query client threads should be used (default: 1)");
    info(0, "-S string");
    info(0, "    the SQL string that is performed while the queries (default: SHOW STATUS)");
    info(0, "-T type");
    info(0, "    the type of database to use [mysql|oracle|sqlite]");
}

/* global variables */
static unsigned long queries = 100;
static Octstr *sql;
static  unsigned int pool_size = 5;

static enum db_type database_type = DBPOOL_MYSQL;

static void (*client_thread)(void*) = NULL;

#ifdef HAVE_MYSQL

static void mysql_client_thread(void *arg)
{
    unsigned long i, succeeded, failed;
    DBPool *pool = arg;
    List *result;
    DBPoolConn *pconn;

    succeeded = failed = 0;

    info(0,"Client thread started with %ld queries to perform on pool", queries);

    for (i = 1; i <= queries; i++) {
        pconn = dbpool_conn_consume(pool);

        if (pconn == NULL)
            continue;
#if 1 /* selects */
        if (dbpool_conn_select(pconn, sql, NULL, &result) == 0) {
            long i,j;
            for (i=0; i < gwlist_len(result); i++) {
                List *row = gwlist_get(result, i);
                for (j=0; j < gwlist_len(row); j++)
                    debug("", 0, "col = %ld   value = '%s'", j, octstr_get_cstr(gwlist_get(row,j)));
                gwlist_destroy(row, octstr_destroy_item);
            }
            succeeded++;
        } else {
            failed++;
        }
        gwlist_destroy(result, NULL);
        dbpool_conn_produce(pconn);
#else /* only updates */
        debug("", 0, "rows processed = %d ", dbpool_conn_update(pconn, sql, NULL));
        dbpool_conn_produce(pconn);
#endif
    }
    info(0, "This thread: %ld succeeded, %ld failed.", succeeded, failed);
}

static DBConf *mysql_create_conf(Octstr *user, Octstr *pass, Octstr *db, Octstr *host)
{
    DBConf *conf;
    conf = gw_malloc(sizeof(DBConf));
    conf->mysql = gw_malloc(sizeof(MySQLConf));

    conf->mysql->username = octstr_duplicate(user);
    conf->mysql->password = octstr_duplicate(pass);
    conf->mysql->database = octstr_duplicate(db);
    conf->mysql->host = octstr_duplicate(host);
    conf->mysql->port = 3306;

    return conf;
}
#endif

#ifdef HAVE_ORACLE
#include <oci.h>

static DBConf *oracle_create_conf(Octstr *user,Octstr *pass, Octstr *db)
{
    DBConf *conf;
    conf = gw_malloc(sizeof(DBConf));
    conf->oracle = gw_malloc(sizeof(OracleConf));

    conf->oracle->username = octstr_duplicate(user);
    conf->oracle->password = octstr_duplicate(pass);
    conf->oracle->tnsname = octstr_duplicate(db);

    return conf;
}

struct ora_conn {
    /* environment handle */
    OCIEnv *envp;
    /* context handle */
    OCISvcCtx *svchp;
    /* error handle */
    OCIError *errhp;
};

static void oracle_client_thread(void *arg)
{
    DBPool *pool = arg;
    DBPoolConn *pconn = NULL;
    int i;
    List *result;

    for (i = 1; i <= queries; i++) {
        pconn = dbpool_conn_consume(pool);

        if (pconn == NULL)
            continue;
#if 1 /* selects */
        if (dbpool_conn_select(pconn, sql, NULL, &result) == 0) {
            long i,j;
            for (i=0; i < gwlist_len(result); i++) {
                List *row = gwlist_get(result, i);
                for (j=0; j < gwlist_len(row); j++)
                    debug("", 0, "col = %ld   value = '%s'", j, octstr_get_cstr(gwlist_get(row,j)));
                gwlist_destroy(row, octstr_destroy_item);
            }
        }
        gwlist_destroy(result, NULL);
        dbpool_conn_produce(pconn);
#else /* only updates */
        debug("", 0, "rows processed = %d ", dbpool_conn_update(pconn, sql, NULL));
        dbpool_conn_produce(pconn);
#endif
    }
}
#endif

#ifdef HAVE_SQLITE
#include <sqlite.h>

static DBConf *sqlite_create_conf(Octstr *db)
{
    DBConf *conf;
    conf = gw_malloc(sizeof(DBConf));
    conf->sqlite = gw_malloc(sizeof(SQLiteConf));

    conf->sqlite->file = octstr_duplicate(db);

    return conf;
}

static int callback(void *not_used, int argc, char **argv, char **col_name)
{
    int i;
    
    for (i = 0; i < argc; i++) {
        debug("",0,"SQLite: result: %s = %s", col_name[i], argv[i]);
    }

    return 0;
}

static void sqlite_client_thread(void *arg)
{
    unsigned long i, succeeded, failed;
    DBPool *pool = arg;
    char *errmsg = 0;

    succeeded = failed = 0;

    info(0,"Client thread started with %ld queries to perform on pool", queries);

    /* perform random queries on the pool */
    for (i = 1; i <= queries; i++) {
        DBPoolConn *pconn;
        int state;

        /* provide us with a connection from the pool */
        pconn = dbpool_conn_consume(pool);
        debug("",0,"Query %ld/%ld: sqlite conn obj at %p",
              i, queries, (void*) pconn->conn);

        state = sqlite_exec(pconn->conn, octstr_get_cstr(sql), callback, 0, &errmsg);
        if (state != SQLITE_OK) {
            error(0, "SQLite: %s", errmsg);
            failed++;
        } else {
            succeeded++;
        }

        /* return the connection to the pool */
        dbpool_conn_produce(pconn);
    }
    info(0, "This thread: %ld succeeded, %ld failed.", succeeded, failed);
}
#endif

#ifdef HAVE_SQLITE3
#include <sqlite3.h>

static DBConf *sqlite3_create_conf(Octstr *db)
{
    DBConf *conf;
    conf = gw_malloc(sizeof(DBConf));
    conf->sqlite3 = gw_malloc(sizeof(SQLite3Conf));

    conf->sqlite3->file = octstr_duplicate(db);

    return conf;
}

static int callback3(void *not_used, int argc, char **argv, char **col_name)
{
    int i;
    
    for (i = 0; i < argc; i++) {
        debug("",0,"SQLite3: result: %s = %s", col_name[i], argv[i]);
    }

    return 0;
}

static void sqlite3_client_thread(void *arg)
{
    unsigned long i, succeeded, failed;
    DBPool *pool = arg;
    char *errmsg = 0;

    succeeded = failed = 0;

    info(0,"Client thread started with %ld queries to perform on pool", queries);

    /* perform random queries on the pool */
    for (i = 1; i <= queries; i++) {
        DBPoolConn *pconn;
        int state;

        /* provide us with a connection from the pool */
        pconn = dbpool_conn_consume(pool);
        debug("",0,"Query %ld/%ld: sqlite conn obj at %p",
              i, queries, (void*) pconn->conn);

        state = sqlite3_exec(pconn->conn, octstr_get_cstr(sql), callback3, 0, &errmsg);
        if (state != SQLITE_OK) {
            error(0, "SQLite3: %s", errmsg);
            failed++;
        } else {
            succeeded++;
        }

        /* return the connection to the pool */
        dbpool_conn_produce(pconn);
    }
    info(0, "This thread: %ld succeeded, %ld failed.", succeeded, failed);
}
#endif

static void inc_dec_thread(void *arg)
{
    DBPool *pool = arg;
    int ret;

    /* decrease */
    info(0,"Decreasing pool by half of size, which is %d connections", abs(pool_size/2));
    ret = dbpool_decrease(pool, abs(pool_size/2));
    debug("",0,"Decreased by %d connections", ret);
    debug("",0,"Connections within pool: %ld", dbpool_conn_count(pool));

    /* increase */
    info(0,"Increasing pool again by %d connections", pool_size);
    ret = dbpool_increase(pool, pool_size);
    debug("",0,"Increased by %d connections", ret);
    debug("",0,"Connections within pool: %ld", dbpool_conn_count(pool));
}

int main(int argc, char **argv)
{
    DBPool *pool;
    DBConf *conf = NULL; /* for compiler please */
    unsigned int num_threads = 1;
    unsigned long i;
    int opt;
    time_t start = 0, end = 0;
    double run_time;
    Octstr *user, *pass, *db, *host, *db_type;
    int j, bail_out;

    user = pass = db = host = db_type = NULL;

    gwlib_init();

    sql = octstr_imm("SHOW STATUS");

    while ((opt = getopt(argc, argv, "v:h:u:p:d:s:q:t:S:T:")) != EOF) {
        switch (opt) {
            case 'v':
                log_set_output_level(atoi(optarg));
                break;

            case 'h':
                host = octstr_create(optarg);
                break;

            case 'u':
                user = octstr_create(optarg);
                break;

            case 'p':
                pass = octstr_create(optarg);
                break;

            case 'd':
                db = octstr_create(optarg);
                break;

            case 'S':
                octstr_destroy(sql);
                sql = octstr_create(optarg);
                break;

            case 's':
                pool_size = atoi(optarg);
                break;

            case 'q':
                queries = atoi(optarg);
                break;

            case 't':
                num_threads = atoi(optarg);
                break;

            case 'T':
                db_type = octstr_create(optarg);
                break;

            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping.");
        }
    }

    if (!optind) {
        help();
        exit(0);
    }

    if (!db_type) {
        info(0, "No database type given assuming MySQL.");
    }
    else if (octstr_case_compare(db_type, octstr_imm("mysql")) == 0) {
        info(0, "Do tests for mysql database.");
        database_type = DBPOOL_MYSQL;
    }
    else if (octstr_case_compare(db_type, octstr_imm("oracle")) == 0) {
        info(0, "Do tests for oracle database.");
        database_type = DBPOOL_ORACLE;
    }
    else if (octstr_case_compare(db_type, octstr_imm("sqlite")) == 0) {
        info(0, "Do tests for sqlite database.");
        database_type = DBPOOL_SQLITE;
    }
    else if (octstr_case_compare(db_type, octstr_imm("sqlite3")) == 0) {
        info(0, "Do tests for sqlite3 database.");
        database_type = DBPOOL_SQLITE3;
    }
    else {
        panic(0, "Unknown database type '%s'", octstr_get_cstr(db_type));
    }

    /* check if we have the database connection details */
    switch (database_type) {
        case DBPOOL_ORACLE:
            bail_out = (!user || !pass || !db) ? 1 : 0;
            break;
        case DBPOOL_SQLITE:
        case DBPOOL_SQLITE3:
            bail_out = (!db) ? 1 : 0;
            break;
        default:
            bail_out = (!host || !user || !pass || !db) ? 1 : 0;
            break;
    }
    if (bail_out) {
        help();
        panic(0, "Database connection details are not fully provided!");
    }

    for (j = 0; j < 1; j++) {

    /* create DBConf */
    switch (database_type) {
#ifdef HAVE_MYSQL
        case DBPOOL_MYSQL:
            conf = mysql_create_conf(user,pass,db,host);
            client_thread = mysql_client_thread;
            break;
#endif
#ifdef HAVE_ORACLE
        case DBPOOL_ORACLE:
            conf = oracle_create_conf(user, pass, db);
            client_thread = oracle_client_thread;
            break;
#endif
#ifdef HAVE_SQLITE
        case DBPOOL_SQLITE:
            conf = sqlite_create_conf(db);
            client_thread = sqlite_client_thread;
            break;
#endif
#ifdef HAVE_SQLITE3
        case DBPOOL_SQLITE3:
            conf = sqlite3_create_conf(db);
            client_thread = sqlite3_client_thread;
            break;
#endif
        default:
            panic(0, "ooops ....");
    };

    /* create */
    info(0,"Creating database pool to `%s' with %d connections type '%s'.",
          (host ? octstr_get_cstr(host) : octstr_get_cstr(db)), pool_size, octstr_get_cstr(db_type));
    pool = dbpool_create(database_type, conf, pool_size);
    debug("",0,"Connections within pool: %ld", dbpool_conn_count(pool));
    if (dbpool_conn_count(pool) == 0) {
        panic(0, "Unable to start without DBConns...");
        exit(1);
    }

    for (i = 0; i < num_threads; ++i) {
        if (gwthread_create(inc_dec_thread, pool) == -1)
            panic(0, "Could not create thread %ld", i);
    }
    gwthread_join_all();

    info(0, "Connections within pool: %ld", dbpool_conn_count(pool));
    info(0, "Checked pool, %d connections still active and ok", dbpool_check(pool));

    /* queries */
    info(0,"SQL query is `%s'", octstr_get_cstr(sql));
    time(&start);
    for (i = 0; i < num_threads; ++i) {
#if 0
        if (gwthread_create(inc_dec_thread, pool) == -1)
            panic(0, "Couldnot create thread %ld", i);
#endif
        if (gwthread_create(client_thread, pool) == -1)
            panic(0, "Couldnot create thread %ld", i);
    }

    gwthread_join_all();
    time(&end);

    run_time = difftime(end, start);
    info(0, "%ld requests in %.2f seconds, %.2f requests/s.",
         (queries * num_threads), run_time, (float) (queries * num_threads) / (run_time==0?1:run_time));

    /* check all active connections */
    debug("",0,"Connections within pool: %ld", dbpool_conn_count(pool));
    info(0,"Checked pool, %d connections still active and ok", dbpool_check(pool));

    info(0,"Destroying pool");
    dbpool_destroy(pool);

    } /* for loop */

    octstr_destroy(sql);
    octstr_destroy(db_type);
    octstr_destroy(user);
    octstr_destroy(pass);
    octstr_destroy(db);
    octstr_destroy(host);
    gwlib_shutdown();

    return 0;
}


#endif /* HAVE_DBPOOL */
