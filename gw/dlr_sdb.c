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
 * dlr_sdb.c
 *
 * Implementation of handling delivery reports (DLRs)
 * for LibSDB.
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <stolj@wapme.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
 * Guillaume Cottenceau 2004 (dbpool support)
*/

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"

#ifdef HAVE_SDB
#include <sdb.h>

/*
 * Our connection pool to sdb.
 */
static DBPool *pool = NULL;

/*
 * Database fields, which we use.
 */
static struct dlr_db_fields *fields = NULL;

enum {
    SDB_ORACLE,
    SDB_MYSQL,
    SDB_POSTGRES,
    SDB_OTHER
};

static long sdb_conn_type = SDB_OTHER;


static const char* sdb_get_limit_str()
{
    switch (sdb_conn_type) {
        case SDB_ORACLE:
            return "AND ROWNUM < 2";
        case SDB_MYSQL:
        case SDB_POSTGRES:
            return "LIMIT 1";
        case SDB_OTHER:
        default:
            return "";
    }
}

static void dlr_sdb_shutdown()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
}

static int gw_sdb_query(char *query,
                        int (*callback)(int, char **, void *), void *closure)
{
    DBPoolConn *pc;
    int rows;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SDB: Database pool got no connection!");
        return -1;
    }

    rows = sdb_query(pc->conn, query, callback, closure);

    dbpool_conn_produce(pc);

    return rows;
}

static void dlr_sdb_add(struct dlr_entry *dlr)
{
    Octstr *sql;
    int	state;

    sql = octstr_format("INSERT INTO %s (%s, %s, %s, %s, %s, %s, %s, %s, %s) VALUES "
                        "('%s', '%s', '%s', '%s', '%s', '%s', '%d', '%s', '%d')",
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(fields->field_ts),
                        octstr_get_cstr(fields->field_src), octstr_get_cstr(fields->field_dst),
                        octstr_get_cstr(fields->field_serv), octstr_get_cstr(fields->field_url),
                        octstr_get_cstr(fields->field_mask), octstr_get_cstr(fields->field_boxc),
                        octstr_get_cstr(fields->field_status),
                        octstr_get_cstr(dlr->smsc), octstr_get_cstr(dlr->timestamp),
                        octstr_get_cstr(dlr->source), octstr_get_cstr(dlr->destination),
                        octstr_get_cstr(dlr->service), octstr_get_cstr(dlr->url), dlr->mask,
                        octstr_get_cstr(dlr->boxc_id), 0);

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "SDB: sql: %s", octstr_get_cstr(sql));
#endif

    state = gw_sdb_query(octstr_get_cstr(sql), NULL, NULL);
    if (state == -1)
        error(0, "SDB: error in inserting DLR for DST <%s>", octstr_get_cstr(dlr->destination));
    else if (!state)
        warning(0, "SDB: No dlr inserted for DST <%s>", octstr_get_cstr(dlr->destination));

    octstr_destroy(sql);
    dlr_entry_destroy(dlr);
}

static int sdb_callback_add(int n, char **p, void *data)
{
    struct dlr_entry *res = (struct dlr_entry *) data;

    if (n != 6) {
        debug("dlr.sdb", 0, "SDB: Result has incorrect number of columns: %d", n);
        return 0;
    }

#if defined(DLR_TRACE)
    debug("dlr.sdb", 0, "row=%s,%s,%s,%s,%s,%s",p[0],p[1],p[2],p[3],p[4],p[5]);
#endif

    if (res->destination != NULL) {
        debug("dlr.sdb", 0, "SDB: Row already stored.");
        return 0;
    }

    res->mask = atoi(p[0]);
    res->service = octstr_create(p[1]);
    res->url = octstr_create(p[2]);
    res->source = octstr_create(p[3]);
    res->destination = octstr_create(p[4]);
    res->boxc_id = octstr_create(p[5]);

    return 0;
}

static int sdb_callback_msgs(int n, char **p, void *data)
{
    long *count = (long *) data;

    if (n != 1) {
        debug("dlr.sdb", 0, "SDB: Result has incorrect number of columns: %d", n);
        return 0;
    }

#if defined(DLR_TRACE)
    debug("dlr.sdb", 0, "SDB: messages=%s",p[0]);
#endif

    *count = atol(p[0]);

    return 0;
}

static struct dlr_entry*  dlr_sdb_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql, *like;
    int	state;
    struct dlr_entry *res = dlr_entry_create();

    gw_assert(res != NULL);

    if (dst)
        like = octstr_format("AND %S LIKE '%%%S'", fields->field_dst, dst);
    else
        like = octstr_imm("");

    sql = octstr_format("SELECT %S, %S, %S, %S, %S, %S FROM %S WHERE %S='%S' "
          "AND %S='%S' %S %s", fields->field_mask, fields->field_serv,
          fields->field_url, fields->field_src, fields->field_dst,
          fields->field_boxc, fields->table, fields->field_smsc, smsc,
          fields->field_ts, ts, like, sdb_get_limit_str());

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "SDB: sql: %s", octstr_get_cstr(sql));
#endif

    state = gw_sdb_query(octstr_get_cstr(sql), sdb_callback_add, res);
    octstr_destroy(sql);
    octstr_destroy(like);
    if (state == -1) {
        error(0, "SDB: error in finding DLR");
        goto notfound;
    }
    else if (state == 0) {
        debug("dlr.sdb", 0, "SDB: no entry found for DST <%s>.", octstr_get_cstr(dst));
        goto notfound;
    }

    res->smsc = octstr_duplicate(smsc);

    return res;

notfound:
    dlr_entry_destroy(res);
    return NULL;
}

static void  dlr_sdb_update(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *sql, *like;
    int	state;

    debug("dlr.sdb", 0, "SDB: updating DLR status in database");

    if (dst)
        like = octstr_format("AND %S LIKE '%%%S'", fields->field_dst, dst);
    else
        like = octstr_imm("");

    sql = octstr_format("UPDATE %S SET %S=%d WHERE %S='%S' AND %S='%S' %S %s",
          fields->table, fields->field_status, status, fields->field_smsc,
          smsc, fields->field_ts, ts, like, sdb_get_limit_str());

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "SDB: sql: %s", octstr_get_cstr(sql));
#endif

    state = gw_sdb_query(octstr_get_cstr(sql), NULL, NULL);
    octstr_destroy(sql);
    octstr_destroy(like);
    if (state == -1)
        error(0, "SDB: error in updating DLR");
    else if (!state)
        warning(0, "SDB: No dlr to update for DST<%s> (status %d)", octstr_get_cstr(dst), status);
}

static void  dlr_sdb_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql, *like;
    int	state;

    debug("dlr.sdb", 0, "removing DLR from database");
    if (sdb_conn_type == SDB_POSTGRES) {
        /*
         * Postgres doesn't support limiting delete/update queries,
         * thus we need to use a select subquery.
         * - notice that for uniqueness use of `oid', postgres suggests
         * to do vacuum regularly, even if it's virtually impossible
         * to hit duplicates since oid's are given in a row
         */
       if (dst)
           like = octstr_format("AND %S LIKE '%%%S')",
                fields->field_dst, dst);
       else
           like = octstr_imm("LIMIT 1)");
       sql = octstr_format("DELETE FROM %S WHERE oid = (SELECT oid FROM %S "
             "WHERE %S='%S' AND %S='%S' %S LIMIT 1", fields->table,
             fields->table, fields->field_smsc, smsc, fields->field_ts, ts,
             like);
    } else {
       if (dst)
           like = octstr_format("AND %S LIKE '%%%S'", fields->field_dst, dst);
       else
           like = octstr_imm("");

       sql = octstr_format("DELETE FROM %S WHERE %S='%S' AND %S='%S' %S %s",
             fields->table, fields->field_smsc, smsc, fields->field_ts, ts,
             like, sdb_get_limit_str());
    }

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "SDB: sql: %s", octstr_get_cstr(sql));
#endif

    state = gw_sdb_query(octstr_get_cstr(sql), NULL, NULL);
    octstr_destroy(sql);
    octstr_destroy(like);
    if (state == -1)
        error(0, "SDB: error in deleting DLR");
    else if (!state)
        warning(0, "SDB: No dlr deleted for DST<%s>", octstr_get_cstr(dst));
}

static long dlr_sdb_messages(void)
{
    Octstr *sql;
    int	state;
    long res = 0;

    sql = octstr_format("SELECT count(*) FROM %s", octstr_get_cstr(fields->table));

#if defined(DLR_TRACE)
    debug("dlr.sdb", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    state = gw_sdb_query(octstr_get_cstr(sql), sdb_callback_msgs, &res);
    octstr_destroy(sql);
    if (state == -1) {
        error(0, "SDB: error in selecting ammount of waiting DLRs");
        return -1;
    }

    return res;
}

static void dlr_sdb_flush(void)
{
    Octstr *sql;
    int	state;

    sql = octstr_format("DELETE FROM %s", octstr_get_cstr(fields->table));

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    state = gw_sdb_query(octstr_get_cstr(sql), NULL, NULL);
    octstr_destroy(sql);
    if (state == -1) {
        error(0, "SDB: error in flusing DLR table");
    }
}


static struct dlr_storage  handles = {
    .type = "sdb",
    .dlr_add = dlr_sdb_add,
    .dlr_get = dlr_sdb_get,
    .dlr_update = dlr_sdb_update,
    .dlr_remove = dlr_sdb_remove,
    .dlr_shutdown = dlr_sdb_shutdown,
    .dlr_messages = dlr_sdb_messages,
    .dlr_flush = dlr_sdb_flush
};

struct dlr_storage *dlr_init_sdb(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *sdb_url, *sdb_id;
    Octstr *p = NULL;
    long pool_size;
    DBConf *db_conf = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
        panic(0, "DLR: SDB: group 'dlr-db' is not specified!");

    if (!(sdb_id = cfg_get(grp, octstr_imm("id"))))
   	    panic(0, "DLR: SDB: directive 'id' is not specified!");

    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    /*
     * now grap the required information from the 'mysql-connection' group
     * with the sdb-id we just obtained
     *
     * we have to loop through all available SDB connection definitions
     * and search for the one we are looking for
     */

    grplist = cfg_get_multi_group(cfg, octstr_imm("sdb-connection"));
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, sdb_id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
    }
    panic(0, "DLR: SDB: connection settings for id '%s' are not specified!",
          octstr_get_cstr(sdb_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(sdb_url = cfg_get(grp, octstr_imm("url"))))
   	    panic(0, "DLR: SDB: directive 'url' is not specified!");

    if (octstr_search(sdb_url, octstr_imm("oracle:"), 0) == 0)
        sdb_conn_type = SDB_ORACLE;
    else if (octstr_search(sdb_url, octstr_imm("mysql:"), 0) == 0) {
        warning(0, "DLR[sdb]: Please use native MySQL support, instead of libsdb.");
        sdb_conn_type = SDB_MYSQL;
    }
    else if (octstr_search(sdb_url, octstr_imm("postgres:"), 0) == 0) {
        sdb_conn_type = SDB_POSTGRES;
    }
    else
        sdb_conn_type = SDB_OTHER;

    /*
     * ok, ready to connect
     */
    info(0,"Connecting to sdb resource <%s>.", octstr_get_cstr(sdb_url));

    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->sdb = gw_malloc(sizeof(SDBConf));
    gw_assert(db_conf->sdb != NULL);

    db_conf->sdb->url = sdb_url;

    pool = dbpool_create(DBPOOL_SDB, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"DLR: SDB: database pool has no connections!");

    return &handles;
}
#else
/*
 * Return NULL , so we point dlr-core that we were
 * not compiled in.
 */
struct dlr_storage *dlr_init_sdb(Cfg* cfg)
{
    return NULL;
}
#endif /* HAVE_SDB */
