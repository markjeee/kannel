#include "gwlib/gwlib.h"
#ifdef HAVE_SDB
#include "gwlib/dbpool.h"
#include <sdb.h>
#define sqlbox_sdb_c
#include "sqlbox_sdb.h"

#define sql_update sdb_update
#define sql_select sdb_select

static Octstr *sqlbox_logtable;
static Octstr *sqlbox_insert_table;


/*
 * Our connection pool to sdb.
 */

static DBPool *pool = NULL;

static struct sqlbox_db_fields *fields = NULL;

static struct sqlbox_db_queries *queries = NULL;

enum {
    SDB_MYSQL,
    SDB_ORACLE,
    SDB_POSTGRES,
    SDB_SQLITE,
    SDB_SQLITE3,
    SDB_OTHER
};

static long sdb_conn_type = SDB_OTHER;

/*
 *-------------------------------------------------
 * sdb thingies
 *-------------------------------------------------
*/

#define octstr_null_create(x) ((x != NULL) ? octstr_create(x) : octstr_create(""))
#define atol_null(x) ((x != NULL) ? atol(x) : -1)
#define get_sdb_octstr_col(x) (octstr_create(octstr_get_cstr(gwlist_get(row,x))))
#define get_sdb_long_col(x) (atol(octstr_get_cstr(gwlist_get(row,x))))

static int sdb_update(DBPoolConn *conn, const Octstr *sql)
{
    int state;

#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    state = sdb_query(conn->conn, octstr_get_cstr(sql), NULL, NULL);
    if (state < 0) {
        error(0, "SDB: Error updating rows");
        return -1;
    }
    return state;
}

static int sdb_select(DBPoolConn *conn, const Octstr *sql,
        int (*callback)(int, char **, void *), void *closure)
{
    int state;

#if defined(SQLBOX_TRACE)
    debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    state = sdb_query(conn->conn, octstr_get_cstr(sql), callback, closure);
    if (state < 0) {
        error(0, "SDB: Error selecting rows");
        return -1;
    }
    return state;
}

void sdb_callback_addrow(int n, char **data, List **rows)
{
    int i;
    List *row = gwlist_create();
    for (i = 0; i < n; i++) {
        gwlist_insert(row, i, octstr_null_create(data[i]));
    }
    gwlist_append(*rows, row);
}

void sqlbox_configure_sdb(Cfg* cfg)
{
    CfgGroup *grp;
    Octstr *sql;
    DBPoolConn *pc;

    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: Sdb: group 'sqlbox' is not specified!");

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
        error(0, "SDB: Database pool got no connection! DB update failed!");
        return;
    }

    /* create send_sms && sent_sms tables if they do not exist */
    sqlbox_run_query(queries->create_log_table, sqlbox_logtable);
    sqlbox_run_query(queries->create_log_sequence, sqlbox_logtable);
    sqlbox_run_query(queries->create_log_trigger, sqlbox_logtable);
    sqlbox_run_query(queries->create_insert_table, sqlbox_insert_table);
    sqlbox_run_query(queries->create_insert_sequence, sqlbox_insert_table);
    sqlbox_run_query(queries->create_insert_trigger, sqlbox_insert_table);
    /* end table creation */

    dbpool_conn_produce(pc);
}

Msg *sdb_fetch_msg()
{
    Msg *msg = NULL;
    Octstr *sql, *delet, *id;
    List *res, *row;
    DBPoolConn *pc;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SDB: Database pool got no connection! DB update failed!");
        return NULL;
    }
    res = gwlist_create();
    gw_assert(res != NULL);
    sql = octstr_format(queries->select_query, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if (sql_select(pc, sql, (void *)sdb_callback_addrow, &res) < 0) {
        debug("sqlbox", 0, "SQL statement failed: %s", octstr_get_cstr(sql));
    } else {
        if (gwlist_len(res) > 0) {
            row = gwlist_extract_first(res);
            gw_assert(row != NULL);
            id = get_sdb_octstr_col(0);
            /* save fields in this row as msg struct */
            msg = msg_create(sms);
            msg->sms.sender     = get_sdb_octstr_col(2);
            msg->sms.receiver   = get_sdb_octstr_col(3);
            msg->sms.udhdata    = get_sdb_octstr_col(4);
            msg->sms.msgdata    = get_sdb_octstr_col(5);
            msg->sms.time       = get_sdb_long_col(6);
            msg->sms.smsc_id    = get_sdb_octstr_col(7);
            msg->sms.service    = get_sdb_octstr_col(8);
            msg->sms.account    = get_sdb_octstr_col(9);
            /* msg->sms.id      = get_sdb_long_col(10); */
            msg->sms.sms_type   = get_sdb_long_col(11);
            msg->sms.mclass     = get_sdb_long_col(12);
            msg->sms.mwi        = get_sdb_long_col(13);
            msg->sms.coding     = get_sdb_long_col(14);
            msg->sms.compress   = get_sdb_long_col(15);
            msg->sms.validity   = get_sdb_long_col(16);
            msg->sms.deferred   = get_sdb_long_col(17);
            msg->sms.dlr_mask   = get_sdb_long_col(18);
            msg->sms.dlr_url    = get_sdb_octstr_col(19);
            msg->sms.pid        = get_sdb_long_col(20);
            msg->sms.alt_dcs    = get_sdb_long_col(21);
            msg->sms.rpi        = get_sdb_long_col(22);
            msg->sms.charset    = get_sdb_octstr_col(23);
            msg->sms.binfo      = get_sdb_octstr_col(25);
            msg->sms.meta_data  = get_sdb_octstr_col(26);
            if (gwlist_get(row,24) == NULL) {
                msg->sms.boxc_id= octstr_duplicate(sqlbox_id);
            }
            else {
                msg->sms.boxc_id= get_sdb_octstr_col(24);
            }
            /* delete current row */
            delet = octstr_format(queries->delete_query, sqlbox_insert_table, id);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(delet));
#endif
            sql_update(pc, delet);
            octstr_destroy(id);
            octstr_destroy(delet);
            gwlist_destroy(row, octstr_destroy_item);
        }
    }
    gwlist_destroy(res, NULL);
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

void sdb_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */)
{
    Octstr *sql;
    Octstr *stuffer[30];
    int stuffcount = 0;
    DBPoolConn *pc;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "SDB: Database pool got no connection! DB update failed!");
        return;
    }

    sql = octstr_format(queries->insert_query, sqlbox_logtable, st_str(momt), st_str(msg->sms.sender),
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

void sdb_leave()
{
    dbpool_destroy(pool);
}

struct server_type *sqlbox_init_sdb(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *sdb_url, *sdb_id;
    Octstr *p = NULL;
    long pool_size;
    DBConf *db_conf = NULL;
    struct server_type *res = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used Sdb table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: Sdb: group 'sqlbox' is not specified!");

    if (!(sdb_id = cfg_get(grp, octstr_imm("id"))))
           panic(0, "SQLBOX: Sdb: directive 'id' is not specified!");

    /*
     * now grap the required information from the 'sdb-connection' group
     * with the sdb-id we just obtained
     *
     * we have to loop through all available Sdb connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("sdb-connection"));
     while (grplist && (grp = (CfgGroup *)gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, sdb_id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
     }
     panic(0, "SQLBOX: Sdb: connection settings for id '%s' are not specified!",
           octstr_get_cstr(sdb_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(sdb_url = cfg_get(grp, octstr_imm("url"))))
            panic(0, "SQLBOX: SDB: directive 'url' is not specified!");

    queries = gw_malloc(sizeof(struct sqlbox_db_queries));
    gw_assert(queries != NULL);

    if (octstr_search(sdb_url, octstr_imm("mysql:"), 0) == 0) {
        warning(0, "SQLBOX[sdb]: Please use native MySQL support, instead of libsdb.");
        sdb_conn_type = SDB_MYSQL;
        queries->create_insert_table = SQLBOX_MYSQL_CREATE_INSERT_TABLE;
        queries->create_insert_sequence = NULL;
        queries->create_insert_trigger = NULL;
        queries->create_log_table = SQLBOX_MYSQL_CREATE_LOG_TABLE;
        queries->create_log_sequence = NULL;
        queries->create_log_trigger = NULL;
        queries->select_query = SQLBOX_MYSQL_SELECT_QUERY;
        queries->delete_query = SQLBOX_MYSQL_DELETE_QUERY;
        queries->insert_query = SQLBOX_MYSQL_INSERT_QUERY;
     }
     else if (octstr_search(sdb_url, octstr_imm("oracle:"), 0) == 0) {
        sdb_conn_type = SDB_ORACLE;
        queries->create_insert_table = SQLBOX_ORACLE_CREATE_INSERT_TABLE;
        queries->create_insert_sequence = SQLBOX_ORACLE_CREATE_INSERT_SEQUENCE;
        queries->create_insert_trigger = SQLBOX_ORACLE_CREATE_INSERT_TRIGGER;
        queries->create_log_table = SQLBOX_ORACLE_CREATE_LOG_TABLE;
        queries->create_log_sequence = SQLBOX_ORACLE_CREATE_LOG_SEQUENCE;
        queries->create_log_trigger = SQLBOX_ORACLE_CREATE_LOG_TRIGGER;
        queries->select_query = SQLBOX_ORACLE_SELECT_QUERY;
        queries->delete_query = SQLBOX_ORACLE_DELETE_QUERY;
            queries->insert_query = SQLBOX_ORACLE_INSERT_QUERY;
     }
     else if (octstr_search(sdb_url, octstr_imm("postgres:"), 0) == 0) {
        sdb_conn_type = SDB_POSTGRES;
        queries->create_insert_table = SQLBOX_PGSQL_CREATE_INSERT_TABLE;
        queries->create_insert_sequence = NULL;
        queries->create_insert_trigger = NULL;
        queries->create_log_table = SQLBOX_PGSQL_CREATE_LOG_TABLE;
        queries->create_log_sequence = NULL;
        queries->create_log_trigger = NULL;
        queries->select_query = SQLBOX_PGSQL_SELECT_QUERY;
        queries->delete_query = SQLBOX_PGSQL_DELETE_QUERY;
         queries->insert_query = SQLBOX_PGSQL_INSERT_QUERY;
     }
     else if (octstr_search(sdb_url, octstr_imm("sqlite:"), 0) == 0) {
        sdb_conn_type = SDB_SQLITE;
        queries->create_insert_table = SQLBOX_SQLITE_CREATE_INSERT_TABLE;
        queries->create_insert_sequence = NULL;
        queries->create_insert_trigger = NULL;
        queries->create_log_table = SQLBOX_SQLITE_CREATE_LOG_TABLE;
        queries->create_log_sequence = NULL;
        queries->create_log_trigger = NULL;
        queries->select_query = SQLBOX_SQLITE_SELECT_QUERY;
        queries->delete_query = SQLBOX_SQLITE_DELETE_QUERY;
        queries->insert_query = SQLBOX_SQLITE_INSERT_QUERY;
     }
     else if (octstr_search(sdb_url, octstr_imm("sqlite3:"), 0) == 0) {
        sdb_conn_type = SDB_SQLITE3;
        queries->create_insert_table = SQLBOX_SQLITE3_CREATE_INSERT_TABLE;
        queries->create_insert_sequence = NULL;
        queries->create_insert_trigger = NULL;
        queries->create_log_table = SQLBOX_SQLITE3_CREATE_LOG_TABLE;
        queries->create_log_sequence = NULL;
        queries->create_log_trigger = NULL;
        queries->select_query = SQLBOX_SQLITE3_SELECT_QUERY;
        queries->delete_query = SQLBOX_SQLITE3_DELETE_QUERY;
        queries->insert_query = SQLBOX_SQLITE3_INSERT_QUERY;
     }
     else {
        sdb_conn_type = SDB_OTHER;
        queries->create_insert_table = NULL;
        queries->create_insert_sequence = NULL;
        queries->create_insert_trigger = NULL;
        queries->create_log_table = NULL;
        queries->create_log_sequence = NULL;
        queries->create_log_trigger = NULL;
        queries->select_query = SQLBOX_OTHER_SELECT_QUERY;
        queries->delete_query = SQLBOX_OTHER_DELETE_QUERY;
        queries->insert_query = SQLBOX_OTHER_INSERT_QUERY;
     }
    /*
     * ok, ready to connect to Sdb
     */
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
        panic(0,"SQLBOX: Sdb: database pool has no connections!");

    octstr_destroy(sdb_id);

    res = gw_malloc(sizeof(struct server_type));
    gw_assert(res != NULL);

    res->type = octstr_create("Sdb");
    res->sql_enter = sqlbox_configure_sdb;
    res->sql_leave = sdb_leave;
    res->sql_fetch_msg = sdb_fetch_msg;
    res->sql_save_msg = sdb_save_msg;
    return res;
}
#endif
