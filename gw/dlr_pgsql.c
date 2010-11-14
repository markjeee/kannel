/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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
 * dlr_pgsql.c
 *
 * Implementation of handling delivery reports (DLRs)
 * for PostgreSQL database
 *
 * modeled after dlr_mysql.c
 *
 * Alexander Malysh <a.malysh@centrium.de>, cleanup 2004
 */

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"


#ifdef HAVE_PGSQL
#include <libpq-fe.h>

/*
 * Our connection pool to pgsql.
 */
static DBPool *pool = NULL;

/*
 * Database fields, which we are use.
 */
static struct dlr_db_fields *fields = NULL;


static inline int pgsql_update(const Octstr *sql)
{
    DBPoolConn *pc;
    int ret = 0;

#if defined(DLR_TRACE)
    debug("dlr.pgsql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "PGSQL: Database pool got no connection! DB update failed!");
        return -1;
    }

    if ((ret = dbpool_conn_update(pc, sql, NULL)) == -1)
        error(0, "PGSQL: DB update failed!");
    
    dbpool_conn_produce(pc);
    return ret;
}


static inline List *pgsql_select(const Octstr *sql)
{
    DBPoolConn *pc;
    List *ret = NULL;

#if defined(DLR_TRACE)
    debug("dlr.pgsql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "PGSQL: Database pool got no connection! DB operation failed!");
        return NULL;
    }

    if (dbpool_conn_select(pc, sql, NULL, &ret) == -1)
        error(0, "PGSQL: Select failed!");
    
    dbpool_conn_produce(pc);
    return ret;
}


static void dlr_pgsql_shutdown()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
}


static void dlr_pgsql_add(struct dlr_entry *entry)
{
    Octstr *sql;

    sql = octstr_format("INSERT INTO \"%s\" (\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\") VALUES "
                        "('%s', '%s', '%s', '%s', '%s', '%s', '%d', '%s', '%d');",
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(fields->field_ts),
                        octstr_get_cstr(fields->field_src), octstr_get_cstr(fields->field_dst),
                        octstr_get_cstr(fields->field_serv), octstr_get_cstr(fields->field_url),
                        octstr_get_cstr(fields->field_mask), octstr_get_cstr(fields->field_boxc),
                        octstr_get_cstr(fields->field_status),
                        octstr_get_cstr(entry->smsc), octstr_get_cstr(entry->timestamp), octstr_get_cstr(entry->source),
                        octstr_get_cstr(entry->destination), octstr_get_cstr(entry->service), octstr_get_cstr(entry->url),
                        entry->mask, octstr_get_cstr(entry->boxc_id), 0);


    if (!pgsql_update(sql))
       warning(0, "DLR: PGSQL: No dlr inserted for DST<%s>", octstr_get_cstr(entry->destination));
    
    octstr_destroy(sql);
    dlr_entry_destroy(entry);
}


static struct dlr_entry *dlr_pgsql_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    struct dlr_entry *res = NULL;
    Octstr *sql, *like;
    List *result, *row;

    if (dst)
        like = octstr_format("AND \"%S\" LIKE '%%%S'", fields->field_dst, dst);
    else
        like = octstr_imm("");

    sql = octstr_format("SELECT \"%S\", \"%S\", \"%S\", \"%S\", \"%S\", "
          "\"%S\" FROM \"%S\" WHERE \"%S\"='%S' AND \"%S\"='%S' %S LIMIT 1;",
          fields->field_mask, fields->field_serv, fields->field_url,
          fields->field_src, fields->field_dst, fields->field_boxc,
          fields->table, fields->field_smsc, smsc, fields->field_ts, ts, like);

    result = pgsql_select(sql);
    octstr_destroy(sql);
    octstr_destroy(like);

    if (result == NULL || gwlist_len(result) < 1) {
        debug("dlr.pgsql", 0, "no rows found");
        while((row = gwlist_extract_first(result)))
            gwlist_destroy(row, octstr_destroy_item);
        gwlist_destroy(result, NULL);
        return NULL;
    }

    row = gwlist_get(result, 0);

    debug("dlr.pgsql", 0, "Found entry, col1=%s, col2=%s, col3=%s, col4=%s, col5=%s col6=%s",
		    octstr_get_cstr(gwlist_get(row, 0)),
		    octstr_get_cstr(gwlist_get(row, 1)),
		    octstr_get_cstr(gwlist_get(row, 2)),
		    octstr_get_cstr(gwlist_get(row, 3)),
		    octstr_get_cstr(gwlist_get(row, 4)),
		    octstr_get_cstr(gwlist_get(row, 5))
	 );

    res = dlr_entry_create();
    gw_assert(res != NULL);
    res->mask        = atoi(octstr_get_cstr(gwlist_get(row, 0)));
    res->service     = octstr_duplicate(gwlist_get(row, 1));
    res->url         = octstr_duplicate(gwlist_get(row, 2));
    res->source      = octstr_duplicate(gwlist_get(row, 3));
    res->destination = octstr_duplicate(gwlist_get(row, 4));
    res->boxc_id     = octstr_duplicate(gwlist_get(row, 5));
    res->smsc        = octstr_duplicate(smsc);

    while((row = gwlist_extract_first(result)))
        gwlist_destroy(row, octstr_destroy_item);
    gwlist_destroy(result, NULL);

    return res;
}


static void dlr_pgsql_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql, *like;

    debug("dlr.pgsql", 0, "removing DLR from database");
    if (dst)
        like = octstr_format("AND \"%S\" LIKE '%%%S'", fields->field_dst, dst);
    else
        like = octstr_imm("");

    sql = octstr_format("DELETE FROM \"%S\" WHERE oid = (SELECT oid FROM "
          "\"%S\" WHERE \"%S\"='%S' AND \"%S\"='%S' %S LIMIT 1);",
          fields->table, fields->table, fields->field_smsc, smsc,
          fields->field_ts, ts, like);

    if (!pgsql_update(sql))
       warning(0, "DLR: PGSQL: No dlr deleted for DST<%s>", octstr_get_cstr(dst));
    octstr_destroy(sql);
    octstr_destroy(like);
}


static void dlr_pgsql_update(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *sql, *like;

    debug("dlr.pgsql", 0, "updating DLR status in database");
    if (dst)
        like = octstr_format("AND \"%S\" LIKE '%%%S'", fields->field_dst, dst);
    else
        like = octstr_imm("");

    sql = octstr_format("UPDATE \"%S\" SET \"%S\"=%d WHERE oid = (SELECT "
        "oid FROM \"%S\" WHERE \"%S\"='%S' AND \"%S\"='%S' %S LIMIT 1);",
        fields->table, fields->field_status, status, fields->table,
        fields->field_smsc, smsc, fields->field_ts, ts, like);

    if (!pgsql_update(sql))
       warning(0, "DLR: PGSQL: No dlr updated for DST<%s> (status: %d)", octstr_get_cstr(dst), status);
    octstr_destroy(sql);
    octstr_destroy(like);
}


static long dlr_pgsql_messages(void)
{
    Octstr *sql;
    long ret;
    List *res;

    sql = octstr_format("SELECT count(*) FROM \"%s\";", octstr_get_cstr(fields->table));

    res = pgsql_select(sql);
    octstr_destroy(sql);

    if (res == NULL || gwlist_len(res) < 1) {
        error(0, "PGSQL: Could not get count of DLR table");
        ret = -1;
    } else {
        ret = atol(octstr_get_cstr(gwlist_get(gwlist_get(res, 0), 0)));
    }

    gwlist_destroy(gwlist_extract_first(res), octstr_destroy_item);
    gwlist_destroy(res, NULL);
        
    return ret;
}


static void dlr_pgsql_flush(void)
{
    Octstr *sql;

    sql = octstr_format("DELETE FROM \"%s\";", octstr_get_cstr(fields->table));

    pgsql_update(sql);
    octstr_destroy(sql);
}


static struct dlr_storage handles = {
    .type = "pgsql",
    .dlr_add = dlr_pgsql_add,
    .dlr_get = dlr_pgsql_get,
    .dlr_update = dlr_pgsql_update,
    .dlr_remove = dlr_pgsql_remove,
    .dlr_shutdown = dlr_pgsql_shutdown,
    .dlr_messages = dlr_pgsql_messages,
    .dlr_flush = dlr_pgsql_flush
};


struct dlr_storage *dlr_init_pgsql(Cfg *cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *pgsql_host, *pgsql_user, *pgsql_pass, *pgsql_db, *pgsql_id;
    long pgsql_port = 0;
    Octstr *p = NULL;
    long pool_size;
    DBConf *db_conf = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the table used
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
        panic(0, "DLR: PgSQL: group 'dlr-db' is not specified!");

    if (!(pgsql_id = cfg_get(grp, octstr_imm("id"))))
   	    panic(0, "DLR: PgSQL: directive 'id' is not specified!");

    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    /*
     * Escaping special quotes for field/table names
     */
    octstr_replace(fields->table, octstr_imm("\""), octstr_imm("\"\""));
    octstr_replace(fields->field_smsc, octstr_imm("\""), octstr_imm("\"\""));
    octstr_replace(fields->field_ts, octstr_imm("\""), octstr_imm("\"\""));
    octstr_replace(fields->field_src, octstr_imm("\""), octstr_imm("\"\""));
    octstr_replace(fields->field_dst, octstr_imm("\""), octstr_imm("\"\""));
    octstr_replace(fields->field_serv, octstr_imm("\""), octstr_imm("\"\""));
    octstr_replace(fields->field_url, octstr_imm("\""), octstr_imm("\"\""));      
    octstr_replace(fields->field_mask, octstr_imm("\""), octstr_imm("\"\""));
    octstr_replace(fields->field_status, octstr_imm("\""), octstr_imm("\"\"")); 
    octstr_replace(fields->field_boxc, octstr_imm("\""), octstr_imm("\"\""));

    /*
     * now grap the required information from the 'pgsql-connection' group
     * with the pgsql-id we just obtained
     *
     * we have to loop through all available PostgreSQL connection definitions
     * and search for the one we are looking for
     */

    grplist = cfg_get_multi_group(cfg, octstr_imm("pgsql-connection"));
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, pgsql_id) == 0) {
            goto found;
        }
        if (p != NULL) 
            octstr_destroy(p);
    }
    panic(0, "DLR: PgSQL: connection settings for id '%s' are not specified!",
          octstr_get_cstr(pgsql_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(pgsql_host = cfg_get(grp, octstr_imm("host"))))
   	    panic(0, "DLR: PgSQL: directive 'host' is not specified!");
    if (!(pgsql_user = cfg_get(grp, octstr_imm("username"))))
   	    panic(0, "DLR: PgSQL: directive 'username' is not specified!");
    if (!(pgsql_pass = cfg_get(grp, octstr_imm("password"))))
   	    panic(0, "DLR: PgSQL: directive 'password' is not specified!");
    if (!(pgsql_db = cfg_get(grp, octstr_imm("database"))))
   	    panic(0, "DLR: PgSQL: directive 'database' is not specified!");
    cfg_get_integer(&pgsql_port, grp, octstr_imm("port"));  /* optional */

    /*
     * ok, ready to connect to the database
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->pgsql = gw_malloc(sizeof(PgSQLConf));
    gw_assert(db_conf->pgsql != NULL);

    db_conf->pgsql->host = pgsql_host;
    db_conf->pgsql->port = pgsql_port;
    db_conf->pgsql->username = pgsql_user;
    db_conf->pgsql->password = pgsql_pass;
    db_conf->pgsql->database = pgsql_db;

    pool = dbpool_create(DBPOOL_PGSQL, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"DLR: PgSQL: database pool has no connections!");

    octstr_destroy(pgsql_id);

    return &handles;
}
#else
/*
 * Return NULL , so we point dlr-core that we were
 * not compiled in.
 */
struct dlr_storage *dlr_init_pgsql(Cfg* cfg)
{
    return NULL;
}
#endif /* HAVE_PGSQL */

