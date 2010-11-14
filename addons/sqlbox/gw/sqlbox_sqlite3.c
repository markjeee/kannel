#include "gwlib/gwlib.h"
#ifdef HAVE_SQLITE3
#include "gwlib/dbpool.h"
#include <sqlite3.h>
#define sqlbox_sqlite3_c
#include "sqlbox_sqlite3.h"

#define sql_update sqlite3_update
#define sql_select sqlite3_select

static Octstr *sqlbox_logtable;
static Octstr *sqlbox_insert_table;

/*
 * Our connection pool to sqlite3.
 */

static DBPool *pool = NULL;

/*
 *-------------------------------------------------
 * sqlite3 thingies
 *-------------------------------------------------
*/

#define octstr_null_create(x) ((x != NULL) ? octstr_create(x) : octstr_create(""))
#define atol_null(x) ((x != NULL) ? atol(x) : -1)

static int sqlite3_update(DBPoolConn *conn, const Octstr *sql)
{
    int state;
    char *errmsg = 0;

#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    state = sqlite3_exec(conn->conn, octstr_get_cstr(sql), NULL, 0, &errmsg);
    if (state != SQLITE_OK) {
        error(0, "SQLITE3: %s", sqlite3_errmsg(conn->conn));
        return -1;
    }
    return sqlite3_changes(conn->conn);
}

sqlite3_stmt* sqlite3_select(DBPoolConn *conn, const Octstr *sql)
{
    int res;
    sqlite3_stmt *stmt = NULL;

#if defined(SQLBOX_TRACE)
    debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    res = sqlite3_prepare_v2(conn->conn, octstr_get_cstr(sql), -1, &stmt, NULL);
    if (res != SQLITE_OK) {
        error(0, "SQLITE3: Could not compile query: %s", sqlite3_errmsg(conn->conn));
        return NULL;
    }
    return stmt;
}

void sqlbox_configure_sqlite3(Cfg* cfg)
{
    CfgGroup *grp;
    Octstr *sql;
    DBPoolConn *pc;

    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: Sqlite3: group 'sqlbox' is not specified!");

    sqlbox_logtable = cfg_get(grp, octstr_imm("sql-log-table"));
    if (sqlbox_logtable == NULL) {
        panic(0, "Parameter 'sql-log-table' configured.");
    }
    sqlbox_insert_table = cfg_get(grp, octstr_imm("sql-insert-table"));
    if (sqlbox_insert_table == NULL) {
        panic(0, "Parameter 'sql-insert-table' configured.");
    }

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SQLITE3: Database pool got no connection! DB update failed!");
        return;
    }

    /* create send_sms && sent_sms tables if they do not exist */
    sql = octstr_format(SQLBOX_SQLITE3_CREATE_LOG_TABLE, sqlbox_logtable);
    sql_update(pc, sql);
    octstr_destroy(sql);
    sql = octstr_format(SQLBOX_SQLITE3_CREATE_LOG_TABLE, sqlbox_insert_table);
    sql_update(pc, sql);
    octstr_destroy(sql);
    /* end table creation */
    dbpool_conn_produce(pc);
}

Msg *sqlite3_fetch_msg()
{
    int state;
    DBPoolConn *pc;
    sqlite3_stmt *res = NULL;
    int rows = 0;
    Msg *msg = NULL;
    Octstr *sql, *delet, *id = NULL;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SQLITE3: Database pool got no connection! DB update failed!");
        return NULL;
    }

    sql = octstr_format(SQLBOX_SQLITE3_SELECT_QUERY, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    res = sql_select(pc, sql);
    do {
        state=sqlite3_step(res);
        if (state==SQLITE_ROW){
            rows++;
            id = octstr_null_create((char *)sqlite3_column_text(res, 0));
            /* save fields in this row as msg struct */
            msg = msg_create(sms);
            msg->sms.sender     = octstr_null_create((char *)sqlite3_column_text(res, 2));
            msg->sms.receiver   = octstr_null_create((char *)sqlite3_column_text(res, 3));
            msg->sms.udhdata    = octstr_null_create((char *)sqlite3_column_text(res, 4));
            msg->sms.msgdata    = octstr_null_create((char *)sqlite3_column_text(res, 5));
            msg->sms.time       = atol_null((char *)sqlite3_column_text(res,6));
            msg->sms.smsc_id    = octstr_null_create((char *)sqlite3_column_text(res, 7));
            msg->sms.service    = octstr_null_create((char *)sqlite3_column_text(res, 8));
            msg->sms.account    = octstr_null_create((char *)sqlite3_column_text(res, 9));
            /* msg->sms.id      = atol_null((char *)sqlite3_column_text(res, 10)); */
            msg->sms.sms_type   = atol_null((char *)sqlite3_column_text(res, 11));
            msg->sms.mclass     = atol_null((char *)sqlite3_column_text(res, 12));
            msg->sms.mwi        = atol_null((char *)sqlite3_column_text(res, 13));
            msg->sms.coding     = atol_null((char *)sqlite3_column_text(res, 14));
            msg->sms.compress   = atol_null((char *)sqlite3_column_text(res, 15));
            msg->sms.validity   = atol_null((char *)sqlite3_column_text(res, 16));
            msg->sms.deferred   = atol_null((char *)sqlite3_column_text(res, 17));
            msg->sms.dlr_mask   = atol_null((char *)sqlite3_column_text(res, 18));
            msg->sms.dlr_url    = octstr_null_create((char *)sqlite3_column_text(res, 19));
            msg->sms.pid        = atol_null((char *)sqlite3_column_text(res, 20));
            msg->sms.alt_dcs    = atol_null((char *)sqlite3_column_text(res, 21));
            msg->sms.rpi        = atol_null((char *)sqlite3_column_text(res, 22));
            msg->sms.charset    = octstr_null_create((char *)sqlite3_column_text(res, 23));
            msg->sms.binfo      = octstr_null_create((char *)sqlite3_column_text(res, 25));
            msg->sms.meta_data  = octstr_null_create((char *)sqlite3_column_text(res, 26));
            msg->sms.boxc_id    = (sqlite3_column_text(res, 24) == NULL) ? octstr_duplicate(sqlbox_id):octstr_null_create((char *)sqlite3_column_text(res, 24));
        }
    } while (state==SQLITE_ROW);
    sqlite3_finalize(res);

    if ( rows > 0) {
        /* delete current row */
        delet = octstr_format(SQLBOX_SQLITE3_DELETE_QUERY, sqlbox_insert_table, id);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(delet));
#endif
        sql_update(pc, delet);
        octstr_destroy(id);
        octstr_destroy(delet);
    }

    octstr_destroy(sql);
    dbpool_conn_produce(pc);
    return msg;
}

static Octstr *get_numeric_value_or_return_null(long int num)
{
    if (num == -1) {
        return octstr_create("NULL");
    }
    return octstr_format("%ld", num);
}

static Octstr *get_string_value_or_return_null(Octstr *str)
{
    if (str == NULL) {
        return octstr_create("NULL");
    }
    if (octstr_compare(str, octstr_imm("")) == 0) {
        return octstr_create("NULL");
    }
    octstr_replace(str, octstr_imm("\\"), octstr_imm("\\\\"));
    octstr_replace(str, octstr_imm("\'"), octstr_imm("\\\'"));
    return octstr_format("\'%S\'", str);
}

#define st_num(x) (stuffer[stuffcount++] = get_numeric_value_or_return_null(x))
#define st_str(x) (stuffer[stuffcount++] = get_string_value_or_return_null(x))

void sqlite3_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */)
{
    Octstr *sql;
    Octstr *stuffer[30];
    int stuffcount = 0;
    DBPoolConn *pc;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SQLITE3: Database pool got no connection! DB update failed!");
        return;
    }

    sql = octstr_format(SQLBOX_SQLITE3_INSERT_QUERY, sqlbox_logtable, st_str(momt), st_str(msg->sms.sender),
        st_str(msg->sms.receiver), st_str(msg->sms.udhdata), st_str(msg->sms.msgdata), st_num(msg->sms.time),
        st_str(msg->sms.smsc_id), st_str(msg->sms.service), st_str(msg->sms.account), st_num(msg->sms.sms_type),
        st_num(msg->sms.mclass), st_num(msg->sms.mwi), st_num(msg->sms.coding), st_num(msg->sms.compress),
        st_num(msg->sms.validity), st_num(msg->sms.deferred), st_num(msg->sms.dlr_mask), st_str(msg->sms.dlr_url),
        st_num(msg->sms.pid), st_num(msg->sms.alt_dcs), st_num(msg->sms.rpi), st_str(msg->sms.charset),
        st_str(msg->sms.boxc_id), st_str(msg->sms.binfo), st_str(msg->sms.meta_data));
    sql_update(pc, sql);
    while (stuffcount > 0) {
        octstr_destroy(stuffer[--stuffcount]);
    }
    octstr_destroy(sql);
    dbpool_conn_produce(pc);
}

void sqlite3_leave()
{
    dbpool_destroy(pool);
}

struct server_type *sqlbox_init_sqlite3(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *sqlite3_db, *sqlite3_id;
    Octstr *p = NULL;
    long pool_size, lock_timeout;
    DBConf *db_conf = NULL;
    struct server_type *res = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used Sqlite3 table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: Sqlite3: group 'sqlbox' is not specified!");

    if (!(sqlite3_id = cfg_get(grp, octstr_imm("id"))))
           panic(0, "SQLBOX: Sqlite3: directive 'id' is not specified!");

    /*
     * now grap the required information from the 'sqlite3-connection' group
     * with the sqlite3-id we just obtained
     *
     * we have to loop through all available Sqlite3 connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("sqlite3-connection"));
     while (grplist && (grp = (CfgGroup *)gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, sqlite3_id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
     }
     panic(0, "SQLBOX: Sqlite3: connection settings for id '%s' are not specified!",
           octstr_get_cstr(sqlite3_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(sqlite3_db = cfg_get(grp, octstr_imm("database"))))
           panic(0, "SQLBOX: Sqlite3: directive 'database' is not specified!");

    if (cfg_get_integer(&lock_timeout, grp, octstr_imm("lock-timeout")) == -1 || lock_timeout == 0 )
           lock_timeout = 0;
    /*
     * ok, ready to connect to Sqlite3
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->sqlite3 = gw_malloc(sizeof(SQLite3Conf));
    gw_assert(db_conf->sqlite3 != NULL);

    db_conf->sqlite3->file = sqlite3_db;
    db_conf->sqlite3->lock_timeout = lock_timeout;

    pool = dbpool_create(DBPOOL_SQLITE3, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"SQLBOX: Sqlite3: database pool has no connections!");

    octstr_destroy(sqlite3_id);

    res = gw_malloc(sizeof(struct server_type));
    gw_assert(res != NULL);

    res->type = octstr_create("Sqlite3");
    res->sql_enter = sqlbox_configure_sqlite3;
    res->sql_leave = sqlite3_leave;
    res->sql_fetch_msg = sqlite3_fetch_msg;
    res->sql_save_msg = sqlite3_save_msg;
    return res;
}
#endif
