/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2016 Kannel Group  
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
 * dlr_cass.c
 *
 * Implementation of handling delivery reports (DLRs)
 * for Cassandra NoSQL database
 *
 * Stipe Tolj <stolj at kannel.org, 01.10.2015
*/

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"


#ifdef HAVE_CASS

/*
 * Our connection pool to mysql.
 */
static DBPool *pool = NULL;

/*
 * Database fields, which we are use.
 */
static struct dlr_db_fields *fields = NULL;


static void dlr_cass_shutdown()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
}

static void dlr_cass_add(struct dlr_entry *entry)
{
    Octstr *sql, *os_mask;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    int res;

    debug("dlr.cass", 0, "adding DLR entry into database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL) {
        dlr_entry_destroy(entry);
        return;
    }

    sql = octstr_format("INSERT INTO %S (%S, %S, %S, %S, %S, %S, %S, %S, %S) VALUES "
                        "(?, ?, ?, ?, ?, ?, ?, ?, 0) ",
                        fields->table, fields->field_smsc, fields->field_ts,
                        fields->field_src, fields->field_dst, fields->field_serv,
                        fields->field_url, fields->field_mask, fields->field_boxc,
                        fields->field_status);
    os_mask = octstr_format("%d", entry->mask);
    gwlist_append(binds, entry->smsc);
    gwlist_append(binds, entry->timestamp);
    gwlist_append(binds, entry->source);
    gwlist_append(binds, entry->destination);
    gwlist_append(binds, entry->service);
    gwlist_append(binds, entry->url);
    gwlist_append(binds, os_mask);
    gwlist_append(binds, entry->boxc_id);

    /* add TTL value */
    if (fields->ttl) {
        octstr_format_append(sql, "USING TTL %ld;", fields->ttl);
    } else {
        /* final semicolon */
        octstr_append_char(sql, ';');
    }


#if defined(DLR_TRACE)
    debug("dlr.cass", 0, "cql: %s", octstr_get_cstr(sql));
#endif
    if ((res = dbpool_conn_update(pconn, sql, binds)) == -1)
        error(0, "DLR: Cassandra: Error while adding dlr entry for DST<%s>", octstr_get_cstr(entry->destination));

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
    gwlist_destroy(binds, NULL);
    octstr_destroy(os_mask);
    dlr_entry_destroy(entry);
}

static struct dlr_entry* dlr_cass_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;
    DBPoolConn *pconn;
    List *result = NULL, *row;
    struct dlr_entry *res = NULL;
    List *binds = gwlist_create();

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL)  /* sanity check */
        return NULL;

    sql = octstr_format("SELECT %S, %S, %S, %S, %S, %S FROM %S WHERE %S=? AND %S=? LIMIT 1;",
                        fields->field_mask, fields->field_serv,
                        fields->field_url, fields->field_src,
                        fields->field_dst, fields->field_boxc,
                        fields->table, fields->field_smsc,
                        fields->field_ts);

    gwlist_append(binds, (Octstr*) smsc);
    gwlist_append(binds, (Octstr*) ts);

#if defined(DLR_TRACE)
    debug("dlr.cass", 0, "cql: %s", octstr_get_cstr(sql));
#endif

    if (dbpool_conn_select(pconn, sql, binds, &result) != 0) {
        octstr_destroy(sql);
        gwlist_destroy(binds, NULL);
        dbpool_conn_produce(pconn);
        return NULL;
    }
    octstr_destroy(sql);
    gwlist_destroy(binds, NULL);
    dbpool_conn_produce(pconn);

#define LO2CSTR(r, i) octstr_get_cstr(gwlist_get(r, i))

    if (gwlist_len(result) > 0) {
        row = gwlist_extract_first(result);
        res = dlr_entry_create();
        gw_assert(res != NULL);
        res->mask = atoi(LO2CSTR(row,0));
        res->service = octstr_create(LO2CSTR(row, 1));
        res->url = octstr_create(LO2CSTR(row,2));
        res->source = octstr_create(LO2CSTR(row, 3));
        res->destination = octstr_create(LO2CSTR(row, 4));
        res->boxc_id = octstr_create(LO2CSTR(row, 5));
        gwlist_destroy(row, octstr_destroy_item);
        res->smsc = octstr_duplicate(smsc);
    }
    gwlist_destroy(result, NULL);

#undef LO2CSTR

    return res;
}

static void dlr_cass_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    int res;

    debug("dlr.cass", 0, "removing DLR from database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    sql = octstr_format("DELETE FROM %S WHERE %S=? AND %S=?;",
                        fields->table, fields->field_smsc,
                        fields->field_ts);

    gwlist_append(binds, (Octstr*) smsc);
    gwlist_append(binds, (Octstr*) ts);

#if defined(DLR_TRACE)
    debug("dlr.cass", 0, "cql: %s", octstr_get_cstr(sql));
#endif

    if ((res = dbpool_conn_update(pconn, sql, binds)) == -1)
        error(0, "DLR: Cassandra: Error while removing dlr entry for DST<%s>", octstr_get_cstr(dst));

    dbpool_conn_produce(pconn);
    gwlist_destroy(binds, NULL);
    octstr_destroy(sql);
}

static void dlr_cass_update(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *sql, *os_status;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    int res;

    debug("dlr.cass", 0, "updating DLR status in database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    sql = octstr_format("UPDATE %S SET %S=? WHERE %S=? AND %S=? LIMIT 1;",
                        fields->table, fields->field_status,
                        fields->field_smsc, fields->field_ts);

    os_status = octstr_format("%d", status);
    gwlist_append(binds, (Octstr*) os_status);
    gwlist_append(binds, (Octstr*) smsc);
    gwlist_append(binds, (Octstr*) ts);

#if defined(DLR_TRACE)
    debug("dlr.cass", 0, "cql: %s", octstr_get_cstr(sql));
#endif
    if ((res = dbpool_conn_update(pconn, sql, binds)) == -1)
        error(0, "DLR: Cassandra: Error while updating dlr entry for DST<%s>", octstr_get_cstr(dst));

    dbpool_conn_produce(pconn);
    gwlist_destroy(binds, NULL);
    octstr_destroy(os_status);
    octstr_destroy(sql);
}

static long dlr_cass_messages(void)
{
    List *result, *row;
    Octstr *sql;
    DBPoolConn *conn;
    long msgs = -1;

    conn = dbpool_conn_consume(pool);
    if (conn == NULL)
        return -1;

    sql = octstr_format("SELECT count(*) FROM %s", octstr_get_cstr(fields->table));
#if defined(DLR_TRACE)
    debug("dlr.cass", 0, "cql: %s", octstr_get_cstr(sql));
#endif

    if (dbpool_conn_select(conn, sql, NULL, &result) != 0) {
        octstr_destroy(sql);
        dbpool_conn_produce(conn);
        return -1;
    }
    dbpool_conn_produce(conn);
    octstr_destroy(sql);

    if (gwlist_len(result) > 0) {
        row = gwlist_extract_first(result);
        msgs = strtol(octstr_get_cstr(gwlist_get(row,0)), NULL, 10);
        gwlist_destroy(row, octstr_destroy_item);
    }
    gwlist_destroy(result, NULL);

    return msgs;
}

static void dlr_cass_flush(void)
{
    Octstr *sql;
    DBPoolConn *pconn;
    int rows;

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    sql = octstr_format("DELETE FROM %S", fields->table);
#if defined(DLR_TRACE)
    debug("dlr.mysql", 0, "cql: %s", octstr_get_cstr(sql));
#endif
    rows = dbpool_conn_update(pconn, sql, NULL);
    if (rows == -1)
        error(0, "DLR: Cassandra: Error while flushing dlr entries from database");
    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
}

static struct dlr_storage handles = {
    .type = "cassandra",
    .dlr_add = dlr_cass_add,
    .dlr_get = dlr_cass_get,
    .dlr_update = dlr_cass_update,
    .dlr_remove = dlr_cass_remove,
    .dlr_shutdown = dlr_cass_shutdown,
    .dlr_messages = dlr_cass_messages,
    .dlr_flush = dlr_cass_flush
};

struct dlr_storage *dlr_init_cass(Cfg *cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *host, *user, *pass, *db, *id;
    long port = 0;
    Octstr *p = NULL;
    long pool_size;
    DBConf *db_conf = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used Cassandra table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
        panic(0, "DLR: Cassandra: group 'dlr-db' is not specified!");

    if (!(id = cfg_get(grp, octstr_imm("id"))))
   	    panic(0, "DLR: Cassandra: directive 'id' is not specified!");

    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    /*
     * now grap the required information from the 'cassandra-connection' group
     * with the id we just obtained
     *
     * we have to loop through all available Cassandra connection definitions
     * and search for the one we are looking for
     */

    grplist = cfg_get_multi_group(cfg, octstr_imm("cassandra-connection"));
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
    }
    panic(0, "DLR: Cassandra: connection settings for id '%s' are not specified!",
          octstr_get_cstr(id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(host = cfg_get(grp, octstr_imm("host"))))
   	    panic(0, "DLR: Cassandra: directive 'host' is not specified!");
    /* optional(s) */
    user = cfg_get(grp, octstr_imm("username"));
    pass = cfg_get(grp, octstr_imm("password"));
    db = cfg_get(grp, octstr_imm("database"));
    cfg_get_integer(&port, grp, octstr_imm("port"));

    /*
     * ok, ready to connect to Cassandra
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->mysql = gw_malloc(sizeof(CassConf));
    gw_assert(db_conf->mysql != NULL);

    db_conf->cass->host = host;
    db_conf->cass->port = port;
    db_conf->cass->username = user;
    db_conf->cass->password = pass;
    db_conf->cass->database = db;

    pool = dbpool_create(DBPOOL_CASS, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"DLR: Cassandra: database pool has no connections!");

    octstr_destroy(id);

    return &handles;
}
#else
/*
 * Return NULL , so we point dlr-core that we were
 * not compiled in.
 */
struct dlr_storage *dlr_init_cass(Cfg* cfg)
{
    return NULL;
}
#endif /* HAVE_CASS */
