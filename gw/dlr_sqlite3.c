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
 * dlr_sqlite3.c - sqlite3 dlr storage implementation.
 *
 * Author: David Butler <gdb@dbSystems.com>
 * 
 * Based on dlr_oracle.c
 *
 * Copyright: See COPYING file that comes with this distribution
 */

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"


#ifdef HAVE_SQLITE3

/*
 * Our connection pool to sqlite3.
 */
static DBPool *pool = NULL;

/*
 * Database fields, which we are use.
 */
static struct dlr_db_fields *fields = NULL;


static long dlr_messages_sqlite3()
{
    List *result, *row;
    Octstr *sql;
    DBPoolConn *conn;
    long msgs = -1;

    conn = dbpool_conn_consume(pool);
    if (conn == NULL)
        return -1;

    sql = octstr_format("SELECT count(*) FROM %S", fields->table);
#if defined(DLR_TRACE)
    debug("dlr.sqlite3", 0, "sql: %s", octstr_get_cstr(sql));
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
        msgs = strtol(octstr_get_cstr(gwlist_get(row, 0)), NULL, 10);
        gwlist_destroy(row, octstr_destroy_item);
    }
    gwlist_destroy(result, NULL);

    return msgs;
}

static void dlr_shutdown_sqlite3()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
}

static void dlr_add_sqlite3(struct dlr_entry *entry)
{
    Octstr *sql, *os_mask;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    int res;

    debug("dlr.sqlite3", 0, "adding DLR entry into database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL) {
        dlr_entry_destroy(entry);
        return;
    }

    sql = octstr_format("INSERT INTO %S (%S, %S, %S, %S, %S, %S, %S, %S, %S) VALUES "
                        "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 0)",
                        fields->table, fields->field_smsc, fields->field_ts,
                        fields->field_src, fields->field_dst, fields->field_serv, 
                        fields->field_url, fields->field_mask, fields->field_boxc,
                        fields->field_status);
    os_mask = octstr_format("%d", entry->mask);
    
    gwlist_append(binds, entry->smsc);         /* ?1 */
    gwlist_append(binds, entry->timestamp);    /* ?2 */
    gwlist_append(binds, entry->source);       /* ?3 */
    gwlist_append(binds, entry->destination);  /* ?4 */
    gwlist_append(binds, entry->service);      /* ?5 */
    gwlist_append(binds, entry->url);          /* ?6 */
    gwlist_append(binds, os_mask);             /* ?7 */
    gwlist_append(binds, entry->boxc_id);      /* ?8 */
#if defined(DLR_TRACE)
    debug("dlr.sqlite3", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if ((res = dbpool_conn_update(pconn, sql, binds)) == -1)
        error(0, "DLR: SQLite3: Error while adding dlr entry for DST<%s>", octstr_get_cstr(entry->destination));
    else if (!res)
        warning(0, "DLR: SQLite3: No dlr inserted for DST<%s>", octstr_get_cstr(entry->destination));

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
    gwlist_destroy(binds, NULL);
    octstr_destroy(os_mask);
    dlr_entry_destroy(entry);
}

static void dlr_remove_sqlite3(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql, *like;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    int res;
    debug("dlr.sqlite3", 0, "removing DLR from database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;
    
    if (dst)
        like = octstr_format("AND %S LIKE '%?3'", fields->field_dst);
    else
        like = octstr_imm("");

    sql = octstr_format("DELETE FROM %S WHERE ROWID IN (SELECT ROWID FROM %S WHERE %S=?1 AND %S=?2 %S LIMIT 1)",
                        fields->table, fields->table,
                        fields->field_smsc, fields->field_ts, like);

    gwlist_append(binds, (Octstr *)smsc);      /* ?1 */
    gwlist_append(binds, (Octstr *)ts);        /* ?2 */
    if (dst)
        gwlist_append(binds, (Octstr *)dst);   /* ?3 */

#if defined(DLR_TRACE)
    debug("dlr.sqlite3", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    if ((res = dbpool_conn_update(pconn, sql, binds)) == -1)
        error(0, "DLR: SQLite3: Error while removing dlr entry for DST<%s>", octstr_get_cstr(dst));
    else if (!res)
        warning(0, "DLR: SQLite3: No dlr deleted for DST<%s>", octstr_get_cstr(dst));

    dbpool_conn_produce(pconn);
    gwlist_destroy(binds, NULL);
    octstr_destroy(sql);
    octstr_destroy(like);
}

static struct dlr_entry* dlr_get_sqlite3(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql, *like;
    DBPoolConn *pconn;
    List *result = NULL, *row;
    struct dlr_entry *res = NULL;
    List *binds = gwlist_create();

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) /* should not happens, but sure is sure */
        return NULL;

    if (dst)
        like = octstr_format("AND %S LIKE '%?3'", fields->field_dst);
    else
        like = octstr_imm("");

    sql = octstr_format("SELECT %S, %S, %S, %S, %S, %S FROM %S WHERE %S=?1 AND %S=?2 %S LIMIT 1",
                        fields->field_mask, fields->field_serv,
                        fields->field_url, fields->field_src,
                        fields->field_dst, fields->field_boxc,
                        fields->table, fields->field_smsc,
                        fields->field_ts, like);

    gwlist_append(binds, (Octstr *)smsc);      /* ?1 */
    gwlist_append(binds, (Octstr *)ts);        /* ?2 */
    if (dst)
        gwlist_append(binds, (Octstr *)dst);   /* ?3 */

#if defined(DLR_TRACE)
    debug("dlr.sqlite3", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if (dbpool_conn_select(pconn, sql, binds, &result) != 0) {
        octstr_destroy(sql);
        dbpool_conn_produce(pconn);
        return NULL;
    }
    octstr_destroy(sql);
    octstr_destroy(like);
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

static void dlr_update_sqlite3(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *sql, *os_status, *like;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    int res;

    debug("dlr.sqlite3", 0, "updating DLR status in database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    if (dst)
        like = octstr_format("AND %S LIKE '%?4'", fields->field_dst);
    else
        like = octstr_imm("");

    sql = octstr_format("UPDATE %S SET %S=?1 WHERE ROWID IN (SELECT ROWID FROM %S WHERE %S=?2 AND %S=?3 %S LIMIT 1)",
                        fields->table, fields->field_status, fields->table,
                        fields->field_smsc, fields->field_ts, fields->field_dst);

    os_status = octstr_format("%d", status);
    gwlist_append(binds, (Octstr *)os_status); /* ?1 */
    gwlist_append(binds, (Octstr *)smsc);      /* ?2 */
    gwlist_append(binds, (Octstr *)ts);        /* ?3 */
    if (dst)
        gwlist_append(binds, (Octstr *)dst);   /* ?4 */
    
#if defined(DLR_TRACE)
    debug("dlr.sqlite3", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if ((res = dbpool_conn_update(pconn, sql, binds)) == -1)
        error(0, "DLR: SQLite3: Error while updating dlr entry for DST<%s>", octstr_get_cstr(dst));
    else if (!res)
        warning(0, "DLR: SQLite3: No dlr found to update for DST<%s> (status: %d)", octstr_get_cstr(dst), status);

    dbpool_conn_produce(pconn);
    gwlist_destroy(binds, NULL);
    octstr_destroy(os_status);
    octstr_destroy(sql);
    octstr_destroy(like);
}

static void dlr_flush_sqlite3 (void)
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
    debug("dlr.sqlite3", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    rows = dbpool_conn_update(pconn, sql, NULL);
    if (rows == -1)
        error(0, "DLR: SQLite3: Error while flushing dlr entries from database");
    else
        debug("dlr.sqlite3", 0, "Flushing %d DLR entries from database", rows);
    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
}

static struct dlr_storage handles = {
    .type = "sqlite3",
    .dlr_messages = dlr_messages_sqlite3,
    .dlr_shutdown = dlr_shutdown_sqlite3,
    .dlr_add = dlr_add_sqlite3,
    .dlr_get = dlr_get_sqlite3,
    .dlr_remove = dlr_remove_sqlite3,
    .dlr_update = dlr_update_sqlite3,
    .dlr_flush = dlr_flush_sqlite3
};

struct dlr_storage *dlr_init_sqlite3(Cfg *cfg)
{
    CfgGroup *grp;
    List *grplist;
    long pool_size;
    DBConf *db_conf = NULL;
    Octstr *id, *file;
    int found;

    if ((grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))) == NULL)
        panic(0, "DLR: SQLite3: group 'dlr-db' is not specified!");

    if (!(id = cfg_get(grp, octstr_imm("id"))))
       panic(0, "DLR: SQLite3: directive 'id' is not specified!");

    /* initialize database fields */
    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    grplist = cfg_get_multi_group(cfg, octstr_imm("sqlite3-connection"));
    found = 0;
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        Octstr *p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, id) == 0) {
            found = 1;
        }
        if (p != NULL) 
            octstr_destroy(p);
        if (found == 1) 
            break;
    }
    gwlist_destroy(grplist, NULL);

    if (found == 0)
        panic(0, "DLR: SQLite3: connection settings for id '%s' are not specified!",
              octstr_get_cstr(id));

    file = cfg_get(grp, octstr_imm("database"));
    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1)
        pool_size = 1;

    if (file == NULL)
        panic(0, "DLR: SQLite3: connection settings missing for id '%s', please"
                 " check you configuration.",octstr_get_cstr(id));

    /* ok we are ready to create dbpool */
    db_conf = gw_malloc(sizeof(*db_conf));
    db_conf->sqlite3 = gw_malloc(sizeof(SQLite3Conf));

    db_conf->sqlite3->file = file;

    pool = dbpool_create(DBPOOL_SQLITE3, db_conf, pool_size);
    gw_assert(pool != NULL);

    if (dbpool_conn_count(pool) == 0)
        panic(0, "DLR: SQLite3: Could not establish sqlite3 connection(s).");

    octstr_destroy(id);

    return &handles;
}
#else
/* no sqlite3 support build in */
struct dlr_storage *dlr_init_sqlite3(Cfg *cfg)
{
    return NULL;
}
#endif /* HAVE_SQLITE3 */
