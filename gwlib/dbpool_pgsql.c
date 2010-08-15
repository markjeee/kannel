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
 * dbpool_pgsql.c - implement PostgreSQL operations for generic database connection pool
 *
 * modeled after dbpool_mysql.c 
 * Martiin Atukunda <matlads@myrealbox.com>
 */

#ifdef HAVE_PGSQL
#include <libpq-fe.h>


#define add1(str, value) \
    if (value != NULL && octstr_len(value) > 0) { \
        tmp = octstr_format(str, value); \
        octstr_append(cs, tmp); \
        octstr_destroy(tmp); \
    }


static void *pgsql_open_conn(const DBConf *db_conf)
{
    PGconn *conn = NULL;
    PgSQLConf *conf = db_conf->pgsql; /* make compiler happy */
    Octstr *tmp, *cs;

    /* sanity check */
    if (conf == NULL)
        return NULL;

    cs = octstr_create("");
    add1(" host=%S", conf->host);
    /* TODO: add hostaddr support via 'host' directive too.
     * This needs an octstr_is_addr(Octstr *os) checking if a given string
     * contains a valid IPv4 address. Obviously parsing on our own via gwlib
     * functions or using regex. If found, insert hostaddr instead of host
     * for the connection string. */
    /* add1(" hostaddr=%S", conf->host); */
    if (conf->port > 0) {   /* add only if user set a value */
        octstr_append_cstr(cs, " port=");    
        octstr_append_decimal(cs, conf->port);
    }
    add1(" user=%S", conf->username);
    add1(" password=%S", conf->password);
    add1(" dbname=%S", conf->database);

#if 0
    /* TODO: This is very bad to show password in the log file */
    info(0, "PGSQL: Using connection string: %s.", octstr_get_cstr(cs));
#endif

    conn = PQconnectdb(octstr_get_cstr(cs));

    octstr_destroy(cs);
    if (conn == NULL)
        goto failed;

    gw_assert(conn != NULL);

    if (PQstatus(conn) == CONNECTION_BAD) {
        error(0, "PGSQL: connection to database '%s' failed!", octstr_get_cstr(conf->database)); 
        panic(0, "PGSQL: %s", PQerrorMessage(conn));
        goto failed;
    }

    info(0, "PGSQL: Connected to server at '%s'.", octstr_get_cstr(conf->host));

    return conn;

failed:
    PQfinish(conn);
    return NULL;
}


static void pgsql_close_conn(void *conn)
{
    if (conn == NULL)
        return;

    PQfinish(conn);
    return;
}


static int pgsql_check_conn(void *conn)
{
    if (conn == NULL)
        return -1;
	
    if (PQstatus(conn) == CONNECTION_BAD) {    
        error(0, "PGSQL: Database check failed!");
        error(0, "PGSQL: %s", PQerrorMessage(conn));
        return -1;
    }	

    return 0;
}


static void pgsql_conf_destroy(DBConf *db_conf)
{
    PgSQLConf *conf = db_conf->pgsql;

    octstr_destroy(conf->host);
    octstr_destroy(conf->username);
    octstr_destroy(conf->password);
    octstr_destroy(conf->database);

    gw_free(conf);
    gw_free(db_conf);
}


static int pgsql_update(void *theconn, const Octstr *sql, List *binds)
{
    int	rows;
    PGresult *res = NULL;
    PGconn *conn = (PGconn*) theconn;

    res = PQexec(conn, octstr_get_cstr(sql));
    if (res == NULL)
        return -1;

    switch (PQresultStatus(res)) {
        case PGRES_BAD_RESPONSE:
        case PGRES_NONFATAL_ERROR:
        case PGRES_FATAL_ERROR:
            error(0, "PGSQL: %s", octstr_get_cstr(sql));
            error(0, "PGSQL: %s", PQresultErrorMessage(res));
            PQclear(res);
            return -1;
        default: /* for compiler please */
            break;
    }
    rows = atoi(PQcmdTuples(res));
    PQclear(res);

    return rows;
}


static int pgsql_select(void *theconn, const Octstr *sql, List *binds, List **list)
{
    int	nTuples, nFields, row_loop, field_loop;
    PGresult *res = NULL;
    List *fields;
    PGconn *conn = (PGconn*) theconn;

    gw_assert(list != NULL);
    *list = NULL;

    res = PQexec(conn, octstr_get_cstr(sql));
    if (res == NULL)
        return -1;

    switch (PQresultStatus(res)) {
        case PGRES_EMPTY_QUERY:
        case PGRES_BAD_RESPONSE:
        case PGRES_NONFATAL_ERROR:
        case PGRES_FATAL_ERROR:
            error(0, "PGSQL: %s", octstr_get_cstr(sql));
            error(0, "PGSQL: %s", PQresultErrorMessage(res));
            PQclear(res);
            return -1;
        default: /* for compiler please */
            break;
    }

    nTuples = PQntuples(res);
    nFields = PQnfields(res);
    *list = gwlist_create();
    for (row_loop = 0; row_loop < nTuples; row_loop++) {
        fields = gwlist_create();
    	for (field_loop = 0; field_loop < nFields; field_loop++) {
            if (PQgetisnull(res, row_loop, field_loop))
                gwlist_produce(fields, octstr_create(""));
            else 
                gwlist_produce(fields, octstr_create(PQgetvalue(res, row_loop, field_loop)));
        }
        gwlist_produce(*list, fields);
    }
    PQclear(res);

    return 0;
}


static struct db_ops pgsql_ops = {
    .open = pgsql_open_conn,
    .close = pgsql_close_conn,
    .check = pgsql_check_conn,
    .conf_destroy = pgsql_conf_destroy,
    .update = pgsql_update,
    .select = pgsql_select
};

#endif /* HAVE_PGSQL */

