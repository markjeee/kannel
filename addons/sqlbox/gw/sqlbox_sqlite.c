#include "gwlib/gwlib.h"
#ifdef HAVE_SQLITE
#include "gwlib/dbpool.h"
#include <sqlite.h>
#define sqlbox_sqlite_c
#include "sqlbox_sqlite.h"

#define sql_update sqlite_update
#define sql_select sqlite_select

static Octstr *sqlbox_logtable;
static Octstr *sqlbox_insert_table;

/*
 * Our connection pool to sqlite.
 */

static DBPool *pool = NULL;

/*
 *-------------------------------------------------
 * sqlite thingies
 *-------------------------------------------------
*/

#define octstr_null_create(x) ((x != NULL) ? octstr_create(x) : octstr_create(""))
#define atol_null(x) ((x != NULL) ? atol(x) : -1)

static int sqlite_update(DBPoolConn *conn, const Octstr *sql)
{
    int state;
    char *errmsg = 0;
    //struct DBPoolConn *conn = (struct DBPoolConn*) theconn;

#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    state = sqlite_exec(conn->conn, octstr_get_cstr(sql), NULL, 0, &errmsg);
    if (state != SQLITE_OK) {
        error(0, "SQLITE: %s", errmsg);
        return -1;
    }
    return sqlite_changes(conn->conn);
}

sqlite_vm* sqlite_select(DBPoolConn *conn, const Octstr *sql)
{
    int res;
    char *errmsg = 0;
    const char *query_tail = NULL;
    sqlite_vm *vm = NULL;
    //struct DBPoolConn *conn = (struct DBPoolConn*) theconn;

#if defined(SQLBOX_TRACE)
    debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    res = sqlite_compile(conn->conn, octstr_get_cstr(sql), &query_tail, &vm, &errmsg);
    if (res != SQLITE_OK) {
        error(0, "SQLITE: Could not compile query: %s", errmsg);
        return NULL;
    }
    return vm;
}

void sqlbox_configure_sqlite(Cfg* cfg)
{
    CfgGroup *grp;
    Octstr *sql;
    DBPoolConn *pc;

    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: Sqlite: group 'sqlbox' is not specified!");

    sqlbox_logtable = cfg_get(grp, octstr_imm("sql-log-table"));
    if (sqlbox_logtable == NULL) {
        panic(0, "Parameter 'sql-log-table' not configured.");
    }
    sqlbox_insert_table = cfg_get(grp, octstr_imm("sql-insert-table"));
    if (sqlbox_insert_table == NULL) {
        panic(0, "Parameter 'sql-insert-table' not configured.");
    }

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SQLITE: Database pool got no connection! DB update failed!");
        return;
    }

    /* create send_sms && sent_sms tables if they do not exist */
    sql = octstr_format(SQLBOX_SQLITE_CREATE_LOG_TABLE, sqlbox_logtable);
    sql_update(pc, sql);
    octstr_destroy(sql);
    sql = octstr_format(SQLBOX_SQLITE_CREATE_INSERT_TABLE, sqlbox_insert_table);
    sql_update(pc, sql);
    octstr_destroy(sql);
    /* end table creation */
    dbpool_conn_produce(pc);
}

Msg *sqlite_fetch_msg()
{
    int state;
    DBPoolConn *pc;
    char *errmsg = 0;
    const char *query_tail = NULL;
    sqlite_vm *res = NULL;
    int i, cols = 0, rows = 0;
    const char **row = NULL;
    const char **col_name = NULL;

    Msg *msg = NULL;
    Octstr *sql, *delet, *id;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SQLITE: Database pool got no connection! DB update failed!");
        return NULL;
    }

    sql = octstr_format(SQLBOX_SQLITE_SELECT_QUERY, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    res = sql_select(pc, sql);
    do {
        state=sqlite_step(res, &cols, &row, &col_name);
        if (state==SQLITE_ROW){
            rows++;
            id = octstr_null_create(row[0]);
            /* save fields in this row as msg struct */
            msg = msg_create(sms);
            msg->sms.sender     = octstr_null_create(row[2]);
            msg->sms.receiver   = octstr_null_create(row[3]);
            msg->sms.udhdata    = octstr_null_create(row[4]);
            msg->sms.msgdata    = octstr_null_create(row[5]);
            msg->sms.time       = atol_null(row[6]);
            msg->sms.smsc_id    = octstr_null_create(row[7]);
            msg->sms.service    = octstr_null_create(row[8]);
            msg->sms.account    = octstr_null_create(row[9]);
            /* msg->sms.id      = atol_null(row[10]); */
            msg->sms.sms_type   = atol_null(row[11]);
            msg->sms.mclass     = atol_null(row[12]);
            msg->sms.mwi        = atol_null(row[13]);
            msg->sms.coding     = atol_null(row[14]);
            msg->sms.compress   = atol_null(row[15]);
            msg->sms.validity   = atol_null(row[16]);
            msg->sms.deferred   = atol_null(row[17]);
            msg->sms.dlr_mask   = atol_null(row[18]);
            msg->sms.dlr_url    = octstr_null_create(row[19]);
            msg->sms.pid        = atol_null(row[20]);
            msg->sms.alt_dcs    = atol_null(row[21]);
            msg->sms.rpi        = atol_null(row[22]);
            msg->sms.charset    = octstr_null_create(row[23]);
            msg->sms.binfo      = octstr_null_create(row[25]);
            msg->sms.meta_data  = octstr_null_create(row[26]);
            if (row[24] == NULL) {
                msg->sms.boxc_id= octstr_duplicate(sqlbox_id);
            } else {
                msg->sms.boxc_id= octstr_null_create(row[24]);
            }
        }
    } while (state==SQLITE_ROW);
    sqlite_finalize(res, NULL);

    if ( rows > 0) {
        /* delete current row */
        delet = octstr_format(SQLBOX_SQLITE_DELETE_QUERY, sqlbox_insert_table, id);
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

void sqlite_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */)
{
    Octstr *sql;
    Octstr *stuffer[30];
    int stuffcount = 0;
    DBPoolConn *pc;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SQLITE: Database pool got no connection! DB update failed!");
        return;
    }

    sql = octstr_format(SQLBOX_SQLITE_INSERT_QUERY, sqlbox_logtable, st_str(momt), st_str(msg->sms.sender),
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

void sqlite_leave()
{
    dbpool_destroy(pool);
}

struct server_type *sqlbox_init_sqlite(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *sqlite_db, *sqlite_id;
    Octstr *p = NULL;
    long pool_size;
    int lock_timeout;
    DBConf *db_conf = NULL;
    struct server_type *res = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used Sqlite table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: Sqlite: group 'sqlbox' is not specified!");

    if (!(sqlite_id = cfg_get(grp, octstr_imm("id"))))
           panic(0, "SQLBOX: Sqlite: directive 'id' is not specified!");

    /*
     * now grap the required information from the 'sqlite-connection' group
     * with the sqlite-id we just obtained
     *
     * we have to loop through all available Sqlite connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("sqlite-connection"));
     while (grplist && (grp = (CfgGroup *)gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, sqlite_id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
     }
     panic(0, "SQLBOX: Sqlite: connection settings for id '%s' are not specified!",
           octstr_get_cstr(sqlite_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(sqlite_db = cfg_get(grp, octstr_imm("database"))))
           panic(0, "SQLBOX: Sqlite: directive 'database' is not specified!");

    if (cfg_get_integer(&lock_timeout, grp, octstr_imm("lock-timeout")) == -1 || lock_timeout == 0 )
           lock_timeout = 0;
    /*
     * ok, ready to connect to Sqlite
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->sqlite = gw_malloc(sizeof(SQLiteConf));
    gw_assert(db_conf->sqlite != NULL);

    db_conf->sqlite->file = sqlite_db;
    db_conf->sqlite->lock_timeout = lock_timeout;

    pool = dbpool_create(DBPOOL_SQLITE, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"SQLBOX: Sqlite: database pool has no connections!");

    octstr_destroy(sqlite_id);

    res = gw_malloc(sizeof(struct server_type));
    gw_assert(res != NULL);

    res->type = octstr_create("Sqlite");
    res->sql_enter = sqlbox_configure_sqlite;
    res->sql_leave = sqlite_leave;
    res->sql_fetch_msg = sqlite_fetch_msg;
    res->sql_save_msg = sqlite_save_msg;
    return res;
}
#endif
